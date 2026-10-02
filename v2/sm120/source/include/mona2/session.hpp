// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "base.hpp"
#include "json.hpp"
#include <map>
namespace mona2 {
    class SessionState {
        SessionKey key_;
        std::string endpoint_;
        bool connected_=false,subscribed_=false,authorized_=false,explicit_diff_=false;
        std::uint64_t seq_=0,sub_epoch_=0,x_epoch_=0,diff_revision_=0,diff_seq_=0,sub_seq_=0,job_revision_=0,view_version_=0;
        double next_diff_=0;
        std::uint64_t registry_expired_=0,registry_evicted_=0;
        Bytes x1_;
        std::size_t x2_size_=0;
        std::shared_ptr<const JobView> latest_;
        std::vector<std::shared_ptr<const JobView>> valid_;
        public:
        SessionState(std::uint64_t run,Authority a,std::string endpoint):key_{
            run,a,0
        }
        ,endpoint_(std::move(endpoint)){}
        void connected();
        void disconnected();
        void subscription(Bytes,std::size_t);
        void extranonce(Bytes,std::size_t);
        void difficulty(double);
        void authorize(bool);
        void notify(JobTemplate,Time);
        void expire(Time);
        SessionView view(Time)const;
        bool can_submit(const Work&,Time)const;
        const SessionKey& key()const{
            return key_;
        }
        bool authorized()const{
            return authorized_;
        }
        std::size_t registry_size()const{
            return valid_.size();
        }
    };
    JobTemplate parse_notify(json_t*params);
    class ExtranonceRegistry {
        struct Prefix {
            Bytes next;
            bool exhausted=false;
        };
        std::map<std::string,Prefix> prefixes_;
        public:
        Bytes allocate(const std::string& endpoint,const Bytes& x1,std::size_t width);
        std::size_t size()const{
            return prefixes_.size();
        }
    };
    std::shared_ptr<const Work> build_work(const JobView&,Bytes xnonce2,std::uint64_t serial,int device);
    class ShareBudget {
        mutable std::mutex mutex_;
        std::uint64_t limit_,accepted_=0,inflight_=0,unknown_=0;
        public:
        explicit ShareBudget(std::uint64_t limit):limit_(limit){}
        bool reserve();
        void accepted();
        void rejected_or_unsent();
        void uncertain();
        bool can_admit()const;
        std::string terminal()const;
        std::array<std::uint64_t,3> counts()const;
        std::uint64_t limit()const{
            return limit_;
        }
    };
    class SubmitBook {
        struct Entry {
            Candidate candidate;
            std::size_t bytes=0;
            bool sent=false;
            Time deadline;
        };
        std::map<std::uint64_t,Entry> entries_;
        ShareBudget& budget_;
        NetworkCounts& counts_;
        Authority authority_;
        std::uint64_t next_id_=999;
        public:
        SubmitBook(Authority a,ShareBudget&b,NetworkCounts&c):budget_(b),counts_(c),authority_(a){}
        std::optional<std::uint64_t> reserve(Candidate,Time);
        void wrote(std::uint64_t,std::size_t,bool complete);
        bool ack(std::uint64_t,bool accepted,bool pool_stale=false);
        void abandon_unsent(std::uint64_t);
        void disconnect();
        bool expired(Time)const;
        bool full()const;
        std::size_t size()const{
            return entries_.size();
        }
        std::uint64_t last_id()const{
            return next_id_;
        }
    };
}
