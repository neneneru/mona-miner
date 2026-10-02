// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/application.hpp"
#include "mona2/frozen_backend.hpp"
#include "mona2/worker.hpp"
#include <iostream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace mona2 {
    namespace {
        std::atomic<StopSignal*> console_stop{nullptr};
#ifdef _WIN32
        BOOL WINAPI on_console(DWORD code){
            if(code==CTRL_C_EVENT||code==CTRL_BREAK_EVENT||code==CTRL_CLOSE_EVENT||code==CTRL_SHUTDOWN_EVENT){
                if(auto*p=console_stop.load())p->request();
                return TRUE;
            }
            return FALSE;
        }
#endif
        struct ConsoleGuard {
            explicit ConsoleGuard(StopSignal&s){
                console_stop.store(&s);
#ifdef _WIN32
                require(SetConsoleCtrlHandler(on_console,TRUE)!=0,"CONSOLE_HANDLER_FAILED");
#endif
            }
            ~ConsoleGuard(){
#ifdef _WIN32
                SetConsoleCtrlHandler(on_console,FALSE);
#endif
                console_stop.store(nullptr);
            }
        };
        std::string network_summary(Authority a,const NetworkCounts&n){
            std::string s="{\"event\":\"NETWORK_SUMMARY\",\"authority\":"+json_quote(a==Authority::User?"USER":"DEVELOPER")+",\"connects\":"+std::to_string(n.connects)+",\"reconnects\":"+std::to_string(n.reconnects)+",\"roles\":[";
            for(unsigned i=0;i<RoleCount;++i){
                if(i)s+=',';
                s+="{\"role\":"+json_quote(role_name(Role(i)))+",\"accepted\":"+std::to_string(n.accepted[i])+",\"rejected\":"+std::to_string(n.rejected[i])+",\"pool_stale_reported\":"+std::to_string(n.stale_pool[i])+",\"unknown\":"+std::to_string(n.unknown[i])+",\"not_sent\":"+std::to_string(n.not_sent[i])+",\"local_stale\":"+std::to_string(n.local_stale[i])+",\"accepted_assigned_difficulty\":"+std::to_string(n.accepted_difficulty[i])+"}";
            }
            return s+"]}";
        }
    }
    int run_application(Options o,std::optional<std::chrono::seconds> duration){
        if(o.help){
            std::cout<<"Mona Miner v2 - Monacoin Lyra2REv2\nUsage:\n  mona-miner.exe -a lyra2v2 -o stratum+tcp://HOST:PORT -u USER -p PASS [--device N] [--shares-limit N] [--all] [--interval N] [--json]\n  mona-miner.exe -a lyra2v2 --benchmark [--device N] [--benchmark-batches N]\nConsole output:\n  default       v1-style 60-second aggregate summary\n  --all         print every USER share result\n  --interval N  aggregate summary interval in seconds (1..86400; default 60)\n  --json        diagnostic JSON event stream\nDeveloper Fee: 2.00% of completed unique local work.\nOnly lyra2v2 is supported; benchmark/help create no network connection.\n";
            return 0;
        }
        if(o.benchmark){
            // Branch before stdin credentials, WSA, resolver, actor or socket construction.
            FrozenBackend gpu(o.device);
            Work w{};
            w.target={};
            gpu.install(w);
            QuotaFrame accounting;
            accounting.availability(true,false);
            Mailbox output;
            std::uint64_t executed=0;
            Count completed;
            const auto start=Clock::now();
            for(std::uint64_t i=0;i<o.benchmark_batches;++i){
                auto work=std::make_shared<Work>(w);
                Assignment a{work,Role::User,i+1,0,std::uint32_t((i%4096)*NativeBatch),NativeBatch};
                accounting.scheduled(Role::User,NativeBatch);
                RangeTransaction t(a);
                while(!t.done()){
                    auto r=t.step(gpu,output,accounting,[]{return true;},[](const Words&x,std::uint32_t n){return cpu_hash(x,n);});
                    executed=add64(executed,r.actual);
                    completed+=Count(r.committed);
                    while(output.pop()){}
                }
            }
            const double seconds=std::chrono::duration<double>(Clock::now()-start).count();
            std::cout<<"{\"event\":\"BENCHMARK\",\"evaluations\":"<<json_quote(completed.decimal())<<",\"repeats_after_2pow32\":true,\"seconds\":"<<seconds<<",\"MHs\":"<<completed.number()/seconds/1e6<<",\"network_created\":false,\"accounting\":"<<gpu.accounting()<<"}\n";
            return 0;
        }
        if(o.credentials_stdin)read_credentials(o);
        AuditLog log(o.json,o.all);
        if(!o.json){
            log.console("Mona Miner - Lyra2REv2");
            log.console("Pool: "+o.user.endpoint.canonical());
        }
        log.secret(o.user.endpoint.canonical());
        log.secret(o.user.worker);
        log.secret(o.user.password);
        StopSignal stop;
        ConsoleGuard console(stop);
        NetworkRuntime sockets;
        FrozenBackend backend(o.device);
        log.gpu_identity(o.device,backend.identity());
        if(o.json)log.line("{\"event\":\"POLICY\",\"developer_fee_percent\":2,\"debt_carry_forward\":false}");
        else log.console("Developer Fee: 2%");
        ShareBudget budget(o.shares_limit);
        std::atomic<bool> producer_done{false};
        const auto run=static_cast<std::uint64_t>(Clock::now().time_since_epoch().count())|1;
        SessionActor user(run,Authority::User,o.user,budget,stop,producer_done,log,false,o.all);
        SessionActor developer(run,Authority::Developer,developer_credentials(),budget,stop,producer_done,log,false,o.all);
        int code=0;
        try {
            user.start();
            developer.start();
            GpuWorker worker(backend,user,developer,budget,stop,log,o.device,o.interval);
            worker.run(duration?std::optional<Time>(Clock::now()+*duration):std::nullopt);
        }
        catch(const Error&e){
            log.error(e.what());
            code=2;
        }
        catch(const std::exception&){
            log.error("WORKER_UNEXPECTED_ERROR");
            code=2;
        }
        producer_done.store(true);
        stop.request();
        user.join();
        developer.join();
        // Actor deadline may expire before a slow GPU leaf completes. Reconcile the
        // remaining queue after both its producer and consumer have terminated.
        user.finalize_queue();
        developer.finalize_queue();
        log.line(network_summary(Authority::User,user.counts()));
        log.line(network_summary(Authority::Developer,developer.counts()));
        log.line(backend.accounting());
        const auto terminal=budget.terminal();
        if(terminal=="SHARE_LIMIT_UNCERTAIN_STOP")return 3;
        return code;
    }
}
