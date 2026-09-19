#include "stratum_client.hpp"
#include "console_ui.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#error This source is Windows-only.
#endif

extern "C" {
#include "jansson.h"
}

#include <algorithm>
#include <chrono>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace phasec {
namespace {

struct SocketGuard {
    SOCKET s = INVALID_SOCKET;
    ~SocketGuard() { if (s != INVALID_SOCKET) closesocket(s); }
};

struct JsonDeleter { void operator()(json_t* p) const { if (p) json_decref(p); } };
using JsonPtr = std::unique_ptr<json_t, JsonDeleter>;

std::string json_dump_compact(json_t* value) {
    char* p = json_dumps(value, JSON_COMPACT);
    if (!p) throw std::runtime_error("json_dumps failed");
    std::string out(p);
    free(p);
    return out;
}

bool send_all(SOCKET s, const char* data, std::size_t n) {
    while (n) {
        const int chunk = send(s, data, static_cast<int>(std::min<std::size_t>(n, INT_MAX)), 0);
        if (chunk <= 0) return false;
        data += chunk;
        n -= static_cast<std::size_t>(chunk);
    }
    return true;
}

bool send_line(SOCKET s, const std::string& line) {
    std::string with_nl = line;
    with_nl.push_back('\n');
    return send_all(s, with_nl.data(), with_nl.size());
}

SOCKET connect_exact(const Endpoint& ep) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* result = nullptr;
    const std::string port = std::to_string(ep.port);
    const int rc = getaddrinfo(ep.host.c_str(), port.c_str(), &hints, &result);
    if (rc != 0) throw std::runtime_error("DNS/getaddrinfo failed: " + std::to_string(rc));
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> holder(result, freeaddrinfo);

    for (addrinfo* ai = result; ai; ai = ai->ai_next) {
        SOCKET s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) return s;
        closesocket(s);
    }
    throw std::runtime_error("could not connect to user-supplied pool endpoint");
}

std::array<std::uint8_t, 4> parse_hex4(json_t* value, const char* name) {
    const char* s = json_string_value(value);
    if (!s || std::strlen(s) != 8) throw std::runtime_error(std::string("invalid ") + name);
    auto v = hex_decode(s);
    std::array<std::uint8_t, 4> out{};
    std::copy(v.begin(), v.end(), out.begin());
    return out;
}

JobTemplate parse_notify(json_t* params) {
    if (!json_is_array(params) || json_array_size(params) < 9) throw std::runtime_error("invalid mining.notify params");
    JobTemplate j;
    const char* job_id = json_string_value(json_array_get(params, 0));
    const char* prevhash = json_string_value(json_array_get(params, 1));
    const char* coinb1 = json_string_value(json_array_get(params, 2));
    const char* coinb2 = json_string_value(json_array_get(params, 3));
    json_t* merkle = json_array_get(params, 4);
    if (!job_id || !prevhash || !coinb1 || !coinb2 || !json_is_array(merkle))
        throw std::runtime_error("invalid mining.notify string/merkle field");
    j.job_id = job_id;
    j.prevhash = hex_decode(prevhash);
    if (j.prevhash.size() != 32) throw std::runtime_error("notify prevhash must be 32 bytes");
    j.coinb1 = hex_decode(coinb1);
    j.coinb2 = hex_decode(coinb2);
    for (std::size_t i = 0; i < json_array_size(merkle); ++i) {
        const char* branch = json_string_value(json_array_get(merkle, i));
        if (!branch) throw std::runtime_error("invalid merkle branch");
        auto bytes = hex_decode(branch);
        if (bytes.size() != 32) throw std::runtime_error("merkle branch must be 32 bytes");
        std::array<std::uint8_t, 32> a{};
        std::copy(bytes.begin(), bytes.end(), a.begin());
        j.merkle.push_back(a);
    }
    j.version = parse_hex4(json_array_get(params, 5), "version");
    j.nbits = parse_hex4(json_array_get(params, 6), "nbits");
    j.ntime = parse_hex4(json_array_get(params, 7), "ntime");
    j.clean = json_is_true(json_array_get(params, 8));
    return j;
}

std::string le_word_hex(std::uint32_t word) {
    std::uint8_t b[4] = {
        static_cast<std::uint8_t>(word),
        static_cast<std::uint8_t>(word >> 8),
        static_cast<std::uint8_t>(word >> 16),
        static_cast<std::uint8_t>(word >> 24),
    };
    return hex_encode(b, 4);
}

std::string make_subscribe() {
    JsonPtr root(json_object());
    json_object_set_new(root.get(), "id", json_integer(1));
    json_object_set_new(root.get(), "method", json_string("mining.subscribe"));
    JsonPtr params(json_array());
    json_array_append_new(params.get(), json_string("mona-miner/1.0"));
    json_object_set_new(root.get(), "params", params.release());
    return json_dump_compact(root.get());
}

std::string make_authorize(const std::string& user, const std::string& pass) {
    JsonPtr root(json_object());
    json_object_set_new(root.get(), "id", json_integer(2));
    json_object_set_new(root.get(), "method", json_string("mining.authorize"));
    JsonPtr params(json_array());
    json_array_append_new(params.get(), json_string(user.c_str()));
    json_array_append_new(params.get(), json_string(pass.c_str()));
    json_object_set_new(root.get(), "params", params.release());
    return json_dump_compact(root.get());
}

std::string make_submit(const std::string& user, const Candidate& c, std::uint64_t id) {
    JsonPtr root(json_object());
    json_object_set_new(root.get(), "id", json_integer(static_cast<json_int_t>(id)));
    json_object_set_new(root.get(), "method", json_string("mining.submit"));
    JsonPtr params(json_array());
    json_array_append_new(params.get(), json_string(user.c_str()));
    json_array_append_new(params.get(), json_string(c.work.job_id.c_str()));
    const std::string xn2 = hex_encode(c.work.xnonce2);
    const std::string nt = le_word_hex(c.work.words[17]);
    const std::string nn = nonce_hex(c.nonce);
    json_array_append_new(params.get(), json_string(xn2.c_str()));
    json_array_append_new(params.get(), json_string(nt.c_str()));
    json_array_append_new(params.get(), json_string(nn.c_str()));
    json_object_set_new(root.get(), "params", params.release());
    return json_dump_compact(root.get());
}

bool contains_stale(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s.find("stale") != std::string::npos || s.find("job not found") != std::string::npos;
}

struct Protocol {
    SharedState& state;
    Counters& counters;
    std::atomic<bool>& stop_flag;
    std::uint64_t shares_limit;
    SOCKET socket;
    bool subscribed = false;
    bool authorized = false;
    bool reconnect_requested = false;
    std::map<std::uint64_t, Candidate> in_flight;

    bool handle(json_t* root) {
        json_t* method_value = json_object_get(root, "method");
        if (json_is_string(method_value)) {
            const std::string method = json_string_value(method_value);
            json_t* params = json_object_get(root, "params");
            if (method == "mining.notify") {
                state.set_job(parse_notify(params));
                if (ui::all_shares_enabled()) ui::log(stdout, "Stratum new job %s", state.current_job_id().c_str());
                return true;
            }
            if (method == "mining.set_difficulty") {
                if (!json_is_array(params) || json_array_size(params) < 1) throw std::runtime_error("invalid set_difficulty");
                const double diff = json_number_value(json_array_get(params, 0));
                state.set_next_difficulty(diff);
                if (ui::all_shares_enabled()) ui::log(stdout, "Stratum difficulty set to %.12g", diff);
                return true;
            }
            if (method == "mining.set_extranonce") {
                if (!json_is_array(params) || json_array_size(params) < 2) throw std::runtime_error("invalid set_extranonce");
                const char* x1 = json_string_value(json_array_get(params, 0));
                const json_int_t n2 = json_integer_value(json_array_get(params, 1));
                if (!x1 || n2 < 2 || n2 > 16) throw std::runtime_error("invalid set_extranonce values");
                state.set_extranonce(hex_decode(x1), static_cast<std::size_t>(n2));
                ui::log(stdout, "Stratum extranonce changed; pending work invalidated");
                return true;
            }
            if (method == "client.reconnect") {
                // Fail-closed against pool-directed endpoint changes. Reconnect only to the CLI endpoint.
                ui::log(stdout, "Stratum client.reconnect requested; ignoring supplied redirect and reconnecting pinned endpoint");
                reconnect_requested = true;
                return true;
            }
            if (method == "mining.ping") {
                JsonPtr reply(json_object());
                json_t* id = json_object_get(root, "id");
                if (id) json_object_set(reply.get(), "id", id); else json_object_set_new(reply.get(), "id", json_null());
                json_object_set_new(reply.get(), "result", json_string("pong"));
                json_object_set_new(reply.get(), "error", json_null());
                return send_line(socket, json_dump_compact(reply.get()));
            }
            // No telemetry method and no optional endpoint-discovery method is implemented.
            return true;
        }

        json_t* idv = json_object_get(root, "id");
        if (!json_is_integer(idv)) return true;
        const std::uint64_t id = static_cast<std::uint64_t>(json_integer_value(idv));
        json_t* result = json_object_get(root, "result");
        json_t* error = json_object_get(root, "error");

        if (id == 1) {
            if (!json_is_array(result) || json_array_size(result) < 3) throw std::runtime_error("subscribe rejected or malformed");
            const char* x1 = json_string_value(json_array_get(result, 1));
            const json_int_t n2 = json_integer_value(json_array_get(result, 2));
            if (!x1 || n2 < 2 || n2 > 16) throw std::runtime_error("subscribe extranonce malformed");
            state.set_subscription(hex_decode(x1), static_cast<std::size_t>(n2));
            subscribed = true;
            return true;
        }
        if (id == 2) {
            if (!json_is_true(result) || (error && !json_is_null(error))) throw std::runtime_error("authorize rejected");
            authorized = true;
            state.set_authorized(true);
            ui::log(stdout, "Stratum authorized");
            return true;
        }

        const auto it = in_flight.find(id);
        if (it != in_flight.end()) {
            if (json_is_true(result) && (!error || json_is_null(error))) {
                ++counters.accepted;
                ui::set_share_totals(counters.accepted.load(), counters.rejected.load());
                if (ui::all_shares_enabled()) {
                    ui::share_result_line(true,
                                          counters.accepted.load(),
                                          counters.rejected.load(),
                                          ui::share_difficulty(it->second));
                }
                if (shares_limit > 0 && counters.accepted.load() >= shares_limit) {
                    ui::log(stdout, "accepted-share limit reached; stopping cleanly");
                    stop_flag.store(true);
                    state.stop();
                }
            } else {
                ++counters.rejected;
                std::string reason;
                if (json_is_array(error) && json_array_size(error) > 1 && json_is_string(json_array_get(error, 1)))
                    reason = json_string_value(json_array_get(error, 1));
                else if (json_is_string(error)) reason = json_string_value(error);
                if (contains_stale(reason)) ++counters.stale_pool;
                ui::set_share_totals(counters.accepted.load(), counters.rejected.load());
                if (ui::all_shares_enabled()) {
                    ui::share_result_line(false,
                                          counters.accepted.load(),
                                          counters.rejected.load(),
                                          ui::share_difficulty(it->second),
                                          reason.c_str());
                } else {
                    ui::rejected_line(counters.accepted.load(), counters.rejected.load(), reason.c_str());
                }
            }
            in_flight.erase(it);
        }
        return true;
    }
};

bool recv_into_lines(SOCKET s, std::string& buffer, std::vector<std::string>& lines) {
    char temp[8192];
    const int n = recv(s, temp, sizeof(temp), 0);
    if (n <= 0) return false;
    buffer.append(temp, temp + n);
    for (;;) {
        const auto pos = buffer.find('\n');
        if (pos == std::string::npos) break;
        std::string line = buffer.substr(0, pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        buffer.erase(0, pos + 1);
        if (!line.empty()) lines.push_back(std::move(line));
    }
    if (buffer.size() > (1u << 20)) throw std::runtime_error("stratum line exceeded 1 MiB");
    return true;
}

void process_line(Protocol& proto, const std::string& line) {
    json_error_t err{};
    JsonPtr root(json_loads(line.c_str(), 0, &err));
    if (!root || !json_is_object(root.get())) throw std::runtime_error(std::string("invalid stratum JSON: ") + err.text);
    if (!proto.handle(root.get())) throw std::runtime_error("stratum method response send failed");
}

void wait_for_handshake(Protocol& proto, const StratumOptions& options, std::atomic<bool>& stop_flag) {
    if (!send_line(proto.socket, make_subscribe())) throw std::runtime_error("subscribe send failed");
    std::string buffer;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    bool authorize_sent = false;
    while (!stop_flag.load() && std::chrono::steady_clock::now() < deadline) {
        fd_set readfds;
        FD_ZERO(&readfds); FD_SET(proto.socket, &readfds);
        timeval tv{0, 250000};
        const int rc = select(0, &readfds, nullptr, nullptr, &tv);
        if (rc == SOCKET_ERROR) throw std::runtime_error("select failed during handshake");
        if (rc > 0 && FD_ISSET(proto.socket, &readfds)) {
            std::vector<std::string> lines;
            if (!recv_into_lines(proto.socket, buffer, lines)) throw std::runtime_error("connection closed during handshake");
            for (const auto& line : lines) process_line(proto, line);
        }
        if (proto.subscribed && !authorize_sent) {
            if (!send_line(proto.socket, make_authorize(options.user, options.password)))
                throw std::runtime_error("authorize send failed");
            authorize_sent = true;
        }
        if (proto.authorized) return;
    }
    throw std::runtime_error("stratum handshake timed out");
}

void session_loop(SOCKET s, const StratumOptions& options, SharedState& state,
                  CandidateQueue& candidates, Counters& counters,
                  std::atomic<bool>& stop_flag) {
    Protocol proto{state, counters, stop_flag, options.shares_limit, s};
    wait_for_handshake(proto, options, stop_flag);
    std::string buffer;
    std::uint64_t next_submit_id = 1000;

    while (!stop_flag.load() && !proto.reconnect_requested) {
        // Final validity check is deliberately on the network-owner thread immediately before send.
        // When --shares-limit is set, also cap outstanding submissions so that even if every
        // in-flight share is accepted, the accepted total cannot overshoot the requested limit.
        // shares_limit == 0 keeps the normal unlimited/high-throughput path unchanged.
        for (int i = 0; i < 64; ++i) {
            if (options.shares_limit > 0) {
                const std::uint64_t accepted = counters.accepted.load();
                if (accepted >= options.shares_limit) break;
                const std::uint64_t remaining = options.shares_limit - accepted;
                if (proto.in_flight.size() >= remaining) break;
            }
            auto candidate = candidates.try_pop();
            if (!candidate) break;
            const std::uint64_t id = next_submit_id++;
            const std::string line = make_submit(options.user, *candidate, id);
            bool attempted_send = false;
            bool sent = false;
            const bool current = state.run_if_submit_valid(candidate->work, [&] {
                attempted_send = true;
                sent = send_line(s, line);
                return sent;
            });
            if (!current && !attempted_send) {
                ++counters.stale_local;
                continue;
            }
            if (!sent) {
                ++counters.send_fail;
                throw std::runtime_error("share submit send failed");
            }
            proto.in_flight.emplace(id, std::move(*candidate));
        }

        fd_set readfds;
        FD_ZERO(&readfds); FD_SET(s, &readfds);
        timeval tv{0, 100000};
        const int rc = select(0, &readfds, nullptr, nullptr, &tv);
        if (rc == SOCKET_ERROR) throw std::runtime_error("select failed");
        if (rc > 0 && FD_ISSET(s, &readfds)) {
            std::vector<std::string> lines;
            if (!recv_into_lines(s, buffer, lines)) throw std::runtime_error("pool disconnected");
            for (const auto& line : lines) process_line(proto, line);
        }
    }
}

} // namespace

void run_stratum(const StratumOptions& options, SharedState& state,
                 CandidateQueue& candidates, Counters& counters,
                 std::atomic<bool>& stop_flag) {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) throw std::runtime_error("WSAStartup failed");
    struct WsaCleanup { ~WsaCleanup() { WSACleanup(); } } cleanup;

    while (!stop_flag.load()) {
        try {
            SocketGuard socket;
            if (ui::all_shares_enabled()) ui::log(stdout, "Stratum connecting to %s", options.endpoint_text.c_str());
            socket.s = connect_exact(options.endpoint);
            state.begin_session();
            session_loop(socket.s, options, state, candidates, counters, stop_flag);
            state.end_session();
        } catch (const std::exception& e) {
            state.end_session();
            if (!stop_flag.load()) ui::log(stdout, "Stratum %s; reconnecting pinned endpoint", e.what());
        }
        if (!stop_flag.load()) std::this_thread::sleep_for(std::chrono::milliseconds(options.reconnect_delay_ms));
    }
    state.end_session();
}

} // namespace phasec
