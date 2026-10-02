// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "base.hpp"
#include <functional>
namespace mona2 {
    struct WorkCounters {
        std::array<Count,RoleCount> scheduled{}
        ,actual{}
        ,committed{};
        Count outage_user,forgiven_fee_numerator;
        std::uint64_t segments=0,overflows=0,cancelled_transactions=0;
    };
    struct QuotaChoice {
        Role role;
        std::uint32_t count;
        std::uint64_t segment;
    };
    class QuotaFrame {
        std::uint32_t batch_;
        std::uint64_t quantum_;
        bool available_=false;
        Role phase_=Role::User;
        std::uint64_t remaining_=0,segment_=0;
        std::int64_t fee_error_=0;
        WorkCounters counts_;
        void close_segment();
        void next_phase();
        public:
        explicit QuotaFrame(std::uint32_t batch=NativeBatch);
        void availability(bool user,bool developer);
        std::optional<QuotaChoice> choose(bool user,bool developer,std::uint64_t nonce_remaining);
        void scheduled(Role,std::uint64_t);
        void actual(Role,std::uint64_t);
        void commit(Role,std::uint64_t,std::uint64_t segment);
        void overflow(){
            counts_.overflows=add64(counts_.overflows,1);
        }
        void cancelled(){
            counts_.cancelled_transactions=add64(counts_.cancelled_transactions,1);
        }
        void finish(){
            close_segment();
        }
        const WorkCounters& counters()const{
            return counts_;
        }
        std::uint64_t remaining()const{
            return remaining_;
        }
        std::uint64_t quantum()const{
            return quantum_;
        }
        std::int64_t fee_error()const{
            return fee_error_;
        }
        std::string json()const;
    };
    struct RawHits {
        std::uint32_t total=0,overflow=0;
        std::array<std::uint32_t,MaxCandidates> nonces{};
    };
    class Backend {
        public:
        virtual ~Backend()=default;
        virtual void install(const Work&)=0;
        virtual RawHits scan(std::uint32_t first,std::uint32_t count,const Target& target,bool debug=false)=0;
        virtual std::vector<std::uint64_t> pre_bmw(std::uint32_t){
            throw Error("DEBUG_UNAVAILABLE");
        }
        virtual std::vector<std::uint64_t> bmw_upper(std::uint32_t){
            throw Error("DEBUG_UNAVAILABLE");
        }
    };
    // Bounded queue with exact reservation for the largest non-overflow leaf.
    class Mailbox {
        mutable std::mutex mutex_;
        std::vector<std::optional<Candidate>> slots_;
        std::size_t head_=0,tail_=0,size_=0,reserved_=0;
        bool closed_=false;
        public:
        struct Ticket {
            Mailbox* box=nullptr;
            std::size_t n=0;
            Ticket()=default;
            Ticket(Mailbox*b,std::size_t v):box(b),n(v){};
            Ticket(const Ticket&)=delete;
            Ticket&operator=(const Ticket&)=delete;
            Ticket(Ticket&&o)noexcept:box(o.box),n(o.n){
                o.box=nullptr;
            }
            ~Ticket();
            void publish(std::span<const Candidate> candidates);
        };
        explicit Mailbox(std::size_t capacity=4096):slots_(capacity){
            require(capacity>=MaxCandidates,"MAILBOX_CAPACITY");
        }
        std::optional<Ticket> reserve(std::size_t n=MaxCandidates);
        std::optional<Candidate> pop();
        std::size_t size()const;
        bool has_capacity()const;
        std::size_t close();
    };
    struct RangeResult {
        std::uint64_t next=0,committed=0,actual=0;
        bool cancelled=false,blocked=false;
    };
    // One GPU-owner. An overflow parent is actual-only; committed leaves are disjoint
    // and candidates retain the original Assignment even if its wire lease expires.
    class RangeTransaction {
        Assignment assignment_;
        std::vector<std::pair<std::uint32_t,std::uint32_t>> pending_;
        std::uint64_t cursor_=0;
        bool done_=false;
        public:
        explicit RangeTransaction(Assignment a);
        RangeResult step(Backend&,Mailbox&,QuotaFrame&,const std::function<bool()>& may_launch,
        const std::function<Target(const Words&,std::uint32_t)>& oracle);
        bool done()const{
            return done_;
        }
        std::uint64_t cursor()const{
            return cursor_;
        }
    };
}
