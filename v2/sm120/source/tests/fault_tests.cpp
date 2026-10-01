// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/actor.hpp"
#include <iostream>
#include <cmath>
using namespace mona2;
static unsigned checks=0;
void ck(bool b){++checks;require(b,"FAULT_TEST");}
template<class F>void fails(F f){bool yes=false;try{f();}catch(const Error&){yes=true;}ck(yes);}
std::string notify(){return "{\"method\":\"mining.notify\",\"params\":[\"job\",\""+std::string(64,'0')+"\",\"0102\",\"0304\",[],\"02000000\",\"1d00ffff\",\"01000000\",true]}\n";}
void flush(Protocol&p,Time t){while(p.wants_write()){auto s=p.writable(t);if(s.empty())break;p.wrote(s.size(),t);}}
void ready(Protocol&p,Time t){p.begin(t);flush(p,t);p.receive("{\"id\":1,\"result\":[[],\"abcd\",2]}\n{\"method\":\"mining.set_difficulty\",\"params\":[90]}\n",t);flush(p,t);p.receive("{\"id\":2,\"result\":true}\n"+notify(),t);}
struct BadHits:Backend{unsigned mode;explicit BadHits(unsigned m):mode(m){};void install(const Work&)override{};RawHits scan(std::uint32_t f,std::uint32_t n,const Target&,bool)override{RawHits r;if(mode==0){r.total=n+1;r.overflow=1;}if(mode==1){r.total=1;r.overflow=1;}if(mode==2){r.total=2;r.nonces[0]=r.nonces[1]=f;}if(mode==3){r.total=1;r.nonces[0]=f+n;}return r;}};
int main(){try{
 Time t=Clock::now();Credentials c{endpoint("stratum+tcp://127.0.0.1:1"),"fault-user","fault-only"};
 for(bool partial:{false,true}){ShareBudget budget(1);Protocol p(44,Authority::User,c,budget);ready(p,t);auto v=p.view(t);auto w=build_work(*v.latest,{0,0},1,0);Candidate cand{{w,Role::User,1,1,0,1},0,{}};ck(p.offer(cand,t));if(partial)p.wrote(1,t);p.receive(notify(),t);ck(p.writable(t).empty());if(partial){ck(p.reconnect_requested());p.disconnect();ck(budget.counts()[2]==1);}else{ck(!p.reconnect_requested()&&budget.counts()[1]==0&&p.counts().local_stale[0]==1);}p.disconnect();}
 {ShareBudget budget(2);Protocol p(45,Authority::User,c,budget);ready(p,t);auto w=build_work(*p.view(t).latest,{0,0},1,0);ck(p.offer({{w,Role::User,1,1,0,1},0,{}},t));flush(p,t);p.receive("{\"id\":1000,\"result\":false,\"error\":[21,\"job not found\",null]}\n",t);ck(p.counts().rejected[0]==1&&p.counts().stale_pool[0]==1&&budget.counts()[1]==0);p.disconnect();}
 {ShareBudget budget(1);Protocol p(45,Authority::User,c,budget);ready(p,t);auto w=build_work(*p.view(t).latest,{0,0},1,0);p.offer({{w,Role::User,1,1,0,1},0,{}},t);flush(p,t);fails([&]{p.tick(t+std::chrono::seconds(61));});p.disconnect();ck(budget.terminal()=="SHARE_LIMIT_UNCERTAIN_STOP");}
 {ShareBudget b(0);NetworkCounts n;SubmitBook user(Authority::User,b,n),dev(Authority::Developer,b,n);auto wu=std::make_shared<Work>(),wd=std::make_shared<Work>();wd->key.session.authority=Authority::Developer;for(unsigned i=0;i<128;++i)ck(bool(user.reserve({{wu,Role::User,1,1,0,1},0,{}},t)));ck(user.full()&&!user.reserve({{wu,Role::User,1,1,0,1},0,{}},t));for(unsigned i=0;i<32;++i)ck(bool(dev.reserve({{wd,Role::DevFee,1,1,0,1},0,{}},t)));ck(dev.full());user.disconnect();dev.disconnect();ck(b.counts()[1]==0);}
 for(unsigned mode=0;mode<4;++mode){auto w=std::make_shared<Work>();w->target.fill(UINT32_MAX);QuotaFrame q;auto choice=q.choose(true,true,NonceSpace);Assignment a{w,Role::User,1,choice->segment,0,33};RangeTransaction tr(a);Mailbox box;BadHits bad(mode);fails([&]{tr.step(bad,box,q,[]{return true;},[](const Words&,unsigned){return Target{};});});ck(q.counters().committed[0]==Count{}&&box.size()==0&&box.has_capacity());}
 {auto w=std::make_shared<Work>();QuotaFrame q;auto choice=q.choose(true,true,NonceSpace);Assignment a{w,Role::User,1,choice->segment,0,33};RangeTransaction tr(a);Mailbox box;BadHits bad(0);auto r=tr.step(bad,box,q,[]{return false;},[](const Words&,unsigned){return Target{};});ck(r.cancelled&&r.actual==0&&r.committed==0);}
 for(auto bad:{std::string("\xC0\x80",2),std::string("\xED\xA0\x80",3),std::string("\xF4\x90\x80\x80",4)})fails([&]{validate_credential(bad,256);});
 for(auto limit:{std::numeric_limits<double>::denorm_min(),1e-200,1e-100})fails([&]{pool_target(limit);});
 {QuotaFrame q;Count total;for(unsigned i=0;i<14000;++i){auto c2=q.choose(true,true,NonceSpace);q.commit(c2->role,c2->count,c2->segment);total+=Count(c2->count);ck(q.fee_error()>=0);}ck(!(q.counters().committed[1]==Count{}));q.finish();}
 std::cout<<"{\"status\":\"PASS\",\"checks\":"<<checks<<",\"real_GPU_calls\":0}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
