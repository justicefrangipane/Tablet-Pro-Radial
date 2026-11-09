#include "PenPositionTracker.h"
#include <QGuiApplication>
#include <QScreen>
#include <QTabletEvent>
#include <cmath>
#include <QRectF>
#include <algorithm>
#include <map>
#include <vector>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <qscreen_platform.h>
extern "C" {
#include <hidsdi.h>
}
#endif

namespace tabletpro {
struct PenPositionTracker::Native {
    PenPositionTracker* owner;
#ifdef Q_OS_WIN
    HHOOK hook=nullptr;
    HWND window=nullptr;
    bool rawRegistered=false;
    HANDLE positionDevice=nullptr;
    std::map<HANDLE,bool> tips;
    inline static Native* active=nullptr;
    static LRESULT CALLBACK receive(int code,WPARAM message,LPARAM data){
        if(code==HC_ACTION && active && (message==WM_MOUSEMOVE || message==WM_LBUTTONDOWN || message==WM_LBUTTONUP)){
            const auto* input=reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
            if(PenPositionTracker::isPenMessage(input->dwExtraInfo)){
                active->owner->observeNative({qreal(input->pt.x),qreal(input->pt.y)});
            }
        }
        return CallNextHookEx(nullptr,code,message,data);
    }
    void rawInput(HRAWINPUT handle){
        UINT size=0;
        if(GetRawInputData(handle,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER))!=0||size<sizeof(RAWINPUTHEADER)||size>65536)return;
        std::vector<BYTE> bytes(size);
        if(GetRawInputData(handle,RID_INPUT,bytes.data(),&size,sizeof(RAWINPUTHEADER))!=size)return;
        const auto* raw=reinterpret_cast<const RAWINPUT*>(bytes.data());
        if(raw->header.dwType!=RIM_TYPEHID||size<offsetof(RAWINPUT,data.hid.bRawData))return;
        const auto& hid=raw->data.hid;
        if(!hid.dwSizeHid||hid.dwSizeHid>4096||hid.dwCount>64||quint64(offsetof(RAWINPUT,data.hid.bRawData))+quint64(hid.dwSizeHid)*hid.dwCount>size)return;
        UINT preSize=0;
        if(GetRawInputDeviceInfoW(raw->header.hDevice,RIDI_PREPARSEDDATA,nullptr,&preSize)==UINT(-1)||!preSize||preSize>65536)return;
        std::vector<BYTE> preparsed(preSize);
        if(GetRawInputDeviceInfoW(raw->header.hDevice,RIDI_PREPARSEDDATA,preparsed.data(),&preSize)==UINT(-1))return;
        auto* data=reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed.data());HIDP_CAPS caps{};
        if(HidP_GetCaps(data,&caps)!=HIDP_STATUS_SUCCESS||caps.UsagePage!=0x0d||(caps.Usage!=1&&caps.Usage!=2)||caps.NumberInputValueCaps>256||caps.NumberInputButtonCaps>256)return;
        USHORT vc=caps.NumberInputValueCaps,bc=caps.NumberInputButtonCaps;
        std::vector<HIDP_VALUE_CAPS> values(vc);std::vector<HIDP_BUTTON_CAPS> buttons(bc);
        if(vc&&HidP_GetValueCaps(HidP_Input,values.data(),&vc,data)!=HIDP_STATUS_SUCCESS)return;
        if(bc&&HidP_GetButtonCaps(HidP_Input,buttons.data(),&bc,data)!=HIDP_STATUS_SUCCESS)return;
        values.resize(vc);buttons.resize(bc);
        RECT device{},display{};POINTER_DEVICE_INFO pointer{};
        const bool mapped=GetPointerDevice(raw->header.hDevice,&pointer)&&GetPointerDeviceRects(raw->header.hDevice,&device,&display);
        for(UINT i=0;i<hid.dwCount;++i){
            auto* report=reinterpret_cast<PCHAR>(const_cast<BYTE*>(hid.bRawData)+i*hid.dwSizeHid);
            std::optional<double> x,y;double minX=0,maxX=0,minY=0,maxY=0;
            for(const auto& cap:values){
                if(cap.UsagePage!=1||(cap.ReportID&&BYTE(report[0])!=cap.ReportID))continue;
                for(USAGE axis:{USAGE(0x30),USAGE(0x31)}){
                    if(!(cap.IsRange?axis>=cap.Range.UsageMin&&axis<=cap.Range.UsageMax:axis==cap.NotRange.Usage))continue;
                    ULONG value=0;
                    if(HidP_GetUsageValue(HidP_Input,1,cap.LinkCollection,axis,&value,data,report,hid.dwSizeHid)!=HIDP_STATUS_SUCCESS)continue;
                    double signedValue=value;
                    if(cap.LogicalMin<0&&cap.BitSize>0&&cap.BitSize<=32){const unsigned shift=32-cap.BitSize;signedValue=qint32(value<<shift)>>shift;}
                    if(axis==0x30){x=signedValue;minX=cap.LogicalMin;maxX=cap.LogicalMax;}
                    else{y=signedValue;minY=cap.LogicalMin;maxY=cap.LogicalMax;}
                }
            }
            std::optional<QPointF> point;
            if(mapped&&x&&y)point=PenPositionTracker::mapHidPosition({*x,*y},{minX,minY,maxX-minX,maxY-minY},
                {qreal(display.left),qreal(display.top),qreal(display.right-display.left),qreal(display.bottom-display.top)},int(pointer.displayOrientation));
            std::optional<bool> tip,inRange;
            for(const auto& cap:buttons){
                if(cap.UsagePage!=0x0d||(cap.ReportID&&BYTE(report[0])!=cap.ReportID))continue;
                for(USAGE usage:{USAGE(0x42),USAGE(0x32)}){
                    if(!(cap.IsRange?usage>=cap.Range.UsageMin&&usage<=cap.Range.UsageMax:usage==cap.NotRange.Usage))continue;
                    USAGE usages[128]{};ULONG count=128;
                    if(HidP_GetUsages(HidP_Input,0x0d,cap.LinkCollection,usages,&count,data,report,hid.dwSizeHid)!=HIDP_STATUS_SUCCESS)continue;
                    const bool set=std::find(usages,usages+count,usage)!=usages+count;
                    if(usage==0x42)tip=set;else inRange=set;
                }
            }
            if(point&&inRange.value_or(true)){positionDevice=raw->header.hDevice;owner->observeNative(*point);}
            if(inRange==false){
                if(positionDevice==raw->header.hDevice){owner->clearPosition();positionDevice=nullptr;emit owner->proximityLeft();}
                // Some proximity-out reports omit Tip Switch altogether.
                if(tips.contains(raw->header.hDevice))tip=false;
            }
            if(tip){
                const bool down=*tip&&inRange.value_or(true);
                const auto [entry,first]=tips.try_emplace(raw->header.hDevice,down);
                if(!first&&entry->second!=down){
                    entry->second=down;POINT cursor{};GetCursorPos(&cursor);
                    if(point)cursor={LONG(std::lround(point->x())),LONG(std::lround(point->y()))};
                    DWORD pid=0;GetWindowThreadProcessId(WindowFromPoint(cursor),&pid);
                    emit owner->contactObserved(reinterpret_cast<quintptr>(raw->header.hDevice),down,pid==GetCurrentProcessId());
                }
            }
        }
    }
    static LRESULT CALLBACK rawWindow(HWND window,UINT message,WPARAM w,LPARAM l){
        if(message==WM_NCCREATE)SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
        auto* self=reinterpret_cast<Native*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(self&&message==WM_INPUT)self->rawInput(reinterpret_cast<HRAWINPUT>(l));
        if(self&&message==WM_INPUT_DEVICE_CHANGE&&w==GIDC_REMOVAL){
            const auto device=reinterpret_cast<HANDLE>(l);
            if(self->positionDevice==device){self->owner->clearPosition();self->positionDevice=nullptr;}
            if(self->tips.erase(device))emit self->owner->deviceLost();
        }
        return DefWindowProcW(window,message,w,l);
    }
#endif
};
PenPositionTracker::PenPositionTracker(QObject* parent):QObject(parent),native_(std::make_unique<Native>()){native_->owner=this;}
PenPositionTracker::~PenPositionTracker(){stop();}
bool PenPositionTracker::isPenMessage(quintptr info){return (info&0xffffff80)==0xff515700;}
std::optional<QPointF> PenPositionTracker::mapHidPosition(QPointF raw,QRectF range,QRectF display,int rotation){
    if(range.width()<=0||range.height()<=0||display.width()<=0||display.height()<=0||!std::isfinite(raw.x())||!std::isfinite(raw.y()))return std::nullopt;
    double u=(raw.x()-range.x())/range.width(),v=(raw.y()-range.y())/range.height();
    if(u<0||u>1||v<0||v>1)return std::nullopt;
    if(rotation==2){const auto old=u;u=1-v;v=old;}
    else if(rotation==3){u=1-u;v=1-v;}
    else if(rotation==4){const auto old=u;u=v;v=1-old;}
    return QPointF(display.x()+u*(display.width()-1),display.y()+v*(display.height()-1));
}
void PenPositionTracker::observeNative(QPointF point){
#ifdef Q_OS_WIN
    const POINT native{LONG(std::lround(point.x())),LONG(std::lround(point.y()))};
    const auto handle=MonitorFromPoint(native,MONITOR_DEFAULTTONULL);
    MONITORINFO monitor{};monitor.cbSize=sizeof monitor;
    if(GetMonitorInfoW(handle,&monitor))for(auto* screen:QGuiApplication::screens()){
        const auto* nativeScreen=screen->nativeInterface<QNativeInterface::QWindowsScreen>();
        if(nativeScreen&&nativeScreen->handle()==handle){observe(screen->geometry().topLeft()+(point-QPointF(monitor.rcMonitor.left,monitor.rcMonitor.top))/screen->devicePixelRatio());break;}
    }
#else
    observe(point);
#endif
}
void PenPositionTracker::observe(QPointF global){if(!std::isfinite(global.x())||!std::isfinite(global.y()))return;position_=global;age_.restart();}
void PenPositionTracker::clearPosition(){position_.reset();age_.invalidate();}
std::optional<QPointF> PenPositionTracker::position() const {return position_&&age_.isValid()&&age_.elapsed()<=30000?position_:std::nullopt;}
bool PenPositionTracker::start(){
    if(started_)return true;
#ifdef Q_OS_WIN
    if(Native::active)return false;
    native_->hook=SetWindowsHookExW(WH_MOUSE_LL,Native::receive,GetModuleHandleW(nullptr),0);
    if(!native_->hook)return false;
    Native::active=native_.get();
    const auto module=GetModuleHandleW(nullptr);
    WNDCLASSW cls{};cls.lpfnWndProc=Native::rawWindow;cls.hInstance=module;cls.lpszClassName=L"TabletPro.PenLocation";
    if(RegisterClassW(&cls)||GetLastError()==ERROR_CLASS_ALREADY_EXISTS){
        native_->window=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,module,native_.get());
        if(native_->window){RAWINPUTDEVICE pens[]={{0x0d,1,RIDEV_INPUTSINK|RIDEV_DEVNOTIFY,native_->window},{0x0d,2,RIDEV_INPUTSINK|RIDEV_DEVNOTIFY,native_->window}};
            native_->rawRegistered=RegisterRawInputDevices(pens,2,sizeof(RAWINPUTDEVICE));}
    }
#endif
    if(qApp)qApp->installEventFilter(this);started_=true;return true;
}
void PenPositionTracker::stop(){
    if(qApp)qApp->removeEventFilter(this);
#ifdef Q_OS_WIN
    if(native_->hook){UnhookWindowsHookEx(native_->hook);native_->hook=nullptr;}
    if(Native::active==native_.get())Native::active=nullptr;
    if(native_->rawRegistered){RAWINPUTDEVICE pens[]={{0x0d,1,RIDEV_REMOVE,nullptr},{0x0d,2,RIDEV_REMOVE,nullptr}};RegisterRawInputDevices(pens,2,sizeof(RAWINPUTDEVICE));native_->rawRegistered=false;}
    if(native_->window){DestroyWindow(native_->window);native_->window=nullptr;}native_->tips.clear();
#endif
    started_=false;position_.reset();age_.invalidate();
}
bool PenPositionTracker::eventFilter(QObject*,QEvent* event){
    if(event->type()==QEvent::TabletMove||event->type()==QEvent::TabletPress||event->type()==QEvent::TabletRelease)
        observe(static_cast<QTabletEvent*>(event)->globalPosition());
    if(event->type()==QEvent::TabletLeaveProximity){clearPosition();emit proximityLeft();}
    return false;
}
}
