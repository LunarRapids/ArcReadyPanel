#include "../src/graphics.hpp"
#include "../src/overlay_order.hpp"
#include <future>
#include <thread>
#include <iostream>
#include <dwmapi.h>
int main(){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"ArcGlassInputCheck";RegisterClassW(&wc);
 std::promise<HWND> ready;auto handle=ready.get_future();std::thread other([&]{HWND window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE,wc.lpszClassName,L"Underlay input check",WS_POPUP,80,80,480,240,nullptr,nullptr,wc.hInstance,nullptr);ShowWindow(window,SW_SHOWNOACTIVATE);ready.set_value(window);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0)DispatchMessageW(&msg);DestroyWindow(window);});
 HWND under=handle.get(),overlay{};int result=0;
 try{if(!under)throw std::runtime_error("probe creation failed");arc::ComPtr<IDXGIFactory1> factory;arc::check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));arc::ComPtr<IDXGIAdapter1> adapter;
  for(UINT i=0;;++i){arc::ComPtr<IDXGIAdapter1> candidate;if(factory->EnumAdapters1(i,&candidate)==DXGI_ERROR_NOT_FOUND)break;DXGI_ADAPTER_DESC1 desc{};candidate->GetDesc1(&desc);if(desc.VendorId==0x8086&&std::wstring(desc.Description).find(L"B390")!=std::wstring::npos){adapter=candidate;break;}}if(!adapter)throw std::runtime_error("target adapter missing");
  const DWORD flags=WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOREDIRECTIONBITMAP;
  overlay=CreateWindowExW(flags,wc.lpszClassName,L"Composition overlay input check",WS_POPUP,80,80,480,240,nullptr,nullptr,wc.hInstance,nullptr);if(!overlay||!SetLayeredWindowAttributes(overlay,0,255,LWA_ALPHA))throw std::runtime_error("layered style failed");
  {auto device=std::make_shared<arc::GraphicsDevice>(adapter.Get());arc::Surface surface(overlay,240,120,GetDpiForWindow(overlay),device);surface.begin();surface.box(0,0,240,120,10,D2D1::ColorF(.2f,.3f,.4f,.8f));surface.end();ShowWindow(overlay,SW_SHOWNOACTIVATE);SetWindowPos(overlay,HWND_TOPMOST,80,80,480,240,SWP_NOACTIVATE);DwmFlush();POINT point{160,160};
   SetWindowPos(under,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);auto active=GetForegroundWindow();if(!arc::OverlayOrder::restoreAbove(overlay,under))throw std::runtime_error("Overlay z-order recovery failed");if(GetForegroundWindow()!=active)throw std::runtime_error("Z-order recovery stole focus");if(arc::OverlayOrder::restoreAbove(overlay,under))throw std::runtime_error("Z-order recovery repeats while already above");
   if(WindowFromPoint(point)!=under)throw std::runtime_error("locked overlay blocked another thread");
   SetWindowLongPtrW(overlay,GWL_EXSTYLE,flags&~WS_EX_TRANSPARENT);SetWindowPos(overlay,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_FRAMECHANGED);DwmFlush();if(WindowFromPoint(point)!=overlay)throw std::runtime_error("unlocked overlay not interactive");
  auto taskbar=FindWindowW(L"Shell_TrayWnd",nullptr);if(taskbar&&IsWindowVisible(taskbar)){RECT bar{};GetWindowRect(taskbar,&bar);SetWindowPos(overlay,HWND_TOPMOST,bar.left,bar.top,120,40,SWP_NOACTIVATE);arc::OverlayOrder::restoreAbove(overlay,taskbar);for(auto prior=GetWindow(overlay,GW_HWNDPREV);prior;prior=GetWindow(prior,GW_HWNDPREV))if(prior==taskbar)throw std::runtime_error("Actual taskbar remains above overlay");std::cout<<"Actual taskbar z-order checked\n";}
  }std::cout<<"Locked composition overlay passes hit testing to another thread; unlocked accepts input\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
 if(overlay)DestroyWindow(overlay);DWORD thread=GetWindowThreadProcessId(under,nullptr);PostThreadMessageW(thread,WM_QUIT,0,0);other.join();CoUninitialize();return result;
}
