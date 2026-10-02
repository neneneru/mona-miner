// Candidate N02+EXP01 rebase. Full 14-round Blake result and mathematical
// 24-round Keccak outputs 0..3 preserved; round 23 is sliced to that contract.
// Three first-round nonce-independent column Gs are evaluated once per immutable job.
#pragma once
__device__ __constant__ uint32_t n02_job_columns[12];

static void __forceinline__ __device__ n02_keccak_23_full_plus_terminal4(uint2 *s)
{
	uint2 bc[5], tmpxor[5], u, v;
	//	uint2 s[25];

	#pragma unroll 1
	for (int i = 0; i < 23; i++)
	{
		#pragma unroll
		for (uint32_t x = 0; x < 5; x++)
			tmpxor[x] = s[x] ^ s[x + 5] ^ s[x + 10] ^ s[x + 15] ^ s[x + 20];

		bc[0] = tmpxor[0] ^ ROL2(tmpxor[2], 1);
		bc[1] = tmpxor[1] ^ ROL2(tmpxor[3], 1);
		bc[2] = tmpxor[2] ^ ROL2(tmpxor[4], 1);
		bc[3] = tmpxor[3] ^ ROL2(tmpxor[0], 1);
		bc[4] = tmpxor[4] ^ ROL2(tmpxor[1], 1);

		u = s[1] ^ bc[0];

		s[0] ^= bc[4];
		s[1] = ROL2(s[6] ^ bc[0], 44);
		s[6] = ROL2(s[9] ^ bc[3], 20);
		s[9] = ROL2(s[22] ^ bc[1], 61);
		s[22] = ROL2(s[14] ^ bc[3], 39);
		s[14] = ROL2(s[20] ^ bc[4], 18);
		s[20] = ROL2(s[2] ^ bc[1], 62);
		s[2] = ROL2(s[12] ^ bc[1], 43);
		s[12] = ROL2(s[13] ^ bc[2], 25);
		s[13] = ROL8(s[19] ^ bc[3]);
		s[19] = ROR8(s[23] ^ bc[2]);
		s[23] = ROL2(s[15] ^ bc[4], 41);
		s[15] = ROL2(s[4] ^ bc[3], 27);
		s[4] = ROL2(s[24] ^ bc[3], 14);
		s[24] = ROL2(s[21] ^ bc[0], 2);
		s[21] = ROL2(s[8] ^ bc[2], 55);
		s[8] = ROL2(s[16] ^ bc[0], 45);
		s[16] = ROL2(s[5] ^ bc[4], 36);
		s[5] = ROL2(s[3] ^ bc[2], 28);
		s[3] = ROL2(s[18] ^ bc[2], 21);
		s[18] = ROL2(s[17] ^ bc[1], 15);
		s[17] = ROL2(s[11] ^ bc[0], 10);
		s[11] = ROL2(s[7] ^ bc[1], 6);
		s[7] = ROL2(s[10] ^ bc[4], 3);
		s[10] = ROL2(u, 1);

		u = s[0]; v = s[1]; s[0] ^= (~v) & s[2]; s[1] ^= (~s[2]) & s[3]; s[2] ^= (~s[3]) & s[4]; s[3] ^= (~s[4]) & u; s[4] ^= (~u) & v;
		u = s[5]; v = s[6]; s[5] ^= (~v) & s[7]; s[6] ^= (~s[7]) & s[8]; s[7] ^= (~s[8]) & s[9]; s[8] ^= (~s[9]) & u; s[9] ^= (~u) & v;
		u = s[10]; v = s[11]; s[10] ^= (~v) & s[12]; s[11] ^= (~s[12]) & s[13]; s[12] ^= (~s[13]) & s[14]; s[13] ^= (~s[14]) & u; s[14] ^= (~u) & v;
		u = s[15]; v = s[16]; s[15] ^= (~v) & s[17]; s[16] ^= (~s[17]) & s[18]; s[17] ^= (~s[18]) & s[19]; s[18] ^= (~s[19]) & u; s[19] ^= (~u) & v;
		u = s[20]; v = s[21]; s[20] ^= (~v) & s[22]; s[21] ^= (~s[22]) & s[23]; s[22] ^= (~s[23]) & s[24]; s[23] ^= (~s[24]) & u; s[24] ^= (~u) & v;
		s[0] ^= keccak_round_constants35[i];
	}


	// Round 23 (the 24th round): retain all theta inputs, then materialize
	// only rho/pi row 0 and chi outputs 0..3 required by the 4x64 contract.
	#pragma unroll
	for (uint32_t x = 0; x < 5; x++)
		tmpxor[x] = s[x] ^ s[x + 5] ^ s[x + 10] ^ s[x + 15] ^ s[x + 20];

	bc[0] = tmpxor[0] ^ ROL2(tmpxor[2], 1); // D[1]
	bc[1] = tmpxor[1] ^ ROL2(tmpxor[3], 1); // D[2]
	bc[2] = tmpxor[2] ^ ROL2(tmpxor[4], 1); // D[3]
	bc[3] = tmpxor[3] ^ ROL2(tmpxor[0], 1); // D[4]
	bc[4] = tmpxor[4] ^ ROL2(tmpxor[1], 1); // D[0]

	const uint2 b0 = s[0] ^ bc[4];
	const uint2 b1 = ROL2(s[6] ^ bc[0], 44);
	const uint2 b2 = ROL2(s[12] ^ bc[1], 43);
	const uint2 b3 = ROL2(s[18] ^ bc[2], 21);
	const uint2 b4 = ROL2(s[24] ^ bc[3], 14);

	s[0] = b0 ^ ((~b1) & b2);
	s[1] = b1 ^ ((~b2) & b3);
	s[2] = b2 ^ ((~b3) & b4);
	s[3] = b3 ^ ((~b4) & b0);
	s[0] ^= keccak_round_constants35[23];
}

extern "C" __global__ __launch_bounds__(64)
void n02_blake_job_columns_b64(const uint32_t threads, const uint32_t *noncePtr, uint32_t * Hash, uint32_t mona_one)
{
	uint32_t thread = (blockDim.x * blockIdx.x + threadIdx.x);
	if (thread < threads)
	{
		const uint32_t nonce = *noncePtr + thread;
		const uint32_t T0 = 640;

		uint32_t h[8];
		#pragma unroll 8
		for (int i = 0; i<8; i++) { h[i] = cpu_h[i]; }

		uint32_t v[16];

		const uint32_t c_Padding[12] = {
			0x80000000, 0, 0, 0,
			0, 0, 0, 0,
			0, 1, 0, 640
		};

		const uint32_t  u256[16] = {
			0x243F6A88, 0x85A308D3,
			0x13198A2E, 0x03707344,
			0xA4093822, 0x299F31D0,
			0x082EFA98, 0xEC4E6C89,
			0x452821E6, 0x38D01377,
			0xBE5466CF, 0x34E90C6C,
			0xC0AC29B7, 0xC97C50DD,
			0x3F84D5B5, 0xB5470917
		};

		uint32_t m[16] = {
			c_data[0], c_data[1], c_data[2], nonce,
			c_Padding[0], c_Padding[1], c_Padding[2], c_Padding[3],
			c_Padding[4], c_Padding[5], c_Padding[6], c_Padding[7],
			c_Padding[8], c_Padding[9], c_Padding[10], c_Padding[11]
		};

		#pragma unroll 8
		for (int i = 0; i < 8; i++)
			v[i] = h[i];

		v[8] = u256[0];
		v[9] = u256[1];
		v[10] = u256[2];
		v[11] = u256[3];
		v[12] = u256[4] ^ T0;
		v[13] = u256[5] ^ T0;
		v[14] = u256[6];
		v[15] = u256[7];

		//	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
		v[0] = n02_job_columns[0];
		v[4] = n02_job_columns[1];
		v[8] = n02_job_columns[2];
		v[12] = n02_job_columns[3];
		v[2] = n02_job_columns[4];
		v[6] = n02_job_columns[5];
		v[10] = n02_job_columns[6];
		v[14] = n02_job_columns[7];
		v[3] = n02_job_columns[8];
		v[7] = n02_job_columns[9];
		v[11] = n02_job_columns[10];
		v[15] = n02_job_columns[11];
		GSPREC(1, 5, 0x9, 0xD, 2, 3);
		GSPREC(0, 5, 0xA, 0xF, 8, 9);
		GSPREC(1, 6, 0xB, 0xC, 10, 11);
		GSPREC(2, 7, 0x8, 0xD, 12, 13);
		GSPREC(3, 4, 0x9, 0xE, 14, 15);
		//	{ 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
		GSPREC(0, 4, 0x8, 0xC, 14, 10);
		GSPREC(1, 5, 0x9, 0xD, 4, 8);
		GSPREC(2, 6, 0xA, 0xE, 9, 15);
		GSPREC(3, 7, 0xB, 0xF, 13, 6);
		GSPREC(0, 5, 0xA, 0xF, 1, 12);
		GSPREC(1, 6, 0xB, 0xC, 0, 2);
		GSPREC(2, 7, 0x8, 0xD, 11, 7);
		GSPREC(3, 4, 0x9, 0xE, 5, 3);
		//	{ 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
		GSPREC(0, 4, 0x8, 0xC, 11, 8);
		GSPREC(1, 5, 0x9, 0xD, 12, 0);
		GSPREC(2, 6, 0xA, 0xE, 5, 2);
		GSPREC(3, 7, 0xB, 0xF, 15, 13);
		GSPREC(0, 5, 0xA, 0xF, 10, 14);
		GSPREC(1, 6, 0xB, 0xC, 3, 6);
		GSPREC(2, 7, 0x8, 0xD, 7, 1);
		GSPREC(3, 4, 0x9, 0xE, 9, 4);
		//	{ 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
		GSPREC(0, 4, 0x8, 0xC, 7, 9);
		GSPREC(1, 5, 0x9, 0xD, 3, 1);
		GSPREC(2, 6, 0xA, 0xE, 13, 12);
		GSPREC(3, 7, 0xB, 0xF, 11, 14);
		GSPREC(0, 5, 0xA, 0xF, 2, 6);
		GSPREC(1, 6, 0xB, 0xC, 5, 10);
		GSPREC(2, 7, 0x8, 0xD, 4, 0);
		GSPREC(3, 4, 0x9, 0xE, 15, 8);
		//	{ 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
		GSPREC(0, 4, 0x8, 0xC, 9, 0);
		GSPREC(1, 5, 0x9, 0xD, 5, 7);
		GSPREC(2, 6, 0xA, 0xE, 2, 4);
		GSPREC(3, 7, 0xB, 0xF, 10, 15);
		GSPREC(0, 5, 0xA, 0xF, 14, 1);
		GSPREC(1, 6, 0xB, 0xC, 11, 12);
		GSPREC(2, 7, 0x8, 0xD, 6, 8);
		GSPREC(3, 4, 0x9, 0xE, 3, 13);
		//	{ 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
		GSPREC(0, 4, 0x8, 0xC, 2, 12);
		GSPREC(1, 5, 0x9, 0xD, 6, 10);
		GSPREC(2, 6, 0xA, 0xE, 0, 11);
		GSPREC(3, 7, 0xB, 0xF, 8, 3);
		GSPREC(0, 5, 0xA, 0xF, 4, 13);
		GSPREC(1, 6, 0xB, 0xC, 7, 5);
		GSPREC(2, 7, 0x8, 0xD, 15, 14);
		GSPREC(3, 4, 0x9, 0xE, 1, 9);
		//	{ 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
		GSPREC(0, 4, 0x8, 0xC, 12, 5);
		GSPREC(1, 5, 0x9, 0xD, 1, 15);
		GSPREC(2, 6, 0xA, 0xE, 14, 13);
		GSPREC(3, 7, 0xB, 0xF, 4, 10);
		GSPREC(0, 5, 0xA, 0xF, 0, 7);
		GSPREC(1, 6, 0xB, 0xC, 6, 3);
		GSPREC(2, 7, 0x8, 0xD, 9, 2);
		GSPREC(3, 4, 0x9, 0xE, 8, 11);
		//	{ 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
		GSPREC(0, 4, 0x8, 0xC, 13, 11);
		GSPREC(1, 5, 0x9, 0xD, 7, 14);
		GSPREC(2, 6, 0xA, 0xE, 12, 1);
		GSPREC(3, 7, 0xB, 0xF, 3, 9);
		GSPREC(0, 5, 0xA, 0xF, 5, 0);
		GSPREC(1, 6, 0xB, 0xC, 15, 4);
		GSPREC(2, 7, 0x8, 0xD, 8, 6);
		GSPREC(3, 4, 0x9, 0xE, 2, 10);
		//	{ 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
		GSPREC(0, 4, 0x8, 0xC, 6, 15);
		GSPREC(1, 5, 0x9, 0xD, 14, 9);
		GSPREC(2, 6, 0xA, 0xE, 11, 3);
		GSPREC(3, 7, 0xB, 0xF, 0, 8);
		GSPREC(0, 5, 0xA, 0xF, 12, 2);
		GSPREC(1, 6, 0xB, 0xC, 13, 7);
		GSPREC(2, 7, 0x8, 0xD, 1, 4);
		GSPREC(3, 4, 0x9, 0xE, 10, 5);
		//	{ 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
		GSPREC(0, 4, 0x8, 0xC, 10, 2);
		GSPREC(1, 5, 0x9, 0xD, 8, 4);
		GSPREC(2, 6, 0xA, 0xE, 7, 6);
		GSPREC(3, 7, 0xB, 0xF, 1, 5);
		GSPREC(0, 5, 0xA, 0xF, 15, 11);
		GSPREC(1, 6, 0xB, 0xC, 9, 14);
		GSPREC(2, 7, 0x8, 0xD, 3, 12);
		GSPREC(3, 4, 0x9, 0xE, 13, 0);
		//	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
		GSPREC(0, 4, 0x8, 0xC, 0, 1);
		GSPREC(1, 5, 0x9, 0xD, 2, 3);
		GSPREC(2, 6, 0xA, 0xE, 4, 5);
		GSPREC(3, 7, 0xB, 0xF, 6, 7);
		GSPREC(0, 5, 0xA, 0xF, 8, 9);
		GSPREC(1, 6, 0xB, 0xC, 10, 11);
		GSPREC(2, 7, 0x8, 0xD, 12, 13);
		GSPREC(3, 4, 0x9, 0xE, 14, 15);
		//	{ 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
		GSPREC(0, 4, 0x8, 0xC, 14, 10);
		GSPREC(1, 5, 0x9, 0xD, 4, 8);
		GSPREC(2, 6, 0xA, 0xE, 9, 15);
		GSPREC(3, 7, 0xB, 0xF, 13, 6);
		GSPREC(0, 5, 0xA, 0xF, 1, 12);
		GSPREC(1, 6, 0xB, 0xC, 0, 2);
		GSPREC(2, 7, 0x8, 0xD, 11, 7);
		GSPREC(3, 4, 0x9, 0xE, 5, 3);
		//	{ 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
		GSPREC(0, 4, 0x8, 0xC, 11, 8);
		GSPREC(1, 5, 0x9, 0xD, 12, 0);
		GSPREC(2, 6, 0xA, 0xE, 5, 2);
		GSPREC(3, 7, 0xB, 0xF, 15, 13);
		GSPREC(0, 5, 0xA, 0xF, 10, 14);
		GSPREC(1, 6, 0xB, 0xC, 3, 6);
		GSPREC(2, 7, 0x8, 0xD, 7, 1);
		GSPREC(3, 4, 0x9, 0xE, 9, 4);
		//	{ 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
		GSPREC(0, 4, 0x8, 0xC, 7, 9);
		GSPREC(1, 5, 0x9, 0xD, 3, 1);
		GSPREC(2, 6, 0xA, 0xE, 13, 12);
		GSPREC(3, 7, 0xB, 0xF, 11, 14);
		GSPREC(0, 5, 0xA, 0xF, 2, 6);
		GSPREC(1, 6, 0xB, 0xC, 5, 10);
		GSPREC(2, 7, 0x8, 0xD, 4, 0);
		GSPREC(3, 4, 0x9, 0xE, 15, 8);

		h[0] = cuda_swab32(h[0] ^ v[0] ^ v[8]);
		h[1] = cuda_swab32(h[1] ^ v[1] ^ v[9]);
		h[2] = cuda_swab32(h[2] ^ v[2] ^ v[10]);
		h[3] = cuda_swab32(h[3] ^ v[3] ^ v[11]);
		h[4] = cuda_swab32(h[4] ^ v[4] ^ v[12]);
		h[5] = cuda_swab32(h[5] ^ v[5] ^ v[13]);
		h[6] = cuda_swab32(h[6] ^ v[6] ^ v[14]);
		h[7] = cuda_swab32(h[7] ^ v[7] ^ v[15]);

		uint2 keccak_gpu_state[25] = { 0 };
		keccak_gpu_state[0].x = h[0];
		keccak_gpu_state[0].y = h[1];
		keccak_gpu_state[1].x = h[2];
		keccak_gpu_state[1].y = h[3];
		keccak_gpu_state[2].x = h[4];
		keccak_gpu_state[2].y = h[5];
		keccak_gpu_state[3].x = h[6];
		keccak_gpu_state[3].y = h[7];
		keccak_gpu_state[4] = UINT2(1, 0);

		keccak_gpu_state[16] = UINT2(0, 0x80000000);
		n02_keccak_23_full_plus_terminal4(keccak_gpu_state);

		uint64_t *outputHash = (uint64_t *)Hash;
		#pragma unroll 4
		for (int i = 0; i<4; i++)
			outputHash[i*threads + thread] = devectorize(keccak_gpu_state[i]);
	}
}
