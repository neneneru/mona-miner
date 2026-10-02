#pragma once

#include "phasec_core.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

namespace phasec {
namespace ui {

inline std::mutex g_console_mutex;
inline std::atomic<double> g_current_mhs{0.0};
inline std::atomic<double> g_current_pool_diff{0.0};
inline std::atomic<std::uint64_t> g_accepted{0};
inline std::atomic<std::uint64_t> g_rejected{0};
inline std::atomic<std::uint64_t> g_last_summary_accepted{0};
inline std::atomic<bool> g_all_shares{false};
inline std::atomic<unsigned> g_summary_interval_seconds{60};

inline void configure_output(bool all_shares, unsigned interval_seconds) noexcept {
    if (interval_seconds == 0) interval_seconds = 60;
    g_all_shares.store(all_shares, std::memory_order_relaxed);
    g_summary_interval_seconds.store(interval_seconds, std::memory_order_relaxed);
}

inline bool all_shares_enabled() noexcept {
    return g_all_shares.load(std::memory_order_relaxed);
}

inline unsigned summary_interval_seconds() noexcept {
    return g_summary_interval_seconds.load(std::memory_order_relaxed);
}

struct SummarySnapshot {
    double mhs = 0.0;
    std::uint64_t accepted = 0;
    std::uint64_t rejected = 0;
    std::uint64_t total = 0;
    std::uint64_t accepted_delta = 0;
    double pool_diff = 0.0;
};

inline void set_hashrate_mhs(double value) noexcept {
    if (std::isfinite(value) && value >= 0.0) g_current_mhs.store(value, std::memory_order_relaxed);
}

inline double current_hashrate_mhs() noexcept {
    return g_current_mhs.load(std::memory_order_relaxed);
}

inline void set_pool_difficulty(double value) noexcept {
    if (std::isfinite(value) && value >= 0.0) g_current_pool_diff.store(value, std::memory_order_relaxed);
}

inline double current_pool_difficulty() noexcept {
    return g_current_pool_diff.load(std::memory_order_relaxed);
}

inline void set_share_totals(std::uint64_t accepted, std::uint64_t rejected) noexcept {
    g_accepted.store(accepted, std::memory_order_relaxed);
    g_rejected.store(rejected, std::memory_order_relaxed);
}

inline void reset_summary_baseline(std::uint64_t accepted = 0) noexcept {
    g_last_summary_accepted.store(accepted, std::memory_order_relaxed);
}

inline SummarySnapshot take_summary_snapshot(double mhs) noexcept {
    SummarySnapshot s;
    s.mhs = (std::isfinite(mhs) && mhs >= 0.0) ? mhs : 0.0;
    s.accepted = g_accepted.load(std::memory_order_relaxed);
    s.rejected = g_rejected.load(std::memory_order_relaxed);
    s.total = s.accepted + s.rejected;
    const auto previous = g_last_summary_accepted.exchange(s.accepted, std::memory_order_relaxed);
    s.accepted_delta = (s.accepted >= previous) ? (s.accepted - previous) : s.accepted;
    s.pool_diff = current_pool_difficulty();
    return s;
}

inline void timestamp(char out[32]) noexcept {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::strftime(out, 32, "%Y-%m-%d %H:%M:%S", &tm);
}

inline void vlog(FILE* stream, const char* fmt, std::va_list ap) {
    char ts[32]{};
    timestamp(ts);
    std::lock_guard lock(g_console_mutex);
    std::fprintf(stream, "[%s] ", ts);
    std::vfprintf(stream, fmt, ap);
    std::fputc('\n', stream);
    std::fflush(stream);
}

inline void log(FILE* stream, const char* fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    vlog(stream, fmt, ap);
    va_end(ap);
}

inline long double uint256_value(const std::array<std::uint32_t, 8>& value) noexcept {
    long double out = 0.0L;
    constexpr long double radix = 4294967296.0L;
    for (int i = 7; i >= 0; --i) out = out * radix + static_cast<long double>(value[static_cast<std::size_t>(i)]);
    return out;
}

// Retained for diagnostic/tests and ccminer-compat presentation math only.
// Share acceptance is always decided by the CPU full-256-bit target check.
inline double share_difficulty(const Candidate& candidate) noexcept {
    const long double hash = uint256_value(candidate.hash);
    const long double target = uint256_value(candidate.work.target);
    if (!(hash > 0.0L) || !(target > 0.0L) || !(candidate.work.pool_diff > 0.0)) return 0.0;
    const long double targetdiff = static_cast<long double>(candidate.work.pool_diff) / 256.0L;
    const long double value = targetdiff * (target / hash);
    const double out = static_cast<double>(value);
    return (std::isfinite(out) && out >= 0.0) ? out : 0.0;
}

inline void gpu_identity_line(int device, const char* device_name, int sm) {
    log(stdout, "GPU #%d: %s, SM %d.%d", device, device_name, sm / 10, sm % 10);
}

inline std::string summary_body(const SummarySnapshot& s) {
    char out[256]{};
    std::snprintf(out, sizeof(out),
                  "%.2f MH/s | accepted: %llu/%llu (+%llu) | diff %.12g",
                  s.mhs,
                  static_cast<unsigned long long>(s.accepted),
                  static_cast<unsigned long long>(s.total),
                  static_cast<unsigned long long>(s.accepted_delta),
                  s.pool_diff);
    return out;
}

inline void summary_line(double mhs) {
    const auto s = take_summary_snapshot(mhs);
    const auto body = summary_body(s);
    log(stdout, "%s", body.c_str());
}

inline void share_result_line(bool accepted,
                              std::uint64_t accepted_count,
                              std::uint64_t rejected_count,
                              double sharediff,
                              const char* reject_reason = nullptr) {
    const auto total = accepted_count + rejected_count;
    const double mhs = current_hashrate_mhs();
    log(stdout,
        "accepted: %llu/%llu (diff %.3f), %.2f MH/s %s",
        static_cast<unsigned long long>(accepted_count),
        static_cast<unsigned long long>(total),
        sharediff,
        mhs,
        accepted ? "yes!" : "booooo");
    if (!accepted && reject_reason && *reject_reason) log(stdout, "reject reason: %s", reject_reason);
}

inline void rejected_line(std::uint64_t accepted_count,
                          std::uint64_t rejected_count,
                          const char* reject_reason = nullptr) {
    const auto total = accepted_count + rejected_count;
    if (reject_reason && *reject_reason) {
        log(stdout,
            "rejected: %llu/%llu (%s)",
            static_cast<unsigned long long>(rejected_count),
            static_cast<unsigned long long>(total),
            reject_reason);
    } else {
        log(stdout,
            "rejected: %llu/%llu",
            static_cast<unsigned long long>(rejected_count),
            static_cast<unsigned long long>(total));
    }
}

} // namespace ui
} // namespace phasec
