// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/frozen_backend.hpp"
#include "mona2/job_snapshot.hpp"
#include "image_contract.hpp"
#include <cuda.h>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cstdlib>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
extern "C" const unsigned char mona2_images[];
namespace mona2 {
    namespace {
        void ck(CUresult result){
            if(result!=CUDA_SUCCESS){
                std::cerr<<"{\"event\":\"CUDA_ERROR\",\"numeric_code\":"<<result<<"}"<<std::endl;
                throw Error("CUDA_DRIVER_FAILURE");
            }
        }
        struct Buffer {
            CUdeviceptr base=0,p=0;
            std::size_t length=0;
            void allocate(std::size_t bytes){
                length=bytes;
                ck(cuMemAlloc(&base,bytes+512));
                p=base+256;
                ck(cuMemsetD8(base,0xA5,bytes+512));
            }
            void guard(std::size_t logical){
                require(logical<=length,"GUARD_LENGTH");
                std::array<unsigned char,256>a{}
                ,b{};
                ck(cuMemcpyDtoH(a.data(),base,256));
                ck(cuMemcpyDtoH(b.data(),p+logical,256));
                require(std::all_of(a.begin(),a.end(),[](auto v){
                    return v==0xA5;
                }
                )&&std::all_of(b.begin(),b.end(),[](auto v){
                    return v==0xA5;
                }
                ),"GPU_CANARY_CORRUPTION");
            }
            void release()noexcept{
                if(base)cuMemFree(base);
                base=p=0;
            }
        };
        struct Pinned {
            std::uint32_t h[8]{}
            ,data[4]{}
            ,record[12]{}
            ,nonce=0;
            RawHits hits{};
        };
        static_assert(sizeof(RawHits)==264);
    }
    struct FrozenBackend::Impl {
        CUdevice device=0;
        CUcontext context=nullptr;
        CUstream stream=nullptr;
        std::array<CUmodule,3> modules{};
        std::array<CUfunction,4> functions{};
        std::array<std::array<CUdeviceptr,3>,3> globals{};
        Buffer hash,upper,hits;
        CUdeviceptr nonce=0;
        Pinned* host=nullptr;
        std::thread::id owner=std::this_thread::get_id();
        bool ready=false,in_flight=false,debug_ready=false;
        std::uint32_t debug_count=0;
        std::uint64_t attempts=0,submitted=0,completed=0,installed=0;
        std::array<std::uint64_t,4> roles{};
        std::string information;
        Target current_target{};
        std::filesystem::path validation_gate;
        void own(){
            require(owner==std::this_thread::get_id(),"GPU_OWNER_MISMATCH");
            if(!validation_gate.empty())require(!std::filesystem::exists(validation_gate/"STOP"),"VALIDATION_SAFETY_STOP");
        }
        CUdeviceptr global(unsigned m,const char*name,std::size_t expected){
            CUdeviceptr p=0;
            std::size_t n=0;
            ck(cuModuleGetGlobal(&p,&n,modules.at(m),name));
            require(n==expected,"GPU_GLOBAL_ABI");
            return p;
        }
        void cleanup()noexcept{
            if(context)cuCtxSetCurrent(context);
            if(stream)cuStreamSynchronize(stream);
            hash.release();
            upper.release();
            hits.release();
            if(nonce)cuMemFree(nonce);
            if(host){
                std::destroy_at(host);
                cuMemFreeHost(host);
            }
            for(auto& m:modules)if(m){
                cuModuleUnload(m);
                m=nullptr;
            }
            if(stream)cuStreamDestroy(stream);
            if(context)cuDevicePrimaryCtxRelease(device);
            stream=nullptr;
            context=nullptr;
            host=nullptr;
            nonce=0;
        }
        explicit Impl(int selected){
            try {
                require(selected>=0,"DEVICE_NEGATIVE");
                ck(cuInit(0));
                ck(cuDeviceGet(&device,selected));
                int major=0,minor=0,driver=0;
                ck(cuDeviceGetAttribute(&major,CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR,device));
                ck(cuDeviceGetAttribute(&minor,CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR,device));
                require(major==12&&minor==0,"SM120_REQUIRED");
                ck(cuDriverGetVersion(&driver));
                ck(cuDevicePrimaryCtxRetain(&context,device));
                ck(cuCtxSetCurrent(context));
                ck(cuStreamCreate(&stream,CU_STREAM_NON_BLOCKING));
                for(unsigned m=0;m<3;++m){
                    ck(cuModuleLoadData(&modules[m],mona2_images+image_contract::offsets[m]));
                    globals[m][0]=global(m,"cpu_h",32);
                    globals[m][1]=global(m,"c_data",16);
                    if(m==0)globals[m][2]=global(m,"n02_job_columns",48);
                    ck(cuMemcpyHtoD(global(m,"u256",64),n02_runtime::c_u256,64));
                    ck(cuMemcpyHtoD(global(m,"sigma",1024),n02_runtime::c_sigma,1024));
                }
                for(unsigned m=1;m<3;++m)require(globals[0][0]!=globals[m][0]&&globals[0][1]!=globals[m][1],"MODULE_GLOBAL_ALIAS");
                std::ostringstream r;
                r<<"{\"event\":\"GPU_IDENTITY\",\"SM\":120,\"driver\":"<<driver<<",\"runtime\":\"Driver API only\",\"modules\":3,\"functions\":[";
                for(unsigned k=0;k<4;++k){
                    ck(cuModuleGetFunction(&functions[k],modules.at(image_contract::module[k]),image_contract::symbol[k]));
                    int reg=0,local=0,stat=0,binary=0,blocks=0;
                    ck(cuFuncGetAttribute(&reg,CU_FUNC_ATTRIBUTE_NUM_REGS,functions[k]));
                    ck(cuFuncGetAttribute(&local,CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES,functions[k]));
                    ck(cuFuncGetAttribute(&stat,CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES,functions[k]));
                    ck(cuFuncGetAttribute(&binary,CU_FUNC_ATTRIBUTE_BINARY_VERSION,functions[k]));
                    require(reg==image_contract::reg[k]&&local==0&&binary==120&&stat>=0&&stat<=1024,"RUNTIME_NATIVE_MISMATCH");
                    ck(cuOccupancyMaxActiveBlocksPerMultiprocessor(&blocks,functions[k],image_contract::block[k],image_contract::shared[k]));
                    if(k)r<<',';
                    r<<"{\"role\":"<<k<<",\"module\":"<<image_contract::module[k]<<",\"registers\":"<<reg<<",\"static_shared\":"<<stat<<",\"dynamic_shared\":"<<image_contract::shared[k]<<",\"occupancy_blocks\":"<<blocks<<'}';
                }
                CUuuid uuid{};
                char pci[64]{}
                ,name[256]{};
                ck(cuDeviceGetUuid(&uuid,device));
                ck(cuDeviceGetPCIBusId(pci,sizeof(pci),device));
                ck(cuDeviceGetName(name,sizeof(name),device));
                r<<"],\"uuid\":"<<json_quote(hex(std::span(reinterpret_cast<const std::uint8_t*>(uuid.bytes),16)))<<",\"pci\":"<<json_quote(pci)<<",\"name\":"<<json_quote(name)<<",\"occupancy_is_not_speed\":true}";
                information=r.str();
                hash.allocate(std::size_t(NativeBatch)*32);
                upper.allocate(std::size_t(NativeBatch)*8);
                hits.allocate(sizeof(RawHits));
                ck(cuMemAlloc(&nonce,4));
                ck(cuMemAllocHost(reinterpret_cast<void**>(&host),sizeof(Pinned)));
                std::construct_at(host);
#ifdef _WIN32
                wchar_t gate_buffer[32768]{};
                auto gate_length=GetEnvironmentVariableW(L"MONA2_VALIDATION_GATE_DIR",gate_buffer,32768);
                if(gate_length){
                    require(gate_length<32768,"VALIDATION_GATE_PATH");
                    validation_gate=std::filesystem::path(gate_buffer);
                }
#else
                if(const char* g=std::getenv("MONA2_VALIDATION_GATE_DIR"))validation_gate=std::filesystem::path(g);
#endif
                if(!validation_gate.empty()){
                    require(std::filesystem::is_directory(validation_gate),"VALIDATION_GATE_DIRECTORY");
                    {
                        std::ofstream ready(validation_gate/"GPU_READY.json",std::ios::binary);
                        ready<<information;
                        require(bool(ready),"VALIDATION_READY_WRITE");
                    }
                    auto end=Clock::now()+std::chrono::seconds(30);
                    while(!std::filesystem::exists(validation_gate/"GO")){
                        require(Clock::now()<end&&!std::filesystem::exists(validation_gate/"STOP"),"VALIDATION_PRE_KERNEL_STOP");
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }
            }
            catch(...){
                cleanup();
                throw;
            }
        }
        ~Impl(){
            cleanup();
        }
    };
    FrozenBackend::FrozenBackend(int device):p_(std::make_unique<Impl>(device)){}
    FrozenBackend::~FrozenBackend()=default;
    void FrozenBackend::install(const Work&w){
        auto&p=*p_;
        p.own();
        require(!p.in_flight,"JOB_MUTATION_IN_FLIGHT");
        p.ready=false;
        p.debug_ready=false;
        ck(cuStreamSynchronize(p.stream));
        const n02_runtime::Snapshot snap(w.words);
        const auto rec=n02_runtime::prepare_record(snap);
        std::copy(snap.h.begin(),snap.h.end(),p.host->h);
        std::copy(snap.data.begin(),snap.data.end(),p.host->data);
        std::copy(rec.word,rec.word+12,p.host->record);
        ck(cuMemcpyHtoDAsync(p.globals[0][0],p.host->h,32,p.stream));
        ck(cuMemcpyHtoDAsync(p.globals[0][1],p.host->data,16,p.stream));
        ck(cuMemcpyHtoDAsync(p.globals[0][2],p.host->record,48,p.stream));
        ck(cuStreamSynchronize(p.stream));
        p.installed=add64(p.installed,1);
        p.current_target=w.target;
        p.ready=true;
    }
    RawHits FrozenBackend::scan(std::uint32_t first,std::uint32_t n,const Target& target,bool debug){
        auto&p=*p_;
        p.own();
        require(p.ready&&!p.in_flight,"GPU_JOB_NOT_READY");
        require(target==p.current_target,"GPU_TARGET_SNAPSHOT_MISMATCH");
        require(n>0&&n<=NativeBatch&&std::uint64_t(first)+n<=NonceSpace,"GPU_RANGE");
        if(debug){
            ck(cuMemsetD8(p.hash.p+std::size_t(n)*32,0xA5,256));
            ck(cuMemsetD8(p.upper.p+std::size_t(n)*8,0xA5,256));
        }
        p.in_flight=true;
        p.debug_ready=false;
        p.host->nonce=first;
        try{
            ck(cuMemcpyHtoDAsync(p.nonce,&p.host->nonce,4,p.stream));
            ck(cuMemsetD8Async(p.hits.p,0,sizeof(RawHits),p.stream));
            std::uint32_t one=1;
            auto threshold=mona2::upper(target);
            CUdeviceptr out=debug?p.upper.p:0;
            void* producer[]={
                &n,&p.nonce,&p.hash.p,&one
            };
            void* fused[]={
                &n,&p.hash.p,&one
            };
            void* bmw[]={
                &n,&p.nonce,&p.hash.p,&threshold,&p.hits.p,&out
            };
            const unsigned rr[3]={
                0,1,debug?3u:2u
            };
            void**argv[3]={
                producer,fused,bmw
            };
            for(unsigned i=0;i<3;++i){
                auto role=rr[i];
                if(p.attempts==0&&!p.validation_gate.empty()){
                    std::ofstream marker(p.validation_gate/"FIRST_KERNEL_ATTEMPT.json");
                    marker<<"{\"module\":0,\"role\":\"producer\",\"image_bound\":true}";
                    require(bool(marker),"FIRST_KERNEL_MARKER_WRITE");
                }
                p.attempts=add64(p.attempts,1);
                ck(cuLaunchKernel(p.functions[role],(n+image_contract::block[role]-1)/image_contract::block[role],1,1,image_contract::block[role],1,1,image_contract::shared[role],p.stream,argv[i],nullptr));
                p.submitted=add64(p.submitted,1);
                p.roles[role]=add64(p.roles[role],1);
            }
            ck(cuMemcpyDtoHAsync(&p.host->hits,p.hits.p,sizeof(RawHits),p.stream));
            ck(cuStreamSynchronize(p.stream));
            p.completed=add64(p.completed,1);
            p.in_flight=false;
            p.debug_ready=debug;
            p.debug_count=n;
            require(p.attempts==p.submitted&&p.submitted==3*p.completed&&p.roles[0]==p.completed&&p.roles[1]==p.completed&&p.roles[2]+p.roles[3]==p.completed,"GPU_SUBMISSION_ACCOUNTING");
            return p.host->hits;
        }
        catch(...){
            p.ready=false;
            throw;
        }
    }
    std::vector<std::uint64_t> FrozenBackend::pre_bmw(std::uint32_t n){
        auto&p=*p_;
        p.own();
        require(!p.in_flight&&p.debug_ready&&n==p.debug_count,"GPU_DEBUG_SCOPE");
        std::vector<std::uint64_t> out(std::size_t(n)*4);
        ck(cuMemcpyDtoH(out.data(),p.hash.p,out.size()*8));
        return out;
    }
    std::vector<std::uint64_t> FrozenBackend::bmw_upper(std::uint32_t n){
        auto&p=*p_;
        p.own();
        require(!p.in_flight&&p.debug_ready&&n==p.debug_count,"GPU_DEBUG_SCOPE");
        std::vector<std::uint64_t> out(n);
        ck(cuMemcpyDtoH(out.data(),p.upper.p,out.size()*8));
        return out;
    }
    void FrozenBackend::check_guards(){
        p_->own();
        require(!p_->in_flight,"GUARD_IN_FLIGHT");
        p_->hash.guard(p_->debug_ready?std::size_t(p_->debug_count)*32:p_->hash.length);
        p_->upper.guard(p_->debug_ready?std::size_t(p_->debug_count)*8:p_->upper.length);
        p_->hits.guard(p_->hits.length);
    }
    std::string FrozenBackend::identity()const{
        return p_->information;
    }
    std::string FrozenBackend::accounting()const{
        return "{\"kernel_attempts\":"+std::to_string(p_->attempts)+",\"kernel_submissions\":"+std::to_string(p_->submitted)+",\"complete_scans\":"+std::to_string(p_->completed)+",\"job_installs\":"+std::to_string(p_->installed)+"}";
    }
    std::uint64_t FrozenBackend::submissions()const{
        return p_->submitted;
    }
    std::uint64_t FrozenBackend::completions()const{
        return p_->completed;
    }
    void FrozenBackend::test_stale_record(const Words&w){
        p_->own();
        require(!p_->in_flight,"FAULT_IN_FLIGHT");
        auto r=n02_runtime::prepare_record(n02_runtime::Snapshot(w));
        ck(cuMemcpyHtoD(p_->globals[0][2],r.word,48));
    }
    void FrozenBackend::test_wrong_module(const Words&w){
        p_->own();
        require(!p_->in_flight,"FAULT_IN_FLIGHT");
        n02_runtime::Snapshot s(w);
        for(unsigned m=1;m<3;++m){
            ck(cuMemcpyHtoD(p_->globals[m][0],s.h.data(),32));
            ck(cuMemcpyHtoD(p_->globals[m][1],s.data.data(),16));
        }
    }
}
