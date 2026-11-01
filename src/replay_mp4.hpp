#pragma once
#include "replay_buffer.hpp"
#include <array>
#include <algorithm>
#include <limits>
namespace arc {
class Av1Bits {
 const std::vector<uint8_t>& data;size_t bit=0;
public:
 explicit Av1Bits(const std::vector<uint8_t>& bytes):data(bytes){}
 uint32_t read(unsigned count){uint32_t v=0;while(count--){if(bit>=data.size()*8)throw std::runtime_error("Invalid AV1 sequence header");v=(v<<1)|((data[bit/8]>>(7-bit%8))&1);++bit;}return v;}
 void skip(unsigned count){while(count){auto n=std::min(count,32u);read(n);count-=n;}}
 void uvlc(){unsigned n=0;while(!read(1)){if(++n>=32)return;}skip(n);}
};
inline std::vector<uint8_t> av1Configuration(const std::vector<uint8_t>& bytes){
 for(size_t pos=0;pos<bytes.size();){size_t begin=pos;uint8_t header=bytes[pos++];if(header&0x80)throw std::runtime_error("Invalid AV1 OBU");unsigned type=(header>>3)&15;if(header&4){if(pos==bytes.size())break;++pos;}if(!(header&2))throw std::runtime_error("AV1 OBU size missing");uint64_t size=0;unsigned shift=0;for(;;){if(pos>=bytes.size()||shift>=56)throw std::runtime_error("Invalid AV1 OBU size");uint8_t v=bytes[pos++];size|=uint64_t(v&127)<<shift;if(!(v&128))break;shift+=7;}if(size>bytes.size()-pos)throw std::runtime_error("Truncated AV1 OBU");
  if(type==1){std::vector<uint8_t> payload(bytes.begin()+pos,bytes.begin()+pos+size);Av1Bits bits(payload);auto profile=bits.read(3);bits.read(1);bool reduced=bits.read(1)!=0;unsigned level=0,tier=0;
   if(reduced)level=bits.read(5);else{bool decoder=false;unsigned delayLength=0;if(bits.read(1)){bits.skip(64);if(bits.read(1))bits.uvlc();decoder=bits.read(1)!=0;if(decoder){delayLength=bits.read(5)+1;bits.skip(42);}}bool initial=bits.read(1)!=0;unsigned points=bits.read(5)+1;for(unsigned i=0;i<points;++i){bits.skip(12);auto currentLevel=bits.read(5);unsigned currentTier=currentLevel>7?bits.read(1):0;if(i==0){level=currentLevel;tier=currentTier;}if(decoder&&bits.read(1))bits.skip(delayLength*2+1);if(initial&&bits.read(1))bits.skip(4);}}
   // Encoder input is constrained to AV1 Main, 8-bit 4:2:0 SDR.
   if(profile!=0)throw std::runtime_error("Unexpected AV1 profile");std::vector<uint8_t> config{0x81,static_cast<uint8_t>((profile<<5)|level),static_cast<uint8_t>((tier<<7)|0x0c),0};config.insert(config.end(),bytes.begin()+begin,bytes.begin()+pos+size);return config;
  }pos+=static_cast<size_t>(size);
 }return {};
}
namespace mp4 {
using Bytes=std::vector<uint8_t>;
inline void u16(Bytes& b,uint16_t v){b.push_back(uint8_t(v>>8));b.push_back(uint8_t(v));}
inline void u32(Bytes& b,uint32_t v){for(int shift=24;shift>=0;shift-=8)b.push_back(uint8_t(v>>shift));}
inline void u64(Bytes& b,uint64_t v){u32(b,uint32_t(v>>32));u32(b,uint32_t(v));}
inline void zeros(Bytes& b,size_t n){b.insert(b.end(),n,0);}
inline void tag(Bytes& b,const char* t){b.insert(b.end(),t,t+4);}
inline Bytes box(const char* name,const Bytes& payload){if(payload.size()>UINT32_MAX-8)throw std::runtime_error("MP4 metadata too large");Bytes b;u32(b,static_cast<uint32_t>(payload.size()+8));tag(b,name);b.insert(b.end(),payload.begin(),payload.end());return b;}
inline void append(Bytes& b,const Bytes& child){b.insert(b.end(),child.begin(),child.end());}
inline void matrix(Bytes& b){for(auto v:std::array<uint32_t,9>{0x10000,0,0,0,0x10000,0,0,0,0x40000000})u32(b,v);}
inline Bytes full(uint32_t flags=0){Bytes b;u32(b,flags);return b;}
inline Bytes descriptor(uint8_t type,const Bytes& payload){Bytes b{type};b.push_back(static_cast<uint8_t>(payload.size()));append(b,payload);return b;}
inline Bytes audioTrack(const ReplaySnapshot& s,uint64_t offset,uint32_t movieDuration){
 Bytes trak,tkhd=full(7);zeros(tkhd,8);u32(tkhd,2);u32(tkhd,0);u32(tkhd,movieDuration);zeros(tkhd,12);u16(tkhd,0x100);u16(tkhd,0);matrix(tkhd);zeros(tkhd,8);append(trak,box("tkhd",tkhd));
 struct Edit{uint32_t duration;int32_t media;};std::vector<Edit> edits;uint64_t cursor=0;
 for(size_t begin=0;begin<s.audio.size();){size_t end=begin+1;while(end<s.audio.size()){uint64_t expected=s.audio[begin].clock+uint64_t(end-begin)*1024*10000000/48000;uint64_t actual=s.audio[end].clock;if(actual>expected+20000||actual+20000<expected)break;++end;}
  uint64_t rawStart=s.audioDelay+(s.audio[begin].clock-s.audio.front().clock)*9/1000;uint64_t cut=s.audioTrim*90000/48000;uint64_t at=rawStart>cut?rawStart-cut:0;
  if(at>cursor&&cursor<movieDuration){auto gap=std::min<uint64_t>(at,movieDuration)-cursor;edits.push_back({static_cast<uint32_t>(gap),-1});cursor+=gap;}
  uint64_t media=begin*1024+(begin==0?s.audioTrim:0);uint64_t available=(end-begin)*1024-(begin==0?s.audioTrim:0);uint64_t duration=std::min<uint64_t>(available*90000/48000,movieDuration-cursor);if(duration){edits.push_back({static_cast<uint32_t>(duration),static_cast<int32_t>(media)});cursor+=duration;}begin=end;if(cursor>=movieDuration)break;
 }
 if(cursor<movieDuration)edits.push_back({movieDuration-static_cast<uint32_t>(cursor),-1});Bytes edit=full();u32(edit,static_cast<uint32_t>(edits.size()));for(auto e:edits){u32(edit,e.duration);u32(edit,static_cast<uint32_t>(e.media));u16(edit,1);u16(edit,0);}append(trak,box("edts",box("elst",edit)));
 Bytes mdia,mdhd=full();zeros(mdhd,8);u32(mdhd,48000);u32(mdhd,static_cast<uint32_t>(s.audio.size()*1024));u16(mdhd,0x55c4);u16(mdhd,0);append(mdia,box("mdhd",mdhd));Bytes hdlr=full();u32(hdlr,0);tag(hdlr,"soun");zeros(hdlr,12);zeros(hdlr,1);append(mdia,box("hdlr",hdlr));Bytes minf,smhd=full();zeros(smhd,4);append(minf,box("smhd",smhd));Bytes dref=full();u32(dref,1);append(dref,box("url ",full(1)));append(minf,box("dinf",box("dref",dref)));
 Bytes stbl,entry;zeros(entry,6);u16(entry,1);zeros(entry,8);u16(entry,2);u16(entry,16);zeros(entry,4);u32(entry,48000u<<16);
 Bytes config{0x11,0x90}; // AAC-LC, 48 kHz, stereo.
 Bytes decoder{0x40,0x15,0,0x20,0};u32(decoder,128000);u32(decoder,128000);append(decoder,descriptor(5,config));Bytes es;u16(es,2);es.push_back(0);append(es,descriptor(4,decoder));append(es,descriptor(6,Bytes{2}));Bytes esds=full();append(esds,descriptor(3,es));append(entry,box("esds",esds));Bytes stsd=full();u32(stsd,1);append(stsd,box("mp4a",entry));append(stbl,box("stsd",stsd));
 Bytes stts=full();u32(stts,1);u32(stts,static_cast<uint32_t>(s.audio.size()));u32(stts,1024);append(stbl,box("stts",stts));Bytes stsc=full();u32(stsc,1);u32(stsc,1);u32(stsc,static_cast<uint32_t>(s.audio.size()));u32(stsc,1);append(stbl,box("stsc",stsc));Bytes stsz=full();u32(stsz,0);u32(stsz,static_cast<uint32_t>(s.audio.size()));for(auto& p:s.audio)u32(stsz,static_cast<uint32_t>(p.bytes->size()));append(stbl,box("stsz",stsz));Bytes co64=full();u32(co64,1);u64(co64,offset);append(stbl,box("co64",co64));append(minf,box("stbl",stbl));append(mdia,box("minf",minf));append(trak,box("mdia",mdia));return box("trak",trak);
}
inline Bytes metadata(const ReplaySnapshot& s,uint64_t offset){
 if(s.packets.empty()||!s.packets.front().key||s.av1Config.empty())throw std::runtime_error("No decodable replay available");auto count=static_cast<uint32_t>(s.packets.size());std::vector<uint32_t> deltas;uint64_t duration=0;for(size_t i=0;i<s.packets.size();++i){auto d=i+1<s.packets.size()?s.packets[i+1].pts-s.packets[i].pts:s.endPts>s.packets[i].pts?s.endPts-s.packets[i].pts:90000/std::max(s.fps,1u);if(!d||d>UINT32_MAX)throw std::runtime_error("Invalid replay timestamps");deltas.push_back(static_cast<uint32_t>(d));duration+=d;}if(duration>UINT32_MAX||s.trimStart>=duration)throw std::runtime_error("Replay duration exceeds MP4 limit");auto mediaTime=static_cast<uint32_t>(duration),time=static_cast<uint32_t>(duration-s.trimStart);
 Bytes moov,mvhd=full();zeros(mvhd,8);u32(mvhd,90000);u32(mvhd,time);u32(mvhd,0x10000);u16(mvhd,0x100);zeros(mvhd,10);matrix(mvhd);zeros(mvhd,24);u32(mvhd,s.audio.empty()?2:3);append(moov,box("mvhd",mvhd));
 Bytes trak,tkhd=full(7);zeros(tkhd,8);u32(tkhd,1);u32(tkhd,0);u32(tkhd,time);zeros(tkhd,16);matrix(tkhd);u32(tkhd,s.width<<16);u32(tkhd,s.height<<16);append(trak,box("tkhd",tkhd));
 if(s.trimStart){if(s.trimStart>INT32_MAX)throw std::runtime_error("Replay edit offset exceeds MP4 limit");Bytes edit=full();u32(edit,1);u32(edit,time);u32(edit,static_cast<uint32_t>(s.trimStart));u16(edit,1);u16(edit,0);append(trak,box("edts",box("elst",edit)));}
 Bytes mdia,mdhd=full();zeros(mdhd,8);u32(mdhd,90000);u32(mdhd,mediaTime);u16(mdhd,0x55c4);u16(mdhd,0);append(mdia,box("mdhd",mdhd));Bytes hdlr=full();u32(hdlr,0);tag(hdlr,"vide");zeros(hdlr,12);const char name[]="ArcReadyPanel AV1";hdlr.insert(hdlr.end(),name,name+sizeof(name));append(mdia,box("hdlr",hdlr));
 Bytes minf,vmhd=full(1);zeros(vmhd,8);append(minf,box("vmhd",vmhd));Bytes dref=full();u32(dref,1);append(dref,box("url ",full(1)));append(minf,box("dinf",box("dref",dref)));
 Bytes stbl,entry;zeros(entry,6);u16(entry,1);zeros(entry,16);u16(entry,static_cast<uint16_t>(s.width));u16(entry,static_cast<uint16_t>(s.height));u32(entry,0x480000);u32(entry,0x480000);u32(entry,0);u16(entry,1);zeros(entry,32);u16(entry,24);u16(entry,0xffff);append(entry,box("av1C",s.av1Config));Bytes stsd=full();u32(stsd,1);append(stsd,box("av01",entry));append(stbl,box("stsd",stsd));
 Bytes stts=full();std::vector<std::pair<uint32_t,uint32_t>> runs;for(auto d:deltas){if(!runs.empty()&&runs.back().second==d)++runs.back().first;else runs.emplace_back(1,d);}u32(stts,static_cast<uint32_t>(runs.size()));for(auto [n,d]:runs){u32(stts,n);u32(stts,d);}append(stbl,box("stts",stts));Bytes stsc=full();u32(stsc,1);u32(stsc,1);u32(stsc,count);u32(stsc,1);append(stbl,box("stsc",stsc));Bytes stsz=full();u32(stsz,0);u32(stsz,count);for(auto& p:s.packets){if(p.size>UINT32_MAX)throw std::runtime_error("MP4 packet too large");u32(stsz,static_cast<uint32_t>(p.size));}append(stbl,box("stsz",stsz));Bytes co64=full();u32(co64,1);u64(co64,offset);append(stbl,box("co64",co64));Bytes stss=full();uint32_t keys=0;for(auto& p:s.packets)keys+=p.key;u32(stss,keys);for(uint32_t i=0;i<count;++i)if(s.packets[i].key)u32(stss,i+1);append(stbl,box("stss",stss));append(minf,box("stbl",stbl));append(mdia,box("minf",minf));append(trak,box("mdia",mdia));append(moov,box("trak",trak));if(!s.audio.empty()){uint64_t audioOffset=offset;for(auto& packet:s.packets)audioOffset+=packet.size;append(moov,audioTrack(s,audioOffset,time));}return box("moov",moov);
}
}
inline void saveReplayMp4(const std::filesystem::path& destination,const ReplaySnapshot& source){
 ReplaySnapshot snapshot=source;
 if(snapshot.endPts&&snapshot.trimStart&&!snapshot.packets.empty()){
  auto start=snapshot.packets.front().pts+snapshot.trimStart;
  // Preserve decode preroll, but present the held tail image at the edit start.
  // A sample that starts before the entire edit is discarded by common players.
  if(snapshot.packets.back().pts<start){snapshot.packets.back().pts=start;snapshot.trimStart=start-snapshot.packets.front().pts;}
 }
 uint64_t bytes=0;for(auto& p:snapshot.packets)bytes+=p.size;for(auto& p:snapshot.audio)bytes+=p.bytes->size();mp4::Bytes ftyp;mp4::tag(ftyp,"isom");mp4::u32(ftyp,0x200);mp4::tag(ftyp,"isom");mp4::tag(ftyp,"iso6");mp4::tag(ftyp,"av01");auto header=mp4::box("ftyp",ftyp);auto metadata=mp4::metadata(snapshot,header.size()+16);std::filesystem::create_directories(destination.parent_path());auto temp=destination;temp+=L".tmp";
 try{std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("Cannot create replay file");auto write=[&out](const std::vector<uint8_t>& data){out.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));if(!out)throw std::runtime_error("Replay write failed (disk full or inaccessible)");};write(header);mp4::Bytes mdat;mp4::u32(mdat,1);mp4::tag(mdat,"mdat");mp4::u64(mdat,bytes+16);write(mdat);for(auto& p:snapshot.packets){if(p.memory)write(*p.memory);else write(p.read());}for(auto& p:snapshot.audio)write(*p.bytes);write(metadata);out.flush();if(!out)throw std::runtime_error("Replay flush failed");out.close();if(!out)throw std::runtime_error("Replay close failed");
#ifdef _WIN32
 if(!MoveFileExW(temp.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Replay finalization failed");
#else
 std::filesystem::rename(temp,destination);
#endif
 }catch(...){std::error_code ec;std::filesystem::remove(temp,ec);throw;}
}
}

