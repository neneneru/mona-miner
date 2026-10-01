// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "base.hpp"
#include <condition_variable>
namespace mona2 {
    // No socket, DNS or WSA initialization in this object.
    class StopSignal {
        std::atomic<bool> stopped_{
            false
        };
        mutable std::mutex mu_;
        std::condition_variable cv_;
#ifdef _WIN32
        void* event_=nullptr;
#endif
        public:
        StopSignal();
        ~StopSignal();
        void request()noexcept;
        bool requested()const{
            return stopped_.load();
        }
        bool wait(std::chrono::milliseconds);
#ifdef _WIN32
        void* event()const{
            return event_;
        }
#endif
    };
    struct IoStats {
        std::atomic<std::uint64_t> wsa{
            0
        }
        ,dns{
            0
        }
        ,sockets{
            0
        }
        ,connects{
            0
        };
    };
    IoStats& io_stats();
    class NetworkRuntime {
        bool active_=false;
        public:NetworkRuntime();
        ~NetworkRuntime();
        NetworkRuntime(const NetworkRuntime&)=delete;
    };
    class Transport {
        struct Impl;
        std::unique_ptr<Impl> impl_;
        public:
        Transport();
        ~Transport();
        void connect(const Endpoint&,StopSignal&,bool loopback_only);
        std::optional<std::size_t> read(std::span<char>);
        std::optional<std::size_t> write(std::string_view);
        void wait(StopSignal&,bool writing,std::chrono::milliseconds timeout);
        void close()noexcept;
    };
}
