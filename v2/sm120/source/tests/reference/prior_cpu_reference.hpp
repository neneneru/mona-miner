// Independent full-chain CPU oracle, no GPU APIs.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include "job_snapshot.hpp"
extern "C" {
#include "sph/sph_blake.h"
#include "sph/sph_keccak.h"
#include "sph/sph_cubehash.h"
#include "sph/sph_skein.h"
#include "sph/sph_bmw.h"
#include "lyra2/Lyra2.h"
}
namespace n02_reference {
using Digest=std::array<std::uint32_t,8>;
inline void encode(const n02_runtime::Words& w,std::uint32_t nonce,unsigned char* b){
 for(unsigned i=0;i<20;++i){auto v=i==19?nonce:w[i];for(unsigned j=0;j<4;++j)b[4*i+j]=static_cast<unsigned char>(v>>(24-8*j));}}
inline Digest prefix(const n02_runtime::Words& w,std::uint32_t nonce){
 unsigned char input[80];encode(w,nonce,input);Digest a{},b{};
 sph_blake256_context c;sph_keccak256_context k;
 sph_blake256_set_rounds(14);sph_blake256_init(&c);sph_blake256(&c,input,80);sph_blake256_close(&c,a.data());
 sph_keccak256_init(&k);sph_keccak256(&k,a.data(),32);sph_keccak256_close(&k,b.data());return b;}
inline Digest full(const n02_runtime::Words& w,std::uint32_t nonce,Digest* pre=nullptr){
 auto a=prefix(w,nonce);Digest b{};sph_cubehash256_context c;sph_skein256_context s;sph_bmw256_context z;
 sph_cubehash256_init(&c);sph_cubehash256(&c,a.data(),32);sph_cubehash256_close(&c,b.data());
 if(LYRA2(a.data(),32,b.data(),32,b.data(),32,1,4,4))throw std::runtime_error("LYRA2 allocation");
 sph_skein256_init(&s);sph_skein256(&s,a.data(),32);sph_skein256_close(&s,b.data());
 sph_cubehash256_init(&c);sph_cubehash256(&c,b.data(),32);sph_cubehash256_close(&c,a.data());if(pre)*pre=a;
 sph_bmw256_init(&z);sph_bmw256(&z,a.data(),32);sph_bmw256_close(&z,b.data());return b;}
inline std::uint64_t upper(const Digest& h){return std::uint64_t(h[6])|(std::uint64_t(h[7])<<32);}
}
