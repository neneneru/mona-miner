#pragma once
#include "phasec_core.hpp"
#include <atomic>
#include <string>

namespace phasec {

struct StratumOptions {
    Endpoint endpoint;
    std::string endpoint_text;
    std::string user;
    std::string password;
    std::uint32_t reconnect_delay_ms = 1000;
    std::uint64_t shares_limit = 0; // 0 = unlimited; useful for deterministic acceptance tests.
};

void run_stratum(const StratumOptions& options,
                 SharedState& state,
                 CandidateQueue& candidates,
                 Counters& counters,
                 std::atomic<bool>& stop_flag);

} // namespace phasec
