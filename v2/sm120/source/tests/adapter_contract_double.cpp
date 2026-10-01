// SPDX-License-Identifier: GPL-3.0-or-later
// CPU API double: tests the actual adapter's module/transfer/owner contract.
// It computes reference values on CPU; it DOES NOT emulate CUDA arithmetic,
// races, occupancy, native instructions, or any real driver/API ABI.
#include "mona2/frozen_backend.hpp"
#include "mona2/job_snapshot.hpp"
#include "image_contract.hpp"
#include <cuda.h>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
using namespace mona2;
extern "C" const unsigned char mona2_images[image_contract::payload_bytes]={
    0
};
struct Ctx{};
struct Str{
    std::vector<std::function<void()>> q;
};
struct Mod{
    unsigned id;
    std::map<std::string,CUdeviceptr> globals;
};
struct Fun{
    Mod*m;
    unsigned role;
};
namespace {
    std::map<CUdeviceptr,std::size_t> memory;
    std::vector<Fun*>funs;
    Words expected{};
    std::uint64_t calls=0,launches=0;
    unsigned order=0;
    bool poisoned=false;
    void*ptr(CUdeviceptr p,std::size_t n){
        auto it=memory.upper_bound(p);
        require(it!=memory.begin(),"DOUBLE_UNKNOWN_ADDRESS");
        --it;
        require(p>=it->first&&p-it->first<=it->second&&n<=it->second-(p-it->first),"DOUBLE_COPY_BOUNDS");
        return reinterpret_cast<void*>(p);
    }
    template<class T>T arg(void*p){
        T v;
        std::memcpy(&v,p,sizeof(T));
        return v;
    }
    CUdeviceptr alloc(std::size_t n){
        auto*p=new unsigned char[n];
        auto v=reinterpret_cast<CUdeviceptr>(p);
        memory.emplace(v,n);
        return v;
    }
}
extern "C" {
    CUresult cuInit(unsigned){
        ++calls;
        return 0;
    }
    CUresult cuDeviceGet(CUdevice*p,int i){
        *p=i;
        return 0;
    }
    CUresult cuDeviceGetAttribute(int*p,int a,CUdevice){
        *p=a==CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR?12:0;
        return 0;
    }
    CUresult cuDriverGetVersion(int*p){
        *p=13040;
        return 0;
    }
    CUresult cuDevicePrimaryCtxRetain(CUcontext*p,CUdevice){
        *p=new Ctx;
        return 0;
    }
    CUresult cuDevicePrimaryCtxRelease(CUdevice){
        return 0;
    }
    CUcontext current=nullptr;
    CUresult cuCtxSetCurrent(CUcontext c){
        current=c;
        return 0;
    }
    CUresult cuDeviceGetUuid(CUuuid*p,CUdevice){
        std::memset(p,0,sizeof(*p));
        return 0;
    }
    CUresult cuDeviceGetName(char*p,int,CUdevice){
        std::strcpy(p,"CPU_API_DOUBLE_NOT_GPU");
        return 0;
    }
    CUresult cuDeviceGetPCIBusId(char*p,int,CUdevice){
        std::strcpy(p,"DOUBLE");
        return 0;
    }
    CUresult cuMemAlloc(CUdeviceptr*p,std::size_t n){
        *p=alloc(n);
        return 0;
    }
    CUresult cuMemFree(CUdeviceptr p){
        ptr(p,0);
        delete[]reinterpret_cast<unsigned char*>(p);
        memory.erase(p);
        return 0;
    }
    CUresult cuMemAllocHost(void**p,std::size_t n){
        *p=::operator new(n);
        return 0;
    }
    CUresult cuMemFreeHost(void*p){
        ::operator delete(p);
        return 0;
    }
    CUresult cuMemsetD8(CUdeviceptr p,unsigned char c,std::size_t n){
        std::memset(ptr(p,n),c,n);
        return 0;
    }
    CUresult cuMemsetD8Async(CUdeviceptr p,unsigned char c,std::size_t n,CUstream s){
        s->q.push_back([=]{
            cuMemsetD8(p,c,n);
        }
        );
        return 0;
    }
    CUresult cuMemcpyDtoH(void*h,CUdeviceptr d,std::size_t n){
        std::memcpy(h,ptr(d,n),n);
        return 0;
    }
    CUresult cuMemcpyHtoD(CUdeviceptr d,const void*h,std::size_t n){
        std::memcpy(ptr(d,n),h,n);
        return 0;
    }
    CUresult cuMemcpyDtoHAsync(void*h,CUdeviceptr d,std::size_t n,CUstream s){
        s->q.push_back([=]{
            cuMemcpyDtoH(h,d,n);
        }
        );
        return 0;
    }
    CUresult cuMemcpyHtoDAsync(CUdeviceptr d,const void*h,std::size_t n,CUstream s){
        s->q.push_back([=]{
            cuMemcpyHtoD(d,h,n);
        }
        );
        return 0;
    }
    CUresult cuStreamCreate(CUstream*p,unsigned flags){
        require(flags==CU_STREAM_NON_BLOCKING,"DOUBLE_STREAM_FLAGS");
        *p=new Str;
        return 0;
    }
    CUresult cuStreamDestroy(CUstream s){
        require(s->q.empty(),"DOUBLE_DESTROY_PENDING");
        delete s;
        return 0;
    }
    CUresult cuStreamSynchronize(CUstream s){
        auto q=std::move(s->q);
        s->q.clear();
        for(auto&f:q)f();
        return 0;
    }
    CUresult cuModuleLoadData(CUmodule*p,const void*d){
        unsigned id=3;
        for(unsigned i=0;i<3;++i)if(d==mona2_images+image_contract::offsets[i])id=i;
        require(id<3,"DOUBLE_WRONG_IMAGE");
        *p=new Mod{
            id,{}
        };
        return 0;
    }
    CUresult cuModuleUnload(CUmodule m){
        for(auto&[name,p]:m->globals)cuMemFree(p);
        delete m;
        return 0;
    }
    CUresult cuModuleGetGlobal(CUdeviceptr*p,std::size_t*n,CUmodule m,const char*name){
        std::map<std::string,unsigned> sizes{
            {
                "cpu_h",32
            }
            ,{
                "c_data",16
            }
            ,{
                "n02_job_columns",48
            }
            ,{
                "sigma",1024
            }
            ,{
                "u256",64
            }
        };
        require(sizes.contains(name),"DOUBLE_GLOBAL_NAME");
        *n=sizes.at(name);
        if(!m->globals.contains(name))m->globals[name]=alloc(*n);
        *p=m->globals.at(name);
        return 0;
    }
    CUresult cuModuleGetFunction(CUfunction*p,CUmodule m,const char*s){
        unsigned r=4;
        for(unsigned i=0;i<4;++i)if(std::string(s)==image_contract::symbol[i])r=i;
        require(r<4&&m->id==image_contract::module[r],"DOUBLE_ORIGIN_FUNCTION");
        *p=new Fun{
            m,r
        };
        funs.push_back(*p);
        return 0;
    }
    CUresult cuFuncGetAttribute(int*p,int a,CUfunction f){
        *p=a==CU_FUNC_ATTRIBUTE_NUM_REGS?int(image_contract::reg[f->role]):a==CU_FUNC_ATTRIBUTE_BINARY_VERSION?120:0;
        return 0;
    }
    CUresult cuOccupancyMaxActiveBlocksPerMultiprocessor(int*p,CUfunction,int,std::size_t){
        *p=4;
        return 0;
    }
    CUresult cuLaunchKernel(CUfunction f,unsigned gx,unsigned gy,unsigned gz,unsigned bx,unsigned by,unsigned bz,unsigned sm,CUstream s,void**p,void**extra){
        auto n=arg<std::uint32_t>(p[0]);
        auto r=f->role;
        require(extra==nullptr&&gy==1&&gz==1&&by==1&&bz==1&&bx==image_contract::block[r]&&sm==image_contract::shared[r]&&gx==(n+bx-1)/bx,"DOUBLE_LAUNCH_SHAPE");
        require((order==0&&r==0)||(order==1&&r==1)||(order==2&&(r==2||r==3)),"DOUBLE_LAUNCH_ORDER");
        order=(order+1)%3;
        ++launches;
        if(r==0){
            auto nonce=arg<CUdeviceptr>(p[1]);
            auto hash=arg<CUdeviceptr>(p[2]);
            require(arg<unsigned>(p[3])==1,"DOUBLE_ONE");
            s->q.push_back([=]{
                std::uint32_t first;cuMemcpyDtoH(&first,nonce,4);const n02_runtime::Snapshot snapshot(expected);auto rec=n02_runtime::prepare_record(snapshot);require(std::memcmp(ptr(f->m->globals.at("cpu_h"),32),snapshot.h.data(),32)==0&&std::memcmp(ptr(f->m->globals.at("c_data"),16),snapshot.data.data(),16)==0,"DOUBLE_JOB_SNAPSHOT");poisoned=std::memcmp(ptr(f->m->globals.at("n02_job_columns"),48),rec.word,48)!=0;
                auto* out=static_cast<std::uint64_t*>(ptr(hash,std::size_t(n)*32));for(unsigned i=0;i<n;++i){
                    if(n>1025){
                        for(unsigned j=0;j<4;++j)out[i+std::size_t(j)*n]=0;continue;
                    }
                    Target pre{};cpu_hash(expected,first+i,&pre);for(unsigned j=0;j<4;++j)out[i+std::size_t(j)*n]=(std::uint64_t(pre[j*2])|(std::uint64_t(pre[j*2+1])<<32))^(poisoned?1:0);
                }
            }
            );
        }
        else if(r==1){
            require(arg<unsigned>(p[2])==1,"DOUBLE_FUSED_ONE");
        }
        else {
            auto np=arg<CUdeviceptr>(p[1]),hp=arg<CUdeviceptr>(p[4]),dp=arg<CUdeviceptr>(p[5]);
            auto t=arg<std::uint64_t>(p[3]);
            s->q.push_back([=]{
                std::uint32_t first;cuMemcpyDtoH(&first,np,4);RawHits hits{};auto*debug=dp?static_cast<std::uint64_t*>(ptr(dp,std::size_t(n)*8)):nullptr;for(unsigned i=0;i<n;++i){
                    auto hi=n>1025?UINT64_MAX:upper(cpu_hash(expected,first+i));if(debug)debug[i]=hi;if(hi<=t){
                        if(hits.total<MaxCandidates)hits.nonces[hits.total]=first+i;else hits.overflow=1;++hits.total;
                    }
                }
                cuMemcpyHtoD(hp,&hits,sizeof(hits));
            }
            );
        }
        return 0;
    }
}
#ifndef MONA2_DOUBLE_NO_MAIN
int main(){
    try{
        {
            FrozenBackend gpu(0);
            for(unsigned seed=0;seed<3;++seed){
                Work w;
                for(unsigned i=0;i<20;++i)w.words[i]=seed*12345+i;
                w.target.fill(UINT32_MAX);
                expected=w.words;
                gpu.install(w);
                for(auto n:{
                    1u,33u,65u
                }
                ){
                    auto h=gpu.scan(std::uint32_t(NonceSpace-n),n,w.target,true);
                    require(h.total==n&&bool(h.overflow)==(n>64),"DOUBLE_HITS");
                    auto high=gpu.bmw_upper(n),pre=gpu.pre_bmw(n);
                    require(high.size()==n&&pre.size()==4*n,"DOUBLE_OUTPUT_SIZE");
                    gpu.check_guards();
                }
            }
            auto old=gpu.submissions();
            bool failed=false;
            try{
                gpu.scan(UINT32_MAX,2,Target{}
                );
            }
            catch(const Error&){
                failed=true;
            }
            require(failed&&old==gpu.submissions(),"DOUBLE_PREFLIGHT");
            require(gpu.completions()==9&&gpu.submissions()==27,"DOUBLE_GRAPH_LEDGER");
            std::cout<<gpu.accounting()<<'\n';
        }
        for(auto*f:funs)delete f;
        delete current;
        require(memory.empty(),"DOUBLE_LEAK");
        std::cout<<"{\"status\":\"PASS\",\"type\":\"CPU_API_DOUBLE\",\"scans\":9,\"launches\":"<<launches<<",\"real_GPU_calls\":0}\n";
        return 0;
    }
    catch(const std::exception&e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
#endif
