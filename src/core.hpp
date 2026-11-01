#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>
#include <stdexcept>
#include <cmath>
namespace arc {
struct FrameStats { double fps{}, low1{}, p99ms{}; size_t samples{}; };
inline std::optional<FrameStats> summarize(std::vector<double> ms) {
    if(ms.size() < 100 || std::any_of(ms.begin(),ms.end(),[](double x){return !(x>0) || x>10000;})) return {};
    double total=0; for(auto x:ms) total+=x;
    std::sort(ms.begin(),ms.end(),std::greater<>());
    const auto n=std::max<size_t>(1,(ms.size()+99)/100);
    double slow=0; for(size_t i=0;i<n;++i) slow+=ms[i];
    // Nearest-rank P99 is separate from the slowest-1%-mean FPS definition.
    const auto p99Index=ms.size()-static_cast<size_t>(std::ceil(.99*static_cast<double>(ms.size())));
    return FrameStats{1000.0*ms.size()/total,1000.0*n/slow,ms[p99Index],ms.size()};
}
struct Packet { int64_t pts{}; bool key{}; std::vector<uint8_t> bytes; };
// Encoded packet storage only: no allocation until append. Eviction preserves a decodable GOP.
class PacketRing {
    size_t limit_, used_{}; std::deque<Packet> packets_;
public:
    explicit PacketRing(size_t bytes):limit_(bytes){}
    void clear(){packets_.clear();used_=0;}
    void append(Packet p){
        if(p.bytes.size()>limit_){clear();return;}
        if(packets_.empty()&&!p.key)return;
        used_+=p.bytes.size();packets_.push_back(std::move(p));
        while(used_>limit_&&!packets_.empty()){
            used_-=packets_.front().bytes.size();packets_.pop_front();
            while(!packets_.empty()&&!packets_.front().key){used_-=packets_.front().bytes.size();packets_.pop_front();}
        }
    }
    size_t bytes()const{return used_;}
    const std::deque<Packet>& packets()const{return packets_;}
};
}
