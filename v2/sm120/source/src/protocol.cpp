// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/protocol.hpp"
#include <cmath>
#include <algorithm>
namespace mona2 {
    void Protocol::send_control(std::string s,Time now){
        require(tx_.size()<64,"CONTROL_QUEUE_LIMIT");
        tx_.push_back(Tx{
            std::move(s)+"\n",0,std::nullopt,nullptr,now+std::chrono::seconds(10)
        }
        );
    }
    void Protocol::begin(Time now){
        disconnect();
        state_.connected();
        ++counts_.connects;
        if(counts_.connects>1)++counts_.reconnects;
        handshake_=now+std::chrono::seconds(15);
        last_rx_=now;
        sub_pending_=true;
        auth_pending_=false;
        reconnect_=false;
        send_control("{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[\"mona-miner/2.0-prototype\"]}",now);
    }
    void Protocol::disconnect(){
        book_.disconnect();
        for(auto&x:tx_)std::fill(x.bytes.begin(),x.bytes.end(),'\0');
        tx_.clear();
        framer_.reset();
        state_.disconnected();
        sub_pending_=auth_pending_=false;
    }
    void Protocol::receive(std::string_view bytes,Time now){
        for(const auto&line:framer_.feed(bytes)){
            auto j=parse_json(line);
            handle(j.get(),now);
            ++counts_.received;
            last_rx_=now;
        }
    }
    void Protocol::handle(json_t*root,Time now){
        auto*m=json_object_get(root,"method");
        if(m){
            require(json_is_string(m),"METHOD_TYPE");
            auto method=json_string(m,128,false);
            auto*p=member(root,"params");
            if(method=="mining.notify"){
                state_.notify(parse_notify(p),now);
                ++counts_.notifications;
            }
            else if(method=="mining.set_difficulty"){
                require(json_is_array(p)&&json_array_size(p)==1&&json_is_number(json_array_get(p,0)),"DIFFICULTY_TYPE");
                state_.difficulty(json_number_value(json_array_get(p,0)));
            }
            else if(method=="mining.set_extranonce"){
                require(json_is_array(p)&&json_array_size(p)==2,"EXTRANONCE_PARAMS");
                auto x=unhex(json_string(json_array_get(p,0),512),256);
                auto n=json_uint(json_array_get(p,1),16);
                state_.extranonce(std::move(x),std::size_t(n));
            }
            else if(method=="client.reconnect"){
                reconnect_=true;
                /* ignore every supplied destination */
            }
            else if(method=="mining.ping"){
                auto id=json_object_get(root,"id");
                std::string v="null";
                if(id&&json_is_integer(id))v=std::to_string(json_uint(id));
                else if(id&&json_is_string(id))v=json_quote(json_string(id,128));
                else require(!id||json_is_null(id),"PING_ID_TYPE");
                send_control("{\"id\":"+v+",\"result\":\"pong\",\"error\":null}",now);
            }
            // Unknown extensions are ignored. No text from them enters an audit log.
            return;
        }
        auto*idv=member(root,"id");
        if(json_is_null(idv))return;
        auto id=json_uint(idv);
        if(id==0)return;
        auto*r=json_object_get(root,"result");
        auto*e=json_object_get(root,"error");
        if(id==1){
            if(!sub_pending_){
                ++counts_.ignored_acks;
                return;
            }
            require((!e||json_is_null(e))&&json_is_array(r)&&json_array_size(r)>=3,"SUBSCRIBE_RESPONSE");
            auto x=unhex(json_string(json_array_get(r,1),512),256);
            auto n=json_uint(json_array_get(r,2),16);
            state_.subscription(std::move(x),std::size_t(n));
            sub_pending_=false;
            auth_pending_=true;
            send_control("{\"id\":2,\"method\":\"mining.authorize\",\"params\":["+json_quote(credentials_.worker)+","+json_quote(credentials_.password)+"]}",now);
            return;
        }
        if(id==2){
            if(!auth_pending_){
                ++counts_.ignored_acks;
                return;
            }
            require(json_is_true(r)&&(!e||json_is_null(e)),"AUTHORIZE_REJECTED");
            state_.authorize(true);
            auth_pending_=false;
            return;
        }
        // Unknown and late ids never mutate a current job. Contradictory success/error
        // is a protocol failure, not acceptance.
        require(r!=nullptr,"ACK_RESULT_MISSING");
        require(!json_is_true(r)||!e||json_is_null(e),"ACK_CONTRADICTION");
        require(json_is_boolean(r)||json_is_null(r),"ACK_RESULT_TYPE");
        std::string reason;
        if(e&&json_is_string(e))reason=json_string(e,FrameMax);
        else if(e&&json_is_array(e)&&json_array_size(e)>1&&json_is_string(json_array_get(e,1)))reason=json_string(json_array_get(e,1),FrameMax);
        std::transform(reason.begin(),reason.end(),reason.begin(),[](unsigned char c){
            return c>='A'&&c<='Z'?char(c-'A'+'a'):char(c);
        }
        );
        bool reported_stale=reason.find("stale")!=std::string::npos||reason.find("job not found")!=std::string::npos;
        book_.ack(id,json_is_true(r)&&(!e||json_is_null(e)),reported_stale);
    }
    bool Protocol::offer(Candidate c,Time now){
        require(c.assignment.work&&origin(c.assignment.role)==c.assignment.work->key.session.authority,"CANDIDATE_ORIGIN");
        if(!state_.can_submit(*c.assignment.work,now)){
            stale(c.assignment.role);
            return true;
        }
        require(c.nonce>=c.assignment.first&&std::uint64_t(c.nonce)<std::uint64_t(c.assignment.first)+c.assignment.count&&full_test(c.hash,c.assignment.work->target),"CANDIDATE_VALIDATION_CONTRACT");
        if(!tx_.empty())return false;
        auto id=book_.reserve(c,now);
        if(!id)return false;
        const auto&w=*c.assignment.work;
        std::string line="{\"id\":"+std::to_string(*id)+",\"method\":\"mining.submit\",\"params\":["+json_quote(credentials_.worker)+","+json_quote(w.job_id)+","+json_quote(hex(w.xnonce2))+","+json_quote(nonce_hex(w.words[17]))+","+json_quote(nonce_hex(c.nonce))+"]}\n";
        tx_.push_back(Tx{
            std::move(line),0,id,c.assignment.work,now+std::chrono::seconds(10)
        }
        );
        return true;
    }
    std::string_view Protocol::writable(Time now){
        if(tx_.empty())return {};
        auto& t=tx_.front();
        require(now<t.deadline,"WRITE_DEADLINE");
        if(t.work&&!state_.can_submit(*t.work,now)){
            // An entirely unsent obsolete frame can be discarded without
            // reconnecting a persistent session. A partial JSON frame cannot.
            if(t.offset==0){require(bool(t.id),"STALE_TX_WITHOUT_REQUEST");book_.abandon_unsent(*t.id);std::fill(t.bytes.begin(),t.bytes.end(),'\0');tx_.pop_front();return {};}
            reconnect_=true;
            return {};
        }
        return std::string_view(t.bytes).substr(t.offset);
    }
    void Protocol::wrote(std::size_t n,Time now){
        require(!tx_.empty()&&n>0,"WRITE_ACCOUNTING");
        auto&t=tx_.front();
        require(n<=t.bytes.size()-t.offset,"WRITE_OVERRUN");
        t.offset+=n;
        bool complete=t.offset==t.bytes.size();
        if(t.id)book_.wrote(*t.id,n,complete);
        if(complete){
            std::fill(t.bytes.begin(),t.bytes.end(),'\0');
            tx_.pop_front();
        }
        (void)now;
    }
    void Protocol::tick(Time now){
        state_.expire(now);
        require(state_.authorized()||now<handshake_,"HANDSHAKE_DEADLINE");
        require(now-last_rx_<std::chrono::seconds(180),"RX_LEASE_EXPIRED");
        require(!book_.expired(now),"ACK_DEADLINE");
        if(!tx_.empty())require(now<tx_.front().deadline,"WRITE_DEADLINE");
    }
    SessionView Protocol::view(Time now)const{
        auto v=state_.view(now);
        if(book_.full()){
            v.ready=false;
            v.state="DEGRADED_PENDING_CAPACITY";
        }
        return v;
    }
}
