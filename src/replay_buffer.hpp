#pragma once
#include <filesystem>
#include <fstream>
#include <memory>
#include <deque>
#include <vector>
#include <mutex>
#include <cstdint>
#include <stdexcept>
namespace arc {
struct ReplayCacheFile {
 std::filesystem::path path;
 ~ReplayCacheFile(){std::error_code ec;std::filesystem::remove(path,ec);}
};
struct ReplayPacket {
 uint64_t pts=0;bool key=false;size_t size=0;uint64_t offset=0;
 std::shared_ptr<const std::vector<uint8_t>> memory;
 std::shared_ptr<ReplayCacheFile> file;
 std::vector<uint8_t> read()const{
  if(memory)return *memory;if(!file)throw std::runtime_error("cache packet unavailable");std::vector<uint8_t> data(size);std::ifstream input(file->path,std::ios::binary);input.seekg(static_cast<std::streamoff>(offset));input.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(size));if(!input)throw std::runtime_error("cache read failed");return data;
 }
};
struct ReplayAudioPacket {uint64_t clock=0;std::shared_ptr<const std::vector<uint8_t>> bytes;};
struct ReplaySnapshot {unsigned width=0,height=0,fps=0;std::vector<uint8_t> av1Config;std::vector<ReplayPacket> packets;uint64_t endPts=0,trimStart=0;std::vector<ReplayAudioPacket> audio;uint64_t audioTrim=0,audioDelay=0;};
class ReplayBuffer {
 std::deque<ReplayPacket> packets;size_t used=0,limit;uint64_t duration;mutable std::mutex mutex;
 void trim(){
  while(!packets.empty()){
   auto next=packets.begin();++next;while(next!=packets.end()&&!next->key)++next;
   const bool overBytes=used>limit;
   const bool overTime=next!=packets.end()&&packets.back().pts>duration&&next->pts<=packets.back().pts-duration;
   if(!overBytes&&!overTime)break;
   if(next==packets.end()){packets.clear();used=0;break;}
   do{used-=packets.front().size;packets.pop_front();}while(!packets.empty()&&!packets.front().key);
  }
 }
public:
 ReplayBuffer(size_t bytes,unsigned seconds):limit(bytes),duration(uint64_t(seconds)*90000){}
 void append(ReplayPacket packet){std::lock_guard lock(mutex);if(packets.empty()&&!packet.key)return;if(!packets.empty()&&packet.pts<=packets.back().pts)throw std::runtime_error("non-monotonic AV1 timestamp");used+=packet.size;packets.push_back(std::move(packet));trim();}
 void configure(size_t bytes,unsigned seconds){std::lock_guard lock(mutex);limit=bytes;duration=uint64_t(seconds)*90000;trim();}
 size_t bytes()const{std::lock_guard lock(mutex);return used;}
 double seconds()const{std::lock_guard lock(mutex);return packets.size()>1?double(packets.back().pts-packets.front().pts)/90000:0;}
 std::vector<ReplayPacket> snapshot()const{std::lock_guard lock(mutex);return {packets.begin(),packets.end()};}
};
}
