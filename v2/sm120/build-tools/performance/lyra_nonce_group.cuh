// SPDX-License-Identifier: GPL-3.0-or-later
// One Lyra2REv2 nonce per CUDA tile, with CubeHash completed by the producer.

namespace lyra_nonce_group {

using Word = uint64_t;

struct Pair {
    Word first, second;
};

struct EightLanes {
    unsigned lane;

    RDX_INLINE bool upper() const { return (lane & 4u) != 0; }

    RDX_INLINE Word from(Word word, unsigned source) const {
        const uint32_t lo = __shfl_sync(0xffffffffu, uint32_t(word), source, 8);
        const uint32_t hi = __shfl_sync(0xffffffffu, uint32_t(word >> 32), source, 8);
        return Word(lo) | (Word(hi) << 32);
    }

    RDX_INLINE Word across(Word word) const {
        const uint32_t lo = __shfl_xor_sync(0xffffffffu, uint32_t(word), 4, 8);
        const uint32_t hi = __shfl_xor_sync(0xffffffffu, uint32_t(word >> 32), 4, 8);
        return Word(lo) | (Word(hi) << 32);
    }

    template<unsigned Bits>
    RDX_INLINE Word rotate(Word value) const {
        return (value >> Bits) | (value << (64 - Bits));
    }

    // A/B and C/D live in different lanes. Exchange A or C only when
    // a dependent step needs it. All lanes execute the same shuffles.
    RDX_INLINE void g(Pair& s) const {
        if (!upper()) s.first += s.second;
        Word peer = across(s.first);
        if (upper()) {
            s.second = rotate<32>(s.second ^ peer);
            s.first += s.second;
        }
        peer = across(s.first);
        if (!upper()) {
            s.second = rotate<24>(s.second ^ peer);
            s.first += s.second;
        }
        peer = across(s.first);
        if (upper()) {
            s.second = rotate<16>(s.second ^ peer);
            s.first += s.second;
        }
        peer = across(s.first);
        if (!upper()) s.second = rotate<63>(s.second ^ peer);
    }

    RDX_INLINE void round(Pair& s) const {
        g(s);
        const unsigned bank = lane & 4u;
        s.first = from(s.first, lane ^ (upper() ? 2u : 0u));
        s.second = from(s.second, bank | ((lane + (upper() ? 3u : 1u)) & 3u));
        g(s);
        s.first = from(s.first, lane ^ (upper() ? 2u : 0u));
        s.second = from(s.second, bank | ((lane + (upper() ? 1u : 3u)) & 3u));
    }

    RDX_INLINE Pair cell(const Pair& s) const { return {s.first, upper() ? 0 : s.second}; }

    RDX_INLINE Pair twist(const Pair& s) const {
        const unsigned column = lane & 3u;
        const unsigned previous = (column - 1u) & 3u;
        const bool wrap = column == 0;
        const unsigned source = previous | ((lane & 4u) ^ (wrap ? 4u : 0u));
        // Only a receiver at column zero reads from column three.
        const Word primary = from(lane == 3 ? s.second : s.first, source);
        const Word secondary = from(lane == 3 ? s.first : s.second, previous);
        return {primary, upper() ? 0 : secondary};
    }

    RDX_INLINE unsigned row(const Pair& s) const {
        return __shfl_sync(0xffffffffu, uint32_t(s.first), 0, 8) & 3u;
    }
};

RDX_INLINE Pair xored(Pair a, Pair b) { return {a.first ^ b.first, a.second ^ b.second}; }

RDX_INLINE void absorb(Pair& s, Pair a, Pair b) {
    s.first ^= a.first + b.first;
    s.second ^= a.second + b.second;
}

RDX_INLINE Pair read_row(const Pair (&column)[4], unsigned row) {
    const Pair low = row & 1u ? column[1] : column[0];
    const Pair high = row & 1u ? column[3] : column[2];
    return row & 2u ? high : low;
}

RDX_INLINE void write_row(Pair (&column)[4], unsigned row, Pair cell) {
    #pragma unroll
    for (unsigned r = 0; r < 4; ++r)
        column[r] = row == r ? cell : column[r];
}

RDX_INLINE void wander(Pair (&matrix)[4][4], Pair& s, const EightLanes& ops,
                       unsigned previous, unsigned current) {
    const unsigned other = ops.row(s);
    #pragma unroll
    for (unsigned c = 0; c < 4; ++c) {
        Pair selected = read_row(matrix[c], other);
        const Pair old_output = matrix[c][current];
        absorb(s, selected, matrix[c][previous]);
        ops.round(s);
        selected = xored(selected, ops.twist(s));
        write_row(matrix[c], other, selected);
        matrix[c][current] = xored(other == current ? selected : old_output, ops.cell(s));
    }
}

RDX_INLINE Word run_eight(Word message, unsigned lane) {
    const EightLanes ops{lane};
    Pair s = ops.upper() ? Pair{rdx::lane_iv(lane, 0), rdx::lane_iv(lane, 1)}
                         : Pair{message, message};
    #pragma unroll 1
    for (unsigned i = 0; i < 12; ++i) ops.round(s);
    if (!ops.upper()) s = xored(s, {rdx::lane_basil(lane, 0), rdx::lane_basil(lane, 1)});
    #pragma unroll 1
    for (unsigned i = 0; i < 12; ++i) ops.round(s);

    Pair matrix[4][4];
    #pragma unroll
    for (unsigned c = 0; c < 4; ++c) {
        matrix[3 - c][0] = ops.cell(s);
        ops.round(s);
    }
    #pragma unroll
    for (unsigned c = 0; c < 4; ++c) {
        const Pair previous = matrix[c][0];
        s = xored(s, previous);
        ops.round(s);
        matrix[3 - c][1] = xored(previous, ops.cell(s));
    }
    #pragma unroll
    for (unsigned c = 0; c < 4; ++c) {
        const Pair a = matrix[c][0], b = matrix[c][1];
        absorb(s, a, b);
        ops.round(s);
        matrix[3 - c][2] = xored(b, ops.cell(s));
        matrix[c][0] = xored(a, ops.twist(s));
    }
    #pragma unroll
    for (unsigned c = 0; c < 4; ++c) {
        const Pair a = matrix[c][1], b = matrix[c][2];
        absorb(s, a, b);
        ops.round(s);
        matrix[3 - c][3] = xored(b, ops.cell(s));
        matrix[c][1] = xored(a, ops.twist(s));
    }
    wander(matrix, s, ops, 3, 0);
    wander(matrix, s, ops, 0, 1);
    wander(matrix, s, ops, 1, 2);

    const unsigned other = ops.row(s);
    Pair last = read_row(matrix[0], other);
    absorb(s, last, matrix[0][2]);
    ops.round(s);
    last = xored(last, ops.twist(s));
    if (other == 3) last = xored(last, ops.cell(s));
    #pragma unroll
    for (unsigned c = 1; c < 4; ++c) {
        absorb(s, read_row(matrix[c], other), matrix[c][2]);
        ops.round(s);
    }
    s = xored(s, last);
    #pragma unroll 1
    for (unsigned i = 0; i < 12; ++i) ops.round(s);
    return s.first;
}

} // namespace lyra_nonce_group

extern "C" __global__ __launch_bounds__(MIDDLE_BLOCK)
void r02_cube1_l2_skein_cube2_b64(uint32_t n, uint2* hash, uint32_t one) {
    extern __shared__ uint64_t scratch[];
    constexpr unsigned width = LYRA_NONCE_GROUP;
    static_assert(width == 4 || width == 8);
    const unsigned lane = threadIdx.x & (width - 1u);
    const size_t nonce_index = (size_t(blockIdx.x) * MIDDLE_BLOCK + threadIdx.x) / width;
    const unsigned word = lane & 3u;
    const size_t index = GROUP_INTERLEAVED ? nonce_index * 4 + word : nonce_index + size_t(word) * n;
    const bool live = nonce_index < n && lane < 4;
    uint2 input = live ? hash[index] : make_uint2(0, 0);
    #if CUBE_FUSED_TILE
    input = cube_tile::Tile<width, CUBE_TILE_SHIFT>{lane}.run(input, one);
    #endif
    const uint64_t message = devectorize(input);
    uint64_t result;
    if constexpr (width == 4) {
        rdx::CudaOps ops{scratch, threadIdx.x & 31u};
        result = lyra_register::run<GROUP_REGISTER_COLUMNS, MIDDLE_BLOCK>(message, scratch, ops);
    } else {
        result = lyra_nonce_group::run_eight(message, lane);
    }
    if (live) hash[index] = make_uint2(uint32_t(result), uint32_t(result >> 32));
}
