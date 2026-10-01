// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/accounting.hpp"
#include <algorithm>
#include <sstream>
namespace mona2 {
    QuotaFrame::QuotaFrame(std::uint32_t b):batch_(b),quantum_(std::uint64_t(b)*64){
        require(b>0&&b<=NativeBatch*2u,"QUOTA_CONFIG");
    }
    void QuotaFrame::close_segment(){
        if(available_){
            require(fee_error_>=0,"QUOTA_OVERCHARGE");
            counts_.forgiven_fee_numerator+=Count(std::uint64_t(fee_error_));
        }
        available_=false;
        phase_=Role::User;
        remaining_=0;
        fee_error_=0;
    }
    void QuotaFrame::availability(bool user,bool developer){
        bool both=user&&developer;
        if(!both){
            close_segment();
            return;
        }
        if(!available_){
            available_=true;
            next64(segment_);
            counts_.segments=add64(counts_.segments,1);
            remaining_=98*quantum_;
            phase_=Role::User;
        }
    }
    std::optional<QuotaChoice> QuotaFrame::choose(bool u,bool d,std::uint64_t n){
        availability(u,d);
        if(!u||n==0)return std::nullopt;
        auto r=available_?remaining_:std::uint64_t(batch_);
        return QuotaChoice{
            available_?phase_:Role::User,std::uint32_t(std::min({
                std::uint64_t(batch_),r,n
            }
            )),available_?segment_:0
        };
    }
    void QuotaFrame::next_phase(){
        if(phase_==Role::User){
            phase_=Role::DevFee;
            remaining_=2*quantum_;
        }
        else{
            phase_=Role::User;
            remaining_=98*quantum_;
        }
    }
    void QuotaFrame::scheduled(Role r,std::uint64_t n){
        counts_.scheduled[index(r)]+=Count(n);
    }
    void QuotaFrame::actual(Role r,std::uint64_t n){
        counts_.actual[index(r)]+=Count(n);
    }
    void QuotaFrame::commit(Role r,std::uint64_t n,std::uint64_t segment){
        require(n>0&&n<=batch_,"COMMIT_SIZE");
        auto updated=counts_.committed[index(r)]+Count(n);
        if(segment){
            require(available_&&segment==segment_&&r==phase_&&n<=remaining_,"COMMIT_PHASE");
            const auto f=fee_error_+std::int64_t(n)*(r==Role::DevFee?-98:2);
            require(f>=0&&std::uint64_t(f)<=196*quantum_,"QUOTA_BOUND");
            counts_.committed[index(r)]=updated;
            fee_error_=f;
            remaining_-=n;
            if(!remaining_)next_phase();
        }
        else{
            require(!available_&&r==Role::User,"OUTAGE_ROLE");
            auto out=counts_.outage_user+Count(n);
            counts_.committed[index(r)]=updated;
            counts_.outage_user=out;
        }
    }
    std::string QuotaFrame::json()const{
        std::ostringstream s;
        s<<"{\"fee_percent\":2,\"q_hashes\":\""<<quantum_<<"\",\"segment\":\""<<segment_<<"\",\"phase\":"<<json_quote(role_name(phase_))<<",\"phase_remaining\":\""<<remaining_<<"\",\"fee_error_numerator\":\""<<fee_error_<<"\",\"roles\":[";
        for(unsigned i=0;i<RoleCount;++i){
            if(i)s<<',';
            s<<"{\"role\":"<<json_quote(role_name(Role(i)))<<",\"scheduled\":\""<<counts_.scheduled[i].decimal()<<"\",\"actual\":\""<<counts_.actual[i].decimal()<<"\",\"committed\":\""<<counts_.committed[i].decimal()<<"\"}";
        }
        s<<"],\"outage_user\":\""<<counts_.outage_user.decimal()<<"\",\"forgiven_fee_numerator\":\""<<counts_.forgiven_fee_numerator.decimal()<<"\",\"overflow_parents\":"<<counts_.overflows<<",\"cancelled_transactions\":"<<counts_.cancelled_transactions<<"}";
        return s.str();
    }
    Mailbox::Ticket::~Ticket(){
        if(box){
            std::lock_guard l(box->mutex_);
            box->reserved_-=n;
        }
    }
    void Mailbox::Ticket::publish(std::span<const Candidate> c){
        require(box&&c.size()<=n,"MAILBOX_RESERVATION");
        std::lock_guard l(box->mutex_);
        require(!box->closed_,"MAILBOX_CLOSED_AFTER_RESERVE");
        for(const auto& v:c){
            box->slots_[box->tail_]=v;
            box->tail_=(box->tail_+1)%box->slots_.size();
            ++box->size_;
        }
        box->reserved_-=n;
        box=nullptr;
    }
    std::optional<Mailbox::Ticket> Mailbox::reserve(std::size_t n){
        std::lock_guard l(mutex_);
        if(closed_||n>slots_.size()-size_-reserved_)return std::nullopt;
        reserved_+=n;
        return Ticket(this,n);
    }
    std::optional<Candidate> Mailbox::pop(){
        std::lock_guard l(mutex_);
        if(!size_)return std::nullopt;
        auto c=std::move(slots_[head_]);
        slots_[head_].reset();
        head_=(head_+1)%slots_.size();
        --size_;
        return c;
    }
    std::size_t Mailbox::size()const{
        std::lock_guard l(mutex_);
        return size_;
    }
    bool Mailbox::has_capacity()const{
        std::lock_guard l(mutex_);
        return !closed_&&slots_.size()-size_-reserved_>=MaxCandidates;
    }
    std::size_t Mailbox::close(){
        std::lock_guard l(mutex_);
        require(reserved_==0,"CLOSE_WITH_GPU_RESERVATION");
        closed_=true;
        auto n=size_;
        for(auto& s:slots_)s.reset();
        size_=0;
        return n;
    }
    RangeTransaction::RangeTransaction(Assignment a):assignment_(std::move(a)),cursor_(assignment_.first){
        require(assignment_.work&&assignment_.count>0&&assignment_.count<=NativeBatch&&std::uint64_t(assignment_.first)+assignment_.count<=NonceSpace,"ASSIGNMENT_RANGE");
        require(origin(assignment_.role)==assignment_.work->key.session.authority,"ASSIGNMENT_AUTHORITY");
        pending_.emplace_back(assignment_.first,assignment_.count);
    }
    RangeResult RangeTransaction::step(Backend&b,Mailbox&box,QuotaFrame&q,const std::function<bool()>&go,const std::function<Target(const Words&,std::uint32_t)>& oracle){
        require(!done_,"TRANSACTION_ALREADY_CLOSED");
        RangeResult out;
        out.next=cursor_;
        if(!go()){
            q.cancelled();
            done_=true;
            out.cancelled=true;
            return out;
        }
        auto ticket=box.reserve();
        if(!ticket){
            out.blocked=true;
            return out;
        }
        auto [first,n]=pending_.back();
        pending_.pop_back();
        require(first==cursor_,"RANGE_NOT_CONTIGUOUS");
        auto raw=b.scan(first,n,assignment_.work->target);
        q.actual(assignment_.role,n);
        require(raw.total<=n&&raw.overflow<=1&&bool(raw.overflow)==(raw.total>MaxCandidates),"GPU_HIT_HEADER_CONTRACT");
        out.actual=n;
        if(raw.overflow||raw.total>MaxCandidates){
            require(n>1,"SINGLE_NONCE_OVERFLOW");
            q.overflow();
            auto left=n/2;
            pending_.emplace_back(first+left,n-left);
            pending_.emplace_back(first,left);
            return out;
        }
        std::array<std::uint32_t,MaxCandidates> sorted=raw.nonces;
        std::sort(sorted.begin(),sorted.begin()+raw.total);
        std::array<Candidate,MaxCandidates> accepted{};
        unsigned count=0;
        for(unsigned i=0;i<raw.total;++i){
            auto nonce=sorted[i];
            require(nonce>=first&&std::uint64_t(nonce)<std::uint64_t(first)+n,"GPU_NONCE_RANGE");
            require(i==0||nonce!=sorted[i-1],"GPU_DUPLICATE_NONCE");
            auto h=oracle(assignment_.work->words,nonce);
            require(upper(h)<=upper(assignment_.work->target),"GPU_CPU_HIGH64_MISMATCH");
            if(full_test(h,assignment_.work->target)){
                auto a=assignment_;
                a.first=first;
                a.count=n;
                accepted[count++]={
                    std::move(a),nonce,h
                };
            }
        }
        // No availability transition can re-label this just-finished assignment.
        q.commit(assignment_.role,n,assignment_.segment);
        ticket->publish(std::span(accepted.data(),count));
        cursor_+=n;
        out.next=cursor_;
        out.committed=n;
        done_=pending_.empty();
        return out;
    }
}
