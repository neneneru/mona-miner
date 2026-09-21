#include <cuda_runtime.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include "mona/core.hpp"
#include "cube.cuh"
#include "upstream_blake.cuh"
#include "upstream_skein.cuh"
#include "lyra.cuh"
#include "upstream_bmw.cuh"

namespace {
void ck(cudaError_t e,const char* call) {
    if(e!=cudaSuccess) throw std::runtime_error(std::string(call)+": "+cudaGetErrorString(e));
}
#define CK(x) ck((x), #x)
struct DeviceCandidates { uint32_t count, overflow, nonces[mona::MaxCandidates]; };
static_assert(sizeof(DeviceCandidates)==(mona::MaxCandidates+2)*4);
std::mutex ownership_mutex;
std::set<int> owned_devices;

template<int Block, bool Debug>
__global__ __launch_bounds__(Block)
void mona_bmw_kernel(uint32_t n,const uint32_t *nonce_ptr,const uint64_t *hash,
                     uint64_t target,DeviceCandidates* hits,uint64_t* debug_upper)
{
    uint32_t t=blockIdx.x*blockDim.x+threadIdx.x;
    if(t>=n) return;
    uint32_t message[16]={0};
    #pragma unroll
    for(int j=0;j<4;++j) {
        uint64_t w=hash[t+size_t(j)*n];
        message[2*j]=uint32_t(w); message[2*j+1]=uint32_t(w>>32);
    }
    message[8]=0x80; message[14]=0x100;
    Compression256(message);
    Compression256_2(message); // Upstream's deliberate 64-bit prefilter.
    uint64_t high=uint64_t(message[14]) | (uint64_t(message[15])<<32);
    if constexpr(Debug) debug_upper[t]=high;
    if(high<=target) {
        uint32_t pos=atomicAdd(&hits->count,1u);
        if(pos<mona::MaxCandidates) hits->nonces[pos]=*nonce_ptr+t;
        else atomicExch(&hits->overflow,1u);
    }
}

template<class F> void linear_block(int block,F&& fn) {
    switch(block) {
    case 64: fn(std::integral_constant<int,64>{}); break;
    case 128: fn(std::integral_constant<int,128>{}); break;
    case 256: fn(std::integral_constant<int,256>{}); break;
    case 512: fn(std::integral_constant<int,512>{}); break;
    default: throw std::invalid_argument("linear block must be 64,128,256,512");
    }
}
template<class F> void cube_block(int block,F&& fn) {
    if(block==1024) fn(std::integral_constant<int,1024>{});
    else if(block==32) fn(std::integral_constant<int,32>{});
    else linear_block(block,std::forward<F>(fn));
}
template<class F> void lyra_block(int block,F&& fn) {
    switch(block) {
    case 32: fn(std::integral_constant<int,32>{}); break;
    case 64: fn(std::integral_constant<int,64>{}); break;
    case 96: fn(std::integral_constant<int,96>{}); break;
    case 128: fn(std::integral_constant<int,128>{}); break;
    case 192: fn(std::integral_constant<int,192>{}); break;
    case 256: fn(std::integral_constant<int,256>{}); break;
    default: throw std::invalid_argument("lyra block must be 32,64,96,128,192,256");
    }
}
}

namespace mona {
class Context {
public:
    Config cfg;
    DeviceInfo info;
    cudaDeviceProp prop{};
    cudaStream_t stream=nullptr;
    cudaGraph_t graph=nullptr;
    cudaGraphExec_t exec=nullptr;
    uint32_t graph_count=0;
    uint64_t *hash=nullptr,*state=nullptr,*upper=nullptr;
    uint32_t *nonce=nullptr,*host_nonce=nullptr;
    DeviceCandidates *hits=nullptr,*host_hits=nullptr;
    uint64_t target=0;
    bool has_job=false,registered=false;
    ~Context() {
        // Never reset the device: other applications own their own contexts.
        if(stream) { cudaSetDevice(info.device); cudaStreamSynchronize(stream); }
        if(exec) cudaGraphExecDestroy(exec);
        if(graph) cudaGraphDestroy(graph);
        if(hash) cudaFree(hash);
        if(state) cudaFree(state);
        if(upper) cudaFree(upper);
        if(nonce) cudaFree(nonce);
        if(hits) cudaFree(hits);
        if(host_nonce) cudaFreeHost(host_nonce);
        if(host_hits) cudaFreeHost(host_hits);
        if(stream) cudaStreamDestroy(stream);
        if(registered) {
            std::lock_guard<std::mutex> lock(ownership_mutex);
            owned_devices.erase(info.device);
        }
    }
    void invalidate_graph() {
        if(exec) { CK(cudaGraphExecDestroy(exec)); exec=nullptr; }
        if(graph) { CK(cudaGraphDestroy(graph)); graph=nullptr; }
        graph_count=0;
    }
};
void ContextDeleter::operator()(Context* p) const noexcept { delete p; }
void validate_config(const Config& c) {
#if defined(MONA_LYRA_STATIC_SHARED64)
    if(c.lyra_block!=64) throw std::invalid_argument("MONA_LYRA_STATIC_SHARED64 requires lyra block 64");
#endif
    if(c.batch<1 || c.batch>(1u<<22)) throw std::invalid_argument("batch must be 1..4194304");
    if(c.fusion>7) throw std::invalid_argument("fusion is a 3-bit mask");
    auto noop=[](auto){};
    linear_block(c.blake_block,noop); linear_block(c.skein_block,noop);
    linear_block(c.bmw_block,noop); cube_block(c.cube_block,noop); lyra_block(c.lyra_block,noop);
    if(c.carveout!=-1 && (c.carveout<0 || c.carveout>100)) throw std::invalid_argument("invalid carveout");
}
ContextPtr create(int device,const Config& cfg) {
    validate_config(cfg);
    ContextPtr c(new Context);
    c->cfg=cfg; c->info.device=device;
    CK(cudaSetDevice(device));
    CK(cudaGetDeviceProperties(&c->prop,device));
    auto& p=c->prop;
    c->info.sm=p.major*10+p.minor;
    if(c->info.sm!=MONA_TARGET_SM) throw std::runtime_error("wrong architecture for this native executable");
    { std::lock_guard<std::mutex> lock(ownership_mutex);
      if(!owned_devices.insert(device).second) throw std::runtime_error("one Context per GPU: upstream job constants are device-global");
      c->registered=true;
    }
    c->info.name=p.name; c->info.multiprocessors=p.multiProcessorCount;
    c->info.memory_bytes=p.totalGlobalMem;
    std::ostringstream uuid; uuid << std::hex << std::setfill('0');
    for(unsigned char b:p.uuid.bytes) uuid<<std::setw(2)<<unsigned(b);
    c->info.uuid=uuid.str();
    CK(cudaDriverGetVersion(&c->info.driver)); CK(cudaRuntimeGetVersion(&c->info.runtime));
    CK(cudaStreamCreateWithFlags(&c->stream,cudaStreamNonBlocking));
    CK(cudaMalloc(&c->hash,size_t(cfg.batch)*32));
    if(!(cfg.fusion&FusedLyra)) CK(cudaMalloc(&c->state,size_t(mona_lyra_stride(cfg.batch))*128));
    CK(cudaMalloc(&c->upper,size_t(cfg.batch)*8));
    CK(cudaMalloc(&c->nonce,sizeof(uint32_t)));
    CK(cudaMallocHost(&c->host_nonce,sizeof(uint32_t)));
    CK(cudaMalloc(&c->hits,sizeof(DeviceCandidates)));
    CK(cudaMallocHost(&c->host_hits,sizeof(DeviceCandidates)));
    *c->host_nonce=0;
    CK(cudaMemcpyToSymbolAsync(u256,c_u256,sizeof(c_u256),0,cudaMemcpyHostToDevice,c->stream));
    CK(cudaMemcpyToSymbolAsync(sigma,c_sigma,sizeof(c_sigma),0,cudaMemcpyHostToDevice,c->stream));
    CK(cudaMemcpyToSymbolAsync(DMatrix,&c->state,sizeof(c->state),0,cudaMemcpyHostToDevice,c->stream));
    const int shared=cfg.lyra_block*48*int(sizeof(uint2));
    if(shared>int(p.sharedMemPerBlockOptin)) throw std::runtime_error("requested Lyra shared memory exceeds device limit");
    auto attributes=[&](auto kernel) {
        if(shared>48*1024) CK(cudaFuncSetAttribute(kernel,cudaFuncAttributeMaxDynamicSharedMemorySize,shared));
        if(cfg.carveout>=0) CK(cudaFuncSetAttribute(kernel,cudaFuncAttributePreferredSharedMemoryCarveout,cfg.carveout));
    };
    if(cfg.fusion&FusedLyra) lyra_block(cfg.lyra_block,[&](auto block){
        constexpr int B=decltype(block)::value; attributes(mona_lyra_fused<B>);
    });
    else attributes(lyra2v2_gpu_hash_32_2);
    CK(cudaStreamSynchronize(c->stream));
    return c;
}
DeviceInfo device_info(const Context& c) { return c.info; }
void set_job(Context& c,const std::array<uint32_t,20>& words,uint64_t target) {
    CK(cudaSetDevice(c.info.device));
    CK(cudaStreamSynchronize(c.stream));
    c.invalidate_graph();
    uint32_t h[8]; std::memcpy(h,c_IV256,sizeof(h));
    // Same midstate calculation and pdata byte order as the windows branch.
    blake256_compress1st(h,words.data(),512);
    CK(cudaMemcpyToSymbolAsync(cpu_h,h,sizeof(h),0,cudaMemcpyHostToDevice,c.stream));
    CK(cudaMemcpyToSymbolAsync(c_data,words.data()+16,sizeof(c_data),0,cudaMemcpyHostToDevice,c.stream));
    CK(cudaStreamSynchronize(c.stream)); // h lives on the stack.
    c.target=target; c.has_job=true;
}

static void launch(Context& c,uint32_t n,bool debug) {
    auto& x=c.cfg;
    auto grid=[&](int b){return (n+uint32_t(b)-1)/uint32_t(b);};
    CK(cudaMemcpyAsync(c.nonce,c.host_nonce,4,cudaMemcpyHostToDevice,c.stream));
    CK(cudaMemsetAsync(c.hits,0,sizeof(DeviceCandidates),c.stream));
    linear_block(x.blake_block,[&](auto block){
        constexpr int B=decltype(block)::value;
        if(x.fusion&FusedBlakeCube) blakeKeccak256_gpu_hash_80<B,true><<<grid(B),B,0,c.stream>>>(n,c.nonce,(uint32_t*)c.hash,1U);
        else blakeKeccak256_gpu_hash_80<B,false><<<grid(B),B,0,c.stream>>>(n,c.nonce,(uint32_t*)c.hash,1U);
    });
    if(!(x.fusion&FusedBlakeCube)) cube_block(x.cube_block,[&](auto block){
        constexpr int B=decltype(block)::value;
        mona_cube_kernel<B><<<grid(B),B,0,c.stream>>>(n,(uint2*)c.hash,1U);
    });
    int b=x.lyra_block;
    const size_t smem=size_t(b)*48*sizeof(uint2);
    if(x.fusion&FusedLyra) lyra_block(b,[&](auto block){
        constexpr int B=decltype(block)::value;
        mona_lyra_fused<B><<<(n+B/4-1)/(B/4),dim3(4,B/4),smem,c.stream>>>(n,(uint2*)c.hash);
    });
    else {
        lyra2v2_gpu_hash_32_1<<<grid(32),32,0,c.stream>>>(n,(uint2*)c.hash);
        lyra2v2_gpu_hash_32_2<<<(n+b/4-1)/(b/4),dim3(4,b/4),smem,c.stream>>>(n);
        lyra2v2_gpu_hash_32_3<<<grid(32),32,0,c.stream>>>(n,(uint2*)c.hash);
    }
    linear_block(x.skein_block,[&](auto block){
        constexpr int B=decltype(block)::value;
        if(x.fusion&FusedSkeinCube) skein256_gpu_hash_32<B,true><<<grid(B),B,0,c.stream>>>(n,0,c.hash,1U);
        else skein256_gpu_hash_32<B,false><<<grid(B),B,0,c.stream>>>(n,0,c.hash,1U);
    });
    if(!(x.fusion&FusedSkeinCube)) cube_block(x.cube_block,[&](auto block){
        constexpr int B=decltype(block)::value;
        mona_cube_kernel<B><<<grid(B),B,0,c.stream>>>(n,(uint2*)c.hash,1U);
    });
    linear_block(x.bmw_block,[&](auto block){
        constexpr int B=decltype(block)::value;
        if(debug) mona_bmw_kernel<B,true><<<grid(B),B,0,c.stream>>>(n,c.nonce,c.hash,c.target,c.hits,c.upper);
        else mona_bmw_kernel<B,false><<<grid(B),B,0,c.stream>>>(n,c.nonce,c.hash,c.target,c.hits,nullptr);
    });
    CK(cudaPeekAtLastError());
    CK(cudaMemcpyAsync(c.host_hits,c.hits,sizeof(DeviceCandidates),cudaMemcpyDeviceToHost,c.stream));
}
static void check_range(Context& c,uint32_t first,uint32_t count) {
    if(!c.has_job) throw std::logic_error("set_job is required");
    if(!count || count>c.cfg.batch) throw std::invalid_argument("invalid batch length");
    if(uint64_t(first)+count>(uint64_t(1)<<32)) throw std::invalid_argument("nonce range crosses 2^32");
    CK(cudaSetDevice(c.info.device));
}
Candidates scan(Context& c,uint32_t first,uint32_t count) {
    check_range(c,first,count);
    *c.host_nonce=first;
    if(c.cfg.graph && (!c.exec || c.graph_count!=count)) {
        c.invalidate_graph();
        CK(cudaStreamBeginCapture(c.stream,cudaStreamCaptureModeThreadLocal));
        try { launch(c,count,false); }
        catch(...) {
            cudaGraph_t abandoned=nullptr;
            cudaStreamEndCapture(c.stream,&abandoned);
            if(abandoned) cudaGraphDestroy(abandoned);
            throw;
        }
        CK(cudaStreamEndCapture(c.stream,&c.graph));
        CK(cudaGraphInstantiateWithFlags(&c.exec,c.graph,0));
        c.graph_count=count;
    }
    if(c.cfg.graph) CK(cudaGraphLaunch(c.exec,c.stream));
    else launch(c,count,false);
    CK(cudaStreamSynchronize(c.stream));
    Candidates out;
    // No per-batch event instrumentation in the throughput/mining path.
    out.total_found=c.host_hits->count;
    out.overflow=c.host_hits->overflow!=0 || out.total_found>MaxCandidates;
    unsigned n=std::min(out.total_found,MaxCandidates);
    out.nonces.assign(c.host_hits->nonces,c.host_hits->nonces+n);
    return out;
}
DebugOutput debug(Context& c,uint32_t first,uint32_t count) {
    check_range(c,first,count);
    *c.host_nonce=first;
    launch(c,count,true);
    DebugOutput out;
    out.pre_bmw_soa.resize(size_t(count)*4);
    out.bmw_upper64.resize(count);
    CK(cudaMemcpyAsync(out.pre_bmw_soa.data(),c.hash,size_t(count)*32,cudaMemcpyDeviceToHost,c.stream));
    CK(cudaMemcpyAsync(out.bmw_upper64.data(),c.upper,size_t(count)*8,cudaMemcpyDeviceToHost,c.stream));
    CK(cudaStreamSynchronize(c.stream));
    return out;
}
std::vector<KernelInfo> kernel_info(Context& c) {
    CK(cudaSetDevice(c.info.device));
    std::vector<KernelInfo> out;
    auto add=[&](const char* name,auto fn,int b,size_t shared=0) {
        cudaFuncAttributes a{}; CK(cudaFuncGetAttributes(&a,fn));
        int blocks=0; CK(cudaOccupancyMaxActiveBlocksPerMultiprocessor(&blocks,fn,b,shared));
        KernelInfo k; k.name=name; k.block=b; k.registers_per_thread=a.numRegs;
        k.local_bytes_per_thread=a.localSizeBytes;
        k.static_shared_bytes=a.sharedSizeBytes; k.dynamic_shared_bytes=shared;
        k.active_blocks_per_sm=blocks; k.max_threads_per_sm=c.prop.maxThreadsPerMultiProcessor;
        k.theoretical_occupancy=double(blocks*b)/c.prop.maxThreadsPerMultiProcessor;
        out.push_back(k);
    };
    auto& x=c.cfg;
    linear_block(x.blake_block,[&](auto block){constexpr int B=decltype(block)::value;
        if(x.fusion&FusedBlakeCube) add("blake_keccak_cube",blakeKeccak256_gpu_hash_80<B,true>,B);
        else add("blake_keccak",blakeKeccak256_gpu_hash_80<B,false>,B);
    });
    if(!(x.fusion&FusedBlakeCube) || !(x.fusion&FusedSkeinCube)) cube_block(x.cube_block,[&](auto block){
        constexpr int B=decltype(block)::value; add("cube",mona_cube_kernel<B>,B);
    });
    size_t shared=size_t(x.lyra_block)*48*sizeof(uint2);
    if(x.fusion&FusedLyra) lyra_block(x.lyra_block,[&](auto block){constexpr int B=decltype(block)::value;
        add("lyra_fused",mona_lyra_fused<B>,B,shared);
    });
    else {
        add("lyra_init",lyra2v2_gpu_hash_32_1,32);
        add("lyra_matrix",lyra2v2_gpu_hash_32_2,x.lyra_block,shared);
        add("lyra_finalize",lyra2v2_gpu_hash_32_3,32);
    }
    linear_block(x.skein_block,[&](auto block){constexpr int B=decltype(block)::value;
        if(x.fusion&FusedSkeinCube) add("skein_cube",skein256_gpu_hash_32<B,true>,B);
        else add("skein",skein256_gpu_hash_32<B,false>,B);
    });
    linear_block(x.bmw_block,[&](auto block){constexpr int B=decltype(block)::value;
        add("bmw",mona_bmw_kernel<B,false>,B);
    });
    return out;
}
} // namespace mona
