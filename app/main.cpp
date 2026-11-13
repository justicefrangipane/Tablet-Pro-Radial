#include "StudioController.h"
#include "ProfileCodec.h"
#include "DesktopShell.h"
#include "DisplayRecovery.h"
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
#include "ui/RadialGeometry.h"
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
#include <QDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QJsonArray>
#include <cmath>
#include <cstring>

using namespace tabletpro;

int main(int argc,char**argv) {
    bool genericPen=false;for(int i=1;i<argc;++i)genericPen|=std::strcmp(argv[i],"--generic-pen")==0;
    for(int i=1;i<argc;++i)if(std::strcmp(argv[i],"--pen-helper")==0)return StylusInput::runHelper(genericPen);
    QApplication app(argc,argv);
    QQuickStyle::setStyle("Basic");
    app.setOrganizationName("TabletPro");app.setApplicationName("RadialStudio");app.setApplicationVersion(TABLETPRO_VERSION);
    app.setWindowIcon(DesktopShell::icon());
    const QStringList args=app.arguments();
    const int diagnosticIndex=args.indexOf("--pen-diagnostics");
    if(diagnosticIndex>=0){
        if(diagnosticIndex+1>=args.size())return 5;
        const auto output=args[diagnosticIndex+1];
        StylusInput pen;PenPositionTracker position;
        QDialog window;window.setWindowTitle("Radial — Pen input check");window.resize(620,380);
        auto* layout=new QVBoxLayout(&window);auto* label=new QLabel;label->setWordWrap(true);layout->addWidget(label);
        QJsonArray transitions;quint64 nativeDowns=0,nativeUps=0;QElapsedTimer elapsed;elapsed.start();
        const auto record=[&](QString source,bool down){
            transitions.append(QJsonObject{{"ms",double(elapsed.elapsed())},{"source",source},{"down",down}});
            while(transitions.size()>200)transitions.removeFirst();
        };
        QObject::connect(&position,&PenPositionTracker::contactObserved,&app,[&](quintptr source,bool down,bool){if(down)++nativeDowns;else ++nativeUps;record(QString("Windows %1").arg(source),down);});
        QObject::connect(&pen,&StylusInput::tipChanged,&app,[&](quintptr source,bool down){record(QString("DLL %1").arg(source),down);});
        QTimer refresh;refresh.setInterval(200);
        const auto update=[&]{
            const auto p=position.position();
            QString text="Hover over the screen, then draw and lift in your drawing app.\\nThis check observes pen input without triggering shortcuts.\\n\\n";
            text.replace("\\n","\n");
            const auto rows=pen.readings();
            for(const auto& row:rows){const auto r=row.toMap();text+=QString("%1: %2\nPackets %3  ·  tip down %4 / up %5\nTilt (DLL units) %6, %7  ·  pressure (raw) %8\nBarrel %9 / invert %10\n\n").arg(r["name"].toString(),r["status"].toString(),r["packets"].toString(),r["tipDowns"].toString(),r["tipUps"].toString(),r["tiltX"].toString(),r["tiltY"].toString(),r["pressure"].toString(),r["barrel"].toBool()?"down":"up",r["invert"].toBool()?"on":"off");}
            auto* penScreen=p?QGuiApplication::screenAt(p->toPoint()):nullptr;
            text+=QString("Windows pen down %1 / up %2\nPen position (logical px): %3").arg(nativeDowns).arg(nativeUps).arg(p?QString("%1, %2").arg(p->x(),0,'f',0).arg(p->y(),0,'f',0):"Waiting for hover");
            if(penScreen)text+=QString("\nDisplay scaling: %1% — coordinates match Radial's layout").arg(penScreen->devicePixelRatio()*100,0,'f',0);
            label->setText(text);
            QJsonObject report{{"version",TABLETPRO_VERSION},{"elapsedMs",double(elapsed.elapsed())},{"channels",QJsonArray::fromVariantList(rows)},
                {"nativeTipDowns",double(nativeDowns)},{"nativeTipUps",double(nativeUps)},{"events",transitions},{"penPositionKnown",bool(p)},
                {"positionUnits","Qt logical screen pixels"},{"tiltUnits","DLL device-scaled values; not calibrated degrees"},{"pressureUnits","raw HID values"}};
            if(penScreen)report["displayScale"]=penScreen->devicePixelRatio();
            if(p){report["penX"]=p->x();report["penY"]=p->y();}
            QSaveFile file(output);if(file.open(QIODevice::WriteOnly)){const auto bytes=QJsonDocument(report).toJson();if(file.write(bytes)==bytes.size())file.commit();}
        };
        QObject::connect(&refresh,&QTimer::timeout,&app,update);
        QObject::connect(&app,&QCoreApplication::aboutToQuit,&app,update);
        position.start();pen.setEnabled(true);pen.start();update();refresh.start();window.show();
        return app.exec();
    }
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
    desktop.setEditorWindow(editor);
    app.setQuitOnLastWindowClosed(!desktop.trayAvailable());
    engine.rootContext()->setContextProperty("desktop",&desktop);
    QObject::connect(&desktop,&DesktopShell::quitRequested,editor,[editor]{QMetaObject::invokeMethod(editor,"requestAppQuit");});
    QObject::connect(&desktop,&DesktopShell::exitApproved,&app,&QCoreApplication::quit);
    studio.platform()->configureOverlay(overlay);
    BackdropEffect backdrop;
    backdrop.configure(overlay);
    DisplayRecovery recovery(overlay,studio.runtime(),&backdrop);
    if(!smoke){studio.platform()->registerToggleHotkey();studio.stylus()->start();}
    auto* surface=overlay->findChild<RadialSurface*>("runtimeSurface");
    auto* settingsSurface=overlay->findChild<RadialSurface*>("settingsSurface");
    auto* rippleEnvelope=overlay->findChild<QQuickItem*>("settingsRippleEnvelope");
    const auto sector=[](QPointF origin,double axis,double inner,double outer,double halfAngle) {
        QPolygonF points;
        for(int i=0;i<=24;++i){const auto a=axis-halfAngle+2*halfAngle*i/24;points<<origin+QPointF(std::cos(a)*outer,std::sin(a)*outer);}
        for(int i=24;i>=0;--i){const auto a=axis-halfAngle+2*halfAngle*i/24;points<<origin+QPointF(std::cos(a)*inner,std::sin(a)*inner);}
        return QRegion(points.toPolygon(),Qt::WindingFill);
    };
    auto updateMask=[&] {
        const auto* session=studio.runtime();
        if(!session->transitioning()&&!session->dragging()&&rippleEnvelope&&rippleEnvelope->isVisible()){
            // One cached local Glass contour contains both the held press and
            // released ripple. The input envelope is independent of Glass.
            const QRegion motion(rippleEnvelope->mapRectToScene(rippleEnvelope->boundingRect()).toAlignedRect(),QRegion::Ellipse);
            backdrop.setVisualRegion(motion,50.5);
            if(motion!=overlay->mask())overlay->setMask(motion);
            if(!recovery.pending())backdrop.setEnabled(studio.backdropEnabled());
            return;
        }
        QRegion region,artworkCoverage;
        for(const auto* name:{"overlayRail","overlayToolbar","overlayMorePanel","overlayResult"}) {
            if(studio.runtime()->parentMenuOpen() && !studio.runtime()->docked() && name!=QString("overlayResult"))continue;
            const auto* item=overlay->findChild<QQuickItem*>(name);
            if(!item||!item->isVisible()||item->opacity()<.01)continue;
            auto rect=item->mapRectToScene(item->boundingRect());
            // QML emits x/y changes separately. Read the shared center so a
            // drag never builds an intermediate, misaligned Glass contour.
            if(name==QString("overlayToolbar"))rect=QRectF(session->radialCenter()-QPointF(item->width()/2,item->height()/2),QSizeF(item->width(),item->height()));
            const bool rail=name==QString("overlayRail");
            if(!rail)rect.adjust(-2,-2,2,2);
            const qreal radius=rail?14:name==QString("overlayToolbar")?38:12;
            QPainterPath path;path.addRoundedRect(rect,radius,radius);
            region+=QRegion(path.toFillPolygon().toPolygon());
        }
        const qreal p=session->dockProgress();
        if(session->transitioning()&&surface)region+=surface->motionGlassRegion(p);
        else if(p<=0 && !session->parentMenuOpen()){
            const auto center=session->radialCenter();
            for(const auto& value:settingsSurface&&settingsSurface->isVisible()?settingsSurface->buttons():session->buttons()){
                const auto angle=value.toMap()["angle"].toDouble();
                region+=sector(center,angle,41,101,3.141592653589793/6-.02);
                if(!session->dragging())artworkCoverage+=tabletpro::radialGeometry::nativeWedge(center,angle);
            }
        }
        else if((!session->parentMenuOpen() || session->docked()) && (!surface||!surface->continuousRail()))for(const auto& value:session->buttons()){
            const auto b=value.toMap();QPainterPath path;
            path.addRoundedRect(QRectF(b["dockX"].toDouble()-25,b["dockY"].toDouble()-25,50,50),12,12);
            region+=QRegion(path.toFillPolygon().toPolygon());
        }
        if(surface&&surface->childAnimating())region+=surface->visualBounds().toAlignedRect();
        else for(const auto& value:session->childButtons()){
            const auto b=value.toMap();
            region+=sector({b["fromX"].toDouble(),b["fromY"].toDouble()},b["axis"].toDouble(),radial::GestureEngine::ChildInnerRadius-3,radial::GestureEngine::ChildOuterRadius+3,(radial::GestureEngine::ChildHalfAngleDegrees+1)*3.141592653589793/180);
        }
        if(session->parentMenuOpen()){
            const auto center=session->parentCenter();QPainterPath hub;hub.addEllipse(center,40,40);
            region+=QRegion(hub.toFillPolygon().toPolygon());
            for(const auto& value:session->parentButtons()){
                const auto angle=value.toMap()["angle"].toDouble();
                region+=sector(center,angle,41,101,3.141592653589793/6-.02);
                if(!session->dragging()&&!session->transitioning())artworkCoverage+=tabletpro::radialGeometry::nativeWedge(center,angle);
            }
        }
        const qreal diffusion=session->parentMenuOpen()?50.5:50.5+(11-50.5)*p;
        backdrop.setVisualRegion(region,diffusion);
        // Keep the existing Glass contour. Only the native cutout needs room
        // for painted edge pixels; gestures still use the actual wedge bounds.
        const QRegion input=session->dragging()||session->transitioning()?QRegion(QRect(QPoint(),overlay->size())):region.united(artworkCoverage);
        if(input!=overlay->mask())overlay->setMask(input);
        if(!recovery.pending())backdrop.setEnabled(studio.backdropEnabled());
    };
    QObject::connect(&recovery,&DisplayRecovery::refreshMask,&app,updateMask);
    QObject::connect(studio.runtime(),&MenuSession::geometryChanged,&app,updateMask);
    QObject::connect(studio.runtime(),&MenuSession::stateChanged,&app,updateMask);
    QObject::connect(studio.runtime(),&MenuSession::draggingChanged,&app,updateMask);
    QObject::connect(&studio,&StudioController::changed,&app,updateMask);
    if(surface)QObject::connect(surface,&RadialSurface::visualBoundsChanged,&app,updateMask);
    if(settingsSurface)QObject::connect(settingsSurface,&QQuickItem::visibleChanged,&app,updateMask);
    for(const auto* name:{"overlayRail","overlayToolbar","overlayMorePanel","overlayResult","settingsRippleEnvelope"})if(auto* item=overlay->findChild<QQuickItem*>(name)){
        QObject::connect(item,&QQuickItem::visibleChanged,&app,updateMask);
        QObject::connect(item,&QQuickItem::xChanged,&app,updateMask);
        QObject::connect(item,&QQuickItem::yChanged,&app,updateMask);
        QObject::connect(item,&QQuickItem::widthChanged,&app,updateMask);
        QObject::connect(item,&QQuickItem::heightChanged,&app,updateMask);
    }
    QObject::connect(&studio,&StudioController::showRuntimeRequested,&app,[&](quintptr target,bool toggle) {
        if(overlay->isVisible()&&toggle){studio.runtime()->cancel();overlay->hide();return;}
        const auto invocationPoint=studio.platform()->penPosition().value_or(QPointF(QCursor::pos()));
        QScreen* screen=QGuiApplication::screenAt(invocationPoint.toPoint());if(!screen)screen=editor->screen();
        if(!screen)screen=QGuiApplication::primaryScreen();
        if(!screen)return;
        const auto available=screen->availableGeometry();
        overlay->setScreen(screen);
        overlay->setGeometry(available);
        studio.runtime()->setViewport(available.width(),available.height());
        const auto cursor=invocationPoint-available.topLeft();
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
    if(!smoke)QTimer::singleShot(0,&desktop,&DesktopShell::showWhatsNewIfNeeded);
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
                        const auto settleFrame=[](int ms){QEventLoop wait;QTimer::singleShot(ms,&wait,&QEventLoop::quit);wait.exec();};
                        studio.runtime()->moveRadialCenter(overlay->width()/2,overlay->height()/2);settleFrame(40);overlay->grabWindow();
                        const auto textureBuilds=surface->textureBuildCount();const auto dragStart=studio.runtime()->radialCenter();
                        studio.runtime()->beginRadialDrag();
                        const bool motionMask=overlay->mask()==QRegion(QRect(QPoint(),overlay->size()))&&backdrop.requestAccepted()&&backdrop.visualRegion()!=overlay->mask();
                        const auto dragGlassBuilds=backdrop.shapeBuildCount();
                        for(int i=1;i<=32;++i){studio.runtime()->moveRadialCenter(dragStart.x()+i*2,dragStart.y()+i);QCoreApplication::processEvents();}
                        const bool reused=overlay->grabWindow().save(output+"/runtime-dragging.png")&&surface->textureBuildCount()==textureBuilds&&backdrop.shapeBuildCount()==dragGlassBuilds;
                        studio.runtime()->endRadialDrag();settleFrame(60);
                        const bool settled=backdrop.enabled()&&overlay->mask().boundingRect().width()<overlay->width();
                        QVariantMap parent;for(const auto& value:studio.runtime()->buttons())if(value.toMap()["id"]=="tools")parent=value.toMap();
                        studio.runtime()->pointerPressed(95,parent["cx"].toDouble(),parent["cy"].toDouble());studio.runtime()->pointerReleased(95,parent["cx"].toDouble(),parent["cy"].toDouble());settleFrame(45);
                        const auto childArea=QRectF(studio.runtime()->radialCenter()-QPointF(104,104),QSizeF(208,208)).toAlignedRect();
                        const bool inPlace=studio.runtime()->parentInPlace()&&studio.runtime()->parentCenter()==studio.runtime()->radialCenter()
                            &&childArea.contains(overlay->mask().boundingRect())&&overlay->grabWindow().save(output+"/runtime-children-in-place.png");
                        studio.runtime()->parentBack();
                        qInfo().noquote()<<QJsonDocument(QJsonObject{{"dragMotionMask",motionMask},{"dragReusesTexture",reused},{"dragRestoresGlass",settled},{"childrenReplaceRing",inPlace}}).toJson(QJsonDocument::Compact);
                        if(!motionMask||!reused||!settled||!inPlace){app.exit(12);return;}
                        const auto* hub=overlay->findChild<QQuickItem*>("overlayToolbar");
                        auto* mode=overlay->findChild<QObject*>("moreMenuButton");
                        auto* more=overlay->findChild<QObject*>("moreMenuButton");
                        const auto center=studio.runtime()->radialCenter();
                        const bool centered=hub&&QLineF(hub->position()+QPointF(hub->width()/2,hub->height()/2),center).length()<1&&mode&&mode->property("glyph")=="more";
                        if(more)QMetaObject::invokeMethod(more,"clicked",Qt::DirectConnection);
                        const auto* morePanel=overlay->findChild<QQuickItem*>("settingsSurface");
                        settleFrame(100);
                        auto* bloomAnimation=overlay->findChild<QObject*>("settingsBloomAnimation");
                        QMetaObject::invokeMethod(bloomAnimation,"pause",Qt::DirectConnection);
                        overlay->setProperty("bloomProgress",.25);settleFrame(50);
                        const bool pressHeld=backdrop.requestAccepted()&&overlay->property("ringRippleDistance").toDouble()>4
                            &&overlay->grabWindow().save(output+"/runtime-ripple-press.png");
                        const auto rippleMask=overlay->mask();
                        const auto rippleBuilds=backdrop.shapeBuildCount();
                        overlay->setProperty("bloomProgress",.57);settleFrame(50);
                        const bool rippleSafe=rippleEnvelope&&rippleEnvelope->isVisible()&&rippleMask==overlay->mask()&&rippleMask.boundingRect().width()==216
                            &&backdrop.requestAccepted()&&backdrop.shapeBuildCount()==rippleBuilds&&overlay->property("ringRippleDistance").toDouble()>5&&overlay->grabWindow().save(output+"/runtime-ripple-outward.png");
                        QMetaObject::invokeMethod(bloomAnimation,"resume",Qt::DirectConnection);
                        QEventLoop ringWait;QTimer::singleShot(1200,&ringWait,&QEventLoop::quit);ringWait.exec();
                        const bool rippleSettled=rippleEnvelope&&!rippleEnvelope->isVisible()&&backdrop.enabled()==studio.backdropEnabled();
                        qInfo().noquote()<<QJsonDocument(QJsonObject{{"pressHeld",pressHeld},{"rippleMaskStable",rippleSafe},{"rippleSettled",rippleSettled}}).toJson(QJsonDocument::Compact);
                        if(!pressHeld||!rippleSafe||!rippleSettled){app.exit(13);return;}
                        const bool overflow=morePanel&&morePanel->isVisible()&&!surface->isVisible()&&settingsSurface->buttons().size()==6&&overlay->property("settingsReady").toBool();
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
                            QEventLoop settle;QTimer::singleShot(1000,&settle,&QEventLoop::quit);settle.exec();
                            const auto* railPanel=overlay->findChild<QQuickItem*>("railSettingsPanel");
                            const bool narrowOpen=railPanel&&railPanel->isVisible()&&!surface->isVisible()&&overlay->mask().boundingRect().width()==44&&saveRail("runtime-rail-settings.png");
                            if(railToggle)QMetaObject::invokeMethod(railToggle,"clicked",Qt::DirectConnection);
                            QEventLoop closeWait;QTimer::singleShot(1000,&closeWait,&QEventLoop::quit);closeWait.exec();
                            const bool flips=narrowClosed&&narrowOpen&&surface->isVisible();
                            studio.runtime()->setReducedMotion(false);
                            const auto captureTransition=[&](const QString& name){
                                QEventLoop frameWait;QTimer::singleShot(600,&frameWait,&QEventLoop::quit);frameWait.exec();
                                const auto progress=studio.runtime()->dockProgress();
                                const bool moving=studio.runtime()->transitioning()&&progress>0&&progress<1
                                    &&surface->isVisible()&&overlay->mask().contains(QPoint(overlay->width()/2,overlay->height()/2))
                                    &&backdrop.requestAccepted()&&backdrop.visualRegion()!=overlay->mask();
                                const bool saved=overlay->grabWindow().save(output+"/"+name+".png");
                                qInfo()<<name<<"progress"<<progress<<"visible"<<surface->isVisible()<<"moving"<<moving;
                                QEventLoop finishWait;
                                QObject::connect(studio.runtime(),&MenuSession::stateChanged,&finishWait,[&]{if(!studio.runtime()->transitioning())finishWait.quit();});
                                QTimer::singleShot(3000,&finishWait,&QEventLoop::quit);
                                if(studio.runtime()->transitioning())finishWait.exec();
                                return moving&&saved&&!studio.runtime()->transitioning();
                            };
                            studio.stylus()->sideButtonHeld();
                            const bool heldToRadial=captureTransition("runtime-undocking")&&!studio.runtime()->docked()&&overlay->isVisible();
                            studio.stylus()->sideButtonHeld();
                            const bool heldToDock=captureTransition("runtime-docking")&&studio.runtime()->docked()&&overlay->isVisible();
                            if(!heldToRadial || !heldToDock){app.exit(10);return;}
                            studio.runtime()->setProfile(radial::legacyDefaultProfile());
                            QVariantMap text;
                            for(const auto& value:studio.runtime()->buttons())if(value.toMap()["id"]=="text")text=value.toMap();
                            const double x=text["dockX"].toDouble(),y=text["dockY"].toDouble();
                            studio.runtime()->pointerPressed(93,x,y);
                            QEventLoop parentWait;QTimer::singleShot(360,&parentWait,&QEventLoop::quit);parentWait.exec();
                            const auto childCenter=studio.runtime()->parentCenter().toPoint();
                            const bool heldDock=studio.runtime()->parentMenuOpen()&&housing&&housing->opacity()==1&&surface->opacity()==1
                                &&overlay->mask().contains(QPoint(qRound(x),qRound(y)))&&overlay->mask().contains(childCenter)
                                &&overlay->grabWindow().save(output+"/runtime-held-parent.png");
                            studio.runtime()->pointerReleased(93,x,y);
                            const bool parentCloses=!studio.runtime()->parentMenuOpen()&&overlay->isVisible()&&overlay->mask().contains(QPoint(qRound(x),qRound(y)));
                            qInfo().noquote()<<QJsonDocument(QJsonObject{{"holdSwitchesToRadial",heldToRadial},{"holdSwitchesToDock",heldToDock},{"dockVisibleWithHeldChildren",heldDock},{"parentReleaseClosesChildren",parentCloses}}).toJson(QJsonDocument::Compact);
                            if(!heldDock || !parentCloses){app.exit(11);return;}
                            studio.runtime()->setReducedMotion(false);
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
