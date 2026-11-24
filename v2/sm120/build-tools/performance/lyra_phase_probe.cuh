// SPDX-License-Identifier: GPL-3.0-or-later
// Diagnostics only. Sample one warp every 128 blocks in a native batch.

struct LyraPhaseTicks {
    uint64_t cube;
    uint64_t prefix;
    uint64_t matrix;
    uint64_t suffix;
    uint64_t total;
};

extern "C" {
__device__ LyraPhaseTicks lyra_phase_ticks[256];
}

__device__ __forceinline__ uint64_t phase_stamp(bool sampled) {
    uint64_t tick = 0;
    if (sampled) {
        __syncwarp();
        if (threadIdx.x == 0) tick = clock64();
        __syncwarp();
    }
    return tick;
}

extern "C" __global__ __launch_bounds__(32)
void r02_cube1_l2_skein_cube2_b64(uint32_t n, uint2* hash, uint32_t one) {
    const size_t owner = size_t(blockIdx.x) * 32 + threadIdx.x;
    const bool sampled = (blockIdx.x & 127) == 0 && (blockIdx.x >> 7) < 256;
    const auto begin = phase_stamp(sampled);
    rdx::CudaOps ops{nullptr, threadIdx.x & 31u};
    uint2 h[4];
    #pragma unroll
    for (unsigned j = 0; j < 4; ++j)
        h[j] = owner < n ? hash[owner + size_t(j) * n] : make_uint2(0, 0);

    const auto cube_begin = phase_stamp(sampled);
    mona_cube32(h, one);
    const auto cube_end = phase_stamp(sampled);

    uint64_t v[4];
    #pragma unroll
    for (unsigned j = 0; j < 4; ++j) v[j] = devectorize(h[j]);
    rdx::transpose4(v, ops);
    uint64_t a = v[0], b = v[1], c = v[2], d = v[3];
    uint64_t prefix_ticks = 0, matrix_ticks = 0, suffix_ticks = 0;
    #pragma unroll 1
    for (unsigned pass = 0; pass < 4; ++pass) {
        __syncwarp();
        const uint64_t x = pass == 0 ? a : pass == 1 ? b : pass == 2 ? c : d;
        const auto prefix_begin = phase_stamp(sampled);
        uint64_t s[4] = {x, x, ops.iv(0), ops.iv(1)};
        #pragma unroll 1
        for (unsigned i = 0; i < 12; ++i) rdx::round_lyra_v5(s, ops);
        s[0] ^= ops.basil(0);
        s[1] ^= ops.basil(1);
        #pragma unroll 1
        for (unsigned i = 0; i < 12; ++i) rdx::round_lyra_v5(s, ops);
        const auto matrix_begin = phase_stamp(sampled);
        lyra_register::matrix_phase<4, 32, false>(s, nullptr, ops);
        const auto suffix_begin = phase_stamp(sampled);
        #pragma unroll 1
        for (unsigned i = 0; i < 12; ++i) rdx::round_lyra_v5(s, ops);
        const auto suffix_end = phase_stamp(sampled);
        if (sampled && threadIdx.x == 0) {
            prefix_ticks += matrix_begin - prefix_begin;
            matrix_ticks += suffix_begin - matrix_begin;
            suffix_ticks += suffix_end - suffix_begin;
        }
        if (pass == 0) a = s[0];
        else if (pass == 1) b = s[0];
        else if (pass == 2) c = s[0];
        else d = s[0];
        __syncwarp();
    }
    v[0] = a; v[1] = b; v[2] = c; v[3] = d;
    rdx::transpose4(v, ops);
    #pragma unroll
    for (unsigned j = 0; j < 4; ++j)
        if (owner < n) hash[owner + size_t(j) * n] = make_uint2(uint32_t(v[j]), uint32_t(v[j] >> 32));
    const auto end = phase_stamp(sampled);
    if (sampled && threadIdx.x == 0)
        lyra_phase_ticks[blockIdx.x >> 7] = {cube_end - cube_begin, prefix_ticks, matrix_ticks,
                                            suffix_ticks, end - begin};
}
