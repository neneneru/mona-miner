// SPDX-License-Identifier: GPL-3.0-or-later
// CubeHash banks distributed over a complete four- or eight-lane tile.

namespace cube_tile {

template<unsigned Width, unsigned Shift>
struct Tile {
    static constexpr unsigned parts = 16 / Width;
    static constexpr unsigned low = (1u << Shift) - 1;
    static_assert(Width == 4 || Width == 8);
    static_assert((Width << Shift) <= 16);

    unsigned lane;

    RDX_INLINE unsigned word(unsigned part) const {
        return (lane << Shift) | (part & low) | ((part & ~low) * Width);
    }

    RDX_INLINE static unsigned part_of(unsigned word) {
        return (word & low) | ((word / Width) & ~low);
    }

    template<unsigned Delta>
    RDX_INLINE uint32_t permute(const uint32_t (&values)[parts], unsigned part) const {
        constexpr unsigned peer = (Delta >> Shift) & (Width - 1);
        const uint32_t v = values[part ^ part_of(Delta)];
        if constexpr (peer == 0) return v;
        return __shfl_xor_sync(0xffffffffu, v, peer, Width);
    }

    RDX_INLINE uint32_t gather(const uint32_t (&values)[parts], unsigned word) const {
        const unsigned source = (word >> Shift) & (Width - 1);
        uint32_t value = 0;
        #pragma unroll
        for (unsigned part = 0; part < parts; ++part) {
            const uint32_t item = __shfl_sync(0xffffffffu, values[part], source, Width);
            if (part == part_of(word)) value = item;
        }
        return value;
    }

    template<unsigned Previous, unsigned Match, unsigned Mix, unsigned Rotation>
    RDX_INLINE void half_round(uint32_t (&lo)[parts], uint32_t (&hi)[parts], uint32_t one) const {
        uint32_t aligned[parts];
        #pragma unroll
        for (unsigned part = 0; part < parts; ++part) {
            const uint32_t high = permute<Previous ^ Match>(hi, part);
            aligned[part] = CUBE_TILE_IMAD ? mona_cube_force_imad_u32(high, lo[part], one)
                                         : high + lo[part];
        }
        #pragma unroll
        for (unsigned part = 0; part < parts; ++part) {
            lo[part] = __funnelshift_l(lo[part], lo[part], Rotation) ^ permute<Mix>(aligned, part);
            hi[part] = aligned[part];
        }
    }

    RDX_INLINE void two_rounds(uint32_t (&lo)[parts], uint32_t (&hi)[parts], uint32_t one) const {
        half_round<5, 0, 8, 7>(lo, hi, one);
        half_round<0, 10, 4, 11>(lo, hi, one);
        half_round<10, 15, 8, 7>(lo, hi, one);
        half_round<15, 5, 4, 11>(lo, hi, one);
    }

    RDX_INLINE uint2 run(uint2 input, uint32_t one) const {
        constexpr uint32_t iv[32] = {CUBE_TILE_INITIAL_VALUES};
        uint32_t lo[parts], hi[parts];
        #pragma unroll
        for (unsigned part = 0; part < parts; ++part) {
            const unsigned i = word(part);
            hi[part] = iv[16 + (i ^ 5u)];
            uint32_t message;
            if constexpr (Shift == 1) {
                message = (part & 1u) ? input.y : input.x;
            } else {
                const unsigned source = (i >> 1) & (Width - 1);
                const uint32_t even = __shfl_sync(0xffffffffu, input.x, source, Width);
                const uint32_t odd = __shfl_sync(0xffffffffu, input.y, source, Width);
                message = (i & 1u) ? odd : even;
            }
            lo[part] = iv[i] ^ (i < 8 ? message : 0);
        }

        #pragma unroll CUBE_UNROLL
        for (unsigned i = 0; i < 8; ++i) two_rounds(lo, hi, one);
        #pragma unroll
        for (unsigned part = 0; part < parts; ++part)
            lo[part] ^= word(part) == 0 ? 0x80u : 0;
        #pragma unroll CUBE_UNROLL
        for (unsigned i = 0; i < 8; ++i) two_rounds(lo, hi, one);
        #pragma unroll
        for (unsigned part = 0; part < parts; ++part)
            hi[part] ^= word(part) == (15u ^ 5u) ? 1u : 0;
        #pragma unroll CUBE_UNROLL
        for (unsigned i = 0; i < 80; ++i) two_rounds(lo, hi, one);
        if constexpr (Shift == 1) return make_uint2(lo[0], lo[1]);
        return make_uint2(gather(lo, (lane & 3u) * 2), gather(lo, (lane & 3u) * 2 + 1));
    }
};

} // namespace cube_tile

#if CUBE_PREFIX_WIDTH
extern "C" __global__ __launch_bounds__(PREFIX_BLOCK)
void mona_lyra_prefix(uint32_t n, uint2* hash, uint64_t*, uint32_t one) {
    constexpr unsigned width = CUBE_PREFIX_WIDTH;
    const unsigned lane = threadIdx.x & (width - 1);
    const size_t owner = (size_t(blockIdx.x) * PREFIX_BLOCK + threadIdx.x) / width;
    const bool live = owner < n && lane < 4;
    const size_t index = owner + size_t(lane) * n;
    const auto input = live ? hash[index] : make_uint2(0, 0);
    const auto result = cube_tile::Tile<width, CUBE_TILE_SHIFT>{lane}.run(input, one);
    if (live) hash[index] = result;
}
#endif
