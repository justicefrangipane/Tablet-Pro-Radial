#include "StudioController.h"
#include "ProfileCodec.h"
#include "DesktopShell.h"
#include "ui/RadialSurface.h"
#include "platform/StylusInput.h"
#include "platform/BackdropEffect.h"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QScreen>
#include <QStandardPaths>
#include <QTimer>
#include <QRegion>
#include <QDir>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickStyle>
#include <QCursor>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QPainterPath>
#include <QLineF>
#include <cmath>
#include <cstring>

using namespace tabletpro;

int main(int argc,char**argv) {
    for(int i=1;i<argc;++i)if(std::strcmp(argv[i],"--pen-helper")==0)return StylusInput::runHelper();
    QApplication app(argc,argv);
    QQuickStyle::setStyle("Basic");
    app.setOrganizationName("TabletPro");app.setApplicationName("RadialStudio");app.setApplicationVersion(TABLETPRO_VERSION);
    app.setWindowIcon(DesktopShell::icon());
    const QStringList args=app.arguments();
    const int exportIndex=args.indexOf("--export-default");
    if(exportIndex>=0&&exportIndex+1<args.size()){
        QSaveFile file(args[exportIndex+1]);const auto bytes=encodeProfile(radial::defaultProfile());
        return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit()?0:5;
    }
    const bool smoke=args.contains("--smoke");
    const bool startInEditor=smoke||args.contains("--editor");
    QTemporaryDir smokeDirectory;
    if(smoke){
        if(!smokeDirectory.isValid())return 4;
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,smokeDirectory.path()+"/settings");
    }
    StudioController studio(nullptr,smoke?smokeDirectory.path()+"/profile":QString{},smoke);
    if(args.contains("--glass"))studio.setBackdropEnabled(true);
    qmlRegisterType<RadialSurface>("TabletPro",1,0,"RadialSurface");
    qmlRegisterUncreatableType<MenuSession>("TabletPro",1,0,"MenuSession","Sessions are provided by the studio.");
    qmlRegisterUncreatableType<StylusInput>("TabletPro",1,0,"StylusInput","Stylus input is provided by the studio.");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("studio",&studio);
    engine.rootContext()->setContextProperty("startInEditor",startInEditor);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if(engine.rootObjects().isEmpty())return 2;
    auto* editor=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    auto* overlay=editor->findChild<QQuickWindow*>("runtimeOverlay");
    if(!overlay){qCritical()<<"Missing runtime overlay";return 3;}
    DesktopShell desktop(&studio,overlay,nullptr,smoke);
    app.setQuitOnLastWindowClosed(!desktop.trayAvailable());
    engine.rootContext()->setContextProperty("desktop",&desktop);
    QObject::connect(&desktop,&DesktopShell::quitRequested,editor,[editor]{QMetaObject::invokeMethod(editor,"requestAppQuit");});
    QObject::connect(&desktop,&DesktopShell::exitApproved,&app,&QCoreApplication::quit);
    studio.platform()->configureOverlay(overlay);
    BackdropEffect backdrop;
    backdrop.configure(overlay);
    if(!smoke){studio.platform()->registerToggleHotkey();studio.stylus()->start();}
    auto* surface=overlay->findChild<RadialSurface*>("runtimeSurface");
    auto* settingsSurface=overlay->findChild<RadialSurface*>("settingsSurface");
    const auto sector=[](QPointF origin,double axis,double inner,double outer,double halfAngle) {
        QPolygonF points;
        for(int i=0;i<=24;++i){const auto a=axis-halfAngle+2*halfAngle*i/24;points<<origin+QPointF(std::cos(a)*outer,std::sin(a)*outer);}
        for(int i=24;i>=0;--i){const auto a=axis-halfAngle+2*halfAngle*i/24;points<<origin+QPointF(std::cos(a)*inner,std::sin(a)*inner);}
        return QRegion(points.toPolygon(),Qt::WindingFill);
    };
    auto updateMask=[&] {
        if(!overlay->isVisible())return;
        QRegion region;
        for(const auto* name:{"overlayRail","overlayToolbar","overlayMorePanel","overlayResult"}) {
            const auto* item=overlay->findChild<QQuickItem*>(name);
            if(!item||!item->isVisible())continue;
            auto rect=item->mapRectToScene(item->boundingRect());
            const bool rail=name==QString("overlayRail");
            if(!rail)rect.adjust(-2,-2,2,2);
            const qreal radius=rail?14:name==QString("overlayToolbar")?38:12;
            QPainterPath path;path.addRoundedRect(rect,radius,radius);
            region+=QRegion(path.toFillPolygon().toPolygon());
        }
        const auto* session=studio.runtime();const qreal p=session->dockProgress();
        const bool moving=session->transitioning()||(surface&&surface->childAnimating());
        // Clear the compositor material before a motion envelope broadens the
        // input mask. Otherwise an intermediate frame could frost the monitor.
        if(moving)backdrop.setEnabled(false);
        if(session->transitioning())region+=QRect(0,0,overlay->width(),overlay->height());
        else if(p<=0){
            const auto center=session->radialCenter();
            for(const auto& value:settingsSurface&&settingsSurface->isVisible()?settingsSurface->buttons():session->buttons())region+=sector(center,value.toMap()["angle"].toDouble(),41,101,3.141592653589793/6-.02);
        }
        else if(!surface||!surface->continuousRail())for(const auto& value:session->buttons()){
            const auto b=value.toMap();QPainterPath path;
            path.addRoundedRect(QRectF(b["dockX"].toDouble()-25,b["dockY"].toDouble()-25,50,50),12,12);
            region+=QRegion(path.toFillPolygon().toPolygon());
        }
        if(surface&&surface->childAnimating())region+=surface->visualBounds().toAlignedRect();
        else for(const auto& value:session->childButtons()){
            const auto b=value.toMap();
            region+=sector({b["fromX"].toDouble(),b["fromY"].toDouble()},b["axis"].toDouble(),radial::GestureEngine::ChildInnerRadius-3,radial::GestureEngine::ChildOuterRadius+3,(radial::GestureEngine::ChildHalfAngleDegrees+1)*3.141592653589793/180);
        }
        if(region!=overlay->mask())overlay->setMask(region);
        if(!moving)backdrop.setEnabled(studio.backdropEnabled());
    };
    QObject::connect(studio.runtime(),&MenuSession::geometryChanged,&app,updateMask);
    QObject::connect(studio.runtime(),&MenuSession::stateChanged,&app,updateMask);
    QObject::connect(&studio,&StudioController::changed,&app,updateMask);
    if(surface)QObject::connect(surface,&RadialSurface::visualBoundsChanged,&app,updateMask);
    if(settingsSurface)QObject::connect(settingsSurface,&QQuickItem::visibleChanged,&app,updateMask);
    for(const auto* name:{"overlayRail","overlayToolbar","overlayMorePanel","overlayResult"})if(auto* item=overlay->findChild<QQuickItem*>(name)){
        QObject::connect(item,&QQuickItem::visibleChanged,&app,updateMask);
        QObject::connect(item,&QQuickItem::xChanged,&app,updateMask);
        QObject::connect(item,&QQuickItem::yChanged,&app,updateMask);
        QObject::connect(item,&QQuickItem::widthChanged,&app,updateMask);
        QObject::connect(item,&QQuickItem::heightChanged,&app,updateMask);
    }
    QObject::connect(&studio,&StudioController::showRuntimeRequested,&app,[&](quintptr target,bool toggle) {
        if(overlay->isVisible()&&toggle){studio.runtime()->cancel();overlay->hide();return;}
        QScreen* screen=QGuiApplication::screenAt(QCursor::pos());if(!screen)screen=editor->screen();
        if(!screen)screen=QGuiApplication::primaryScreen();
        if(!screen)return;
        const auto available=screen->availableGeometry();
        overlay->setGeometry(available);
        studio.runtime()->setViewport(available.width(),available.height());
        const auto cursor=QCursor::pos()-available.topLeft();
        studio.runtime()->setRadialCenter(cursor.x(),cursor.y());
        studio.runtime()->setInvocationTarget(target);
        backdrop.setEnabled(false); // A hidden window may retain a previous motion mask.
        overlay->show(); // Runtime is explicitly non-activating. Never requestActivate here.
        updateMask();
    });
    QObject::connect(&studio,&StudioController::hideRuntimeRequested,&app,[&] {overlay->hide();editor->show();editor->requestActivate();});
    QObject::connect(&studio,&StudioController::dismissRuntimeRequested,&app,[&] {overlay->hide();});
    QObject::connect(&app,&QGuiApplication::aboutToQuit,&app,[&] {studio.runtime()->cancel();studio.stylus()->stop();studio.platform()->unregisterToggleHotkey();});
    if(args.contains("--docked")){
        studio.runtime()->setReducedMotion(true);studio.runtime()->toggleDock();
        studio.runtime()->setReducedMotion(studio.reducedMotion());
    }
    if(!startInEditor)QTimer::singleShot(0,&studio,&StudioController::showOverlay);
    if(smoke) {
        int index=args.indexOf("--output");const QString output=index>=0&&index+1<args.size()?args[index+1]:QDir::currentPath();QDir().mkpath(output);
        index=args.indexOf("--size");if(index>=0&&index+1<args.size()){const auto parts=args[index+1].split('x');if(parts.size()==2)editor->resize(parts[0].toInt(),parts[1].toInt());}
        QTimer::singleShot(1200,&app,[&,output] {
            const bool saved=editor->grabWindow().save(output+"/editor.png");
            qInfo().noquote()<<QJsonDocument(QJsonObject{{"qmlLoaded",true},{"screenshot",saved},{"width",editor->width()},{"height",editor->height()},{"roots",studio.preview()->buttons().size()}}).toJson(QJsonDocument::Compact);
            studio.preview()->setProfile(radial::legacyDefaultProfile());
            studio.preview()->replayDock();
            QTimer::singleShot(750,&app,[&,output] {editor->grabWindow().save(output+"/docking.png");});
            QTimer::singleShot(1900,&app,[&,output,saved] {
                // Screenshot/render work can delay animation ticks on Windows.
                // Wait for completion rather than assuming a wall-clock delay.
                if(studio.preview()->transitioning()){
                    QEventLoop wait;QTimer timeout;timeout.setSingleShot(true);
                    QObject::connect(studio.preview(),&MenuSession::stateChanged,&wait,[&]{if(!studio.preview()->transitioning())wait.quit();});
                    QObject::connect(&timeout,&QTimer::timeout,&wait,&QEventLoop::quit);
                    timeout.start(6000);wait.exec();
                }
                const bool docked=studio.preview()->docked()&&!studio.preview()->transitioning();editor->grabWindow().save(output+"/docked.png");
                QVariantMap text;
                for(const auto& value:studio.preview()->buttons())if(value.toMap()["id"]=="text")text=value.toMap();
                const double x=text["dockX"].toDouble(),y=text["dockY"].toDouble();
                studio.selectNode("text");studio.preview()->pointerPressed(91,x,y);studio.preview()->pointerMoved(91,x+42,y);
                const auto children=studio.preview()->childButtons();
                if(children.size()!=6){qCritical()<<"Expected six Text children";app.exit(6);return;}
                const auto bold=children.first().toMap();studio.preview()->pointerMoved(91,bold["cx"].toDouble(),bold["cy"].toDouble());
                QTimer::singleShot(200,&app,[&,output,saved,docked,bold] {
                    editor->grabWindow().save(output+"/nested.png");
                    studio.preview()->pointerReleased(91,bold["cx"].toDouble(),bold["cy"].toDouble());
                    const bool committed=studio.preview()->status()=="Preview: Ctrl+B";
                    QVariantMap settings;
                    for(const auto& value:studio.preview()->buttons())if(value.toMap()["id"]=="settings")settings=value.toMap();
                    const double sx=settings["dockX"].toDouble(),sy=settings["dockY"].toDouble();
                    studio.preview()->pointerPressed(92,sx,sy);studio.preview()->pointerMoved(92,sx+42,sy);
                    const bool lastGroupFits=studio.preview()->childButtons().size()==5;
                    studio.preview()->cancel();
                    qInfo().noquote()<<QJsonDocument(QJsonObject{{"dockCompleted",docked},{"sixChildren",true},{"previewCommitted",committed},{"lastDockGroupFits",lastGroupFits}}).toJson(QJsonDocument::Compact);
                    const bool previewPassed=saved&&docked&&committed&&lastGroupFits;
                    if(!previewPassed||!args.contains("--runtime-smoke")){app.exit(previewPassed?0:7);return;}
                    studio.showOverlay();editor->hide();
                    QTimer::singleShot(250,&app,[&,output]{
                        const bool shown=overlay->isVisible()&&overlay->grabWindow().save(output+"/runtime-floating.png");
                        const auto* hub=overlay->findChild<QQuickItem*>("overlayToolbar");
                        auto* mode=overlay->findChild<QObject*>("moreMenuButton");
                        auto* more=overlay->findChild<QObject*>("moreMenuButton");
                        const auto center=studio.runtime()->radialCenter();
                        const bool centered=hub&&QLineF(hub->position()+QPointF(hub->width()/2,hub->height()/2),center).length()<1&&mode&&mode->property("glyph")=="radial";
                        if(more)QMetaObject::invokeMethod(more,"clicked",Qt::DirectConnection);
                        const auto* morePanel=overlay->findChild<QQuickItem*>("settingsSurface");
                        const bool overflow=morePanel&&morePanel->isVisible()&&!surface->isVisible()&&settingsSurface->buttons().size()==6;
                        QEventLoop ringWait;QTimer::singleShot(260,&ringWait,&QEventLoop::quit);ringWait.exec();
                        overlay->grabWindow().save(output+"/runtime-more.png");
                        overlay->setProperty("menuOpen",false);
                        QMetaObject::invokeMethod(studio.stylus(),"sideButtonPressed",Qt::DirectConnection);
                        const bool hidden=!overlay->isVisible();
                        QMetaObject::invokeMethod(studio.stylus(),"sideButtonPressed",Qt::DirectConnection);
                        const bool reopened=overlay->isVisible();
                        studio.runtime()->setReducedMotion(true);
                        if(!studio.runtime()->docked())studio.runtime()->toggleDock();
                        QTimer::singleShot(150,&app,[&,output,shown,hidden,reopened,centered,overflow]{
                            const bool rail=studio.runtime()->docked()&&overlay->grabWindow().save(output+"/runtime-docked.png");
                            auto* housing=overlay->findChild<QQuickItem*>("overlayRail");
                            auto* railToggle=overlay->findChild<QObject*>("railSettingsButton");
                            const auto saveRail=[&](const QString& name){
                                if(!housing)return false;
                                const auto image=overlay->grabWindow();
                                const auto rect=housing->mapRectToScene(housing->boundingRect()).adjusted(-3,-3,3,3);
                                const qreal scale=qreal(image.width())/overlay->width();
                                return image.copy(QRectF(rect.x()*scale,rect.y()*scale,rect.width()*scale,rect.height()*scale).toAlignedRect()).save(output+"/"+name);
                            };
                            const bool narrowClosed=housing&&housing->width()==44&&overlay->mask().boundingRect().width()==44&&saveRail("runtime-rail-closed.png");
                            if(railToggle)QMetaObject::invokeMethod(railToggle,"clicked",Qt::DirectConnection);
                            QCoreApplication::processEvents();
                            QEventLoop settle;QTimer::singleShot(260,&settle,&QEventLoop::quit);settle.exec();
                            const auto* railPanel=overlay->findChild<QQuickItem*>("railSettingsPanel");
                            const bool narrowOpen=railPanel&&railPanel->isVisible()&&!surface->isVisible()&&overlay->mask().boundingRect().width()==44&&saveRail("runtime-rail-settings.png");
                            if(railToggle)QMetaObject::invokeMethod(railToggle,"clicked",Qt::DirectConnection);
                            const bool flips=narrowClosed&&narrowOpen&&surface->isVisible();
                            studio.runtime()->setReducedMotion(false);
                            studio.runtime()->setProfile(radial::legacyDefaultProfile());
                            QVariantMap text;
                            for(const auto& value:studio.runtime()->buttons())if(value.toMap()["id"]=="text")text=value.toMap();
                            const double x=text["dockX"].toDouble(),y=text["dockY"].toDouble();
                            studio.runtime()->pointerPressed(94,x,y);studio.runtime()->pointerMoved(94,x+42,y);
                            const auto children=studio.runtime()->childButtons();
                            if(children.size()!=6){app.exit(9);return;}
                            const auto bold=children.first().toMap();
                            studio.runtime()->pointerMoved(94,bold["cx"].toDouble(),bold["cy"].toDouble());
                            QTimer::singleShot(400,&app,[&,output,shown,hidden,reopened,rail,centered,overflow,flips,bold]{
                                const bool fan=overlay->grabWindow().save(output+"/runtime-fan.png");
                                studio.runtime()->pointerReleased(94,bold["cx"].toDouble(),bold["cy"].toDouble());
                                QTimer::singleShot(110,&app,[&,output]{overlay->grabWindow().save(output+"/runtime-dissolve.png");});
                                QTimer::singleShot(400,&app,[&,output,shown,hidden,reopened,rail,centered,overflow,flips,fan]{
                                    const bool delivered=studio.runtime()->status()=="Preview: Ctrl+B";
                                    studio.showEditor();
                                    auto* dialog=editor->findChild<QObject*>("penSetupDialog");
                                    const bool opened=dialog&&QMetaObject::invokeMethod(dialog,"open");
                                    QTimer::singleShot(200,&app,[&,output,shown,hidden,reopened,rail,centered,overflow,flips,fan,delivered,opened]{
                                        const bool penSetup=opened&&editor->grabWindow().save(output+"/pen-setup.png");
                                        qInfo().noquote()<<QJsonDocument(QJsonObject{{"runtimeShown",shown},{"sideButtonHides",hidden},{"sideButtonReopens",reopened},{"runtimeDocked",rail},{"centeredControls",centered},{"settingsReplaceOuterRing",overflow},{"continuousRailAndMask",flips},{"fan",fan},{"queuedActionDelivered",delivered},{"penSetup",penSetup}}).toJson(QJsonDocument::Compact);
                                        app.exit(shown&&hidden&&reopened&&rail&&centered&&overflow&&flips&&fan&&delivered&&penSetup?0:8);
                                    });
                                });
                            });
                        });
                    });
                });
            });
        });
    }
    return app.exec();
}
