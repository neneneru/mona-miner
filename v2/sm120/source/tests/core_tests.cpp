// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/accounting.hpp"
#include "mona2/session.hpp"
#include "mona2/cli.hpp"
#include <iostream>
#include <random>
#include <cmath>
#include <functional>
using namespace mona2;
static unsigned checks=0;
static void check(bool b){
    ++checks;
    require(b,"TEST_ASSERTION");
}
template<class F>void fails(F f){
    bool caught=false;
    try{
        f();
    }
    catch(const Error&){
        caught=true;
    }
    check(caught);
}
JobTemplate job(std::string id="job",bool clean=false){
    JobTemplate j;
    j.id=std::move(id);
    j.previous=Bytes(32,0);
    j.coinb1={
        1,2
    };
    j.coinb2={
        3,4
    };
    j.version={
        2,0,0,0
    };
    j.ntime={
        5,0,0,0
    };
    j.bits={
        1,0,0,0
    };
    j.clean=clean;
    return j;
}
void ready(SessionState&s,Time t){
    s.connected();
    s.difficulty(256);
    s.subscription({
        1,2
    }
    ,2);
    s.notify(job(),t);
    s.authorize(true);
}
struct NoHit:Backend {
    void install(const Work&)override{};
    RawHits scan(std::uint32_t,std::uint32_t,const Target&,bool)override{
        return {};
    }
};
struct AllHit:Backend {
    void install(const Work&)override{};
    RawHits scan(std::uint32_t f,std::uint32_t n,const Target&,bool)override{
        RawHits r;
        r.total=n;
        r.overflow=n>64;
        for(unsigned i=0;i<std::min(64u,n);++i)r.nonces[i]=f+i;
        return r;
    }
};
int main(){
    try{
        Count a(UINT64_MAX);
        a+=Count(1);
        check(a.hi==1&&a.lo==0&&a.decimal()=="18446744073709551616");
        check(a.times(100).decimal()=="1844674407370955161600");
        check((a-Count(1)).lo==UINT64_MAX);
        Count max;
        max.hi=max.lo=UINT64_MAX;
        fails([&]{
            max+=Count(1);
        }
        );
        for(auto s:{
            "","+1","-1","01","1x"," 1","1 ","1.0","18446744073709551616"
        }
        )fails([&]{
            decimal(s,UINT64_MAX);
        }
        );
        check(decimal("18446744073709551615",UINT64_MAX)==UINT64_MAX);
        for(auto d:{
            1.0,90.0,128.0,256.0,32768.0
        }
        ){
            check(upper(pool_target(d))>0);
        }
        for(auto d:{
            0.0,-1.0,1e-300,1e300,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()
        }
        )fails([&]{
            pool_target(d);
        }
        );
        for(auto s:{
            "stratum+tcp://u@host:10","stratum+tcp://host:10/path","stratum+tcp://host:0","stratum+tcp://host:999999999999999999999","https://host:80"
        }
        )fails([&]{
            endpoint(s);
        }
        );
        check(endpoint("stratum+tcp://[::1]:90").port==90);
        const std::string wire="{\"id\":2,\"result\":true}\n{\"method\":\"mining.notify\",\"params\":[]}\n";
        for(std::size_t i=0;i<=wire.size();++i){
            Framer f;
            auto x=f.feed(std::string_view(wire).substr(0,i));
            auto y=f.feed(std::string_view(wire).substr(i));
            check(x.size()+y.size()==2&&f.residual()==0);
        }
        {
            Framer f;
            fails([&]{
                f.feed(std::string(FrameMax+1,'x')+"\n");
            }
            );
        }
        fails([&]{
            parse_json("{\"x\":1,\"x\":2}");
        }
        );
        fails([&]{
            parse_json("{\"x\":\"\\u0000\"}");
        }
        );
        fails([&]{
            parse_json(std::string(34,'[')+"0"+std::string(34,']'));
        }
        );
        Time t=Clock::now();
        SessionState s(1,Authority::User,"dummy-user-endpoint"),d(1,Authority::Developer,"dummy-dev-endpoint");
        ready(s,t);
        ready(d,t);
        check(s.view(t).ready);
        ExtranonceRegistry xn;
        auto make=[&](){
            auto v=s.view(t).latest;
            return build_work(*v,xn.allocate(v->endpoint_key,v->xnonce1,v->xnonce2_size),1,0);
        };
        auto old=make();
        check(s.can_submit(*old,t));
        check(!d.can_submit(*old,t));
        auto j=job();
        j.coinb1={
            9
        };
        s.notify(j,t);
        check(!s.can_submit(*old,t));
        old=make();
        s.notify(job("second"),t);
        check(s.can_submit(*old,t));
        s.notify(job("third",true),t);
        check(!s.can_submit(*old,t));
        old=make();
        s.difficulty(90);
        check(!s.view(t).ready&&s.can_submit(*old,t));
        s.notify(job("after_diff"),t);
        check(s.view(t).ready&&s.view(t).latest->job.difficulty==90);
        s.extranonce({
            7
        }
        ,2);
        check(!s.view(t).ready&&!s.can_submit(*old,t));
        s.notify(job(),t);
        check(s.view(t).ready);
        s.disconnected();
        check(!s.view(t).ready);
        SessionState ordering(2,Authority::User,"order");
        ordering.connected();
        ordering.notify(job(),t);
        ordering.difficulty(32768);
        ordering.subscription({
            0
        }
        ,2);
        ordering.authorize(true);
        check(!ordering.view(t).ready);
        ordering.notify(job(),t);
        check(ordering.view(t).ready&&ordering.view(t).latest->job.difficulty==32768);
        check(!ordering.view(t+std::chrono::seconds(181)).ready);
        for(unsigned i=0;i<300;++i)ordering.notify(job(std::to_string(i)),t);
        check(ordering.registry_size()==256);
        check(ordering.view(t).registry_evicted==45);
        ordering.expire(t+std::chrono::seconds(181));
        check(ordering.view(t).registry_expired==256);
        {
            ExtranonceRegistry r;
            for(unsigned i=0;i<65536;++i){
                auto v=r.allocate("prefix",{
                    1
                }
                ,2);
                check(v[0]==(i&255)&&v[1]==(i>>8));
            }
            fails([&]{
                r.allocate("prefix",{
                    1
                }
                ,2);
            }
            );
            check(r.allocate("prefix",{
                2
            }
            ,2)==Bytes({
                0,0
            }
            ));
        }
        std::mt19937_64 rng(20261001);
        std::uint64_t commits=0;
        for(unsigned trial=0;trial<12;++trial){
            QuotaFrame q(17);
            std::uint64_t limit=(1000+trial)*q.quantum(),total=0,ticks=0;
            while(total<limit){
                ++ticks;
                bool user=true,dev=true;
                if(trial>0&&ticks%191==0)dev=false;
                if(trial>1&&ticks%307==0)user=false;
                auto c=q.choose(user,dev,3+rng()%NonceSpace);
                if(!c){
                    check(!user);
                    q.availability(true,true);
                    continue;
                }
                auto n=std::min<std::uint64_t>(c->count,limit-total);
                q.scheduled(c->role,n);
                q.actual(c->role,n);
                q.commit(c->role,n,c->segment);
                total+=n;
                ++commits;
                check(q.fee_error()>=0&&std::uint64_t(q.fee_error())<=196*q.quantum());
            }
            q.finish();
            auto& c=q.counters();
            auto H=c.committed[0]+c.committed[1];
            check(H.times(2)-c.committed[1].times(100)==c.outage_user.times(2)+c.forgiven_fee_numerator);
        }
        {
            QuotaFrame q;
            auto c=q.choose(true,true,NonceSpace);
            check(c&&c->role==Role::User);
            q.commit(c->role,100,c->segment);
            q.availability(true,false);
            auto f=q.choose(true,true,NonceSpace);
            check(f&&f->role==Role::User&&q.remaining()==98*q.quantum());
        }
        {
            auto w=std::make_shared<Work>();
            w->target.fill(UINT32_MAX);
            w->key.session.authority=Authority::User;
            QuotaFrame q;
            auto c=q.choose(true,true,NonceSpace);
            Assignment as{
                w,Role::User,1,c->segment,UINT32_MAX-64,65
            };
            RangeTransaction tr(as);
            Mailbox box;
            AllHit b;
            std::uint64_t actual=0,unique=0;
            while(!tr.done()){
                auto r=tr.step(b,box,q,[]{
                    return true;
                }
                ,[](const Words&,std::uint32_t){
                    return Target{};
                }
                );
                actual+=r.actual;
                unique+=r.committed;
            }
            check(actual==130&&unique==65&&tr.cursor()==NonceSpace);
            unsigned hits=0;
            while(box.pop())++hits;
            check(hits==65);
            fails([&]{
                tr.step(b,box,q,[]{
                    return true;
                }
                ,[](const Words&,std::uint32_t){
                    return Target{};
                }
                );
            }
            );
        }
        {
            Mailbox box(64);
            auto a=box.reserve();
            check(bool(a));
            check(!box.reserve());
            a.reset();
            check(box.has_capacity());
        }
        {
            ShareBudget b(1);
            NetworkCounts n;
            SubmitBook book(Authority::User,b,n);
            auto w=std::make_shared<Work>();
            w->key.session.authority=Authority::User;
            Candidate c{
                {
                    w,Role::User,1,1,0,1
                }
                ,0,{}
            };
            auto id=book.reserve(c,t);
            check(id&&b.counts()[1]==1);
            check(!book.reserve(c,t));
            book.wrote(*id,1,false);
            book.disconnect();
            check(b.counts()[2]==1&&b.terminal()=="SHARE_LIMIT_UNCERTAIN_STOP");
            check(!book.ack(*id,true));
        }
        {
            ShareBudget b(2);
            NetworkCounts n;
            SubmitBook book(Authority::User,b,n);
            auto w=std::make_shared<Work>();
            Candidate c{
                {
                    w,Role::User,1,1,0,1
                }
                ,0,{}
            };
            auto id=book.reserve(c,t);
            book.wrote(*id,10,true);
            check(book.ack(*id,false)&&b.can_admit());
            auto id2=book.reserve(c,t);
            check(id2&&*id2>*id);
            book.wrote(*id2,10,true);
            book.ack(*id2,true);
            check(!book.ack(*id2,true)&&b.counts()[0]==1);
            auto id3=book.reserve(c,t);
            book.disconnect();
            check(b.counts()[1]==0&&b.counts()[2]==0);
            check(*id3>*id2);
        }
        {
            Words w{};
            auto h=cpu_hash(w,0);
            check(h==cpu_hash(w,0));
            Target pre{};
            check(cpu_hash(w,0,&pre)==h&&pre!=h);
        }
        std::cout<<"{\"status\":\"PASS\",\"checks\":"<<checks<<",\"accounting_commits\":"<<commits<<",\"GPU_executions\":0}"<<std::endl;
        return 0;
    }
    catch(const std::exception&e){
        std::cerr<<"FAIL "<<e.what()<<" checks="<<checks<<'\n';
        return 1;
    }
}
