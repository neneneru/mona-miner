// Derived from tpruvot/ccminer windows@6ff4e50987e59a70056324a94ed8667cc0bf598d.
// See upstream LICENSE.txt and per-file notices. Generated; do not hand edit.
#include "cuda_helper.h"

\
__device__ __forceinline__ uint32_t mona_cube_force_imad_u32(uint32_t a, uint32_t b, uint32_t one)
{
    uint32_t out;
    asm volatile ("mad.lo.u32 %0, %1, %2, %3;" : "=r"(out) : "r"(a), "r"(one), "r"(b));
    return out;
}

#if (MONA_CUBE_IMAD_PHASE_MASK & 1)
#define MONA_CUBE_ADD_PHASE0(a, b, one) mona_cube_force_imad_u32((a), (b), (one))
#else
#define MONA_CUBE_ADD_PHASE0(a, b, one) ((a) + (b))
#endif
#if (MONA_CUBE_IMAD_PHASE_MASK & 2)
#define MONA_CUBE_ADD_PHASE1(a, b, one) mona_cube_force_imad_u32((a), (b), (one))
#else
#define MONA_CUBE_ADD_PHASE1(a, b, one) ((a) + (b))
#endif
#if (MONA_CUBE_IMAD_PHASE_MASK & 4)
#define MONA_CUBE_ADD_PHASE2(a, b, one) mona_cube_force_imad_u32((a), (b), (one))
#else
#define MONA_CUBE_ADD_PHASE2(a, b, one) ((a) + (b))
#endif
#if (MONA_CUBE_IMAD_PHASE_MASK & 8)
#define MONA_CUBE_ADD_PHASE3(a, b, one) mona_cube_force_imad_u32((a), (b), (one))
#else
#define MONA_CUBE_ADD_PHASE3(a, b, one) ((a) + (b))
#endif


#define CUBEHASH_ROUNDS 16 /* this is r for CubeHashr/b */
#define CUBEHASH_BLOCKBYTES 32 /* this is b for CubeHashr/b */

#ifdef __INTELLISENSE__
/* just for vstudio code colors */
#define __CUDA_ARCH__ 520
#endif

#if __CUDA_ARCH__ < 350
#define LROT(x,bits) ((x << bits) | (x >> (32 - bits)))
#else
#define LROT(x, bits) __funnelshift_l(x, x, bits)
#endif



#define ROTATEUPWARDS7(a)  LROT(a,7)
#define ROTATEUPWARDS11(a) LROT(a,11)

__device__ __forceinline__ void rrounds(uint32_t x[2][2][2][2][2], uint32_t mona_one)
{
	int r;

	uint32_t x0[2][2][2][2];
	uint32_t x1[2][2][2][2];

	for (r = 0; r < CUBEHASH_ROUNDS; r += 2) {
		/* "rotate x_0jklm upwards by 7 bits" */
		x0[0][0][0][0] = ROTATEUPWARDS7(x[0][0][0][0][0]);
		x0[0][0][0][1] = ROTATEUPWARDS7(x[0][0][0][0][1]);
		x0[0][0][1][0] = ROTATEUPWARDS7(x[0][0][0][1][0]);
		x0[0][0][1][1] = ROTATEUPWARDS7(x[0][0][0][1][1]);
		x0[0][1][0][0] = ROTATEUPWARDS7(x[0][0][1][0][0]);
		x0[0][1][0][1] = ROTATEUPWARDS7(x[0][0][1][0][1]);
		x0[0][1][1][0] = ROTATEUPWARDS7(x[0][0][1][1][0]);
		x0[0][1][1][1] = ROTATEUPWARDS7(x[0][0][1][1][1]);
		x0[1][0][0][0] = ROTATEUPWARDS7(x[0][1][0][0][0]);
		x0[1][0][0][1] = ROTATEUPWARDS7(x[0][1][0][0][1]);
		x0[1][0][1][0] = ROTATEUPWARDS7(x[0][1][0][1][0]);
		x0[1][0][1][1] = ROTATEUPWARDS7(x[0][1][0][1][1]);
		x0[1][1][0][0] = ROTATEUPWARDS7(x[0][1][1][0][0]);
		x0[1][1][0][1] = ROTATEUPWARDS7(x[0][1][1][0][1]);
		x0[1][1][1][0] = ROTATEUPWARDS7(x[0][1][1][1][0]);
		x0[1][1][1][1] = ROTATEUPWARDS7(x[0][1][1][1][1]);

		/* "add x_0jklm into x_1jklm modulo 2^32" */
		x1[0][0][0][0] = MONA_CUBE_ADD_PHASE0(x[1][0][0][0][0], x[0][0][0][0][0], mona_one);
		x1[0][0][0][1] = MONA_CUBE_ADD_PHASE0(x[1][0][0][0][1], x[0][0][0][0][1], mona_one);
		x1[0][0][1][0] = MONA_CUBE_ADD_PHASE0(x[1][0][0][1][0], x[0][0][0][1][0], mona_one);
		x1[0][0][1][1] = MONA_CUBE_ADD_PHASE0(x[1][0][0][1][1], x[0][0][0][1][1], mona_one);
		x1[0][1][0][0] = MONA_CUBE_ADD_PHASE0(x[1][0][1][0][0], x[0][0][1][0][0], mona_one);
		x1[0][1][0][1] = MONA_CUBE_ADD_PHASE0(x[1][0][1][0][1], x[0][0][1][0][1], mona_one);
		x1[0][1][1][0] = MONA_CUBE_ADD_PHASE0(x[1][0][1][1][0], x[0][0][1][1][0], mona_one);
		x1[0][1][1][1] = MONA_CUBE_ADD_PHASE0(x[1][0][1][1][1], x[0][0][1][1][1], mona_one);
		x1[1][0][0][0] = MONA_CUBE_ADD_PHASE0(x[1][1][0][0][0], x[0][1][0][0][0], mona_one);
		x1[1][0][0][1] = MONA_CUBE_ADD_PHASE0(x[1][1][0][0][1], x[0][1][0][0][1], mona_one);
		x1[1][0][1][0] = MONA_CUBE_ADD_PHASE0(x[1][1][0][1][0], x[0][1][0][1][0], mona_one);
		x1[1][0][1][1] = MONA_CUBE_ADD_PHASE0(x[1][1][0][1][1], x[0][1][0][1][1], mona_one);
		x1[1][1][0][0] = MONA_CUBE_ADD_PHASE0(x[1][1][1][0][0], x[0][1][1][0][0], mona_one);
		x1[1][1][0][1] = MONA_CUBE_ADD_PHASE0(x[1][1][1][0][1], x[0][1][1][0][1], mona_one);
		x1[1][1][1][0] = MONA_CUBE_ADD_PHASE0(x[1][1][1][1][0], x[0][1][1][1][0], mona_one);
		x1[1][1][1][1] = MONA_CUBE_ADD_PHASE0(x[1][1][1][1][1], x[0][1][1][1][1], mona_one);
		/* "xor x_1~jklm into x_0jklm" */
		x[0][0][0][0][0] = x0[0][0][0][0] ^ x1[1][0][0][0];
		x[0][0][0][0][1] = x0[0][0][0][1] ^ x1[1][0][0][1];
		x[0][0][0][1][0] = x0[0][0][1][0] ^ x1[1][0][1][0];
		x[0][0][0][1][1] = x0[0][0][1][1] ^ x1[1][0][1][1];
		x[0][0][1][0][0] = x0[0][1][0][0] ^ x1[1][1][0][0];
		x[0][0][1][0][1] = x0[0][1][0][1] ^ x1[1][1][0][1];
		x[0][0][1][1][0] = x0[0][1][1][0] ^ x1[1][1][1][0];
		x[0][0][1][1][1] = x0[0][1][1][1] ^ x1[1][1][1][1];
		x[0][1][0][0][0] = x0[1][0][0][0] ^ x1[0][0][0][0];
		x[0][1][0][0][1] = x0[1][0][0][1] ^ x1[0][0][0][1];
		x[0][1][0][1][0] = x0[1][0][1][0] ^ x1[0][0][1][0];
		x[0][1][0][1][1] = x0[1][0][1][1] ^ x1[0][0][1][1];
		x[0][1][1][0][0] = x0[1][1][0][0] ^ x1[0][1][0][0];
		x[0][1][1][0][1] = x0[1][1][0][1] ^ x1[0][1][0][1];
		x[0][1][1][1][0] = x0[1][1][1][0] ^ x1[0][1][1][0];
		x[0][1][1][1][1] = x0[1][1][1][1] ^ x1[0][1][1][1];

		/* "rotate x_0jklm upwards by 11 bits" */
		x0[0][0][0][0] = ROTATEUPWARDS11(x[0][0][0][0][0]);
		x0[0][0][0][1] = ROTATEUPWARDS11(x[0][0][0][0][1]);
		x0[0][0][1][0] = ROTATEUPWARDS11(x[0][0][0][1][0]);
		x0[0][0][1][1] = ROTATEUPWARDS11(x[0][0][0][1][1]);
		x0[0][1][0][0] = ROTATEUPWARDS11(x[0][0][1][0][0]);
		x0[0][1][0][1] = ROTATEUPWARDS11(x[0][0][1][0][1]);
		x0[0][1][1][0] = ROTATEUPWARDS11(x[0][0][1][1][0]);
		x0[0][1][1][1] = ROTATEUPWARDS11(x[0][0][1][1][1]);
		x0[1][0][0][0] = ROTATEUPWARDS11(x[0][1][0][0][0]);
		x0[1][0][0][1] = ROTATEUPWARDS11(x[0][1][0][0][1]);
		x0[1][0][1][0] = ROTATEUPWARDS11(x[0][1][0][1][0]);
		x0[1][0][1][1] = ROTATEUPWARDS11(x[0][1][0][1][1]);
		x0[1][1][0][0] = ROTATEUPWARDS11(x[0][1][1][0][0]);
		x0[1][1][0][1] = ROTATEUPWARDS11(x[0][1][1][0][1]);
		x0[1][1][1][0] = ROTATEUPWARDS11(x[0][1][1][1][0]);
		x0[1][1][1][1] = ROTATEUPWARDS11(x[0][1][1][1][1]);

		/* "add x_0jklm into x_1~jk~lm modulo 2^32" */
		x[1][1][0][1][0] = MONA_CUBE_ADD_PHASE1(x1[1][0][1][0], x[0][0][0][0][0], mona_one);
		x[1][1][0][1][1] = MONA_CUBE_ADD_PHASE1(x1[1][0][1][1], x[0][0][0][0][1], mona_one);
		x[1][1][0][0][0] = MONA_CUBE_ADD_PHASE1(x1[1][0][0][0], x[0][0][0][1][0], mona_one);
		x[1][1][0][0][1] = MONA_CUBE_ADD_PHASE1(x1[1][0][0][1], x[0][0][0][1][1], mona_one);
		x[1][1][1][1][0] = MONA_CUBE_ADD_PHASE1(x1[1][1][1][0], x[0][0][1][0][0], mona_one);
		x[1][1][1][1][1] = MONA_CUBE_ADD_PHASE1(x1[1][1][1][1], x[0][0][1][0][1], mona_one);
		x[1][1][1][0][0] = MONA_CUBE_ADD_PHASE1(x1[1][1][0][0], x[0][0][1][1][0], mona_one);
		x[1][1][1][0][1] = MONA_CUBE_ADD_PHASE1(x1[1][1][0][1], x[0][0][1][1][1], mona_one);
		x[1][0][0][1][0] = MONA_CUBE_ADD_PHASE1(x1[0][0][1][0], x[0][1][0][0][0], mona_one);
		x[1][0][0][1][1] = MONA_CUBE_ADD_PHASE1(x1[0][0][1][1], x[0][1][0][0][1], mona_one);
		x[1][0][0][0][0] = MONA_CUBE_ADD_PHASE1(x1[0][0][0][0], x[0][1][0][1][0], mona_one);
		x[1][0][0][0][1] = MONA_CUBE_ADD_PHASE1(x1[0][0][0][1], x[0][1][0][1][1], mona_one);
		x[1][0][1][1][0] = MONA_CUBE_ADD_PHASE1(x1[0][1][1][0], x[0][1][1][0][0], mona_one);
		x[1][0][1][1][1] = MONA_CUBE_ADD_PHASE1(x1[0][1][1][1], x[0][1][1][0][1], mona_one);
		x[1][0][1][0][0] = MONA_CUBE_ADD_PHASE1(x1[0][1][0][0], x[0][1][1][1][0], mona_one);
		x[1][0][1][0][1] = MONA_CUBE_ADD_PHASE1(x1[0][1][0][1], x[0][1][1][1][1], mona_one);
		/* "xor x_1~j~k~lm into x_0jklm" */
		x[0][0][0][0][0] = x0[0][0][0][0] ^ x[1][1][1][1][0];
		x[0][0][0][0][1] = x0[0][0][0][1] ^ x[1][1][1][1][1];
		x[0][0][0][1][0] = x0[0][0][1][0] ^ x[1][1][1][0][0];
		x[0][0][0][1][1] = x0[0][0][1][1] ^ x[1][1][1][0][1];
		x[0][0][1][0][0] = x0[0][1][0][0] ^ x[1][1][0][1][0];
		x[0][0][1][0][1] = x0[0][1][0][1] ^ x[1][1][0][1][1];
		x[0][0][1][1][0] = x0[0][1][1][0] ^ x[1][1][0][0][0];
		x[0][0][1][1][1] = x0[0][1][1][1] ^ x[1][1][0][0][1];
		x[0][1][0][0][0] = x0[1][0][0][0] ^ x[1][0][1][1][0];
		x[0][1][0][0][1] = x0[1][0][0][1] ^ x[1][0][1][1][1];
		x[0][1][0][1][0] = x0[1][0][1][0] ^ x[1][0][1][0][0];
		x[0][1][0][1][1] = x0[1][0][1][1] ^ x[1][0][1][0][1];
		x[0][1][1][0][0] = x0[1][1][0][0] ^ x[1][0][0][1][0];
		x[0][1][1][0][1] = x0[1][1][0][1] ^ x[1][0][0][1][1];
		x[0][1][1][1][0] = x0[1][1][1][0] ^ x[1][0][0][0][0];
		x[0][1][1][1][1] = x0[1][1][1][1] ^ x[1][0][0][0][1];

		/* "rotate x_0jklm upwards by 7 bits" */
		x0[0][0][0][0] = ROTATEUPWARDS7(x[0][0][0][0][0]);
		x0[0][0][0][1] = ROTATEUPWARDS7(x[0][0][0][0][1]);
		x0[0][0][1][0] = ROTATEUPWARDS7(x[0][0][0][1][0]);
		x0[0][0][1][1] = ROTATEUPWARDS7(x[0][0][0][1][1]);
		x0[0][1][0][0] = ROTATEUPWARDS7(x[0][0][1][0][0]);
		x0[0][1][0][1] = ROTATEUPWARDS7(x[0][0][1][0][1]);
		x0[0][1][1][0] = ROTATEUPWARDS7(x[0][0][1][1][0]);
		x0[0][1][1][1] = ROTATEUPWARDS7(x[0][0][1][1][1]);
		x0[1][0][0][0] = ROTATEUPWARDS7(x[0][1][0][0][0]);
		x0[1][0][0][1] = ROTATEUPWARDS7(x[0][1][0][0][1]);
		x0[1][0][1][0] = ROTATEUPWARDS7(x[0][1][0][1][0]);
		x0[1][0][1][1] = ROTATEUPWARDS7(x[0][1][0][1][1]);
		x0[1][1][0][0] = ROTATEUPWARDS7(x[0][1][1][0][0]);
		x0[1][1][0][1] = ROTATEUPWARDS7(x[0][1][1][0][1]);
		x0[1][1][1][0] = ROTATEUPWARDS7(x[0][1][1][1][0]);
		x0[1][1][1][1] = ROTATEUPWARDS7(x[0][1][1][1][1]);

		/* "add x_0jklm into x_1~j~k~l~m modulo 2^32" */
		x1[1][1][1][1] = MONA_CUBE_ADD_PHASE2(x[1][1][1][1][1], x[0][0][0][0][0], mona_one);
		x1[1][1][1][0] = MONA_CUBE_ADD_PHASE2(x[1][1][1][1][0], x[0][0][0][0][1], mona_one);
		x1[1][1][0][1] = MONA_CUBE_ADD_PHASE2(x[1][1][1][0][1], x[0][0][0][1][0], mona_one);
		x1[1][1][0][0] = MONA_CUBE_ADD_PHASE2(x[1][1][1][0][0], x[0][0][0][1][1], mona_one);
		x1[1][0][1][1] = MONA_CUBE_ADD_PHASE2(x[1][1][0][1][1], x[0][0][1][0][0], mona_one);
		x1[1][0][1][0] = MONA_CUBE_ADD_PHASE2(x[1][1][0][1][0], x[0][0][1][0][1], mona_one);
		x1[1][0][0][1] = MONA_CUBE_ADD_PHASE2(x[1][1][0][0][1], x[0][0][1][1][0], mona_one);
		x1[1][0][0][0] = MONA_CUBE_ADD_PHASE2(x[1][1][0][0][0], x[0][0][1][1][1], mona_one);
		x1[0][1][1][1] = MONA_CUBE_ADD_PHASE2(x[1][0][1][1][1], x[0][1][0][0][0], mona_one);
		x1[0][1][1][0] = MONA_CUBE_ADD_PHASE2(x[1][0][1][1][0], x[0][1][0][0][1], mona_one);
		x1[0][1][0][1] = MONA_CUBE_ADD_PHASE2(x[1][0][1][0][1], x[0][1][0][1][0], mona_one);
		x1[0][1][0][0] = MONA_CUBE_ADD_PHASE2(x[1][0][1][0][0], x[0][1][0][1][1], mona_one);
		x1[0][0][1][1] = MONA_CUBE_ADD_PHASE2(x[1][0][0][1][1], x[0][1][1][0][0], mona_one);
		x1[0][0][1][0] = MONA_CUBE_ADD_PHASE2(x[1][0][0][1][0], x[0][1][1][0][1], mona_one);
		x1[0][0][0][1] = MONA_CUBE_ADD_PHASE2(x[1][0][0][0][1], x[0][1][1][1][0], mona_one);
		x1[0][0][0][0] = MONA_CUBE_ADD_PHASE2(x[1][0][0][0][0], x[0][1][1][1][1], mona_one);
		/* "xor x_1j~k~l~m into x_0jklm" */
		x[0][0][0][0][0] = x0[0][0][0][0] ^ x1[0][1][1][1];
		x[0][0][0][0][1] = x0[0][0][0][1] ^ x1[0][1][1][0];
		x[0][0][0][1][0] = x0[0][0][1][0] ^ x1[0][1][0][1];
		x[0][0][0][1][1] = x0[0][0][1][1] ^ x1[0][1][0][0];
		x[0][0][1][0][0] = x0[0][1][0][0] ^ x1[0][0][1][1];
		x[0][0][1][0][1] = x0[0][1][0][1] ^ x1[0][0][1][0];
		x[0][0][1][1][0] = x0[0][1][1][0] ^ x1[0][0][0][1];
		x[0][0][1][1][1] = x0[0][1][1][1] ^ x1[0][0][0][0];
		x[0][1][0][0][0] = x0[1][0][0][0] ^ x1[1][1][1][1];
		x[0][1][0][0][1] = x0[1][0][0][1] ^ x1[1][1][1][0];
		x[0][1][0][1][0] = x0[1][0][1][0] ^ x1[1][1][0][1];
		x[0][1][0][1][1] = x0[1][0][1][1] ^ x1[1][1][0][0];
		x[0][1][1][0][0] = x0[1][1][0][0] ^ x1[1][0][1][1];
		x[0][1][1][0][1] = x0[1][1][0][1] ^ x1[1][0][1][0];
		x[0][1][1][1][0] = x0[1][1][1][0] ^ x1[1][0][0][1];
		x[0][1][1][1][1] = x0[1][1][1][1] ^ x1[1][0][0][0];

		/* "rotate x_0jklm upwards by 11 bits" */
		x0[0][0][0][0] = ROTATEUPWARDS11(x[0][0][0][0][0]);
		x0[0][0][0][1] = ROTATEUPWARDS11(x[0][0][0][0][1]);
		x0[0][0][1][0] = ROTATEUPWARDS11(x[0][0][0][1][0]);
		x0[0][0][1][1] = ROTATEUPWARDS11(x[0][0][0][1][1]);
		x0[0][1][0][0] = ROTATEUPWARDS11(x[0][0][1][0][0]);
		x0[0][1][0][1] = ROTATEUPWARDS11(x[0][0][1][0][1]);
		x0[0][1][1][0] = ROTATEUPWARDS11(x[0][0][1][1][0]);
		x0[0][1][1][1] = ROTATEUPWARDS11(x[0][0][1][1][1]);
		x0[1][0][0][0] = ROTATEUPWARDS11(x[0][1][0][0][0]);
		x0[1][0][0][1] = ROTATEUPWARDS11(x[0][1][0][0][1]);
		x0[1][0][1][0] = ROTATEUPWARDS11(x[0][1][0][1][0]);
		x0[1][0][1][1] = ROTATEUPWARDS11(x[0][1][0][1][1]);
		x0[1][1][0][0] = ROTATEUPWARDS11(x[0][1][1][0][0]);
		x0[1][1][0][1] = ROTATEUPWARDS11(x[0][1][1][0][1]);
		x0[1][1][1][0] = ROTATEUPWARDS11(x[0][1][1][1][0]);
		x0[1][1][1][1] = ROTATEUPWARDS11(x[0][1][1][1][1]);

		/* "add x_0jklm into x_1j~kl~m modulo 2^32" */
		x[1][0][1][0][1] = MONA_CUBE_ADD_PHASE3(x1[0][1][0][1], x[0][0][0][0][0], mona_one);
		x[1][0][1][0][0] = MONA_CUBE_ADD_PHASE3(x1[0][1][0][0], x[0][0][0][0][1], mona_one);
		x[1][0][1][1][1] = MONA_CUBE_ADD_PHASE3(x1[0][1][1][1], x[0][0][0][1][0], mona_one);
		x[1][0][1][1][0] = MONA_CUBE_ADD_PHASE3(x1[0][1][1][0], x[0][0][0][1][1], mona_one);
		x[1][0][0][0][1] = MONA_CUBE_ADD_PHASE3(x1[0][0][0][1], x[0][0][1][0][0], mona_one);
		x[1][0][0][0][0] = MONA_CUBE_ADD_PHASE3(x1[0][0][0][0], x[0][0][1][0][1], mona_one);
		x[1][0][0][1][1] = MONA_CUBE_ADD_PHASE3(x1[0][0][1][1], x[0][0][1][1][0], mona_one);
		x[1][0][0][1][0] = MONA_CUBE_ADD_PHASE3(x1[0][0][1][0], x[0][0][1][1][1], mona_one);
		x[1][1][1][0][1] = MONA_CUBE_ADD_PHASE3(x1[1][1][0][1], x[0][1][0][0][0], mona_one);
		x[1][1][1][0][0] = MONA_CUBE_ADD_PHASE3(x1[1][1][0][0], x[0][1][0][0][1], mona_one);
		x[1][1][1][1][1] = MONA_CUBE_ADD_PHASE3(x1[1][1][1][1], x[0][1][0][1][0], mona_one);
		x[1][1][1][1][0] = MONA_CUBE_ADD_PHASE3(x1[1][1][1][0], x[0][1][0][1][1], mona_one);
		x[1][1][0][0][1] = MONA_CUBE_ADD_PHASE3(x1[1][0][0][1], x[0][1][1][0][0], mona_one);
		x[1][1][0][0][0] = MONA_CUBE_ADD_PHASE3(x1[1][0][0][0], x[0][1][1][0][1], mona_one);
		x[1][1][0][1][1] = MONA_CUBE_ADD_PHASE3(x1[1][0][1][1], x[0][1][1][1][0], mona_one);
		x[1][1][0][1][0] = MONA_CUBE_ADD_PHASE3(x1[1][0][1][0], x[0][1][1][1][1], mona_one);
		/* "xor x_1jkl~m into x_0jklm" */
		x[0][0][0][0][0] = x0[0][0][0][0] ^ x[1][0][0][0][1];
		x[0][0][0][0][1] = x0[0][0][0][1] ^ x[1][0][0][0][0];
		x[0][0][0][1][0] = x0[0][0][1][0] ^ x[1][0][0][1][1];
		x[0][0][0][1][1] = x0[0][0][1][1] ^ x[1][0][0][1][0];
		x[0][0][1][0][0] = x0[0][1][0][0] ^ x[1][0][1][0][1];
		x[0][0][1][0][1] = x0[0][1][0][1] ^ x[1][0][1][0][0];
		x[0][0][1][1][0] = x0[0][1][1][0] ^ x[1][0][1][1][1];
		x[0][0][1][1][1] = x0[0][1][1][1] ^ x[1][0][1][1][0];
		x[0][1][0][0][0] = x0[1][0][0][0] ^ x[1][1][0][0][1];
		x[0][1][0][0][1] = x0[1][0][0][1] ^ x[1][1][0][0][0];
		x[0][1][0][1][0] = x0[1][0][1][0] ^ x[1][1][0][1][1];
		x[0][1][0][1][1] = x0[1][0][1][1] ^ x[1][1][0][1][0];
		x[0][1][1][0][0] = x0[1][1][0][0] ^ x[1][1][1][0][1];
		x[0][1][1][0][1] = x0[1][1][0][1] ^ x[1][1][1][0][0];
		x[0][1][1][1][0] = x0[1][1][1][0] ^ x[1][1][1][1][1];
		x[0][1][1][1][1] = x0[1][1][1][1] ^ x[1][1][1][1][0];
	}
}

__device__ __forceinline__
void Final(uint32_t x[2][2][2][2][2], uint32_t *hashval, uint32_t mona_one)
{
	/* "the integer 1 is xored into the last state word x_11111" */
	x[1][1][1][1][1] ^= 1U;

	/* "the state is then transformed invertibly through 10r identical rounds" */
	for (int i = 0; i < 10; ++i) rrounds(x, mona_one);

	/* "output the first h/8 bytes of the state" */
	hashval[0] = x[0][0][0][0][0];
	hashval[1] = x[0][0][0][0][1];
	hashval[2] = x[0][0][0][1][0];
	hashval[3] = x[0][0][0][1][1];
	hashval[4] = x[0][0][1][0][0];
	hashval[5] = x[0][0][1][0][1];
	hashval[6] = x[0][0][1][1][0];
	hashval[7] = x[0][0][1][1][1];
}
