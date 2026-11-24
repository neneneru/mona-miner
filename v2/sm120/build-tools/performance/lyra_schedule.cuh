// SPDX-License-Identifier: GPL-3.0-or-later
// Independent nonce states share a warp subgroup, with separate matrix phases.

namespace lyra_schedule {

template<unsigned Count>
struct SpongeOps {
    using Word = lyra_layout::Words<Count>;
    rdx::CudaOps& base;

    RDX_INLINE Word rdx_rot64(Word v, unsigned distance) const {
        #pragma unroll
        for (unsigned i = 0; i < Count; ++i) v.part[i] = base.rdx_rot64(v.part[i], distance);
        return v;
    }

    RDX_INLINE Word shuffle(Word v, int distance) const {
        #pragma unroll
        for (unsigned i = 0; i < Count; ++i) v.part[i] = base.shuffle(v.part[i], distance);
        return v;
    }
};

template<unsigned Count, unsigned Block, bool XorUpdate, bool WeaveSuffix, bool PeelFinal>
RDX_INLINE void run(uint64_t (&data)[4], uint64_t* scratch, rdx::CudaOps& base) {
    static_assert(Count == 2 || Count == 4);
    SpongeOps<Count> ops{base};
    using Word = typename SpongeOps<Count>::Word;
    #pragma unroll 1
    for (unsigned start = 0; start < 4; start += Count) {
        Word s[4];
        const uint64_t iv0 = base.iv(0);
        const uint64_t iv1 = base.iv(1);
        #pragma unroll
        for (unsigned i = 0; i < Count; ++i) {
            s[0].part[i] = data[start+i];
            s[1].part[i] = data[start+i];
            s[2].part[i] = iv0;
            s[3].part[i] = iv1;
        }
        #pragma unroll 1
        for (unsigned round = 0; round < 12; ++round) rdx::round_lyra_v5(s, ops);
        const uint64_t basil0 = base.basil(0);
        const uint64_t basil1 = base.basil(1);
        #pragma unroll
        for (unsigned i = 0; i < Count; ++i) {
            s[0].part[i] ^= basil0;
            s[1].part[i] ^= basil1;
        }
        #pragma unroll 1
        for (unsigned round = 0; round < 12; ++round) rdx::round_lyra_v5(s, ops);

        // The matrix phases execute in order and can reuse the same registers.
        #pragma unroll 1
        for (unsigned i = 0; i < Count; ++i) {
            uint64_t current[4] = {s[0].part[i], s[1].part[i], s[2].part[i], s[3].part[i]};
            lyra_register::matrix_phase<4, Block, XorUpdate>(current, scratch, base);
            if constexpr (WeaveSuffix) {
                #pragma unroll
                for (unsigned word = 0; word < 4; ++word) s[word].part[i] = current[word];
            } else {
                #pragma unroll 1
                for (unsigned round = 0; round < (PeelFinal ? 11 : 12); ++round)
                    rdx::round_lyra_v5(current, base);
                if constexpr (PeelFinal) rdx::round_lyra_v5(current, base);
                data[start+i] = current[0];
            }
        }
        if constexpr (WeaveSuffix) {
            #pragma unroll 1
            for (unsigned round = 0; round < (PeelFinal ? 11 : 12); ++round)
                rdx::round_lyra_v5(s, ops);
            if constexpr (PeelFinal) rdx::round_lyra_v5(s, ops);
            #pragma unroll
            for (unsigned i = 0; i < Count; ++i) data[start+i] = s[0].part[i];
        }
    }
}

} // namespace lyra_schedule
