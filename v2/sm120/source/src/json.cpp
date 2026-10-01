// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/json.hpp"
namespace mona2 {
    Json parse_json(std::string_view s){
        require(s.size()<=FrameMax,"JSON_FRAME_TOO_LARGE");
        int depth=0;
        bool str=false,esc=false;
        for(unsigned char c:s){
            require(c!=0,"JSON_NUL");
            if(str){
                if(esc)esc=false;
                else if(c=='\\')esc=true;
                else if(c=='"')str=false;
            }
            else if(c=='"')str=true;
            else if(c=='['||c=='{'){
                require(++depth<=32,"JSON_DEPTH");
            }
            else if(c==']'||c=='}'){
                require(--depth>=0,"JSON_BALANCE");
            }
        }
        require(depth==0&&!str,"JSON_UNTERMINATED");
        json_error_t e{};
        Json j(json_loadb(s.data(),s.size(),JSON_REJECT_DUPLICATES,&e));
        require(j&&json_is_object(j.get()),"JSON_INVALID_OBJECT");
        return j;
    }
    std::string json_string(json_t*j,std::size_t max,bool empty){
        require(json_is_string(j),"JSON_STRING_TYPE");
        const char*p=json_string_value(j);
        std::string s(p);
        require(s.size()<=max&&(empty||!s.empty()),"JSON_STRING_LENGTH");
        return s;
    }
    std::uint64_t json_uint(json_t*j,std::uint64_t max){
        require(json_is_integer(j),"JSON_INTEGER_TYPE");
        auto n=json_integer_value(j);
        require(n>=0&&std::uint64_t(n)<=max,"JSON_INTEGER_RANGE");
        return std::uint64_t(n);
    }
    json_t* member(json_t*j,const char*k){
        auto*p=json_object_get(j,k);
        require(p!=nullptr,"JSON_MISSING_FIELD");
        return p;
    }
    std::vector<std::string> Framer::feed(std::string_view s){
        require(s.size()<=FrameMax+8192,"RX_TURN_TOO_LARGE");
        std::vector<std::string> out;
        for(char c:s){
            require(c!=0,"RX_NUL");
            if(c=='\n'){
                require(buffer_.size()<=FrameMax,"RX_FRAME_TOO_LARGE");
                if(!buffer_.empty()&&buffer_.back()=='\r')buffer_.pop_back();
                if(!buffer_.empty())out.push_back(std::move(buffer_));
                buffer_.clear();
            }
            else{
                require(buffer_.size()<FrameMax,"RX_FRAME_TOO_LARGE");
                buffer_+=c;
            }
        }
        return out;
    }
}
