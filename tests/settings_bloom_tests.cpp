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
#include <QTabletEvent>
#include <QPointingDevice>
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
        QStringList soundCues;
        QObject::connect(studio.findChild<tabletpro::AudioFeedback*>("audioFeedback"),&tabletpro::AudioFeedback::cuePlayed,[&](const QString&,const QString& cue,bool){soundCues.append(cue);});
        studio.setSoundEnabled(true);
        expect(overlay->rendererInterface()->graphicsApi()!=QSGRendererInterface::Software,"visual check uses a hardware shader backend");
        auto* animation=overlay->findChild<QObject*>("settingsBloomAnimation");
        auto* surface=overlay->findChild<QQuickItem*>("runtimeSurface");
        auto* ring=overlay->findChild<QQuickItem*>("settingsSurface");
        auto* rail=overlay->findChild<QQuickItem*>("overlayRail");
        auto* panel=overlay->findChild<QQuickItem*>("railSettingsPanel");
        auto* toolsCenter=overlay->findChild<QQuickItem*>("radialToolCenter");
        auto* settingsCenter=overlay->findChild<QQuickItem*>("radialSettingsCenter");
        auto* hub=overlay->findChild<QQuickItem*>("overlayToolbar");
        auto* envelope=overlay->findChild<QQuickItem*>("settingsRippleEnvelope");
        expect(animation&&surface&&ring&&rail&&panel,"both layouts exist");
        expect(toolsCenter&&settingsCenter&&hub&&envelope,"ripple visuals and stable center input exist");
        const QString out=QDir::currentPath()+"/settings-bloom-captures";QDir().mkpath(out);
        QPointingDevice pen("center pressure test",7001,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
        quint64 penTimestamp=1000;
        const auto penEvent=[&](QEvent::Type type,QPointF point){const bool up=type==QEvent::TabletRelease;
            QTabletEvent event(type,&pen,point,overlay->mapToGlobal(point.toPoint()),up?0:.6,0,0,0,0,0,Qt::NoModifier,type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,up?Qt::NoButton:Qt::LeftButton);
            event.setTimestamp(penTimestamp+=20);QCoreApplication::sendEvent(overlay,&event);QCoreApplication::processEvents();};
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
            const auto closed=capture("closed");const int baseline=bronzePixels(closed);
            if(!docked){
                const auto center=studio.runtime()->radialCenter();penEvent(QEvent::TabletPress,center);wait(160);
                expect(!soundCues.isEmpty()&&soundCues.last()=="press","pen press plays the soft press sound before opening");
                const int heldCues=soundCues.size();
                std::cout<<"center hold: scale "<<toolsCenter->scale()<<" pressed "<<overlay->findChild<QObject*>("radialCenterTap")->property("pressed").toBool()<<" held "<<overlay->property("centerHeld").toBool()<<" ripple "<<overlay->property("rippleAllowed").toBool()<<" hub "<<hub->isVisible()<<" menu "<<overlay->property("menuOpen").toBool()<<"\n";
                expect(toolsCenter->scale()>1.075&&overlay->property("bloomProgress").toDouble()==0&&!overlay->property("menuOpen").toBool(),"stylus down expands the center without opening settings");
                expect(envelope->isVisible()&&overlay->property("ringRippleDistance").toDouble()==0,"held press preserves a bounded visual envelope without moving the outer buttons");
                wait(1050);capture("press-hold");
                expect(!overlay->property("menuOpen").toBool(),"a long hold does not fire the release action early");
                expect(soundCues.size()==heldCues,"holding does not repeat or start the opening sound");
                penEvent(QEvent::TabletRelease,center);
            }else QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);
            expect(overlay->property("menuOpen").toBool()&&!surface->isEnabled()&&!actions->isEnabled(),"no hidden tool or premature setting can fire during the fill");
            expect(!soundCues.isEmpty()&&soundCues.last()=="open","radial and dock open sound starts with the fill");
            expect(until([&]{return overlay->property("bloomProgress").toDouble()>.08;}),"bloom advances after the ellipsis");
            QMetaObject::invokeMethod(animation,"pause",Qt::DirectConnection);
            expect(animation->property("duration").toInt()==(docked?360:420),"settings use the faster release timing in both layouts");
            int last=baseline;
            for(const double progress:{.15,.45,.70}){
                overlay->setProperty("bloomProgress",progress);
                const int filled=bronzePixels(capture(QString::number(progress,'f',2)));
                std::cout<<mode.toStdString()<<" "<<progress<<" bronze pixels "<<filled<<'\n';
                expect(filled>last,"rendered bronze area grows at each sampled frame");last=filled;
                expect(bloom->mapRectToScene(bloom->boundingRect())==bounds,"fill never changes the menu footprint");
                if(docked)expect(bloom->width()==44&&rail->width()==44&&overlay->property("ringRippleDistance").toDouble()==0&&overlay->property("centerRippleScale").toDouble()==1,"docked fill stays 44 px wide without outward movement");
            }
            if(!docked){
                const auto toolBuilds=qobject_cast<RadialSurface*>(surface)->textureBuildCount();
                const auto settingBuilds=qobject_cast<RadialSurface*>(ring)->textureBuildCount();
                expect(hub->width()==76&&hub->height()==76&&hub->scale()==1,"the center click and drag target remains stable");
                overlay->setProperty("bloomProgress",.57);const auto released=capture("outward-ripple");
                expect(toolsCenter->scale()<1.03&&overlay->property("ringRippleDistance").toDouble()>5&&overlay->property("ringRippleDistance").toDouble()<=6,"outer buttons follow the center by only a few pixels");
                const auto topEdge=[](const QImage& image){for(int y=0;y<image.height()/2;++y)if(image.pixelColor(image.width()/2,y).alpha()>150)return y;return image.height();};
                expect(topEdge(released)<topEdge(closed)-3,"rendered outer edge actually moves outward after release");
                expect(qobject_cast<RadialSurface*>(surface)->textureBuildCount()==toolBuilds&&qobject_cast<RadialSurface*>(ring)->textureBuildCount()==settingBuilds,"ripple movement reuses sharp cached icon textures");
                expect(envelope->isVisible()&&envelope->width()==216,"ripple has a bounded envelope to prevent native clipping");
            }
            QMetaObject::invokeMethod(animation,"resume",Qt::DirectConnection);
            expect(until([&]{return overlay->property("settingsReady").toBool();}),"settings animation settles");
            expect(!surface->isVisible()&&actions->isEnabled(),"finished settings replace tools and become interactive");
            expect(bronzePixels(capture("open"))>=last,"final frame is fully bronze");
            expect(!envelope->isVisible(),"the temporary ripple envelope retires at rest");
            if(!docked)for(const auto& value:qobject_cast<RadialSurface*>(ring)->buttons()){
                const auto b=value.toMap();expect(qobject_cast<RadialSurface*>(ring)->hitRoot(b["cx"].toDouble(),b["cy"].toDouble())==b["id"].toString(),"settings hit targets follow their resting artwork");
            }
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);
            expect(!overlay->property("menuOpen").toBool()&&overlay->property("bloomProgress").toDouble()==1&&!surface->isEnabled()&&!actions->isEnabled(),"Back reverses from the fully open frame with input blocked");
            expect(soundCues.last()=="close","closing sound accompanies reverse fill");
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
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);expect(until([&]{return overlay->property("toolsReady").toBool();}),"reopened menu closes completely");
            expect(!overlay->property("menuOpen").toBool()&&overlay->property("bloomProgress").toDouble()==0,"closing mid-fill cannot reopen from an old animation");
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);wait(90);overlay->hide();wait(850);
            expect(!overlay->property("menuOpen").toBool()&&overlay->property("bloomProgress").toDouble()==0,"hiding cancels the fill");overlay->show();wait(60);
            studio.setReducedMotion(true);QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);
            expect(overlay->property("settingsReady").toBool()&&!animation->property("running").toBool(),"Reduced motion switches instantly");
            expect(overlay->property("centerRippleScale").toDouble()==1&&overlay->property("ringRippleDistance").toDouble()==0,"Reduced motion skips center and ring movement");
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);studio.setReducedMotion(false);
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);wait(90);studio.setReducedMotion(true);
            expect(overlay->property("settingsReady").toBool()&&!animation->property("running").toBool(),"enabling Reduced motion during a fill settles immediately");
            QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);studio.setReducedMotion(false);
            if(!docked){
                const auto center=studio.runtime()->radialCenter();penEvent(QEvent::TabletPress,center);wait(120);
                penEvent(QEvent::TabletMove,center+QPointF(12,3));wait(20);
                penEvent(QEvent::TabletMove,center+QPointF(45,20));wait(220);
                expect(studio.runtime()->dragging()&&toolsCenter->scale()<1.01,"dragging cancels center press feedback");
                penEvent(QEvent::TabletRelease,center+QPointF(45,20));wait(80);
                expect(!overlay->property("menuOpen").toBool()&&!studio.runtime()->dragging(),"releasing a center drag does not open settings");
            }
        }
        // Observe the real hardware-rendered handoff from assembly to housing.
        studio.runtime()->setReducedMotion(true);studio.runtime()->toggleDock();
        studio.runtime()->setReducedMotion(false);studio.runtime()->setDurationMs(1600);
        studio.runtime()->toggleDock();
        expect(until([&]{return studio.runtime()->dockProgress()>.8;}),"dock assembly advances");
        auto* railToggle=overlay->findChild<QQuickItem*>("railSettingsButton");
        expect(studio.runtime()->transitioning()&&rail->isVisible()&&rail->opacity()>0&&rail->opacity()<1&&!railToggle->isEnabled(),"housing fades in around landing icons without enabling controls mid-transition");
        const auto landingBounds=rail->mapRectToScene(rail->boundingRect());
        expect(until([&]{return studio.runtime()->dockProgress()>.95;}),"dock icons reach their landing positions");
        const auto landing=overlay->grabWindow();landing.save(out+"/dock-landing.png");
        expect(until([&]{return !studio.runtime()->transitioning();}),"dock transition completes");wait(70);
        const auto landed=overlay->grabWindow();landed.save(out+"/dock-landed.png");
        expect(rail->mapRectToScene(rail->boundingRect())==landingBounds&&rail->opacity()==1&&railToggle->isEnabled(),"housing keeps the same position as interaction returns");
        const qreal dockScale=qreal(landing.width())/overlay->width();int iconPixels=0;
        const auto white=[](QColor c){return c.alpha()>180&&c.red()>180&&c.green()>180&&c.blue()>180;};
        QVector<QPoint> iconPoints;
        for(int y=qCeil((landingBounds.top()+44)*dockScale);y<qFloor(landingBounds.bottom()*dockScale);++y)
            for(int x=qCeil(landingBounds.left()*dockScale);x<qFloor(landingBounds.right()*dockScale);++x){
                if(white(landed.pixelColor(x,y))){++iconPixels;iconPoints.append({x,y});}
            }
        // Compare registration, allowing the landing glow to fade: the unshifted
        // frame must match better than any one-physical-pixel displacement.
        const auto error=[&](int dx,int dy){qint64 sum=0;for(const auto p:iconPoints){
            const auto a=landing.pixelColor(p+QPoint(dx,dy)),b=landed.pixelColor(p);
            sum+=std::abs(a.red()-b.red())+std::abs(a.green()-b.green())+std::abs(a.blue()-b.blue());
        }return sum;};
        const auto alignedError=error(0,0);bool registered=true;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy)registered&=alignedError*2<error(dx,dy);
        std::cout<<"Dock landing: "<<iconPixels<<" icon pixels, registered "<<registered<<"\n";
        expect(iconPixels>100&&registered,"native icon pixels stay aligned when the dock appears");
        studio.runtime()->toggleDock();wait(150);
        expect(rail->isVisible()&&rail->opacity()>0&&!railToggle->isEnabled(),"undocking reverses the housing handoff");
        studio.runtime()->toggleDock();
        expect(until([&]{return !studio.runtime()->transitioning();})&&rail->opacity()==1,"reversing mid-undock restores the same housing");
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
            const auto frame=overlay->grabWindow();const auto r=rail->mapRectToScene(rail->boundingRect());const qreal s=qreal(frame.width())/overlay->width();
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
