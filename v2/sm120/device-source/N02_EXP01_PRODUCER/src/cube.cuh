#pragma once
#include "upstream_cube.cuh"

// Fixed 32-byte CubeHash-256. Upstream round arithmetic, caller-owned registers.
// mona_one stays runtime-visible so selected add phases cannot fold IMAD back to IADD.
__device__ __forceinline__ void mona_cube32(uint2 *h, uint32_t mona_one)
{
    uint32_t x[2][2][2][2][2] = {
        0xEA2BD4B4,0xCCD6F29F,0x63117E71,0x35481EAE,
        0x22512D5B,0xE5D94E63,0x7E624131,0xF4CC12BE,
        0xC2D0B696,0x42AF2070,0xD0720C35,0x3361DA8C,
        0x28CCECA4,0x8EF8AD83,0x4680AC00,0x40E5FBAB,
        0xD89041C3,0x6107FBD5,0x6C859D41,0xF0B26679,
        0x09392549,0x5FA25603,0x65C892FD,0x93CB6285,
        0x2AF2B5AE,0x9E4B4E60,0x774ABFDD,0x85254725,
        0x15815AEB,0x4AB6AAD6,0x9CDAF8AF,0xD6032C0A
    };
    x[0][0][0][0][0] ^= h[0].x;
    x[0][0][0][0][1] ^= h[0].y;
    x[0][0][0][1][0] ^= h[1].x;
    x[0][0][0][1][1] ^= h[1].y;
    x[0][0][1][0][0] ^= h[2].x;
    x[0][0][1][0][1] ^= h[2].y;
    x[0][0][1][1][0] ^= h[3].x;
    x[0][0][1][1][1] ^= h[3].y;
    rrounds(x, mona_one);
    x[0][0][0][0][0] ^= 0x80U;
    rrounds(x, mona_one);
    // Avoid type-punning uint2 as a uint32 array in the new code.
    uint32_t out[8];
    Final(x, out, mona_one);
    #pragma unroll
    for (int i=0; i<4; ++i) h[i]=make_uint2(out[2*i],out[2*i+1]);
}

template<int Block>
__global__ __launch_bounds__(Block)
void mona_cube_kernel(uint32_t n, uint2 *hash, uint32_t mona_one)
{
    uint32_t t=blockIdx.x*blockDim.x+threadIdx.x;
    if(t>=n) return;
    uint2 h[4];
    #pragma unroll
    for(int k=0;k<4;++k) h[k]=hash[t+size_t(k)*n];
    mona_cube32(h, mona_one);
    #pragma unroll
    for(int k=0;k<4;++k) hash[t+size_t(k)*n]=h[k];
}
