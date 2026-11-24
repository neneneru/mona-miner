// SPDX-License-Identifier: GPL-3.0-or-later
// Local GPU tests for slot reuse, graph target changes and overflow aggregation.
#include "mona2/preview_backend.hpp"
#include <algorithm>
#include <iostream>

using namespace mona2;
namespace {
    Target threshold(std::uint64_t high){
        Target target;
        target.fill(UINT32_MAX);
        target[6]=std::uint32_t(high);
        target[7]=std::uint32_t(high>>32);
        return target;
    }

    Work header(unsigned seed){
        Work w{};
        std::uint32_t word=seed;
        for(auto& v:w.words){
            word=word*1664525+1013904223;
            v=word;
        }
        w.key.session.authority=Authority::User;
        w.serial=seed;
        return w;
    }

    std::vector<std::uint32_t> hits(const RawHits& raw){
        require(!raw.overflow&&raw.total<=MaxCandidates,"PREVIEW_TEST_HIT_OVERFLOW");
        std::vector<std::uint32_t> nonces(raw.nonces.begin(),raw.nonces.begin()+raw.total);
        std::sort(nonces.begin(),nonces.end());
        return nonces;
    }

    void quota_window(){
        QuotaFrame quota(NativeBatch,PreviewBackend::scan_window);
        const auto quantum=64ull*NativeBatch;
        require(quota.quantum()==quantum,"PREVIEW_FEE_QUANTUM");
        const auto total=100*quantum;
        std::uint64_t left=total;
        while(left){
            auto choice=quota.choose(true,true,left);
            require(choice&&choice->count<=PreviewBackend::scan_window,"PREVIEW_QUOTA_RANGE");
            quota.commit(choice->role,choice->count,choice->segment);
            left-=choice->count;
        }
        const auto& committed=quota.counters().committed;
        require(committed[0]==Count(98*quantum)&&committed[1]==Count(2*quantum),"PREVIEW_FEE_SPLIT");
    }

    void scan(PreviewBackend& gpu){
        const auto n=PreviewBackend::chunk_size*6+17;
        const auto first=std::uint32_t(NonceSpace-n);
        auto a=header(13),b=header(29);
        a.target=threshold(0);
        gpu.install(a);
        gpu.scan(first,n,a.target,true);
        auto high=gpu.bmw_upper(n);
        for(unsigned i=0;i<65;++i){
            const auto offset=std::uint32_t(std::uint64_t(i)*(n-1)/64);
            require(high[offset]==upper(cpu_hash(a.words,first+offset)),"PREVIEW_CPU_HIGH64");
        }

        auto ordered=high;
        std::nth_element(ordered.begin(),ordered.begin()+23,ordered.end());
        a.target=threshold(ordered[23]);
        gpu.install(a);
        std::vector<std::uint32_t> expected;
        for(unsigned i=0;i<n;++i)if(high[i]<=upper(a.target))expected.push_back(first+i);
        require(expected.size()==24,"PREVIEW_THRESHOLD_VECTOR");
        require(hits(gpu.scan(first,n,a.target))==expected,"PREVIEW_GRAPH_HITS");
        for(auto nonce:expected)
            require(full_test(cpu_hash(a.words,nonce),a.target),"PREVIEW_GRAPH_CPU_HIT");

        b.target=threshold(UINT64_MAX);
        gpu.install(b);
        const auto all=gpu.scan(first,n,b.target);
        require(all.total==n&&all.overflow,"PREVIEW_OVERFLOW_MERGE");
        gpu.install(a);
        require(hits(gpu.scan(first,n,a.target))==expected,"PREVIEW_GRAPH_A_B_A");

        std::nth_element(ordered.begin(),ordered.begin()+79,ordered.end());
        a.target=threshold(ordered[79]);
        gpu.install(a);
        auto work=std::make_shared<Work>(a);
        QuotaFrame quota(NativeBatch,gpu.max_scan_count());
        quota.availability(true,false);
        quota.scheduled(Role::User,n);
        RangeTransaction transaction({work,Role::User,1,0,first,n},gpu.max_scan_count());
        Mailbox mailbox;
        std::vector<std::uint32_t> received;
        while(!transaction.done()){
            auto result=transaction.step(gpu,mailbox,quota,[]{return true;},
                                         [](const Words& w,std::uint32_t nonce){return cpu_hash(w,nonce);});
            require(!result.blocked&&!result.cancelled,"PREVIEW_UNEXPECTED_TRANSACTION_STOP");
            while(auto item=mailbox.pop())received.push_back(item->nonce);
        }
        expected.clear();
        for(unsigned i=0;i<n;++i)if(high[i]<=upper(a.target))expected.push_back(first+i);
        require(received==expected&&received.size()==80,"PREVIEW_OVERFLOW_RETRY_HITS");
        const auto& counters=quota.counters();
        require(counters.overflows>0&&counters.committed[0]==Count(n)&&
                counters.actual[0].number()>n,"PREVIEW_OVERFLOW_RETRY_ACCOUNTING");
    }
}

int main(){
    try{
        quota_window();
        PreviewBackend gpu(0);
        scan(gpu);
        std::cout<<"{\"status\":\"PASS\",\"test\":\"SM120_PREVIEW_GPU_AND_FEE\"}\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
