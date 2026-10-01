#pragma once
#include "cuda_lyra2_vectors.h"

#if defined(MONA_LYRA_DUMMY_TAIL) && !defined(MONA_LYRA_FULL_MASK)
#error "MONA_LYRA_DUMMY_TAIL requires MONA_LYRA_FULL_MASK"
#endif
#if defined(MONA_LYRA_DUMMY_TAIL) && (defined(MONA_LYRA_ACTIVE_MASK) || defined(MONA_LYRA_STABLE_BALLOT))
#error "MONA_LYRA_DUMMY_TAIL is exclusive with ACTIVE_MASK and STABLE_BALLOT"
#endif

__host__ __device__ constexpr uint32_t mona_lyra_stride(uint32_t n)
{ return (n + 31u) & ~31u; }

#if defined(MONA_LYRA_STABLE_BALLOT)
#define MONA_LYRA_MEMBER_PARAM , unsigned mona_members
#define MONA_LYRA_MEMBER_ARG , mona_members
__device__ __forceinline__ unsigned mona_lyra_members(bool live)
{
    // Whole warps reach this point before any tail exit. threadIdx.x is the
    // four-lane hash lane and `live` depends only on threadIdx.y, so this
    // ballot is a union of complete 4-lane hash groups.
    return __ballot_sync(0xffffffffu, live);
}
#else
#define MONA_LYRA_MEMBER_PARAM
#define MONA_LYRA_MEMBER_ARG
#endif

__device__ __forceinline__ unsigned mona_group_mask(
#if defined(MONA_LYRA_STABLE_BALLOT)
    unsigned mona_members
#endif
)
{
#if defined(MONA_LYRA_STABLE_BALLOT)
    return mona_members;
#elif defined(MONA_LYRA_FULL_MASK)
    return 0xffffffffu;
#elif defined(MONA_LYRA_ACTIVE_MASK)
    // Measured performance reference only; not a logical-membership proof.
    return __activemask();
#else
    // Deterministic subgroup mask: exactly the four lanes of this hash.
    unsigned linear=threadIdx.x+blockDim.x*threadIdx.y;
    return 0xFu << (linear & 28u);
#endif
}
__device__ __forceinline__ uint32_t mona_shfl4(
    uint32_t v, uint32_t source, uint32_t MONA_LYRA_MEMBER_PARAM)
{
    return __shfl_sync(mona_group_mask(
#if defined(MONA_LYRA_STABLE_BALLOT)
        mona_members
#endif
    ), v, int(source & 3u), 4);
}

__device__ __forceinline__ uint32_t mona_lyra_shared_index(uint32_t index)
{
#if defined(MONA_LYRA_STATIC_SHARED64)
    return index * 64u + threadIdx.y * 4u + threadIdx.x;
#else
    return (index * blockDim.y + threadIdx.y) * blockDim.x + threadIdx.x;
#endif
}

#if defined(MONA_LYRA_ROW3_FIRSTREAD)
struct mona_lyra_row3_firstread
{
    uint2 v[4][3];
};
__device__ __forceinline__ void mona_reduceDuplexRowt2_firstread(
    int rowInOut, uint2 state[4] MONA_LYRA_MEMBER_PARAM,
    const mona_lyra_row3_firstread &row3);
#endif

#include "upstream_lyra.cuh"

#if defined(MONA_LYRA_ROW3_FIRSTREAD)
// The first wandering call is always rowIn=3,rowOut=0. Setup already owns
// the final row3 values in registers immediately before writing them to
// shared memory. Carry those values into this one call and avoid the fixed
// row3 LDS traffic. Logical row3 remains in shared for every later dynamic
// rowInOut selection, so shared footprint and occupancy remain unchanged.
__device__ __forceinline__ void mona_reduceDuplexRowt2_firstread(
    int rowInOut, uint2 state[4] MONA_LYRA_MEMBER_PARAM,
    const mona_lyra_row3_firstread &row3)
{
    uint2 state1[3], state2[3];
#if defined(MONA_LYRA_PREFETCH_ROWOUT)
    uint2 state3[3];
#endif
    const uint32_t ps2 = memshift * Ncol * uint32_t(rowInOut);
    constexpr uint32_t ps3 = 0; // rowOut = 0

    for (int i = 0; i < Ncol; ++i)
    {
        const uint32_t s2 = ps2 + uint32_t(i) * memshift;
        const uint32_t s3 = ps3 + uint32_t(i) * memshift;

        #pragma unroll
        for (int j = 0; j < 3; ++j)
            state1[j] = row3.v[i][j];

        #pragma unroll
        for (int j = 0; j < 3; ++j)
            state2[j] = LD4S(s2 + j);

#if defined(MONA_LYRA_PREFETCH_ROWOUT)
        #pragma unroll
        for (int j = 0; j < 3; ++j)
            state3[j] = LD4S(s3 + j);
#endif

        #pragma unroll
        for (int j = 0; j < 3; ++j)
            state[j] ^= state1[j] + state2[j];

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
        for (int j = 0; j < 3; ++j)
            ST4S(s2 + j, state2[j]);

        #pragma unroll
        for (int j = 0; j < 3; ++j)
#if defined(MONA_LYRA_PREFETCH_ROWOUT)
        {
            const uint2 prior = (rowInOut == 0) ? state2[j] : state3[j];
            ST4S(s3 + j, prior ^ state[j]);
        }
#else
            ST4S(s3 + j, LD4S(s3 + j) ^ state[j]);
#endif
    }
}
#endif

// One entire Lyra2REv2 (T=1,R=4,C=4) invocation per group of four lanes.
// The 1536-byte/hash matrix remains shared. Only the four-lane distributed
// sponge state stays in registers; there is no global DMatrix intermediate.
template<int Block>
__global__ __launch_bounds__(Block)
void mona_lyra_fused(uint32_t n, uint2 *hash)
{
    static_assert(Block%32==0 && Block<=256, "whole warps, <=99KiB shared/block");
    const uint32_t t=blockIdx.x*blockDim.y+threadIdx.y;
#if defined(MONA_LYRA_DUMMY_TAIL)
    // FULL_MASK is safe only when every physical lane in the warp reaches
    // every cooperative shuffle. Invalid hash groups therefore execute the
    // complete Lyra path using deterministic register/shared-memory state.
    // They perform no global hash load/store and are discarded at the end.
    const bool live=t<n;
#elif defined(MONA_LYRA_STABLE_BALLOT)
    const bool live=t<n;
    const unsigned mona_members=mona_lyra_members(live);
    if(!live) return; // membership captured before any tail exit.
#else
    if(t>=n) return; // All four lanes of a hash take the same branch.
#endif
    const unsigned lane=threadIdx.x;
    const uint2 iv[8] = {
        {0xf3bcc908u,0x6a09e667u},{0x84caa73bu,0xbb67ae85u},
        {0xfe94f82bu,0x3c6ef372u},{0x5f1d36f1u,0xa54ff53au},
        {0xade682d1u,0x510e527fu},{0x2b3e6c1fu,0x9b05688cu},
        {0xfb41bd6bu,0x1f83d9abu},{0x137e2179u,0x5be0cd19u}
    };
    const uint2 basil[8] = {
        {32,0},{32,0},{32,0},{1,0},
        {4,0},{4,0},{0x80,0},{0,0x01000000u}
    };
    uint2 s[4];
#if defined(MONA_LYRA_DUMMY_TAIL)
    if(live) s[0]=s[1]=hash[t+size_t(lane)*n];
    else s[0]=s[1]=make_uint2(0u,0u);
#else
    s[0]=s[1]=hash[t+size_t(lane)*n];
#endif
    s[2]=iv[lane]; s[3]=iv[lane+4];
    #pragma unroll 1
    for(int r=0;r<12;++r) round_lyra_v5(s MONA_LYRA_MEMBER_ARG);
    s[0]^=basil[lane]; s[1]^=basil[lane+4];
    #pragma unroll 1
    for(int r=0;r<12;++r) round_lyra_v5(s MONA_LYRA_MEMBER_ARG);
#if defined(MONA_LYRA_ROW3_FIRSTREAD)
    mona_lyra_row3_firstread row3_first;
    reduceDuplexRowSetup2(s MONA_LYRA_MEMBER_ARG,row3_first);
    unsigned rowa=mona_shfl4(s[0].x,0,4 MONA_LYRA_MEMBER_ARG)&3u;
    mona_reduceDuplexRowt2_firstread(int(rowa),s MONA_LYRA_MEMBER_ARG,row3_first);
    int prev=0;
    #pragma unroll 1
    for(int row=1;row<3;++row) {
        rowa=mona_shfl4(s[0].x,0,4 MONA_LYRA_MEMBER_ARG)&3u;
        reduceDuplexRowt2(prev,int(rowa),row,s MONA_LYRA_MEMBER_ARG);
        prev=row;
    }
    // The final wandering step selects its row from the state AFTER row 2.
    rowa=mona_shfl4(s[0].x,0,4 MONA_LYRA_MEMBER_ARG)&3u;
#else
    reduceDuplexRowSetup2(s MONA_LYRA_MEMBER_ARG);
    int prev=3;
    #pragma unroll 1
    for(int row=0;row<3;++row) {
        unsigned rowa=mona_shfl4(s[0].x,0,4 MONA_LYRA_MEMBER_ARG)&3u;
        reduceDuplexRowt2(prev,int(rowa),row,s MONA_LYRA_MEMBER_ARG);
        prev=row;
    }
    unsigned rowa=mona_shfl4(s[0].x,0,4 MONA_LYRA_MEMBER_ARG)&3u;
#endif
    reduceDuplexRowt2x4(int(rowa),s MONA_LYRA_MEMBER_ARG);
    #pragma unroll 1
    for(int r=0;r<12;++r) round_lyra_v5(s MONA_LYRA_MEMBER_ARG);
#if defined(MONA_LYRA_DUMMY_TAIL)
    if(live) hash[t+size_t(lane)*n]=s[0];
#else
    hash[t+size_t(lane)*n]=s[0];
#endif
}
