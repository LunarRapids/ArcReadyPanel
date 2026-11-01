#pragma once
#include <windows.h>
#include <filesystem>
#include <optional>
#include <cmath>
#include <vector>
#include "../third_party/igcl/igcl_api.h"
namespace arc {
// Driver-owned API, read-only. No bundled DLL, service, MSR driver or OC calls.
class IntelTelemetry {
 HMODULE library{};ctl_api_handle_t api{};ctl_device_adapter_handle_t adapter{};ctl_freq_handle_t frequency{};
 ctl_pfnClose_t close{};ctl_pfnPowerTelemetryGet_t power{};ctl_pfnFrequencyGetState_t freqState{};
 std::optional<double> lastEnergy,lastTime;
 template<class T>T symbol(const char* name){return reinterpret_cast<T>(GetProcAddress(library,name));}
 static std::optional<double> scalar(const ctl_oc_telemetry_item_t& item,ctl_units_t units){if(!item.bSupported||item.units!=units)return {};double value;
  if(item.type==CTL_DATA_TYPE_DOUBLE)value=item.value.datadouble;else if(item.type==CTL_DATA_TYPE_UINT64)value=static_cast<double>(item.value.datau64);else return {};if(!std::isfinite(value)||value<0)return {};return value;}
 void release(){if(api&&close)close(api);api=nullptr;adapter=nullptr;frequency=nullptr;if(library)FreeLibrary(library);library=nullptr;}
public:
 struct Values {std::optional<double> mhz,watts;};std::wstring problem=L"驱动未提供 Intel 遥测接口";
 explicit IntelTelemetry(LUID target){
  wchar_t system[32768]{};GetSystemDirectoryW(system,32768);auto root=std::filesystem::path(system)/L"DriverStore/FileRepository";std::error_code error;
  for(auto& entry:std::filesystem::directory_iterator(root,error)){if(entry.path().filename().wstring().find(L"iigd")!=0)continue;auto path=entry.path()/L"ControlLib.dll";if(!std::filesystem::exists(path,error))continue;
   library=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!library)continue;auto init=symbol<ctl_pfnInit_t>("ctlInit");close=symbol<ctl_pfnClose_t>("ctlClose");auto enumerate=symbol<ctl_pfnEnumerateDevices_t>("ctlEnumerateDevices");auto properties=symbol<ctl_pfnGetDeviceProperties_t>("ctlGetDeviceProperties");if(!init||!close||!enumerate||!properties){release();continue;}
   ctl_init_args_t args{};args.Size=sizeof(args);args.AppVersion=CTL_MAKE_VERSION(1,1);args.flags=CTL_INIT_FLAG_USE_LEVEL_ZERO;auto status=init(&args,&api);if(status!=CTL_RESULT_SUCCESS){problem=L"Intel 遥测初始化失败 "+std::to_wstring(static_cast<unsigned>(status));release();continue;}
   uint32_t count=0;if(enumerate(api,&count,nullptr)!=CTL_RESULT_SUCCESS||!count||count>32){release();continue;}std::vector<ctl_device_adapter_handle_t> devices(count);if(enumerate(api,&count,devices.data())!=CTL_RESULT_SUCCESS){release();continue;}
   for(auto device:devices){LUID id{};ctl_device_adapter_properties_t props{};props.Size=sizeof(props);props.pDeviceID=&id;props.device_id_size=sizeof(id);if(properties(device,&props)==CTL_RESULT_SUCCESS&&id.HighPart==target.HighPart&&id.LowPart==target.LowPart){adapter=device;break;}}
   if(!adapter){release();continue;}power=symbol<ctl_pfnPowerTelemetryGet_t>("ctlPowerTelemetryGet");freqState=symbol<ctl_pfnFrequencyGetState_t>("ctlFrequencyGetState");auto enums=symbol<ctl_pfnEnumFrequencyDomains_t>("ctlEnumFrequencyDomains");auto props=symbol<ctl_pfnFrequencyGetProperties_t>("ctlFrequencyGetProperties");count=0;
   if(enums&&props&&enums(adapter,&count,nullptr)==CTL_RESULT_SUCCESS&&count&&count<=32){std::vector<ctl_freq_handle_t> domains(count);if(enums(adapter,&count,domains.data())==CTL_RESULT_SUCCESS)for(auto domain:domains){ctl_freq_properties_t p{};p.Size=sizeof(p);if(props(domain,&p)==CTL_RESULT_SUCCESS&&p.type==CTL_FREQ_DOMAIN_GPU){frequency=domain;break;}}}
   problem=L"该驱动未公开此项遥测";return;
  }
 }
 ~IntelTelemetry(){release();}
 Values read(bool wantPower=true){Values out;if(!wantPower&&frequency&&freqState){ctl_freq_state_t state{};state.Size=sizeof(state);if(freqState(frequency,&state)==CTL_RESULT_SUCCESS&&std::isfinite(state.actual)&&state.actual>=0){out.mhz=state.actual;return out;}}ctl_power_telemetry_t data{};data.Size=sizeof(data);
  if(power&&adapter&&power(adapter,&data)==CTL_RESULT_SUCCESS){out.mhz=scalar(data.gpuCurrentClockFrequency,CTL_UNITS_FREQUENCY_MHZ);auto energy=scalar(data.gpuEnergyCounter,CTL_UNITS_ENERGY_JOULES),time=scalar(data.timeStamp,CTL_UNITS_TIME_SECONDS);if(wantPower&&energy&&time){if(lastEnergy&&lastTime&&*time>*lastTime&&*energy>=*lastEnergy){double watts=(*energy-*lastEnergy)/(*time-*lastTime);if(std::isfinite(watts)&&watts>=0&&watts<500)out.watts=watts;}lastEnergy=energy;lastTime=time;}}
  if(!out.mhz&&frequency&&freqState){ctl_freq_state_t state{};state.Size=sizeof(state);if(freqState(frequency,&state)==CTL_RESULT_SUCCESS&&std::isfinite(state.actual)&&state.actual>=0)out.mhz=state.actual;}return out;
 }
};
}
