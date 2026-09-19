#pragma once
#include "phasec_core.hpp"
#include <atomic>

namespace phasec {

void run_miner(int device,
               SharedState& state,
               CandidateQueue& candidates,
               std::atomic<bool>& stop_flag);

void run_benchmark(int device, std::uint32_t batches);

} // namespace phasec
