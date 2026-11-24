// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental 64-bit Threefish schedule for the two fixed Skein UBI blocks.

namespace skein_native {

using Word = uint64_t;

template<unsigned Rotation>
RDX_INLINE void mix(Word& a, Word& b) {
    a += b;
    b = ((b << Rotation) | (b >> (64 - Rotation))) ^ a;
}

template<unsigned R0, unsigned R1, unsigned R2, unsigned R3>
RDX_INLINE void round(Word& a, Word& b, Word& c, Word& d,
                      Word& e, Word& f, Word& g, Word& h) {
    mix<R0>(a, b);
    mix<R1>(c, d);
    mix<R2>(e, f);
    mix<R3>(g, h);
}

template<bool Second>
RDX_INLINE void four_rounds(Word (&s)[8]) {
    if constexpr (!Second) {
        round<46, 36, 19, 37>(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
        round<33, 27, 14, 42>(s[2], s[1], s[4], s[7], s[6], s[5], s[0], s[3]);
        round<17, 49, 36, 39>(s[4], s[1], s[6], s[3], s[0], s[5], s[2], s[7]);
        round<44,  9, 54, 56>(s[6], s[1], s[0], s[7], s[2], s[5], s[4], s[3]);
    } else {
        round<39, 30, 34, 24>(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
        round<13, 50, 10, 17>(s[2], s[1], s[4], s[7], s[6], s[5], s[0], s[3]);
        round<25, 29, 39, 43>(s[4], s[1], s[6], s[3], s[0], s[5], s[2], s[7]);
        round< 8, 35, 56, 22>(s[6], s[1], s[0], s[7], s[2], s[5], s[4], s[3]);
    }
}

template<unsigned Subkey, bool Message>
RDX_INLINE void inject(Word (&state)[8], const Word (&key)[9], unsigned base) {
    constexpr Word t0 = Message ? 32 : 8;
    constexpr Word t1 = Message ? 0xf000000000000000ULL : 0xff00000000000000ULL;
    constexpr Word tweak[3] = {t0, t1, t0 ^ t1};

    #pragma unroll
    for (unsigned i = 0; i < 5; ++i) state[i] += key[(Subkey + i) % 9];
    state[5] += key[(Subkey + 5) % 9] + tweak[Subkey % 3];
    state[6] += key[(Subkey + 6) % 9] + tweak[(Subkey + 1) % 3];
    state[7] += key[(Subkey + 7) % 9] + (base + Subkey);
}

template<unsigned Offset, bool Message>
RDX_INLINE void eight_rounds(Word (&state)[8], const Word (&key)[9], unsigned base) {
    four_rounds<false>(state);
    inject<Offset + 1, Message>(state, key, base);
    four_rounds<true>(state);
    inject<Offset + 2, Message>(state, key, base);
}

template<unsigned Offset, bool Message>
RDX_INLINE void full(Word (&state)[8], const Word (&key)[9]) {
    eight_rounds<Offset, Message>(state, key, 0);
    if constexpr (Offset < 16) full<Offset + 2, Message>(state, key);
}

RDX_INLINE void rotate_key(Word (&key)[9]) {
    // Advance by six subkeys. This keeps the tweak indices constant across
    // all three iterations and avoids a dynamically indexed private array.
    const Word first = key[0], second = key[1], third = key[2];
    const Word fourth = key[3], fifth = key[4], sixth = key[5];
    key[0] = key[6]; key[1] = key[7]; key[2] = key[8];
    key[3] = first; key[4] = second; key[5] = third;
    key[6] = fourth; key[7] = fifth; key[8] = sixth;
}

RDX_INLINE void output_rounds(Word (&state)[8], Word (&key)[9]) {
    #if SKEIN_LAYOUT == 2
    #pragma unroll 1
    for (unsigned base = 0; base < 18; base += 6) {
        eight_rounds<0, false>(state, key, base);
        eight_rounds<2, false>(state, key, base);
        eight_rounds<4, false>(state, key, base);
        rotate_key(key);
    }
    #else
    full<0, false>(state, key);
    #endif
}

} // namespace skein_native

__device__ __forceinline__ void r02_skein_registers(uint2 (&words)[4]) {
    using namespace skein_native;
    constexpr Word initial_key[9] = {
        0xccd044a12fdb3e13ULL, 0xe83590301a79a9ebULL, 0x55aea0614f816e6fULL,
        0x2a2767a4ae9b94dbULL, 0xec06025e74dd7683ULL, 0xe7a436cdc4746251ULL,
        0xc36fbaf9393ad185ULL, 0x3eedba1833edfc13ULL, 0xb69d3cfcc73a4e2aULL
    };
    Word input[4], state[8] = {};
    #pragma unroll
    for (unsigned i = 0; i < 4; ++i) {
        input[i] = devectorize(words[i]);
        state[i] = input[i];
    }
    inject<0, true>(state, initial_key, 0);
    full<0, true>(state, initial_key);

    Word key[9];
    key[8] = 0x1bd11bdaa9fc1a22ULL;
    #pragma unroll
    for (unsigned i = 0; i < 8; ++i) {
        key[i] = i < 4 ? state[i] ^ input[i] : state[i];
        key[8] ^= key[i];
        state[i] = 0;
    }
    inject<0, false>(state, key, 0);
    output_rounds(state, key);
    #pragma unroll
    for (unsigned i = 0; i < 4; ++i)
        words[i] = make_uint2(uint32_t(state[i]), uint32_t(state[i] >> 32));
}
