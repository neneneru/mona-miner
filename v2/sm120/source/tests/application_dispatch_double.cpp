// SPDX-License-Identifier: GPL-3.0-or-later
// CPU dispatch-only checks. Fake backend values are not cryptographic evidence.
#define MONA2_DOUBLE_NO_MAIN
#include "adapter_contract_double.cpp"
#include "mona2/application.hpp"
#include "mona2/transport.hpp"
namespace mona2 {
    Credentials developer_credentials(){
        throw Error("UNEXPECTED_DEVELOPER_DESTINATION_ACCESS");
    }
}
int main(){
    try{
        const char* help[]={
            "probe","--help"
        };
        auto before=calls;
        require(run_application(parse_cli(2,help))==0&&calls==before,"HELP_TOUCHED_GPU");
        for(auto bad:{
            "1x","-1","+1"," 1","1 ","18446744073709551616"
        }
        ){
            const char*a[]={
                "probe","-a","lyra2v2","--device",bad
            };
            bool failed=false;
            try{
                run_application(parse_cli(5,a));
            }
            catch(const Error&){
                failed=true;
            }
            require(failed&&calls==before,"INVALID_CLI_TOUCHED_GPU");
        }
        // Removed/unsafe legacy options must fail before backend or network creation.
        for(const auto* option:{"--donation","--donation=0","--donate","--fee","--devfee"}){
            const char* args[]={"probe","-a","lyra2v2",option,"2"};
            bool failed=false;
            try{run_application(parse_cli(5,args));}catch(const Error&){failed=true;}
            require(failed&&calls==before,"REMOVED_OPTION_CREATED_GPU");
        }
        const char* compat[]={
            "probe","-a","lyra2v2","-o","stratum+tcp://127.0.0.1:1","-u","fixture","-p","fixture-pass"
        };
        auto parsed=parse_cli(9,compat);
        require(parsed.credentials_cli&&!parsed.credentials_stdin&&parsed.user.worker=="fixture"&&calls==before,"V1_CLI_COMPATIBILITY");
        const char* mixed[]={
            "probe","-a","lyra2v2","-o","stratum+tcp://127.0.0.1:1","-u","fixture","--credentials-stdin"
        };
        bool mixed_failed=false;
        try{(void)parse_cli(8,mixed);}catch(const Error&){mixed_failed=true;}
        require(mixed_failed&&calls==before,"CREDENTIAL_MODE_CONFLICT_CREATED_GPU");
        const char* bench[]={
            "probe","-a","lyra2v2","--benchmark","--benchmark-batches","1","--credentials-stdin"
        };
        require(run_application(parse_cli(7,bench))==0,"BENCHMARK_DISPATCH");
        require(io_stats().dns==0&&io_stats().sockets==0&&io_stats().wsa==0,"NONET_MODE_CREATED_NETWORK");
        for(auto*f:funs)delete f;
        delete current;
        require(memory.empty(),"DOUBLE_LEAK");
        std::cout<<"{\"status\":\"PASS\",\"type\":\"APPLICATION_CPU_DISPATCH_DOUBLE\",\"real_GPU_calls\":0,\"sockets\":0}\n";
        return 0;
    }
    catch(const std::exception&e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
