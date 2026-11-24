/* SPDX-License-Identifier: GPL-3.0-or-later
 * Offline CPU oracle using the repository's vendored Lyra2REv2 primitives.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sph/sph_blake.h"
#include "sph/sph_keccak.h"
#include "sph/sph_cubehash.h"
#include "sph/sph_skein.h"
#include "sph/sph_bmw.h"
#include "lyra2/Lyra2.h"

static uint64_t reference_upper(const uint32_t words[20], uint32_t nonce) {
    uint8_t header[80], a[32], b[32];
    for (unsigned i = 0; i < 20; ++i) {
        uint32_t word = i == 19 ? nonce : words[i];
        for (unsigned j = 0; j < 4; ++j) header[4*i+j] = (uint8_t)(word >> (24-8*j));
    }
    sph_blake256_context blake;
    sph_keccak256_context keccak;
    sph_cubehash256_context cube;
    sph_skein256_context skein;
    sph_bmw256_context bmw;
    sph_blake256_init(&blake);
    sph_blake256(&blake, header, sizeof(header));
    sph_blake256_close(&blake, a);
    sph_keccak256_init(&keccak);
    sph_keccak256(&keccak, a, 32);
    sph_keccak256_close(&keccak, b);
    sph_cubehash256_init(&cube);
    sph_cubehash256(&cube, b, 32);
    sph_cubehash256_close(&cube, a);
    if (LYRA2(b, 32, a, 32, a, 32, 1, 4, 4) != 0) abort();
    sph_skein256_init(&skein);
    sph_skein256(&skein, b, 32);
    sph_skein256_close(&skein, a);
    sph_cubehash256_init(&cube);
    sph_cubehash256(&cube, a, 32);
    sph_cubehash256_close(&cube, b);
    sph_bmw256_init(&bmw);
    sph_bmw256(&bmw, b, 32);
    sph_bmw256_close(&bmw, a);
    uint64_t high = 0;
    for (unsigned i = 0; i < 8; ++i) high |= (uint64_t)a[24+i] << (8*i);
    return high;
}

int main(int argc, char **argv) {
    const unsigned small[] = {1, 7, 33, 63, 64, 65, 129, 257};
    const unsigned extended[] = {1, 7, 31, 32, 33, 63, 64, 65, 95, 96, 97, 127, 128, 129,
                                 191, 192, 193, 255, 256, 257, 1023, 1024, 1025, 4095, 4096, 8193};
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--extended") != 0)) {
        fputs("usage: oracle [--extended]\n", stderr);
        return 2;
    }
    const unsigned *sizes = argc == 2 ? extended : small;
    const unsigned cases = argc == 2 ? sizeof(extended) / sizeof(*extended)
                                     : sizeof(small) / sizeof(*small);
    sph_blake256_set_rounds(14);
    putchar('[');
    for (unsigned index = 0; index < cases; ++index) {
        const unsigned n = sizes[index];
        const uint32_t first = n % 2 ? UINT32_MAX - n + 1 : 96;
        uint64_t seed = n + 0x12345678U;
        uint32_t words[20];
        printf("%s{\"first\":%" PRIu32 ",\"count\":%u,\"words\":[", index ? "," : "", first, n);
        for (unsigned i = 0; i < 20; ++i) {
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            words[i] = (uint32_t)seed;
            printf("%s%" PRIu32, i ? "," : "", words[i]);
        }
        printf("],\"upper\":[");
        for (unsigned i = 0; i < n; ++i) {
            printf("%s%" PRIu64, i ? "," : "", reference_upper(words, first + i));
        }
        printf("]}");
    }
    puts("]");
}
