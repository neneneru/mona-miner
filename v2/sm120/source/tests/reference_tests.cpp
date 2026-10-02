// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/base.hpp"
#include "reference/prior_cpu_reference.hpp"
#include <iostream>
using namespace mona2;
int main(){
    try{
        std::uint64_t x=20261001;
        unsigned tests=0;
        for(unsigned s=0;s<16;++s){
            Words w{};
            for(auto&v:w){
                x^=x<<13;
                x^=x>>7;
                x^=x<<17;
                v=std::uint32_t(x);
            }
            for(auto nonce:{
                0u,1u,31u,32u,33u,65535u,0x7fffffffu,0x80000000u,0xfffffffeu,0xffffffffu
            }
            ){
                Target a{}
                ,b{};
                auto h=cpu_hash(w,nonce,&a);
                auto old=n02_reference::full(w,nonce,&b);
                require(a==b&&h==old,"PRIOR_CPU_CHAIN_MISMATCH");
                ++tests;
            }
        }
        std::cout<<"{\"status\":\"PASS\",\"distinct_header_nonce_pairs\":"<<tests<<",\"reference\":\"prior full-chain call graph; same pinned SPHlib primitives\",\"GPU\":0}\n";
        return 0;
    }
    catch(const std::exception&e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
