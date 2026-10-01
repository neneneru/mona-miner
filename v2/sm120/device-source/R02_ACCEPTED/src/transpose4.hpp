// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#if defined(__CUDACC__)
#define RDX_INLINE __device__ __forceinline__
#else
#define RDX_INLINE inline
#endif
namespace rdx {
// Swap one lane-index bit with the corresponding slot-index bit.
// Ops exposes Word, select_bit<Bit>(if_set,if_clear), shuffle_xor<Bit>.
// All 32 lanes participate; subgroups have width 4. No early return.
template<int Bit, class Ops>
RDX_INLINE void transpose_pair(typename Ops::Word& lo, typename Ops::Word& hi, Ops& ops) {
    auto sent=ops.template select_bit<Bit>(lo,hi);
    auto peer=ops.template shuffle_xor<Bit>(sent);
    lo=ops.template select_bit<Bit>(peer,lo);
    hi=ops.template select_bit<Bit>(hi,peer);
}
template<class Ops>
RDX_INLINE void transpose4(typename Ops::Word (&v)[4], Ops& ops) {
    transpose_pair<1>(v[0],v[1],ops);
    transpose_pair<1>(v[2],v[3],ops);
    transpose_pair<2>(v[0],v[2],ops);
    transpose_pair<2>(v[1],v[3],ops);
}
}
