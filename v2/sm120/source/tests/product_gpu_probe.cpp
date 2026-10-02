// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only live-host overhead harness. Both sessions use literal loopback only
// and dummy credentials. USER-only control is created by withholding the mock
// Developer job, not by a shipping fee-disable option or different backend.
#include "mona2/worker.hpp"
#include "mona2/frozen_backend.hpp"
#include <iostream>
using namespace mona2;
int main(int argc,char**argv){
    try{
        require(argc==5,"PRODUCT_GPU_PROBE_ARGS");
        int device=int(decimal(argv[1],INT32_MAX));
        auto pu=decimal(argv[2],65535),pd=decimal(argv[3],65535);
        std::string mode=argv[4];
        require(mode=="USER_ONLY_HARNESS"||mode=="DEVFEE_ONLY_FSA","PRODUCT_GPU_PROBE_MODE");
        StopSignal stop;
        NetworkRuntime network;
        AuditLog log;
        ShareBudget budget(0);
        std::atomic<bool> done{
            false
        };
        FrozenBackend gpu(device);
        log.line(gpu.identity());
        Credentials uc{
            endpoint("stratum+tcp://127.0.0.1:"+std::to_string(pu)),"mock-user-alpha","user-dummy-only"
        };
        Credentials dc{
            endpoint("stratum+tcp://127.0.0.1:"+std::to_string(pd)),"mock-developer-beta","developer-dummy-only"
        };
        SessionActor u(777,Authority::User,uc,budget,stop,done,log,true),d(777,Authority::Developer,dc,budget,stop,done,log,true);
        u.start();
        d.start();
        auto deadline=Clock::now()+std::chrono::seconds(20);
        for(;;){
            auto uv=u.view(),dv=d.view();
            bool dr=mode=="DEVFEE_ONLY_FSA"?dv->ready:dv->state=="WAIT_JOB_AFTER_DIFFICULTY";
            if(uv->ready&&dr)break;
            require(Clock::now()<deadline&&!stop.requested(),"MOCK_READINESS_DEADLINE");
            stop.wait(std::chrono::milliseconds(5));
        }
        Work warm{};
        gpu.install(warm);
        for(unsigned i=0;i<256;++i){
            auto hits=gpu.scan(i*NativeBatch,NativeBatch,warm.target);
            require(hits.total==0&&!hits.overflow,"WARMUP_UNEXPECTED_HIT");
        }
        GpuWorker worker(gpu,u,d,budget,stop,log,device,60);
        worker.run(Clock::now()+std::chrono::seconds(300),std::uint64_t(NativeBatch)*8192);
        done.store(true);
        stop.request();
        u.join();
        d.join();
        u.finalize_queue();
        d.finalize_queue();
        auto& cc=worker.quota().counters().committed;
        auto total=cc[0]+cc[1];
        require(total==Count(std::uint64_t(NativeBatch)*8192),"PRODUCT_PROBE_INCOMPLETE");
        if(mode=="DEVFEE_ONLY_FSA")require(!(cc[1]==Count{}
        ),"PRODUCT_PROBE_FEE_MISSING");
        else require(cc[1]==Count{}
        ,"PRODUCT_PROBE_CONTROL_ROLE");
        require(io_stats().dns==0,"PRODUCT_PROBE_EXTERNAL_DNS");
        std::cout<<"{\"status\":\"PASS\",\"kind\":\"PRODUCT_LOCALHOST_GPU_HARNESS\",\"mode\":"<<json_quote(mode)<<",\"main_work_per_visit\":8589934592,\"warmup_separate\":268435456,\"connections_user\":"<<u.counts().connects<<",\"connections_developer\":"<<d.counts().connects<<",\"GPU_event_active_duration\":null,\"external_network\":false,\"GPU\":"<<gpu.accounting()<<"}\n";
        return 0;
    }
    catch(const Error&e){
        std::cerr<<"{\"status\":\"STOP\",\"code\":"<<json_quote(e.what())<<"}\n";
        return 2;
    }
    catch(...){
        std::cerr<<"PRODUCT_GPU_PROBE_UNEXPECTED\n";
        return 2;
    }
}
