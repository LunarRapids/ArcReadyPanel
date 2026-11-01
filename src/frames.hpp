#pragma once
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <thread>
#include <mutex>
#include <map>
#include <deque>
#include <vector>
#include <optional>
#include <string>
#include "core.hpp"
namespace arc {
inline constexpr GUID dxgiProvider={0xca11c036,0x0102,0x4a2d,{0xa6,0xad,0xf0,0x3c,0xfe,0xd5,0xd3,0xc9}};
// DXGI manifest v0: analytic 42/55 = chain, flags, sync interval;
// logging 178 = chain, sync interval, flags (Windows 11 build 26340).
// This measures successful application Presents, not displayed/ generated frames.
class GameFrames {
 friend struct FrameTestHarness;
 struct Pending { uint64_t chain{}; LONGLONG time{}; USHORT start{}; };
 struct Stream { bool discontinuity=false; LONGLONG last{};std::deque<std::pair<LONGLONG,double>> intervals; };
 TRACEHANDLE session{},trace=INVALID_PROCESSTRACE_HANDLE;std::thread worker;HANDLE process{};
 std::vector<unsigned char> storage;std::wstring name;DWORD pid;unsigned seconds;
 std::mutex mutex;std::map<DWORD,std::vector<Pending>> pending;std::map<uint64_t,Stream> streams;LONGLONG frequency{},arrival{};uint64_t delivered{};
 static void WINAPI event(EVENT_RECORD* record){static_cast<GameFrames*>(record->UserContext)->consume(*record);}
 void consume(const EVENT_RECORD& r){
  if(r.EventHeader.ProcessId!=pid||r.EventHeader.EventDescriptor.Version!=0||!IsEqualGUID(r.EventHeader.ProviderId,dxgiProvider))return;
  const auto id=r.EventHeader.EventDescriptor.Id;std::lock_guard lock(mutex);
  if(id==42||id==55||id==178){if(r.UserDataLength<16)return;uint64_t chain;DWORD flags;memcpy(&chain,r.UserData,8);memcpy(&flags,static_cast<const char*>(r.UserData)+(id==178?12:8),4);if(pending.size()>256)pending.clear();auto& stack=pending[r.EventHeader.ThreadId];if(stack.size()>8||!stack.empty()&&stack.back().start==id){for(auto& [key,stream]:streams){(void)key;stream.discontinuity=true;}stack.clear();}stack.push_back({flags&1?0:chain,r.EventHeader.TimeStamp.QuadPart,id});}
  else if(id==43||id==56||id==179){auto it=pending.find(r.EventHeader.ThreadId);if(it==pending.end()||it->second.empty()||id!=it->second.back().start+1)return;auto p=it->second.back();it->second.pop_back();if(!it->second.empty())return;pending.erase(it);if(!p.chain||r.UserDataLength<4)return;HRESULT result;memcpy(&result,r.UserData,4);if(result!=S_OK)return;
   LARGE_INTEGER received;QueryPerformanceCounter(&received);arrival=received.QuadPart;++delivered;
   auto& s=streams[p.chain];if(s.discontinuity){s.last=0;s.discontinuity=false;}if(s.last&&p.time>s.last){double ms=1000.0*double(p.time-s.last)/double(frequency);if(ms<5000)s.intervals.push_back({p.time,ms});else s.intervals.clear();}s.last=p.time;
   while(!s.intervals.empty()&&(p.time-s.intervals.front().first>frequency*seconds||s.intervals.size()>20000))s.intervals.pop_front();
   if(streams.size()>32){auto oldest=std::min_element(streams.begin(),streams.end(),[](auto& a,auto& b){return a.second.last<b.second.last;});streams.erase(oldest);}
  }
 }
 EVENT_TRACE_PROPERTIES* properties(){return reinterpret_cast<EVENT_TRACE_PROPERTIES*>(storage.data());}
 GameFrames(DWORD target,unsigned windowSeconds,bool):pid(target),seconds(windowSeconds){LARGE_INTEGER q;QueryPerformanceFrequency(&q);frequency=q.QuadPart;}
public:
 DWORD error=ERROR_SUCCESS;ULONGLONG lossChecked=0;ULONG lostEvents=0,lostBuffers=0;
 GameFrames(DWORD target,unsigned windowSeconds):pid(target),seconds(windowSeconds){
  process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,target);if(!process){error=GetLastError();return;}
  LARGE_INTEGER q;QueryPerformanceFrequency(&q);frequency=q.QuadPart;name=L"ArcGlass.Frames."+std::to_wstring(GetCurrentProcessId());storage.resize(sizeof(EVENT_TRACE_PROPERTIES)+(name.size()+1)*sizeof(wchar_t));auto p=properties();p->Wnode.BufferSize=static_cast<ULONG>(storage.size());p->Wnode.Guid={0xd0110157,0x3395,0x4a9d,{0xb4,0x9d,0x91,0x6d,0x1a,0x22,0x55,0x70}};p->Wnode.Guid.Data1^=GetCurrentProcessId();p->Wnode.Flags=WNODE_FLAG_TRACED_GUID;p->Wnode.ClientContext=1;p->LogFileMode=EVENT_TRACE_REAL_TIME_MODE;p->BufferSize=64;p->MinimumBuffers=8;p->MaximumBuffers=64;p->FlushTimer=1;p->LoggerNameOffset=sizeof(EVENT_TRACE_PROPERTIES);
  error=StartTraceW(&session,name.c_str(),p);if(error)return;
  EVENT_TRACE_LOGFILEW log{};log.LoggerName=name.data();log.ProcessTraceMode=PROCESS_TRACE_MODE_REAL_TIME|PROCESS_TRACE_MODE_EVENT_RECORD|PROCESS_TRACE_MODE_RAW_TIMESTAMP;log.EventRecordCallback=event;log.Context=this;trace=OpenTraceW(&log);if(trace==INVALID_PROCESSTRACE_HANDLE){error=GetLastError();return;}worker=std::thread([this]{ProcessTrace(&trace,1,nullptr,nullptr);});
  struct EventIds {BOOLEAN FilterIn=TRUE;UCHAR Reserved=0;USHORT Count=6;USHORT Events[6]={42,43,55,56,178,179};} ids;
  EVENT_FILTER_DESCRIPTOR filter{};filter.Ptr=reinterpret_cast<ULONGLONG>(&ids);filter.Size=sizeof(ids);filter.Type=EVENT_FILTER_TYPE_EVENT_ID;ENABLE_TRACE_PARAMETERS params{};params.Version=ENABLE_TRACE_PARAMETERS_VERSION_2;params.EnableProperty=EVENT_ENABLE_PROPERTY_IGNORE_KEYWORD_0;params.SourceId=p->Wnode.Guid;EVENT_FILTER_DESCRIPTOR filters[2]={filter,{reinterpret_cast<ULONGLONG>(&pid),sizeof(pid),EVENT_FILTER_TYPE_PID}};params.EnableFilterDesc=filters;params.FilterDescCount=2;
  error=EnableTraceEx2(session,&dxgiProvider,EVENT_CONTROL_CODE_ENABLE_PROVIDER,0,0xc000000000000002ULL,0,0,&params);if(error)return;
 }
 ~GameFrames(){if(session)ControlTraceW(session,name.c_str(),properties(),EVENT_TRACE_CONTROL_STOP);if(trace!=INVALID_PROCESSTRACE_HANDLE)CloseTrace(trace);if(worker.joinable())worker.join();if(process)CloseHandle(process);}
 void setWindow(unsigned value){std::lock_guard lock(mutex);seconds=std::clamp(value,2u,120u);}
 uint64_t serial(){std::lock_guard lock(mutex);return delivered;}
 bool alive()const{return !process||WaitForSingleObject(process,0)==WAIT_TIMEOUT;}
 std::optional<FrameStats> stats(){
  if(error)return {};LARGE_INTEGER now;QueryPerformanceCounter(&now);std::lock_guard lock(mutex);Stream* best=nullptr;
  // ETW batches arrive after their QPC event timestamps. Allow a bounded five-second
  // delivery grace instead of erasing good statistics at each delayed buffer flush.
  if(!arrival||now.QuadPart-arrival>frequency*5)return {};
  for(auto& [key,s]:streams){(void)key;while(!s.intervals.empty()&&s.last-s.intervals.front().first>frequency*seconds)s.intervals.pop_front();if(now.QuadPart-s.last<frequency*5&&(!best||s.intervals.size()>best->intervals.size()))best=&s;}
  if(!best)return {};std::vector<double> ms;double total=0;for(auto& p:best->intervals){ms.push_back(p.second);total+=p.second;}if(ms.size()<2)return {};if(ms.size()<100)return FrameStats{1000.0*ms.size()/total,0,0,ms.size()};return summarize(std::move(ms));
 }
 bool lost(){if(!session)return false;auto now=GetTickCount64();if(lossChecked&&now-lossChecked<5000)return false;lossChecked=now;if(ControlTraceW(session,name.c_str(),properties(),EVENT_TRACE_CONTROL_QUERY)!=ERROR_SUCCESS)return false;auto events=properties()->EventsLost,buffers=properties()->RealTimeBuffersLost;bool changed=events>lostEvents||buffers>lostBuffers;lostEvents=events;lostBuffers=buffers;if(changed){std::lock_guard lock(mutex);pending.clear();for(auto& [key,stream]:streams){(void)key;stream.discontinuity=true;}}return changed;}

};
}
