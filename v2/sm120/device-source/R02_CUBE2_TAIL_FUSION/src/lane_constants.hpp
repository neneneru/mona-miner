// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include "transpose4.hpp"
namespace rdx {
// Finite-domain selector; no thread-private dynamically indexed table.
RDX_INLINE uint64_t lane_four(unsigned lane,uint64_t a,uint64_t b,uint64_t c,uint64_t d) {
    const unsigned l=lane&3u;
    return (l&2u) ? ((l&1u)?d:c) : ((l&1u)?b:a);
}
RDX_INLINE uint64_t lane_iv(unsigned lane,int half) {
    if(half==0) return lane_four(lane,0x6a09e667f3bcc908ULL,0xbb67ae8584caa73bULL,0x3c6ef372fe94f82bULL,0xa54ff53a5f1d36f1ULL);
    return lane_four(lane,0x510e527fade682d1ULL,0x9b05688c2b3e6c1fULL,0x1f83d9abfb41bd6bULL,0x5be0cd19137e2179ULL);
}
RDX_INLINE uint64_t lane_basil(unsigned lane,int half) {
    if(half==0) return (lane&3u)==3u ? 1ULL : 32ULL;
    return lane_four(lane,4ULL,4ULL,128ULL,0x0100000000000000ULL);
}
}
