#include "package_energy.hpp"
#pragma once
#include "settings.hpp"
#include "intel_telemetry.hpp"
#include <dxgi1_2.h>
#include <pdh.h>
#include <powerbase.h>
#include <pdhmsg.h>
#include <wrl/client.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <map>
#include <vector>
#include <deque>
#include <optional>
#include <cstddef>
#include <sstream>
namespace arc {
using Clock=std::chrono::steady_clock;
struct Reading { std::optional<double> value; std::wstring reason=L"等待首个样本"; ULONGLONG time{},goodTime{}; uint64_t reads{}; std::deque<double> history; };
struct Snapshot { std::array<Reading,metricCount> metrics; uint64_t wakes{},engineQueries{},memoryQueries{}; double lastWorkMs{},maxWorkMs{}; };
struct CounterItem { std::wstring name; double value; };
inline std::wstring luidToken(LUID id){wchar_t token[64];swprintf_s(token,L"luid_0x%08X_0x%08X",static_cast<unsigned>(id.HighPart),static_cast<unsigned>(id.LowPart));return token;}
inline size_t luidPosition(const std::wstring& name,const std::wstring& token){
 for(size_t pos=0;(pos=name.find(L"luid_",pos))!=std::wstring::npos;++pos)if(name.size()-pos>=token.size()&&_wcsnicmp(name.c_str()+pos,token.c_str(),token.size())==0&&(pos==0||name[pos-1]==L'_')&&(pos+token.size()==name.size()||name[pos+token.size()]==L'_'))return pos;return std::wstring::npos;
}
inline bool belongs(const std::wstring& name,const std::wstring& token){
 return luidPosition(name,token)!=std::wstring::npos;
}
// Sum processes on the same physical engine, then report the busiest matching engine.
inline std::optional<double> engineUsage(const std::vector<CounterItem>& items,const std::wstring& luid,const std::wstring& type){
 std::map<std::wstring,double> engines;
 for(auto& item:items){if(!belongs(item.name,luid))continue;const auto p=item.name.find(L"_engtype_");if(p==std::wstring::npos||item.name.substr(p+9)!=type||!std::isfinite(item.value)||item.value<0)continue;
  const auto key=item.name.substr(luidPosition(item.name,luid));engines[key]+=item.value;
 }
 if(engines.empty())return {};double result=0;for(auto& [key,value]:engines){(void)key;result=std::max(result,value);}return std::clamp(result,0.0,100.0);
}
inline std::optional<double> memoryUsage(const std::vector<CounterItem>& items,const std::wstring& luid){
 bool found=false;double bytes=0;for(auto& item:items)if(belongs(item.name,luid)&&std::isfinite(item.value)&&item.value>=0){found=true;bytes+=item.value;}if(!found)return {};return bytes/(1024.0*1024.0);
}
class PdhQuery {
 PDH_HQUERY query{};std::vector<PDH_HCOUNTER> counters;std::wstring error;
public:
 explicit PdhQuery(std::initializer_list<const wchar_t*> paths){
  auto status=PdhOpenQueryW(nullptr,0,&query);if(status!=ERROR_SUCCESS){query=nullptr;error=L"PDH query unavailable";return;}
  for(auto path:paths){if(!path){counters.push_back(nullptr);continue;}PDH_HCOUNTER counter{};status=PdhAddEnglishCounterW(query,path,0,&counter);counters.push_back(status==ERROR_SUCCESS?counter:nullptr);if(status!=ERROR_SUCCESS){std::wostringstream s;s<<L"PDH 0x"<<std::hex<<static_cast<unsigned long>(status);error=s.str();}}
  PdhCollectQueryData(query);
 }
 ~PdhQuery(){if(query)PdhCloseQuery(query);}
 PdhQuery(const PdhQuery&)=delete;PdhQuery& operator=(const PdhQuery&)=delete;
 bool collect(){return query&&PdhCollectQueryData(query)==ERROR_SUCCESS;}
 std::vector<CounterItem> values(size_t which){
  std::vector<CounterItem> out;if(which>=counters.size()||!counters[which])return out;
  DWORD bytes=0,count=0;auto status=PdhGetFormattedCounterArrayW(counters[which],PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,&bytes,&count,nullptr);
  if(status!=PDH_MORE_DATA||!bytes||bytes>16*1024*1024)return out;
  // max_align_t makes the PDH structs correctly aligned. Dynamic instance churn is retried.
  for(int retry=0;retry<3;++retry){std::vector<std::max_align_t> buffer((bytes+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t));DWORD capacity=static_cast<DWORD>(buffer.size()*sizeof(std::max_align_t));
   auto items=reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());status=PdhGetFormattedCounterArrayW(counters[which],PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,&capacity,&count,items);
   if(status==ERROR_SUCCESS){for(DWORD i=0;i<count;++i)if(items[i].szName&&(items[i].FmtValue.CStatus==PDH_CSTATUS_VALID_DATA||items[i].FmtValue.CStatus==PDH_CSTATUS_NEW_DATA))out.push_back({items[i].szName,items[i].FmtValue.doubleValue});break;}
   if(status!=PDH_MORE_DATA)break;bytes=0;count=0;PdhGetFormattedCounterArrayW(counters[which],PDH_FMT_DOUBLE|PDH_FMT_NOCAP100,&bytes,&count,nullptr);if(bytes==0||bytes>16*1024*1024)break;
  }return out;
 }
 const std::wstring& problem()const{return error;}
};
inline DWORD nominalProcessorMhz(){struct ProcessorPowerData{ULONG number,maxMhz,currentMhz,limit,maxIdle,currentIdle;};static_assert(sizeof(ProcessorPowerData)==24);auto count=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);if(!count||count>64)return 0;std::vector<ProcessorPowerData> values(count);if(CallNtPowerInformation(ProcessorInformation,nullptr,0,values.data(),static_cast<ULONG>(values.size()*sizeof(values[0])))!=0)return 0;DWORD nominal=0;for(auto& cpu:values)if(cpu.maxMhz<=10000)nominal=std::max(nominal,cpu.maxMhz);return nominal;}
class Telemetry {
 Settings config;std::wstring luid;LUID adapterId{};std::function<void()> notify;
 std::mutex waitMutex,dataMutex;std::condition_variable cv;bool stopped=false;std::thread worker;Snapshot data;
 static ULONGLONG ticks(FILETIME t){return (ULONGLONG(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
 void run(){
  std::unique_ptr<PdhQuery> engines,memory,cpuFrequency;DWORD baseMhz=0;std::unique_ptr<IntelTelemetry> intel;std::unique_ptr<PackageEnergy> packageEnergy;
  const auto enabled=[&](Metric m){return config.metrics[index(m)].enabled;};
  if(enabled(Metric::Render)||enabled(Metric::Compute)||enabled(Metric::Media)||enabled(Metric::Copy)||enabled(Metric::Decode)||enabled(Metric::Processing)||enabled(Metric::GpuUsage))engines=std::make_unique<PdhQuery>(std::initializer_list<const wchar_t*>{L"\\GPU Engine(*)\\Utilization Percentage"});
  if(enabled(Metric::Shared)||enabled(Metric::Dedicated))memory=std::make_unique<PdhQuery>(std::initializer_list<const wchar_t*>{enabled(Metric::Shared)?L"\\GPU Adapter Memory(*)\\Shared Usage":nullptr,enabled(Metric::Dedicated)?L"\\GPU Adapter Memory(*)\\Dedicated Usage":nullptr});
  if(enabled(Metric::CpuPower))packageEnergy=std::make_unique<PackageEnergy>();
  if(enabled(Metric::CpuFrequency)){baseMhz=nominalProcessorMhz();cpuFrequency=std::make_unique<PdhQuery>(std::initializer_list<const wchar_t*>{L"\\Processor Information(_Total)\\% Processor Performance",L"\\Processor Information(_Total)\\Processor Frequency"});}
  if(enabled(Metric::GpuFrequency)||enabled(Metric::GpuPower))intel=std::make_unique<IntelTelemetry>(adapterId);
  Snapshot local;{std::lock_guard lock(dataMutex);local=data;}local.wakes=local.engineQueries=local.memoryQueries=0;local.lastWorkMs=local.maxWorkMs=0;for(size_t i=0;i<metricCount;++i)if(!config.metrics[i].enabled){local.metrics[i]=Reading{};local.metrics[i].reason=L"已关闭";}
  ULONGLONG idlePrev=0,totalPrev=0,processPrev=0;bool processInitialized=false;auto processTime=Clock::now();
  std::array<Clock::time_point,metricCount> due;due.fill(Clock::now());
  const auto publish=[&](size_t i,std::optional<double> value,const std::wstring& reason){auto& r=local.metrics[i];r.time=GetTickCount64();++r.reads;
   if(value){r.value=value;r.goodTime=r.time;r.reason=L"";r.history.push_back(*value);if(r.history.size()>config.metrics[i].history)r.history.pop_front();}
   else{r.reason=reason;if(!r.goodTime||r.time-r.goodTime>std::max(5000u,config.metrics[i].period*3))r.value.reset();}
  };
  for(;;){
   {std::lock_guard lock(waitMutex);if(stopped)break;}
   auto begin=Clock::now();++local.wakes;bool gpuDue=false,memDue=false,intelDue=false,any=false;
   for(size_t i=0;i<metricCount;++i)if(config.metrics[i].enabled&&begin>=due[i]){any=true;if(i>=index(Metric::Render)&&(i<=index(Metric::Media)||(i>=index(Metric::Copy)&&i<=index(Metric::Processing))||i==index(Metric::GpuUsage)))gpuDue=true;if(i==index(Metric::Shared)||i==index(Metric::Dedicated))memDue=true;if(i==index(Metric::GpuPower)||i==index(Metric::GpuFrequency))intelDue=true;}
   std::vector<CounterItem> engineValues,sharedValues,dedicatedValues;
   if(gpuDue&&engines){++local.engineQueries;if(engines->collect())engineValues=engines->values(0);}
   if(memDue&&memory){++local.memoryQueries;if(memory->collect()){if(enabled(Metric::Shared))sharedValues=memory->values(0);if(enabled(Metric::Dedicated))dedicatedValues=memory->values(1);}}
   IntelTelemetry::Values intelValues;if(intelDue&&intel)intelValues=intel->read(enabled(Metric::GpuPower));
   for(size_t i=0;i<metricCount;++i){if(!config.metrics[i].enabled||begin<due[i])continue;due[i]=begin+std::chrono::milliseconds(config.metrics[i].period);auto m=static_cast<Metric>(i);
    if(m==Metric::Cpu){FILETIME idle,kernel,user;if(GetSystemTimes(&idle,&kernel,&user)){auto it=ticks(idle),total=ticks(kernel)+ticks(user);std::optional<double> v;if(totalPrev&&total>totalPrev)v=std::clamp(100.0*(1.0-double(it-idlePrev)/double(total-totalPrev)),0.0,100.0);idlePrev=it;totalPrev=total;publish(i,v,L"正在建立 CPU 基线");}else publish(i,{},L"GetSystemTimes 失败");}
    else if(m==Metric::SelfCpu){FILETIME created,exit,kernel,user;if(GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user)){auto total=ticks(kernel)+ticks(user);double seconds=std::chrono::duration<double>(begin-processTime).count();std::optional<double> v;if(processInitialized&&seconds>0)v=std::clamp(double(total-processPrev)/1e7/seconds/GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)*100.0,0.0,100.0);processPrev=total;processTime=begin;processInitialized=true;publish(i,v,L"正在建立进程 CPU 基线");}else publish(i,{},L"GetProcessTimes 失败");}
    else if(m==Metric::Ram){MEMORYSTATUSEX ram{sizeof(ram)};if(GlobalMemoryStatusEx(&ram))publish(i,double(ram.dwMemoryLoad),L"");else publish(i,{},L"GlobalMemoryStatusEx 失败");}
    else if(i<=index(Metric::Media)||(i>=index(Metric::Copy)&&i<=index(Metric::Processing))){auto type=m==Metric::Render?L"3D":m==Metric::Compute?L"Compute":m==Metric::Copy?L"Copy":m==Metric::Decode?L"VideoDecode":m==Metric::Processing?L"VideoProcessing":L"VideoEncode";publish(i,engineUsage(engineValues,luid,type),L"引擎数据暂不可用");}
    else if(m==Metric::RamUsed){MEMORYSTATUSEX ram{sizeof(ram)};if(GlobalMemoryStatusEx(&ram))publish(i,double(ram.ullTotalPhys-ram.ullAvailPhys)/(1024*1024),L"");else publish(i,{},L"内存数据暂不可用");}
    else if(m==Metric::CpuFrequency){std::optional<double> value;if(cpuFrequency&&cpuFrequency->collect()){auto values=cpuFrequency->values(0);if(baseMhz&& !values.empty()&&std::isfinite(values[0].value)&&values[0].value>=0&&values[0].value<=500)value=baseMhz*values[0].value/100;if(!value){auto raw=cpuFrequency->values(1);if(!raw.empty()&&raw[0].value>=0)value=raw[0].value;}}publish(i,value,L"CPU 频率暂不可用");}
    else if(m==Metric::GpuFrequency)publish(i,intelValues.mhz,intel?intel->problem:L"GPU 频率接口不可用");
    else if(m==Metric::GpuPower)publish(i,intelValues.watts,intel?intel->problem:L"GPU 功率接口不可用");
    else if(m==Metric::CpuPower){publish(i,packageEnergy?packageEnergy->read():std::optional<double>{},L"Windows 封装能量计数器暂不可用");}
    else if(m==Metric::GpuUsage){std::map<std::wstring,double> grouped;for(auto& item:engineValues)if(belongs(item.name,luid)&&std::isfinite(item.value)&&item.value>=0)grouped[item.name.substr(luidPosition(item.name,luid))]+=item.value;std::optional<double> value;for(auto& [key,v]:grouped){(void)key;value=std::max(value.value_or(0),std::clamp(v,0.0,100.0));}publish(i,value,L"GPU 引擎数据暂不可用");}
    else publish(i,memoryUsage(m==Metric::Shared?sharedValues:dedicatedValues,luid),L"内存数据暂不可用");
   }
   local.lastWorkMs=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();local.maxWorkMs=std::max(local.maxWorkMs,local.lastWorkMs);
   if(any){{std::lock_guard lock(dataMutex);data=local;}if(notify)notify();}
   auto next=Clock::time_point::max();for(size_t i=0;i<metricCount;++i)if(config.metrics[i].enabled)next=std::min(next,due[i]);
   std::unique_lock lock(waitMutex);cv.wait_until(lock,next,[&]{return stopped;});if(stopped)break;
  }
 }
public:
 Telemetry(Settings settings,LUID id,std::function<void()> callback,Snapshot initial={}):config(std::move(settings)),luid(luidToken(id)),adapterId(id),notify(std::move(callback)),data(std::move(initial)){worker=std::thread([this]{run();});}
 ~Telemetry(){{std::lock_guard lock(waitMutex);stopped=true;}cv.notify_one();if(worker.joinable())worker.join();}
 Snapshot snapshot(){std::lock_guard lock(dataMutex);return data;}
};
}

