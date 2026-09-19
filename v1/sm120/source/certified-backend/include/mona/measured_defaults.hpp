#pragma once
#include "core.hpp"
namespace mona {
struct MeasuredDefaults { bool verified; Config fastest; Config efficient; bool has_efficient; };
inline MeasuredDefaults defaults_sm89() { return {false,Config{},Config{},false}; }

// Conservative RTX 5090 / SM120 Base selected for qualification, 2026-09-14.
// These are proposed Base parameters, NOT a binary/build identity certificate.
// No historical NextCol, ActiveMask, or energy certificate is inherited.
#if defined(MONA_TARGET_SM) && (MONA_TARGET_SM == 120) && \
    defined(MONA_LYRA_STABLE_BALLOT) && !defined(MONA_LYRA_ACTIVE_MASK) && \
    defined(MONA_LYRA_PREFETCH_ROWOUT) && !defined(MONA_LYRA_PREFETCH_NEXTCOL) && \
    !defined(MONA_LYRA_DUMMY_TAIL) && \
    !defined(MONA_LYRA_FULL_MASK) && !defined(MONA_LYRA_STATIC_SHARED64) && \
    !defined(MONA_LYRA_ROW3_FIRSTREAD) && defined(MONA_CUBE_IMAD_PHASE_MASK) && \
    (MONA_CUBE_IMAD_PHASE_MASK == 15)
inline MeasuredDefaults defaults_sm120() {
    constexpr Config tuned{1048576u,0u,false,64,32,64,64,256,-1};
    // Retain the Base configuration without certification. Runtime/build identity,
    // Base-specific evidence and personal-pool qualification remain separate gates.
    return {false,tuned,tuned,false};
}
#else
inline MeasuredDefaults defaults_sm120() { return {false,Config{},Config{},false}; }
#endif
}
