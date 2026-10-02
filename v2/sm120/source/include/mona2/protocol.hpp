// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "session.hpp"
#include <deque>
namespace mona2 {
    class Protocol {
        struct Tx {
            std::string bytes;
            std::size_t offset=0;
            std::optional<std::uint64_t> id;
            std::shared_ptr<const Work> work;
            Time deadline;
        };
        Credentials credentials_;
        SessionState state_;
        NetworkCounts counts_;
        SubmitBook book_;
        Framer framer_;
        std::deque<Tx> tx_;
        bool sub_pending_=false,auth_pending_=false,reconnect_=false;
        Time handshake_{}
        ,last_rx_{};
        void send_control(std::string,Time);
        void handle(json_t*,Time);
        public:
        Protocol(std::uint64_t run,Authority a,Credentials c,ShareBudget&b):credentials_(std::move(c)),state_(run,a,credentials_.endpoint.canonical()),book_(a,b,counts_){}
        void begin(Time now);
        void disconnect();
        void receive(std::string_view,Time);
        bool offer(Candidate,Time);
        std::string_view writable(Time);
        void wrote(std::size_t,Time);
        void tick(Time);
        bool wants_write()const{
            return !tx_.empty();
        }
        bool reconnect_requested()const{
            return reconnect_;
        }
        SessionView view(Time now)const;
        const NetworkCounts& counts()const{
            return counts_;
        }
        std::size_t pending()const{
            return book_.size();
        }
        bool can_submit(const Work&w,Time now)const{
            return state_.can_submit(w,now);
        }
        void stale(Role r){
            counts_.local_stale[index(r)]=add64(counts_.local_stale[index(r)],1);
        }
        void unsent(Role r){
            counts_.not_sent[index(r)]=add64(counts_.not_sent[index(r)],1);
        }
        const Endpoint& endpoint_value()const{
            return credentials_.endpoint;
        }
        std::size_t framer_residual()const{
            return framer_.residual();
        }
    };
}
