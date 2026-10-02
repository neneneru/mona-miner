// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/session.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace mona2 {
    namespace {
        std::uint32_t le(const std::uint8_t*p){
            return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);
        }
        std::uint32_t be(const std::uint8_t*p){
            return std::uint32_t(p[3])|(std::uint32_t(p[2])<<8)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[0])<<24);
        }
        std::string canonical(const JobTemplate& j){
            std::string s;
            auto append=[&](std::string_view b){
                s+=std::to_string(b.size());
                s+=':';
                s+=b;
            };
            append(j.id);
            append(hex(j.previous));
            append(hex(j.coinb1));
            append(hex(j.coinb2));
            append(std::to_string(j.merkle.size()));
            for(auto& m:j.merkle)append(hex(m));
            append(hex(j.version));
            append(hex(j.bits));
            append(hex(j.ntime));
            append(j.clean?"true":"false");
            for(auto w:j.target)append(std::to_string(w));
            return s;
        }
    }
    void SessionState::connected(){
        next64(key_.connection);
        connected_=true;
        subscribed_=authorized_=explicit_diff_=false;
        latest_.reset();
        valid_.clear();
        x1_.clear();
        seq_=diff_seq_=sub_seq_=0;
        next64(view_version_);
    }
    void SessionState::disconnected(){
        connected_=subscribed_=authorized_=explicit_diff_=false;
        latest_.reset();
        valid_.clear();
        next64(view_version_);
    }
    void SessionState::subscription(Bytes x,std::size_t n){
        require(connected_&&n>=2&&n<=16&&x.size()<=256,"SUBSCRIPTION_INVALID");
        x1_=std::move(x);
        x2_size_=n;
        subscribed_=true;
        sub_seq_=next64(seq_);
        next64(sub_epoch_);
        next64(x_epoch_);
        latest_.reset();
        valid_.clear();
        next64(view_version_);
    }
    void SessionState::extranonce(Bytes x,std::size_t n){
        require(subscribed_,"EXTRANONCE_BEFORE_SUBSCRIBE");
        subscription(std::move(x),n);
    }
    void SessionState::difficulty(double d){
        (void)pool_target(d);
        next_diff_=d;
        explicit_diff_=true;
        diff_seq_=next64(seq_);
        next64(diff_revision_);
        next64(view_version_);
    }
    void SessionState::authorize(bool b){
        require(connected_&&subscribed_&&b,"AUTHORIZATION_INVALID");
        authorized_=true;
        next64(seq_);
        next64(view_version_);
    }
    void SessionState::notify(JobTemplate j,Time now){
        next64(seq_);
        next64(view_version_);
        expire(now);
        if(j.clean)valid_.clear();
        valid_.erase(std::remove_if(valid_.begin(),valid_.end(),[&](const auto&v){
            return v->job.id==j.id;
        }
        ),valid_.end());
        if(!subscribed_||!explicit_diff_||seq_<=diff_seq_||seq_<=sub_seq_){
            latest_.reset();
            return;
        }
        j.difficulty=next_diff_;
        j.target=pool_target(next_diff_);
        auto v=std::make_shared<JobView>();
        v->key={
            key_,sub_epoch_,x_epoch_,next64(job_revision_),diff_revision_,fingerprint(canonical(j))
        };
        v->job=std::move(j);
        v->xnonce1=x1_;
        v->xnonce2_size=x2_size_;
        v->received=now;
        v->endpoint_key=endpoint_;
        if(valid_.size()>=256){valid_.erase(valid_.begin());next64(registry_evicted_);}
        valid_.push_back(v);
        latest_=std::move(v);
    }
    void SessionState::expire(Time now){
        const auto before=valid_.size();
        valid_.erase(std::remove_if(valid_.begin(),valid_.end(),[&](const auto&v){
            return now-v->received>=std::chrono::seconds(180);
        }
        ),valid_.end());
        registry_expired_=add64(registry_expired_,before-valid_.size());
        if(latest_&&now-latest_->received>=std::chrono::seconds(180)){
            latest_.reset();
            next64(view_version_);
        }
    }
    SessionView SessionState::view(Time now)const{
        SessionView v;
        v.version=view_version_;
        v.registry_expired=registry_expired_;v.registry_evicted=registry_evicted_;
        v.latest=latest_;
        v.ready=connected_&&subscribed_&&authorized_&&explicit_diff_&&latest_&&latest_->key.target_revision==diff_revision_&&latest_->key.extranonce==x_epoch_&&now-latest_->received<std::chrono::seconds(180);
        v.state=!connected_?"DISCONNECTED":!subscribed_?"CONNECTED":!authorized_?"SUBSCRIBED":!explicit_diff_?"AUTHORIZED_WAIT_DIFFICULTY":!v.ready?"WAIT_JOB_AFTER_DIFFICULTY":"READY";
        return v;
    }
    bool SessionState::can_submit(const Work&w,Time now)const{
        if(!connected_||!authorized_||!subscribed_||!(w.key.session==key_)||w.key.extranonce!=x_epoch_||w.key.subscription!=sub_epoch_)return false;
        for(const auto& v:valid_)if(v->key==w.key&&v->job.id==w.job_id&&v->job.target==w.target&&v->job.difficulty==w.pool_difficulty&&now-v->received<std::chrono::seconds(180))return true;
        return false;
    }
    JobTemplate parse_notify(json_t*p){
        require(json_is_array(p)&&json_array_size(p)==9,"NOTIFY_ARRAY");
        JobTemplate j;
        j.id=json_string(json_array_get(p,0),256,false);
        validate_credential(j.id,256);
        j.previous=unhex(json_string(json_array_get(p,1),64,false),32);
        require(j.previous.size()==32,"PREVHASH_SIZE");
        j.coinb1=unhex(json_string(json_array_get(p,2),FrameMax/2),FrameMax/4);
        j.coinb2=unhex(json_string(json_array_get(p,3),FrameMax/2),FrameMax/4);
        auto*m=json_array_get(p,4);
        require(json_is_array(m)&&json_array_size(m)<=256,"MERKLE_COUNT");
        for(std::size_t i=0;i<json_array_size(m);++i){
            auto b=unhex(json_string(json_array_get(m,i),64,false),32);
            require(b.size()==32,"MERKLE_SIZE");
            Hash256 h{};
            std::copy(b.begin(),b.end(),h.begin());
            j.merkle.push_back(h);
        }
        auto h4=[&](unsigned i){
            auto b=unhex(json_string(json_array_get(p,i),8,false),4);
            require(b.size()==4,"HEADER_WORD_SIZE");
            std::array<std::uint8_t,4> v{};
            std::copy(b.begin(),b.end(),v.begin());
            return v;
        };
        j.version=h4(5);
        j.bits=h4(6);
        j.ntime=h4(7);
        require(json_is_boolean(json_array_get(p,8)),"CLEAN_BOOLEAN");
        j.clean=json_is_true(json_array_get(p,8));
        return j;
    }
    Bytes ExtranonceRegistry::allocate(const std::string&e,const Bytes&x,std::size_t n){
        require(n>=2&&n<=16,"EXTRANONCE_WIDTH");
        auto key=e+"|"+hex(x)+"|"+std::to_string(n);
        auto i=prefixes_.find(key);
        if(i==prefixes_.end()){
            require(prefixes_.size()<1024,"EXTRANONCE_PREFIX_CAPACITY");
            i=prefixes_.emplace(key,Prefix{
                Bytes(n,0),false
            }
            ).first;
        }
        require(!i->second.exhausted,"EXTRANONCE_EXHAUSTED");
        Bytes out=i->second.next;
        bool carry=true;
        for(auto&b:i->second.next){
            b=std::uint8_t(b+1);
            if(b){
                carry=false;
                break;
            }
        }
        i->second.exhausted=carry;
        return out;
    }
    std::shared_ptr<const Work> build_work(const JobView&v,Bytes x2,std::uint64_t serial,int device){
        require(x2.size()==v.xnonce2_size,"EXTRANONCE_WORK_WIDTH");
        const auto&j=v.job;
        Bytes coin=j.coinb1;
        coin.insert(coin.end(),v.xnonce1.begin(),v.xnonce1.end());
        coin.insert(coin.end(),x2.begin(),x2.end());
        coin.insert(coin.end(),j.coinb2.begin(),j.coinb2.end());
        auto root=sha256d(coin);
        for(const auto&m:j.merkle){
            std::array<std::uint8_t,64> b{};
            std::copy(root.begin(),root.end(),b.begin());
            std::copy(m.begin(),m.end(),b.begin()+32);
            root=sha256d(b);
        }
        auto w=std::make_shared<Work>();
        w->key=v.key;
        w->target=j.target;
        w->job_id=j.id;
        w->pool_difficulty=j.difficulty;
        w->xnonce2=std::move(x2);
        w->serial=serial;
        w->device=device;
        w->words[0]=le(j.version.data());
        for(unsigned i=0;i<8;++i){
            w->words[i+1]=le(j.previous.data()+4*i);
            w->words[i+9]=be(root.data()+4*i);
        }
        w->words[17]=le(j.ntime.data());
        w->words[18]=le(j.bits.data());
        return w;
    }
    bool ShareBudget::reserve(){
        std::lock_guard l(mutex_);
        if(limit_&&accepted_+inflight_+unknown_>=limit_)return false;
        inflight_=add64(inflight_,1);
        return true;
    }
    void ShareBudget::accepted(){
        std::lock_guard l(mutex_);
        require(inflight_>0,"BUDGET_NO_RESERVATION");
        --inflight_;
        accepted_=add64(accepted_,1);
    }
    void ShareBudget::rejected_or_unsent(){
        std::lock_guard l(mutex_);
        require(inflight_>0,"BUDGET_NO_RESERVATION");
        --inflight_;
    }
    void ShareBudget::uncertain(){
        std::lock_guard l(mutex_);
        require(inflight_>0,"BUDGET_NO_RESERVATION");
        --inflight_;
        unknown_=add64(unknown_,1);
    }
    bool ShareBudget::can_admit()const{
        std::lock_guard l(mutex_);
        return !limit_||accepted_+inflight_+unknown_<limit_;
    }
    std::string ShareBudget::terminal()const{
        std::lock_guard l(mutex_);
        if(limit_&&accepted_==limit_)return "USER_SHARE_LIMIT_REACHED";
        if(limit_&&accepted_+unknown_==limit_&&!inflight_)return "SHARE_LIMIT_UNCERTAIN_STOP";
        return "";
    }
    std::array<std::uint64_t,3> ShareBudget::counts()const{
        std::lock_guard l(mutex_);
        return {
            accepted_,inflight_,unknown_
        };
    }
    bool SubmitBook::full()const{
        return entries_.size()>=(authority_==Authority::User?128:32);
    }
    std::optional<std::uint64_t> SubmitBook::reserve(Candidate c,Time now){
        require(origin(c.assignment.role)==authority_&&c.assignment.work&&c.assignment.work->key.session.authority==authority_,"SUBMIT_ORIGIN");
        if(full())return std::nullopt;
        require(next_id_<std::uint64_t(INT64_MAX),"REQUEST_ID_EXHAUSTED");
        auto id=++next_id_;
        if(authority_==Authority::User&&!budget_.reserve())return std::nullopt;
        try{
            entries_.emplace(id,Entry{
                std::move(c),0,false,now+std::chrono::seconds(60)
            }
            );
        }
        catch(...){
            if(authority_==Authority::User)budget_.rejected_or_unsent();
            throw;
        }
        return id;
    }
    void SubmitBook::wrote(std::uint64_t id,std::size_t n,bool complete){
        auto i=entries_.find(id);
        require(i!=entries_.end(),"TX_UNKNOWN_ID");
        i->second.bytes+=n;
        i->second.sent=complete;
    }
    bool SubmitBook::ack(std::uint64_t id,bool ok,bool pool_stale){
        auto i=entries_.find(id);
        if(i==entries_.end()){
            ++counts_.ignored_acks;
            return false;
        }
        require(i->second.sent,"ACK_BEFORE_COMPLETE_FRAME");
        auto role=index(i->second.candidate.assignment.role);
        if(ok){
            counts_.accepted[role]=add64(counts_.accepted[role],1);
            counts_.accepted_difficulty[role]+=i->second.candidate.assignment.work->pool_difficulty;
            if(authority_==Authority::User)budget_.accepted();
        }
        else{
            counts_.rejected[role]=add64(counts_.rejected[role],1);
            if(pool_stale)counts_.stale_pool[role]=add64(counts_.stale_pool[role],1);
            if(authority_==Authority::User)budget_.rejected_or_unsent();
        }
        entries_.erase(i);
        return true;
    }
    void SubmitBook::disconnect(){
        for(const auto&[id,e]:entries_){
            (void)id;
            auto role=index(e.candidate.assignment.role);
            if(e.bytes){
                ++counts_.unknown[role];
                if(authority_==Authority::User)budget_.uncertain();
            }
            else{
                ++counts_.not_sent[role];
                if(authority_==Authority::User)budget_.rejected_or_unsent();
            }
        }
        entries_.clear();
    }
    void SubmitBook::abandon_unsent(std::uint64_t id){
        auto it=entries_.find(id);require(it!=entries_.end()&&it->second.bytes==0&&!it->second.sent,"CANNOT_ABANDON_SENT_FRAME");
        auto role=index(it->second.candidate.assignment.role);counts_.local_stale[role]=add64(counts_.local_stale[role],1);
        if(authority_==Authority::User)budget_.rejected_or_unsent();entries_.erase(it);
    }
    bool SubmitBook::expired(Time now)const{
        for(const auto&[id,e]:entries_){
            (void)id;
            if(now>=e.deadline)return true;
        }
        return false;
    }
}
