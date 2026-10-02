// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/application.hpp"
#include <iostream>
#include <string_view>
namespace {
    bool requested_json(int argc,char**argv){
        for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--json")return true;
        return false;
    }
}
int main(int argc,char**argv){
    const bool json=requested_json(argc,argv);
    try{
        return mona2::run_application(mona2::parse_cli(argc,argv));
    }
    catch(const mona2::Error&e){
        if(json)std::cerr<<"{\"event\":\"ERROR\",\"code\":"<<mona2::json_quote(e.what())<<"}\n";
        else std::cerr<<"ERROR: "<<e.what()<<'\n';
        return 2;
    }
    catch(const std::exception&){
        if(json)std::cerr<<"{\"event\":\"ERROR\",\"code\":\"UNEXPECTED_EXCEPTION_REDACTED\"}\n";
        else std::cerr<<"ERROR: UNEXPECTED_EXCEPTION_REDACTED\n";
        return 2;
    }
}
