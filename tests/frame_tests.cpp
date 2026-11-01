#include "../src/frames.hpp"
#include <d3d11.h>
#include <iostream>
#include <wrl/client.h>
namespace arc {
struct FrameTestHarness {
 static uint64_t received(GameFrames& value){return value.delivered;}
 static void require(bool v){if(!v)throw std::runtime_error("frame filtering/statistics failure");}
 static void run(){
  GameFrames frames(123,10,true);LARGE_INTEGER now;QueryPerformanceCounter(&now);
  struct Payload {uint64_t chain;DWORD flags;int sync;};Payload payload{100,0,1};HRESULT success=S_OK;
  auto emit=[&](USHORT id,LONGLONG time,DWORD process,DWORD flags=0){EVENT_RECORD event{};event.EventHeader.ProviderId=dxgiProvider;event.EventHeader.ProcessId=process;event.EventHeader.ThreadId=456;event.EventHeader.EventDescriptor.Id=id;event.EventHeader.TimeStamp.QuadPart=time;payload.flags=id==178?1:flags;payload.sync=id==178?int(flags):1;event.UserData=(id==42||id==55||id==178)?static_cast<void*>(&payload):static_cast<void*>(&success);event.UserDataLength=(id==42||id==55||id==178)?16:4;frames.consume(event);};
  auto start=now.QuadPart-frames.frequency;for(int i=0;i<181;++i){auto t=start+i*frames.frequency/200;emit(42,t,123);emit(43,t+1,123);}
  auto stats=frames.stats();require(stats&&std::abs(stats->fps-200)<.1&&std::abs(stats->low1-200)<.1);auto count=frames.streams.at(100).intervals.size();
  emit(42,now.QuadPart,999);emit(43,now.QuadPart+1,999);emit(42,now.QuadPart,123,1);emit(43,now.QuadPart+1,123);require(frames.streams.at(100).intervals.size()==count);
  success=DXGI_STATUS_OCCLUDED;emit(42,now.QuadPart,123);emit(43,now.QuadPart+1,123);require(frames.streams.at(100).intervals.size()==count);
  success=S_OK;auto before=frames.delivered;emit(42,now.QuadPart,123);emit(55,now.QuadPart+1,123);emit(56,now.QuadPart+2,123);emit(43,now.QuadPart+3,123);require(frames.delivered==before+1);
  before=frames.delivered;emit(178,now.QuadPart+4,123);emit(42,now.QuadPart+5,123);emit(43,now.QuadPart+6,123);emit(179,now.QuadPart+7,123);require(frames.delivered==before+1);before=frames.delivered;emit(178,now.QuadPart+8,123,1);emit(179,now.QuadPart+9,123);require(frames.delivered==before);
  EVENT_RECORD malformed{};malformed.EventHeader.ProviderId=dxgiProvider;malformed.EventHeader.ProcessId=123;malformed.EventHeader.EventDescriptor.Id=42;frames.consume(malformed);require(frames.pending.empty());
  while(frames.streams.at(100).intervals.size()>50)frames.streams.at(100).intervals.pop_front();auto warm=frames.stats();require(warm&&warm->fps>0&&warm->low1==0&&warm->samples==50);frames.arrival=now.QuadPart-frames.frequency*3;frames.streams.at(100).last=frames.arrival;require(frames.stats().has_value());frames.arrival=now.QuadPart-frames.frequency*6;require(!frames.stats());
 }
};
}
int main(int argc,char**){
 try{if(argc==1){arc::FrameTestHarness::run();std::cout<<"PID, test Present, failure, malformed events, statistics and stale stream checks passed\n";return 0;}
  arc::GameFrames frames(GetCurrentProcessId(),10);if(frames.error==ERROR_ACCESS_DENIED){std::cout<<"Real DXGI ETW requires administrator/Performance Log Users access; integration skipped\n";return 77;}if(frames.error)throw std::runtime_error("ETW start failed");
  WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"ArcGlassFrameTest";RegisterClassW(&wc);HWND hwnd=CreateWindowW(wc.lpszClassName,L"Frame producer",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,wc.hInstance,nullptr);ShowWindow(hwnd,SW_SHOWNOACTIVATE);
  DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=320;desc.BufferDesc.Height=240;desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.OutputWindow=hwnd;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swap;Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&swap,&device,nullptr,&context)))throw std::runtime_error("producer failed");
  Microsoft::WRL::ComPtr<ID3D11Texture2D> back;swap->GetBuffer(0,IID_PPV_ARGS(&back));Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;device->CreateRenderTargetView(back.Get(),nullptr,&target);Sleep(1200);std::map<unsigned,unsigned> presentResults;auto begin=GetTickCount64();for(int i=0;i<240;++i){MSG msg;while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessage(&msg);}if(i%16==0)frames.lost();const float color[]={float(i%60)/60.f,.2f,.3f,1.f};context->ClearRenderTargetView(target.Get(),color);++presentResults[static_cast<unsigned>(swap->Present(1,0))];Sleep(16);}
  for(auto [hr,count]:presentResults)std::cout<<"Present hr="<<hr<<" count="<<count<<" elapsed="<<GetTickCount64()-begin<<"\n";Sleep(1500);auto stats=frames.stats();DestroyWindow(hwnd);std::cout<<"valid="<<bool(stats)<<" samples="<<(stats?stats->samples:0)<<" received="<<arc::FrameTestHarness::received(frames)<<" loss events="<<frames.lostEvents<<" buffers="<<frames.lostBuffers<<"\n";if(!stats||stats->fps<=0||stats->low1<=0)throw std::runtime_error("real DXGI Present statistics unavailable");std::cout<<"Real Presents average="<<stats->fps<<" 1% Low="<<stats->low1<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
