// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/base.hpp"
#include <iostream>
using namespace mona2;
Target legacy(double diff){
    double d=diff/256.;
    int k=6;
    for(;k>0&&d>1.;--k)d/=4294967296.;
    auto m=std::uint64_t(4294901760./d);
    Target t{};
    t[k]=std::uint32_t(m);
    t[k+1]=std::uint32_t(m>>32);
    return t;
}
int main(){
    try{
        std::cout<<"[";
        unsigned count=0;
        for(auto d:{
            1u,90u,128u,256u,32768u
        }
        ){
            auto t=pool_target(d);
            require(t==legacy(d),"LEGACY_NORMAL_TARGET_CHANGED");
            if(count++)std::cout<<',';
            std::cout<<"{\"difficulty\":"<<d<<",\"words\":[";
            for(unsigned i=0;i<8;++i){
                if(i)std::cout<<',';
                std::cout<<t[i];
            }
            std::cout<<"]}";
        }
        std::cout<<"]\n";
        return 0;
    }
    catch(...){
        return 1;
    }
}
