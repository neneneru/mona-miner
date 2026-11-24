// SPDX-License-Identifier: GPL-3.0-or-later
// Separate the 24 initial Lyra sponge rounds from the register-matrix phase.
// Store all lanes of the final warp: a padded lane can own a live nonce's column.

extern "C" __global__ __launch_bounds__(PREFIX_BLOCK)
void mona_lyra_prefix(uint32_t n, const uint2* hash, uint64_t* sponge, uint32_t one) {
    const size_t owner = size_t(blockIdx.x) * PREFIX_BLOCK + threadIdx.x;
    const size_t stride = (size_t(n) + 31) & ~size_t(31);
    if (owner >= stride) return;
    rdx::CudaOps ops{nullptr, threadIdx.x & 31u};
    uint2 h[4];
    #pragma unroll
    for (unsigned j = 0; j < 4; ++j)
        h[j] = owner < n ? hash[owner + size_t(j) * n] : make_uint2(0, 0);
    #if !MOVE_PRODUCER
    mona_cube32(h, one);
    #endif
    uint64_t v[4];
    #pragma unroll
    for (unsigned j = 0; j < 4; ++j) v[j] = devectorize(h[j]);
    rdx::transpose4(v, ops);

    #pragma unroll 1
    for (unsigned pass = 0; pass < 4; ++pass) {
        const uint64_t input = pass == 0 ? v[0] : pass == 1 ? v[1] : pass == 2 ? v[2] : v[3];
        uint64_t s[4] = {input, input, ops.iv(0), ops.iv(1)};
        #pragma unroll 1
        for (unsigned i = 0; i < 12; ++i) rdx::round_lyra_v5(s, ops);
        s[0] ^= ops.basil(0);
        s[1] ^= ops.basil(1);
        #pragma unroll 1
        for (unsigned i = 0; i < 12; ++i) rdx::round_lyra_v5(s, ops);
        #pragma unroll
        for (unsigned j = 0; j < 4; ++j) sponge[(4 * pass + j) * stride + owner] = s[j];
    }
}

extern "C" __global__ __launch_bounds__(MIDDLE_BLOCK)
void r02_cube1_l2_skein_cube2_b64(uint32_t n, uint2* hash, uint32_t, const uint64_t* sponge) {
    const size_t owner = size_t(blockIdx.x) * MIDDLE_BLOCK + threadIdx.x;
    const size_t stride = (size_t(n) + 31) & ~size_t(31);
    if (owner >= stride) return;
    rdx::CudaOps ops{nullptr, threadIdx.x & 31u};
    uint64_t a, b, c, d;
    #pragma unroll 1
    for (unsigned pass = 0; pass < 4; ++pass) {
        uint64_t s[4];
        #pragma unroll
        for (unsigned j = 0; j < 4; ++j) s[j] = sponge[(4 * pass + j) * stride + owner];
        lyra_register::matrix_phase<4, MIDDLE_BLOCK, false>(s, nullptr, ops);
        #pragma unroll 1
        for (unsigned i = 0; i < 12; ++i) rdx::round_lyra_v5(s, ops);
        if (pass == 0) a = s[0];
        else if (pass == 1) b = s[0];
        else if (pass == 2) c = s[0];
        else d = s[0];
    }
    uint64_t v[4] = {a, b, c, d};
    rdx::transpose4(v, ops);
    #pragma unroll
    for (unsigned j = 0; j < 4; ++j)
        if (owner < n) hash[owner + size_t(j) * n] = make_uint2(uint32_t(v[j]), uint32_t(v[j] >> 32));
}
