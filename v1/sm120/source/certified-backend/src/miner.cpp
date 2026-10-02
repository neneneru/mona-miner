#include "mona/miner.hpp"
#include "mona/measured_defaults.hpp"
#include <algorithm>
#include <stdexcept>

namespace mona {
Config measured_configuration(int sm,Objective objective) {
    MeasuredDefaults d{};
    if(sm==89) d=defaults_sm89();
    else if(sm==120) d=defaults_sm120();
    else throw std::invalid_argument("unsupported architecture");
    if(!d.verified) throw std::runtime_error("no measured default for this architecture");
    if(objective==Objective::Efficiency && !d.has_efficient)
        throw std::runtime_error("power measurements did not qualify an efficiency default");
    return objective==Objective::Throughput?d.fastest:d.efficient;
}
MiningEngine::MiningEngine(int device,const Config& c):context_(create(device,c)),config_(c){}
void MiningEngine::set_work(uint64_t generation,const std::array<uint32_t,20>& words,
                            const std::array<uint32_t,8>& target) {
    set_job(*context_,words,uint64_t(target[7])<<32|target[6]);
    words_=words; target_=target;generation_=generation;work_set_=true;
}
Accounting MiningEngine::scan_once(uint32_t first,uint32_t count,
    const std::function<void(const Solution&)>& emit,const std::function<bool()>& cancel) {
    if(!work_set_) throw std::logic_error("set_work must precede scan_once");
    if(!emit || !cancel) throw std::invalid_argument("callbacks must be callable");
    if(!count || count>config_.batch || uint64_t(first)+count>(uint64_t(1)<<32))
        throw std::invalid_argument("invalid nonce range");
    Accounting total;total.next_nonce=first;
    const uint64_t target_hi=uint64_t(target_[7])<<32|target_[6];
    std::function<bool(uint32_t,uint32_t)> visit=[&](uint32_t begin,uint32_t length) {
        if(cancel()) {total.cancelled=true;return false;}
        auto hits=scan(*context_,begin,length);
        total.executed_hashes+=length;
        if(cancel()) {total.cancelled=true;return false;}
        if(hits.overflow) {
            if(length==1) throw std::runtime_error("impossible single-nonce result overflow");
            // The incomplete parent result is discarded, not submitted. Replay
            // disjoint children and count physical work separately from unique work.
            const uint32_t left=length/2;
            return visit(begin,left) && visit(begin+left,length-left);
        }
        if(hits.total_found!=hits.nonces.size())
            throw std::runtime_error("inconsistent GPU candidate count");
        std::sort(hits.nonces.begin(),hits.nonces.end());
        if(std::adjacent_find(hits.nonces.begin(),hits.nonces.end())!=hits.nonces.end())
            throw std::runtime_error("duplicate GPU candidate");
        for(uint32_t nonce:hits.nonces) {
            if(nonce<begin || uint64_t(nonce)>=uint64_t(begin)+length)
                throw std::runtime_error("GPU candidate outside requested range");
            const auto hash=cpu_hash(words_,nonce);
            const uint64_t hi=uint64_t(hash[7])<<32|hash[6];
            if(hi>target_hi) throw std::runtime_error("GPU/CPU disagreement; mining stopped");
            // High-word ties may fail on lower words. They are valid prefilter
            // candidates, but never submitted as valid shares.
            if(full_test(hash,target_)) {
                emit(Solution{generation_,nonce,hash});
                ++total.solutions;
            }
        }
        total.unique_hashes+=length;
        total.next_nonce=uint64_t(begin)+length;
        return true;
    };
    visit(first,count);
    return total;
}
}
