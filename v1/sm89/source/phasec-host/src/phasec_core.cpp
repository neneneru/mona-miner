#include "phasec_core.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

extern "C" {
#include "sph/sph_sha2.h"
}

namespace phasec {
namespace {

std::uint32_t le32dec(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::uint32_t be32dec(const std::uint8_t* p) {
    return std::uint32_t(p[3]) | (std::uint32_t(p[2]) << 8) |
           (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[0]) << 24);
}

void sha256d(std::uint8_t out[32], const std::uint8_t* data, std::size_t n) {
    sph_sha256_context ctx;
    std::uint8_t first[32];
    sph_sha256_init(&ctx);
    sph_sha256(&ctx, data, n);
    sph_sha256_close(&ctx, first);
    sph_sha256_init(&ctx);
    sph_sha256(&ctx, first, sizeof(first));
    sph_sha256_close(&ctx, out);
}

std::array<std::uint8_t, 32> merkle_root(const JobTemplate& job,
                                         const std::vector<std::uint8_t>& xnonce1,
                                         const std::vector<std::uint8_t>& xnonce2) {
    std::vector<std::uint8_t> coinbase;
    coinbase.reserve(job.coinb1.size() + xnonce1.size() + xnonce2.size() + job.coinb2.size());
    coinbase.insert(coinbase.end(), job.coinb1.begin(), job.coinb1.end());
    coinbase.insert(coinbase.end(), xnonce1.begin(), xnonce1.end());
    coinbase.insert(coinbase.end(), xnonce2.begin(), xnonce2.end());
    coinbase.insert(coinbase.end(), job.coinb2.begin(), job.coinb2.end());

    std::array<std::uint8_t, 32> root{};
    sha256d(root.data(), coinbase.data(), coinbase.size());
    for (const auto& branch : job.merkle) {
        std::array<std::uint8_t, 64> pair{};
        std::copy(root.begin(), root.end(), pair.begin());
        std::copy(branch.begin(), branch.end(), pair.begin() + 32);
        sha256d(root.data(), pair.data(), pair.size());
    }
    return root;
}

ConcreteWork assemble_work(const JobTemplate& job,
                           const std::vector<std::uint8_t>& xnonce1,
                           const std::vector<std::uint8_t>& xnonce2,
                           const Epochs& epochs,
                           std::uint64_t serial) {
    if (job.prevhash.size() != 32) throw std::invalid_argument("prevhash must be 32 bytes");
    ConcreteWork w;
    w.job_id = job.job_id;
    w.xnonce2 = xnonce2;
    w.epochs = epochs;
    w.pool_diff = job.diff;
    w.serial = serial;

    const auto root = merkle_root(job, xnonce1, xnonce2);
    w.words[0] = le32dec(job.version.data());
    for (int i = 0; i < 8; ++i) w.words[1 + i] = le32dec(job.prevhash.data() + 4 * i);
    for (int i = 0; i < 8; ++i) w.words[9 + i] = be32dec(root.data() + 4 * i);
    w.words[17] = le32dec(job.ntime.data());
    w.words[18] = le32dec(job.nbits.data());
    w.words[19] = 0;
    w.target = lyra2v2_pool_target(job.diff);
    return w;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

Endpoint parse_endpoint(std::string_view url) {
    constexpr std::string_view prefix = "stratum+tcp://";
    if (!url.starts_with(prefix)) throw std::invalid_argument("only stratum+tcp:// is supported");
    url.remove_prefix(prefix.size());
    if (url.empty()) throw std::invalid_argument("missing pool endpoint");
    Endpoint ep;
    if (url.front() == '[') {
        const auto close = url.find(']');
        if (close == std::string_view::npos || close + 2 > url.size() || url[close + 1] != ':')
            throw std::invalid_argument("invalid IPv6 endpoint");
        ep.host = std::string(url.substr(1, close - 1));
        url.remove_prefix(close + 2);
    } else {
        const auto colon = url.rfind(':');
        if (colon == std::string_view::npos) throw std::invalid_argument("pool port is required");
        ep.host = std::string(url.substr(0, colon));
        url.remove_prefix(colon + 1);
    }
    if (ep.host.empty() || url.empty()) throw std::invalid_argument("invalid pool endpoint");
    unsigned long port = 0;
    for (char c : url) {
        if (c < '0' || c > '9') throw std::invalid_argument("invalid pool port");
        port = port * 10 + unsigned(c - '0');
        if (port > 65535) throw std::invalid_argument("pool port out of range");
    }
    if (port == 0) throw std::invalid_argument("pool port out of range");
    ep.port = static_cast<std::uint16_t>(port);
    return ep;
}

std::vector<std::uint8_t> hex_decode(std::string_view hex) {
    if (hex.size() % 2) throw std::invalid_argument("odd-length hex");
    std::vector<std::uint8_t> out(hex.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int hi = hex_value(hex[2 * i]);
        const int lo = hex_value(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) throw std::invalid_argument("invalid hex");
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return out;
}

std::string hex_encode(const std::uint8_t* p, std::size_t n) {
    static constexpr char d[] = "0123456789abcdef";
    std::string s(2 * n, '0');
    for (std::size_t i = 0; i < n; ++i) {
        s[2 * i] = d[p[i] >> 4];
        s[2 * i + 1] = d[p[i] & 15];
    }
    return s;
}

std::string hex_encode(const std::vector<std::uint8_t>& v) {
    return hex_encode(v.data(), v.size());
}

std::string nonce_hex(std::uint32_t nonce) {
    std::uint8_t b[4] = {
        static_cast<std::uint8_t>(nonce),
        static_cast<std::uint8_t>(nonce >> 8),
        static_cast<std::uint8_t>(nonce >> 16),
        static_cast<std::uint8_t>(nonce >> 24),
    };
    return hex_encode(b, sizeof(b));
}

std::array<std::uint32_t, 8> diff_to_target(double diff) {
    if (!(diff > 0.0) || !std::isfinite(diff)) throw std::invalid_argument("difficulty must be finite and positive");
    std::array<std::uint32_t, 8> target{};
    int k;
    for (k = 6; k > 0 && diff > 1.0; --k) diff /= 4294967296.0;
    const auto m = static_cast<std::uint64_t>(4294901760.0 / diff);
    if (m == 0 && k == 6) {
        target.fill(0xffffffffu);
    } else {
        target.fill(0);
        target.at(static_cast<std::size_t>(k)) = static_cast<std::uint32_t>(m);
        target.at(static_cast<std::size_t>(k + 1)) = static_cast<std::uint32_t>(m >> 32);
    }
    return target;
}

std::array<std::uint32_t, 8> lyra2v2_pool_target(double pool_diff) {
    // Pinned ccminer windows@6ff4e50 applies a /256 factor for ALGO_LYRA2v2.
    return diff_to_target(pool_diff / 256.0);
}

void increment_le(std::vector<std::uint8_t>& value, bool& wrapped) {
    wrapped = true;
    for (auto& b : value) {
        b = static_cast<std::uint8_t>(b + 1u);
        if (b != 0) {
            wrapped = false;
            return;
        }
    }
}

void SharedState::changed_unlocked() {
    ++change_serial_;
    cv_.notify_all();
}

void SharedState::stop() {
    std::lock_guard lock(mu_);
    stopped_ = true;
    connected_ = false;
    authorized_ = false;
    changed_unlocked();
}

bool SharedState::stopped() const {
    std::lock_guard lock(mu_);
    return stopped_;
}

void SharedState::begin_session() {
    std::lock_guard lock(mu_);
    ++epochs_.session;
    connected_ = true;
    authorized_ = false;
    have_subscription_ = false;
    have_job_ = false;
    xnonce2_.clear();
    xnonce2_exhausted_ = false;
    submit_valid_jobs_.clear();
    changed_unlocked();
}

void SharedState::end_session() {
    std::lock_guard lock(mu_);
    if (connected_ || authorized_) ++epochs_.session;
    connected_ = false;
    authorized_ = false;
    have_job_ = false;
    submit_valid_jobs_.clear();
    changed_unlocked();
}

void SharedState::set_authorized(bool value) {
    std::lock_guard lock(mu_);
    authorized_ = value;
    changed_unlocked();
}

void SharedState::set_subscription(const std::vector<std::uint8_t>& xnonce1, std::size_t xnonce2_size) {
    if (xnonce2_size < 2 || xnonce2_size > 16) throw std::invalid_argument("extranonce2 size must be 2..16");
    std::lock_guard lock(mu_);
    xnonce1_ = xnonce1;
    xnonce2_.assign(xnonce2_size, 0);
    xnonce2_exhausted_ = false;
    have_subscription_ = true;
    // Pinned ccminer resets next_diff to 1.0 after a fresh Stratum subscription.
    // Do not carry a prior session's difficulty into a reconnect.
    next_diff_ = 1.0;
    ++epochs_.extranonce;
    submit_valid_jobs_.clear();
    changed_unlocked();
}

void SharedState::set_extranonce(const std::vector<std::uint8_t>& xnonce1, std::size_t xnonce2_size) {
    if (xnonce2_size < 2 || xnonce2_size > 16) throw std::invalid_argument("extranonce2 size must be 2..16");
    std::lock_guard lock(mu_);
    xnonce1_ = xnonce1;
    xnonce2_.assign(xnonce2_size, 0);
    xnonce2_exhausted_ = false;
    have_subscription_ = true;
    ++epochs_.extranonce;
    submit_valid_jobs_.clear();
    changed_unlocked();
}

void SharedState::set_next_difficulty(double diff) {
    if (!(diff > 0.0) || !std::isfinite(diff)) throw std::invalid_argument("difficulty must be finite and positive");
    std::lock_guard lock(mu_);
    // Stratum set_difficulty applies to the next notify/job, matching pinned ccminer.
    next_diff_ = diff;
}

void SharedState::set_job(JobTemplate job) {
    if (job.prevhash.size() != 32) throw std::invalid_argument("notify prevhash must be 32 bytes");
    if (job.job_id.empty()) throw std::invalid_argument("notify job_id is empty");
    std::lock_guard lock(mu_);
    job.diff = next_diff_;
    const bool clean = job.clean;
    job_ = std::move(job);
    have_job_ = true;
    xnonce2_exhausted_ = false;
    ++epochs_.job;
    ++epochs_.target;
    if (clean) submit_valid_jobs_.clear();
    // Reusing a job id with changed contents is treated conservatively: only the newest
    // template for that id remains locally submit-valid.  clean=false may keep other ids.
    submit_valid_jobs_.erase(std::remove_if(submit_valid_jobs_.begin(), submit_valid_jobs_.end(),
        [&](const ValidJob& v) { return v.job_id == job_.job_id; }), submit_valid_jobs_.end());
    const auto target = lyra2v2_pool_target(job_.diff);
    const auto ntime = le32dec(job_.ntime.data());
    submit_valid_jobs_.push_back(ValidJob{job_.job_id, target, ntime, epochs_.session, epochs_.extranonce});
    changed_unlocked();
}

std::optional<ConcreteWork> SharedState::make_work() {
    std::lock_guard lock(mu_);
    if (stopped_ || !connected_ || !authorized_ || !have_subscription_ || !have_job_ || xnonce2_exhausted_)
        return std::nullopt;
    const auto current = xnonce2_;
    bool wrapped = false;
    increment_le(xnonce2_, wrapped);
    if (wrapped) xnonce2_exhausted_ = true;
    return assemble_work(job_, xnonce1_, current, epochs_, ++work_serial_);
}

bool SharedState::still_current(const ConcreteWork& work) const {
    std::lock_guard lock(mu_);
    if (stopped_ || !connected_ || !authorized_ || !have_job_) return false;
    if (!(work.epochs == epochs_)) return false;
    if (work.job_id != job_.job_id) return false;
    if (work.target != lyra2v2_pool_target(job_.diff)) return false;
    return true;
}

bool SharedState::run_if_current(const ConcreteWork& work, const std::function<bool()>& action) {
    std::lock_guard lock(mu_);
    if (stopped_ || !connected_ || !authorized_ || !have_job_) return false;
    if (!(work.epochs == epochs_)) return false;
    if (work.job_id != job_.job_id) return false;
    if (work.target != lyra2v2_pool_target(job_.diff)) return false;
    // Keep the state lock through the externally-visible action. Network state
    // mutation therefore cannot slip between final validation and socket send.
    return action();
}

bool SharedState::run_if_submit_valid(const ConcreteWork& work, const std::function<bool()>& action) {
    std::lock_guard lock(mu_);
    if (stopped_ || !connected_ || !authorized_) return false;
    if (work.epochs.session != epochs_.session || work.epochs.extranonce != epochs_.extranonce) return false;
    const auto it = std::find_if(submit_valid_jobs_.begin(), submit_valid_jobs_.end(), [&](const ValidJob& v) {
        return v.session == work.epochs.session && v.extranonce == work.epochs.extranonce &&
               v.job_id == work.job_id && v.target == work.target && v.ntime == work.words[17];
    });
    if (it == submit_valid_jobs_.end()) return false;
    // Keep the state lock through the externally-visible action. Session/extranonce/
    // clean-job mutation therefore cannot slip between final validation and socket send.
    return action();
}

std::uint64_t SharedState::change_serial() const {
    std::lock_guard lock(mu_);
    return change_serial_;
}

bool SharedState::wait_for_change(std::uint64_t previous, std::uint32_t timeout_ms) const {
    std::unique_lock lock(mu_);
    return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
        return stopped_ || change_serial_ != previous;
    });
}

Epochs SharedState::epochs() const {
    std::lock_guard lock(mu_);
    return epochs_;
}

std::string SharedState::current_job_id() const {
    std::lock_guard lock(mu_);
    return have_job_ ? job_.job_id : std::string{};
}

CandidateQueue::CandidateQueue(std::size_t capacity) : capacity_(capacity) {
    if (capacity_ == 0) throw std::invalid_argument("candidate queue capacity must be nonzero");
}

bool CandidateQueue::push(Candidate candidate, const std::atomic<bool>& stop_flag) {
    std::unique_lock lock(mu_);
    not_full_.wait(lock, [&] { return closed_ || stop_flag.load(std::memory_order_relaxed) || queue_.size() < capacity_; });
    if (closed_ || stop_flag.load(std::memory_order_relaxed)) return false;
    queue_.push_back(std::move(candidate));
    return true;
}

std::optional<Candidate> CandidateQueue::try_pop() {
    std::lock_guard lock(mu_);
    if (queue_.empty()) return std::nullopt;
    Candidate out = std::move(queue_.front());
    queue_.pop_front();
    not_full_.notify_one();
    return out;
}

void CandidateQueue::close() {
    std::lock_guard lock(mu_);
    closed_ = true;
    not_full_.notify_all();
}

std::size_t CandidateQueue::size() const {
    std::lock_guard lock(mu_);
    return queue_.size();
}

} // namespace phasec
