// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental CubeHash2 + BMW256 in the same owning CUDA thread.
#include "cube.cuh"
#include "upstream_bmw.cuh"

struct DeviceCandidates {
    uint32_t count;
    uint32_t overflow;
    uint32_t nonces[64];
};
static_assert(sizeof(DeviceCandidates) == 264);

#ifndef MONA_EXPERIMENT_BLOCK
#define MONA_EXPERIMENT_BLOCK 256
#endif

template<bool Debug>
__device__ __forceinline__ void cube2_bmw(
    uint32_t count, const uint32_t* nonce, const uint2* hash,
    uint64_t target, DeviceCandidates* hits, uint64_t* upper, uint32_t one) {
    const uint32_t index = blockIdx.x * blockDim.x + threadIdx.x;
    if (index >= count) return;

    uint2 parts[4];
    #pragma unroll
    for (int i = 0; i < 4; ++i) {
        parts[i] = hash[index + size_t(i) * count];
    }
    mona_cube32(parts, one);

    uint32_t message[16] = {};
    #pragma unroll
    for (int i = 0; i < 4; ++i) {
        message[i * 2] = parts[i].x;
        message[i * 2 + 1] = parts[i].y;
    }
    message[8] = 0x80;
    message[14] = 0x100;
    Compression256(message);
    Compression256_2(message);
    const uint64_t high = uint64_t(message[14]) | (uint64_t(message[15]) << 32);
    if constexpr (Debug) upper[index] = high;
    if (high <= target) {
        const auto slot = atomicAdd(&hits->count, 1u);
        if (slot < 64) hits->nonces[slot] = *nonce + index;
        else atomicExch(&hits->overflow, 1u);
    }
}

extern "C" __global__ __launch_bounds__(MONA_EXPERIMENT_BLOCK)
void mona_cube2_bmw(uint32_t n, const uint32_t* nonce, const uint2* hash,
                    uint64_t target, DeviceCandidates* hits, uint64_t* upper, uint32_t one) {
    cube2_bmw<false>(n, nonce, hash, target, hits, upper, one);
}

extern "C" __global__ __launch_bounds__(MONA_EXPERIMENT_BLOCK)
void mona_cube2_bmw_debug(uint32_t n, const uint32_t* nonce, const uint2* hash,
                          uint64_t target, DeviceCandidates* hits, uint64_t* upper, uint32_t one) {
    cube2_bmw<true>(n, nonce, hash, target, hits, upper, one);
}
