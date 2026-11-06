#include "app/StudioController.h"
#include "app/DisplayRecovery.h"
#include "platform/BackdropEffect.h"
#include "ui/RadialSurface.h"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSettings>
#include <QEventLoop>
#include <QTimer>
#include <QDir>
#include <QImage>
#include <QPainterPath>
#include <QScreen>
#include <iostream>
#include <stdexcept>
#include <functional>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
int checks=0;
void expect(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
void wait(int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
bool until(const std::function<bool()>& predicate){for(int i=0;i<80;++i){if(predicate())return true;wait(25);}return predicate();}
int bronzePixels(const QImage& image){
    int count=0;
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){const auto c=image.pixelColor(x,y);
        if(c.alpha()>100&&c.red()>c.blue()*1.4&&c.red()>c.green()*1.2)++count;
    }
    return count;
}
}
int main(int argc,char**argv){
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QQuickStyle::setStyle("Basic");
    app.setOrganizationName("TabletProTests");app.setApplicationName("SettingsBloom");
    QTemporaryDir temp;QStandardPaths::setTestModeEnabled(true);QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
    try{
        tabletpro::StudioController studio(nullptr,temp.path()+"/profile",true);
        studio.setReducedMotion(false);studio.setBackdropEnabled(false);
        qmlRegisterType<RadialSurface>("TabletPro",1,0,"RadialSurface");
        qmlRegisterUncreatableType<tabletpro::MenuSession>("TabletPro",1,0,"MenuSession","Provided");
        qmlRegisterUncreatableType<tabletpro::StylusInput>("TabletPro",1,0,"StylusInput","Provided");
        QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&studio);
        engine.rootContext()->setContextProperty("startInEditor",false);
        engine.load(QUrl("qrc:/qml/Main.qml"));expect(!engine.rootObjects().isEmpty(),"load settings UI");
        auto* editor=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        auto* overlay=editor->findChild<QQuickWindow*>("runtimeOverlay");expect(overlay,"overlay exists");
        overlay->setGeometry(100,100,720,600);studio.runtime()->setViewport(720,600);studio.runtime()->setRadialCenter(360,300);
        overlay->show();wait(150);
        expect(overlay->rendererInterface()->graphicsApi()!=QSGRendererInterface::Software,"visual check uses a hardware shader backend");
        auto* animation=overlay->findChild<QObject*>("settingsBloomAnimation");
        auto* surface=overlay->findChild<QQuickItem*>("runtimeSurface");
        auto* ring=overlay->findChild<QQuickItem*>("settingsSurface");
        auto* rail=overlay->findChild<QQuickItem*>("overlayRail");
        auto* panel=overlay->findChild<QQuickItem*>("railSettingsPanel");
        expect(animation&&surface&&ring&&rail&&panel,"both layouts exist");
        const QString out=QDir::currentPath()+"/settings-bloom-captures";QDir().mkpath(out);
        for(const bool docked:{false,true}){
            if(docked){studio.runtime()->setReducedMotion(true);studio.runtime()->toggleDock();studio.runtime()->setReducedMotion(false);wait(100);}
            const QString mode=docked?"rail":"radial";
            auto* button=overlay->findChild<QObject*>(docked?"railSettingsButton":"moreMenuButton");
            auto* bloom=overlay->findChild<QQuickItem*>(docked?"railSettingsBloom":"radialSettingsBloom");
            auto* actions=docked?panel:ring;
            const QRectF bounds=bloom->mapRectToScene(bloom->boundingRect());
            const auto capture=[&](const QString& name){
                wait(45);const auto full=overlay->grabWindow();const qreal scale=qreal(full.width())/overlay->width();
                const auto pixels=full.copy(QRectF(bounds.x()*scale,bounds.y()*scale,bounds.width()*scale,bounds.height()*scale).toAlignedRect());
                expect(!pixels.isNull(),"native frame captured");pixels.save(out+"/"+mode+"-"+name+".png");return pixels;
            };
            const int baseline=bronzePixels(capture("closed"));
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);
            expect(overlay->property("menuOpen").toBool()&&!surface->isEnabled()&&!actions->isEnabled(),"no hidden tool or premature setting can fire during the fill");
            expect(until([&]{return overlay->property("bloomProgress").toDouble()>.08;}),"bloom advances after the ellipsis");
            QMetaObject::invokeMethod(animation,"pause",Qt::DirectConnection);
            int last=baseline;
            for(const double progress:{.15,.45,.70}){
                overlay->setProperty("bloomProgress",progress);
                const int filled=bronzePixels(capture(QString::number(progress,'f',2)));
                std::cout<<mode.toStdString()<<" "<<progress<<" bronze pixels "<<filled<<'\n';
                expect(filled>last,"rendered bronze area grows at each sampled frame");last=filled;
                expect(bloom->mapRectToScene(bloom->boundingRect())==bounds,"fill never changes the menu footprint");
                if(docked)expect(bloom->width()==44&&rail->width()==44,"docked fill stays 44 px wide");
            }
            QMetaObject::invokeMethod(animation,"resume",Qt::DirectConnection);
            expect(until([&]{return overlay->property("settingsReady").toBool();}),"760 ms animation settles");
            expect(!surface->isVisible()&&actions->isEnabled(),"finished settings replace tools and become interactive");
            expect(bronzePixels(capture("open"))>=last,"final frame is fully bronze");
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);
            expect(!overlay->property("menuOpen").toBool()&&overlay->property("bloomProgress").toDouble()==1&&!surface->isEnabled()&&!actions->isEnabled(),"Back reverses from the fully open frame with input blocked");
            expect(until([&]{return overlay->property("bloomProgress").toDouble()<.75;}),"closing bloom moves backward");
            const int closingEarly=bronzePixels(capture("closing-early"));
            expect(until([&]{return overlay->property("bloomProgress").toDouble()<.3;}),"closing bloom keeps contracting");
            expect(bronzePixels(capture("closing-late"))<closingEarly,"rendered bronze recedes toward the ellipsis");
            expect(until([&]{return overlay->property("toolsReady").toBool();})&&surface->isEnabled()&&!bloom->isVisible(),"tools enable only after reverse bloom finishes");
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);wait(100);
            const double partial=overlay->property("bloomProgress").toDouble();
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);
            expect(partial>0&&partial<1&&overlay->property("bloomProgress").toDouble()==partial,"closing mid-fill reverses without resetting its current progress");
            wait(35);const double reversing=overlay->property("bloomProgress").toDouble();
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);
            expect(overlay->property("bloomProgress").toDouble()==reversing,"reopening during reverse continues without a jump");
            expect(until([&]{return overlay->property("settingsReady").toBool();}),"reopened bloom reaches its open endpoint");
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);wait(850);
            expect(!overlay->property("menuOpen").toBool()&&overlay->property("bloomProgress").toDouble()==0,"closing mid-fill cannot reopen from an old animation");
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);wait(90);overlay->hide();wait(850);
            expect(!overlay->property("menuOpen").toBool()&&overlay->property("bloomProgress").toDouble()==0,"hiding cancels the fill");overlay->show();wait(60);
            studio.setReducedMotion(true);QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);
            expect(overlay->property("settingsReady").toBool()&&!animation->property("running").toBool(),"Reduced motion switches instantly");
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);studio.setReducedMotion(false);
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);wait(90);studio.setReducedMotion(true);
            expect(overlay->property("settingsReady").toBool()&&!animation->property("running").toBool(),"enabling Reduced motion during a fill settles immediately");
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);studio.setReducedMotion(false);
        }
        tabletpro::BackdropEffect backdrop;backdrop.configure(overlay);
        tabletpro::DisplayRecovery recovery(overlay,studio.runtime(),&backdrop);
        int refreshes=0;
        const auto mask=[&]{
            ++refreshes;
            QPainterPath path;path.addRoundedRect(rail->mapRectToScene(rail->boundingRect()),14,14);
            overlay->setMask(QRegion(path.toFillPolygon().toPolygon()));
            backdrop.setDiffusion(11);
            if(!recovery.pending())backdrop.setEnabled(true);
        };
        QObject::connect(&recovery,&tabletpro::DisplayRecovery::refreshMask,overlay,mask);
        mask();wait(150);
        const auto cropRail=[&]{
            const auto frame=overlay->grabWindow();const auto r=rail->mapRectToScene(rail->boundingRect());const qreal s=frame.devicePixelRatio();
            return frame.copy(QRectF(r.x()*s,r.y()*s,r.width()*s,r.height()*s).toAlignedRect());
        };
        const auto intact=cropRail();expect(!intact.isNull(),"dock pixels available before display reset");
        const int initialRefreshes=refreshes;
        for(const UINT kind:{WM_DISPLAYCHANGE,WM_DPICHANGED,WM_DWMCOMPOSITIONCHANGED}){
            MSG message{};message.hwnd=reinterpret_cast<HWND>(overlay->winId());message.message=kind;qintptr result=0;
            expect(!recovery.nativeEventFilter("windows_generic_MSG",&message,&result),"display observer leaves native messages available to Qt");
        }
        expect(!backdrop.requestAccepted(),"display change immediately removes stale Glass surface");
        expect(until([&]{return !recovery.pending();}),"display changes settle and rebuild");wait(200);
        expect(refreshes==initialRefreshes+2,"a burst of display changes produces one rebuild");
        expect(overlay->isVisible()&&overlay->geometry()==overlay->screen()->availableGeometry(),"recovered overlay fits current screen work area");
        expect(overlay->mask().boundingRect().width()==44&&backdrop.requestAccepted(),"dock mask and Glass return after display recovery");
        const auto restored=cropRail();int different=0;
        expect(restored.size()==intact.size(),"dock keeps its dimensions after graphics recovery");
        for(int y=0;y<intact.height();++y)for(int x=0;x<intact.width();++x)if(restored.pixelColor(x,y)!=intact.pixelColor(x,y))++different;
        expect(different<intact.width()*intact.height()/20,"dock artwork survives graphics reset without missing strips");
        restored.save(out+"/rail-display-recovered.png");
        recovery.requestRecovery();overlay->hide();wait(350);
        expect(!overlay->isVisible()&&!backdrop.requestAccepted(),"pending recovery never reopens a user-hidden menu");
        std::cout<<checks<<" native settings and display recovery checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}
}
