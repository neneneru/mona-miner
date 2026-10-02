// Derived from tpruvot/ccminer windows@6ff4e50987e59a70056324a94ed8667cc0bf598d.
// See upstream LICENSE.txt and per-file notices. Generated; do not hand edit.
#define Nrow 4
#define Ncol 4
#define memshift 3

__device__ uint2x4 *DMatrix;

__device__ __forceinline__ uint2 LD4S(const int index)
{
	extern __shared__ uint2 shared_mem[];
	return shared_mem[mona_lyra_shared_index(index)];
}

__device__ __forceinline__ void ST4S(const int index, const uint2 data)
{
	extern __shared__ uint2 shared_mem[];
	shared_mem[mona_lyra_shared_index(index)] = data;
}

__device__ __forceinline__ uint2 shuffle2(uint2 a, uint32_t b, uint32_t c MONA_LYRA_MEMBER_PARAM)
{
	return make_uint2(mona_shfl4(a.x, b, c MONA_LYRA_MEMBER_ARG), mona_shfl4(a.y, b, c MONA_LYRA_MEMBER_ARG));
}

__device__ __forceinline__
void Gfunc_v5(uint2 &a, uint2 &b, uint2 &c, uint2 &d)
{
	a += b; d ^= a; d = SWAPUINT2(d);
	c += d; b ^= c; b = ROR2(b, 24);
	a += b; d ^= a; d = ROR2(d, 16);
	c += d; b ^= c; b = ROR2(b, 63);
}

__device__ __forceinline__
void round_lyra_v5(uint2x4 s[4])
{
	Gfunc_v5(s[0].x, s[1].x, s[2].x, s[3].x);
	Gfunc_v5(s[0].y, s[1].y, s[2].y, s[3].y);
	Gfunc_v5(s[0].z, s[1].z, s[2].z, s[3].z);
	Gfunc_v5(s[0].w, s[1].w, s[2].w, s[3].w);

	Gfunc_v5(s[0].x, s[1].y, s[2].z, s[3].w);
	Gfunc_v5(s[0].y, s[1].z, s[2].w, s[3].x);
	Gfunc_v5(s[0].z, s[1].w, s[2].x, s[3].y);
	Gfunc_v5(s[0].w, s[1].x, s[2].y, s[3].z);
}

__device__ __forceinline__
void round_lyra_v5(uint2 s[4] MONA_LYRA_MEMBER_PARAM)
{
	Gfunc_v5(s[0], s[1], s[2], s[3]);
	s[1] = shuffle2(s[1], threadIdx.x + 1, 4 MONA_LYRA_MEMBER_ARG);
	s[2] = shuffle2(s[2], threadIdx.x + 2, 4 MONA_LYRA_MEMBER_ARG);
	s[3] = shuffle2(s[3], threadIdx.x + 3, 4 MONA_LYRA_MEMBER_ARG);
	Gfunc_v5(s[0], s[1], s[2], s[3]);
	s[1] = shuffle2(s[1], threadIdx.x + 3, 4 MONA_LYRA_MEMBER_ARG);
	s[2] = shuffle2(s[2], threadIdx.x + 2, 4 MONA_LYRA_MEMBER_ARG);
	s[3] = shuffle2(s[3], threadIdx.x + 1, 4 MONA_LYRA_MEMBER_ARG);
}

__device__ __forceinline__
void reduceDuplexRowSetup2(uint2 state[4] MONA_LYRA_MEMBER_PARAM
#if defined(MONA_LYRA_ROW3_FIRSTREAD)
	, mona_lyra_row3_firstread &row3_first
#endif
)
{
	uint2 state1[Ncol][3], state0[Ncol][3], state2[3];
	int i, j;

	#pragma unroll
	for (int i = 0; i < Ncol; i++)
	{
		#pragma unroll
		for (j = 0; j < 3; j++)
			state0[Ncol - i - 1][j] = state[j];
		round_lyra_v5(state MONA_LYRA_MEMBER_ARG);
	}

	//#pragma unroll 4
	for (i = 0; i < Ncol; i++)
	{
		#pragma unroll
		for (j = 0; j < 3; j++)
			state[j] ^= state0[i][j];

		round_lyra_v5(state MONA_LYRA_MEMBER_ARG);

		#pragma unroll
		for (j = 0; j < 3; j++)
			state1[Ncol - i - 1][j] = state0[i][j];

		#pragma unroll
		for (j = 0; j < 3; j++)
			state1[Ncol - i - 1][j] ^= state[j];
	}

	for (i = 0; i < Ncol; i++)
	{
		const uint32_t s0 = memshift * Ncol * 0 + i * memshift;
		const uint32_t s2 = memshift * Ncol * 2 + memshift * (Ncol - 1) - i*memshift;

		#pragma unroll
		for (j = 0; j < 3; j++)
			state[j] ^= state1[i][j] + state0[i][j];

		round_lyra_v5(state MONA_LYRA_MEMBER_ARG);

		#pragma unroll
		for (j = 0; j < 3; j++)
			state2[j] = state1[i][j];

		#pragma unroll
		for (j = 0; j < 3; j++)
			state2[j] ^= state[j];

		#pragma unroll
		for (j = 0; j < 3; j++)
			ST4S(s2 + j, state2[j]);

		uint2 Data0 = shuffle2(state[0], threadIdx.x - 1, 4 MONA_LYRA_MEMBER_ARG);
		uint2 Data1 = shuffle2(state[1], threadIdx.x - 1, 4 MONA_LYRA_MEMBER_ARG);
		uint2 Data2 = shuffle2(state[2], threadIdx.x - 1, 4 MONA_LYRA_MEMBER_ARG);

		if (threadIdx.x == 0) {
			state0[i][0] ^= Data2;
			state0[i][1] ^= Data0;
			state0[i][2] ^= Data1;
		} else {
			state0[i][0] ^= Data0;
			state0[i][1] ^= Data1;
			state0[i][2] ^= Data2;
		}

		#pragma unroll
		for (j = 0; j < 3; j++)
			ST4S(s0 + j, state0[i][j]);

		#pragma unroll
		for (j = 0; j < 3; j++)
			state0[i][j] = state2[j];

	}

	for (i = 0; i < Ncol; i++)
	{
		const uint32_t s1 = memshift * Ncol * 1 + i*memshift;
		const uint32_t s3 = memshift * Ncol * 3 + memshift * (Ncol - 1) - i*memshift;

		#pragma unroll
		for (j = 0; j < 3; j++)
			state[j] ^= state1[i][j] + state0[Ncol - i - 1][j];

		round_lyra_v5(state MONA_LYRA_MEMBER_ARG);

		#pragma unroll
		for (j = 0; j < 3; j++)
			state0[Ncol - i - 1][j] ^= state[j];

		#pragma unroll
		for (j = 0; j < 3; j++) {
#if defined(MONA_LYRA_ROW3_FIRSTREAD)
			row3_first.v[Ncol - i - 1][j] = state0[Ncol - i - 1][j];
#endif
			ST4S(s3 + j, state0[Ncol - i - 1][j]);
		}

		uint2 Data0 = shuffle2(state[0], threadIdx.x - 1, 4 MONA_LYRA_MEMBER_ARG);
		uint2 Data1 = shuffle2(state[1], threadIdx.x - 1, 4 MONA_LYRA_MEMBER_ARG);
		uint2 Data2 = shuffle2(state[2], threadIdx.x - 1, 4 MONA_LYRA_MEMBER_ARG);

		if (threadIdx.x == 0) {
			state1[i][0] ^= Data2;
			state1[i][1] ^= Data0;
			state1[i][2] ^= Data1;
		} else  {
			state1[i][0] ^= Data0;
			state1[i][1] ^= Data1;
			state1[i][2] ^= Data2;
		}

		#pragma unroll
		for (j = 0; j < 3; j++)
			ST4S(s1 + j, state1[i][j]);
	}
}

__device__
void reduceDuplexRowt2(const int rowIn, const int rowInOut, const int rowOut, uint2 state[4] MONA_LYRA_MEMBER_PARAM)
{
	uint2 state1[3], state2[3];
#if defined(MONA_LYRA_PREFETCH_NEXTCOL)
	uint2 state2_next[3];
#endif
#if defined(MONA_LYRA_PREFETCH_ROWOUT)
	uint2 state3[3];
#endif
	const uint32_t ps1 = memshift * Ncol * rowIn;
	const uint32_t ps2 = memshift * Ncol * rowInOut;
	const uint32_t ps3 = memshift * Ncol * rowOut;

#if defined(MONA_LYRA_PREFETCH_NEXTCOL)
	// Prime column 0. Later iterations consume values loaded during the
	// previous column's round.
	#pragma unroll
	for (int j = 0; j < 3; ++j)
		state1[j] = LD4S(ps1 + j);
	#pragma unroll
	for (int j = 0; j < 3; ++j)
		state2[j] = LD4S(ps2 + j);
#endif

	for (int i = 0; i < Ncol; i++)
	{
		const uint32_t s1 = ps1 + i*memshift;
		const uint32_t s2 = ps2 + i*memshift;
		const uint32_t s3 = ps3 + i*memshift;

#if !defined(MONA_LYRA_PREFETCH_NEXTCOL)
		#pragma unroll
		for (int j = 0; j < 3; j++)
			state1[j] = LD4S(s1 + j);

		#pragma unroll
		for (int j = 0; j < 3; j++)
			state2[j] = LD4S(s2 + j);
#endif

#if defined(MONA_LYRA_PREFETCH_ROWOUT)
		#pragma unroll
		for (int j = 0; j < 3; j++)
			state3[j] = LD4S(s3 + j);
#endif

		#pragma unroll
		for (int j = 0; j < 3; j++)
			state[j] ^= state1[j] + state2[j];

#if defined(MONA_LYRA_PREFETCH_NEXTCOL)
		// state1 is dead after the XOR, so reuse it for next-column rowIn.
		// state2 survives until the current-column store, so only next
		// rowInOut needs extra live storage.
		if (i + 1 < Ncol)
		{
			const uint32_t next_s1 = ps1 + uint32_t(i + 1) * memshift;
			const uint32_t next_s2 = ps2 + uint32_t(i + 1) * memshift;
			#pragma unroll
			for (int j = 0; j < 3; ++j)
				state1[j] = LD4S(next_s1 + j);
			#pragma unroll
			for (int j = 0; j < 3; ++j)
				state2_next[j] = LD4S(next_s2 + j);
		}
#endif

		round_lyra_v5(state MONA_LYRA_MEMBER_ARG);

		uint2 Data0 = shuffle2(state[0], threadIdx.x - 1, 4 MONA_LYRA_MEMBER_ARG);
		uint2 Data1 = shuffle2(state[1], threadIdx.x - 1, 4 MONA_LYRA_MEMBER_ARG);
		uint2 Data2 = shuffle2(state[2], threadIdx.x - 1, 4 MONA_LYRA_MEMBER_ARG);

		if (threadIdx.x == 0) {
			state2[0] ^= Data2;
			state2[1] ^= Data0;
			state2[2] ^= Data1;
		} else {
			state2[0] ^= Data0;
			state2[1] ^= Data1;
			state2[2] ^= Data2;
		}

		#pragma unroll
		for (int j = 0; j < 3; j++)
			ST4S(s2 + j, state2[j]);

		#pragma unroll
		for (int j = 0; j < 3; j++)
#if defined(MONA_LYRA_PREFETCH_ROWOUT)
		{
			const uint2 prior = (rowOut == rowInOut) ? state2[j] : state3[j];
			ST4S(s3 + j, prior ^ state[j]);
		}
#else
			ST4S(s3 + j, LD4S(s3 + j) ^ state[j]);
#endif
#if defined(MONA_LYRA_PREFETCH_NEXTCOL)
		if (i + 1 < Ncol)
		{
			#pragma unroll
			for (int j = 0; j < 3; ++j)
				state2[j] = state2_next[j];
		}
#endif

	}
}

__device__
void reduceDuplexRowt2x4(const int rowInOut, uint2 state[4] MONA_LYRA_MEMBER_PARAM)
{
	const int rowIn = 2;
	const int rowOut = 3;

	int i, j;
	uint2 last[3];
	const uint32_t ps1 = memshift * Ncol * rowIn;
	const uint32_t ps2 = memshift * Ncol * rowInOut;

	#pragma unroll
	for (int j = 0; j < 3; j++)
		last[j] = LD4S(ps2 + j);

	#pragma unroll
	for (int j = 0; j < 3; j++)
		state[j] ^= LD4S(ps1 + j) + last[j];

	round_lyra_v5(state MONA_LYRA_MEMBER_ARG);

	uint2 Data0 = shuffle2(state[0], threadIdx.x - 1, 4 MONA_LYRA_MEMBER_ARG);
	uint2 Data1 = shuffle2(state[1], threadIdx.x - 1, 4 MONA_LYRA_MEMBER_ARG);
	uint2 Data2 = shuffle2(state[2], threadIdx.x - 1, 4 MONA_LYRA_MEMBER_ARG);

	if (threadIdx.x == 0) {
		last[0] ^= Data2;
		last[1] ^= Data0;
		last[2] ^= Data1;
	} else {
		last[0] ^= Data0;
		last[1] ^= Data1;
		last[2] ^= Data2;
	}

	if (rowInOut == rowOut)
	{
		#pragma unroll
		for (j = 0; j < 3; j++)
			last[j] ^= state[j];
	}

	for (i = 1; i < Ncol; i++)
	{
		const uint32_t s1 = ps1 + i*memshift;
		const uint32_t s2 = ps2 + i*memshift;

		#pragma unroll
		for (j = 0; j < 3; j++)
			state[j] ^= LD4S(s1 + j) + LD4S(s2 + j);

		round_lyra_v5(state MONA_LYRA_MEMBER_ARG);
	}

	#pragma unroll
	for (int j = 0; j < 3; j++)
		state[j] ^= last[j];
}

__global__
__launch_bounds__(256)
void lyra2v2_gpu_hash_32_1(uint32_t threads, uint2 *inputHash)
{
	const uint32_t thread = blockDim.x * blockIdx.x + threadIdx.x;

	const uint2x4 blake2b_IV[2] = {
		0xf3bcc908UL, 0x6a09e667UL, 0x84caa73bUL, 0xbb67ae85UL,
		0xfe94f82bUL, 0x3c6ef372UL, 0x5f1d36f1UL, 0xa54ff53aUL,
		0xade682d1UL, 0x510e527fUL, 0x2b3e6c1fUL, 0x9b05688cUL,
		0xfb41bd6bUL, 0x1f83d9abUL, 0x137e2179UL, 0x5be0cd19UL
	};

	const uint2x4 Mask[2] = {
		0x00000020UL, 0x00000000UL, 0x00000020UL, 0x00000000UL,
		0x00000020UL, 0x00000000UL, 0x00000001UL, 0x00000000UL,
		0x00000004UL, 0x00000000UL, 0x00000004UL, 0x00000000UL,
		0x00000080UL, 0x00000000UL, 0x00000000UL, 0x01000000UL
	};

	uint2x4 state[4];

	if (thread < threads)
	{
		state[0].x = state[1].x = __ldg(&inputHash[thread + threads * 0]);
		state[0].y = state[1].y = __ldg(&inputHash[thread + threads * 1]);
		state[0].z = state[1].z = __ldg(&inputHash[thread + threads * 2]);
		state[0].w = state[1].w = __ldg(&inputHash[thread + threads * 3]);
		state[2] = blake2b_IV[0];
		state[3] = blake2b_IV[1];

		for (int i = 0; i<12; i++)
			round_lyra_v5(state);

		state[0] ^= Mask[0];
		state[1] ^= Mask[1];

		for (int i = 0; i<12; i++)
			round_lyra_v5(state);

		DMatrix[mona_lyra_stride(threads) * 0 + thread] = state[0];
		DMatrix[mona_lyra_stride(threads) * 1 + thread] = state[1];
		DMatrix[mona_lyra_stride(threads) * 2 + thread] = state[2];
		DMatrix[mona_lyra_stride(threads) * 3 + thread] = state[3];
	}
}

__global__
__launch_bounds__(256)
void lyra2v2_gpu_hash_32_2(uint32_t threads)
{
	const uint32_t thread = blockDim.y * blockIdx.x + threadIdx.y;
#if defined(MONA_LYRA_DUMMY_TAIL)
	const bool live = thread < threads;
	if (true) // all warp lanes remain participants; invalid groups use dummy state
#elif defined(MONA_LYRA_STABLE_BALLOT)
	const bool live = thread < threads;
	const unsigned mona_members = mona_lyra_members(live);

	if (live)
#else
	if (thread < threads)
#endif
	{
		uint2 state[4];
#if defined(MONA_LYRA_DUMMY_TAIL)
		if (live) {
			state[0] = ((uint2*)DMatrix)[(0 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x];
			state[1] = ((uint2*)DMatrix)[(1 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x];
			state[2] = ((uint2*)DMatrix)[(2 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x];
			state[3] = ((uint2*)DMatrix)[(3 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x];
		} else {
			#pragma unroll
			for (int k = 0; k < 4; ++k) state[k] = make_uint2(0u, 0u);
		}
#else
		state[0] = ((uint2*)DMatrix)[(0 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x];
		state[1] = ((uint2*)DMatrix)[(1 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x];
		state[2] = ((uint2*)DMatrix)[(2 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x];
		state[3] = ((uint2*)DMatrix)[(3 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x];
#endif

#if defined(MONA_LYRA_ROW3_FIRSTREAD)
		mona_lyra_row3_firstread row3_first;
		reduceDuplexRowSetup2(state MONA_LYRA_MEMBER_ARG, row3_first);

		uint32_t rowa = mona_shfl4(state[0].x, 0, 4 MONA_LYRA_MEMBER_ARG) & 3;
		mona_reduceDuplexRowt2_firstread(rowa, state MONA_LYRA_MEMBER_ARG, row3_first);
		int prev = 0;
		for (int i = 1; i < 3; i++)
		{
			rowa = mona_shfl4(state[0].x, 0, 4 MONA_LYRA_MEMBER_ARG) & 3;
			reduceDuplexRowt2(prev, rowa, i, state MONA_LYRA_MEMBER_ARG);
			prev = i;
		}
#else
		reduceDuplexRowSetup2(state MONA_LYRA_MEMBER_ARG);

		uint32_t rowa;
		int prev = 3;

		for (int i = 0; i < 3; i++)
		{
			rowa = mona_shfl4(state[0].x, 0, 4 MONA_LYRA_MEMBER_ARG) & 3;
			reduceDuplexRowt2(prev, rowa, i, state MONA_LYRA_MEMBER_ARG);
			prev = i;
		}
#endif

		rowa = mona_shfl4(state[0].x, 0, 4 MONA_LYRA_MEMBER_ARG) & 3;
		reduceDuplexRowt2x4(rowa, state MONA_LYRA_MEMBER_ARG);

#if defined(MONA_LYRA_DUMMY_TAIL)
		if (live) {
			((uint2*)DMatrix)[(0 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x] = state[0];
			((uint2*)DMatrix)[(1 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x] = state[1];
			((uint2*)DMatrix)[(2 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x] = state[2];
			((uint2*)DMatrix)[(3 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x] = state[3];
		}
#else
		((uint2*)DMatrix)[(0 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x] = state[0];
		((uint2*)DMatrix)[(1 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x] = state[1];
		((uint2*)DMatrix)[(2 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x] = state[2];
		((uint2*)DMatrix)[(3 * mona_lyra_stride(threads) + thread) * blockDim.x + threadIdx.x] = state[3];
#endif
	}
}

__global__
__launch_bounds__(256)
void lyra2v2_gpu_hash_32_3(uint32_t threads, uint2 *outputHash)
{
	const uint32_t thread = blockDim.x * blockIdx.x + threadIdx.x;

	uint2x4 state[4];

	if (thread < threads)
	{
		state[0] = __ldg4(&DMatrix[mona_lyra_stride(threads) * 0 + thread]);
		state[1] = __ldg4(&DMatrix[mona_lyra_stride(threads) * 1 + thread]);
		state[2] = __ldg4(&DMatrix[mona_lyra_stride(threads) * 2 + thread]);
		state[3] = __ldg4(&DMatrix[mona_lyra_stride(threads) * 3 + thread]);

		for (int i = 0; i < 12; i++)
			round_lyra_v5(state);

		outputHash[thread + threads * 0] = state[0].x;
		outputHash[thread + threads * 1] = state[0].y;
		outputHash[thread + threads * 2] = state[0].z;
		outputHash[thread + threads * 3] = state[0].w;
	}
}
