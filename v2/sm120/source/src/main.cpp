// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/application.hpp"
#include <iostream>
int main(int argc,char**argv){
    try{
        return mona2::run_application(mona2::parse_cli(argc,argv));
    }
    catch(const mona2::Error&e){
        std::cerr<<"{\"event\":\"ERROR\",\"code\":"<<mona2::json_quote(e.what())<<"}\n";
        return 2;
    }
    catch(const std::exception&){
        std::cerr<<"{\"event\":\"ERROR\",\"code\":\"UNEXPECTED_EXCEPTION_REDACTED\"}\n";
        return 2;
    }
}
