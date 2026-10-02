// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "base.hpp"
namespace mona2 {
    struct Options {
        bool help=false,benchmark=false,all=false,credentials_stdin=false,credentials_cli=false;
        unsigned interval=60;
        int device=0;
        std::uint32_t benchmark_batches=16;
        std::uint64_t shares_limit=0;
        std::string algo;
        Credentials user;
    };
    Options parse_cli(int argc,const char*const*argv);
    void read_credentials(Options&);
    Credentials developer_credentials();
}
