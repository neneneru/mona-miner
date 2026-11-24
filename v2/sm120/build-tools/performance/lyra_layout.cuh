// SPDX-License-Identifier: GPL-3.0-or-later
// Include after the qualified Lyra core, CubeHash and Skein device helpers.

namespace lyra_layout {

template<unsigned Count>
struct Words {
    uint64_t part[Count];

    RDX_INLINE Words& operator+=(const Words& other) {
        #pragma unroll
        for (unsigned i = 0; i < Count; ++i) part[i] += other.part[i];
        return *this;
    }

    RDX_INLINE Words& operator^=(const Words& other) {
        #pragma unroll
        for (unsigned i = 0; i < Count; ++i) part[i] ^= other.part[i];
        return *this;
    }

    RDX_INLINE Words operator+(const Words& other) const {
        Words sum = *this;
        return sum += other;
    }

    RDX_INLINE Words operator^(const Words& other) const {
        Words difference = *this;
        return difference ^= other;
    }
};

template<unsigned Width, unsigned Block>
struct Ops {
    static_assert(Width == 1 || Width == 2 || Width == 4);
    static constexpr unsigned Cols = 4 / Width;
    using Word = Words<Cols>;
    uint64_t* scratch;
    unsigned lane;

    RDX_INLINE Word load(unsigned index) const {
        Word value;
        #pragma unroll
        for (unsigned i = 0; i < Cols; ++i)
            value.part[i] = scratch[(index * Cols + i) * Block + threadIdx.x];
        return value;
    }

    RDX_INLINE void store(unsigned index, const Word& value) const {
        #pragma unroll
        for (unsigned i = 0; i < Cols; ++i)
            scratch[(index * Cols + i) * Block + threadIdx.x] = value.part[i];
    }

    RDX_INLINE Word rdx_rot64(Word value, unsigned k) const {
        #pragma unroll
        for (unsigned i = 0; i < Cols; ++i)
            value.part[i] = (value.part[i] >> k) | (value.part[i] << (64 - k));
        return value;
    }

    RDX_INLINE uint64_t xor_lane(uint64_t value, unsigned bit) const {
        uint32_t lo = __shfl_xor_sync(0xffffffffu, uint32_t(value), bit, Width);
        uint32_t hi = __shfl_xor_sync(0xffffffffu, uint32_t(value >> 32), bit, Width);
        return uint64_t(lo) | (uint64_t(hi) << 32);
    }

    RDX_INLINE Word shuffle(const Word& value, int delta) const {
        Word result;
        const unsigned step = unsigned(delta) & 3;
        #pragma unroll
        for (unsigned i = 0; i < Cols; ++i) {
            if constexpr (Width == 1) {
                result.part[i] = value.part[(i + step) & 3];
            } else if constexpr (Width == 2) {
                // An odd rotation swaps lanes. Select the element in the
                // sending lane, accounting for the carry into the next pair.
                if (step & 1) {
                    const unsigned base = (i + (step >> 1)) & 1;
                    const uint64_t send = lane ? value.part[base] : value.part[base ^ 1];
                    result.part[i] = xor_lane(send, 1);
                } else {
                    result.part[i] = value.part[(i + (step >> 1)) & 1];
                }
            } else {
                const unsigned source = (lane + step) & 3;
                const uint64_t word = value.part[i];
                uint32_t lo = __shfl_sync(0xffffffffu, uint32_t(word), source, 4);
                uint32_t hi = __shfl_sync(0xffffffffu, uint32_t(word >> 32), source, 4);
                result.part[i] = uint64_t(lo) | (uint64_t(hi) << 32);
            }
        }
        return result;
    }

    RDX_INLINE Word iv(int half) const {
        Word value;
        #pragma unroll
        for (unsigned i = 0; i < Cols; ++i) value.part[i] = rdx::lane_iv(lane + i * Width, half);
        return value;
    }

    RDX_INLINE Word basil(int half) const {
        Word value;
        #pragma unroll
        for (unsigned i = 0; i < Cols; ++i) value.part[i] = rdx::lane_basil(lane + i * Width, half);
        return value;
    }

    RDX_INLINE int row(const Word& value) const {
        uint32_t column0 = uint32_t(value.part[0]);
        if constexpr (Width != 1) column0 = __shfl_sync(0xffffffffu, column0, 0, Width);
        return int(column0 & 3u);
    }

    RDX_INLINE void twist(Word (&values)[3], const Word& x, const Word& y, const Word& z) const {
        #pragma unroll
        for (unsigned i = 0; i < Cols; ++i) {
            const bool wrap = i == 0 && lane == 0;
            values[0].part[i] ^= wrap ? z.part[i] : x.part[i];
            values[1].part[i] ^= wrap ? x.part[i] : y.part[i];
            values[2].part[i] ^= wrap ? y.part[i] : z.part[i];
        }
    }

    RDX_INLINE void transpose_pair(uint64_t& lo, uint64_t& hi, unsigned bit) const {
        const bool upper = (lane & bit) != 0;
        const uint64_t other = xor_lane(upper ? lo : hi, bit);
        if (upper) lo = other;
        else hi = other;
    }

    RDX_INLINE void transpose(uint64_t (&values)[4]) const {
        if constexpr (Width >= 2) {
            transpose_pair(values[0], values[1], 1);
            transpose_pair(values[2], values[3], 1);
        }
        if constexpr (Width == 4) {
            transpose_pair(values[0], values[2], 2);
            transpose_pair(values[1], values[3], 2);
        }
    }
};

template<unsigned Width, unsigned Block>
RDX_INLINE void run(uint64_t (&values)[4], uint64_t* scratch) {
    Ops<Width, Block> ops{scratch, threadIdx.x & (Width - 1)};
    ops.transpose(values);
    // All lanes run all passes, including dummy lanes in a partial block.
    #pragma unroll 1
    for (unsigned pass = 0; pass < Width; ++pass) {
        typename Ops<Width, Block>::Word packed;
        #pragma unroll
        for (unsigned i = 0; i < ops.Cols; ++i) packed.part[i] = values[i * Width + pass];
        const auto result = rdx::lyra_word(packed, ops);
        #pragma unroll
        for (unsigned i = 0; i < ops.Cols; ++i) values[i * Width + pass] = result.part[i];
    }
    ops.transpose(values);
}

} // namespace lyra_layout
