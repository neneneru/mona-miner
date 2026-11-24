// SPDX-License-Identifier: GPL-3.0-or-later
// Fixed-length CubeHash-256 using zero-copy logical XOR permutations.

namespace cube_word_layout {

__device__ __forceinline__ uint32_t add(uint32_t a, uint32_t b, uint32_t one, unsigned phase,
                                       unsigned word, unsigned pair_bit) {
    const bool imad = CUBE_MIX == 1 ? (word & pair_bit) == 0 :
                      CUBE_MIX == 2 ? ((word + phase) & 3) != 0 :
                      CUBE_MIX == 3 ? ((word + phase) & 1) == 0 :
                      ((MONA_CUBE_IMAD_PHASE_MASK >> phase) & 1) != 0;
    if (imad)
        return mona_cube_force_imad_u32(a, b, one);
    return a + b;
}

template<unsigned Match, unsigned Permute, unsigned Rotate, unsigned Phase, bool Grouped>
__device__ __forceinline__ void half_round(uint32_t (&lo)[16], uint32_t (&hi)[16], uint32_t one) {
    if constexpr (Grouped) {
        // Each pair of lo words needs only the corresponding two hi additions.
        #pragma unroll
        for (unsigned pair = 0; pair < 8; ++pair) {
            const unsigned i = ((pair & ~(Permute - 1)) << 1) | (pair & (Permute - 1));
            const unsigned j = i ^ Permute;
            const uint32_t hi_i = add(hi[i ^ Match], lo[i], one, Phase, i, Permute);
            const uint32_t hi_j = add(hi[j ^ Match], lo[j], one, Phase, j, Permute);
            hi[i ^ Match] = hi_i;
            hi[j ^ Match] = hi_j;
            lo[i] = __funnelshift_l(lo[i], lo[i], Rotate) ^ hi_j;
            lo[j] = __funnelshift_l(lo[j], lo[j], Rotate) ^ hi_i;
        }
    } else {
        #pragma unroll
        for (unsigned i = 0; i < 16; ++i)
            hi[i ^ Match] = add(hi[i ^ Match], lo[i], one, Phase, i, Permute);
        #pragma unroll
        for (unsigned i = 0; i < 16; ++i)
            lo[i] = __funnelshift_l(lo[i], lo[i], Rotate) ^ hi[i ^ Match ^ Permute];
    }
}

template<bool Grouped>
__device__ __forceinline__ void two_rounds(uint32_t (&lo)[16], uint32_t (&hi)[16], uint32_t one) {
    // Track the XOR indices of the banks, returning to canonical order after two rounds.
    half_round<0, 8, 7, 0, Grouped>(lo, hi, one);
    half_round<8 ^ 2, 4, 11, 1, Grouped>(lo, hi, one);
    half_round<12 ^ 3, 8, 7, 2, Grouped>(lo, hi, one);
    half_round<4 ^ 1, 4, 11, 3, Grouped>(lo, hi, one);
}

} // namespace cube_word_layout

__device__ __forceinline__ void mona_cube32(uint2* hash, uint32_t one) {
    uint32_t lo[16] = {
        0xEA2BD4B4, 0xCCD6F29F, 0x63117E71, 0x35481EAE,
        0x22512D5B, 0xE5D94E63, 0x7E624131, 0xF4CC12BE,
        0xC2D0B696, 0x42AF2070, 0xD0720C35, 0x3361DA8C,
        0x28CCECA4, 0x8EF8AD83, 0x4680AC00, 0x40E5FBAB,
    };
    uint32_t hi[16] = {
        0xD89041C3, 0x6107FBD5, 0x6C859D41, 0xF0B26679,
        0x09392549, 0x5FA25603, 0x65C892FD, 0x93CB6285,
        0x2AF2B5AE, 0x9E4B4E60, 0x774ABFDD, 0x85254725,
        0x15815AEB, 0x4AB6AAD6, 0x9CDAF8AF, 0xD6032C0A,
    };
    constexpr bool grouped = CUBE_GROUPED;
    #pragma unroll
    for (unsigned i = 0; i < 4; ++i) {
        lo[2*i] ^= hash[i].x;
        lo[2*i+1] ^= hash[i].y;
    }
    #pragma unroll CUBE_UNROLL
    for (unsigned i = 0; i < 8; ++i) cube_word_layout::two_rounds<grouped>(lo, hi, one);
    lo[0] ^= 0x80U;
    #pragma unroll CUBE_UNROLL
    for (unsigned i = 0; i < 8; ++i) cube_word_layout::two_rounds<grouped>(lo, hi, one);
    hi[15] ^= 1U;
    #pragma unroll CUBE_UNROLL
    for (unsigned i = 0; i < 80; ++i) cube_word_layout::two_rounds<grouped>(lo, hi, one);
    #pragma unroll
    for (unsigned i = 0; i < 4; ++i) hash[i] = make_uint2(lo[2*i], lo[2*i+1]);
}
