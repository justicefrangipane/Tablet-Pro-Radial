// Sends input only to this executable's separate, disposable receiver process.
#include "platform/WindowPlatform.h"
#include <QGuiApplication>
#include <QProcess>
#include <QEventLoop>
#include <QTimer>
#include <iostream>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
int checks=0;
void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
void settle(int ms=60){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
int downs[256]{},ups[256]{},bModifiers=0;
LRESULT CALLBACK receiver(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_KEYDOWN||message==WM_SYSKEYDOWN){
        if(w<256)++downs[w];
        if(w=='B')bModifiers=(GetKeyState(VK_CONTROL)&0x8000?1:0)|(GetKeyState(VK_SHIFT)&0x8000?2:0)|(GetKeyState(VK_MENU)&0x8000?4:0);
        return 0;
    }
    if(message==WM_KEYUP||message==WM_SYSKEYUP){if(w<256)++ups[w];return 0;}
    if(message==WM_APP+1)return w<256?downs[w]:0;
    if(message==WM_APP+2)return w<256?ups[w]:0;
    if(message==WM_APP+3)return bModifiers;
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,w,l);
}
int target(){
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=receiver;cls.lpszClassName=L"RadialModifierReceiver";
    RegisterClassW(&cls);
    auto window=CreateWindowExW(0,cls.lpszClassName,L"Radial input verification",WS_OVERLAPPEDWINDOW,80,80,300,160,nullptr,nullptr,cls.hInstance,nullptr);
    if(!window)return 2;
    ShowWindow(window,SW_SHOW);SetForegroundWindow(window);
    std::cout<<reinterpret_cast<quintptr>(window)<<std::endl;
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    return 0;
}
}
int main(int argc,char** argv){
    if(argc>1&&std::string(argv[1])=="--input-target")return target();
    QGuiApplication app(argc,argv);
    HWND original=GetForegroundWindow();
    POINT originalCursor{};GetCursorPos(&originalCursor);
    QProcess child;HWND window=nullptr;
    int result=0;
    try {
        for(int key:{VK_CONTROL,VK_SHIFT,VK_MENU})check(!(GetAsyncKeyState(key)&0x8000),"physical modifiers must be released before verification");
        child.start(QCoreApplication::applicationFilePath(),{"--input-target"});
        check(child.waitForStarted(3000)&&child.waitForReadyRead(3000),"receiver starts");
        window=reinterpret_cast<HWND>(child.readLine().trimmed().toULongLong());
        check(IsWindow(window),"receiver window is valid");
        SetForegroundWindow(window);settle();
        check(GetForegroundWindow()==window,"verification receiver has focus");
        tabletpro::WindowPlatform platform;
        auto captured=platform.captureForegroundTarget();check(captured==reinterpret_cast<quintptr>(window),"captures separate receiver");
        check(!platform.setModifierHeld("Ctrl",true,0),"no target refuses held input");
        check(!platform.setModifierHeld("Meta",true,captured),"unsupported modifier refused");
        const bool ctrlHeld=platform.setModifierHeld("Ctrl",true,captured);
        if(!ctrlHeld)std::cerr<<platform.lastError().toStdString()<<'\n';
        check(ctrlHeld,"Ctrl down inserted");settle();
        check(GetAsyncKeyState(VK_CONTROL)&0x8000,"Windows reports Ctrl down");
        check(SendMessageW(window,WM_APP+1,VK_CONTROL,0)==1,"receiver gets Ctrl down");
        platform.releaseTarget();settle();
        check(GetAsyncKeyState(VK_CONTROL)&0x8000,"latch survives completion of a gesture");
        captured=platform.captureForegroundTarget();
        check(platform.executeShortcut("B",{"Ctrl"},captured),"shortcut works with an owned modifier");settle();
        check((GetAsyncKeyState(VK_CONTROL)&0x8000)&&SendMessageW(window,WM_APP+3,0,0)==1,"shortcut preserves held Ctrl and reaches receiver as Ctrl+B");
        check(SendMessageW(window,WM_APP+1,VK_CONTROL,0)==1&&SendMessageW(window,WM_APP+2,VK_CONTROL,0)==0,"shortcut neither duplicates nor releases latched modifier");
        captured=platform.captureForegroundTarget();
        check(platform.setModifierHeld("Shift",true,captured),"second held modifier inserted");settle();
        check((GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_SHIFT)&0x8000),"two modifiers coexist");
        check(platform.setModifierHeld("Shift",false),"release only Shift");settle();
        check((GetAsyncKeyState(VK_CONTROL)&0x8000)&&!(GetAsyncKeyState(VK_SHIFT)&0x8000),"release preserves other key");
        check(platform.setModifierHeld("Ctrl",false),"release Ctrl");settle();
        check(!(GetAsyncKeyState(VK_CONTROL)&0x8000)&&SendMessageW(window,WM_APP+2,VK_CONTROL,0)==1,"receiver gets exactly one Ctrl up");
        for(const auto& key:{QString("Alt"),QString("Ctrl"),QString("Shift")}){
            captured=platform.captureForegroundTarget();check(platform.setModifierHeld(key,true,captured),"hold each supported modifier");
        }
        platform.releaseModifiers();settle();
        check(platform.heldModifiers().isEmpty()&&!(GetAsyncKeyState(VK_MENU)&0x8000)&&!(GetAsyncKeyState(VK_CONTROL)&0x8000)&&!(GetAsyncKeyState(VK_SHIFT)&0x8000),"cleanup releases all owned modifiers");
        {
            tabletpro::PenStrokeMonitor monitor;int strokes=0;
            QObject::connect(&monitor,&tabletpro::PenStrokeMonitor::strokeFinished,[&]{++strokes;});
            check(monitor.start(),"pen observer starts");
            monitor.contact(1,false,false);monitor.contact(1,true,true);monitor.contact(1,false,true);
            check(strokes==0,"selection lift and own app contacts cannot consume next stroke");
            monitor.contact(7,true,false);monitor.contact(9,false,false);
            check(strokes==0,"other device or promoted channel cannot end a stroke");
            monitor.contact(7,false,true);monitor.contact(7,false,false);
            check(strokes==1,"first external stroke ends once even if its release is over the menu");
            monitor.stop();check(monitor.start(),"observer restarts after cleanup");
            monitor.contact(1,true,false);monitor.stop();monitor.contact(1,false,false);
            check(strokes==1,"canceled observer cannot emit a stale stroke completion");
        }
        captured=platform.captureForegroundTarget();
        check(platform.setModifierHeld("Ctrl",true,captured)&&platform.armNextStroke("Ctrl"),"Ctrl arms native next stroke observer");
        check(platform.setModifierHeld("Shift",true,captured)&&platform.armNextStroke("Shift"),"Shift joins same next stroke");
        SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetForegroundWindow(window);settle();
        RECT client{};GetClientRect(window,&client);
        POINT point{(client.right-client.left)/2,(client.bottom-client.top)/2};ClientToScreen(window,&point);
        check(SetCursorPos(point.x,point.y),"move pointer inside owned receiver");settle();
        const auto pen=[&](bool down,ULONG_PTR signature){
            if(GetForegroundWindow()!=window||WindowFromPoint(point)!=window)std::cerr<<"receiver="<<window<<" foreground="<<GetForegroundWindow()<<" hit="<<WindowFromPoint(point)<<" at "<<point.x<<","<<point.y<<'\n';
            check(GetForegroundWindow()==window&&WindowFromPoint(point)==window,"pen test stays inside owned receiver");
            INPUT input{};input.type=INPUT_MOUSE;input.mi.dwFlags=down?MOUSEEVENTF_LEFTDOWN:MOUSEEVENTF_LEFTUP;input.mi.dwExtraInfo=signature;
            check(SendInput(1,&input,sizeof(input))==1,"send tagged pointer to owned receiver");settle();
        };
        pen(false,0xff515701);
        check(platform.heldModifiers().size()==2,"initial selection pen up does not release armed modifiers");
        pen(true,0);pen(false,0);pen(true,0xff515781);pen(false,0xff515781);
        check(platform.heldModifiers().size()==2,"ordinary mouse and touch promotion do not consume pen stroke");
        pen(true,0xff515701);
        check((GetAsyncKeyState(VK_CONTROL)&0x8000)&&(GetAsyncKeyState(VK_SHIFT)&0x8000),"native modifiers remain held throughout pen contact");
        pen(false,0xff515701);
        check(platform.heldModifiers().isEmpty()&&!(GetAsyncKeyState(VK_CONTROL)&0x8000)&&!(GetAsyncKeyState(VK_SHIFT)&0x8000),"native pen up releases every armed modifier");
        captured=platform.captureForegroundTarget();check(platform.setModifierHeld("Alt",true,captured),"hold before target exits");
        PostMessageW(window,WM_CLOSE,0,0);child.waitForFinished(3000);settle(100);
        check(platform.heldModifiers().isEmpty()&&!(GetAsyncKeyState(VK_MENU)&0x8000),"target loss releases held key");
        std::cout<<"PASS: "<<checks<<" native modifier checks\n";
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';result=1;}
    if(window&&IsWindow(window))PostMessageW(window,WM_CLOSE,0,0);
    if(child.state()!=QProcess::NotRunning&&!child.waitForFinished(3000)){child.kill();child.waitForFinished();}
    if(original&&IsWindow(original))SetForegroundWindow(original);
    SetCursorPos(originalCursor.x,originalCursor.y);
    return result;
}
