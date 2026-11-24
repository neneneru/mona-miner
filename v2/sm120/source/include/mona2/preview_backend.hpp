// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "accounting.hpp"

namespace mona2 {
    class PreviewBackend final : public Backend {
        struct Impl;
        std::unique_ptr<Impl> p_;
    public:
        static constexpr std::uint32_t chunk_size=304640;
        static constexpr std::uint32_t scan_window=24*chunk_size;
        explicit PreviewBackend(int device);
        ~PreviewBackend() override;
        void install(const Work&) override;
        RawHits scan(std::uint32_t first,std::uint32_t count,const Target&,bool debug=false) override;
        std::uint32_t max_scan_count()const noexcept override{return scan_window;}
        std::vector<std::uint64_t> bmw_upper(std::uint32_t count) override;
        std::string identity()const;
        std::string accounting()const;
    };
}
