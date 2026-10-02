// GPL-3.0-or-later. Job snapshots own their inputs; no mutable job pointers.
#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include "n02_job_columns.hpp"
namespace n02_runtime {
inline constexpr std::uint32_t c_sigma[16][16] = {
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	{ 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
	{ 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
	{ 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
	{ 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
	{ 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
	{ 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
	{ 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
	{ 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
	{ 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
	{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
	{ 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
	{ 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
	{ 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
	{ 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
	{ 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 }
};
inline constexpr std::uint32_t c_IV256[8] = {
	0x6A09E667, 0xBB67AE85,
	0x3C6EF372, 0xA54FF53A,
	0x510E527F, 0x9B05688C,
	0x1F83D9AB, 0x5BE0CD19
};
inline constexpr std::uint32_t c_u256[16] = {
	0x243F6A88, 0x85A308D3,
	0x13198A2E, 0x03707344,
	0xA4093822, 0x299F31D0,
	0x082EFA98, 0xEC4E6C89,
	0x452821E6, 0x38D01377,
	0xBE5466CF, 0x34E90C6C,
	0xC0AC29B7, 0xC97C50DD,
	0x3F84D5B5, 0xB5470917
};
using Words=std::array<std::uint32_t,20>;
inline std::array<std::uint32_t,8> midstate(const Words& w) {
    std::array<std::uint32_t,8> h{};
    for(unsigned i=0;i<8;++i)h[i]=c_IV256[i];
    std::uint32_t v[16];for(unsigned i=0;i<8;++i)v[i]=h[i];
    for(unsigned i=0;i<8;++i)v[8+i]=c_u256[i];v[12]^=512;v[13]^=512;
    constexpr unsigned positions[8][4]={{0,4,8,12},{1,5,9,13},{2,6,10,14},{3,7,11,15},{0,5,10,15},{1,6,11,12},{2,7,8,13},{3,4,9,14}};
    for(unsigned r=0;r<14;++r)for(unsigned q=0;q<8;++q) {
        auto a=positions[q][0],b=positions[q][1],c=positions[q][2],d=positions[q][3];
        auto x=c_sigma[r][q*2],y=c_sigma[r][q*2+1];
        n02::g(v[a],v[b],v[c],v[d],w[x],w[y],c_u256[x],c_u256[y]);
    }
    for(unsigned i=0;i<16;++i)h[i&7]^=v[i];return h;
}
struct Snapshot final {
    const Words words;
    const std::array<std::uint32_t,8> h;
    const std::array<std::uint32_t,4> data;
    explicit Snapshot(Words value):words(value),h(midstate(words)),data{words[16],words[17],words[18],words[19]} {}
};
inline n02::JobColumns prepare_record(const Snapshot& s) {return n02::prepare(s.h.data(),s.data.data());}
static_assert(sizeof(n02::JobColumns)==48,"job record must be exactly 48 bytes");
// Device copies and their completion are owned by the runtime, not this ledger.
// Any failed update leaves the transaction invalid. No previously valid record
// may be re-used after begin(), even for an A->B->A or same-header update.
class CommitState {
    std::uint64_t next_=0,committed_=0;
    unsigned fields_=0;bool valid_=false,candidate_=false;
public:
    void begin(){valid_=false;fields_=0;++next_;}
    void copied_h(){fields_|=1;} void copied_data(){fields_|=2;} void copied_record(){fields_|=4;}
    void publish_after_sync(bool candidate){
        if((fields_&(candidate?7:3))!=unsigned(candidate?7:3))throw std::logic_error("incomplete job transaction");
        candidate_=candidate;committed_=next_;valid_=true;
    }
    std::uint64_t generation()const{return committed_;}
    void require_ready(bool candidate,std::uint64_t expected)const{
        if(!valid_||candidate!=candidate_||expected!=committed_)throw std::logic_error("stale/mixed/uncommitted job");
    }
};
}
