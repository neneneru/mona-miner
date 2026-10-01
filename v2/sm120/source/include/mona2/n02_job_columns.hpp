#pragma once
#include <cstdint>
namespace n02 {
struct JobColumns { std::uint32_t word[12]; };
inline std::uint32_t rotr32(std::uint32_t x, unsigned r) { return (x >> r) | (x << (32-r)); }
inline void g(std::uint32_t& a, std::uint32_t& b, std::uint32_t& c, std::uint32_t& d,
              std::uint32_t mx, std::uint32_t my, std::uint32_t ux, std::uint32_t uy) {
    a += (mx ^ uy) + b; d=rotr32(d ^ a,16); c+=d; b=rotr32(b ^ c,12);
    a += (my ^ ux) + b; d=rotr32(d ^ a,8); c+=d; b=rotr32(b ^ c,7);
}
// Must be rebuilt from exactly the same immutable job as cpu_h/c_data.
// This pure CPU helper performs no device update, launch or integration.
inline JobColumns prepare(const std::uint32_t h[8], const std::uint32_t data[3]) {
    const std::uint32_t u[16]={0x243F6A88,0x85A308D3,0x13198A2E,0x03707344,
      0xA4093822,0x299F31D0,0x082EFA98,0xEC4E6C89,0x452821E6,0x38D01377,
      0xBE5466CF,0x34E90C6C,0xC0AC29B7,0xC97C50DD,0x3F84D5B5,0xB5470917};
    std::uint32_t v[16]; for(int i=0;i<8;++i) v[i]=h[i];
    v[8]=u[0];v[9]=u[1];v[10]=u[2];v[11]=u[3];
    v[12]=u[4]^640u;v[13]=u[5]^640u;v[14]=u[6];v[15]=u[7];
    g(v[0],v[4],v[8],v[12],data[0],data[1],u[0],u[1]);
    g(v[2],v[6],v[10],v[14],0x80000000u,0u,u[4],u[5]);
    g(v[3],v[7],v[11],v[15],0u,0u,u[6],u[7]);
    const unsigned ix[12]={0,4,8,12,2,6,10,14,3,7,11,15}; JobColumns out{};
    for(unsigned i=0;i<12;++i)out.word[i]=v[ix[i]];
    return out;
}
}
