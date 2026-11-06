#include "PenPositionTracker.h"
#include <QGuiApplication>
#include <QScreen>
#include <QTabletEvent>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <qscreen_platform.h>
#endif

namespace tabletpro {
struct PenPositionTracker::Native {
    PenPositionTracker* owner;
#ifdef Q_OS_WIN
    HHOOK hook=nullptr;
    inline static Native* active=nullptr;
    static LRESULT CALLBACK receive(int code,WPARAM message,LPARAM data){
        if(code==HC_ACTION && active && (message==WM_MOUSEMOVE || message==WM_LBUTTONDOWN || message==WM_LBUTTONUP)){
            const auto* input=reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
            if(PenPositionTracker::isPenMessage(input->dwExtraInfo)){
                // Hook points are native per-monitor pixels; Qt globals are logical.
                const auto handle=MonitorFromPoint(input->pt,MONITOR_DEFAULTTONULL);
                MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
                if(GetMonitorInfoW(handle,&monitor)){
                    for(auto* screen:QGuiApplication::screens()){
                        const auto* nativeScreen=screen->nativeInterface<QNativeInterface::QWindowsScreen>();
                        if(nativeScreen && nativeScreen->handle()==handle){
                            active->owner->observe(screen->geometry().topLeft()+QPointF(input->pt.x-monitor.rcMonitor.left,input->pt.y-monitor.rcMonitor.top)/screen->devicePixelRatio());
                            break;
                        }
                    }
                }
            }
        }
        return CallNextHookEx(nullptr,code,message,data);
    }
#endif
};
PenPositionTracker::PenPositionTracker(QObject* parent):QObject(parent),native_(std::make_unique<Native>()){native_->owner=this;}
PenPositionTracker::~PenPositionTracker(){stop();}
bool PenPositionTracker::isPenMessage(quintptr info){return (info&0xffffff80)==0xff515700;}
void PenPositionTracker::observe(QPointF global){if(!std::isfinite(global.x())||!std::isfinite(global.y()))return;position_=global;age_.restart();}
std::optional<QPointF> PenPositionTracker::position() const {return position_&&age_.isValid()&&age_.elapsed()<=30000?position_:std::nullopt;}
bool PenPositionTracker::start(){
    if(started_)return true;
#ifdef Q_OS_WIN
    if(Native::active)return false;
    native_->hook=SetWindowsHookExW(WH_MOUSE_LL,Native::receive,GetModuleHandleW(nullptr),0);
    if(!native_->hook)return false;
    Native::active=native_.get();
#endif
    if(qApp)qApp->installEventFilter(this);started_=true;return true;
}
void PenPositionTracker::stop(){
    if(qApp)qApp->removeEventFilter(this);
#ifdef Q_OS_WIN
    if(native_->hook){UnhookWindowsHookEx(native_->hook);native_->hook=nullptr;}
    if(Native::active==native_.get())Native::active=nullptr;
#endif
    started_=false;position_.reset();age_.invalidate();
}
bool PenPositionTracker::eventFilter(QObject*,QEvent* event){
    if(event->type()==QEvent::TabletMove||event->type()==QEvent::TabletPress||event->type()==QEvent::TabletRelease)
        observe(static_cast<QTabletEvent*>(event)->globalPosition());
    if(event->type()==QEvent::TabletLeaveProximity){position_.reset();age_.invalidate();}
    return false;
}
}
