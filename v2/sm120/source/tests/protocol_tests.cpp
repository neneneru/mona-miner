// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/protocol.hpp"
#include "mona2/actor.hpp"
#include <iostream>
using namespace mona2;
static unsigned n=0;
void check(bool b){
    ++n;
    require(b,"PROTOCOL_ASSERTION");
}
template<class F>void fails(F f){
    bool bad=false;
    try{
        f();
    }
    catch(const Error&){
        bad=true;
    }
    check(bad);
}
std::string notice="{\"method\":\"mining.notify\",\"params\":[\"same-job\",\""+std::string(64,'0')+"\",\"0102\",\"0304\",[],\"02000000\",\"1d00ffff\",\"01000000\",true]}\n";
std::string sub="{\"id\":1,\"result\":[[],\"abcd\",2],\"error\":null}\n";
std::string auth="{\"id\":2,\"result\":true,\"error\":null}\n";
std::string diff="{\"method\":\"mining.set_difficulty\",\"params\":[90]}\n";
void flush(Protocol&p,Time t){
    for(;;){
        auto x=p.writable(t);
        if(x.empty())break;
        p.wrote(x.size(),t);
    }
}
int main(){
    try{
        Time t=Clock::now();
        Credentials c{
            endpoint("stratum+tcp://127.0.0.1:1"),"mock-user","mock-user-secret"
        };
        ShareBudget b(0);
        for(unsigned split=0;split<=auth.size()+notice.size();++split){
            Protocol p(1,Authority::User,c,b);
            p.begin(t);
            flush(p,t);
            p.receive(diff+sub,t);
            flush(p,t);
            auto a=auth+notice;
            p.receive(std::string_view(a).substr(0,split),t);
            p.receive(std::string_view(a).substr(split),t);
            check(p.view(t).ready&&p.view(t).latest->job.difficulty==90);
            p.receive(sub+auth,t);
            check(p.view(t).ready&&p.counts().ignored_acks==2);
            p.disconnect();
        }
        {
            Protocol p(1,Authority::User,c,b);
            p.begin(t);
            flush(p,t);
            p.receive(sub,t);
            flush(p,t);
            p.receive(notice+auth+diff,t);
            check(!p.view(t).ready);
            p.receive(notice,t);
            check(p.view(t).ready);
            p.receive("{\"method\":\"client.reconnect\",\"params\":[\"attacker.invalid\",9999]}\n",t);
            check(p.reconnect_requested());
            check(p.endpoint_value().host=="127.0.0.1");
            p.disconnect();
        }
        {
            Protocol p(1,Authority::User,c,b);
            p.begin(t);
            flush(p,t);
            p.receive(sub+diff+notice,t);
            flush(p,t);
            p.receive(auth,t);
            auto v=p.view(t);
            check(v.ready);
            ExtranonceRegistry xr;
            auto w=build_work(*v.latest,xr.allocate(v.latest->endpoint_key,v.latest->xnonce1,v.latest->xnonce2_size),1,0);
            Candidate ca{
                {
                    w,Role::User,1,1,0,1
                }
                ,0,{}
            };
            check(p.offer(ca,t));
            auto text=p.writable(t);
            check(text.find("mock-user")!=text.npos&&text.find("mock-user-secret")==text.npos);
            p.wrote(1,t);
            p.disconnect();
            check(b.counts()[2]==1);
            p.begin(t);
            flush(p,t);
            p.receive(sub+diff+notice,t);
            flush(p,t);
            p.receive(auth,t);
            p.receive("{\"id\":1000,\"result\":true,\"error\":null}\n",t);
            check(p.counts().accepted[0]==0&&p.counts().ignored_acks>0);
            check(!p.can_submit(*w,t));
            p.disconnect();
        }
        {
            Protocol p(1,Authority::User,c,b);
            p.begin(t);
            fails([&]{
                p.tick(t+std::chrono::seconds(16));
            }
            );
            p.disconnect();
        }
        for(auto line:{
            "{\"method\":\"mining.set_difficulty\",\"params\":[true]}\n","{\"id\":1,\"result\":[[],\"ab\",2.0]}\n","{\"id\":1,\"result\":[[],\"ab\",1]}\n"
        }
        ){
            Protocol p(1,Authority::User,c,b);
            p.begin(t);
            flush(p,t);
            fails([&]{
                p.receive(line,t);
            }
            );
            p.disconnect();
        }
        {
            auto j=parse_json(notice.substr(0,notice.size()-1));
            auto*p=json_object_get(j.get(),"params");
            json_array_set_new(p,8,json_integer(1));
            fails([&]{
                parse_notify(p);
            }
            );
        }
        {
            AuditLog l;
            l.secret("private-value");
            check(l.sanitize("A\nprivate-value\x1b")=="A\\u000a[REDACTED]\\u001b");
        }
        {
            std::uint64_t s=1;
            for(unsigned f=1;f<20;++f){
                auto delay=backoff_ms(f,s);
                check(delay<=(f>=10?66000u:33000u));
            }
        }
        std::cout<<"{\"status\":\"PASS\",\"checks\":"<<n<<",\"GPU_executions\":0}"<<std::endl;
        return 0;
    }
    catch(const std::exception&e){
        std::cerr<<"FAIL "<<e.what()<<" n="<<n<<'\n';
        return 1;
    }
}
