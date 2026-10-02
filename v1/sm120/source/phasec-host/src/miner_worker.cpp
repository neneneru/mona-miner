#include "miner_worker.hpp"
#include "console_ui.hpp"
#include "mona/miner.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace phasec {
namespace {

mona::Config stable_base_config() {
    mona::Config c;
    c.batch = 1048576u;
    c.fusion = 0u;
    c.graph = false;
    c.blake_block = 64;
    c.cube_block = 32;
    c.lyra_block = 64;
    c.skein_block = 64;
    c.bmw_block = 256;
    c.carveout = -1;
    return c;
}

} // namespace

void run_miner(int device, SharedState& state, CandidateQueue& candidates,
               std::atomic<bool>& stop_flag) {
    const auto cfg = stable_base_config();
    mona::MiningEngine engine(device, cfg);
    const auto info = engine.device();
    const auto id = mona::build_identity();
    (void)id; // Source authority remains embedded; normal console UI stays concise.
    ui::gpu_identity_line(info.device, info.name.c_str(), info.sm);

    // The normal console is intentionally low-I/O: one aggregate line per configured interval.
    // --all restores per-share presentation; in that mode the aggregate interval is ignored.
    // Keep the accounting window across Stratum job changes so the displayed MH/s reflects
    // the whole interval rather than restarting whenever a new job arrives.
    const bool all_shares = ui::all_shares_enabled();
    const double summary_interval = static_cast<double>(ui::summary_interval_seconds());
    std::uint64_t hashes_since_summary = 0;
    auto summary_start = std::chrono::steady_clock::now();
    bool summary_active = false;
    ui::reset_summary_baseline();

    std::uint64_t generation = 0;
    while (!stop_flag.load()) {
        auto work = state.make_work();
        if (!work) {
            // Do not dilute the displayed mining rate with handshake/disconnect idle time.
            // The next real work item starts a fresh 60-second accounting window.
            summary_active = false;
            hashes_since_summary = 0;
            const auto serial = state.change_serial();
            state.wait_for_change(serial, 250);
            continue;
        }

        if (!summary_active) {
            summary_start = std::chrono::steady_clock::now();
            hashes_since_summary = 0;
            summary_active = true;
        }
        ui::set_pool_difficulty(work->pool_diff);
        engine.set_work(++generation, work->words, work->target);
        std::uint64_t cursor = 0;

        while (!stop_flag.load() && cursor < (std::uint64_t{1} << 32) && state.still_current(*work)) {
            const std::uint64_t remain = (std::uint64_t{1} << 32) - cursor;
            const std::uint32_t count = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(cfg.batch, remain));
            if (count == 0) break;

            const auto accounting = engine.scan_once(
                static_cast<std::uint32_t>(cursor), count,
                [&](const mona::Solution& solution) {
                    Candidate c;
                    c.work = *work;
                    c.nonce = solution.nonce;
                    c.hash = solution.hash;
                    // MiningEngine has already performed the CPU full 256-bit target check.
                    // Queue backpressure blocks rather than silently dropping a valid share.
                    if (!candidates.push(std::move(c), stop_flag) && !stop_flag.load())
                        throw std::runtime_error("candidate queue closed unexpectedly");
                },
                [&] { return stop_flag.load() || !state.still_current(*work); });

            hashes_since_summary += accounting.unique_hashes;
            cursor = accounting.next_nonce;
            if (accounting.cancelled) break;

            const auto now = std::chrono::steady_clock::now();
            const double seconds = std::chrono::duration<double>(now - summary_start).count();
            if (seconds > 0.0) {
                const double mh = (static_cast<double>(hashes_since_summary) / seconds) / 1.0e6;
                ui::set_hashrate_mhs(mh);
                if (!all_shares && seconds >= summary_interval) {
                    ui::summary_line(mh);
                    hashes_since_summary = 0;
                    summary_start = now;
                }
            }
        }
        // If cursor reached exactly 2^32, make_work() advances extranonce2 and starts at nonce 0.
        // No uint32 cursor wrap occurs inside the backend.
    }
}

void run_benchmark(int device, std::uint32_t batches) {
    const auto cfg = stable_base_config();
    mona::MiningEngine engine(device, cfg);
    std::array<std::uint32_t, 20> words{};
    std::array<std::uint32_t, 8> target{};
    target.fill(0); // Extremely hard target; benchmark never submits/network-connects.
    engine.set_work(1, words, target);

    const auto start = std::chrono::steady_clock::now();
    std::uint64_t total = 0;
    std::uint64_t cursor = 0;
    for (std::uint32_t i = 0; i < batches; ++i) {
        if (cursor + cfg.batch > (std::uint64_t{1} << 32)) cursor = 0;
        const auto a = engine.scan_once(static_cast<std::uint32_t>(cursor), cfg.batch,
                                        [](const mona::Solution&) {});
        cursor = a.next_nonce;
        total += a.unique_hashes;
    }
    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();
    std::printf("benchmark hashes=%llu seconds=%.6f rate=%.3f MH/s\n",
                static_cast<unsigned long long>(total), seconds,
                (static_cast<double>(total) / seconds) / 1.0e6);
}

} // namespace phasec
