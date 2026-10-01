// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "cuda_ops.cuh"
#include "lyra_core.hpp"
#include "r02_skein_registers.cuh"
extern "C" __global__ __launch_bounds__(64)
void r02_cube1_l2_skein_b64(uint32_t n,uint2* hash,uint32_t mona_one) {
    extern __shared__ uint64_t scratch[];
    const size_t owner=size_t(blockIdx.x)*64+threadIdx.x;
    const bool live=owner<n;
    rdx::CudaOps ops{scratch,threadIdx.x&31u};
    uint2 h[4];
    #pragma unroll
    for(int j=0;j<4;++j) h[j]=live?hash[owner+size_t(j)*n]:make_uint2(0,0);
    // Qualified arithmetic/IMAD mask 15. Not the D01 experimental schedule.
    mona_cube32(h,mona_one);
    uint64_t v[4];
    #pragma unroll
    for(int j=0;j<4;++j) v[j]=devectorize(h[j]);
    rdx::transpose4(v,ops);
    uint64_t a=v[0],b=v[1],c=v[2],d=v[3];
    #pragma unroll 1
    for(unsigned pass=0;pass<4;++pass) {
        __syncwarp();
        const uint64_t x=pass==0?a:pass==1?b:pass==2?c:d;
        const uint64_t y=rdx::lyra_word(x,ops);
        if(pass==0)a=y;else if(pass==1)b=y;else if(pass==2)c=y;else d=y;
        __syncwarp();
    }
    v[0]=a;v[1]=b;v[2]=c;v[3]=d;
    rdx::transpose4(v,ops);
    #pragma unroll
    for(int j=0;j<4;++j) h[j]=make_uint2(uint32_t(v[j]),uint32_t(v[j]>>32));
    r02_skein_registers(h);
    #pragma unroll
    for(int j=0;j<4;++j) if(live)hash[owner+size_t(j)*n]=h[j];
}
