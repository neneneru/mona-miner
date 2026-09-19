#include "miner_worker.hpp"
#include "console_ui.hpp"
#include "phasec_core.hpp"
#include "stratum_client.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
std::atomic<bool>* g_stop = nullptr;
#ifdef _WIN32
BOOL WINAPI ctrl_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT ||
        type == CTRL_SHUTDOWN_EVENT) {
        if (g_stop) g_stop->store(true);
        return TRUE;
    }
    return FALSE;
}
#endif

void usage() {
    std::fprintf(stderr,
        "Mona Miner - Monacoin Lyra2REv2\n"
        "Usage:\n"
        "  mona-miner.exe -a lyra2v2 -o stratum+tcp://HOST:PORT -u USER -p PASS [--device N] [--shares-limit N] [--all] [--interval N]\n"
        "Console output:\n"
        "  default       60-second aggregate summary\n"
        "  --all         print every accepted share (overrides --interval for display)\n"
        "  --interval N  aggregate summary interval in seconds (1..86400; default 60)\n"
        "  mona-miner.exe -a lyra2v2 --benchmark [--device N] [--benchmark-batches N]\n"
        "Only lyra2v2 is supported; unsupported algorithms fail closed.\n");
}
}

int main(int argc, char** argv) {
    try {
        std::string algo;
        std::string url;
        std::string user;
        std::string pass;
        int device = 0;
        bool benchmark = false;
        std::uint32_t benchmark_batches = 16;
        std::uint64_t shares_limit = 0;
        bool all_shares = false;
        unsigned summary_interval = 60;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto require_value = [&](const char* name) -> std::string {
                if (i + 1 >= argc) throw std::invalid_argument(std::string("missing value for ") + name);
                return argv[++i];
            };
            if (arg == "-a" || arg == "--algo") algo = require_value(arg.c_str());
            else if (arg == "-o" || arg == "--url") url = require_value(arg.c_str());
            else if (arg == "-u" || arg == "--user") user = require_value(arg.c_str());
            else if (arg == "-p" || arg == "--pass") pass = require_value(arg.c_str());
            else if (arg == "--device" || arg == "-d") device = std::stoi(require_value(arg.c_str()));
            else if (arg == "--benchmark") benchmark = true;
            else if (arg == "--benchmark-batches") benchmark_batches = static_cast<std::uint32_t>(std::stoul(require_value(arg.c_str())));
            else if (arg == "--shares-limit") shares_limit = std::stoull(require_value(arg.c_str()));
            else if (arg == "--all") all_shares = true;
            else if (arg == "--interval") {
                const auto value = std::stoul(require_value(arg.c_str()));
                if (value < 1 || value > 86400) throw std::invalid_argument("--interval must be between 1 and 86400 seconds");
                summary_interval = static_cast<unsigned>(value);
            }
            else if (arg == "-h" || arg == "--help") { usage(); return 0; }
            else throw std::invalid_argument("unsupported option: " + arg);
        }

        if (algo != "lyra2v2") {
            std::fprintf(stderr, "ERROR: only -a lyra2v2 is supported by Mona Miner.\n");
            return 2;
        }
        if (device < 0) throw std::invalid_argument("device must be >= 0");

        if (benchmark) {
            if (!url.empty() || !user.empty() || !pass.empty())
                std::fprintf(stderr, "[benchmark] pool credentials ignored; benchmark mode creates no network connection\n");
            phasec::run_benchmark(device, benchmark_batches);
            return 0;
        }

        if (url.empty() || user.empty()) {
            usage();
            return 2;
        }
        const auto endpoint = phasec::parse_endpoint(url);
        phasec::ui::configure_output(all_shares, summary_interval);

        phasec::ui::log(stdout, "Mona Miner - Lyra2REv2");
        phasec::ui::log(stdout, "Pool: %s", url.c_str());

        std::atomic<bool> stop{false};
        std::atomic<bool> fatal_error{false};
        g_stop = &stop;
#ifdef _WIN32
        SetConsoleCtrlHandler(ctrl_handler, TRUE);
#endif
        phasec::SharedState state;
        phasec::CandidateQueue queue(65536); // bounded, backpressured, no silent candidate drops
        phasec::Counters counters;
        phasec::StratumOptions options{endpoint, url, user, pass, 1000, shares_limit};

        std::thread network([&] {
            try { phasec::run_stratum(options, state, queue, counters, stop); }
            catch (const std::exception& e) {
                fatal_error.store(true);
                phasec::ui::log(stderr, "fatal network error: %s", e.what());
                stop.store(true);
                state.stop();
                queue.close();
            }
        });
        std::thread miner([&] {
            try { phasec::run_miner(device, state, queue, stop); }
            catch (const std::exception& e) {
                fatal_error.store(true);
                phasec::ui::log(stderr, "fatal miner error: %s", e.what());
                stop.store(true);
                state.stop();
                queue.close();
            }
        });

        network.join();
        stop.store(true);
        state.stop();
        queue.close();
        miner.join();

        std::fprintf(stderr,
            "[exit] accepted=%llu rejected=%llu stale_pool=%llu stale_local=%llu send_fail=%llu\n",
            static_cast<unsigned long long>(counters.accepted.load()),
            static_cast<unsigned long long>(counters.rejected.load()),
            static_cast<unsigned long long>(counters.stale_pool.load()),
            static_cast<unsigned long long>(counters.stale_local.load()),
            static_cast<unsigned long long>(counters.send_fail.load()));
        return fatal_error.load() ? 2 : 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR: %s\n", e.what());
        usage();
        return 2;
    }
}
