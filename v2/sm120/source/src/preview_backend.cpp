// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/preview_backend.hpp"
#include "mona2/job_snapshot.hpp"
#include "preview_image.hpp"
#include <cuda.h>
#include <algorithm>
#include <iostream>
#include <thread>

extern "C" const unsigned char mona2_images[];

namespace mona2 {
    namespace {
        constexpr std::size_t GuardBytes=256;
        constexpr unsigned StreamCount=3;
        static_assert(PreviewBackend::scan_window<=MaxScanWindow);
        static_assert(sizeof(RawHits)==264);

        void cuda(CUresult result){
            if(result==CUDA_SUCCESS)return;
            std::cerr<<"{\"event\":\"CUDA_ERROR\",\"numeric_code\":"<<result<<"}\n";
            throw Error("CUDA_DRIVER_FAILURE");
        }

        struct Buffer {
            CUdeviceptr base=0,p=0;

            void allocate(std::size_t bytes){
                cuda(cuMemAlloc(&base,bytes+2*GuardBytes));
                p=base+GuardBytes;
            }
            void set_guards(CUstream stream,std::size_t bytes)const{
                cuda(cuMemsetD8Async(base,0xa5,GuardBytes,stream));
                cuda(cuMemsetD8Async(p+bytes,0xa5,GuardBytes,stream));
            }
            void check_guards(std::size_t bytes)const{
                std::array<unsigned char,GuardBytes> data{};
                for(auto ptr:{base,p+bytes}){
                    cuda(cuMemcpyDtoH(data.data(),ptr,data.size()));
                    require(std::all_of(data.begin(),data.end(),[](auto value){return value==0xa5;}),
                            "GPU_PREVIEW_CANARY_CORRUPTION");
                }
            }
            void release()noexcept{
                if(base)cuMemFree(base);
                base=p=0;
            }
        };

        struct Pinned {
            std::uint32_t nonce=0;
            RawHits hits{};
        };

        struct ScanSlot {
            CUstream stream=nullptr;
            CUgraphExec graph=nullptr;
            std::uint64_t graph_target=0;
            Pinned* host=nullptr;
            Buffer hash,nonce,hits,upper;

            void initialize(){
                cuda(cuStreamCreate(&stream,CU_STREAM_NON_BLOCKING));
                cuda(cuMemAllocHost(reinterpret_cast<void**>(&host),sizeof(Pinned)));
                std::construct_at(host);
                hash.allocate(std::size_t(PreviewBackend::chunk_size)*32);
                nonce.allocate(sizeof(host->nonce));
                hits.allocate(sizeof(RawHits));
                upper.allocate(std::size_t(PreviewBackend::chunk_size)*8);
            }
            void release()noexcept{
                if(stream)cuStreamSynchronize(stream);
                if(graph)cuGraphExecDestroy(graph);
                graph=nullptr;
                hash.release();
                nonce.release();
                hits.release();
                upper.release();
                if(host){
                    std::destroy_at(host);
                    cuMemFreeHost(host);
                    host=nullptr;
                }
                if(stream)cuStreamDestroy(stream);
                stream=nullptr;
            }
        };
    }

    struct PreviewBackend::Impl {
        CUdevice device=0;
        CUcontext context=nullptr;
        std::array<CUmodule,2> modules{};
        std::array<CUfunction,4> functions{};
        std::array<CUdeviceptr,3> job_globals{};
        std::array<ScanSlot,StreamCount> slots;
        std::thread::id owner=std::this_thread::get_id();
        Target target{};
        bool ready=false;
        std::string information;
        std::vector<std::uint64_t> debug_upper;
        std::uint64_t attempts=0,submitted=0,completed=0,installed=0;

        void own()const{
            require(owner==std::this_thread::get_id(),"GPU_OWNER_MISMATCH");
        }
        CUdeviceptr producer_global(const char* name,std::size_t expected)const{
            CUdeviceptr ptr=0;
            std::size_t bytes=0;
            cuda(cuModuleGetGlobal(&ptr,&bytes,modules[0],name));
            require(bytes==expected,"GPU_PREVIEW_GLOBAL_ABI");
            return ptr;
        }
        void cleanup()noexcept{
            if(!context)return;
            cuCtxSetCurrent(context);
            for(auto& slot:slots)slot.release();
            for(auto& module:modules){
                if(module)cuModuleUnload(module);
                module=nullptr;
            }
            cuCtxSetCurrent(nullptr);
            cuDevicePrimaryCtxRelease(device);
            context=nullptr;
        }
        explicit Impl(int selected){
            try{
                require(selected>=0,"DEVICE_NEGATIVE");
                cuda(cuInit(0));
                cuda(cuDeviceGet(&device,selected));
                int major=0,minor=0,driver=0;
                cuda(cuDeviceGetAttribute(&major,CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR,device));
                cuda(cuDeviceGetAttribute(&minor,CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR,device));
                require(major==12&&minor==0,"SM120_REQUIRED");
                cuda(cuDriverGetVersion(&driver));
                cuda(cuDevicePrimaryCtxRetain(&context,device));
                cuda(cuCtxSetCurrent(context));
                cuda(cuModuleLoadData(&modules[0],mona2_images));
                cuda(cuModuleLoadData(&modules[1],mona2_images+preview_image::middle_offset));
                job_globals={producer_global("cpu_h",32),producer_global("c_data",16),
                             producer_global("n02_job_columns",48)};
                cuda(cuMemcpyHtoD(producer_global("u256",64),n02_runtime::c_u256,64));
                cuda(cuMemcpyHtoD(producer_global("sigma",1024),n02_runtime::c_sigma,1024));
                for(unsigned i=0;i<functions.size();++i){
                    cuda(cuModuleGetFunction(&functions[i],modules[i?1:0],preview_image::symbols[i]));
                    int registers=0,local=0,native=0;
                    cuda(cuFuncGetAttribute(&registers,CU_FUNC_ATTRIBUTE_NUM_REGS,functions[i]));
                    cuda(cuFuncGetAttribute(&local,CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES,functions[i]));
                    cuda(cuFuncGetAttribute(&native,CU_FUNC_ATTRIBUTE_BINARY_VERSION,functions[i]));
                    require(native==120&&local==0&&registers==preview_image::registers[i],
                            "GPU_PREVIEW_NATIVE_MISMATCH");
                }
                for(auto& slot:slots)slot.initialize();
                char name[256]{};
                cuda(cuDeviceGetName(name,sizeof(name),device));
                information="{\"event\":\"GPU_IDENTITY\",\"SM\":120,\"driver\":"+std::to_string(driver)+
                    ",\"name\":"+json_quote(name)+",\"version\":\"2.1.0\",\"streams\":"+std::to_string(StreamCount)+
                    ",\"chunk_size\":"+std::to_string(chunk_size)+"}";
            }catch(...){
                cleanup();
                throw;
            }
        }
        ~Impl(){cleanup();}

        void enqueue(ScanSlot& slot,std::uint32_t n,std::uint64_t high_target,bool debug,bool capture){
            cuda(cuMemcpyHtoDAsync(slot.nonce.p,&slot.host->nonce,4,slot.stream));
            cuda(cuMemsetD8Async(slot.hits.p,0,sizeof(RawHits),slot.stream));
            std::uint32_t one=1;
            CUdeviceptr output=debug?slot.upper.p:0;
            void* producer[]={&n,&slot.nonce.p,&slot.hash.p,&one};
            void* middle[]={&n,&slot.hash.p,&one};
            void* tail[]={&n,&slot.nonce.p,&slot.hash.p,&high_target,&slot.hits.p,&output,&one};
            void** args[]={producer,middle,tail};
            for(unsigned i=0;i<3;++i){
                const unsigned role=debug&&i==2?3:i;
                const unsigned block=preview_image::blocks[role];
                if(!capture)attempts=add64(attempts,1);
                cuda(cuLaunchKernel(functions[role],(n+block-1)/block,1,1,block,1,1,0,slot.stream,args[i],nullptr));
                if(!capture)submitted=add64(submitted,1);
            }
            cuda(cuMemcpyDtoHAsync(&slot.host->hits,slot.hits.p,sizeof(RawHits),slot.stream));
        }

        void prepare_graph(ScanSlot& slot,std::uint64_t high_target){
            if(slot.graph&&slot.graph_target==high_target)return;
            if(slot.graph){
                cuda(cuGraphExecDestroy(slot.graph));
                slot.graph=nullptr;
            }
            CUgraph graph=nullptr;
            cuda(cuStreamBeginCapture(slot.stream,CU_STREAM_CAPTURE_MODE_THREAD_LOCAL));
            try{
                enqueue(slot,chunk_size,high_target,false,true);
            }catch(...){
                cuStreamEndCapture(slot.stream,&graph);
                if(graph)cuGraphDestroy(graph);
                throw;
            }
            auto result=cuStreamEndCapture(slot.stream,&graph);
            if(result==CUDA_SUCCESS)result=cuGraphInstantiateWithFlags(&slot.graph,graph,0);
            if(graph)cuGraphDestroy(graph);
            cuda(result);
            slot.graph_target=high_target;
        }

        void launch(ScanSlot& slot,std::uint32_t first,std::uint32_t n,std::uint64_t threshold,bool debug){
            slot.host->nonce=first;
            if(!debug&&n==chunk_size){
                prepare_graph(slot,threshold);
                attempts=add64(attempts,3);
                cuda(cuGraphLaunch(slot.graph,slot.stream));
                submitted=add64(submitted,3);
                return;
            }
            if(debug){
                slot.hash.set_guards(slot.stream,std::size_t(n)*32);
                slot.upper.set_guards(slot.stream,std::size_t(n)*8);
                slot.nonce.set_guards(slot.stream,4);
                slot.hits.set_guards(slot.stream,sizeof(RawHits));
            }
            enqueue(slot,n,threshold,debug,false);
        }

        void collect(ScanSlot& slot,std::uint32_t first,std::uint32_t count,
                     std::size_t offset,RawHits& merged,bool debug){
            cuda(cuStreamSynchronize(slot.stream));
            ++completed;
            const auto& hits=slot.host->hits;
            require(hits.total<=count&&hits.overflow==unsigned(hits.total>MaxCandidates),
                    "GPU_PREVIEW_HIT_HEADER");
            const auto present=std::min<unsigned>(hits.total,MaxCandidates);
            for(unsigned i=0;i<present;++i)
                require(hits.nonces[i]>=first&&std::uint64_t(hits.nonces[i])<std::uint64_t(first)+count,
                        "GPU_PREVIEW_NONCE_RANGE");
            const auto used=std::min<unsigned>(merged.total,MaxCandidates);
            std::copy_n(hits.nonces.begin(),std::min<unsigned>(present,MaxCandidates-used),merged.nonces.begin()+used);
            merged.total+=hits.total;
            merged.overflow=unsigned(merged.total>MaxCandidates);
            if(debug){
                cuda(cuMemcpyDtoH(debug_upper.data()+offset,slot.upper.p,std::size_t(count)*8));
                slot.hash.check_guards(std::size_t(count)*32);
                slot.upper.check_guards(std::size_t(count)*8);
                slot.nonce.check_guards(4);
                slot.hits.check_guards(sizeof(RawHits));
            }
        }
    };

    PreviewBackend::PreviewBackend(int device):p_(std::make_unique<Impl>(device)){}
    PreviewBackend::~PreviewBackend()=default;

    void PreviewBackend::install(const Work& work){
        auto& p=*p_;
        p.own();
        p.ready=false;
        p.debug_upper.clear();
        const n02_runtime::Snapshot snapshot(work.words);
        const auto record=n02_runtime::prepare_record(snapshot);
        cuda(cuMemcpyHtoD(p.job_globals[0],snapshot.h.data(),32));
        cuda(cuMemcpyHtoD(p.job_globals[1],snapshot.data.data(),16));
        cuda(cuMemcpyHtoD(p.job_globals[2],record.word,48));
        p.target=work.target;
        p.installed=add64(p.installed,1);
        p.ready=true;
    }

    RawHits PreviewBackend::scan(std::uint32_t first,std::uint32_t count,const Target& target,bool debug){
        auto& p=*p_;
        p.own();
        require(p.ready,"GPU_JOB_NOT_READY");
        require(p.target==target,"GPU_TARGET_SNAPSHOT_MISMATCH");
        require(count>0&&count<=scan_window&&std::uint64_t(first)+count<=NonceSpace,"GPU_RANGE");
        p.debug_upper.resize(debug?count:0);
        const auto threshold=upper(target);
        const unsigned chunks=(count+chunk_size-1)/chunk_size;
        RawHits out;
        const auto finish=[&](unsigned i){
            const auto start=i*chunk_size;
            p.collect(p.slots[i%StreamCount],first+start,std::min(chunk_size,count-start),start,out,debug);
        };
        try{
            for(unsigned i=0;i<chunks;++i){
                if(i>=StreamCount)finish(i-StreamCount);
                const auto start=i*chunk_size;
                p.launch(p.slots[i%StreamCount],first+start,std::min(chunk_size,count-start),threshold,debug);
            }
            for(unsigned i=chunks>StreamCount?chunks-StreamCount:0;i<chunks;++i)finish(i);
            require(p.attempts==p.submitted&&p.completed*3==p.submitted,"GPU_PREVIEW_SUBMISSION_ACCOUNTING");
        }catch(...){
            p.ready=false;
            p.debug_upper.clear();
            for(auto& slot:p.slots)cuStreamSynchronize(slot.stream);
            throw;
        }
        return out;
    }

    std::vector<std::uint64_t> PreviewBackend::bmw_upper(std::uint32_t count){
        p_->own();
        require(count>0&&p_->debug_upper.size()==count,"GPU_DEBUG_SCOPE");
        return p_->debug_upper;
    }
    std::string PreviewBackend::identity()const{return p_->information;}
    std::string PreviewBackend::accounting()const{
        return "{\"kernel_attempts\":"+std::to_string(p_->attempts)+",\"kernel_submissions\":"+
            std::to_string(p_->submitted)+",\"complete_scans\":"+std::to_string(p_->completed)+
            ",\"job_installs\":"+std::to_string(p_->installed)+"}";
    }
}
