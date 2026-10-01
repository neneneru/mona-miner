#include "mona2/cli.hpp"
#include "mona2/transport.hpp"
#include <iostream>
int main(int argc,char**argv){
    bool ok=true;
    try{
        (void)mona2::parse_cli(argc,argv);
    }
    catch(...){
        ok=false;
    }
    auto&s=mona2::io_stats();
    std::cout<<"{\"valid\":"<<(ok?"true":"false")<<",\"WSA\":"<<s.wsa.load()<<",\"DNS\":"<<s.dns.load()<<",\"sockets\":"<<s.sockets.load()<<"}\n";
    return ok?0:2;
}
