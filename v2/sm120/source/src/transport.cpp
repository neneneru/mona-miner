// SPDX-License-Identifier: GPL-3.0-or-later
#include "mona2/transport.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif
namespace mona2 {
    IoStats&io_stats(){
        static IoStats s;
        return s;
    }
    StopSignal::StopSignal(){
#ifdef _WIN32
        event_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        require(event_!=nullptr,"STOP_EVENT_CREATE");
#endif
    }
    StopSignal::~StopSignal(){
#ifdef _WIN32
        if(event_)CloseHandle(event_);
#endif
    }
    void StopSignal::request()noexcept{
        stopped_.store(true);
#ifdef _WIN32
        if(event_)SetEvent(event_);
#endif
        cv_.notify_all();
    }
    bool StopSignal::wait(std::chrono::milliseconds t){
        std::unique_lock l(mu_);
        return cv_.wait_for(l,t,[this]{
            return requested();
        }
        );
    }
    NetworkRuntime::NetworkRuntime(){
#ifdef _WIN32
        WSADATA d{};
        require(WSAStartup(MAKEWORD(2,2),&d)==0,"WSA_STARTUP");
        io_stats().wsa.fetch_add(1);
#endif
        active_=true;
    }
    NetworkRuntime::~NetworkRuntime(){
#ifdef _WIN32
        if(active_)WSACleanup();
#endif
    }
    struct Transport::Impl{
#ifdef _WIN32
        SOCKET s=INVALID_SOCKET;
        WSAEVENT event=WSA_INVALID_EVENT;
#else
        int s=-1;
#endif
    };
    Transport::Transport():impl_(std::make_unique<Impl>()){}
    Transport::~Transport(){
        close();
    }
    void Transport::close()noexcept{
#ifdef _WIN32
        if(impl_->s!=INVALID_SOCKET){
            closesocket(impl_->s);
            impl_->s=INVALID_SOCKET;
        }
        if(impl_->event!=WSA_INVALID_EVENT){
            WSACloseEvent(impl_->event);
            impl_->event=WSA_INVALID_EVENT;
        }
#else
        if(impl_->s>=0){
            ::close(impl_->s);
            impl_->s=-1;
        }
#endif
    }
#ifdef _WIN32
    namespace {
        struct DnsResult {
            PADDRINFOEXW result=nullptr;
            OVERLAPPED ov{};
            HANDLE handle=nullptr;
            bool outstanding=false;
            DnsResult(){
                ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
                require(ov.hEvent!=nullptr,"DNS_EVENT");
            }
            void cancel_wait()noexcept{
                if(outstanding){
                    GetAddrInfoExCancel(&handle);
                    auto rc=WaitForSingleObject(ov.hEvent,5000);
                    if(rc!=WAIT_OBJECT_0){
                        // Never free provider-visible memory or WSACleanup while an unresolved async
                        // query still owns it. OS cancellation-contract failure is a fatal process
                        // stop, not a detached thread or unbounded join. No input strings are printed.
                        std::fputs("FATAL_DNS_CANCEL_COMPLETION\n",stderr);
                        std::fflush(stderr);
                        std::_Exit(70);
                    }
                    outstanding=false;
                }
            }
            ~DnsResult(){
                cancel_wait();
                if(result)FreeAddrInfoExW(result);
                if(ov.hEvent)CloseHandle(ov.hEvent);
            }
        };
    }
#endif
    void Transport::connect(const Endpoint&e,StopSignal&stop,bool local){
        close();
        require(!stop.requested(),"CONNECT_CANCELLED");
        if(local)require(e.host=="127.0.0.1"||e.host=="::1","LOOPBACK_ONLY");
#ifdef _WIN32
        DnsResult query;
        ADDRINFOEXW hints{};
        hints.ai_family=AF_UNSPEC;
        hints.ai_socktype=SOCK_STREAM;
        hints.ai_protocol=IPPROTO_TCP;
        hints.ai_flags=local?AI_NUMERICHOST:0;
        std::wstring host(e.host.begin(),e.host.end()),port=std::to_wstring(e.port);
        timeval budget{
            10,0
        };
        if(!local)io_stats().dns.fetch_add(1);
        int rc=GetAddrInfoExW(host.c_str(),port.c_str(),NS_DNS,nullptr,&hints,&query.result,&budget,&query.ov,nullptr,&query.handle);
        if(rc==WSA_IO_PENDING){
            query.outstanding=true;
            HANDLE waits[2]={
                query.ov.hEvent,stop.event()
            };
            auto w=WaitForMultipleObjects(2,waits,FALSE,10000);
            if(w!=WAIT_OBJECT_0){
                query.cancel_wait();
                throw Error(stop.requested()?"DNS_CANCELLED":"DNS_DEADLINE");
            }
            query.outstanding=false;
            rc=GetAddrInfoExOverlappedResult(&query.ov);
        }
        require(rc==0,"DNS_FAILED");
        const auto deadline=Clock::now()+std::chrono::seconds(10);
        for(auto*a=query.result;a&&!stop.requested()&&Clock::now()<deadline;a=a->ai_next){
            impl_->s=socket(a->ai_family,a->ai_socktype,a->ai_protocol);
            if(impl_->s==INVALID_SOCKET)continue;
            io_stats().sockets.fetch_add(1);
            impl_->event=WSACreateEvent();
            require(impl_->event!=WSA_INVALID_EVENT,"SOCKET_EVENT");
            require(WSAEventSelect(impl_->s,impl_->event,FD_READ|FD_WRITE|FD_CONNECT|FD_CLOSE)==0,"NONBLOCK_EVENT");
            BOOL keep=TRUE;
            require(setsockopt(impl_->s,SOL_SOCKET,SO_KEEPALIVE,reinterpret_cast<const char*>(&keep),sizeof(keep))==0,"KEEPALIVE");
            tcp_keepalive k{
                1,60000,10000
            };
            DWORD returned=0;
            require(WSAIoctl(impl_->s,SIO_KEEPALIVE_VALS,&k,sizeof(k),nullptr,0,&returned,nullptr,nullptr)==0,"KEEPALIVE_VALUES");
            io_stats().connects.fetch_add(1);
            rc=::connect(impl_->s,a->ai_addr,int(a->ai_addrlen));
            if(rc==0)return;
            if(WSAGetLastError()!=WSAEWOULDBLOCK){
                close();
                continue;
            }
            bool connected=false,failed=false;
            while(!stop.requested()&&Clock::now()<deadline){
                HANDLE ws[2]={
                    impl_->event,stop.event()
                };
                auto w=WaitForMultipleObjects(2,ws,FALSE,50);
                if(w==WAIT_OBJECT_0){
                    WSANETWORKEVENTS ne{};
                    require(WSAEnumNetworkEvents(impl_->s,impl_->event,&ne)==0,"CONNECT_EVENT_ENUM");
                    if(ne.lNetworkEvents&FD_CONNECT){
                        int err=0,len=sizeof(err);
                        require(getsockopt(impl_->s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&err),&len)==0,"CONNECT_SOCKET_STATUS");
                        connected=ne.iErrorCode[FD_CONNECT_BIT]==0&&err==0;
                        failed=!connected;
                        break;
                    }
                }
                else if(w==WAIT_OBJECT_0+1)break;
                else require(w==WAIT_TIMEOUT,"CONNECT_WAIT");
            }
            if(connected)return;
            close();
            if(failed)continue;
        }
        require(!stop.requested(),"CONNECT_CANCELLED");
        throw Error("CONNECT_FAILED_OR_DEADLINE");
#else
        // The POSIX transport is an authoring/loopback adapter only. External DNS and
        // mining are deliberately unavailable on this platform.
        require(local,"POSIX_EXTERNAL_NETWORK_DISABLED");
        require(e.host=="127.0.0.1","POSIX_LOOPBACK_IPV4");
        impl_->s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        require(impl_->s>=0,"SOCKET_CREATE");
        io_stats().sockets.fetch_add(1);
        int flags=fcntl(impl_->s,F_GETFL,0);
        require(flags>=0&&fcntl(impl_->s,F_SETFL,flags|O_NONBLOCK)==0,"NONBLOCK_SET");
        int keep=1;
        require(setsockopt(impl_->s,SOL_SOCKET,SO_KEEPALIVE,&keep,sizeof(keep))==0,"KEEPALIVE");
        sockaddr_in a{};
        a.sin_family=AF_INET;
        a.sin_port=htons(e.port);
        a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        io_stats().connects.fetch_add(1);
        int rc=::connect(impl_->s,reinterpret_cast<sockaddr*>(&a),sizeof(a));
        if(rc==0)return;
        require(errno==EINPROGRESS,"CONNECT_FAILED");
        auto end=Clock::now()+std::chrono::seconds(10);
        while(!stop.requested()&&Clock::now()<end){
            fd_set w,x;
            FD_ZERO(&w);
            FD_ZERO(&x);
            FD_SET(impl_->s,&w);
            FD_SET(impl_->s,&x);
            timeval t{
                0,20000
            };
            rc=select(impl_->s+1,nullptr,&w,&x,&t);
            if(rc<0&&errno==EINTR)continue;
            require(rc>=0,"CONNECT_SELECT");
            if(rc){
                int err=0;
                socklen_t n=sizeof(err);
                require(getsockopt(impl_->s,SOL_SOCKET,SO_ERROR,&err,&n)==0,"CONNECT_SOCKET_STATUS");
                require(err==0,"CONNECT_FAILED");
                return;
            }
        }
        throw Error(stop.requested()?"CONNECT_CANCELLED":"CONNECT_DEADLINE");
#endif
    }
    std::optional<std::size_t> Transport::read(std::span<char>b){
#ifdef _WIN32
        int n=recv(impl_->s,b.data(),int(b.size()),0);
        if(n==SOCKET_ERROR){
            if(WSAGetLastError()==WSAEWOULDBLOCK)return std::nullopt;
            throw Error("RECV_FAILED");
        }
#else
        auto n=recv(impl_->s,b.data(),b.size(),0);
        if(n<0){
            if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)return std::nullopt;
            throw Error("RECV_FAILED");
        }
#endif
        require(n!=0,"PEER_CLOSED");
        return std::size_t(n);
    }
    std::optional<std::size_t> Transport::write(std::string_view b){
#ifdef _WIN32
        int n=send(impl_->s,b.data(),int(std::min<std::size_t>(b.size(),65536)),0);
        if(n==SOCKET_ERROR){
            if(WSAGetLastError()==WSAEWOULDBLOCK)return std::nullopt;
            throw Error("SEND_FAILED");
        }
#else
        auto n=send(impl_->s,b.data(),std::min<std::size_t>(b.size(),65536),MSG_NOSIGNAL);
        if(n<0){
            if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)return std::nullopt;
            throw Error("SEND_FAILED");
        }
#endif
        require(n>0,"SEND_ZERO");
        return std::size_t(n);
    }
    void Transport::wait(StopSignal&stop,bool writing,std::chrono::milliseconds timeout){
#ifdef _WIN32
        (void)writing;
        HANDLE h[2]={
            impl_->event,stop.event()
        };
        auto w=WaitForMultipleObjects(2,h,FALSE,DWORD(timeout.count()));
        if(w==WAIT_OBJECT_0){
            WSANETWORKEVENTS ev{};
            require(WSAEnumNetworkEvents(impl_->s,impl_->event,&ev)==0,"SOCKET_EVENT_ENUM");
        }
        else require(w==WAIT_TIMEOUT||w==WAIT_OBJECT_0+1,"SOCKET_WAIT");
#else
        (void)stop;
        fd_set r,w;
        FD_ZERO(&r);
        FD_ZERO(&w);
        FD_SET(impl_->s,&r);
        if(writing)FD_SET(impl_->s,&w);
        timeval t{
            0,long(std::min<std::int64_t>(timeout.count(),20)*1000)
        };
        auto n=select(impl_->s+1,&r,writing?&w:nullptr,nullptr,&t);
        require(n>=0||errno==EINTR,"SOCKET_WAIT");
#endif
    }
}
