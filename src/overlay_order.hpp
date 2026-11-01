#pragma once
#include <windows.h>
#include <cwchar>
namespace arc {
// Restore z-order only in response to shell/foreground events; no polling timer.
class OverlayOrder {
 HWND receiver;HWINEVENTHOOK foreground{},shell{},location{};DWORD shellPid{};
 inline static HWND destination{};
 static bool isTaskbar(HWND window){wchar_t name[64]{};GetClassNameW(window,name,64);return !wcscmp(name,L"Shell_TrayWnd")||!wcscmp(name,L"Shell_SecondaryTrayWnd");}
 static void CALLBACK event(HWINEVENTHOOK,DWORD type,HWND window,LONG object,LONG child,DWORD,DWORD){if(!destination)return;if(type==EVENT_SYSTEM_FOREGROUND||(type==EVENT_OBJECT_REORDER&&(!window||window==GetDesktopWindow()||window==GetShellWindow()))||(window&&object==OBJID_WINDOW&&child==CHILDID_SELF&&isTaskbar(window)))PostMessageW(destination,WM_APP+11,0,0);}
public:
 explicit OverlayOrder(HWND window):receiver(window){destination=window;foreground=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,event,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);rebind();}
 ~OverlayOrder(){destination=nullptr;if(foreground)UnhookWinEvent(foreground);if(shell)UnhookWinEvent(shell);if(location)UnhookWinEvent(location);}
 void rebind(){DWORD pid{};GetWindowThreadProcessId(FindWindowW(L"Shell_TrayWnd",nullptr),&pid);if(pid==shellPid&&shell)return;if(shell)UnhookWinEvent(shell);if(location)UnhookWinEvent(location);shell=location=nullptr;shellPid=pid;if(pid){shell=SetWinEventHook(EVENT_OBJECT_SHOW,EVENT_OBJECT_REORDER,nullptr,event,pid,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);location=SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE,nullptr,event,pid,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);}}
 static bool restoreAbove(HWND overlay,HWND blocker){if(!IsWindowVisible(overlay)||!IsWindowVisible(blocker))return false;RECT a{},b{},overlap{};GetWindowRect(overlay,&a);GetWindowRect(blocker,&b);if(!IntersectRect(&overlap,&a,&b))return false;for(auto prior=GetWindow(overlay,GW_HWNDPREV);prior;prior=GetWindow(prior,GW_HWNDPREV))if(prior==blocker)return SetWindowPos(overlay,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE;return false;}
 void restore(HWND overlay){rebind();for(auto window=GetTopWindow(nullptr);window&&window!=overlay;window=GetWindow(window,GW_HWNDNEXT))if(isTaskbar(window)&&restoreAbove(overlay,window))break;}
};
}
