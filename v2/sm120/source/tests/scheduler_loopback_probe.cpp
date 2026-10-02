// SPDX-License-Identifier: GPL-3.0-or-later
// Actual actors/transport/worker; no-hit backend is explicitly a CPU test double.
#include "mona2/worker.hpp"
#include <iostream>
using namespace mona2;
struct NoHitBackend:Backend {
    std::uint64_t installs=0,scans=0,hashes=0; Work current{};
    void install(const Work&w)override{current=w;++installs;}
    RawHits scan(std::uint32_t f,std::uint32_t n,const Target&t,bool)override{
        require(n>0&&n<=NativeBatch&&std::uint64_t(f)+n<=NonceSpace,"TEST_SCAN_RANGE");
        require(t==current.target,"TEST_TARGET_BINDING");++scans;hashes=add64(hashes,n);return {};
    }
};
int main(int argc,char**argv){try{
 require(argc==3,"TWO_PORTS_REQUIRED");auto up=decimal(argv[1],65535),dp=decimal(argv[2],65535);
 StopSignal stop;NetworkRuntime net;AuditLog log;ShareBudget budget(0);std::atomic<bool>done=false;
 Credentials uc{endpoint("stratum+tcp://127.0.0.1:"+std::to_string(up)),"mock-user-alpha","user-dummy-only"};
 Credentials dc{endpoint("stratum+tcp://127.0.0.1:"+std::to_string(dp)),"mock-developer-beta","developer-dummy-only"};
 SessionActor u(431,Authority::User,uc,budget,stop,done,log,true),d(431,Authority::Developer,dc,budget,stop,done,log,true);
 u.start();d.start();auto deadline=Clock::now()+std::chrono::seconds(15);
 while(!u.view()->ready||!d.view()->ready){require(Clock::now()<deadline,"MOCK_READINESS");stop.wait(std::chrono::milliseconds(2));}
 NoHitBackend backend;GpuWorker worker(backend,u,d,budget,stop,log,0,60);
 worker.run(Clock::now()+std::chrono::seconds(15),std::uint64_t(6400)*NativeBatch);
 done=true;stop.request();u.join();d.join();u.finalize_queue();d.finalize_queue();
 const auto&c=worker.quota().counters();
 require(c.committed[0]==Count(std::uint64_t(6272)*NativeBatch)&&c.committed[1]==Count(std::uint64_t(128)*NativeBatch),"MOCK_98_2_ALLOCATION");
 require(backend.scans==6400&&backend.hashes==std::uint64_t(6400)*NativeBatch,"MOCK_WORK_ACCOUNTING");
 require(u.counts().connects==1&&d.counts().connects==1,"MOCK_RECONNECTED_PER_EPISODE");
 require(io_stats().dns==0,"MOCK_EXTERNAL_DNS");
 std::cout<<"{\"status\":\"PASS\",\"test_backend\":\"NO_HIT_CPU_DOUBLE\",\"scans\":6400,\"USER_scans\":6272,\"DEVFEE_scans\":128,\"user_connections\":1,\"developer_connections\":1,\"GPU_executions\":0}\n";
 return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
