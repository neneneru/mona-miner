// Uses production actors and transport with a hard loopback-only restriction.
#include "mona2/actor.hpp"
#include <iostream>
#include <thread>
using namespace mona2;
int main(int argc,char**argv){
    try{
        require(argc==3,"TWO_PORTS_REQUIRED");
        auto up=decimal(argv[1],65535),dp=decimal(argv[2],65535);
        StopSignal stop;
        NetworkRuntime net;
        AuditLog log;
        ShareBudget budget(0);
        std::atomic<bool>done=false;
        Credentials u{
            endpoint("stratum+tcp://127.0.0.1:"+std::to_string(up)),"mock-user-alpha","user-dummy-only"
        };
        Credentials d{
            endpoint("stratum+tcp://127.0.0.1:"+std::to_string(dp)),"mock-developer-beta","developer-dummy-only"
        };
        SessionActor user(101,Authority::User,u,budget,stop,done,log,true),dev(101,Authority::Developer,d,budget,stop,done,log,true);
        user.start();
        dev.start();
        auto end=Clock::now()+std::chrono::seconds(5);
        ExtranonceRegistry x;
        std::uint64_t serial=0;
        unsigned sent[RoleCount]={};
        std::uint64_t seen_connection[RoleCount]={};
        while(Clock::now()<end){
            for(Role role:{
                Role::User,Role::DevFee
            }
            ){
                auto&act=role==Role::User?user:dev;
                auto v=act.view();
                if(!v||!v->ready||!v->latest)continue;
                if(seen_connection[index(role)]!=v->latest->key.session.connection){
                    seen_connection[index(role)]=v->latest->key.session.connection;
                    sent[index(role)]=0;
                }
                if(sent[index(role)]>=4)continue;
                auto w=build_work(*v->latest,x.allocate(v->latest->endpoint_key,v->latest->xnonce1,v->latest->xnonce2_size),++serial,0);
                Target h{};
                unsigned nonce=0;
                for(;nonce<128;++nonce){
                    h=cpu_hash(w->words,nonce);
                    if(full_test(h,w->target))break;
                }
                if(nonce==128)continue;
                auto ticket=act.mailbox().reserve();
                if(!ticket)continue;
                Candidate c{
                    {
                        w,role,serial,1,nonce,1
                    }
                    ,nonce,h
                };
                ticket->publish(std::span(&c,1));
                ++sent[index(role)];
            }
            if(user.counts().accepted[0]>=2&&dev.counts().accepted[1]>=2)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        done=true;
        auto begin=Clock::now();
        stop.request();
        user.join();
        dev.join();
        double shutdown=std::chrono::duration<double>(Clock::now()-begin).count();
        auto uc=user.counts(),dc=dev.counts();
        require(shutdown<6,"SHUTDOWN_DEADLINE");
        require(uc.accepted[0]>=1&&dc.accepted[1]>=1,"LOOPBACK_ACCEPTS");
        require(uc.accepted[1]==0&&dc.accepted[0]==0,"CROSS_AUTHORITY_COUNTER");
        std::cout<<"{\"status\":\"PASS\",\"user_accepted\":"<<uc.accepted[0]<<",\"fee_accepted\":"<<dc.accepted[1]<<",\"shutdown_seconds\":"<<shutdown<<",\"GPU_executions\":0}"<<std::endl;
        return 0;
    }
    catch(const std::exception&e){
        std::cerr<<"FAIL "<<e.what()<<'\n';
        return 1;
    }
}
