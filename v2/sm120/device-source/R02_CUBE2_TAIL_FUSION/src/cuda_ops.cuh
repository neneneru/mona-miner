// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cuda_runtime.h>
#include <cstdint>
#include "transpose4.hpp"
#include "lane_constants.hpp"
namespace rdx {
struct CudaOps {
    using Word=uint64_t;
    Word* shared;
    unsigned lane;
    RDX_INLINE Word load(int index){return shared[size_t(index)*blockDim.x+threadIdx.x];}
    RDX_INLINE void store(int index,Word value){shared[size_t(index)*blockDim.x+threadIdx.x]=value;}
    RDX_INLINE Word rdx_rot64(Word value,unsigned k){return (value>>k)|(value<<(64-k));}
    RDX_INLINE Word from_lane(Word value,unsigned source,unsigned width=32){
        uint32_t lo=__shfl_sync(0xffffffffu,uint32_t(value),source,width);
        uint32_t hi=__shfl_sync(0xffffffffu,uint32_t(value>>32),source,width);
        return uint64_t(lo)|(uint64_t(hi)<<32);
    }
    RDX_INLINE Word shuffle(Word value,int delta){return from_lane(value,unsigned(int(lane&3)+delta)&3u,4);}
    template<int Bit> RDX_INLINE Word shuffle_xor(Word value){
        uint32_t lo=__shfl_xor_sync(0xffffffffu,uint32_t(value),Bit,4);
        uint32_t hi=__shfl_xor_sync(0xffffffffu,uint32_t(value>>32),Bit,4);
        return uint64_t(lo)|(uint64_t(hi)<<32);
    }
    template<int Bit> RDX_INLINE Word select_bit(Word yes,Word no){return (lane&Bit)?yes:no;}
    RDX_INLINE Word iv(int half){return lane_iv(lane,half);}
    RDX_INLINE Word basil(int half){return lane_basil(lane,half);}
    RDX_INLINE int row(Word x){return int(__shfl_sync(0xffffffffu,uint32_t(x),0,4)&3u);}
    RDX_INLINE void twist(Word (&a)[3],Word d0,Word d1,Word d2){
        if((lane&3)==0){a[0]^=d2;a[1]^=d0;a[2]^=d1;}
        else {a[0]^=d0;a[1]^=d1;a[2]^=d2;}
    }
};
}
