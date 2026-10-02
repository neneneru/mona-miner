// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "actor.hpp"
namespace mona2 {
    struct WorkerMetrics {
        std::uint64_t assignments=0,installs=0;
        double install_seconds=0,queue_wait_seconds=0,scan_contract_seconds=0;
    };
    class MiningRateWindow {
        bool active_=false;
        Time start_{};
        Count baseline_{};
        public:
        void reset()noexcept{active_=false;}
        void start(Time now,Count committed){
            if(active_)return;
            active_=true;
            start_=now;
            baseline_=committed;
        }
        std::optional<double> sample(Time now,Count committed,unsigned interval_seconds){
            require(interval_seconds>0,"INTERVAL_ZERO");
            if(!active_)return std::nullopt;
            const double seconds=std::chrono::duration<double>(now-start_).count();
            if(seconds<double(interval_seconds))return std::nullopt;
            const auto delta=committed-baseline_;
            start_=now;
            baseline_=committed;
            return seconds>0.0?delta.number()/seconds/1e6:0.0;
        }
        bool active()const noexcept{return active_;}
    };
    class GpuWorker {
        Backend&backend_;
        SessionActor&user_;
        SessionActor&developer_;
        ShareBudget&budget_;
        StopSignal&stop_;
        AuditLog&log_;
        QuotaFrame quota_;
        ExtranonceRegistry nonces_;
        std::array<std::shared_ptr<const Work>,2> work_{};
        std::array<std::uint64_t,2> cursor_{};
        std::uint64_t serial_=0,execution_=0,installed_serial_=0;
        WorkerMetrics metrics_;
        Time start_=Clock::now();
        std::chrono::system_clock::time_point utc_start_=std::chrono::system_clock::now();
        int device_;
        unsigned interval_;
        public:
        GpuWorker(Backend&b,SessionActor&u,SessionActor&d,ShareBudget&budget,StopSignal&s,AuditLog&l,int device,unsigned interval=60):backend_(b),user_(u),developer_(d),budget_(budget),stop_(s),log_(l),quota_(),device_(device),interval_(interval){}
        void run(std::optional<Time> until=std::nullopt,std::optional<std::uint64_t> validation_work_limit=std::nullopt);
        const QuotaFrame&quota()const{return quota_;}
        const WorkerMetrics&metrics()const{return metrics_;}
        std::string summary()const;
    };
}
