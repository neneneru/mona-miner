// SPDX-License-Identifier: GPL-3.0-or-later
// Keep selected columns in registers when each CUDA lane owns more Lyra words.

namespace lyra_hybrid_layout {

template<unsigned Width, unsigned RegisterColumns, unsigned Block>
RDX_INLINE void run(uint64_t (&values)[4], uint64_t* scratch) {
    static_assert(Width == 1 || Width == 2);
    lyra_layout::Ops<Width, Block> ops{scratch, threadIdx.x & (Width - 1)};
    using Word = typename lyra_layout::Ops<Width, Block>::Word;
    if constexpr (Width == 1) {
        const Word input{{values[0], values[1], values[2], values[3]}};
        const auto result = lyra_register::run<RegisterColumns, Block, false, 4>(input, scratch, ops);
        #pragma unroll
        for (unsigned i = 0; i < 4; ++i) values[i] = result.part[i];
    } else {
        ops.transpose(values);
        uint64_t a = values[0], b = values[1], c = values[2], d = values[3];
        #pragma unroll 1
        for (unsigned pass = 0; pass < 2; ++pass) {
            const Word input{{pass == 0 ? a : b, pass == 0 ? c : d}};
            const auto result = lyra_register::run<RegisterColumns, Block, false, 2>(input, scratch, ops);
            if (pass == 0) {
                a = result.part[0];
                c = result.part[1];
            } else {
                b = result.part[0];
                d = result.part[1];
            }
        }
        values[0] = a; values[1] = b; values[2] = c; values[3] = d;
        ops.transpose(values);
    }
}

} // namespace lyra_hybrid_layout
