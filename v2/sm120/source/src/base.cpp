// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/base.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <sstream>
extern "C" {
#include "jansson.h"
}
extern "C" {
#include "sph/sph_sha2.h"
#include "sph/sph_blake.h"
#include "sph/sph_keccak.h"
#include "sph/sph_cubehash.h"
#include "sph/sph_skein.h"
#include "sph/sph_bmw.h"
#include "lyra2/Lyra2.h"
}
namespace mona2 {
    std::string Count::decimal()const{
        std::array<std::uint32_t,4> w{
            std::uint32_t(lo),std::uint32_t(lo>>32),std::uint32_t(hi),std::uint32_t(hi>>32)
        };
        std::string s;
        do{
            std::uint64_t rem=0;
            for(int i=3;i>=0;--i){
                auto v=(rem<<32)|w[std::size_t(i)];
                w[std::size_t(i)]=std::uint32_t(v/10);
                rem=v%10;
            }
            s.push_back(char('0'+rem));
        }
        while(w[0]||w[1]||w[2]||w[3]);
        std::reverse(s.begin(),s.end());
        return s;
    }
    double Count::number()const{
        return std::ldexp(double(hi),64)+double(lo);
    }
    std::uint64_t decimal(std::string_view s,std::uint64_t max){
        require(!s.empty()&&!(s.size()>1&&s.front()=='0'),"INVALID_CANONICAL_INTEGER");
        std::uint64_t n=0;
        for(char c:s){
            require(c>='0'&&c<='9',"INVALID_CANONICAL_INTEGER");
            const auto d=unsigned(c-'0');
            require(n<=max/10&&(n<max/10||d<=max%10),"INTEGER_OUT_OF_RANGE");
            n=n*10+d;
        }
        return n;
    }
    std::string Endpoint::canonical()const{
        return "stratum+tcp://"+(host.find(':')==std::string::npos?host:"["+host+"]")+":"+std::to_string(port);
    }
    Endpoint endpoint(std::string_view s){
        constexpr std::string_view prefix="stratum+tcp://";
        require(s.starts_with(prefix),"ENDPOINT_SCHEME");
        s.remove_prefix(prefix.size());
        require(s.size()<=512,"ENDPOINT_LENGTH");
        Endpoint e;
        std::string_view port;
        if(!s.empty()&&s[0]=='['){
            auto p=s.find(']');
            require(p!=s.npos&&p+1<s.size()&&s[p+1]==':',"ENDPOINT_IPV6");
            e.host=std::string(s.substr(1,p-1));
            port=s.substr(p+2);
        }
        else {
            auto p=s.find(':');
            require(p!=s.npos,"ENDPOINT_PORT_REQUIRED");
            e.host=std::string(s.substr(0,p));
            port=s.substr(p+1);
        }
        require(!e.host.empty()&&e.host.size()<=253,"ENDPOINT_HOST");
        for(char& c:e.host){
            require((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-'||c==':',"ENDPOINT_HOST_CHAR");
            if(c>='A'&&c<='Z')c=char(c-'A'+'a');
        }
        const auto n=decimal(port,65535);
        require(n>0,"ENDPOINT_PORT_ZERO");
        e.port=std::uint16_t(n);
        return e;
    }
    void validate_credential(std::string_view s,std::size_t max,bool empty){
        require((empty||!s.empty())&&s.size()<=max,"CREDENTIAL_LENGTH");
        for(unsigned char c:s)require(c>=32&&c!=127,"CREDENTIAL_CONTROL");
        std::string copy(s);
        auto* value=::json_string(copy.c_str());
        require(value!=nullptr,"CREDENTIAL_UTF8");
        json_decref(value);
        std::fill(copy.begin(),copy.end(),'\0');
    }
    std::string hex(std::span<const std::uint8_t>b){
        constexpr char d[]="0123456789abcdef";
        std::string s;
        s.reserve(b.size()*2);
        for(auto v:b){
            s.push_back(d[v>>4]);
            s.push_back(d[v&15]);
        }
        return s;
    }
    Bytes unhex(std::string_view s,std::size_t max){
        require(s.size()%2==0&&s.size()/2<=max,"HEX_SIZE");
        auto digit=[](char c)->unsigned{
            if(c>='0'&&c<='9')return unsigned(c-'0');
            if(c>='a'&&c<='f')return unsigned(c-'a'+10);
            if(c>='A'&&c<='F')return unsigned(c-'A'+10);
            throw Error("HEX_DIGIT");
        };
        Bytes b(s.size()/2);
        for(std::size_t i=0;i<b.size();++i)b[i]=std::uint8_t(digit(s[i*2])*16+digit(s[i*2+1]));
        return b;
    }
    std::string nonce_hex(std::uint32_t n){
        std::array<std::uint8_t,4>b{};
        for(unsigned i=0;i<4;++i)b[i]=std::uint8_t(n>>(8*i));
        return hex(b);
    }
    Hash256 sha256(std::span<const std::uint8_t>b){
        sph_sha256_context c;
        Hash256 out{};
        sph_sha256_init(&c);
        sph_sha256(&c,b.data(),b.size());
        sph_sha256_close(&c,out.data());
        return out;
    }
    Hash256 sha256d(std::span<const std::uint8_t>b){
        auto t=sha256(b);
        return sha256(t);
    }
    Hash256 fingerprint(std::string_view b){
        return sha256(std::span(reinterpret_cast<const std::uint8_t*>(b.data()),b.size()));
    }
    Target pool_target(double d){
        require(std::isfinite(d)&&d>0,"DIFFICULTY_INVALID");
        d/=256.0;
        require(std::isfinite(d)&&d>0,"DIFFICULTY_SCALE_RANGE");
        int k=6;
        for(;k>0&&d>1.0;--k)d/=4294967296.0;
        const double scaled=4294901760.0/d;
        require(std::isfinite(scaled)&&scaled>=1.0&&scaled<18446744073709551616.0,"DIFFICULTY_TARGET_RANGE");
        const auto m=static_cast<std::uint64_t>(scaled);
        Target t{};
        t[std::size_t(k)]=std::uint32_t(m);
        t[std::size_t(k+1)]=std::uint32_t(m>>32);
        return t;
    }
    Target cpu_hash(const Words&w,std::uint32_t nonce,Target* pre){
        // SPH BLAKE has a process-global round count in this pinned source. It is
        // initialized once before any concurrent oracle calls and never changed.
        static std::once_flag init;
        std::call_once(init,[]{
            sph_blake256_set_rounds(14);
        }
        );
        std::array<std::uint8_t,80> input{};
        for(unsigned i=0;i<20;++i){
            auto v=i==19?nonce:w[i];
            for(unsigned j=0;j<4;++j)input[4*i+j]=std::uint8_t(v>>(24-8*j));
        }
        Target a{}
        ,b{};
        sph_blake256_context bl;
        sph_keccak256_context ke;
        sph_cubehash256_context cu;
        sph_skein256_context sk;
        sph_bmw256_context bm;
        sph_blake256_init(&bl);
        sph_blake256(&bl,input.data(),80);
        sph_blake256_close(&bl,a.data());
        sph_keccak256_init(&ke);
        sph_keccak256(&ke,a.data(),32);
        sph_keccak256_close(&ke,b.data());
        sph_cubehash256_init(&cu);
        sph_cubehash256(&cu,b.data(),32);
        sph_cubehash256_close(&cu,a.data());
        require(LYRA2(b.data(),32,a.data(),32,a.data(),32,1,4,4)==0,"REFERENCE_ALLOCATION");
        sph_skein256_init(&sk);
        sph_skein256(&sk,b.data(),32);
        sph_skein256_close(&sk,a.data());
        sph_cubehash256_init(&cu);
        sph_cubehash256(&cu,a.data(),32);
        sph_cubehash256_close(&cu,b.data());
        if(pre)*pre=b;
        sph_bmw256_init(&bm);
        sph_bmw256(&bm,b.data(),32);
        sph_bmw256_close(&bm,a.data());
        return a;
    }
    std::uint64_t upper(const Target&h){
        return std::uint64_t(h[6])|(std::uint64_t(h[7])<<32);
    }
    bool full_test(const Target&h,const Target&t){
        for(int i=7;i>=0;--i){
            if(h[std::size_t(i)]<t[std::size_t(i)])return true;
            if(h[std::size_t(i)]>t[std::size_t(i)])return false;
        }
        return true;
    }
    std::string json_quote(std::string_view s){
        std::string o="\"";
        constexpr char h[]="0123456789abcdef";
        for(unsigned char c:s){
            if(c=='"'||c=='\\'){
                o+='\\';
                o+=char(c);
            }
            else if(c<32||c==127){
                o+="\\u00";
                o+=h[c>>4];
                o+=h[c&15];
            }
            else o+=char(c);
        }
        return o+'"';
    }
    std::string escape_controls(std::string_view s){
        auto q=json_quote(s);
        return q.substr(1,q.size()-2);
    }
}
