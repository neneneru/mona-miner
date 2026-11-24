// SPDX-License-Identifier: GPL-3.0-or-later
// Lyra2REv2 matrix with statically indexed columns. Used by offline probes.

namespace lyra_register {

template<class Word>
struct CellWords {
    Word word[3];
};

template<unsigned RegisterColumns, unsigned Block, class Word = uint64_t, unsigned Parts = 1>
struct Matrix {
    using Cell = CellWords<Word>;
    Cell cells[RegisterColumns ? RegisterColumns : 1][4];
    uint64_t* scratch;

    RDX_INLINE unsigned offset(unsigned column, unsigned row, unsigned word) const {
        return (((column - RegisterColumns) * 4 + row) * 3 + word) * Parts * Block + threadIdx.x;
    }

    RDX_INLINE Cell read(unsigned column, unsigned row) const {
        if (column < RegisterColumns) {
            Cell value;
            #pragma unroll
            for (unsigned w = 0; w < 3; ++w) {
                const auto low = row & 1 ? cells[column][1].word[w] : cells[column][0].word[w];
                const auto high = row & 1 ? cells[column][3].word[w] : cells[column][2].word[w];
                value.word[w] = row & 2 ? high : low;
            }
            return value;
        }
        Cell value;
        #pragma unroll
        for (unsigned w = 0; w < 3; ++w) {
            const unsigned base = offset(column, row, w);
            if constexpr (Parts == 1) value.word[w] = scratch[base];
            else {
                #pragma unroll
                for (unsigned p = 0; p < Parts; ++p) value.word[w].part[p] = scratch[base + p * Block];
            }
        }
        return value;
    }

    RDX_INLINE Cell read_dynamic(unsigned column, unsigned row) const {
        #if LYRA_PREDICATED_ROW
        if constexpr (RegisterColumns == 4 && Parts == 1) {
            Cell value;
            #pragma unroll
            for (unsigned w = 0; w < 3; ++w) {
                asm("{ .reg .pred p1,p2,p3; .reg .b64 v; "
                    "setp.eq.u32 p1,%5,1; setp.eq.u32 p2,%5,2; setp.eq.u32 p3,%5,3; "
                    "mov.b64 v,%1; @p1 mov.b64 v,%2; @p2 mov.b64 v,%3; @p3 mov.b64 v,%4; mov.b64 %0,v; }"
                    : "=l"(value.word[w])
                    : "l"(cells[column][0].word[w]), "l"(cells[column][1].word[w]),
                      "l"(cells[column][2].word[w]), "l"(cells[column][3].word[w]), "r"(row));
            }
            return value;
        }
        #endif
        return read(column, row);
    }

    RDX_INLINE void write(unsigned column, unsigned row, const Cell& value) {
        if (column < RegisterColumns) {
            #pragma unroll
            for (unsigned r = 0; r < 4; ++r)
                cells[column][r] = row == r ? value : cells[column][r];
        } else {
            #pragma unroll
            for (unsigned w = 0; w < 3; ++w) {
                const unsigned base = offset(column, row, w);
                if constexpr (Parts == 1) scratch[base] = value.word[w];
                else {
                    #pragma unroll
                    for (unsigned p = 0; p < Parts; ++p) scratch[base + p * Block] = value.word[w].part[p];
                }
            }
        }
    }

    RDX_INLINE void xor_row(unsigned column, unsigned row, const Cell& delta) {
        static_assert(Parts == 1);
        // The row selected at runtime uses the same predicate for all words.
        #pragma unroll
        for (unsigned r = 0; r < 4; ++r) {
            #pragma unroll
            for (unsigned w = 0; w < 3; ++w) {
                auto cell = cells[column][r].word[w];
                asm("{ .reg .pred p; setp.eq.u32 p, %2, %3; @p xor.b64 %0, %0, %1; }"
                    : "+l"(cell) : "l"(delta.word[w]), "r"(row), "r"(r));
                cells[column][r].word[w] = cell;
            }
        }
    }
};

#if LYRA_SHARED_ROWS
// Each lane owns the same state words as the register-only kernel. Only the
// selected rows use shared memory, at disjoint addresses for each lane.
template<unsigned Mask, unsigned Block>
struct RowMatrix {
    using Cell = CellWords<uint64_t>;
    static constexpr unsigned shared_count =
        (Mask & 1u) + ((Mask >> 1) & 1u) + ((Mask >> 2) & 1u) + ((Mask >> 3) & 1u);
    static constexpr unsigned lowest_shared = Mask & (0u - Mask);
    static constexpr unsigned resident_mask = 15u ^ Mask;
    static constexpr unsigned first_resident = (resident_mask & 1u) ? 0 :
        (resident_mask & 2u) ? 1 : (resident_mask & 4u) ? 2 : 3;

    static_assert(shared_count == 1 || shared_count == 2);
    Cell resident[4][4];
    uint64_t* scratch;

    RDX_INLINE unsigned shared_offset(unsigned column, unsigned row) const {
        const unsigned slot = shared_count == 2 && (1u << row) != lowest_shared ? 1 : 0;
        return (column * shared_count + slot) * 3 * Block + threadIdx.x;
    }

    RDX_INLINE Cell read_shared(unsigned column, unsigned row) const {
        const unsigned offset = shared_offset(column, row);
        return Cell{{scratch[offset], scratch[offset + Block], scratch[offset + 2 * Block]}};
    }

    RDX_INLINE Cell read_resident(unsigned column, unsigned row) const {
        Cell cell = resident[column][first_resident];
        #pragma unroll
        for (unsigned r = 0; r < 4; ++r)
            if ((resident_mask & (1u << r)) && r != first_resident)
                cell = row == r ? resident[column][r] : cell;
        return cell;
    }

    RDX_INLINE Cell read(unsigned column, unsigned row) const {
        if (Mask & (1u << row)) return read_shared(column, row);
        return read_resident(column, row);
    }

    RDX_INLINE Cell read_dynamic(unsigned column, unsigned row) const {
        #if LYRA_PREFETCH_ROW
        // The alternate shared address is valid even when row is resident.
        const auto from_shared = read_shared(column, row);
        const auto from_resident = read_resident(column, row);
        return Mask & (1u << row) ? from_shared : from_resident;
        #else
        return read(column, row);
        #endif
    }

    RDX_INLINE void write(unsigned column, unsigned row, const Cell& cell) {
        if (Mask & (1u << row)) {
            const unsigned offset = shared_offset(column, row);
            #pragma unroll
            for (unsigned w = 0; w < 3; ++w) scratch[offset + w * Block] = cell.word[w];
        } else {
            #pragma unroll
            for (unsigned r = 0; r < 4; ++r)
                if (resident_mask & (1u << r))
                    resident[column][r] = row == r ? cell : resident[column][r];
        }
    }
};
#endif

template<class Word>
RDX_INLINE CellWords<Word> mixed(CellWords<Word> cell, const Word (&state)[4]) {
    #pragma unroll
    for (unsigned w = 0; w < 3; ++w) cell.word[w] ^= state[w];
    return cell;
}

template<class Word>
RDX_INLINE void absorb(Word (&state)[4], const CellWords<Word>& a, const CellWords<Word>& b) {
    #pragma unroll
    for (unsigned w = 0; w < 3; ++w) state[w] ^= a.word[w] + b.word[w];
}

template<class Word, class Ops>
RDX_INLINE void twist(CellWords<Word>& cell, const Word (&state)[4], Ops& ops) {
    #if LYRA_SENDER_TWIST
    // At the wrap boundary lane 3 supplies a rotated set of words to lane 0.
    const bool wrap = (ops.lane & 3) == 3;
    cell.word[0] ^= ops.shuffle(wrap ? state[2] : state[0], -1);
    cell.word[1] ^= ops.shuffle(wrap ? state[0] : state[1], -1);
    cell.word[2] ^= ops.shuffle(wrap ? state[1] : state[2], -1);
    #else
    ops.twist(cell.word, ops.shuffle(state[0], -1), ops.shuffle(state[1], -1),
              ops.shuffle(state[2], -1));
    #endif
}

template<class Layout, class Word, class Ops>
RDX_INLINE void setup(Layout& matrix, Word (&s)[4], Ops& ops) {
    #pragma unroll
    for (unsigned column = 0; column < 4; ++column) {
        matrix.write(3 - column, 0, CellWords<Word>{{s[0], s[1], s[2]}});
        rdx::round_lyra_v5(s, ops);
    }
    #pragma unroll
    for (unsigned column = 0; column < 4; ++column) {
        const auto a = matrix.read(column, 0);
        #pragma unroll
        for (unsigned w = 0; w < 3; ++w) s[w] ^= a.word[w];
        rdx::round_lyra_v5(s, ops);
        matrix.write(3 - column, 1, mixed(a, s));
    }
    #pragma unroll
    for (unsigned column = 0; column < 4; ++column) {
        auto a = matrix.read(column, 0);
        const auto b = matrix.read(column, 1);
        absorb(s, a, b);
        rdx::round_lyra_v5(s, ops);
        matrix.write(3 - column, 2, mixed(b, s));
        twist(a, s, ops);
        matrix.write(column, 0, a);
    }
    #pragma unroll
    for (unsigned column = 0; column < 4; ++column) {
        auto a = matrix.read(column, 1);
        const auto b = matrix.read(column, 2);
        absorb(s, a, b);
        rdx::round_lyra_v5(s, ops);
        matrix.write(3 - column, 3, mixed(b, s));
        twist(a, s, ops);
        matrix.write(column, 1, a);
    }
}

template<bool XorUpdate, class Layout, class Word, class Ops>
RDX_INLINE void wander(Layout& matrix, unsigned previous, unsigned row,
                       Word (&s)[4], Ops& ops) {
    const unsigned other = ops.row(s[0]);
    #pragma unroll
    for (unsigned column = 0; column < 4; ++column) {
        auto a = matrix.read_dynamic(column, other);
        const auto b = matrix.read(column, previous);
        absorb(s, a, b);
        if constexpr (XorUpdate) {
            rdx::round_lyra_v5(s, ops);
            CellWords<Word> delta{};
            twist(delta, s, ops);
            matrix.xor_row(column, other, delta);
            matrix.write(column, row, mixed(matrix.read(column, row), s));
        } else {
            const auto c = matrix.read(column, row);
            rdx::round_lyra_v5(s, ops);
            twist(a, s, ops);
            matrix.write(column, other, a);
            matrix.write(column, row, mixed(other == row ? a : c, s));
        }
    }
}

#if LYRA_DEFER_COLUMNS
template<unsigned Block>
struct DeferredRow {
    using Cell = CellWords<uint64_t>;
    Cell values[LYRA_DEFER_SHARED ? 1 : LYRA_DEFER_COLUMNS];
    uint64_t* scratch;
    unsigned row;

    RDX_INLINE void store(unsigned column, const Cell& cell) {
        #if LYRA_DEFER_SHARED
        #pragma unroll
        for (unsigned w = 0; w < 3; ++w)
            scratch[(column * 3 + w) * Block + threadIdx.x] = cell.word[w];
        #else
        values[column] = cell;
        #endif
    }

    template<class Layout>
    RDX_INLINE Cell read(const Layout& matrix, unsigned column, unsigned requested) const {
        if (column >= LYRA_DEFER_COLUMNS || requested != row || requested == 2)
            return matrix.read_dynamic(column, requested);
        #if LYRA_DEFER_SHARED
        Cell cell;
        #pragma unroll
        for (unsigned w = 0; w < 3; ++w)
            cell.word[w] = scratch[(column * 3 + w) * Block + threadIdx.x];
        return cell;
        #else
        return values[column];
        #endif
    }
};

template<unsigned Block, class Layout, class Ops>
RDX_INLINE void wander_last(Layout& matrix, DeferredRow<Block>& deferred, uint64_t (&s)[4], Ops& ops) {
    deferred.row = ops.row(s[0]);
    #pragma unroll
    for (unsigned column = 0; column < 4; ++column) {
        auto selected = matrix.read_dynamic(column, deferred.row);
        const auto out = matrix.read(column, 2);
        absorb(s, selected, matrix.read(column, 1));
        rdx::round_lyra_v5(s, ops);
        twist(selected, s, ops);
        // The next phase only reads row 2 and its selected row. Keep the
        // updated row 2 in place and consult the deferred row on demand.
        if (column < LYRA_DEFER_COLUMNS) deferred.store(column, selected);
        else matrix.write(column, deferred.row, selected);
        matrix.write(column, 2, mixed(deferred.row == 2 ? selected : out, s));
    }
}
#endif

template<unsigned Reg, unsigned Block, bool XorUpdate, unsigned Parts = 1, class Word, class Ops>
RDX_INLINE void matrix_phase(Word (&s)[4], uint64_t* scratch, Ops& ops) {
    #if LYRA_SHARED_ROWS
    static_assert(Parts == 1 && !XorUpdate);
    RowMatrix<LYRA_SHARED_ROWS, Block> matrix;
    #else
    static_assert(!XorUpdate || Reg == 4);
    Matrix<Reg, Block, Word, Parts> matrix;
    #endif
    matrix.scratch = scratch;
    setup(matrix, s, ops);

    wander<XorUpdate>(matrix, 3, 0, s, ops);
    wander<XorUpdate>(matrix, 0, 1, s, ops);
    #if LYRA_DEFER_COLUMNS
    static_assert(Parts == 1 && Reg == 4 && !XorUpdate);
    DeferredRow<Block> deferred;
    deferred.scratch = scratch;
    wander_last(matrix, deferred, s, ops);
    const auto read_last = [&](unsigned column, unsigned row) { return deferred.read(matrix, column, row); };
    #else
    wander<XorUpdate>(matrix, 1, 2, s, ops);
    const auto read_last = [&](unsigned column, unsigned row) { return matrix.read_dynamic(column, row); };
    #endif

    const unsigned other = ops.row(s[0]);
    auto last = read_last(0, other);
    absorb(s, last, matrix.read(0, 2));
    rdx::round_lyra_v5(s, ops);
    twist(last, s, ops);
    if (other == 3) last = mixed(last, s);
    #pragma unroll
    for (unsigned column = 1; column < 4; ++column) {
        absorb(s, read_last(column, other), matrix.read(column, 2));
        rdx::round_lyra_v5(s, ops);
    }
    #pragma unroll
    for (unsigned w = 0; w < 3; ++w) s[w] ^= last.word[w];
}

template<unsigned Reg, unsigned Block, bool XorUpdate = false, unsigned Parts = 1, class Ops>
RDX_INLINE typename Ops::Word run(typename Ops::Word input, uint64_t* scratch, Ops& ops) {
    typename Ops::Word s[4] = {input, input, ops.iv(0), ops.iv(1)};
    #ifndef LYRA_SPONGE_UNROLL
    #define LYRA_SPONGE_UNROLL 1
    #endif
    #pragma unroll LYRA_SPONGE_UNROLL
    for (unsigned i = 0; i < 12; ++i) rdx::round_lyra_v5(s, ops);
    s[0] ^= ops.basil(0);
    s[1] ^= ops.basil(1);
    #pragma unroll LYRA_SPONGE_UNROLL
    for (unsigned i = 0; i < 12; ++i) rdx::round_lyra_v5(s, ops);
    matrix_phase<Reg, Block, XorUpdate, Parts>(s, scratch, ops);
    #pragma unroll LYRA_SPONGE_UNROLL
    #if LYRA_PEEL_FINAL
    for (unsigned i = 0; i < 11; ++i) rdx::round_lyra_v5(s, ops);
    // Only word zero is consumed after this round. Keep it outside the loop
    // so the compiler can discard the unused final words and shuffles.
    rdx::round_lyra_v5(s, ops);
    #else
    for (unsigned i = 0; i < 12; ++i) rdx::round_lyra_v5(s, ops);
    #endif
    return s[0];
}

} // namespace lyra_register
