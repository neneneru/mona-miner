#include "mona/core.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
double unix_time() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::string quoted(const std::string& in) {
    std::ostringstream o; o<<'"';
    for(unsigned char c:in) {
        if(c=='"' || c=='\\') o<<'\\'<<char(c);
        else if(c<32) o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;
        else o<<char(c);
    }
    o<<'"'; return o.str();
}
uint64_t number(const std::string& s) {
    if(s.empty() || s.front()=='-') throw std::invalid_argument("expected unsigned integer");
    size_t used=0; auto v=std::stoull(s,&used,10);
    if(used!=s.size()) throw std::invalid_argument("invalid integer: "+s);
    return v;
}
unsigned as_unsigned(const std::string& s) {
    auto n=number(s); if(n>std::numeric_limits<unsigned>::max()) throw std::invalid_argument("integer too large");
    return unsigned(n);
}
int as_int(const std::string& s) {
    auto n=number(s); if(n>unsigned(std::numeric_limits<int>::max())) throw std::invalid_argument("integer too large");
    return int(n);
}
std::vector<int> blocks(const std::string& s) {
    std::vector<int> b; std::istringstream stream(s); std::string t;
    while(std::getline(stream,t,',')) b.push_back(as_int(t));
    if(b.size()!=5) throw std::invalid_argument("blocks: blake,cube,lyra,skein,bmw");
    return b;
}
std::string config_json(const mona::Config& c) {
    std::ostringstream o; o<<"{\"batch\":"<<c.batch<<",\"fusion\":"<<c.fusion
      <<",\"graph\":"<<(c.graph?"true":"false")<<",\"blocks\":["
      <<c.blake_block<<','<<c.cube_block<<','<<c.lyra_block<<','<<c.skein_block<<','<<c.bmw_block
      <<"],\"carveout\":"<<c.carveout<<'}'; return o.str();
}
std::string device_json(const mona::DeviceInfo& d) {
    std::ostringstream o; o<<"{\"name\":"<<quoted(d.name)<<",\"uuid\":"<<quoted(d.uuid)
      <<",\"index\":"<<d.device<<",\"sm\":"<<d.sm<<",\"multiprocessors\":"<<d.multiprocessors
      <<",\"driver\":"<<d.driver<<",\"runtime\":"<<d.runtime<<",\"memory_bytes\":"<<d.memory_bytes<<'}'; return o.str();
}
uint64_t upper(const std::array<uint32_t,8>& h) { return uint64_t(h[6]) | (uint64_t(h[7])<<32); }
struct CheckReport {
    uint64_t compared_hashes=0;
    uint64_t small_cpu_compared_hashes=0;
    uint64_t tail_launch_hashes=0;
    uint64_t tail_cpu_compared_hashes=0;
    uint64_t production_capacity=0;
    uint64_t production_launch_hashes=0;
    uint64_t production_launch_cpu_compared_hashes=0;
};
CheckReport check(mona::Context& ctx,const mona::Config& cfg) {
    CheckReport report;
    std::mt19937 rng(0x4d4f4e41);
    uint64_t compared=0;
    for(unsigned header=0;header<3;++header) {
        std::array<uint32_t,20> words{};
        for(auto& w:words) w=header==0?0:header==1?0xffffffffu:rng();
        for(uint32_t length:{1u,7u,8u,9u,31u,32u,33u,127u,128u,129u,257u}) {
            if(length>cfg.batch) continue;
            for(uint32_t first:{0u,uint32_t((uint64_t(1)<<32)-length)}) {
                mona::set_job(ctx,words,0);
                auto got=mona::debug(ctx,first,length);
                std::vector<uint64_t> highs;
                for(uint32_t i=0;i<length;++i) {
                    std::array<uint64_t,4> pre{};
                    auto h=mona::cpu_hash(words,first+i,&pre);
                    for(unsigned p=0;p<4;++p)
                        if(pre[p]!=got.pre_bmw_soa[size_t(p)*length+i]) throw std::runtime_error("GPU pre-BMW 256-bit mismatch");
                    if(upper(h)!=got.bmw_upper64[i]) throw std::runtime_error("GPU BMW upper64 mismatch");
                    highs.push_back(upper(h)); ++compared;
                }
                // Candidate-set equality tests the actual production launch
                // path (including graph replay), not merely the debug kernel.
                for(uint64_t target:{uint64_t(0),highs[length/2],~uint64_t(0)}) {
                    mona::set_job(ctx,words,target);
                    for(int replay=0;replay<2;++replay) {
                        auto hits=mona::scan(ctx,first,length);
                        std::vector<uint32_t> expected;
                        for(uint32_t i=0;i<length;++i) if(highs[i]<=target) expected.push_back(first+i);
                        if(hits.total_found!=expected.size()) throw std::runtime_error("candidate count mismatch");
                        bool overflow=expected.size()>mona::MaxCandidates;
                        if(hits.overflow!=overflow) throw std::runtime_error("overflow contract mismatch");
                        if(!overflow) {
                            std::sort(hits.nonces.begin(),hits.nonces.end());
                            if(hits.nonces!=expected) throw std::runtime_error("candidate set mismatch (nonce zero/tail/graph)");
                        }
                    }
                }
            }
        }
    }
    report.small_cpu_compared_hashes=compared;
    // Large-grid tail audit: non-multiple-of-warp/hash-group count.
    if(cfg.batch>=65537) {
        constexpr uint32_t length=65537;
        constexpr uint32_t first=0x00123456u;
        std::array<uint32_t,20> words{};
        for(auto& w:words) w=rng();
        mona::set_job(ctx,words,0);
        auto got=mona::debug(ctx,first,length);
        std::vector<uint32_t> sample={0,1,2,3,4,7,8,15,16,31,32,33,63,64,65,
                                      length-5,length-4,length-3,length-2,length-1};
        std::mt19937 sample_rng(0x5342414c);
        for(int i=0;i<32;++i) sample.push_back(sample_rng()%length);
        std::sort(sample.begin(),sample.end());
        sample.erase(std::unique(sample.begin(),sample.end()),sample.end());
        for(uint32_t i:sample) {
            std::array<uint64_t,4> pre{};
            auto h=mona::cpu_hash(words,first+i,&pre);
            for(unsigned plane=0;plane<4;++plane)
                if(pre[plane]!=got.pre_bmw_soa[size_t(plane)*length+i])
                    throw std::runtime_error("large-grid GPU pre-BMW mismatch");
            if(upper(h)!=got.bmw_upper64[i])
                throw std::runtime_error("large-grid GPU BMW upper64 mismatch");
            ++compared;
        }
        mona::set_job(ctx,words,~uint64_t(0));
        auto all=mona::scan(ctx,first,length);
        if(all.total_found!=length || !all.overflow)
            throw std::runtime_error("large-grid production/tail candidate mismatch");
        report.tail_launch_hashes=length;
        report.tail_cpu_compared_hashes=compared-report.small_cpu_compared_hashes;
    }

    // Exercise the actual production launch at the configured capacity without
    // pretending to CPU-compare every hash.  Sanitizer qualification uses this
    // same path, so capacity and comparison coverage remain distinct metrics.
    if(cfg.batch>0) {
        std::array<uint32_t,20> words{};
        for(auto& w:words) w=rng();
        constexpr uint32_t first=0x01020304u;
        mona::set_job(ctx,words,0);
        auto full=mona::scan(ctx,first,cfg.batch);
        if(full.overflow) throw std::runtime_error("unexpected production-capacity candidate overflow");
        for(uint32_t nonce:full.nonces) {
            uint64_t delta=uint64_t(nonce)-uint64_t(first);
            if(delta>=cfg.batch) throw std::runtime_error("production-capacity nonce out of range");
        }
        report.production_capacity=cfg.batch;
        report.production_launch_hashes=cfg.batch;
        report.production_launch_cpu_compared_hashes=0;
    }

    // Alternating start nonces on one graph: catches captured stale parameters.
    if(cfg.batch>=17) {
        std::array<uint32_t,20> words{};
        mona::set_job(ctx,words,~uint64_t(0));
        for(uint32_t first:{0u,19u,101u,0xffffffe0u}) {
            auto h=mona::scan(ctx,first,17);
            std::sort(h.nonces.begin(),h.nonces.end());
            if(h.overflow || h.nonces.size()!=17) throw std::runtime_error("graph nonce-parameter mismatch");
            for(unsigned i=0;i<17;++i) if(h.nonces[i]!=first+i) throw std::runtime_error("stale graph nonce");
        }
    }
    report.compared_hashes=compared;
    return report;
}
}
int main(int argc,char** argv) {
    try {
        mona::Config cfg;
        int device=0; std::string mode="check",path;
        unsigned seconds=8,warmup=3;
        for(int i=1;i<argc;++i) {
            std::string k=argv[i];
            if(k=="--help") {
                std::cout<<"mona_bench --mode identity|check|bench|profile --device N --batch N --fusion 0..7\n"
                 " --graph 0|1 --blocks blake,cube,lyra,skein,bmw --carveout -1|0..100\n"
                 " --seconds N --warmup N --json file\n"; return 0;
            }
            if(i+1==argc) throw std::invalid_argument("missing value: "+k);
            std::string v=argv[++i];
            if(k=="--mode") mode=v;
            else if(k=="--device") device=as_int(v);
            else if(k=="--batch") cfg.batch=as_unsigned(v);
            else if(k=="--fusion") cfg.fusion=as_unsigned(v);
            else if(k=="--graph") { if(v!="0" && v!="1") throw std::invalid_argument("graph must be 0 or 1"); cfg.graph=v=="1"; }
            else if(k=="--blocks") {auto b=blocks(v); cfg.blake_block=b[0];cfg.cube_block=b[1];cfg.lyra_block=b[2];cfg.skein_block=b[3];cfg.bmw_block=b[4];}
            else if(k=="--carveout") cfg.carveout=v=="-1"?-1:as_int(v);
            else if(k=="--seconds") seconds=as_unsigned(v);
            else if(k=="--warmup") warmup=as_unsigned(v);
            else if(k=="--json") path=v;
            else throw std::invalid_argument("unknown option: "+k);
        }
        if(seconds==0 || seconds>86400 || warmup>3600) throw std::invalid_argument("invalid duration");
        auto build=mona::build_identity();
        std::ostringstream o; o<<std::setprecision(17);
        o<<"{\"schema\":2,\"base\":\"6ff4e50987e59a70056324a94ed8667cc0bf598d\","
         <<"\"build_id\":"<<quoted(build.build_id)<<",\"build_profile\":"<<quoted(build.profile)
         <<",\"build_identity_schema\":"<<quoted(build.schema)<<",\"type\":"<<quoted(mode)
         <<",\"config\":"<<config_json(cfg);
        if(mode=="identity") {
            o<<"}"<<'\n';
            if(!path.empty()) {std::ofstream f(path); if(!f)throw std::runtime_error("cannot open output JSON");f<<o.str();if(!f)throw std::runtime_error("output write failed");}
            std::cout<<o.str(); return 0;
        }
        auto ctx=mona::create(device,cfg);
        auto info=mona::device_info(*ctx);
        o<<",\"device\":"<<device_json(info);
        if(mode=="check") {
            auto r=check(*ctx,cfg);
            o<<",\"passed\":true,\"compared_hashes\":"<<r.compared_hashes
             <<",\"coverage\":{\"small_cpu_compared_hashes\":"<<r.small_cpu_compared_hashes
             <<",\"tail_launch_hashes\":"<<r.tail_launch_hashes
             <<",\"tail_cpu_compared_hashes\":"<<r.tail_cpu_compared_hashes
             <<",\"production_capacity\":"<<r.production_capacity
             <<",\"production_launch_hashes\":"<<r.production_launch_hashes
             <<",\"production_launch_cpu_compared_hashes\":"<<r.production_launch_cpu_compared_hashes<<'}'
             <<",\"pre_bmw_bits\":256,\"gpu_bmw_checked_bits\":64";
        } else if(mode=="bench" || mode=="profile") {
            std::array<uint32_t,20> words{}; std::mt19937 rng(0x4d4f4e41);
            for(auto& w:words) w=rng();
            mona::set_job(*ctx,words,0); // Do not use a synthetic easy target.
            uint64_t cursor=0;
            auto batch=[&](){
                if(cursor+cfg.batch>(uint64_t(1)<<32)) cursor=0; // benchmark only
                auto r=mona::scan(*ctx,uint32_t(cursor),cfg.batch);
                if(r.overflow) throw std::runtime_error("unexpected benchmark overflow");
                cursor+=cfg.batch;
            };
            if(mode=="profile") { batch(); }
            else {
                auto w=Clock::now();
                do {batch();} while(std::chrono::duration<double>(Clock::now()-w).count()<warmup);
                double start_unix=unix_time(); auto start=Clock::now();
                uint64_t hashes=0,batches=0;
                do {batch(); hashes+=cfg.batch; ++batches;}
                while(std::chrono::duration<double>(Clock::now()-start).count()<seconds);
                double elapsed=std::chrono::duration<double>(Clock::now()-start).count();
                double end_unix=unix_time();
                o<<",\"hashes\":"<<hashes<<",\"batches\":"<<batches<<",\"elapsed_s\":"<<elapsed
                 <<",\"mhs\":"<<double(hashes)/elapsed/1e6
                 <<",\"mean_batch_ms\":"<<elapsed*1000/double(batches)
                 <<",\"start_unix\":"<<start_unix<<",\"end_unix\":"<<end_unix;
            }
            o<<",\"kernels\":["; bool comma=false;
            for(auto& k:mona::kernel_info(*ctx)) {
                if(comma) o<<',';
                comma=true;
                o<<"{\"name\":"<<quoted(k.name)<<",\"block\":"<<k.block
                 <<",\"registers_per_thread\":"<<k.registers_per_thread
                 <<",\"local_bytes_per_thread\":"<<k.local_bytes_per_thread
                 <<",\"static_shared_bytes\":"<<k.static_shared_bytes
                 <<",\"dynamic_shared_bytes\":"<<k.dynamic_shared_bytes
                 <<",\"active_blocks_per_sm\":"<<k.active_blocks_per_sm
                 <<",\"theoretical_occupancy\":"<<k.theoretical_occupancy<<'}';
            }
            o<<']';
        } else throw std::invalid_argument("mode must be check, bench or profile");
        o<<"}\n";
        if(!path.empty()) {std::ofstream f(path); if(!f)throw std::runtime_error("cannot open output JSON");f<<o.str();if(!f)throw std::runtime_error("output write failed");}
        std::cout<<o.str(); return 0;
    } catch(const std::exception& e) {
        std::cerr<<"FAILED: "<<e.what()<<'\n'; return 1;
    }
}
