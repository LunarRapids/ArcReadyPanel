#pragma once
#include <windows.h>
#include <setupapi.h>
#include <winioctl.h>
#include <emi.h>
#include <vector>
#include <array>
#include <string>
#include <optional>
#include <cmath>
namespace arc {
inline std::optional<double> packageWatts(ULONGLONG oldEnergy,ULONGLONG oldTime,ULONGLONG energy,ULONGLONG time){if(!oldTime||time<=oldTime||energy<oldEnergy)return {};double watts=double(energy-oldEnergy)*.036/double(time-oldTime);if(!std::isfinite(watts)||watts<0||watts>500)return {};return watts;}
class PackageEnergy {
 HANDLE device=INVALID_HANDLE_VALUE;unsigned channel=0,count=1;ULONGLONG previousEnergy=0,previousTime=0;
 static constexpr GUID guid={0x45bd8344,0x7ed6,0x49cf,{0xa4,0x40,0xc2,0x76,0xc9,0x33,0xb0,0x53}};
 static bool package(const std::wstring& name){return name.find(L"RAPL_Package0_PKG")!=std::wstring::npos;}
public:
 std::wstring source;
 PackageEnergy(){auto set=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);if(set==INVALID_HANDLE_VALUE)return;SP_DEVICE_INTERFACE_DATA item{sizeof(item)};for(DWORD i=0;SetupDiEnumDeviceInterfaces(set,nullptr,&guid,i,&item);++i){DWORD size=0;SetupDiGetDeviceInterfaceDetailW(set,&item,nullptr,0,&size,nullptr);if(size<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||size>65536)continue;std::vector<BYTE> path(size);auto detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(path.data());detail->cbSize=sizeof(*detail);if(!SetupDiGetDeviceInterfaceDetailW(set,&item,detail,size,nullptr,nullptr))continue;HANDLE h=CreateFileW(detail->DevicePath,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);if(h==INVALID_HANDLE_VALUE)continue;DWORD bytes=0;EMI_VERSION version{};EMI_METADATA_SIZE metadataSize{};if(!DeviceIoControl(h,IOCTL_EMI_GET_VERSION,nullptr,0,&version,sizeof(version),&bytes,nullptr)||!DeviceIoControl(h,IOCTL_EMI_GET_METADATA_SIZE,nullptr,0,&metadataSize,sizeof(metadataSize),&bytes,nullptr)||metadataSize.MetadataSize<32||metadataSize.MetadataSize>65536){CloseHandle(h);continue;}std::vector<BYTE> metadata(metadataSize.MetadataSize);if(!DeviceIoControl(h,IOCTL_EMI_GET_METADATA,nullptr,0,metadata.data(),static_cast<DWORD>(metadata.size()),&bytes,nullptr)){CloseHandle(h);continue;}bool found=false;if(version.EmiVersion==EMI_VERSION_V1&&bytes>=offsetof(EMI_METADATA_V1,MeteredHardwareName)){auto m=reinterpret_cast<EMI_METADATA_V1*>(metadata.data());size_t offset=offsetof(EMI_METADATA_V1,MeteredHardwareName);if(m->MeasurementUnit==EmiMeasurementUnitPicowattHours&&m->MeteredHardwareNameSize>=2&&offset+m->MeteredHardwareNameSize<=bytes){source.assign(m->MeteredHardwareName,m->MeteredHardwareNameSize/2);found=package(source);}}
 else if(version.EmiVersion==EMI_VERSION_V2&&bytes>=offsetof(EMI_METADATA_V2,Channels)){auto m=reinterpret_cast<EMI_METADATA_V2*>(metadata.data());size_t offset=offsetof(EMI_METADATA_V2,Channels);count=m->ChannelCount;if(count&&count<=128)for(unsigned c=0;c<count;++c){if(offset+offsetof(EMI_CHANNEL_V2,ChannelName)>bytes)break;auto ch=reinterpret_cast<EMI_CHANNEL_V2*>(metadata.data()+offset);size_t length=offsetof(EMI_CHANNEL_V2,ChannelName)+ch->ChannelNameSize;if(ch->ChannelNameSize<2||length>bytes-offset)break;std::wstring name(ch->ChannelName,ch->ChannelNameSize/2);if(ch->MeasurementUnit==EmiMeasurementUnitPicowattHours&&package(name)){source=name;channel=c;found=true;break;}offset+=length;}}
 if(found){device=h;break;}CloseHandle(h);}SetupDiDestroyDeviceInfoList(set);}
 ~PackageEnergy(){if(device!=INVALID_HANDLE_VALUE)CloseHandle(device);}
 bool available()const{return device!=INVALID_HANDLE_VALUE;}
 std::optional<double> read(){if(!available())return {};std::array<EMI_CHANNEL_MEASUREMENT_DATA,128> values{};DWORD bytes=0;if(!DeviceIoControl(device,IOCTL_EMI_GET_MEASUREMENT,nullptr,0,values.data(),static_cast<DWORD>(count*sizeof(values[0])),&bytes,nullptr)||bytes<(channel+1)*sizeof(values[0]))return {};auto v=values[channel];auto watts=packageWatts(previousEnergy,previousTime,v.AbsoluteEnergy,v.AbsoluteTime);previousEnergy=v.AbsoluteEnergy;previousTime=v.AbsoluteTime;return watts;}
};
}
