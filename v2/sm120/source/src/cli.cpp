// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/cli.hpp"
#include "mona2/json.hpp"
#include <iostream>
#include <set>
#include <algorithm>
namespace mona2 {
    Options parse_cli(int argc,const char*const*argv){
        Options o;
        std::set<std::string> seen;
        for(int i=1;i<argc;++i){
            std::string k=argv[i];
            if(k=="-a")k="--algo";
            else if(k=="-d")k="--device";
            else if(k=="-h")k="--help";
            else if(k=="-o")k="--url";
            else if(k=="-u")k="--user";
            else if(k=="-p")k="--pass";
            require(seen.insert(k).second,"DUPLICATE_OPTION");
            auto val=[&](){ require(i+1<argc,"MISSING_OPTION_VALUE"); return std::string(argv[++i]); };
            if(k=="--help")o.help=true;
            else if(k=="--benchmark")o.benchmark=true;
            else if(k=="--all")o.all=true;
            else if(k=="--json")o.json=true;
            else if(k=="--credentials-stdin")o.credentials_stdin=true;
            else if(k=="--algo")o.algo=val();
            else if(k=="--url")o.user.endpoint=endpoint(val());
            else if(k=="--user"){o.user.worker=val();validate_credential(o.user.worker,256);}
            else if(k=="--pass"){o.user.password=val();validate_credential(o.user.password,1024,true);}
            else if(k=="--device")o.device=int(decimal(val(),INT32_MAX));
            else if(k=="--interval"){ o.interval=unsigned(decimal(val(),86400)); require(o.interval>0,"INTERVAL_ZERO"); }
            else if(k=="--benchmark-batches"){ o.benchmark_batches=std::uint32_t(decimal(val(),UINT32_MAX)); require(o.benchmark_batches>0,"BENCHMARK_ZERO"); }
            else if(k=="--shares-limit")o.shares_limit=decimal(val(),UINT64_MAX);
            else throw Error("UNSUPPORTED_OPTION");
        }
        require((o.algo.empty()&&o.help)||o.algo=="lyra2v2","ALGORITHM_UNSUPPORTED");
        const bool has_url=seen.contains("--url"),has_user=seen.contains("--user"),has_pass=seen.contains("--pass");
        const bool cli_any=has_url||has_user||has_pass;
        o.credentials_cli=cli_any;
        require(!(o.credentials_stdin&&cli_any),"CREDENTIAL_MODE_CONFLICT");
        if(!o.help&&!o.benchmark&&!o.credentials_stdin){
            require(has_url&&has_user,"MINING_CREDENTIALS_REQUIRED");
        }
        return o;
    }
    void read_credentials(Options&o){
        require(o.credentials_stdin&&!o.credentials_cli,"STDIN_CREDENTIAL_MODE");
        std::string s;
        char c;
        while(std::cin.get(c)&&c!='\n'){ require(s.size()<8192,"CREDENTIAL_INPUT_SIZE"); s+=c; }
        auto j=parse_json(s);
        require(json_is_object(j.get())&&json_object_size(j.get())==3,"CREDENTIAL_INPUT_FIELDS");
        auto url=json_string(member(j.get(),"endpoint"),512,false);
        o.user.endpoint=endpoint(url);
        o.user.worker=json_string(member(j.get(),"worker"),256,false);
        o.user.password=json_string(member(j.get(),"password"),1024);
        validate_credential(o.user.worker,256);
        validate_credential(o.user.password,1024,true);
        std::fill(s.begin(),s.end(),'\0');
    }
}
