// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/actor.hpp"
#include "mona2/worker.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
using namespace mona2;
static void check(bool b){require(b,"CONSOLE_ASSERTION");}
static std::size_t occurrences(const std::string&s,const std::string&needle){
    std::size_t n=0,p=0;
    while((p=s.find(needle,p))!=std::string::npos){++n;p+=needle.size();}
    return n;
}
int main(){
    try{
        std::ostringstream human_out;
        auto* old=std::cout.rdbuf(human_out.rdbuf());
        {
            AuditLog human(false,false);
            human.console("Mona Miner - Lyra2REv2");
            human.console("Pool: stratum+tcp://stratum1.vippool.net:8888");
            human.gpu_identity(0,"{\"event\":\"GPU_IDENTITY\",\"SM\":120,\"name\":\"NVIDIA GeForce RTX 5090\"}");
            human.console("Developer Fee: 2%");
            human.event(Authority::User,"WAIT_JOB_AFTER_DIFFICULTY",1);
            human.event(Authority::User,"READY",1);
            human.event(Authority::Developer,"READY",1);
            human.summary(412.12,4,0,256.0);
            human.summary(413.00,5,0,256.0);
            human.reconnect_backoff(Authority::Developer,1000);
            human.line("{\"event\":\"INTERNAL_ONLY\"}");
        }
        std::cout.rdbuf(old);
        const auto h=human_out.str();
        check(h.find("Mona Miner - Lyra2REv2")!=std::string::npos);
        check(h.find("Pool: stratum+tcp://stratum1.vippool.net:8888")!=std::string::npos);
        check(h.find("GPU #0: NVIDIA GeForce RTX 5090, SM 12.0")!=std::string::npos);
        check(h.find("Developer Fee: 2%")!=std::string::npos);
        check(occurrences(h,"Stratum authorized")==1);
        check(h.find("412.12 MH/s | accepted: 4/4 (+4) | diff 256")!=std::string::npos);
        check(h.find("413.00 MH/s | accepted: 5/5 (+1) | diff 256")!=std::string::npos);
        check(h.find("INTERNAL_ONLY")==std::string::npos);
        check(h.find("DEVELOPER")==std::string::npos);
        check(h.find("reconnecting")==std::string::npos);

        std::ostringstream all_out;
        old=std::cout.rdbuf(all_out.rdbuf());
        {
            AuditLog all(false,true);
            all.share_total(Authority::Developer,Role::DevFee,1,0,256.0);
            all.share_total(Authority::User,Role::User,1,0,256.0);
            all.share_total(Authority::User,Role::User,2,0,256.0);
            all.share_total(Authority::User,Role::User,2,1,256.0);
        }
        std::cout.rdbuf(old);
        const auto a=all_out.str();
        check(a.find("DEVFEE")==std::string::npos);
        check(a.find("accepted: 1/1 (diff 256), yes!")!=std::string::npos);
        check(a.find("accepted: 2/2 (diff 256), yes!")!=std::string::npos);
        check(a.find("rejected: 1/3")!=std::string::npos);

        MiningRateWindow window;
        const Time zero{};
        window.start(zero+std::chrono::seconds(40),Count(0));
        check(!window.sample(zero+std::chrono::seconds(99),Count(24000000000ull),60));
        auto mhs=window.sample(zero+std::chrono::seconds(100),Count(24727200000ull),60);
        check(bool(mhs)&&std::abs(*mhs-412.12)<0.0001);

        // Job changes do not reset an active display window; the next interval stays continuous.
        check(!window.sample(zero+std::chrono::seconds(159),Count(48727200000ull),60));
        auto next=window.sample(zero+std::chrono::seconds(160),Count(49454400000ull),60);
        check(bool(next)&&std::abs(*next-412.12)<0.0001);

        // USER unavailability explicitly resets the window.
        window.reset();
        check(!window.sample(zero+std::chrono::seconds(220),Count(70000000000ull),60));
        window.start(zero+std::chrono::seconds(220),Count(70000000000ull));
        check(!window.sample(zero+std::chrono::seconds(279),Count(94000000000ull),60));
        auto after_reset=window.sample(zero+std::chrono::seconds(280),Count(94727200000ull),60);
        check(bool(after_reset)&&std::abs(*after_reset-412.12)<0.0001);

        std::ostringstream json_out;
        old=std::cout.rdbuf(json_out.rdbuf());
        {
            AuditLog json(true,false);
            json.event(Authority::User,"READY",1);
        }
        std::cout.rdbuf(old);
        check(json_out.str().find("\"event\":\"READY\"")!=std::string::npos);

        std::cout<<"{\"status\":\"PASS\",\"type\":\"CONSOLE_UI\",\"GPU_calls\":0}\n";
        return 0;
    }catch(const std::exception&e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
