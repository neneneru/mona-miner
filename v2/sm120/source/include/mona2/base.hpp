// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace mona2 {
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;
    using Bytes = std::vector<std::uint8_t>;
    using Words = std::array<std::uint32_t,20>;
    using Target = std::array<std::uint32_t,8>;
    using Hash256 = std::array<std::uint8_t,32>;
    inline constexpr std::uint32_t NativeBatch=1048576;
    inline constexpr std::uint64_t NonceSpace=std::uint64_t{
        1
    }
    <<32;
    inline constexpr unsigned MaxCandidates=64;
    inline constexpr std::size_t FrameMax=1u<<20;
    struct Error : std::runtime_error {
        explicit Error(const char* code):std::runtime_error(code){}
    };
    inline void require(bool b,const char* code){
        if(!b)throw Error(code);
    }
    inline std::uint64_t add64(std::uint64_t a,std::uint64_t b){
        require(a<=UINT64_MAX-b,"COUNTER_OVERFLOW");
        return a+b;
    }
    inline std::uint64_t next64(std::uint64_t& n){
        n=add64(n,1);
        return n;
    }
    // Portable checked unsigned 128-bit counters; no compiler-specific __int128.
    struct Count {
        std::uint64_t hi=0,lo=0;
        Count()=default;
        Count(std::uint64_t n):lo(n){}
        friend bool operator==(const Count&,const Count&)=default;
        friend bool operator<(const Count&a,const Count&b){
            return a.hi<b.hi||(a.hi==b.hi&&a.lo<b.lo);
        }
        Count& operator+=(Count b){
            auto l=lo+b.lo;
            auto carry=l<lo?1u:0u;
            auto h=add64(hi,b.hi);
            h=add64(h,carry);
            hi=h;
            lo=l;
            return *this;
        }
        friend Count operator+(Count a,Count b){
            return a+=b;
        }
        friend Count operator-(Count a,Count b){
            require(!(a<b),"NEGATIVE_COUNTER");
            auto borrow=a.lo<b.lo?1u:0u;
            a.lo-=b.lo;
            a.hi-=b.hi;
            require(a.hi>=borrow,"COUNTER_UNDERFLOW");
            a.hi-=borrow;
            return a;
        }
        Count times(unsigned n)const{
            Count out;
            for(unsigned i=0;i<n;++i)out+=*this;
            return out;
        }
        std::string decimal()const;
        double number()const;
    };
    enum class Authority:unsigned {
        User=0,Developer=1
    };
    enum class Role:unsigned { User=0, DevFee=1 };
    inline constexpr unsigned RoleCount=2;
    inline unsigned index(Role r){
        const auto value=static_cast<unsigned>(r);
        require(value<RoleCount,"INVALID_ROLE");
        return value;
    }
    inline Authority origin(Role r){
        return index(r)==0?Authority::User:Authority::Developer;
    }
    inline const char* role_name(Role r){
        return index(r)==0?"USER":"DEVFEE";
    }
    inline unsigned index(Authority a){
        return static_cast<unsigned>(a);
    }
    struct Endpoint {
        std::string host;
        std::uint16_t port=0;
        std::string canonical()const;
    };
    struct Credentials {
        Endpoint endpoint;
        std::string worker,password;
    };
    std::uint64_t decimal(std::string_view s,std::uint64_t max);
    Endpoint endpoint(std::string_view s);
    void validate_credential(std::string_view s,std::size_t max,bool empty=false);
    std::string hex(std::span<const std::uint8_t> b);
    Bytes unhex(std::string_view s,std::size_t max);
    std::string nonce_hex(std::uint32_t n);
    Hash256 sha256(std::span<const std::uint8_t> b);
    Hash256 sha256d(std::span<const std::uint8_t> b);
    Hash256 fingerprint(std::string_view b);
    Target pool_target(double difficulty);
    Target cpu_hash(const Words&w,std::uint32_t nonce,Target* pre=nullptr);
    std::uint64_t upper(const Target& h);
    bool full_test(const Target& hash,const Target& target);
    std::string json_quote(std::string_view s);
    std::string escape_controls(std::string_view s);
    struct SessionKey {
        std::uint64_t run=0;
        Authority authority=Authority::User;
        std::uint64_t connection=0;
        friend bool operator==(const SessionKey&,const SessionKey&)=default;
    };
    struct JobKey {
        SessionKey session;
        std::uint64_t subscription=0,extranonce=0,revision=0,target_revision=0;
        Hash256 fingerprint{};
        friend bool operator==(const JobKey&,const JobKey&)=default;
    };
    struct JobTemplate {
        std::string id;
        Bytes previous,coinb1,coinb2;
        std::vector<Hash256> merkle;
        std::array<std::uint8_t,4> version{}
        ,bits{}
        ,ntime{};
        bool clean=false;
        double difficulty=0;
        Target target{};
    };
    struct JobView {
        JobKey key;
        JobTemplate job;
        Bytes xnonce1;
        std::size_t xnonce2_size=0;
        Time received{};
        std::string endpoint_key;
    };
    struct Work {
        JobKey key;
        Words words{};
        Target target{};
        std::string job_id;
        Bytes xnonce2;
        double pool_difficulty=0;
        std::uint64_t serial=0;
        int device=0;
    };
    struct Assignment {
        std::shared_ptr<const Work> work;
        Role role=Role::User;
        std::uint64_t execution=0,segment=0;
        std::uint32_t first=0,count=0;
    };
    struct Candidate {
        Assignment assignment;
        std::uint32_t nonce=0;
        Target hash{};
    };
    struct SessionView {
        bool ready=false;
        std::uint64_t version=0;
        std::shared_ptr<const JobView> latest;
        std::string state="DISCONNECTED";
        std::uint64_t registry_expired=0,registry_evicted=0;
    };
    struct NetworkCounts {
        std::array<std::uint64_t,RoleCount> stale_pool{};
        std::array<std::uint64_t,RoleCount> accepted{}
        ,rejected{}
        ,unknown{}
        ,not_sent{}
        ,local_stale{};
        std::array<double,RoleCount> accepted_difficulty{};
        std::uint64_t connects=0,reconnects=0,received=0,notifications=0,ignored_acks=0,protocol_errors=0,queue_expired=0;
    };
}
