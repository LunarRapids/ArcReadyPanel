#include "../src/core.hpp"
#include <cmath>
#include <iostream>
int main(){
 auto require=[](bool x){if(!x)throw std::runtime_error("test failed");};
 require(!arc::summarize({10,20}));
 std::vector<double> frames(100,10);frames[0]=20;
 auto s=arc::summarize(frames);require(s&&std::abs(s->low1-50)<0.001&&s->p99ms==10);
 frames.push_back(30);s=arc::summarize(frames);require(s&&s->p99ms==20&&s->low1==40);
 arc::PacketRing ring(10);
 ring.append({0,false,{1}});require(ring.bytes()==0);
 ring.append({0,true,std::vector<uint8_t>(4)});
 ring.append({1,false,std::vector<uint8_t>(4)});
 ring.append({2,true,std::vector<uint8_t>(4)});
 require(ring.bytes()==4&&ring.packets().front().pts==2);
 ring.append({3,true,std::vector<uint8_t>(11)});require(ring.bytes()==0);
 std::cout<<"Frame statistics and GOP eviction passed\n";
}
