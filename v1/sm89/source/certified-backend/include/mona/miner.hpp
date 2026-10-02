#pragma once
#include "core.hpp"
#include <functional>

namespace mona {
struct Solution { uint64_t generation; uint32_t nonce; std::array<uint32_t,8> hash; };
struct Accounting {
    uint64_t next_nonce=0; // Exclusive cursor; can equal 2^32. Never wrap silently.
    uint64_t unique_hashes=0, executed_hashes=0, solutions=0;
    bool cancelled=false;
};
enum class Objective { Throughput, Efficiency };
Config measured_configuration(int sm,Objective objective);
// Single-owner-thread API. A network thread may only update the atomic state
// read by cancel; it must not mutate the engine/context during scan_once().
class MiningEngine {
    ContextPtr context_;
    Config config_;
    std::array<uint32_t,20> words_{};
    std::array<uint32_t,8> target_{};
    uint64_t generation_=0;
    bool work_set_=false;
public:
    // Explicit configuration permits experiments. It does not imply verified
    // defaults; use measured_configuration for a benchmark-qualified profile.
    MiningEngine(int device,const Config& config);
    void set_work(uint64_t generation,const std::array<uint32_t,20>& words,
                  const std::array<uint32_t,8>& target);
    Accounting scan_once(uint32_t first,uint32_t count,
        const std::function<void(const Solution&)>& emit,
        const std::function<bool()>& cancel=[] {return false;});
    DeviceInfo device() const { return device_info(*context_); }
};
}
