// SPDX-License-Identifier: GPL-3.0-or-later
// OWNER-APPROVED PUBLIC MINING-ONLY DESTINATION. This password is deliberately
// public, not a Web-login/withdrawal credential. No effective target is compiled.
// This is the sole production credential-literal location. Tests inject dummies.
#include "mona2/cli.hpp"
namespace mona2 {
    Credentials developer_credentials(){
        return {
            endpoint("stratum+tcp://stratum1.vippool.net:8888"),"mona2chan.monadev120","monadev"
        };
    }
}
