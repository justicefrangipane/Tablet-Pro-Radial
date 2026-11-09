#include "PenStrokeMonitor.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tabletpro {
struct PenStrokeMonitor::Native {
    PenStrokeMonitor* owner;
#ifdef Q_OS_WIN
    HHOOK hook=nullptr;
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
#endif
};
PenStrokeMonitor::PenStrokeMonitor(QObject* parent):QObject(parent),native_(std::make_unique<Native>()) {native_->owner=this;}
PenStrokeMonitor::~PenStrokeMonitor(){stop();}
bool PenStrokeMonitor::start(){
    if(running_)return true;
#ifdef Q_OS_WIN
    if(Native::hookOwner)return false;
    const auto module=GetModuleHandleW(nullptr);
    native_->hook=SetWindowsHookExW(WH_MOUSE_LL,Native::mouseHook,module,0);
    if(!native_->hook){stop();return false;}
    Native::hookOwner=native_.get();
#endif
    contactSources_.clear();running_=true;return true;
}
void PenStrokeMonitor::stop(){
    running_=false;contactSources_.clear();
#ifdef Q_OS_WIN
    if(native_->hook){UnhookWindowsHookEx(native_->hook);native_->hook=nullptr;}
    if(Native::hookOwner==native_.get())Native::hookOwner=nullptr;
#endif
}
void PenStrokeMonitor::contactAtCursor(quintptr source,bool down){
#ifdef Q_OS_WIN
    POINT cursor{};GetCursorPos(&cursor);contact(source,down,Native::ownWindow(cursor));
#else
    contact(source,down,false);
#endif
}
void PenStrokeMonitor::contact(quintptr source,bool down,bool inOwnApplication){
    if(!running_)return;
    if(down){if(!inOwnApplication)contactSources_.insert(source);return;}
    if(contactSources_.contains(source)){running_=false;contactSources_.clear();emit strokeFinished();}
}
}
