// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/worker.hpp"
#include <sstream>
#include <iomanip>
namespace mona2 {
    void GpuWorker::run(std::optional<Time> until,std::optional<std::uint64_t> work_limit){
        auto last=Clock::now();
        while(!stop_.requested()){
            if(until&&Clock::now()>=*until){
                stop_.request();
                break;
            }
            std::uint64_t work_left=UINT64_MAX;
            if(work_limit){
                const auto& totals=quota_.counters().committed;
                auto total=totals[0]+totals[1];
                require(total.hi==0&&total.lo<=*work_limit,"VALIDATION_WORK_OVERRUN");
                if(total.lo==*work_limit){
                    stop_.request();
                    break;
                }
                work_left=*work_limit-total.lo;
            }
            auto uv=user_.view(),dv=developer_.view();
            bool u=uv&&uv->ready&&budget_.can_admit(),d=dv&&dv->ready;
            auto choice=quota_.choose(u,d,NonceSpace);
            if(!choice){
                stop_.wait(std::chrono::milliseconds(10));
                continue;
            }
            Authority auth=origin(choice->role);
            unsigned a=index(auth);
            auto& actor=auth==Authority::User?user_:developer_;
            auto v=auth==Authority::User?uv:dv;
            require(v&&v->latest,"READY_WITHOUT_JOB");
            if(!work_[a]||!(work_[a]->key==v->latest->key)||cursor_[a]==NonceSpace){
                auto x=nonces_.allocate(v->latest->endpoint_key,v->latest->xnonce1,v->latest->xnonce2_size);
                work_[a]=build_work(*v->latest,std::move(x),next64(serial_),device_);
                cursor_[a]=0;
            }
            choice=quota_.choose(u,d,std::min(NonceSpace-cursor_[a],work_left));
            require(bool(choice),"QUOTA_ADMISSION_LOST");
            if(!actor.mailbox().has_capacity()){
                auto t=Clock::now();
                stop_.wait(std::chrono::milliseconds(2));
                metrics_.queue_wait_seconds+=std::chrono::duration<double>(Clock::now()-t).count();
                continue;
            }
            if(installed_serial_!=work_[a]->serial){
                auto t=Clock::now();
                backend_.install(*work_[a]);
                metrics_.install_seconds+=std::chrono::duration<double>(Clock::now()-t).count();
                ++metrics_.installs;
                installed_serial_=work_[a]->serial;
            }
            Assignment assignment{
                work_[a],choice->role,next64(execution_),choice->segment,std::uint32_t(cursor_[a]),choice->count
            };
            quota_.scheduled(assignment.role,assignment.count);
            ++metrics_.assignments;
            RangeTransaction transaction(assignment);
            auto admissible=[&](){
                if(stop_.requested())return false;
                auto user=user_.view();
                if(!user||!user->ready||!budget_.can_admit())return false;
                auto v2=actor.view();
                return v2&&v2->ready&&v2->latest&&v2->latest->key==assignment.work->key;
            };
            while(!transaction.done()){
                auto t=Clock::now();
                auto result=transaction.step(backend_,actor.mailbox(),quota_,admissible,[](const Words&w,std::uint32_t n){
                    return cpu_hash(w,n);
                }
                );
                metrics_.scan_contract_seconds+=std::chrono::duration<double>(Clock::now()-t).count();
                cursor_[a]=result.next;
                if(result.blocked){
                    auto qstart=Clock::now();
                    stop_.wait(std::chrono::milliseconds(2));
                    metrics_.queue_wait_seconds+=std::chrono::duration<double>(Clock::now()-qstart).count();
                }
                if(result.cancelled)break;
            }
            if(Clock::now()-last>=std::chrono::seconds(interval_)){
                log_.line(summary());
                last=Clock::now();
            }
        }
        quota_.finish();
        log_.line(summary());
    }
    std::string GpuWorker::summary()const{
        const auto ended=Clock::now();
        const auto utc_end=std::chrono::system_clock::now();
        double wall=std::chrono::duration<double>(ended-start_).count();
        const auto&c=quota_.counters();
        auto total=c.committed[0]+c.committed[1];
        std::ostringstream s;
        s<<std::setprecision(17)<<"{\"event\":\"WORK_SUMMARY\",\"monotonic_start_ns\":\""<<std::chrono::duration_cast<std::chrono::nanoseconds>(start_.time_since_epoch()).count()<<"\",\"monotonic_end_ns\":\""<<std::chrono::duration_cast<std::chrono::nanoseconds>(ended.time_since_epoch()).count()<<"\",\"utc_start_ns\":\""<<std::chrono::duration_cast<std::chrono::nanoseconds>(utc_start_.time_since_epoch()).count()<<"\",\"utc_end_ns\":\""<<std::chrono::duration_cast<std::chrono::nanoseconds>(utc_end.time_since_epoch()).count()<<"\",\"wall_seconds\":"<<wall<<",\"total_committed_MHs\":"<<(wall>0?total.number()/wall/1e6:0)<<",\"user_committed_MHs\":"<<(wall>0?c.committed[0].number()/wall/1e6:0)<<",\"installs\":"<<metrics_.installs<<",\"install_seconds\":"<<metrics_.install_seconds<<",\"queue_wait_seconds\":"<<metrics_.queue_wait_seconds<<",\"scan_contract_seconds\":"<<metrics_.scan_contract_seconds<<",\"accounting\":"<<quota_.json()<<"}";
        return s.str();
    }
}
