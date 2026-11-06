// This test displays and captures only its own checkerboard test window.
// It verifies rendered pixels, not just successful compositor API returns.
#include "platform/BackdropEffect.h"
#include "ui/RadialSurface.h"
#include "app/MenuSession.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QImage>
#include <QPainter>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <iostream>
#include <stdexcept>
#include <cmath>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void settle(int ms=500){QElapsedTimer timer;timer.start();while(timer.elapsed()<ms){QCoreApplication::processEvents();QThread::msleep(10);}DwmFlush();}
LRESULT CALLBACK pattern(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(window,&ps);RECT bounds;GetClientRect(window,&bounds);bool flip=GetWindowLongPtrW(window,GWLP_USERDATA)!=0;
        for(int y=0;y<bounds.bottom;y+=12)for(int x=0;x<bounds.right;x+=12){
            bool odd=(x/12+y/12)%2;bool upper=(y<bounds.bottom/2)!=flip;
            COLORREF color=upper?(odd?RGB(25,170,240):RGB(240,225,100)):(odd?RGB(230,90,45):RGB(60,50,135));
            auto brush=CreateSolidBrush(color);RECT tile{x,y,x+12,y+12};FillRect(dc,&tile,brush);DeleteObject(brush);
        }EndPaint(window,&ps);return 0;
    }return DefWindowProcW(window,message,w,l);
}
QImage capture(HWND ownedBackground){
    RECT rect;GetWindowRect(ownedBackground,&rect);
    POINT corner{rect.left+3,rect.top+3};
    check(WindowFromPoint(corner)==ownedBackground,"test pattern must cover capture area");
    int width=rect.right-rect.left,height=rect.bottom-rect.top;
    auto screen=GetDC(nullptr);auto memory=CreateCompatibleDC(screen);void* bits=nullptr;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(memory,bitmap);
    bool copied=BitBlt(memory,0,0,width,height,screen,rect.left,rect.top,SRCCOPY|CAPTUREBLT);
    QImage image=QImage(static_cast<uchar*>(bits),width,height,QImage::Format_RGB32).copy();
    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,screen);
    check(copied&&!image.isNull(),"capture of owned test window must succeed");return image;
}
double difference(QColor a,QColor b){return std::abs(a.red()-b.red())+std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue());}
QColor mean(const QImage& image,QRect rect){double r=0,g=0,b=0,n=0;for(int y=rect.top();y<=rect.bottom();++y)for(int x=rect.left();x<=rect.right();++x){auto c=image.pixelColor(x,y);r+=c.red();g+=c.green();b+=c.blue();++n;}return QColor(int(r/n),int(g/n),int(b/n));}
double detail(const QImage& image,QRect rect){double sum=0,n=0;for(int y=rect.top();y<rect.bottom();++y)for(int x=rect.left();x<rect.right();++x){sum+=difference(image.pixelColor(x,y),image.pixelColor(x+1,y));sum+=difference(image.pixelColor(x,y),image.pixelColor(x,y+1));n+=2;}return sum/n;}
}
int main(int argc,char** argv){
    QGuiApplication app(argc,argv);HWND background=nullptr;
    try{
        QQuickWindow window;window.setFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::WindowDoesNotAcceptFocus);window.setColor(Qt::transparent);window.setGeometry(100,100,640,480);
        QRegion shape(QRect(200,120,240,240),QRegion::Ellipse);shape-=QRegion(QRect(270,190,100,100),QRegion::Ellipse);shape+=QRect(510,200,50,80);window.setMask(shape);window.show();settle(100);
        HWND overlay=reinterpret_cast<HWND>(window.winId());RECT bounds;GetWindowRect(overlay,&bounds);
        WNDCLASSW wc{};wc.lpfnWndProc=pattern;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"TabletProGlassOwnedPattern";RegisterClassW(&wc);
        background=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,wc.lpszClassName,L"Tablet Pro glass pixel test",WS_POPUP,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,wc.hInstance,nullptr);
        check(background!=nullptr,"create private test pattern");SetWindowPos(background,overlay,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);UpdateWindow(background);settle();
        const auto plain=capture(background);const HWND foreground=GetForegroundWindow();
        tabletpro::BackdropEffect glass;glass.setEnabled(false);glass.configure(&window);glass.setEnabled(true);settle(800);
        check(glass.requestAccepted(),"composition bridge must create a glass visual");const auto blurred=capture(background);
        const qreal scale=window.devicePixelRatio();
        auto point=[&](int x,int y){return QPoint(qRound(x*scale),qRound(y*scale));};
        auto rect=[&](int x,int y,int w,int h){return QRect(point(x,y),QSize(qRound(w*scale),qRound(h*scale)));};
        for(QPoint p:{point(15,15),point(320,240),point(460,240),point(205,125)})check(difference(plain.pixelColor(p),blurred.pixelColor(p))<3,"outside, gap, and center hole must remain visually clear");
        const QRect top=rect(290,143,60,15),bottom=rect(290,322,60,15);
        const auto before=detail(plain,top),after=detail(blurred,top);
        check(before>15&&after<before*.25,"glass must blur the pattern, not merely tint it");
        check(difference(mean(blurred,top),mean(blurred,bottom))>80,"glass must retain background colors, not a solid fallback");
        SetWindowLongPtrW(background,GWLP_USERDATA,1);InvalidateRect(background,nullptr,FALSE);settle(600);const auto changed=capture(background);
        check(difference(mean(blurred,top),mean(changed,top))>80,"glass must update from the live background");
        glass.setEnabled(false);settle();const auto disabled=capture(background);
        check(detail(disabled,top)>15,"Glass off must restore sharp background");
        check(GetForegroundWindow()==foreground,"visual glass test must preserve focus");

        tabletpro::MenuSession session(true,nullptr);session.setProfile(radial::defaultProfile());session.setViewport(640,480);session.setRadialCenter(320,240);
        RadialSurface surface;surface.setSize({640,480});surface.setButtons(session.buttons());
        QImage opaque(640,480,QImage::Format_ARGB32_Premultiplied);opaque.fill(Qt::transparent);{QPainter p(&opaque);surface.paint(&p);}
        surface.setGlassEnabled(true);QImage translucent(640,480,QImage::Format_ARGB32_Premultiplied);translucent.fill(Qt::transparent);{QPainter p(&translucent);surface.paint(&p);}
        check(opaque.pixelColor(335,155).alpha()>250,"Glass off root body stays solid");
        const int alpha=translucent.pixelColor(335,155).alpha();check(alpha>60&&alpha<190,"Glass on root body must expose the backdrop");
        int sharpIcon=0;for(int y=160;y<180;++y)for(int x=310;x<330;++x){auto c=translucent.pixelColor(x,y);if(c.alpha()>245&&c.red()>190)++sharpIcon;}
        check(sharpIcon>3,"glass icons must remain opaque and legible");
        QRegion menuShape;
        constexpr double pi=3.141592653589793;
        for(const auto& item:session.buttons()){
            const double axis=item.toMap()["angle"].toDouble();QPolygon points;
            for(int n=0;n<=24;++n){double a=axis-pi/6+.02+(pi/3-.04)*n/24;points<<QPoint(qRound(320+101*std::cos(a)),qRound(240+101*std::sin(a)));}
            for(int n=24;n>=0;--n){double a=axis-pi/6+.02+(pi/3-.04)*n/24;points<<QPoint(qRound(320+41*std::cos(a)),qRound(240+41*std::sin(a)));}
            menuShape+=QRegion(points);
        }
        window.setMask(menuShape);surface.setParentItem(window.contentItem());surface.setGlassEnabled(false);
        SetWindowLongPtrW(background,GWLP_USERDATA,0);InvalidateRect(background,nullptr,FALSE);settle();const auto menuOff=capture(background);
        surface.setGlassEnabled(true);glass.setEnabled(true);settle(700);const auto menuOn=capture(background);
        SetWindowLongPtrW(background,GWLP_USERDATA,1);InvalidateRect(background,nullptr,FALSE);settle(600);const auto menuChanged=capture(background);
        check(difference(menuOn.pixelColor(point(335,155)),menuChanged.pixelColor(point(335,155)))>30,"rendered Qt menu fill must reveal live glass colors");
        check(GetForegroundWindow()==foreground,"rendered menu must not take focus");
        const int output=app.arguments().indexOf("--output");if(output>=0&&output+1<app.arguments().size()){
            const QString path=app.arguments()[output+1];QDir().mkpath(path);plain.save(path+"/pattern.png");blurred.save(path+"/glass.png");changed.save(path+"/glass-live-change.png");menuOff.save(path+"/menu-off.png");menuOn.save(path+"/menu-on.png");
        }
        glass.setEnabled(false);window.hide();DestroyWindow(background);background=nullptr;
        std::cout<<checks<<" rendered-glass checks passed; pattern detail "<<before<<" -> "<<after<<"\n";return 0;
    }catch(const std::exception& error){if(background)DestroyWindow(background);std::cerr<<"Visual glass regression: "<<error.what()<<"\n";return 1;}
}
