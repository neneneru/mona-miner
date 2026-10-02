// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "base.hpp"
extern "C" {
#include "jansson.h"
}
namespace mona2 {
    struct JsonDelete{
        void operator()(json_t*p)const{
            if(p)json_decref(p);
        }
    };
    using Json=std::unique_ptr<json_t,JsonDelete>;
    Json parse_json(std::string_view s);
    std::string json_string(json_t*j,std::size_t max=FrameMax,bool empty=true);
    std::uint64_t json_uint(json_t*j,std::uint64_t max=INT64_MAX);
    json_t* member(json_t*j,const char*key);
    class Framer {
        std::string buffer_;
        public:
        std::vector<std::string> feed(std::string_view s);
        std::size_t residual()const{
            return buffer_.size();
        }
        void reset(){
            buffer_.clear();
        }
    };
}
