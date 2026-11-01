#pragma once
#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <cmath>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace arc {
inline std::wstring cpuName=L"Intel® Core™ Ultra X7 Processor 358H";
inline std::wstring gpuName=L"Intel® Arc™ B390 GPU";
enum class Metric : size_t { Cpu, SelfCpu, Ram, Render, Compute, Media, Shared, Dedicated, Copy, Decode, Processing, RamUsed, CpuFrequency, GpuFrequency, CpuPower, GpuPower, GpuUsage, Count };
constexpr size_t metricCount=static_cast<size_t>(Metric::Count);
inline size_t index(Metric m){return static_cast<size_t>(m);}
inline const std::array<const wchar_t*,metricCount> names={L"系统 CPU",L"本进程 CPU",L"系统内存",L"GPU 渲染",L"GPU 计算",L"GPU 视频编码",L"GPU 共享内存",L"GPU 专用内存",L"GPU 复制",L"GPU 视频解码",L"GPU 视频处理",L"系统内存用量",L"CPU 当前频率",L"GPU 当前频率",L"CPU 封装功率",L"GPU 功率",L"GPU 总占用"};
inline const std::array<const wchar_t*,metricCount> sources={L"GetSystemTimes · 整机",L"GetProcessTimes · 整机归一",L"GlobalMemoryStatusEx · 整机",L"Intel GPU · 3D",L"Intel GPU · Compute",L"Intel GPU · VideoEncode",L"Intel GPU · Shared Usage",L"Intel GPU · Dedicated Usage",L"Intel GPU · Copy",L"Intel GPU · VideoDecode",L"Intel GPU · VideoProcessing",L"GlobalMemoryStatusEx · 已用 MiB",L"Processor Performance × Windows 标称 MHz · 估算",L"Intel Graphics Control Library · MHz",L"Windows EMI · RAPL Package · 能量差分",L"Intel Graphics Control Library · GPU Energy delta",L"目标适配器最忙物理引擎"};
struct MetricConfig { bool enabled=true, visible=true, graph=false; unsigned period=1000, decimals=1,history=120; float size=0,labelSize=12; unsigned color=0,fontColor=0,labelColor=0,order=0; std::wstring label,font; };
struct MonitorAppearance { float opacity=.82f, fontSize=20, radius=12, scale=1; int x=24,y=24; unsigned theme=0,columns=1,style=1,detail=static_cast<unsigned>(metricCount); bool graph=true, background=true; unsigned color=0,textColor=0,graphColor=0x9fafff; bool hardware=true,captureTitle=true,cpuClock=true,gpuClock=true; float spacing=8,curveWidth=426; };
struct ReplaySettings {unsigned seconds=60,fps=60,bitrate=20,width=1920,height=1080,cacheMiB=256,hotModifiers=MOD_ALT,hotKey=VK_F10,toggleModifiers=MOD_ALT,toggleKey=VK_F9;bool diskCache=false,windowCapture=false,enabled=false;std::wstring saveDirectory,cacheDirectory,windowTitle,windowExe;bool operator==(const ReplaySettings&)const=default;};
struct Settings {
 ReplaySettings replay;
 bool overlayOpen=false,silentStart=false,autoStart=false,alwaysAdmin=false;unsigned hotModifiers=MOD_CONTROL|MOD_ALT,hotKey='O';std::wstring manualTitle,manualExe; bool monitoring=false, light=false; unsigned theme=2,notificationPosition=0; float uiScale=1,uiGlass=.65f; std::wstring font=L"Segoe UI"; bool gameEnabled=true,autoGame=true;std::array<bool,4> gameVisible{true,true,true,true};unsigned frameWindow=10;
 std::array<MetricConfig,metricCount> metrics;
 MonitorAppearance monitor;
 std::array<MetricConfig,4> gameMetrics;
 Settings(){
  for(size_t i=0;i<metricCount;++i){metrics[i].label=names[i];metrics[i].order=static_cast<unsigned>(i);if(i>=index(Metric::Render)){metrics[i].period=2000;metrics[i].enabled=false;metrics[i].visible=false;} }
  metrics[index(Metric::Cpu)].graph=true;metrics[index(Metric::CpuFrequency)].enabled=true;metrics[index(Metric::GpuFrequency)].enabled=true;
  const wchar_t* labels[]={L"平均 FPS",L"1% Low",L"平均帧时间",L"P99 帧时间"};for(size_t i=0;i<4;++i){gameMetrics[i].label=labels[i];gameMetrics[i].graph=i==0;}
 }
 MonitorAppearance& active(){return monitor;} const MonitorAppearance& active()const{return monitor;}
 void sanitize(){
  auto& r=replay;r.seconds=std::clamp(r.seconds,15u,1800u);r.fps=std::clamp(r.fps,15u,120u);r.bitrate=std::clamp(r.bitrate,1u,150u);r.cacheMiB=std::clamp(r.cacheMiB,64u,r.diskCache?8192u:2048u);r.width=r.width?std::clamp(r.width,320u,7680u)&~1u:0;r.height=r.height?std::clamp(r.height,240u,4320u)&~1u:0;r.hotModifiers&=(MOD_CONTROL|MOD_ALT|MOD_SHIFT);if(!r.hotModifiers||r.hotModifiers==(MOD_CONTROL|MOD_ALT|MOD_SHIFT)||!r.hotKey||r.hotKey>254){r.hotModifiers=MOD_ALT;r.hotKey=VK_F10;}r.toggleModifiers&=(MOD_CONTROL|MOD_ALT|MOD_SHIFT);if(!r.toggleModifiers||r.toggleModifiers==(MOD_CONTROL|MOD_ALT|MOD_SHIFT)||!r.toggleKey||r.toggleKey>254){r.toggleModifiers=MOD_ALT;r.toggleKey=VK_F9;}r.saveDirectory.resize(std::min(r.saveDirectory.size(),size_t(32767)));r.cacheDirectory.resize(std::min(r.cacheDirectory.size(),size_t(32767)));r.windowTitle.resize(std::min(r.windowTitle.size(),size_t(256)));r.windowExe.resize(std::min(r.windowExe.size(),size_t(32767)));
  hotModifiers&=(MOD_CONTROL|MOD_ALT|MOD_SHIFT);if(!hotModifiers||hotModifiers==(MOD_CONTROL|MOD_ALT|MOD_SHIFT)||!hotKey||hotKey>254){hotModifiers=MOD_CONTROL|MOD_ALT;hotKey='O';}manualTitle.resize(std::min(manualTitle.size(),size_t(256)));manualExe.resize(std::min(manualExe.size(),size_t(32767)));
  if(font.empty()||font.size()>31)font=L"Segoe UI";
  if(!std::isfinite(uiScale))uiScale=1;uiScale=std::clamp(uiScale,.75f,1.5f);
  for(auto& m:metrics){m.period=std::clamp(m.period,250u,10000u);m.decimals=std::min(m.decimals,3u);if(m.label.empty())m.label=L"指标";m.label.resize(std::min(m.label.size(),size_t(48)));}
  if(!std::isfinite(uiGlass))uiGlass=.65f;uiGlass=std::clamp(uiGlass,0.f,1.f);notificationPosition=std::min(notificationPosition,5u);theme=std::min(theme,2u);monitor.theme=std::min(monitor.theme,3u);frameWindow=std::clamp(frameWindow,2u,120u);for(auto& m:metrics){m.history=std::clamp(m.history,10u,600u);if(!std::isfinite(m.size))m.size=0;m.size=std::clamp(m.size,0.f,40.f);m.font.resize(std::min(m.font.size(),size_t(31)));}
  std::array<size_t,metricCount> order{};for(size_t i=0;i<metricCount;++i)order[i]=i;std::stable_sort(order.begin(),order.end(),[this](size_t a,size_t b){return metrics[a].order<metrics[b].order;});for(size_t i=0;i<metricCount;++i)metrics[order[i]].order=static_cast<unsigned>(i);
  {auto& p=monitor;if(!std::isfinite(p.opacity))p.opacity=.82f;if(!std::isfinite(p.fontSize))p.fontSize=20;if(!std::isfinite(p.radius))p.radius=18;
   if(!std::isfinite(p.scale))p.scale=1;p.scale=std::clamp(p.scale,.5f,2.f);
   p.opacity=std::clamp(p.opacity,0.f,1.f);p.fontSize=std::clamp(p.fontSize,12.f,32.f);p.radius=std::clamp(p.radius,0.f,32.f);p.columns=std::clamp(p.columns,1u,4u);p.style=1;if(!std::isfinite(p.curveWidth))p.curveWidth=426;p.curveWidth=std::clamp(p.curveWidth,180.f,900.f);p.detail=std::min(p.detail,static_cast<unsigned>(metricCount+3));
   p.x=std::clamp(p.x,-16000,16000);p.y=std::clamp(p.y,-16000,16000);if(!std::isfinite(p.spacing))p.spacing=8;p.spacing=std::clamp(p.spacing,0.f,24.f);}
  const auto labels=[](MetricConfig& m){if(!std::isfinite(m.labelSize))m.labelSize=12;m.labelSize=std::clamp(m.labelSize,8.f,30.f);};for(auto& m:metrics)labels(m);for(auto& m:gameMetrics){labels(m);if(!std::isfinite(m.size))m.size=0;m.size=std::clamp(m.size,0.f,64.f);m.decimals=std::min(m.decimals,3u);m.history=std::clamp(m.history,10u,600u);m.font.resize(std::min(m.font.size(),size_t(31)));m.label.resize(std::min(m.label.size(),size_t(48)));}
 }
};
// Quoted labels permit spaces; disk encoding is UTF-16LE on Windows.
inline void writeSettings(std::wostream& out,const Settings& s){
 out<<L"ArcReadyPanel 13\n"<<s.monitoring<<L' '<<std::quoted(s.font)<<L' '<<s.theme<<L' '<<s.uiScale<<L'\n';
 for(auto& m:s.metrics)out<<m.enabled<<L' '<<m.visible<<L' '<<m.graph<<L' '<<m.period<<L' '<<m.decimals<<L' '<<std::quoted(m.label)<<L' '<<m.history<<L' '<<m.size<<L' '<<m.color<<L' '<<m.fontColor<<L' '<<m.order<<L' '<<std::quoted(m.font)<<L'\n';
 auto& p=s.monitor;out<<p.opacity<<L' '<<p.fontSize<<L' '<<p.radius<<L' '<<p.x<<L' '<<p.y<<L' '<<p.columns<<L' '<<p.graph<<L' '<<p.scale<<L' '<<p.background<<L' '<<p.color<<L' '<<p.textColor<<L' '<<p.graphColor<<L' '<<p.hardware<<L' '<<p.spacing<<L'\n';
 out<<s.gameEnabled<<L' '<<s.frameWindow<<L' '<<s.autoGame;for(auto b:s.gameVisible)out<<L' '<<b;out<<L'\n';
 out<<p.style<<L' '<<p.detail<<L' '<<p.captureTitle<<L'\n';for(auto& m:s.metrics)out<<m.labelSize<<L' '<<m.labelColor<<L'\n';
 for(auto& m:s.gameMetrics)out<<m.graph<<L' '<<m.decimals<<L' '<<m.history<<L' '<<m.size<<L' '<<m.labelSize<<L' '<<m.color<<L' '<<m.fontColor<<L' '<<m.labelColor<<L' '<<std::quoted(m.label)<<L' '<<std::quoted(m.font)<<L'\n';
 out<<p.cpuClock<<L' '<<p.gpuClock<<L' '<<s.overlayOpen<<L' '<<s.silentStart<<L' '<<s.autoStart<<L' '<<s.hotModifiers<<L' '<<s.hotKey<<L' '<<std::quoted(s.manualTitle)<<L' '<<std::quoted(s.manualExe)<<L'\n';out<<p.curveWidth<<L'\n';out<<s.alwaysAdmin<<L'\n';
 const auto& r=s.replay;out<<r.seconds<<L' '<<r.fps<<L' '<<r.bitrate<<L' '<<r.width<<L' '<<r.height<<L' '<<r.cacheMiB<<L' '<<r.diskCache<<L' '<<r.windowCapture<<L' '<<r.hotModifiers<<L' '<<r.hotKey<<L' '<<std::quoted(r.saveDirectory)<<L' '<<std::quoted(r.cacheDirectory)<<L' '<<std::quoted(r.windowTitle)<<L' '<<std::quoted(r.windowExe)<<L'\n';
 out<<r.toggleModifiers<<L' '<<r.toggleKey<<L'\n'<<r.enabled<<L'\n'<<s.monitor.theme<<L' '<<s.uiGlass<<L'\n'<<s.notificationPosition<<L'\n';
}
inline bool readSettings(std::wistream& in,Settings& s){
 Settings candidate;std::wstring header;unsigned version{};in>>header>>version;if((header!=L"ArcGlass"&&header!=L"ArcControl"&&header!=L"ArcReadyPanel")||(version<2||version>13))return false;
 if(version>=4){
  in>>candidate.monitoring>>std::quoted(candidate.font)>>candidate.theme>>candidate.uiScale;
  for(size_t i=0;i<(version==4?12:metricCount);++i){auto& m=candidate.metrics[i];in>>m.enabled>>m.visible>>m.graph>>m.period>>m.decimals>>std::quoted(m.label)>>m.history>>m.size>>m.color>>m.fontColor>>m.order>>std::quoted(m.font);}
  auto& p=candidate.monitor;in>>p.opacity>>p.fontSize>>p.radius>>p.x>>p.y>>p.columns>>p.graph>>p.scale>>p.background>>p.color>>p.textColor>>p.graphColor>>p.hardware>>p.spacing;
  in>>candidate.gameEnabled>>candidate.frameWindow>>candidate.autoGame;for(auto& b:candidate.gameVisible)in>>b;
  if(version>=5){in>>p.style>>p.detail>>p.captureTitle;for(auto& m:candidate.metrics)in>>m.labelSize>>m.labelColor;for(auto& m:candidate.gameMetrics)in>>m.graph>>m.decimals>>m.history>>m.size>>m.labelSize>>m.color>>m.fontColor>>m.labelColor>>std::quoted(m.label)>>std::quoted(m.font);}
 }else{
  unsigned selected=0;in>>candidate.monitoring>>selected>>std::quoted(candidate.font);if(version==3)in>>candidate.light>>candidate.uiScale;
  for(size_t i=0;i<8;++i){auto& m=candidate.metrics[i];in>>m.enabled>>m.visible>>m.graph>>m.period>>m.decimals>>std::quoted(m.label);}
  // Import only the active legacy appearance; presets are no longer retained.
  for(unsigned i=0;i<3;++i){MonitorAppearance p;std::wstring oldName;in>>std::quoted(oldName)>>p.opacity>>p.fontSize>>p.radius>>p.x>>p.y>>p.columns>>p.graph;if(version==3)in>>p.scale>>p.background;if(i==std::min(selected,2u))candidate.monitor=p;}
  candidate.theme=candidate.light?1:0;
 }
 if(version>=6)in>>candidate.monitor.cpuClock>>candidate.monitor.gpuClock>>candidate.overlayOpen>>candidate.silentStart>>candidate.autoStart>>candidate.hotModifiers>>candidate.hotKey>>std::quoted(candidate.manualTitle)>>std::quoted(candidate.manualExe);
 if(version>=7)in>>candidate.monitor.curveWidth;
 if(version>=8)in>>candidate.alwaysAdmin;
 if(version>=9){auto& r=candidate.replay;in>>r.seconds>>r.fps>>r.bitrate>>r.width>>r.height>>r.cacheMiB>>r.diskCache>>r.windowCapture>>r.hotModifiers>>r.hotKey>>std::quoted(r.saveDirectory)>>std::quoted(r.cacheDirectory)>>std::quoted(r.windowTitle)>>std::quoted(r.windowExe);}
 if(version>=10)in>>candidate.replay.toggleModifiers>>candidate.replay.toggleKey;
 if(version>=11)in>>candidate.replay.enabled;
 if(version>=12)in>>candidate.monitor.theme>>candidate.uiGlass;
 if(version>=13)in>>candidate.notificationPosition;
 if(version<8){candidate.gameEnabled=true;const wchar_t* legacy[]={L"GPU Render",L"GPU Compute",L"GPU Encode",L"GPU Copy",L"GPU Decode"};const size_t ids[]={3,4,5,8,9};for(size_t j=0;j<5;++j)if(candidate.metrics[ids[j]].label==legacy[j])candidate.metrics[ids[j]].label=names[ids[j]];}
 if(!in)return false;candidate.sanitize();candidate.light=candidate.theme==1;s=std::move(candidate);return true;
}
inline bool saveSettings(const std::filesystem::path& path,const Settings& s){
 // Binary UTF-16 avoids the default C locale dropping non-ASCII text.
 std::wostringstream text;writeSettings(text,s);auto payload=text.str();auto temp=path;temp+=L".tmp";
 {std::ofstream out(temp,std::ios::binary|std::ios::trunc);if(!out)return false;out.write(reinterpret_cast<const char*>(payload.data()),static_cast<std::streamsize>(payload.size()*sizeof(wchar_t)));out.flush();if(!out)return false;out.close();if(!out)return false;}
#ifdef _WIN32
 return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
#else
 std::error_code error;std::filesystem::rename(temp,path,error);return !error;
#endif
}
inline bool loadSettings(const std::filesystem::path& path,Settings& s){
 std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in)return false;auto size=in.tellg();if(size<=0||size>65536||size%sizeof(wchar_t))return false;
 std::wstring payload(static_cast<size_t>(size)/sizeof(wchar_t),L'\0');in.seekg(0);in.read(reinterpret_cast<char*>(payload.data()),size);if(!in)return false;
 std::wistringstream text(payload);return readSettings(text,s);
}
inline std::filesystem::path portableSettingsPath(const std::filesystem::path& directory){return directory/L"ArcReadyPanel.settings";}
inline bool loadPortableSettings(const std::filesystem::path& directory,const std::filesystem::path& legacy,Settings& settings){
 auto current=portableSettingsPath(directory);std::error_code ec;
 if(std::filesystem::exists(current,ec))return loadSettings(current,settings);
 if(loadSettings(directory/L"ArcGlass.settings",settings))return true;
 return loadSettings(legacy,settings);
}

}
