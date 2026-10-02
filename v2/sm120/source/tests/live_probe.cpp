// SPDX-License-Identifier: GPL-3.0-or-later
// Private validation executable; the shipping CLI has no time/fee bypass.
#include "mona2/application.hpp"
#include <iostream>
int main(int argc,char**argv){
    try{
        mona2::require(argc==2,"LIVE_PROBE_ARGUMENTS");
        mona2::Options o;
        o.algo="lyra2v2";
        o.credentials_stdin=true;
        o.json=true;
        o.device=int(mona2::decimal(argv[1],INT32_MAX));
        return mona2::run_application(o,std::chrono::seconds(1800));
    }catch(const mona2::Error&e){
        std::cerr<<"{\"event\":\"LIVE_STOP\",\"code\":"<<mona2::json_quote(e.what())<<"}\n";return 2;
    }catch(...){std::cerr<<"LIVE_UNEXPECTED_REDACTED\n";return 2;}
}
