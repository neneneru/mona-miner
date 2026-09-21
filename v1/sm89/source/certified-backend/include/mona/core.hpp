#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mona {
constexpr unsigned FusedLyra=1, FusedBlakeCube=2, FusedSkeinCube=4;
constexpr unsigned MaxCandidates=64;
// Development defaults, NOT measured fastest defaults. See measured_defaults.hpp.
struct Config {
    uint32_t batch=262144;
    unsigned fusion=0;
    bool graph=false;
    int blake_block=256, cube_block=256, lyra_block=32;
    int skein_block=256, bmw_block=256, carveout=-1;
};
struct BuildIdentity {
    std::string build_id;
    std::string profile;
    std::string schema;
};
BuildIdentity build_identity();
struct DeviceInfo {
    std::string name;
    std::string uuid;
    int device=0, sm=0, multiprocessors=0, driver=0, runtime=0;
    uint64_t memory_bytes=0;
};
struct KernelInfo {
    std::string name;
    int block=0, registers_per_thread=0, active_blocks_per_sm=0;
    int max_threads_per_sm=0;
    uint64_t local_bytes_per_thread=0, static_shared_bytes=0, dynamic_shared_bytes=0;
    double theoretical_occupancy=0;
};
struct Candidates {
    std::vector<uint32_t> nonces;
    uint32_t total_found=0;
    bool overflow=false;
};
struct DebugOutput {
    std::vector<uint64_t> pre_bmw_soa; // Four uint64 planes, stride=count.
    std::vector<uint64_t> bmw_upper64;
};
class Context;
struct ContextDeleter { void operator()(Context*) const noexcept; };
using ContextPtr=std::unique_ptr<Context,ContextDeleter>;
ContextPtr create(int device, const Config&);
DeviceInfo device_info(const Context&);
std::vector<KernelInfo> kernel_info(Context&);
// Input words have ccminer's pdata representation: CPU reference encodes each
// word big-endian. A nonce is substituted for word 19 in that representation.
void set_job(Context&, const std::array<uint32_t,20>& words, uint64_t upper_target);
Candidates scan(Context&, uint32_t first_nonce, uint32_t count);
DebugOutput debug(Context&, uint32_t first_nonce, uint32_t count);
std::array<uint32_t,8> cpu_hash(const std::array<uint32_t,20>&, uint32_t nonce,
                              std::array<uint64_t,4>* pre_bmw=nullptr);
bool full_test(const std::array<uint32_t,8>& hash, const std::array<uint32_t,8>& target);
void validate_config(const Config&);
}
