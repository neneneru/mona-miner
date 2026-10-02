// SPDX-License-Identifier: GPL-3.0-or-later
// Explicitly authorized local GPU validation. No network actor is constructed.
#include "mona2/frozen_backend.hpp"
#include "mona2/cli.hpp"
#include <algorithm>
#include <iostream>
#include <numeric>
using namespace mona2;
namespace {
    std::uint64_t checks=0;
    Words words(unsigned seed){
        Words w{};
        std::uint64_t x=seed+0x12345678ull;
        for(auto&v:w){
            x^=x<<13;
            x^=x>>7;
            x^=x<<17;
            v=std::uint32_t(x);
        }
        return w;
    }
    std::shared_ptr<Work> work(unsigned seed){
        auto w=std::make_shared<Work>();
        w->words=words(seed);
        w->serial=seed+1;
        w->key.session={
            1,Authority::User,1
        };
        w->key.revision=seed+1;
        w->key.extranonce=1;
        w->key.subscription=1;
        return w;
    }
    void exact(FrozenBackend&gpu,std::shared_ptr<Work>w,std::uint32_t first,std::uint32_t n,bool all=true){
        gpu.install(*w);
        auto h=gpu.scan(first,n,w->target,true);
        auto pre=gpu.pre_bmw(n),high=gpu.bmw_upper(n);
        std::vector<unsigned> spots;
        if(all){
            spots.resize(n);
            std::iota(spots.begin(),spots.end(),0u);
        }
        else for(unsigned i=0;i<32;++i)spots.push_back(unsigned(std::uint64_t(i)*(n-1)/31));
        for(auto i:spots){
            Target p{};
            auto hash=cpu_hash(w->words,first+i,&p);
            require(high[i]==upper(hash),"GPU_FULLCHAIN_UPPER_MISMATCH");
            for(unsigned j=0;j<4;++j)require(pre[i+std::size_t(j)*n]==(std::uint64_t(p[j*2])|(std::uint64_t(p[j*2+1])<<32)),"GPU_PRE_BMW_MISMATCH");
            ++checks;
        }
        if(all){
            std::vector<std::uint32_t> want;
            for(unsigned i=0;i<n;++i)if(high[i]<=upper(w->target))want.push_back(first+i);
            require(h.total==want.size()&&bool(h.overflow)==(want.size()>MaxCandidates),"GPU_HIT_HEADER");
            if(want.size()<=MaxCandidates){
                auto got=std::vector<std::uint32_t>(h.nonces.begin(),h.nonces.begin()+h.total);
                std::sort(got.begin(),got.end());
                require(got==want,"GPU_HIT_LIST");
            }
        }
        gpu.check_guards();
        require(gpu.submissions()==gpu.completions()*3,"GPU_THREE_ROLE_ROUTE");
    }
    void semantics(FrozenBackend&gpu,bool small){
        for(auto n:{
            1u,2u,3u,7u,8u,15u,16u,31u,32u,33u,63u,64u,65u,127u,128u,129u,257u,1025u
        }
        ){
            if(small&&n>129)continue;
            auto w=work(n);
            exact(gpu,w,0,n);
            exact(gpu,w,std::uint32_t(NonceSpace-n),n);
        }
        auto a=work(100),b=work(101);
        exact(gpu,a,0,33);
        auto before=gpu.pre_bmw(33);
        exact(gpu,b,0,33);
        exact(gpu,a,0,33);
        require(gpu.pre_bmw(33)==before,"GPU_A_B_A");
        gpu.test_wrong_module(b->words);
        gpu.scan(0,33,a->target,true);
        require(gpu.pre_bmw(33)==before,"GPU_WRONG_MODULE_DEPENDENCY");
        gpu.test_stale_record(b->words);
        gpu.scan(0,33,a->target,true);
        require(gpu.pre_bmw(33)!=before,"STALE_RECORD_NEGATIVE_NOT_DETECTED");
        exact(gpu,a,0,33);
        for(unsigned i=0;i<8;++i){
            auto w=work(200+i);
            auto expected=cpu_hash(w->words,UINT32_MAX);
            auto t=upper(expected);
            for(int d:{
                -1,0,1
            }
            ){
                if((d<0&&t==0)||(d>0&&t==UINT64_MAX))continue;
                auto value=d<0?t-1:d>0?t+1:t;
                w->target.fill(UINT32_MAX);
                w->target[6]=std::uint32_t(value);
                w->target[7]=std::uint32_t(value>>32);
                exact(gpu,w,UINT32_MAX,1);
            }
        }
        // Same production range transaction as the miner. No fake GPU hit list here.
        for(auto role:{
            Role::User,Role::DevFee
        }
        ){
            auto w=work(300);
            w->key.session.authority=origin(role);
            w->target.fill(UINT32_MAX);
            gpu.install(*w);
            QuotaFrame q;
            Mailbox m;
            auto c=q.choose(true,true,NonceSpace);
            while(c->role!=role){
                q.commit(c->role,c->count,c->segment);
                c=q.choose(true,true,NonceSpace);
            }
            Assignment ass{
                w,role,1,c->segment,17,257
            };
            q.scheduled(role,257);
            RangeTransaction tx(ass);
            std::uint64_t committed=0,actual=0;
            std::vector<unsigned> got;
            while(!tx.done()){
                auto r=tx.step(gpu,m,q,[]{
                    return true;
                }
                ,[](const Words&x,unsigned n){
                    return cpu_hash(x,n);
                }
                );
                committed+=r.committed;
                actual+=r.actual;
                while(auto hit=m.pop()){
                    require(hit->assignment.role==role&&hit->assignment.work->key==w->key,"GPU_ORIGIN_BINDING");
                    got.push_back(hit->nonce);
                }
            }
            std::sort(got.begin(),got.end());
            require(committed==257&&actual>257&&got.size()==257&&std::adjacent_find(got.begin(),got.end())==got.end()&&got.front()==17&&got.back()==273,"GPU_OVERFLOW_REPLAY");
        }
        if(!small){
            auto w=work(400);
            exact(gpu,w,0,NativeBatch,false);
            auto before_full=gpu.pre_bmw(NativeBatch);
            exact(gpu,w,0,NativeBatch,false);
            require(before_full==gpu.pre_bmw(NativeBatch),"GPU_FULL_BATCH_REPEAT");
        }
        // Fail before launch for invalid ranges and target mutation.
        auto w=work(500);
        gpu.install(*w);
        auto old=gpu.submissions();
        for(auto n:{
            0u,NativeBatch+1
        }
        ){
            bool fail=false;
            try{
                gpu.scan(0,n,w->target);
            }
            catch(const Error&){
                fail=true;
            }
            require(fail,"INVALID_GPU_RANGE_ACCEPTED");
        }
        bool fail=false;
        try{
            gpu.scan(UINT32_MAX,2,w->target);
        }
        catch(const Error&){
            fail=true;
        }
        require(fail&&old==gpu.submissions(),"NONCE_WRAP_ACCEPTED");
    }
}
int main(int argc,char**argv){
    try{
        require(argc==3,"GPU_TEST_USAGE");
        auto d=int(decimal(argv[1],INT32_MAX));
        std::string mode=argv[2];
        require(mode=="correctness"||mode=="sanitizer","GPU_TEST_MODE");
        std::cout<<"{\"event\":\"GPU_API_BEGIN\",\"network\":false}"<<std::endl;
        FrozenBackend gpu(d);
        std::cout<<gpu.identity()<<std::endl;
        semantics(gpu,mode=="sanitizer");
        std::cout<<"{\"status\":\"GPU_TEST_PASS\",\"cpu_spot_or_all_compares\":"<<checks<<",\"execution\":"<<gpu.accounting()<<"}\n";
        return 0;
    }
    catch(const Error&e){
        std::cerr<<"{\"status\":\"GPU_TEST_STOP\",\"code\":"<<json_quote(e.what())<<"}\n";
        return 2;
    }
    catch(...){
        std::cerr<<"GPU_TEST_UNEXPECTED_REDACTED\n";
        return 2;
    }
}
