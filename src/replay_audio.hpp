#pragma once
#include "replay_buffer.hpp"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
#include <thread>
#include <atomic>
#include <functional>
#include <condition_variable>
#include <algorithm>
namespace arc {
// Only eRender + LOOPBACK: no microphone endpoint is ever opened.
class ReplayAudio {
 class EndpointNotice final:public IMMNotificationClient {
  std::atomic<ULONG> refs{1};HANDLE event;
 public:
  explicit EndpointNotice(HANDLE handle):event(handle){}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id==__uuidof(IUnknown)||id==__uuidof(IMMNotificationClient)){*out=static_cast<IMMNotificationClient*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
  ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
  ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
  HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,ERole role,LPCWSTR)override{if(flow==eRender&&role==eConsole)SetEvent(event);return S_OK;}
  HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR,DWORD)override{return S_OK;}
  HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR)override{return S_OK;}
  HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR)override{return S_OK;}
  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,const PROPERTYKEY)override{return S_OK;}
 };
 using Ptr=Microsoft::WRL::ComPtr<IMFTransform>;
 HANDLE stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 HANDLE changeEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);bool restarting=false;
 std::jthread worker;mutable std::mutex mutex;mutable std::condition_variable ready;std::deque<ReplayAudioPacket> packets;
 std::atomic<unsigned> seconds;std::atomic<size_t> byteLimit;std::wstring failure,deviceName;std::function<void()> notify;size_t used=0;
 struct AudioError:std::runtime_error {HRESULT hr;explicit AudioError(HRESULT value):std::runtime_error([value]{char text[64]{};sprintf_s(text,"System audio error 0x%08lX",static_cast<unsigned long>(value));return std::string(text);}()),hr(value){}};
 static void check(HRESULT hr){if(FAILED(hr))throw AudioError(hr);}
 void run(){
  using Microsoft::WRL::ComPtr;bool com=false,mf=false;HANDLE audioEvent=nullptr;ComPtr<IAudioClient> client;ComPtr<IMMDeviceEnumerator> enumerator;ComPtr<IMMNotificationClient> notice;bool registered=false;
  try{check(CoInitializeEx(nullptr,COINIT_MULTITHREADED));com=true;SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);check(MFStartup(MF_VERSION,MFSTARTUP_LITE));mf=true;
   check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&enumerator)));notice.Attach(new EndpointNotice(changeEvent));check(enumerator->RegisterEndpointNotificationCallback(notice.Get()));registered=true;ComPtr<IMMDevice> device;check(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device));ComPtr<IPropertyStore> properties;if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&properties))){PROPVARIANT name{};if(SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName,&name))&&name.vt==VT_LPWSTR){std::lock_guard lock(mutex);deviceName=name.pwszVal;}PropVariantClear(&name);}if(notify)notify();check(device->Activate(__uuidof(IAudioClient),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(client.GetAddressOf())));
   WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=48000;format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=192000;
   check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK|AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,1000000,0,&format,nullptr));audioEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!audioEvent)throw std::runtime_error("Cannot create audio event");check(client->SetEventHandle(audioEvent));ComPtr<IAudioCaptureClient> capture;check(client->GetService(IID_PPV_ARGS(&capture)));
   Ptr encoder;check(CoCreateInstance(CLSID_AACMFTEncoder,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&encoder)));ComPtr<IMFMediaType> input,output;check(MFCreateMediaType(&input));check(MFCreateMediaType(&output));
   for(auto* type:{input.Get(),output.Get()}){check(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio));check(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2));check(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,48000));check(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16));}
   check(input->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM));check(input->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,4));check(input->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,192000));check(output->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_AAC));check(output->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,16000));check(output->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE,0));check(encoder->SetOutputType(0,output.Get(),0));check(encoder->SetInputType(0,input.Get(),0));check(encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0));check(encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0));MFT_OUTPUT_STREAM_INFO info{};check(encoder->GetOutputStreamInfo(0,&info));
   uint64_t base=0;bool first=true;
   auto drain=[&](){for(;;){ComPtr<IMFSample> sample;ComPtr<IMFMediaBuffer> data;if(!(info.dwFlags&MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)){check(MFCreateSample(&sample));check(MFCreateMemoryBuffer(std::max(info.cbSize,8192ul),&data));check(sample->AddBuffer(data.Get()));}MFT_OUTPUT_DATA_BUFFER out{};out.pSample=sample.Get();DWORD status{};HRESULT hr=encoder->ProcessOutput(0,1,&out,&status);if(out.pEvents)out.pEvents->Release();if(hr==MF_E_TRANSFORM_NEED_MORE_INPUT)break;check(hr);if(!sample)sample.Attach(out.pSample);check(sample->ConvertToContiguousBuffer(&data));BYTE* raw{};DWORD length{};check(data->Lock(&raw,nullptr,&length));auto bytes=std::make_shared<std::vector<uint8_t>>(raw,raw+length);data->Unlock();LONGLONG time{};check(sample->GetSampleTime(&time));if(time<0)continue;ReplayAudioPacket packet{base+static_cast<uint64_t>(time),std::move(bytes)};std::lock_guard lock(mutex);used+=packet.bytes->size();packets.push_back(std::move(packet));ready.notify_all();uint64_t keep=uint64_t(seconds.load()+3)*10000000;while(!packets.empty()&&(used>byteLimit.load()||packets.back().clock-packets.front().clock>keep)){used-=packets.front().bytes->size();packets.pop_front();}}};
   check(client->Start());HANDLE events[]{stopEvent,changeEvent,audioEvent};
   for(;;){DWORD wait=WaitForMultipleObjects(3,events,FALSE,INFINITE);if(wait==WAIT_OBJECT_0)break;if(wait==WAIT_OBJECT_0+1){restarting=true;break;}if(wait!=WAIT_OBJECT_0+2)throw std::runtime_error("Audio wait failed");
    // Batch audio into 40 ms work periods; no high-resolution timer request.
    if(WaitForSingleObject(stopEvent,40)==WAIT_OBJECT_0)break;
    UINT32 count{};check(capture->GetNextPacketSize(&count));while(count){BYTE* raw{};UINT32 frames{};DWORD flags{};UINT64 position{},clock{};check(capture->GetBuffer(&raw,&frames,&flags,&position,&clock));if(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR){LARGE_INTEGER tick{},hz{};QueryPerformanceCounter(&tick);QueryPerformanceFrequency(&hz);uint64_t now=uint64_t(tick.QuadPart)/hz.QuadPart*10000000+(uint64_t(tick.QuadPart)%hz.QuadPart)*10000000/hz.QuadPart;clock=now-uint64_t(frames)*10000000/48000;}if(first){base=clock;first=false;}ComPtr<IMFSample> sample;ComPtr<IMFMediaBuffer> data;check(MFCreateSample(&sample));check(MFCreateMemoryBuffer(frames*4,&data));BYTE* dest{};check(data->Lock(&dest,nullptr,nullptr));if(flags&AUDCLNT_BUFFERFLAGS_SILENT)memset(dest,0,frames*4);else memcpy(dest,raw,frames*4);data->Unlock();check(data->SetCurrentLength(frames*4));check(capture->ReleaseBuffer(frames));check(sample->AddBuffer(data.Get()));check(sample->SetSampleTime(static_cast<LONGLONG>(clock-base)));check(sample->SetSampleDuration(uint64_t(frames)*10000000/48000));HRESULT hr=encoder->ProcessInput(0,sample.Get(),0);if(hr==MF_E_NOTACCEPTING){drain();hr=encoder->ProcessInput(0,sample.Get(),0);}check(hr);drain();check(capture->GetNextPacketSize(&count));}
   }
   client->Stop();encoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN,0);drain();encoder.Reset();capture.Reset();device.Reset();input.Reset();output.Reset();
  }catch(const AudioError& e){if(e.hr==AUDCLNT_E_DEVICE_INVALIDATED||e.hr==HRESULT_FROM_WIN32(ERROR_NOT_FOUND)){restarting=true;HANDLE events[]{stopEvent,changeEvent};WaitForMultipleObjects(2,events,FALSE,e.hr==AUDCLNT_E_DEVICE_INVALIDATED?300:INFINITE);}else{{std::lock_guard lock(mutex);std::string text=e.what();failure.assign(text.begin(),text.end());}if(notify)notify();}}catch(const std::exception& e){{std::lock_guard lock(mutex);std::string text=e.what();failure.assign(text.begin(),text.end());}if(notify)notify();}
  if(WaitForSingleObject(changeEvent,0)==WAIT_OBJECT_0){restarting=true;std::lock_guard lock(mutex);failure.clear();}
  if(registered)enumerator->UnregisterEndpointNotificationCallback(notice.Get());notice.Reset();enumerator.Reset();if(client)client->Stop();client.Reset();if(audioEvent)CloseHandle(audioEvent);if(mf)MFShutdown();if(com)CoUninitialize();
 }
public:
 ReplayAudio(unsigned duration,std::function<void()> callback,size_t budget=8*1024*1024):seconds(duration),byteLimit(budget),notify(std::move(callback)){if(!stopEvent||!changeEvent){if(stopEvent)CloseHandle(stopEvent);if(changeEvent)CloseHandle(changeEvent);throw std::runtime_error("Cannot create audio event");}worker=std::jthread([this]{do{restarting=false;run();}while(restarting&&WaitForSingleObject(stopEvent,0)!=WAIT_OBJECT_0);});}
 ~ReplayAudio(){SetEvent(stopEvent);if(worker.joinable())worker.join();CloseHandle(stopEvent);CloseHandle(changeEvent);}
 void configure(unsigned duration,size_t budget){seconds=duration;byteLimit=budget;}
 void refreshDefaultDevice(){SetEvent(changeEvent);}
 std::wstring name()const{std::lock_guard lock(mutex);return deviceName;}
 std::wstring error()const{std::lock_guard lock(mutex);return failure;}
 size_t bytes()const{std::lock_guard lock(mutex);return used;}
 void snapshot(ReplaySnapshot& clip,uint64_t start,uint64_t end)const{std::unique_lock lock(mutex);ready.wait_for(lock,std::chrono::milliseconds(200),[&]{return !packets.empty()&&packets.back().clock+213334>=end;});for(auto& packet:packets){if(packet.clock+213334<=start||packet.clock>=end)continue;clip.audio.push_back(packet);}if(!clip.audio.empty()){auto first=clip.audio.front().clock;clip.audioTrim=start>first?(start-first)*48000/10000000:0;clip.audioDelay=first>start?(first-start)*9/1000:0;}}
};
}
