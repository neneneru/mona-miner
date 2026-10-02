#include "mona/core.hpp"
#include <cstring>
#include <stdexcept>
extern "C" {
#include "sph/sph_blake.h"
#include "sph/sph_keccak.h"
#include "sph/sph_cubehash.h"
#include "sph/sph_skein.h"
#include "sph/sph_bmw.h"
#include "lyra2/Lyra2.h"
}
namespace mona {
std::array<uint32_t,8> cpu_hash(const std::array<uint32_t,20>& data,uint32_t nonce,
                               std::array<uint64_t,4>* pre_bmw) {
    unsigned char input[80];
    for(unsigned i=0;i<20;++i) {
        uint32_t w=i==19?nonce:data[i];
        input[4*i]=uint8_t(w>>24); input[4*i+1]=uint8_t(w>>16);
        input[4*i+2]=uint8_t(w>>8); input[4*i+3]=uint8_t(w);
    }
    std::array<uint32_t,8> a{},b{};
    sph_blake256_context blake; sph_keccak256_context keccak;
    sph_cubehash256_context cube; sph_skein256_context skein; sph_bmw256_context bmw;
    // Dedicated library: initialize the upstream global round selector once,
    // with C++ thread-safe static initialization, not on every candidate.
    static const bool rounds_configured=[] { sph_blake256_set_rounds(14); return true; }();
    (void)rounds_configured;
    sph_blake256_init(&blake); sph_blake256(&blake,input,80); sph_blake256_close(&blake,a.data());
    sph_keccak256_init(&keccak); sph_keccak256(&keccak,a.data(),32); sph_keccak256_close(&keccak,b.data());
    sph_cubehash256_init(&cube); sph_cubehash256(&cube,b.data(),32); sph_cubehash256_close(&cube,a.data());
    if(LYRA2(b.data(),32,a.data(),32,a.data(),32,1,4,4)!=0) throw std::runtime_error("CPU LYRA2 allocation failed");
    sph_skein256_init(&skein); sph_skein256(&skein,b.data(),32); sph_skein256_close(&skein,a.data());
    sph_cubehash256_init(&cube); sph_cubehash256(&cube,a.data(),32); sph_cubehash256_close(&cube,b.data());
    if(pre_bmw) std::memcpy(pre_bmw->data(),b.data(),32);
    sph_bmw256_init(&bmw); sph_bmw256(&bmw,b.data(),32); sph_bmw256_close(&bmw,a.data());
    return a;
}
bool full_test(const std::array<uint32_t,8>& hash,const std::array<uint32_t,8>& target) {
    for(int i=7;i>=0;--i) { if(hash[i]<target[i]) return true; if(hash[i]>target[i]) return false; }
    return true;
}
}
