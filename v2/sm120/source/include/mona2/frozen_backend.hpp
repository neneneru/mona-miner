// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "accounting.hpp"
#include <thread>
namespace mona2 {
    // The implementation uses only the CUDA Driver API and three embedded cubins.
    // Construction is forbidden in help, parse-error, Inspect, Build and Mock paths.
    class FrozenBackend final : public Backend {
        struct Impl;
        std::unique_ptr<Impl> p_;
        public:
        explicit FrozenBackend(int device);
        ~FrozenBackend() override;
        FrozenBackend(const FrozenBackend&)=delete;
        FrozenBackend& operator=(const FrozenBackend&)=delete;
        void install(const Work&) override;
        RawHits scan(std::uint32_t first,std::uint32_t count,const Target&,bool debug=false) override;
        std::vector<std::uint64_t> pre_bmw(std::uint32_t) override;
        std::vector<std::uint64_t> bmw_upper(std::uint32_t) override;
        void check_guards();
        std::string identity()const;
        std::string accounting()const;
        // Validation binary only; these are not connected to product CLI options.
        void test_stale_record(const Words&);
        void test_wrong_module(const Words&);
        std::uint64_t submissions()const;
        std::uint64_t completions()const;
    };
}
