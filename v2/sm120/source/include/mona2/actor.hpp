// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "protocol.hpp"
#include "accounting.hpp"
#include "transport.hpp"
#include <functional>
#include <thread>
namespace mona2 {
    class AuditLog {
        mutable std::mutex mutex_;
        std::vector<std::string> secrets_;
        public:
        void secret(std::string s);
        std::string sanitize(std::string_view)const;
        void event(Authority,const char*code,std::uint64_t connection=0);
        void line(std::string_view);
    };
    class SessionActor {
        Authority authority_;
        Protocol protocol_;
        Mailbox mailbox_;
        ShareBudget&budget_;
        StopSignal&stop_;
        std::atomic<bool>&producer_done_;
        AuditLog&log_;
        bool loopback_,all_;
        std::atomic<std::shared_ptr<const SessionView>> view_;
        mutable std::mutex counts_mu_;
        NetworkCounts counts_{};
        std::jthread thread_;
        std::uint64_t random_;
        void run()noexcept;
        void publish(Time,bool stopping=false);
        public:
        SessionActor(std::uint64_t run,Authority a,Credentials c,ShareBudget&b,StopSignal&s,std::atomic<bool>&done,AuditLog&l,bool loopback=false,bool all=false);
        ~SessionActor();
        void start();
        void join();
        void finalize_queue();
        std::shared_ptr<const SessionView> view()const{
            return view_.load();
        }
        Mailbox&mailbox(){
            return mailbox_;
        }
        NetworkCounts counts()const{
            std::lock_guard l(counts_mu_);
            return counts_;
        }
    };
    std::uint32_t backoff_ms(unsigned failures,std::uint64_t&state);
}
