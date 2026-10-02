// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only removed-feature literals are never linked into the shipping target.
#include "mona2/accounting.hpp"
#include "mona2/cli.hpp"
#include "mona2/json.hpp"
#include <iostream>
#include <random>
#include <sstream>
#include <functional>
#include <type_traits>
using namespace mona2;
static std::uint64_t checks=0,commits=0;
void ck(bool b){++checks;require(b,"DEVFEE_ONLY_ASSERTION");}
template<class F> void fails(F f){bool caught=false;try{f();}catch(const Error&){caught=true;}ck(caught);}
void invariants(const QuotaFrame&q){
 const auto& c=q.counters();auto total=c.committed[0]+c.committed[1];
 const auto lhs=total.times(2)-c.committed[1].times(100);
 ck(lhs==c.outage_user.times(2)+c.forgiven_fee_numerator+Count(std::uint64_t(q.fee_error())));
 ck(q.fee_error()>=0&&std::uint64_t(q.fee_error())<=196*q.quantum());
}
void commit(QuotaFrame&q,const QuotaChoice&c,std::uint64_t n){q.scheduled(c.role,n);q.actual(c.role,n);q.commit(c.role,n,c.segment);++commits;invariants(q);}
int main(){try{
 static_assert(RoleCount==2);
 static_assert(std::tuple_size_v<decltype(WorkCounters{}.committed)> == 2);
 static_assert(std::tuple_size_v<decltype(NetworkCounts{}.accepted)> == 2);
 ck(index(Role::User)==0&&index(Role::DevFee)==1);
 ck(origin(Role::User)==Authority::User&&origin(Role::DevFee)==Authority::Developer);
 for(unsigned x:{2u,3u,UINT32_MAX}){fails([&]{index(Role(x));});fails([&]{origin(Role(x));});fails([&]{role_name(Role(x));});}
 for(auto batch:{1u,17u,NativeBatch,NativeBatch*2u}){
  QuotaFrame q(batch);ck(q.quantum()==64ull*batch);
  for(unsigned cycle=0;cycle<4;++cycle){
   auto c=q.choose(true,true,NonceSpace);ck(c->role==Role::User&&q.remaining()==98*q.quantum());
   for(unsigned i=0;i<98*64;++i){c=q.choose(true,true,NonceSpace);ck(c->role==Role::User&&c->count==batch);commit(q,*c,c->count);}
   c=q.choose(true,true,NonceSpace);ck(c->role==Role::DevFee&&q.remaining()==2*q.quantum());
   for(unsigned i=0;i<2*64;++i){c=q.choose(true,true,NonceSpace);ck(c->role==Role::DevFee);commit(q,*c,c->count);}
   ck(q.fee_error()==0);invariants(q);
  }
  auto v=parse_json(q.json());auto*roles=json_object_get(v.get(),"roles");ck(json_array_size(roles)==2);
  ck(json_integer_value(json_object_get(v.get(),"fee_percent"))==2);
  ck(q.json().find("donation")==std::string::npos&&q.json().find("DONATION")==std::string::npos);
 }
 for(unsigned trial=0;trial<32;++trial){
  QuotaFrame q(17);std::mt19937_64 rng(20261001u+trial);
  for(unsigned step=0;step<4000;++step){
   bool user=(rng()%31)!=0,dev=(rng()%17)!=0;
   auto c=q.choose(user,dev,1+rng()%257);
   if(!user){ck(!c);continue;}ck(bool(c));
   auto n=1+rng()%c->count;commit(q,*c,n);
   if(!dev)ck(c->role==Role::User&&c->segment==0);
  }
  q.finish();invariants(q);q.finish();invariants(q);
  QuotaFrame restart(17);auto c=restart.choose(true,true,NonceSpace);ck(c->role==Role::User&&restart.remaining()==98*restart.quantum());
 }
 {
  QuotaFrame q(1);for(unsigned i=0;i<98*64+7;++i){auto c=q.choose(true,true,NonceSpace);commit(q,*c,c->count);}
  ck(q.choose(true,true,NonceSpace)->role==Role::DevFee);q.availability(true,false);invariants(q);
  auto c=q.choose(true,false,NonceSpace);ck(c->role==Role::User);commit(q,*c,c->count);
  c=q.choose(true,true,NonceSpace);ck(c->role==Role::User&&q.remaining()==98*q.quantum());
  q.availability(false,true);ck(!q.choose(false,true,NonceSpace));invariants(q);
 }
 for(const char* option:{"--donation","--donation=0","--donation=9","--donate","--devfee","--fee","--donation-percent"}){
  const char* a[]={"test","-a","lyra2v2",option,"0"};fails([&]{parse_cli(5,a);});
 }
 const char* ok[]={"test","-a","lyra2v2","--credentials-stdin","--shares-limit","1"};ck(parse_cli(6,ok).shares_limit==1);
 {
  const char* cli[]={"test","-a","lyra2v2","-o","stratum+tcp://127.0.0.1:1","-u","fixture","-p","fixture-pass","--shares-limit","1"};
  auto o=parse_cli(11,cli);ck(o.credentials_cli&&!o.credentials_stdin&&o.user.endpoint.canonical()=="stratum+tcp://127.0.0.1:1"&&o.user.worker=="fixture"&&o.user.password=="fixture-pass"&&o.shares_limit==1);
 }
 {
  const char* cli[]={"test","-a","lyra2v2","--url","stratum+tcp://127.0.0.1:1","--user","fixture"};
  auto o=parse_cli(7,cli);ck(o.credentials_cli&&o.user.password.empty());
 }
 {
  const char* missing[]={"test","-a","lyra2v2","-o","stratum+tcp://127.0.0.1:1"};fails([&]{parse_cli(5,missing);});
  const char* mixed[]={"test","-a","lyra2v2","-o","stratum+tcp://127.0.0.1:1","-u","fixture","--credentials-stdin"};fails([&]{parse_cli(8,mixed);});
 }
 // No configuration keys beyond the three runtime credential values.
 for(auto extra:{"donation","fee","devfee","developer_endpoint"}){
  Options o;o.credentials_stdin=true;
  std::string raw="{\"endpoint\":\"stratum+tcp://127.0.0.1:1\",\"worker\":\"fixture\",\"password\":\"\",\""+std::string(extra)+"\":0}\n";
  std::istringstream in(raw);auto* old=std::cin.rdbuf(in.rdbuf());
  fails([&]{read_credentials(o);});std::cin.rdbuf(old);std::cin.clear();
 }
 std::cout<<"{\"status\":\"PASS\",\"checks\":"<<checks<<",\"commits\":"<<commits<<",\"roles\":[\"USER\",\"DEVFEE\"],\"GPU_calls\":0}\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<" checks="<<checks<<'\n';return 1;}}
