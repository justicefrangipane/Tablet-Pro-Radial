#include "PenStrokeMonitor.h"
#include <vector>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
extern "C" {
#include <hidsdi.h>
}
#endif

namespace tabletpro {
struct PenStrokeMonitor::Native {
    PenStrokeMonitor* owner;
#ifdef Q_OS_WIN
    HWND window=nullptr;
    HHOOK hook=nullptr;
    bool rawRegistered=false;
    inline static Native* hookOwner=nullptr;
    static bool ownWindow(POINT point) {
        DWORD pid=0;GetWindowThreadProcessId(WindowFromPoint(point),&pid);
        return pid==GetCurrentProcessId();
    }
    static LRESULT CALLBACK mouseHook(int code,WPARAM message,LPARAM data) {
        if(code==HC_ACTION && hookOwner && (message==WM_LBUTTONDOWN || message==WM_LBUTTONUP)) {
            const auto* event=reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
            // Microsoft's pen promotion signature; touch has bit 0x80 set.
            if((event->dwExtraInfo&0xffffff80)==0xff515700)
                hookOwner->owner->contact((quintptr(1)<<(sizeof(quintptr)*8-1))|(event->dwExtraInfo&0x7f),
                                          message==WM_LBUTTONDOWN,ownWindow(event->pt));
        }
        return CallNextHookEx(nullptr,code,message,data);
    }
    void rawContact(HRAWINPUT handle) {
        UINT size=0;
        if(GetRawInputData(handle,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER))!=0 || size<sizeof(RAWINPUTHEADER) || size>65536)return;
        std::vector<BYTE> bytes(size);
        if(GetRawInputData(handle,RID_INPUT,bytes.data(),&size,sizeof(RAWINPUTHEADER))!=size)return;
        const auto* raw=reinterpret_cast<const RAWINPUT*>(bytes.data());
        if(raw->header.dwType!=RIM_TYPEHID || size<offsetof(RAWINPUT,data.hid.bRawData))return;
        const auto& hid=raw->data.hid;
        if(hid.dwSizeHid==0 || hid.dwSizeHid>4096 || hid.dwCount>64 ||
           quint64(offsetof(RAWINPUT,data.hid.bRawData))+quint64(hid.dwSizeHid)*hid.dwCount>size)return;
        UINT preSize=0;
        if(GetRawInputDeviceInfoW(raw->header.hDevice,RIDI_PREPARSEDDATA,nullptr,&preSize)==UINT(-1) || !preSize || preSize>65536)return;
        std::vector<BYTE> preparsed(preSize);
        if(GetRawInputDeviceInfoW(raw->header.hDevice,RIDI_PREPARSEDDATA,preparsed.data(),&preSize)==UINT(-1))return;
        auto* data=reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed.data());
        HIDP_CAPS caps{};
        if(HidP_GetCaps(data,&caps)!=HIDP_STATUS_SUCCESS || caps.UsagePage!=0x0d || caps.Usage!=2 || caps.NumberInputButtonCaps>256)return;
        USHORT count=caps.NumberInputButtonCaps;std::vector<HIDP_BUTTON_CAPS> buttons(count);
        if(!count || HidP_GetButtonCaps(HidP_Input,buttons.data(),&count,data)!=HIDP_STATUS_SUCCESS)return;
        for(const auto& cap:buttons) {
            const bool tip=cap.UsagePage==0x0d && (cap.IsRange ? cap.Range.UsageMin<=0x42&&cap.Range.UsageMax>=0x42 : cap.NotRange.Usage==0x42);
            if(!tip)continue;
            for(UINT i=0;i<hid.dwCount;++i) {
                auto* report=reinterpret_cast<PCHAR>(const_cast<BYTE*>(hid.bRawData)+i*hid.dwSizeHid);
                if(cap.ReportID && BYTE(report[0])!=cap.ReportID)continue;
                USAGE usages[128]{};ULONG usagesCount=128;
                if(HidP_GetUsages(HidP_Input,0x0d,cap.LinkCollection,usages,&usagesCount,data,report,hid.dwSizeHid)!=HIDP_STATUS_SUCCESS)continue;
                bool down=false;for(ULONG j=0;j<usagesCount;++j)if(usages[j]==0x42)down=true;
                POINT cursor{};GetCursorPos(&cursor);
                owner->contact(reinterpret_cast<quintptr>(raw->header.hDevice),down,ownWindow(cursor));
            }
            break;
        }
    }
    static LRESULT CALLBACK receive(HWND window,UINT message,WPARAM w,LPARAM l) {
        if(message==WM_NCCREATE)SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
        auto* self=reinterpret_cast<Native*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(self && message==WM_INPUT)self->rawContact(reinterpret_cast<HRAWINPUT>(l));
        return DefWindowProcW(window,message,w,l);
    }
#endif
};
PenStrokeMonitor::PenStrokeMonitor(QObject* parent):QObject(parent),native_(std::make_unique<Native>()) {native_->owner=this;}
PenStrokeMonitor::~PenStrokeMonitor(){stop();}
bool PenStrokeMonitor::start(){
    if(running_)return true;
#ifdef Q_OS_WIN
    if(Native::hookOwner)return false;
    const auto module=GetModuleHandleW(nullptr);
    WNDCLASSW cls{};cls.lpfnWndProc=Native::receive;cls.hInstance=module;cls.lpszClassName=L"TabletProPenStrokeObserver";
    if(!RegisterClassW(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    native_->window=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,module,native_.get());
    if(!native_->window)return false;
    RAWINPUTDEVICE pen{0x0d,2,RIDEV_INPUTSINK,native_->window};
    native_->rawRegistered=RegisterRawInputDevices(&pen,1,sizeof(pen));
    native_->hook=SetWindowsHookExW(WH_MOUSE_LL,Native::mouseHook,module,0);
    if(!native_->rawRegistered || !native_->hook){stop();return false;}
    Native::hookOwner=native_.get();
#endif
    contactSource_.reset();running_=true;return true;
}
void PenStrokeMonitor::stop(){
    running_=false;contactSource_.reset();
#ifdef Q_OS_WIN
    if(native_->hook){UnhookWindowsHookEx(native_->hook);native_->hook=nullptr;}
    if(Native::hookOwner==native_.get())Native::hookOwner=nullptr;
    if(native_->rawRegistered){RAWINPUTDEVICE pen{0x0d,2,RIDEV_REMOVE,nullptr};RegisterRawInputDevices(&pen,1,sizeof(pen));native_->rawRegistered=false;}
    if(native_->window){DestroyWindow(native_->window);native_->window=nullptr;}
#endif
}
void PenStrokeMonitor::contact(quintptr source,bool down,bool inOwnApplication){
    if(!running_)return;
    if(!contactSource_){if(down&&!inOwnApplication)contactSource_=source;return;}
    if(*contactSource_==source&&!down){running_=false;contactSource_.reset();emit strokeFinished();}
}
}
