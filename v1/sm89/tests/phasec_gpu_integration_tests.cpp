#include "mona/miner.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
mona::Config stable_base_config() {
    mona::Config c;
    c.batch = 2097152u;
    c.fusion = 6u;
    c.graph = false;
    c.blake_block = 256;
    c.cube_block = 256;
    c.lyra_block = 256;
    c.skein_block = 256;
    c.bmw_block = 256;
    c.carveout = -1;
    return c;
}
}

int main(int argc, char** argv) {
    try {
        const int device = argc > 1 ? std::stoi(argv[1]) : 0;
        mona::MiningEngine engine(device, stable_base_config());
        const auto info = engine.device();
        if (info.sm != 89) throw std::runtime_error("Mona Miner SM89 acceptance test requires SM89");

        std::array<std::uint32_t,20> words{};
        for (std::size_t i=0;i<words.size();++i) words[i] = 0x01020304u * static_cast<std::uint32_t>(i+1);
        words[19] = 0;
        // Prove that the GPU BMW high64 hit is only a prefilter: construct a target
        // whose high64 exactly matches a CPU hash but whose lower words are stricter.
        // The GPU must return the nonce as a high64 candidate, while MiningEngine must
        // suppress it after the CPU full 256-bit comparison.
        constexpr std::uint32_t tie_nonce = 12345u;
        const auto tie_hash = mona::cpu_hash(words, tie_nonce);
        auto tie_target = tie_hash;
        bool made_stricter = false;
        for (int i=5; i>=0; --i) {
            if (tie_target[static_cast<std::size_t>(i)] != 0) {
                --tie_target[static_cast<std::size_t>(i)];
                for (int j=i-1; j>=0; --j) tie_target[static_cast<std::size_t>(j)] = 0xffffffffu;
                made_stricter = true;
                break;
            }
        }
        if (!made_stricter) throw std::runtime_error("could not construct high64-tie strict target");
        engine.set_work(1, words, tie_target);
        std::size_t tie_emitted = 0;
        const auto tie = engine.scan_once(tie_nonce, 1, [&](const mona::Solution&) { ++tie_emitted; });
        if (tie.unique_hashes != 1 || tie.solutions != 0 || tie_emitted != 0)
            throw std::runtime_error("GPU high64 prefilter candidate bypassed CPU full 256-bit target check");

        std::array<std::uint32_t,8> target{};
        target.fill(0xffffffffu); // Every hash is valid: forces >64-candidate overflow/replay.
        engine.set_work(2, words, target);

        constexpr std::uint32_t first = 0xfffffeffu;
        constexpr std::uint32_t count = 257u; // range ends exactly at 2^32
        std::vector<mona::Solution> solutions;
        const auto a = engine.scan_once(first, count, [&](const mona::Solution& s) {
            solutions.push_back(s);
        });

        if (a.next_nonce != (1ull<<32)) throw std::runtime_error("nonce boundary wrapped or next_nonce is wrong");
        if (a.unique_hashes != count) throw std::runtime_error("unique_hashes mismatch");
        if (a.solutions != count || solutions.size() != count) throw std::runtime_error("candidate overflow/replay lost a solution");
        if (a.cancelled) throw std::runtime_error("unexpected cancellation");

        std::set<std::uint32_t> seen;
        for (const auto& s : solutions) {
            if (!seen.insert(s.nonce).second) throw std::runtime_error("duplicate solution emitted during overflow replay");
            if (s.hash != mona::cpu_hash(words, s.nonce)) throw std::runtime_error("integrated GPU candidate != CPU oracle");
        }
        for (std::uint64_t n=first;n<(1ull<<32);++n) {
            if (!seen.count(static_cast<std::uint32_t>(n))) throw std::runtime_error("missing nonce at 32-bit boundary");
        }

        std::printf("phasec_gpu_integration_tests: PASS device=%s sm=%d unique=%llu executed=%llu solutions=%llu next_nonce=%llu\n",
            info.name.c_str(), info.sm,
            static_cast<unsigned long long>(a.unique_hashes),
            static_cast<unsigned long long>(a.executed_hashes),
            static_cast<unsigned long long>(a.solutions),
            static_cast<unsigned long long>(a.next_nonce));
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "phasec_gpu_integration_tests: FAIL: %s\n", e.what());
        return 1;
    }
}
