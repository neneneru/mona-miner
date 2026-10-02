// SPDX-License-Identifier: GPL-3.0-or-later
// Generated from pinned public upstream_lyra.cuh. Do not hand edit.
#pragma once
#include "transpose4.hpp"
namespace rdx {
constexpr int RDX_NCOL=4, RDX_NROW=4, RDX_MEMSHIFT=3;
template<class Ops>
RDX_INLINE void Gfunc_v5(typename Ops::Word &a, typename Ops::Word &b, typename Ops::Word &c, typename Ops::Word &d, Ops& ops)
{
	a += b; d ^= a; d = ops.rdx_rot64(d,32);
	c += d; b ^= c; b = ops.rdx_rot64(b,24);
	a += b; d ^= a; d = ops.rdx_rot64(d,16);
	c += d; b ^= c; b = ops.rdx_rot64(b,63);
}

template<class Ops>
RDX_INLINE void round_lyra_v5(typename Ops::Word s[4], Ops& ops)
{
	Gfunc_v5(s[0], s[1], s[2], s[3], ops);
	s[1] = ops.shuffle(s[1], +1);
	s[2] = ops.shuffle(s[2], +2);
	s[3] = ops.shuffle(s[3], +3);
	Gfunc_v5(s[0], s[1], s[2], s[3], ops);
	s[1] = ops.shuffle(s[1], +3);
	s[2] = ops.shuffle(s[2], +2);
	s[3] = ops.shuffle(s[3], +1);
}

template<class Ops>
RDX_INLINE void reduceDuplexRowSetup2(typename Ops::Word state[4]
, Ops& ops)
{
	typename Ops::Word state1[RDX_NCOL][3], state0[RDX_NCOL][3], state2[3];
	int i, j;

	#pragma unroll
	for (int i = 0; i < RDX_NCOL; i++)
	{
		#pragma unroll
		for (j = 0; j < 3; j++)
			state0[RDX_NCOL - i - 1][j] = state[j];
		round_lyra_v5(state, ops);
	}

	//#pragma unroll 4
	for (i = 0; i < RDX_NCOL; i++)
	{
		#pragma unroll
		for (j = 0; j < 3; j++)
			state[j] ^= state0[i][j];

		round_lyra_v5(state, ops);

		#pragma unroll
		for (j = 0; j < 3; j++)
			state1[RDX_NCOL - i - 1][j] = state0[i][j];

		#pragma unroll
		for (j = 0; j < 3; j++)
			state1[RDX_NCOL - i - 1][j] ^= state[j];
	}

	for (i = 0; i < RDX_NCOL; i++)
	{
		const uint32_t s0 = RDX_MEMSHIFT * RDX_NCOL * 0 + i * RDX_MEMSHIFT;
		const uint32_t s2 = RDX_MEMSHIFT * RDX_NCOL * 2 + RDX_MEMSHIFT * (RDX_NCOL - 1) - i*RDX_MEMSHIFT;

		#pragma unroll
		for (j = 0; j < 3; j++)
			state[j] ^= state1[i][j] + state0[i][j];

		round_lyra_v5(state, ops);

		#pragma unroll
		for (j = 0; j < 3; j++)
			state2[j] = state1[i][j];

		#pragma unroll
		for (j = 0; j < 3; j++)
			state2[j] ^= state[j];

		#pragma unroll
		for (j = 0; j < 3; j++)
			ops.store(s2 + j, state2[j]);

		typename Ops::Word Data0 = ops.shuffle(state[0], -1);
		typename Ops::Word Data1 = ops.shuffle(state[1], -1);
		typename Ops::Word Data2 = ops.shuffle(state[2], -1);

		ops.twist(state0[i], Data0, Data1, Data2);

		#pragma unroll
		for (j = 0; j < 3; j++)
			ops.store(s0 + j, state0[i][j]);

		#pragma unroll
		for (j = 0; j < 3; j++)
			state0[i][j] = state2[j];

	}

	for (i = 0; i < RDX_NCOL; i++)
	{
		const uint32_t s1 = RDX_MEMSHIFT * RDX_NCOL * 1 + i*RDX_MEMSHIFT;
		const uint32_t s3 = RDX_MEMSHIFT * RDX_NCOL * 3 + RDX_MEMSHIFT * (RDX_NCOL - 1) - i*RDX_MEMSHIFT;

		#pragma unroll
		for (j = 0; j < 3; j++)
			state[j] ^= state1[i][j] + state0[RDX_NCOL - i - 1][j];

		round_lyra_v5(state, ops);

		#pragma unroll
		for (j = 0; j < 3; j++)
			state0[RDX_NCOL - i - 1][j] ^= state[j];

		#pragma unroll
		for (j = 0; j < 3; j++) {
			ops.store(s3 + j, state0[RDX_NCOL - i - 1][j]);
		}

		typename Ops::Word Data0 = ops.shuffle(state[0], -1);
		typename Ops::Word Data1 = ops.shuffle(state[1], -1);
		typename Ops::Word Data2 = ops.shuffle(state[2], -1);

		ops.twist(state1[i], Data0, Data1, Data2);

		#pragma unroll
		for (j = 0; j < 3; j++)
			ops.store(s1 + j, state1[i][j]);
	}
}

template<class Ops>
RDX_INLINE void reduceDuplexRowt2(const int rowIn, const int rowInOut, const int rowOut, typename Ops::Word state[4], Ops& ops)
{
	typename Ops::Word state1[3], state2[3];
	typename Ops::Word state3[3];
	const uint32_t ps1 = RDX_MEMSHIFT * RDX_NCOL * rowIn;
	const uint32_t ps2 = RDX_MEMSHIFT * RDX_NCOL * rowInOut;
	const uint32_t ps3 = RDX_MEMSHIFT * RDX_NCOL * rowOut;


	for (int i = 0; i < RDX_NCOL; i++)
	{
		const uint32_t s1 = ps1 + i*RDX_MEMSHIFT;
		const uint32_t s2 = ps2 + i*RDX_MEMSHIFT;
		const uint32_t s3 = ps3 + i*RDX_MEMSHIFT;

		#pragma unroll
		for (int j = 0; j < 3; j++)
			state1[j] = ops.load(s1 + j);

		#pragma unroll
		for (int j = 0; j < 3; j++)
			state2[j] = ops.load(s2 + j);

		#pragma unroll
		for (int j = 0; j < 3; j++)
			state3[j] = ops.load(s3 + j);

		#pragma unroll
		for (int j = 0; j < 3; j++)
			state[j] ^= state1[j] + state2[j];


		round_lyra_v5(state, ops);

		typename Ops::Word Data0 = ops.shuffle(state[0], -1);
		typename Ops::Word Data1 = ops.shuffle(state[1], -1);
		typename Ops::Word Data2 = ops.shuffle(state[2], -1);

		ops.twist(state2, Data0, Data1, Data2);

		#pragma unroll
		for (int j = 0; j < 3; j++)
			ops.store(s2 + j, state2[j]);

		#pragma unroll
		for (int j = 0; j < 3; j++)
		{
			const typename Ops::Word prior = (rowOut == rowInOut) ? state2[j] : state3[j];
			ops.store(s3 + j, prior ^ state[j]);
		}

	}
}

template<class Ops>
RDX_INLINE void reduceDuplexRowt2x4(const int rowInOut, typename Ops::Word state[4], Ops& ops)
{
	const int rowIn = 2;
	const int rowOut = 3;

	int i, j;
	typename Ops::Word last[3];
	const uint32_t ps1 = RDX_MEMSHIFT * RDX_NCOL * rowIn;
	const uint32_t ps2 = RDX_MEMSHIFT * RDX_NCOL * rowInOut;

	#pragma unroll
	for (int j = 0; j < 3; j++)
		last[j] = ops.load(ps2 + j);

	#pragma unroll
	for (int j = 0; j < 3; j++)
		state[j] ^= ops.load(ps1 + j) + last[j];

	round_lyra_v5(state, ops);

	typename Ops::Word Data0 = ops.shuffle(state[0], -1);
	typename Ops::Word Data1 = ops.shuffle(state[1], -1);
	typename Ops::Word Data2 = ops.shuffle(state[2], -1);

	ops.twist(last, Data0, Data1, Data2);

	if (rowInOut == rowOut)
	{
		#pragma unroll
		for (j = 0; j < 3; j++)
			last[j] ^= state[j];
	}

	for (i = 1; i < RDX_NCOL; i++)
	{
		const uint32_t s1 = ps1 + i*RDX_MEMSHIFT;
		const uint32_t s2 = ps2 + i*RDX_MEMSHIFT;

		#pragma unroll
		for (j = 0; j < 3; j++)
			state[j] ^= ops.load(s1 + j) + ops.load(s2 + j);

		round_lyra_v5(state, ops);
	}

	#pragma unroll
	for (int j = 0; j < 3; j++)
		state[j] ^= last[j];
}
// Fixed Lyra2REv2 invocation T=1,R=4,C=4. Input and output are distributed
// across one complete four-lane group. Ops provides lane IV/basil and row broadcast.
template<class Ops>
RDX_INLINE typename Ops::Word lyra_word(typename Ops::Word input, Ops& ops) {
    typename Ops::Word s[4];
    s[0]=s[1]=input; s[2]=ops.iv(0); s[3]=ops.iv(1);
    #pragma unroll 1
    for(int r=0;r<12;++r) round_lyra_v5(s,ops);
    s[0]^=ops.basil(0);s[1]^=ops.basil(1);
    #pragma unroll 1
    for(int r=0;r<12;++r) round_lyra_v5(s,ops);
    reduceDuplexRowSetup2(s,ops);
    int prev=3;
    #pragma unroll 1
    for(int row=0;row<3;++row) {
        int rowa=ops.row(s[0]);
        reduceDuplexRowt2(prev,rowa,row,s,ops);
        prev=row;
    }
    reduceDuplexRowt2x4(ops.row(s[0]),s,ops);
    #pragma unroll 1
    for(int r=0;r<12;++r) round_lyra_v5(s,ops);
    return s[0];
}
}
