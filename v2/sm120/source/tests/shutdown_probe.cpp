// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/actor.hpp"
#include <iostream>
using namespace mona2;
int main(int argc,char**argv){
    try{
        require(argc==3,"SHUTDOWN_PROBE_ARGS");
        auto p0=decimal(argv[1],65535),p1=decimal(argv[2],65535);
        StopSignal stop;
        NetworkRuntime runtime;
        AuditLog log;
        ShareBudget budget(1);
        std::atomic<bool> done{
            false
        };
        SessionActor u(9,Authority::User,{
            endpoint("stratum+tcp://127.0.0.1:"+std::to_string(p0)),"shutdown-user","user-dummy"
        }
        ,budget,stop,done,log,true);
        SessionActor d(9,Authority::Developer,{
            endpoint("stratum+tcp://127.0.0.1:"+std::to_string(p1)),"shutdown-developer","developer-dummy"
        }
        ,budget,stop,done,log,true);
        u.start();
        d.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto t=Clock::now();
        done.store(true);
        stop.request();
        u.join();
        d.join();
        u.finalize_queue();
        d.finalize_queue();
        auto elapsed=std::chrono::duration<double>(Clock::now()-t).count();
        require(elapsed<2,"SHUTDOWN_DEADLINE_TEST");
        require(io_stats().dns==0,"SHUTDOWN_EXTERNAL_DNS");
        std::cout<<"{\"status\":\"PASS\",\"cancel_seconds\":"<<elapsed<<",\"sockets\":"<<io_stats().sockets<<",\"GPU\":0}\n";
        return 0;
    }
    catch(const Error&e){
        std::cerr<<e.what()<<'\n';
        return 2;
    }
}
