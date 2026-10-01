// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/actor.hpp"
#include <algorithm>
#include <iostream>
namespace mona2 {
    void AuditLog::secret(std::string s){
        if(s.empty())return;
        std::lock_guard l(mutex_);
        secrets_.push_back(std::move(s));
        std::sort(secrets_.begin(),secrets_.end(),[](auto&a,auto&b){
            return a.size()>b.size();
        }
        );
    }
    std::string AuditLog::sanitize(std::string_view in)const{
        std::lock_guard l(mutex_);
        std::string s(in);
        for(const auto& secret:secrets_){
            std::size_t p=0;
            while((p=s.find(secret,p))!=s.npos){
                s.replace(p,secret.size(),"[REDACTED]");
                p+=10;
            }
        }
        return escape_controls(s);
    }
    void AuditLog::event(Authority a,const char*c,std::uint64_t conn){
        std::lock_guard l(mutex_);
        std::cout<<"{\"event\":"<<json_quote(c)<<",\"monotonic_ns\":\""<<std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()<<"\",\"authority\":"<<json_quote(a==Authority::User?"USER":"DEVELOPER")<<",\"connection\":\""<<conn<<"\"}"<<std::endl;
    }
    void AuditLog::line(std::string_view s){
        std::lock_guard l(mutex_);
        std::cout<<s<<std::endl;
    }
    std::uint32_t backoff_ms(unsigned f,std::uint64_t& state){
        static constexpr unsigned bases[]={
            1000,2000,4000,8000,16000,30000
        };
        unsigned base=f>=10?60000:bases[std::min<unsigned>(f?f-1:0,5)];
        state^=state<<13;
        state^=state>>7;
        state^=state<<17;
        return base+std::uint32_t(state%(base/10+1));
    }
    SessionActor::SessionActor(std::uint64_t run,Authority a,Credentials c,ShareBudget&b,StopSignal&s,std::atomic<bool>&d,AuditLog&l,bool local,bool all):authority_(a),protocol_(run,a,c,b),budget_(b),stop_(s),producer_done_(d),log_(l),loopback_(local),all_(all),random_(run+index(a)+1){
        validate_credential(c.worker,256);
        validate_credential(c.password,1024,true);
        log_.secret(c.password);
        log_.secret(c.worker);
        log_.secret(c.endpoint.canonical());
        view_.store(std::make_shared<SessionView>());
    }
    SessionActor::~SessionActor(){
        if(thread_.joinable()){
            stop_.request();
            thread_.request_stop();
            thread_.join();
        }
    }
    void SessionActor::start(){
        require(!thread_.joinable(),"ACTOR_ALREADY_STARTED");
        thread_=std::jthread([this](std::stop_token token){
            std::stop_callback cb(token,[this]{
                stop_.request();
            }
            );run();
        }
        );
    }
    void SessionActor::join(){
        if(thread_.joinable())thread_.join();
    }
    void SessionActor::finalize_queue(){
        require(!thread_.joinable(),"ACTOR_STILL_RUNNING");
        require(producer_done_.load(),"PRODUCER_STILL_RUNNING");
        while(auto c=mailbox_.pop())protocol_.unsent(c->assignment.role);
        mailbox_.close();
        publish(Clock::now(),true);
    }
    void SessionActor::publish(Time now,bool stopping){
        auto v=protocol_.view(now);
        if(stopping||!mailbox_.has_capacity()||(authority_==Authority::User&&!budget_.can_admit())){
            v.ready=false;
            v.state=stopping?"STOPPING":"DEGRADED_BACKPRESSURE";
        }
        view_.store(std::make_shared<SessionView>(std::move(v)));
        std::lock_guard l(counts_mu_);
        counts_=protocol_.counts();
    }
    void SessionActor::run()noexcept{
        try{
            unsigned failures=0;
            std::uint64_t expired_seen=0,evicted_seen=0;
            while(!stop_.requested()){
                Transport transport;
                std::optional<Candidate> retained;
                auto began=Clock::now();
                try{
                    log_.event(authority_,"CONNECTING");
                    transport.connect(protocol_.endpoint_value(),stop_,loopback_);
                    protocol_.begin(Clock::now());
                    publish(Clock::now());
                    std::string last_state;
                    std::uint64_t lease_warning=0;
                    std::optional<Time> stop_at;
                    while(!protocol_.reconnect_requested()){
                        auto now=Clock::now();
                        if(stop_.requested()&&!stop_at)stop_at=now+std::chrono::seconds(5);
                        if(stop_at&&now>=*stop_at)break;
                        // Apply all buffered complete control frames before share admission. If a
                        // read turn hits the byte budget, skip writes and continue draining.
                        std::array<char,8192> bytes{};
                        std::size_t read=0;
                        bool drained=false;
                        while(read<65536){
                            auto n=transport.read(bytes);
                            if(!n){
                                drained=true;
                                break;
                            }
                            read+=*n;
                            auto before=protocol_.counts();
                            protocol_.receive(std::string_view(bytes.data(),*n),now);
                            if(all_){
                                const auto&after=protocol_.counts();
                                for(unsigned ri=0;ri<RoleCount;++ri)if(before.accepted[ri]!=after.accepted[ri]||before.rejected[ri]!=after.rejected[ri])log_.line("{\"event\":\"SHARE_TOTAL\",\"role\":"+json_quote(role_name(Role(ri)))+",\"accepted\":"+std::to_string(after.accepted[ri])+",\"rejected\":"+std::to_string(after.rejected[ri])+"}");
                            }
                        }
                        protocol_.tick(now);
                        publish(now,bool(stop_at));
                        auto state=protocol_.view(now);
                        if(state.registry_expired!=expired_seen||state.registry_evicted!=evicted_seen){
                            expired_seen=state.registry_expired;evicted_seen=state.registry_evicted;
                            log_.line("{\"event\":\"JOB_REGISTRY_RETIREMENT\",\"authority\":"+json_quote(authority_==Authority::User?"USER":"DEVELOPER")+",\"expired\":"+std::to_string(expired_seen)+",\"capacity_evicted\":"+std::to_string(evicted_seen)+"}");
                        }
                        if(state.latest&&now-state.latest->received>=std::chrono::seconds(90)&&lease_warning!=state.latest->key.revision){
                            lease_warning=state.latest->key.revision;
                            log_.event(authority_,"JOB_LEASE_90_SECOND_WARNING",state.latest->key.session.connection);
                        }
                        if(state.state!=last_state){
                            log_.event(authority_,state.state.c_str(),state.latest?state.latest->key.session.connection:0);
                            last_state=state.state;
                            if(state.ready&&state.latest)log_.line("{\"event\":\"EFFECTIVE_DIFFICULTY\",\"authority\":"+json_quote(authority_==Authority::User?"USER":"DEVELOPER")+",\"difficulty\":"+std::to_string(state.latest->job.difficulty)+"}");
                        }
                        if(drained){
                            for(unsigned sends=0;sends<16;++sends){
                                if(!protocol_.wants_write()){
                                    if(!retained)retained=mailbox_.pop();
                                    if(retained){
                                        if(protocol_.offer(*retained,now))retained.reset();
                                        else break;
                                    }
                                }
                                auto out=protocol_.writable(now);
                                if(out.empty())break;
                                auto n=transport.write(out);
                                if(!n)break;
                                protocol_.wrote(*n,now);
                            }
                        }
                        const auto terminal=budget_.terminal();
                        if(!terminal.empty()){
                            log_.event(authority_,terminal.c_str());
                            stop_.request();
                        }
                        if(stop_at&&producer_done_.load()&&!retained&&mailbox_.size()==0&&protocol_.pending()==0&&!protocol_.wants_write())break;
                        if(read<65536){
                            if(stop_at)std::this_thread::sleep_for(std::chrono::milliseconds(2));
                            else transport.wait(stop_,protocol_.wants_write(),std::chrono::milliseconds(20));
                        }
                    }
                }
                catch(const Error&e){
                    log_.event(authority_,e.what());
                    const std::string code=e.what();if(code=="REQUEST_ID_EXHAUSTED"||code=="COUNTER_OVERFLOW"||code=="SUBMIT_ORIGIN"||code=="CANDIDATE_ORIGIN"||code=="CANDIDATE_VALIDATION_CONTRACT")stop_.request();
                }
                catch(const std::exception&){
                    log_.event(authority_,"ACTOR_RUNTIME_EXCEPTION");
                    stop_.request();
                }
                // Socket/overlapped objects are destroyed only by this owner.
                protocol_.disconnect();
                transport.close();
                if(retained){
                    protocol_.unsent(retained->assignment.role);
                    retained.reset();
                }
                publish(Clock::now(),stop_.requested());
                const auto terminal=budget_.terminal();
                if(!terminal.empty()){
                    log_.event(authority_,terminal.c_str());
                    stop_.request();
                }
                if(stop_.requested())break;
                if(Clock::now()-began>=std::chrono::seconds(30))failures=0;
                failures=std::min(failures+1,10u);
                auto delay=backoff_ms(failures,random_);
                log_.line("{\"event\":\"RECONNECT_BACKOFF\",\"authority\":"+json_quote(authority_==Authority::User?"USER":"DEVELOPER")+",\"milliseconds\":"+std::to_string(delay)+"}");
                stop_.wait(std::chrono::milliseconds(delay));
            }
            // No producer reservation is closed underneath an unfinished GPU scan. Queued
            // candidates arriving during shutdown are counted explicitly as not sent.
            while(auto c=mailbox_.pop())protocol_.unsent(c->assignment.role);
            publish(Clock::now(),true);
        }
        catch(...){
            log_.event(authority_,"ACTOR_FATAL");
            stop_.request();
        }
    }
}
