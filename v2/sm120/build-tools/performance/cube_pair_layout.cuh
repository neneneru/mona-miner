// SPDX-License-Identifier: GPL-3.0-or-later
// Each pair splits the even and odd words of two independent CubeHash states.

namespace cube_pair_layout {

template<unsigned Match, unsigned Mix, unsigned Rotate, unsigned Phase>
__device__ __forceinline__ void half_round(uint32_t (&lo)[8], uint32_t (&hi)[8], uint32_t one) {
    #pragma unroll
    for (unsigned i = 0; i < 8; ++i) {
        uint32_t& high = hi[i ^ Match];
        if constexpr ((MONA_CUBE_IMAD_PHASE_MASK >> Phase) & 1)
            high = mona_cube_force_imad_u32(high, lo[i], one);
        else
            high += lo[i];
    }
    #pragma unroll
    for (unsigned i = 0; i < 8; ++i)
        lo[i] = __funnelshift_l(lo[i], lo[i], Rotate) ^ hi[i ^ Match ^ Mix];
}

__device__ __forceinline__ uint32_t peer(uint32_t word) {
    return __shfl_xor_sync(0xffffffffU, word, 1, 2);
}

__device__ __forceinline__ void two_rounds(uint32_t (&lo)[8], uint32_t (&hi)[8], uint32_t one) {
    // The low bank only permutes bits 2 and 3 of the word index. The high
    // bank needs one exchange of its parity bit after each complete round.
    half_round<0, 4, 7, 0>(lo, hi, one);
    half_round<5, 2, 11, 1>(lo, hi, one);
    #pragma unroll
    for (unsigned i = 0; i < 8; ++i) hi[i] = peer(hi[i]);
    half_round<7, 4, 7, 2>(lo, hi, one);
    half_round<2, 2, 11, 3>(lo, hi, one);
    #pragma unroll
    for (unsigned i = 0; i < 8; ++i) hi[i] = peer(hi[i]);
}

__device__ __forceinline__ void hash_half(uint32_t (&message)[4], uint32_t one, bool odd) {
    constexpr uint32_t iv[32] = {CUBE_INITIAL_VALUES};
    uint32_t lo[8], hi[8];
    #pragma unroll
    for (unsigned j = 0; j < 8; ++j) {
        lo[j] = odd ? iv[2*j + 1] : iv[2*j];
        hi[j] = odd ? iv[2*j + 17] : iv[2*j + 16];
    }
    #pragma unroll
    for (unsigned j = 0; j < 4; ++j) lo[j] ^= message[j];
    #pragma unroll CUBE_UNROLL
    for (unsigned i = 0; i < 8; ++i) two_rounds(lo, hi, one);
    lo[0] ^= odd ? 0U : 0x80U;
    #pragma unroll CUBE_UNROLL
    for (unsigned i = 0; i < 8; ++i) two_rounds(lo, hi, one);
    hi[7] ^= odd ? 1U : 0U;
    #pragma unroll CUBE_UNROLL
    for (unsigned i = 0; i < 80; ++i) two_rounds(lo, hi, one);
    #pragma unroll
    for (unsigned j = 0; j < 4; ++j) message[j] = lo[j];
}

} // namespace cube_pair_layout

__device__ __forceinline__ void mona_cube_pair(uint2* hash, uint32_t one) {
    const bool odd = threadIdx.x & 1;
    uint32_t first[4], second[4];
    #pragma unroll
    for (unsigned j = 0; j < 4; ++j) {
        const uint32_t exchanged = cube_pair_layout::peer(odd ? hash[j].x : hash[j].y);
        first[j] = odd ? exchanged : hash[j].x;
        second[j] = odd ? hash[j].y : exchanged;
    }
    cube_pair_layout::hash_half(first, one, odd);
    cube_pair_layout::hash_half(second, one, odd);
    #pragma unroll
    for (unsigned j = 0; j < 4; ++j) {
        const uint32_t exchanged = cube_pair_layout::peer(odd ? first[j] : second[j]);
        hash[j] = odd ? make_uint2(exchanged, second[j]) : make_uint2(first[j], exchanged);
    }
}
