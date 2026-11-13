#include "app/UpdateChecker.h"
#include "app/DesktopShell.h"
#include "app/StudioController.h"
#include "ui/RadialSurface.h"
#include "ui/RadialGeometry.h"
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QColor>
#include <QPainter>
#include <cmath>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QPointingDevice>
#include <QtTest/qtesttouch.h>
#include <QtTest/QTest>
#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QTextBrowser>
#include <QLineF>
#include <QTimer>
#include <QScreen>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace tabletpro;
namespace {
int checks=0;
void expect(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}
struct Response { int code=200;QByteArray body;QUrl redirect;bool hang=false; };
class MemoryReply:public QNetworkReply {
    QByteArray data; qint64 offset=0;bool done=false;
public:
    MemoryReply(const QNetworkRequest& req,const Response& response,QObject* parent):QNetworkReply(parent),data(response.body) {
        setRequest(req);setUrl(req.url());setOperation(QNetworkAccessManager::GetOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute,response.code);
        if(!response.redirect.isEmpty())setAttribute(QNetworkRequest::RedirectionTargetAttribute,response.redirect);
        open(QIODevice::ReadOnly|QIODevice::Unbuffered);
        if(!response.hang)QTimer::singleShot(0,this,[this]{emit readyRead();if(!done){done=true;setFinished(true);emit finished();}});
    }
    void abort() override {if(done)return;done=true;setError(OperationCanceledError,"Cancelled");setFinished(true);emit finished();}
    qint64 bytesAvailable() const override {return data.size()-offset+QNetworkReply::bytesAvailable();}
    qint64 readData(char* dest,qint64 max) override {const auto n=qMin(max,qint64(data.size()-offset));if(n<=0)return -1;std::memcpy(dest,data.constData()+offset,size_t(n));offset+=n;return n;}
};
class Network:public QNetworkAccessManager {
public:
    QList<Response> queue;QList<QNetworkRequest> requests;
    QNetworkReply* createRequest(Operation,const QNetworkRequest& request,QIODevice*) override {
        requests.append(request);if(queue.isEmpty())throw std::runtime_error("unexpected request");
        return new MemoryReply(request,queue.takeFirst(),this);
    }
};
QByteArray installer(2048,'x');
QJsonObject release(QString version="0.3.4") {
    const QString base="https://github.com/test/radial/releases/";
    const QString name="Tablet-Pro-Radial-"+version+"-Windows-x64-Setup.exe";
    return {{"tag_name","v"+version},{"html_url",base+"tag/v"+version},{"draft",false},{"prerelease",false},{"body","A tested release"},
        {"assets",QJsonArray{QJsonObject{{"name",name},{"browser_download_url",base+"download/v"+version+"/"+name},{"size",installer.size()},
        {"digest","sha256:"+QString::fromLatin1(QCryptographicHash::hash(installer,QCryptographicHash::Sha256).toHex())},{"state","uploaded"}}}}};
}
QByteArray encode(QJsonObject root){return QJsonDocument(root).toJson(QJsonDocument::Compact);}
void drain(UpdateChecker& checker) {
    if(!checker.busy())return;QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);
    QObject::connect(&checker,&UpdateChecker::changed,&loop,[&]{if(!checker.busy())loop.quit();});
    QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);timeout.start(1500);loop.exec();expect(!checker.busy(),"request completed");
}
void parseTests(){
    expect(UpdateChecker::normalizeRepository("https://github.com/test/radial.git/")=="test/radial","normalize GitHub URL");
    for(const auto* value:{"http://github.com/test/radial","https://github.com.evil/test/radial","test/radial?x=1","test/../radial","test/radial#x","https://user@github.com/test/radial","/test/radial"})expect(UpdateChecker::normalizeRepository(value).isEmpty(),"reject invalid repository");
    expect(UpdateChecker::newer("0.3.10","0.3.9"),"numeric versions");
    expect(!UpdateChecker::newer("0.3.3","0.3.3"),"same version no update");
    expect(!UpdateChecker::newer("0.3.2","0.3.3"),"no downgrade");
    expect(!UpdateChecker::newer("0.3.4-beta","0.3.3"),"no prerelease version");
    QString error;UpdateRelease result;
    expect(UpdateChecker::parseRelease(encode(release()),"test/radial",result,error)&&result.version=="0.3.4"&&result.size==2048,"valid release");
    for(const auto* key:{"draft","prerelease"}){auto bad=release();bad[key]=true;expect(!UpdateChecker::parseRelease(encode(bad),"test/radial",result,error),"draft/prerelease rejected");}
    auto bad=release();bad["html_url"]="https://evil.example";expect(!UpdateChecker::parseRelease(encode(bad),"test/radial",result,error),"foreign release URL rejected");
    for(const auto* key:{"digest","browser_download_url","state"}){bad=release();auto assets=bad["assets"].toArray();auto asset=assets[0].toObject();asset[key]="invalid";assets[0]=asset;bad["assets"]=assets;expect(!UpdateChecker::parseRelease(encode(bad),"test/radial",result,error),"unsafe asset rejected");}
    bad=release();bad["assets"]=QJsonArray{};expect(UpdateChecker::parseRelease(encode(bad),"test/radial",result,error)&&result.download.isEmpty(),"release without installer supports notes only");
    expect(!UpdateChecker::parseRelease(QByteArray(UpdateChecker::MaxMetadataBytes+1,'x'),"test/radial",result,error),"oversized feed rejected");
    expect(!UpdateChecker::allowedRedirect(QUrl("http://release-assets.githubusercontent.com/x")),"HTTP refused");
    expect(!UpdateChecker::allowedRedirect(QUrl("https://release-assets.githubusercontent.com.evil/x")),"foreign redirect refused");
    expect(UpdateChecker::allowedRedirect(QUrl("https://release-assets.githubusercontent.com/x?token=asset")),"GitHub asset redirect allowed");
}
void networkTests(){
    QTemporaryDir temp;expect(temp.isValid(),"temporary settings");
    Network net;UpdateChecker checker(nullptr,temp.path()+"/settings.ini",&net,"0.3.3");
    checker.check();expect(net.requests.isEmpty()&&!checker.available(),"unconfigured does not request or claim current");
    expect(checker.setRepository("test/radial"),"set repo");checker.setAutomatic(false);checker.check(false);expect(net.requests.isEmpty(),"automatic opt-out");
    net.queue.append({200,encode(release())});checker.check();checker.check();drain(checker);
    expect(net.requests.size()==1&&checker.available(),"manual check available, concurrent check suppressed");
    expect(net.requests[0].url().toString()=="https://api.github.com/repos/test/radial/releases/latest","expected API only");
    expect(!net.requests[0].hasRawHeader("Authorization")&&!net.requests[0].hasRawHeader("Cookie"),"no account credentials sent");
    checker.setAutomatic(true);checker.check(false);expect(net.requests.size()==1,"successful checks throttled");
    net.queue.append({302,{},QUrl("https://release-assets.githubusercontent.com/test.exe")});net.queue.append({200,installer});
    checker.download();drain(checker);expect(checker.ready()&&checker.progress()==100,"redirected installer downloaded and verified");
    const auto paths=QDir(temp.path()+"/downloads").entryList({"*.exe"},QDir::Files);expect(paths.size()==1,"one committed installer");
    QFile tamper(temp.path()+"/downloads/"+paths.first());expect(tamper.open(QIODevice::WriteOnly|QIODevice::Append),"open fixture to tamper");tamper.write("wrong");tamper.close();
    expect(!checker.launchInstaller()&&!checker.ready(),"modified installer never launched");
    net.queue.append({200,encode(release())});checker.check();drain(checker);
    net.queue.append({200,QByteArray(2048,'y')});checker.download();drain(checker);expect(!checker.ready()&&checker.status().contains("checksum"),"hash mismatch refused");
    net.queue.append({200,QByteArray(4096,'y')});checker.download();drain(checker);expect(!checker.ready()&&checker.status().contains("size"),"oversized download refused");
    net.queue.append({302,{},QUrl("https://example.com/setup.exe")});checker.download();drain(checker);expect(!checker.ready()&&checker.status().contains("unexpected"),"foreign redirect refused at runtime");
    net.queue.append({200,{},QUrl{},true});checker.download();expect(checker.busy(),"hanging download active");checker.cancel();drain(checker);expect(!checker.ready()&&checker.status().contains("cancelled"),"cancellation cleans state");
    net.queue.append({404,"{}"});checker.check();drain(checker);expect(!checker.available()&&checker.status().contains("No published"),"404 not up to date");
    net.queue.append({403,"{}"});checker.check();drain(checker);expect(checker.status().contains("limit"),"rate limit reported");
    net.queue.append({200,encode(release("0.3.3"))});checker.check();drain(checker);expect(!checker.available()&&checker.status().contains("up to date"),"current version correctly reported");
    net.queue.append({200,"not JSON"});checker.check();drain(checker);expect(checker.status().contains("invalid")&&!checker.available(),"malformed feed not up to date");
    net.queue.append({200,QByteArray(UpdateChecker::MaxMetadataBytes+1,'x')});checker.check();drain(checker);expect(checker.status().contains("too large"),"response bound enforced");
    expect(checker.setRepository("test/another")&&checker.lastChecked().isEmpty(),"changing source clears prior result");
    UpdateChecker restored(nullptr,temp.path()+"/settings.ini",&net,"0.3.3");expect(restored.repository()=="test/another"&&restored.automatic(),"preferences persist");
}
QDialog* notesWindow(const char* name){
    for(auto* widget:QApplication::topLevelWidgets())if(widget->objectName()==name)return qobject_cast<QDialog*>(widget);
    return nullptr;
}
void installHandoffUiTests(){
    QFile self(QCoreApplication::applicationFilePath());expect(self.open(QIODevice::ReadOnly),"read executable handoff fixture");
    const auto originalInstaller=installer;const auto fixture=self.readAll();
    for(const QString scenario:{"radial","docked","editor","dirty-docked","cancel","failure"}){
        QTemporaryDir temp;Network net;const auto settingsPath=temp.path()+"/updates.ini";
        {QSettings prefs(settingsPath,QSettings::IniFormat);prefs.setValue("updates/repository","test/radial");}
        installer=scenario=="failure"?QByteArray(2048,'x'):fixture;
        StudioController studio(nullptr,temp.path()+"/profile",true);studio.setReducedMotion(true);
        QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&studio);engine.rootContext()->setContextProperty("startInEditor",false);
        engine.load(QUrl("qrc:/qml/Main.qml"));auto* editor=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        auto* overlay=editor->findChild<QQuickWindow*>("runtimeOverlay");overlay->resize(900,700);
        DesktopShell shell(&studio,overlay,nullptr,true,&net,settingsPath);shell.setEditorWindow(editor);engine.rootContext()->setContextProperty("desktop",&shell);
        int exits=0;QObject::connect(&shell,&DesktopShell::quitRequested,[&]{QMetaObject::invokeMethod(editor,"requestAppQuit");});
        QObject::connect(&shell,&DesktopShell::exitApproved,[&]{++exits;});
        QObject::connect(&studio,&StudioController::hideRuntimeRequested,[&]{editor->show();overlay->hide();});
        const auto layout=[&]{editor->hide();overlay->show();if(scenario=="editor"){overlay->hide();editor->show();}};
        if(scenario.contains("docked"))studio.runtime()->toggleDock();layout();
        net.queue.append({200,encode(release("9.0.0"))});shell.showUpdates();
        auto* dialog=notesWindow("updatesDialog");expect(dialog,"update dialog exists");
        auto* download=dialog->findChild<QPushButton*>("downloadUpdateButton");auto* install=dialog->findChild<QPushButton*>("installUpdateButton");
        expect(QTest::qWaitFor([&]{return download->isVisible()&&download->isEnabled();},2000),"test release available");
        net.queue.append({200,installer});download->click();
        expect(QTest::qWaitFor([&]{return install->isVisible();},5000),"verified installer ready for a single click");
        const auto receipt=temp.path()+"/handoff.json";qputenv("TABLETPRO_TEST_INSTALL_RECEIPT",receipt.toUtf8());
        if(scenario=="dirty-docked"||scenario=="cancel")studio.renameSelected("Unapplied update test");
        install->click();
        if(scenario=="dirty-docked"||scenario=="cancel"){
            expect(exits==0&&!QFile::exists(receipt)&&studio.dirty(),"draft is protected before updater starts");
            if(scenario=="cancel"){
                auto* prompt=editor->findChild<QObject*>("unappliedDialog");QMetaObject::invokeMethod(prompt,"close");QTest::qWait(220);
                expect(exits==0&&!QFile::exists(receipt)&&studio.dirty(),"dismissing draft prompt cancels installation");
                layout();install->click();
                expect(prompt->property("visible").toBool(),"install can be retried after cancelling the prompt");
            }
            QMetaObject::invokeMethod(editor->findChild<QObject*>("applyAndClose"),"clicked",Qt::DirectConnection);
            expect(!studio.dirty(),"draft is applied before installation");
        }
        if(scenario=="failure")expect(exits==0&&install->isEnabled()&&overlay->isVisible(),"failed installer launch leaves Radial open for retry");
        else {
            expect(exits==1&&!install->isEnabled(),"one Install click hands off and requests a clean app exit");
            install->click();expect(exits==1,"repeated install clicks cannot launch another installer");
            expect(QTest::qWaitFor([&]{return QFile::exists(receipt);},5000),"real detached installer process receives handoff");
            QFile received(receipt);expect(received.open(QIODevice::ReadOnly),"handoff receipt readable");const auto arguments=QJsonDocument::fromJson(received.readAll()).array();
            const auto mode=scenario.contains("docked")?"docked":scenario=="editor"?"editor":"radial";
            expect(arguments==QJsonArray{"--wait-for-pid",QString::number(QCoreApplication::applicationPid()),"--relaunch-mode",mode},"handoff carries owning app PID and original layout");
        }
        qunsetenv("TABLETPRO_TEST_INSTALL_RECEIPT");engine.rootContext()->setContextProperty("desktop",static_cast<QObject*>(nullptr));
    }
    installer=originalInstaller;
}
void whatsNewTests(){
    QTemporaryDir temp;StudioController studio(nullptr,temp.path()+"/profile",true);QQuickWindow overlay;
    const auto realVersion=QCoreApplication::applicationVersion();
    QSettings settings;
    settings.remove("updates/whatsNewSeen");settings.sync();
    QCoreApplication::setApplicationVersion("0.3.9");
    {
        DesktopShell first(&studio,&overlay,nullptr,true);
        first.showWhatsNewIfNeeded();QCoreApplication::processEvents();
        auto* popup=notesWindow("whatsNewDialog");expect(popup&&popup->isVisible(),"first launch shows what's new offline");
        expect(popup->findChild<QTextBrowser*>("historyContent")->toPlainText().contains("See what changed in every update"),"welcome uses the running version's notes, not the newest bundled entry");
        expect(!popup->findChild<QComboBox*>("historyVersionPicker")->isVisible(),"welcome focuses on the installed version");
        expect(QSettings().value("updates/whatsNewSeen/0.3.9").toBool(),"shown version is persisted");
        first.showWhatsNewIfNeeded();expect(notesWindow("whatsNewDialog")==popup,"repeated startup callback does not duplicate the welcome");
        popup->findChild<QPushButton*>("releaseNotesCloseButton")->click();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        expect(!notesWindow("whatsNewDialog"),"Let's go closes the welcome");
    }
    QCoreApplication::setApplicationVersion(realVersion);
    {
        DesktopShell upgraded(&studio,&overlay,nullptr,true);
        upgraded.showUpdateLog();expect(!QSettings().value("updates/whatsNewSeen/"+realVersion,false).toBool(),"manual history does not suppress the first-launch welcome");
        upgraded.showWhatsNewIfNeeded();QCoreApplication::processEvents();
        auto* popup=notesWindow("whatsNewDialog");expect(popup&&popup->isVisible(),"a newly installed version shows its welcome");
        expect(popup->findChild<QComboBox*>("historyVersionPicker")->currentText().startsWith(realVersion),"welcome selects the installed version");
        expect(popup->findChild<QTextBrowser*>("historyContent")->toPlainText()==notesWindow("updateHistoryDialog")->findChild<QTextBrowser*>("historyContent")->toPlainText(),"welcome and full log show the same release changes");
        expect(popup->grab().save(QDir::currentPath()+"/whats-new-current.png"),"welcome renders");
        popup->resize(400,380);QCoreApplication::processEvents();
        expect(popup->findChild<QPushButton*>("releaseNotesCloseButton")->geometry().right()<popup->width(),"welcome actions fit the narrow window");
        expect(popup->grab().save(QDir::currentPath()+"/whats-new-narrow.png"),"narrow welcome renders");
        popup->findChild<QPushButton*>("whatsNewHistoryButton")->click();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        expect(!notesWindow("whatsNewDialog")&&notesWindow("updateHistoryDialog")->isVisible(),"welcome opens the full history");
    }
    {
        DesktopShell restarted(&studio,&overlay,nullptr,true);restarted.showWhatsNewIfNeeded();
        expect(!notesWindow("whatsNewDialog"),"same version stays quiet after restarting");
        restarted.showUpdateLog();expect(notesWindow("updateHistoryDialog"),"history remains available after dismissing welcome");
    }
    QCoreApplication::setApplicationVersion("0.3.9");
    { DesktopShell older(&studio,&overlay,nullptr,true);older.showWhatsNewIfNeeded();expect(!notesWindow("whatsNewDialog"),"returning to a previously seen version does not repeat its welcome"); }
    QCoreApplication::setApplicationVersion("9.9.9");
    { DesktopShell missing(&studio,&overlay,nullptr,true);missing.showWhatsNewIfNeeded();expect(!notesWindow("whatsNewDialog")&&!QSettings().value("updates/whatsNewSeen/9.9.9",false).toBool(),"missing release notes neither show the wrong version nor consume its marker"); }
    QCoreApplication::setApplicationVersion(realVersion);
}
void parentPointerUiTests(){
    QTemporaryDir temp;StudioController studio(nullptr,temp.path()+"/profile",true);
    studio.setReducedMotion(true);studio.runtime()->setProfile(radial::legacyDefaultProfile());
    QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&studio);engine.rootContext()->setContextProperty("startInEditor",false);
    engine.load(QUrl("qrc:/qml/Main.qml"));expect(!engine.rootObjects().isEmpty(),"parent input UI loads");
    auto* editor=qobject_cast<QQuickWindow*>(engine.rootObjects().first());auto* overlay=editor->findChild<QQuickWindow*>("runtimeOverlay");
    overlay->resize(900,700);overlay->show();studio.runtime()->toggleDock();QTest::qWait(50);
    QVariantMap text;for(const auto& value:studio.runtime()->buttons())if(value.toMap()["id"]=="text")text=value.toMap();
    const QPoint launch(text["dockX"].toInt(),text["dockY"].toInt());auto* finger=QTest::createTouchDevice();
    int reports=0;QObject::connect(studio.runtime(),&MenuSession::actionReported,[&]{++reports;});
    QTest::touchEvent(overlay,finger).press(1,launch,overlay);QTest::qWait(340);
    expect(studio.runtime()->parentMenuOpen(),"actual touch hold opens a parent radial");
    auto* rail=overlay->findChild<QQuickItem*>("overlayRail");auto* surface=overlay->findChild<RadialSurface*>("runtimeSurface");
    expect(rail&&surface&&rail->isVisible()&&rail->opacity()==1&&surface->opacity()==1,"docked housing and buttons stay visible beside the held child radial");
    expect(overlay->grabWindow().save(QDir::currentPath()+"/docked-held-parent.png"),"docked parent and child render together");
    QVariantMap bold;for(const auto& value:studio.runtime()->parentButtons())if(value.toMap()["id"]=="text.bold")bold=value.toMap();
    const QPointF at(bold["cx"].toDouble(),bold["cy"].toDouble());
    QPointingDevice penDevice("parent test pen",4001,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
    const auto pen=[&](QEvent::Type type){const bool up=type==QEvent::TabletRelease;QTabletEvent event(type,&penDevice,at,overlay->mapToGlobal(at.toPoint()),up?0:0.5,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,up?Qt::NoButton:Qt::LeftButton);QCoreApplication::sendEvent(overlay,&event);QCoreApplication::processEvents();};
    pen(QEvent::TabletPress);expect(studio.runtime()->parentSelectedId()=="text.bold","pen selects a child while launching touch remains down");
    QTest::touchEvent(overlay,finger).release(1,launch,overlay);
    expect(!studio.runtime()->parentMenuOpen()&&reports==0&&overlay->isVisible(),"parent release closes children and leaves the dock visible");
    pen(QEvent::TabletRelease);expect(!studio.runtime()->active()&&reports==0,"late pen release cannot choose a child after the parent closes");
    QTest::touchEvent(overlay,finger).press(1,launch,overlay);QTest::qWait(340);
    pen(QEvent::TabletPress);pen(QEvent::TabletRelease);
    expect(!studio.runtime()->parentMenuOpen()&&reports==1&&overlay->isVisible(),"pen can choose a child while parent stays held and dock stays visible");
    QTest::touchEvent(overlay,finger).release(1,launch,overlay);
    QTest::touchEvent(overlay,finger).press(2,launch,overlay);
    QTest::touchEvent(overlay,finger).move(2,QPoint(240,350),overlay);QTest::qWait(25);
    expect(!studio.runtime()->parentMenuOpen()&&studio.runtime()->childButtons().size()==6,"actual touch slide opens the original fan");
    for(const auto& value:studio.runtime()->childButtons())if(value.toMap()["id"]=="text.bold")bold=value.toMap();
    const QPoint fanBold(bold["cx"].toInt(),bold["cy"].toInt());
    QTest::touchEvent(overlay,finger).move(2,fanBold,overlay);QTest::qWait(340);
    expect(!studio.runtime()->parentMenuOpen()&&studio.runtime()->selectedId()=="text.bold","slide fan stays open without delayed relocation");
    QTest::touchEvent(overlay,finger).release(2,fanBold,overlay);
    expect(!studio.runtime()->active()&&reports==2,"touch fan release commits once");
    studio.runtime()->setProfile(radial::defaultProfile());
    QVariantMap tools;for(const auto& value:studio.runtime()->buttons())if(value.toMap()["id"]=="tools")tools=value.toMap();
    const QPoint toolsAt(tools["dockX"].toInt(),tools["dockY"].toInt());
    QTest::touchEvent(overlay,finger).press(3,toolsAt,overlay);QTest::qWait(340);
    for(const auto& value:studio.runtime()->parentButtons())if(value.toMap()["id"]=="text")text=value.toMap();
    const QPoint textAt(text["cx"].toInt(),text["cy"].toInt());const auto angle=text["angle"].toDouble();
    QTest::touchEvent(overlay,finger).stationary(3).press(4,textAt,overlay);
    expect(studio.runtime()->parentSelectedId()=="text","touch selects the nested parent before sliding");
    for(int step=20;step<=50;step+=10){
        QTest::touchEvent(overlay,finger).stationary(3).move(4,textAt+QPoint(qRound(step*std::cos(angle)),qRound(step*std::sin(angle))),overlay);
        QTest::qWait(20); // Deliver compressed touch moves at frame boundaries.
    }
    const bool nestedFan=QTest::qWaitFor([&]{return !studio.runtime()->parentMenuOpen()&&studio.runtime()->childButtons().size()==6;},100);
    if(!nestedFan)std::cerr<<"nested touch state: "<<studio.runtime()->status().toStdString()<<"; selected="<<studio.runtime()->parentSelectedId().toStdString()<<"; active="<<studio.runtime()->active()<<'\n';
    expect(nestedFan,"sliding a nested parent out of a held radial opens its fan");
    for(const auto& value:studio.runtime()->childButtons())if(value.toMap()["id"]=="text.bold")bold=value.toMap();
    const QPoint nestedBold(bold["cx"].toInt(),bold["cy"].toInt());
    QTest::touchEvent(overlay,finger).stationary(3).move(4,nestedBold,overlay);QTest::touchEvent(overlay,finger).stationary(3).release(4,nestedBold,overlay);
    expect(!studio.runtime()->active()&&reports==3,"nested radial-to-fan transition retains touch ownership until release");
    QTest::touchEvent(overlay,finger).release(3,toolsAt,overlay);
    overlay->hide();
}
void inPlaceParentUiTests(){
    QTemporaryDir temp;StudioController studio(nullptr,temp.path()+"/profile",true);studio.setReducedMotion(true);
    QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&studio);engine.rootContext()->setContextProperty("startInEditor",false);
    engine.load(QUrl("qrc:/qml/Main.qml"));auto* editor=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    auto* overlay=editor->findChild<QQuickWindow*>("runtimeOverlay");overlay->resize(1000,800);overlay->show();QTest::qWait(30);
    auto* session=studio.runtime();session->setRadialCenter(480,400);
    const auto point=[&](const QString& id,bool child=false){for(const auto& value:child?session->parentButtons():session->buttons()){const auto b=value.toMap();if(b["id"]==id)return QPoint(qRound(b["cx"].toDouble()),qRound(b["cy"].toDouble()));}throw std::runtime_error("missing UI button");};
    int reports=0;QObject::connect(session,&MenuSession::actionReported,[&]{++reports;});
    QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,point("tools"));
    expect(session->parentInPlace()&&session->parentCenter()==session->radialCenter(),"mouse tap replaces the current floating ring");
    auto* surface=overlay->findChild<RadialSurface*>("runtimeSurface");
    expect(surface->opacity()==0&&session->childButtons().isEmpty(),"child ring covers the root without a side fan");
    const auto start=session->parentCenter().toPoint();QTest::mousePress(overlay,Qt::LeftButton,Qt::NoModifier,start);
    for(int i=1;i<=18;++i){QTest::mouseMove(overlay,start+QPoint(i*5,i*2));QTest::qWait(4);}
    QTest::mouseRelease(overlay,Qt::LeftButton,Qt::NoModifier,start+QPoint(90,36));
    expect(session->parentInPlace()&&!session->dragging()&&QLineF(session->parentCenter(),start+QPoint(90,36)).length()<1,"dragging a child-ring center keeps navigation and follows the pointer");
    QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,session->parentCenter().toPoint());
    expect(!session->active()&&!overlay->property("menuOpen").toBool()&&surface->opacity()==1,"child center tap returns to the root without opening settings");
    auto* finger=QTest::createTouchDevice();const auto tools=point("tools");
    QTest::touchEvent(overlay,finger).press(12,tools,overlay);QTest::touchEvent(overlay,finger).release(12,tools,overlay);
    expect(session->parentInPlace(),"touch tap opens children in place");session->parentBack();
    const auto center=session->radialCenter();const auto axis=QPointF(tools)-center;const auto delta=axis*(45/std::hypot(axis.x(),axis.y()));const auto out=tools+delta.toPoint();
    QTest::touchEvent(overlay,finger).press(12,tools,overlay);QTest::touchEvent(overlay,finger).move(12,out,overlay);QTest::qWait(25);QTest::touchEvent(overlay,finger).release(12,out,overlay);
    expect(session->parentInPlace()&&session->parentCenter()==center&&reports==0,"touch slide release leaves the replaced child ring open");
    QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,point("text",true));
    expect(session->parentLabel()=="Text"&&session->parentCenter()==center,"nested tap reuses the same ring");
    const auto bold=point("text.bold",true);QPointingDevice pen("in-place child pen",4002,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
    for(const auto type:{QEvent::TabletPress,QEvent::TabletRelease}){const bool up=type==QEvent::TabletRelease;QTabletEvent e(type,&pen,bold,overlay->mapToGlobal(bold),up?0:.5,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,up?Qt::NoButton:Qt::LeftButton);QCoreApplication::sendEvent(overlay,&e);QCoreApplication::processEvents();}
    expect(reports==1&&!session->active()&&!overlay->isVisible(),"pen child selection dispatches once and closes the floating menu");
}
void textureReuseTests(){
    MenuSession session(true,nullptr);session.setViewport(700,600);session.setProfile(radial::defaultProfile());
    RadialSurface surface;QImage image(700,600,QImage::Format_ARGB32_Premultiplied);
    const auto render=[&]{image.fill(Qt::transparent);surface.setButtons(session.buttons());QPainter painter(&image);surface.paint(&painter);};
    render();const auto first=image;const auto builds=surface.textureBuildCount();
    for(int i=0;i<80;++i){session.moveRadialCenter(300+i,260+i/2);render();}
    expect(surface.textureBuildCount()==builds&&image!=first,"many drag frames move rendered pixels without rebuilding their icon texture");
    surface.setDockProgress(.4);render();expect(surface.textureBuildCount()==builds,"cached particles retain correct movable geometry for mode transitions");
    auto profile=radial::defaultProfile();profile.nodes.at("brush").icon="eraser";session.setProfile(profile);render();
    expect(surface.textureBuildCount()==builds+1,"actual artwork edits still rebuild the texture");
}
void dockLandingTests(){
    MenuSession session(true,nullptr);RadialSurface surface;surface.setContinuousRail(true);
    for(const bool sparse:{false,true}){
        auto profile=radial::defaultProfile();
        if(sparse){profile.rootSlots[0].reset();profile.rootSlots[5].reset();}
        session.setProfile(profile);
        for(const int height:{480,768,1080})for(const qreal density:{1.0,1.5,2.0}){
            session.setViewport(720,height);surface.setButtons(session.buttons());
            const auto rail=session.dockBounds();
            const auto render=[&](qreal progress){
                surface.setDockProgress(progress);
                QImage image(qCeil(720*density),qCeil(height*density),QImage::Format_ARGB32_Premultiplied);
                image.setDevicePixelRatio(density);image.fill(Qt::transparent);QPainter painter(&image);surface.paint(&painter);return image;
            };
            const auto guide=render(.4);
            // Sample the empty header, away from the button halos and particles.
            int strongestX=-1,strongest=0;
            for(int x=0;x<int(25*density);++x){int alpha=0;
                for(int y=qCeil((rail.top()+20)*density);y<qFloor((rail.top()+33)*density);++y)alpha+=qAlpha(guide.pixel(x,y));
                if(alpha>strongest){strongest=alpha;strongestX=x;}
            }
            expect(strongest>100&&std::abs((strongestX+.5)/density-(rail.left()+1.5))<1,"assembly guide coincides with the finished rail border, including sparse menus and display scaling");
            const auto landing=render(.99999),finished=render(1);
            expect(landing==finished,"landed icon pixels do not jump when the transition finishes");
            for(const auto& value:session.buttons()){
                const auto b=value.toMap();const QPointF at(b["dockX"].toDouble(),b["dockY"].toDouble());
                expect(rail.contains(QRectF(at-QPointF(22,22),QSizeF(44,44)))&&surface.hitRoot(at.x(),at.y())==b["id"].toString(),"landed artwork and final hit targets remain inside the same housing");
            }
        }
    }
}
void displayDensityTests(){
    RadialSurface surface;surface.setSize({256,256});surface.setReducedMotion(true);
    QVariantList buttons;const QStringList icons={"select","color","type","settings","undo","Ctrl"};
    const QStringList captions={"Select","Color","Text","Settings","Undo",""};
    for(int i=0;i<6;++i){const qreal angle=-M_PI/2+i*M_PI/3;
        buttons.append(QVariantMap{{"id",QString::number(i)},{"icon",icons[i]},{"caption",captions[i]},
            {"slot",i},{"angle",angle},{"cx",128+70*std::cos(angle)},{"cy",128+70*std::sin(angle)},
            {"dockX",128},{"dockY",30+i*38}});
    }
    surface.setButtons(buttons);
    const auto render=[&](qreal scale){QImage result(qCeil(256*scale),qCeil(256*scale),QImage::Format_ARGB32_Premultiplied);
        result.setDevicePixelRatio(scale);result.fill(Qt::transparent);QPainter p(&result);surface.paint(&p);return result;};
    const auto detail=[](const QImage& image,QRectF logical){const qreal scale=image.devicePixelRatio();
        const QRect area(QPoint(qRound(logical.x()*scale),qRound(logical.y()*scale)),QSize(qRound(logical.width()*scale),qRound(logical.height()*scale)));
        double energy=0;for(int y=area.top();y<area.bottom();++y)for(int x=area.left();x<area.right();++x){
            const double value=qGray(image.pixel(x,y));const double dx=value-qGray(image.pixel(x+1,y)),dy=value-qGray(image.pixel(x,y+1));energy+=dx*dx+dy*dy;
        }return energy;};
    const auto baseline=render(1);const auto output=qEnvironmentVariable("TABLETPRO_DENSITY_OUTPUT");if(!output.isEmpty())QDir().mkpath(output);
    for(const qreal scale:{1.25,1.5,2.0,3.0,1.0}){
        const auto native=render(scale);const auto count=surface.textureBuildCount();
        expect(render(scale)==native&&surface.textureBuildCount()==count,"unchanged display density reuses cached artwork and identical pixels");
        if(scale>1){auto stretched=baseline.scaled(native.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);stretched.setDevicePixelRatio(scale);
            const auto iconNative=detail(native,{114,39,28,26}),iconStretched=detail(stretched,{114,39,28,26});
            const auto textNative=detail(native,{104,65,48,14}),textStretched=detail(stretched,{104,65,48,14});
            std::cout<<"Density "<<scale<<": icon detail "<<iconNative/iconStretched<<", text detail "<<textNative/textStretched<<"\n";
            expect(iconNative>iconStretched*1.05&&textNative>textStretched*1.05,"native density preserves sharper icon and caption edges than enlarged low-resolution artwork");
        }else expect(native==baseline,"returning to 100 percent restores the original dimensions and pixels");
        expect(qAlpha(native.pixel(qRound(128*scale),qRound(35*scale)))>0&&qAlpha(native.pixel(qRound(128*scale),qRound(25*scale)))==0,"display density leaves the logical menu footprint unchanged");
        if(!output.isEmpty())expect(native.save(output+QString("/radial-%1.png").arg(qRound(scale*100))),"density preview saved");
        surface.setDockProgress(1);const auto rail=render(scale);expect(surface.textureBuildCount()==count,"docked icons reuse native-density artwork");
        if(!output.isEmpty())rail.save(output+QString("/dock-%1.png").arg(qRound(scale*100)));
        surface.setDockProgress(0);
    }
}
void wedgeClippingTests(){
    RadialSurface surface;surface.setSize({256,256});surface.setReducedMotion(true);
    int clipped=0,painted=0;
    for(const qreal scale:{1.0,1.25,1.5,2.0,3.0})for(const qreal offset:{0.0,.25,.5,.75}){
        const QPointF center(128+offset,128+offset);
        QVariantList buttons;
        QRegion mask(QRectF(center-QPointF(40,40),QSizeF(80,80)).toAlignedRect(),QRegion::Ellipse);
        for(int i=0;i<6;++i){const double angle=-M_PI/2+i*M_PI/3;
            buttons.append(QVariantMap{{"id",QString::number(i)},{"slot",i},{"angle",angle},
                {"cx",center.x()+70*std::cos(angle)},{"cy",center.y()+70*std::sin(angle)}});
            mask+=tabletpro::radialGeometry::nativeWedge(center,angle);
        }
        surface.setButtons(buttons);
        for(const bool bronze:{false,true}){
            surface.setBronzeTheme(bronze);
            QImage image(qCeil(256*scale),qCeil(256*scale),QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(scale);image.fill(Qt::transparent);
            {QPainter painter(&image);surface.paint(&painter);}
            // A native region is integer logical geometry before Windows scales
            // it. Test the actual painted border against that conservative area.
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){
                if(qAlpha(image.pixel(x,y))<16)continue;
                ++painted;
                if(!mask.contains(QPoint(qFloor((x+.5)/scale),qFloor((y+.5)/scale))))++clipped;
            }
        }
    }
    std::cout<<"Wedge mask: "<<clipped<<" clipped of "<<painted<<" painted pixels\n";
    expect(painted>100000&&clipped==0,"native cutout preserves all six wedge borders at fractional positions and display scales");
}
void sideButtonModeUiTests(){
    QTemporaryDir temp;StudioController studio(nullptr,temp.path()+"/profile",true);studio.setReducedMotion(true);
    QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&studio);engine.rootContext()->setContextProperty("startInEditor",false);
    engine.load(QUrl("qrc:/qml/Main.qml"));auto* editor=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    auto* overlay=editor->findChild<QQuickWindow*>("runtimeOverlay");overlay->resize(900,700);overlay->show();QTest::qWait(30);
    int shows=0,toggles=0;
    QObject::connect(&studio,&StudioController::showRuntimeRequested,[&](quintptr,bool toggle){++shows;if(toggle)++toggles;if(toggle&&overlay->isVisible())overlay->hide();else overlay->show();});
    overlay->setProperty("menuOpen",true);
    studio.stylus()->sideButtonDown();expect(overlay->isVisible()&&shows==0,"side-button down waits without hiding the wheel");
    studio.stylus()->sideButtonHeld();
    expect(studio.runtime()->docked()&&overlay->isVisible()&&!overlay->property("menuOpen").toBool()&&toggles==0,"long hold docks the visible menu and exits settings");
    studio.stylus()->sideButtonHeld();expect(!studio.runtime()->docked()&&overlay->isVisible()&&toggles==0,"next hold returns the dock to a visible radial");
    studio.stylus()->sideButtonPressed();expect(!overlay->isVisible()&&toggles==1,"quick side-button press still hides the menu");
    studio.stylus()->sideButtonHeld();expect(studio.runtime()->docked()&&overlay->isVisible(),"long hold also switches and shows a hidden menu");
    studio.setReducedMotion(false);studio.runtime()->setDurationMs(250);
    studio.stylus()->sideButtonHeld();QTest::qWait(40);studio.stylus()->sideButtonHeld();QTest::qWait(300);
    expect(studio.runtime()->docked()&&!studio.runtime()->transitioning()&&overlay->isVisible(),"another hold can reverse a mode transition without hiding it");
    overlay->hide();
}
void selectionDismissalUiTests(){
    QTemporaryDir temp;StudioController studio(nullptr,temp.path()+"/profile",true);studio.setReducedMotion(true);
    QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&studio);engine.rootContext()->setContextProperty("startInEditor",false);
    engine.load(QUrl("qrc:/qml/Main.qml"));expect(!engine.rootObjects().isEmpty(),"selection dismissal UI loads");
    auto* editor=qobject_cast<QQuickWindow*>(engine.rootObjects().first());auto* overlay=editor->findChild<QQuickWindow*>("runtimeOverlay");
    overlay->resize(900,700);overlay->show();QTest::qWait(30);
    const auto point=[&](QString id){for(const auto& value:studio.runtime()->buttons()){const auto b=value.toMap();if(b["id"]==id)return QPoint(b[studio.runtime()->docked()?"dockX":"cx"].toInt(),b[studio.runtime()->docked()?"dockY":"cy"].toInt());}return QPoint();};
    QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,point("modifier.Alt"));QCoreApplication::processEvents();
    expect(!overlay->isVisible()&&studio.runtime()->heldModifiers()==QStringList{"Alt"},"floating modifier tap hides the wheel without canceling its next stroke");
    studio.runtime()->finishNextStroke();expect(studio.runtime()->heldModifiers().isEmpty(),"hidden one-stroke modifier still releases");
    overlay->show();QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,point("brush"));
    expect(!overlay->isVisible(),"floating ordinary action closes the radial");
    overlay->show();studio.runtime()->toggleDock();QTest::qWait(30);
    QTest::mouseClick(overlay,Qt::LeftButton,Qt::NoModifier,point("brush"));expect(overlay->isVisible(),"docked rail remains visible after choosing an action");
    const auto alt=point("modifier.Alt");QTest::mousePress(overlay,Qt::LeftButton,Qt::NoModifier,alt);QTest::mouseMove(overlay,alt+QPoint(80,0),20);QTest::mouseRelease(overlay,Qt::LeftButton,Qt::NoModifier,alt+QPoint(80,0));
    expect(overlay->isVisible()&&studio.runtime()->heldModifiers()==QStringList{"Alt"},"docked drag-out survives finger lift and leaves the rail visible");
    studio.runtime()->finishPenHover();expect(studio.runtime()->heldModifiers().isEmpty(),"hover exit releases the docked drag-out modifier");
    studio.runtime()->toggleDock();
    auto profile=radial::defaultProfile();profile.nodes.at("tools").children[4]="modifier.Alt";profile.rootSlots[2].reset();studio.runtime()->setProfile(profile);
    const auto tools=point("tools");auto* finger=QTest::createTouchDevice();
    QTest::touchEvent(overlay,finger).press(71,tools,overlay);QTest::qWait(340);
    QVariantMap child;for(const auto& value:studio.runtime()->parentButtons())if(value.toMap()["id"]=="modifier.Alt")child=value.toMap();
    expect(!child.isEmpty(),"held parent exposes its child modifier");
    const QPointF at(child["cx"].toDouble(),child["cy"].toDouble());
    QPointingDevice pen("dismissal pen",4100,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
    for(const auto type:{QEvent::TabletPress,QEvent::TabletRelease}){const bool up=type==QEvent::TabletRelease;QTabletEvent event(type,&pen,at,overlay->mapToGlobal(at.toPoint()),up?0:0.5,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,up?Qt::NoButton:Qt::LeftButton);QCoreApplication::sendEvent(overlay,&event);QCoreApplication::processEvents();}
    expect(!overlay->isVisible()&&studio.runtime()->heldModifiers()==QStringList{"Alt"},"child modifier closes floating menu and survives cancellation of its still-held launching finger");
    studio.runtime()->finishNextStroke();
}
void editorPenTriggerUiTests(){
    QSettings settings;settings.remove("penSideButton");settings.remove("penInputEnabled");
    QTemporaryDir temp;StudioController studio(nullptr,temp.path()+"/profile",true);
    QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&studio);engine.rootContext()->setContextProperty("startInEditor",true);
    engine.load(QUrl("qrc:/qml/Main.qml"));expect(!engine.rootObjects().isEmpty(),"pen trigger editor loads");
    auto* editor=qobject_cast<QQuickWindow*>(engine.rootObjects().first());editor->resize(900,640);editor->show();QTest::qWait(60);
    auto* combo=editor->findChild<QQuickItem*>("editorPenTrigger");auto* enabled=editor->findChild<QQuickItem*>("editorPenTriggerEnabled");
    auto* details=editor->findChild<QQuickItem*>("editorPenSettings");
    expect(combo&&enabled&&details&&combo->isVisible(),"pen trigger is available directly in the editor");
    expect(studio.stylus()->enabled()&&studio.stylus()->selectedSideButton()==1&&combo->property("currentText").toString()=="Barrel button (default)","fresh settings enable the explicitly named barrel default");
    combo->forceActiveFocus();QTest::keyClick(editor,Qt::Key_Down);QCoreApplication::processEvents();
    expect(studio.stylus()->selectedSideButton()==2&&settings.value("penSideButton").toInt()==2,"keyboard selection changes and immediately saves the trigger");
    {StudioController reopened(nullptr,temp.path()+"/reopened",true);expect(reopened.stylus()->selectedSideButton()==2,"chosen trigger survives controller restart");}
    QTest::keyClick(editor,Qt::Key_Up);enabled->forceActiveFocus();QTest::keyClick(editor,Qt::Key_Space);QCoreApplication::processEvents();
    expect(!studio.stylus()->enabled()&&!combo->isEnabled()&&!settings.value("penInputEnabled").toBool(),"editor switch disables and saves pen-button input");
    QTest::keyClick(editor,Qt::Key_Space);QCoreApplication::processEvents();
    expect(studio.stylus()->enabled()&&studio.stylus()->selectedSideButton()==1,"barrel choice is retained when re-enabling input");
    const auto bounds=details->mapRectToScene(details->boundingRect());
    expect(editor->grabWindow().save(QDir::currentPath()+"/editor-pen-trigger.png"),"compact pen trigger editor renders");
    if(bounds.right()>editor->width())std::cerr<<"trigger right="<<bounds.right()<<" width="<<editor->width()<<" combo="<<combo->width()<<'\n';
    expect(bounds.right()<=editor->width()&&bounds.top()>=0&&bounds.bottom()<=editor->height(),"trigger row fits the minimum editor size");
    editor->hide();
}
void editorDragUiTests(){
    QTemporaryDir temp;StudioController studio(nullptr,temp.path()+"/profile",true);studio.setReducedMotion(true);
    QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&studio);engine.rootContext()->setContextProperty("startInEditor",true);
    engine.load(QUrl("qrc:/qml/Main.qml"));expect(!engine.rootObjects().isEmpty(),"drag editor loads");
    auto* editor=qobject_cast<QQuickWindow*>(engine.rootObjects().first());editor->resize(1180,1100);editor->show();editor->requestActivate();QTest::qWait(80);
    auto* surface=editor->findChild<QQuickItem*>("editorSurface");auto* list=editor->findChild<QQuickItem*>("outlineList");
    expect(surface&&list,"drag source surfaces are available");
    std::function<QQuickItem*(QQuickItem*,const QString&)> findItem=[&](QQuickItem* parent,const QString& name)->QQuickItem*{
        if(parent->objectName()==name)return parent;
        for(auto* child:parent->childItems())if(auto* found=findItem(child,name))return found;
        return nullptr;
    };
    const auto rowPoint=[&](const QString& id){
        auto* row=findItem(list,"outline-"+id);
        if(!row)throw std::runtime_error(("Missing outline row "+id).toStdString());
        return row->mapToScene({row->width()/2,row->height()/2}).toPoint();
    };
    const auto wheelPoint=[&](const QString& id){
        for(const auto& entry:studio.preview()->buttons()){
            const auto b=entry.toMap();if(b["id"]==id)return surface->mapToScene({b[studio.preview()->docked()?"dockX":"cx"].toDouble(),b[studio.preview()->docked()?"dockY":"cy"].toDouble()}).toPoint();
        }throw std::runtime_error("Missing preview button");
    };
    const auto moveMouse=[&](QPoint from,QPoint to){QTest::mousePress(editor,Qt::LeftButton,Qt::NoModifier,from);QTest::mouseMove(editor,from+QPoint(12,0),20);QTest::mouseMove(editor,to,20);QTest::qWait(30);};
    const auto before=studio.draft();const auto brush=rowPoint("brush"),tools=rowPoint("tools");
    moveMouse(brush,tools);
    expect(editor->property("draggedButtonId")=="brush"&&editor->property("dropParentId")=="tools"&&editor->property("dropProblem").toString().isEmpty(),"real list drag recognizes parent and shows valid feedback");
    if(!qEnvironmentVariableIsEmpty("RADIAL_DRAG_CAPTURE"))editor->grabWindow().save(qEnvironmentVariable("RADIAL_DRAG_CAPTURE")+"-list.png");
    QTest::mouseRelease(editor,Qt::LeftButton,Qt::NoModifier,tools);QTest::qWait(40);
    expect(!studio.draft().rootSlots[0]&&studio.draft().nodes.at("tools").children[4]=="brush","list mouse drop moves exactly one button into the parent");
    studio.undo();QTest::qWait(40);expect(studio.draft()==before,"list drop is one undoable edit");
    auto* finger=QTest::createTouchDevice();
    QTest::touchEvent(editor,finger).press(12,brush,editor);
    QTest::touchEvent(editor,finger).move(12,brush+QPoint(0,14),editor);QTest::qWait(25);
    QTest::touchEvent(editor,finger).move(12,tools,editor);QTest::qWait(25);
    QTest::touchEvent(editor,finger).release(12,tools,editor);QTest::qWait(40);
    expect(studio.draft().nodes.at("tools").children[4]=="brush","vertical touch drag in the outline moves the button instead of flicking the list");
    studio.undo();editor->resize(1180,760);list->setProperty("contentY",0);QTest::qWait(50);
    const auto scrollStart=rowPoint("brush"),scrollEdge=list->mapToScene({list->width()/2,list->height()-8}).toPoint();
    moveMouse(scrollStart,scrollEdge);QTest::qWait(450);
    expect(list->property("contentY").toDouble()>50&&editor->property("draggedButtonId")=="brush","outline scrolls near its edge without losing the dragged source");
    expect(QTest::qWaitFor([&]{return rowPoint("tools").y()<list->mapToScene({0,list->height()-24}).y();},1500),"edge scrolling reveals the parent inside the shorter editor outline");
    const auto scrolledTools=rowPoint("tools");QTest::mouseMove(editor,scrolledTools,20);QTest::qWait(20);QTest::mouseRelease(editor,Qt::LeftButton,Qt::NoModifier,scrolledTools);QTest::qWait(40);
    expect(studio.draft().nodes.at("tools").children[4]=="brush","drop reaches a parent revealed by automatic scrolling");
    studio.undo();editor->resize(1180,1100);list->setProperty("contentY",0);QTest::qWait(50);
    editor->setProperty("arrangeButtons",true);QTest::qWait(30);
    const auto rootBrush=wheelPoint("brush"),rootTools=wheelPoint("tools");moveMouse(rootBrush,rootTools);
    expect(editor->property("dropParentId")=="tools"&&!studio.preview()->active(),"arrange preview drag targets parent without running its hold gesture");
    if(!qEnvironmentVariableIsEmpty("RADIAL_DRAG_CAPTURE"))editor->grabWindow().save(qEnvironmentVariable("RADIAL_DRAG_CAPTURE")+"-preview.png");
    QTest::mouseRelease(editor,Qt::LeftButton,Qt::NoModifier,rootTools);QTest::qWait(40);
    expect(studio.draft().nodes.at("tools").children[4]=="brush","preview mouse drop creates a child");studio.undo();QTest::qWait(40);
    QTest::touchEvent(editor,finger).press(11,rootBrush,editor);
    QTest::touchEvent(editor,finger).move(11,rootBrush+QPoint(14,0),editor);QTest::qWait(25);
    QTest::touchEvent(editor,finger).move(11,rootTools,editor);QTest::qWait(25);
    QTest::touchEvent(editor,finger).release(11,rootTools,editor);QTest::qWait(40);
    expect(studio.draft().nodes.at("tools").children[4]=="brush","preview touch drag creates a child");studio.undo();QTest::qWait(40);
    QPointingDevice penDevice("editor drag pen",4002,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
    const auto pen=[&](QEvent::Type type,QPoint at){const bool up=type==QEvent::TabletRelease;QTabletEvent event(type,&penDevice,at,editor->mapToGlobal(at),up?0:0.5,0,0,0,0,0,Qt::NoModifier,type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,up?Qt::NoButton:Qt::LeftButton);QCoreApplication::sendEvent(editor,&event);QCoreApplication::processEvents();};
    pen(QEvent::TabletPress,rootBrush);pen(QEvent::TabletMove,rootBrush+QPoint(14,0));pen(QEvent::TabletMove,rootTools);pen(QEvent::TabletRelease,rootTools);QTest::qWait(40);
    expect(studio.draft().nodes.at("tools").children[4]=="brush","preview stylus drag creates a child");studio.undo();QTest::qWait(40);
    moveMouse(rootBrush,rootTools);QTest::keyClick(editor,Qt::Key_Escape);QCoreApplication::processEvents();
    if(!editor->property("draggedButtonId").toString().isEmpty())std::cerr<<"Escape left drag active; editor active="<<editor->isActive()<<" focus="<<(QGuiApplication::focusWindow()==editor)<<'\n';
    QTest::mouseRelease(editor,Qt::LeftButton,Qt::NoModifier,rootTools);QTest::qWait(40);
    expect(studio.draft()==before&&editor->property("draggedButtonId").toString().isEmpty(),"Escape cancels a drag without a late drop");
    moveMouse(rootBrush,QPoint(editor->width()/2,100));QTest::mouseRelease(editor,Qt::LeftButton,Qt::NoModifier,QPoint(editor->width()/2,100));QTest::qWait(40);
    expect(studio.draft()==before,"dropping outside buttons leaves the menu intact");
    studio.preview()->toggleDock();QTest::qWait(40);
    const auto dockBrush=wheelPoint("brush"),dockTools=wheelPoint("tools");moveMouse(dockBrush,dockTools);QTest::mouseRelease(editor,Qt::LeftButton,Qt::NoModifier,dockTools);QTest::qWait(40);
    expect(studio.draft().nodes.at("tools").children[4]=="brush","docked preview also supports dragging into a parent");
    editor->hide();
}
void trayTests(){
    QTemporaryDir temp;StudioController studio(nullptr,temp.path()+"/profile",true);
    qmlRegisterType<RadialSurface>("TabletPro",1,0,"RadialSurface");
    qmlRegisterUncreatableType<MenuSession>("TabletPro",1,0,"MenuSession","Provided");
    qmlRegisterUncreatableType<StylusInput>("TabletPro",1,0,"StylusInput","Provided");
    QQmlApplicationEngine engine;engine.rootContext()->setContextProperty("studio",&studio);engine.rootContext()->setContextProperty("startInEditor",false);
    QObject::connect(&engine,&QQmlApplicationEngine::warnings,[](const QList<QQmlError>& warnings){for(const auto& warning:warnings)std::cerr<<warning.toString().toStdString()<<'\n';});
    engine.load(QUrl("qrc:/qml/Main.qml"));expect(!engine.rootObjects().isEmpty(),"QML loaded");
    auto* editor=qobject_cast<QQuickWindow*>(engine.rootObjects().first());auto* overlay=editor->findChild<QQuickWindow*>("runtimeOverlay");expect(overlay,"overlay found");
    DesktopShell shell(&studio,overlay,nullptr,true);engine.rootContext()->setContextProperty("desktop",&shell);
    int show=0,hide=0,edit=0,exit=0;QObject::connect(&studio,&StudioController::showRuntimeRequested,[&]{++show;overlay->show();});
    QObject::connect(&studio,&StudioController::dismissRuntimeRequested,[&]{++hide;overlay->hide();});
    QObject::connect(&studio,&StudioController::hideRuntimeRequested,[&]{++edit;editor->show();overlay->hide();});
    QObject::connect(&shell,&DesktopShell::quitRequested,[&]{QMetaObject::invokeMethod(editor,"requestAppQuit");});
    QObject::connect(&shell,&DesktopShell::exitApproved,[&]{++exit;});
    auto* toggle=shell.menu()->findChild<QAction*>("trayToggle");auto* quit=shell.menu()->findChild<QAction*>("trayQuit");
    expect(toggle&&quit&&shell.menu()->findChild<QAction*>("trayUpdates"),"tray actions present");
    toggle->trigger();expect(show==1&&toggle->text().startsWith("Hide"),"tray show");toggle->trigger();expect(hide==1&&toggle->text().startsWith("Show"),"tray hide");
    shell.menu()->findChild<QAction*>("trayEditor")->trigger();expect(edit==1,"tray editor");
    studio.selectNode("text");studio.renameSelected("Edited text");expect(studio.dirty(),"dirty draft");
    quit->trigger();auto* dialog=editor->findChild<QObject*>("unappliedDialog");expect(dialog&&dialog->property("visible").toBool()&&exit==0,"quit prompts before discarding");
    auto* keep=editor->findChild<QObject*>("keepEditingButton");QMetaObject::invokeMethod(keep,"clicked",Qt::DirectConnection);
    expect(exit==0&&studio.dirty()&&!editor->property("quitAfterClose").toBool(),"keep editing cancels quit");
    quit->trigger();auto* apply=editor->findChild<QObject*>("applyAndClose");QMetaObject::invokeMethod(apply,"clicked",Qt::DirectConnection);
    expect(exit==1&&!studio.dirty(),"apply before quitting");
    studio.renameSelected("Another draft");quit->trigger();QMetaObject::invokeMethod(editor->findChild<QObject*>("discardAndClose"),"clicked",Qt::DirectConnection);
    expect(exit==2&&!studio.dirty(),"discard explicit before quitting");
    quit->trigger();expect(exit==3,"clean quit immediate");
    expect(!DesktopShell::icon().pixmap(16,16).isNull(),"tray icon at small size");
    auto* historyAction=shell.menu()->findChild<QAction*>("trayUpdateLog");expect(historyAction,"update log is available directly in the tray");
    historyAction->trigger();QCoreApplication::processEvents();
    QDialog* history=nullptr;for(auto* widget:QApplication::topLevelWidgets())if(widget->objectName()=="updateHistoryDialog")history=qobject_cast<QDialog*>(widget);
    expect(history&&history->isVisible(),"offline update log opens");
    auto* versions=history->findChild<QComboBox*>("historyVersionPicker");auto* content=history->findChild<QTextBrowser*>("historyContent");
    expect(versions&&content&&versions->count()>=6,"bundled history includes the published versions");
    expect(versions->currentText().contains(TABLETPRO_VERSION)&&versions->currentText().contains("Installed"),"history marks and selects the installed version");
    expect(!content->toPlainText().trimmed().isEmpty()&&!content->toPlainText().contains("not available"),"current changes are readable without a network request");
    expect(history->grab().save(QDir::currentPath()+"/update-log-current.png"),"current history renders");
    int bloom=-1;for(int i=0;i<versions->count();++i)if(versions->itemText(i).startsWith("0.3.7"))bloom=i;
    expect(bloom>=0,"older animation release is recorded");versions->setCurrentIndex(bloom);
    expect(content->toPlainText().contains("Gentle bloom")&&content->toPlainText().contains("760 ms"),"version selection shows that version's changes");
    expect(history->grab().save(QDir::currentPath()+"/update-log-older.png"),"older history renders");
    history->resize(400,380);QCoreApplication::processEvents();
    expect(versions->geometry().right()<=history->width()&&content->geometry().right()<=history->width(),"history fits a narrow window");
    expect(history->grab().save(QDir::currentPath()+"/update-log-narrow.png"),"narrow history renders");
    shell.showUpdateLog();int historyWindows=0;for(auto* widget:QApplication::topLevelWidgets())if(widget->objectName()=="updateHistoryDialog")++historyWindows;
    expect(historyWindows==1,"reopening history reuses the existing window");
    history->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    studio.runtime()->setReducedMotion(true);
    studio.runtime()->toggleDock();
    overlay->show();
    auto* rail=overlay->findChild<QQuickItem*>("overlayRail");
    auto* settings=overlay->findChild<QQuickItem*>("railSettingsPanel");
    auto* railToggle=overlay->findChild<QObject*>("railSettingsButton");
    auto* surface=overlay->findChild<RadialSurface*>("runtimeSurface");
    auto* floatingHub=overlay->findChild<QQuickItem*>("overlayToolbar");
    auto* floatingOptions=overlay->findChild<QQuickItem*>("settingsSurface");
    expect(rail&&settings&&railToggle&&surface&&floatingHub&&floatingOptions,"continuous rail controls exist");
    expect(rail->isVisible()&&!floatingHub->isVisible()&&surface->isVisible(),"docked rail replaces the circular hub");
    for(const auto height:{480,768,1080}){
        overlay->resize(720,height);QCoreApplication::processEvents();
        expect(rail->width()==44&&rail->x()==13&&rail->y()>=0&&rail->y()+rail->height()<=height,"rail stays inside the viewport at supported desktop sizes");
        for(const auto& value:studio.runtime()->buttons()){
            const auto b=value.toMap();expect(b["dockX"].toDouble()-22==rail->x()&&b["dockX"].toDouble()+22==rail->x()+rail->width(),"tool and rail edges align exactly");
        }
        expect(rail->mapRectToScene(rail->boundingRect())==studio.runtime()->dockBounds(),"visible housing uses the same layout as the landing renderer");
    }
    QMetaObject::invokeMethod(railToggle,"clicked",Qt::DirectConnection);
    expect(settings->isVisible()&&!surface->isVisible()&&!surface->isEnabled()&&!floatingOptions->isVisible(),"settings replace drawing tools without a flyout or hidden action targets");
    for(const auto* key:{"radial","edit","sound","glass","hide","quit"}){
        const auto name=QString("railAction_")+key;
        auto* action=settings->findChild<QQuickItem*>(name);
        if(!action)for(auto* child:settings->childItems())if(child->objectName()==name){action=child;break;}
        if(!action||!action->isVisible())std::cerr<<"Missing visible action: "<<name.toStdString()<<'\n';
        expect(action&&action->isVisible(),"settings action visible");
        const auto rect=action->mapRectToScene(action->boundingRect());
        expect(rect.left()==rail->x()&&rect.right()==rail->x()+44&&rect.top()>=rail->y()&&rect.bottom()<=rail->y()+rail->height(),"every open settings action stays within the 44px rail");
        expect(action->width()==44&&action->height()==44,"settings retain full-size pointer targets");
        expect(action->property("iconColor").value<QColor>()!=QColor("#e0edfa"),"settings icons are distinct from the drawing-control palette");
    }
    const auto actionItem=[&](const QString& key)->QQuickItem*{for(auto* child:settings->childItems())if(child->objectName()=="railAction_"+key)return child;return nullptr;};
    auto* sound=actionItem("sound");const bool soundBefore=studio.soundEnabled();
    QMetaObject::invokeMethod(sound,"clicked",Qt::DirectConnection);
    expect(studio.soundEnabled()!=soundBefore&&sound->property("highlighted").toBool()==studio.soundEnabled()&&settings->isVisible(),"sound toggles in place with visible state");
    auto* glass=actionItem("glass");const bool glassBefore=studio.backdropEnabled();
    QMetaObject::invokeMethod(glass,"clicked",Qt::DirectConnection);
    expect(studio.backdropEnabled()!=glassBefore&&glass->property("highlighted").toBool()==studio.backdropEnabled()&&settings->isVisible(),"glass toggles in place with visible state");
    QMetaObject::invokeMethod(railToggle,"clicked",Qt::DirectConnection);
    expect(!settings->isVisible()&&surface->isVisible()&&surface->isEnabled(),"back restores the drawing tools");
    QMetaObject::invokeMethod(railToggle,"clicked",Qt::DirectConnection);
    QMetaObject::invokeMethod(actionItem("radial"),"clicked",Qt::DirectConnection);
    expect(!studio.runtime()->docked()&&!rail->isVisible()&&floatingHub->isVisible()&&!overlay->property("menuOpen").toBool(),"undock restores centered floating controls and closes settings");
    QVariantMap alt;
    for(const auto& value:studio.runtime()->buttons())if(value.toMap()["id"]=="modifier.Alt")alt=value.toMap();
    const QPointF at(alt["cx"].toDouble(),alt["cy"].toDouble());
    const auto mouse=[&](QEvent::Type type){QMouseEvent event(type,at,at,Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(surface,&event);};
    mouse(QEvent::MouseButtonPress);mouse(QEvent::MouseButtonRelease);
    expect(studio.runtime()->heldModifiers().contains("Alt")&&!overlay->isVisible(),"floating tap arms the next pen stroke and closes the wheel");
    overlay->show();
    mouse(QEvent::MouseButtonDblClick);mouse(QEvent::MouseButtonRelease);
    expect(studio.runtime()->heldModifiers().isEmpty(),"second floating tap cancels armed modifier");
    overlay->show();
    mouse(QEvent::MouseButtonPress);mouse(QEvent::MouseButtonRelease);
    studio.runtime()->finishNextStroke();
    expect(studio.runtime()->heldModifiers().isEmpty(),"next pen stroke releases the floating modifier");
    overlay->show();
    auto* center=overlay->findChild<QObject*>("moreMenuButton");
    auto* centerItem=qobject_cast<QQuickItem*>(center);
    auto* glyph=center->findChild<QQuickItem*>("hubGlyph");
    auto* hoverBackground=center->findChild<QQuickItem*>("hubHoverBackground");
    expect(glyph&&hoverBackground&&!hoverBackground->isVisible(),"center has no rectangular hover highlight");
    expect(QLineF(glyph->mapToItem(centerItem,{glyph->width()/2,glyph->height()/2}),{38,38}).length()<0.5,"ellipsis is centered in the inner circle");
    expect(centerItem->contains({38,38})&&centerItem->contains({69,38})&&!centerItem->contains({2,2}),"drag hit area follows the circle rather than its square bounds");
    overlay->resize(1200,900);QCoreApplication::processEvents();studio.runtime()->setRadialCenter(600,450);
    quint64 stamp=1000;
    const auto sendMouse=[&](QEvent::Type type,QPointF point){
        QMouseEvent event(type,point,point,overlay->mapToGlobal(point.toPoint()),type==QEvent::MouseMove?Qt::NoButton:Qt::LeftButton,type==QEvent::MouseButtonRelease?Qt::NoButton:Qt::LeftButton,Qt::NoModifier);
        event.setTimestamp(stamp+=20);QCoreApplication::sendEvent(overlay,&event);QCoreApplication::processEvents();
    };
    const QPointF start=studio.runtime()->radialCenter()+QPointF(25,0);
    sendMouse(QEvent::MouseButtonPress,start);sendMouse(QEvent::MouseMove,start+QPointF(20,15));sendMouse(QEvent::MouseMove,start+QPointF(120,80));sendMouse(QEvent::MouseButtonRelease,start+QPointF(120,80));
    expect(QLineF(studio.runtime()->radialCenter(),{720,530}).length()<1,"drag from any point inside center moves the radial without an initial jump");
    expect(!overlay->property("menuOpen").toBool()&&!studio.runtime()->active(),"drag release neither opens settings nor dispatches a tool");
    const auto tap=studio.runtime()->radialCenter();sendMouse(QEvent::MouseButtonPress,tap);sendMouse(QEvent::MouseButtonRelease,tap);
    expect(overlay->property("menuOpen").toBool(),"tap after drag still opens settings");
    sendMouse(QEvent::MouseButtonPress,tap);sendMouse(QEvent::MouseMove,tap+QPointF(-20,0));sendMouse(QEvent::MouseMove,tap+QPointF(-100,-60));sendMouse(QEvent::MouseButtonRelease,tap+QPointF(-100,-60));
    expect(overlay->property("menuOpen").toBool()&&QLineF(studio.runtime()->radialCenter(),{620,470}).length()<1,"open settings move with the radial and stay open after dragging");
    const auto back=studio.runtime()->radialCenter();sendMouse(QEvent::MouseButtonPress,back);sendMouse(QEvent::MouseButtonRelease,back);
    expect(!overlay->property("menuOpen").toBool(),"center tap returns to tools after dragging settings");
    QPointingDevice testPen("center test pen",3001,QInputDevice::DeviceType::Stylus,QPointingDevice::PointerType::Pen,QInputDevice::Capability::Position|QInputDevice::Capability::Pressure,1,1);
    const auto pen=[&](QEvent::Type type,QPointF p){
        const bool up=type==QEvent::TabletRelease;
        QTabletEvent event(type,&testPen,p,overlay->mapToGlobal(p.toPoint()),up?0:0.5,0,0,0,0,0,Qt::NoModifier,type==QEvent::TabletMove?Qt::NoButton:Qt::LeftButton,up?Qt::NoButton:Qt::LeftButton);
        event.setTimestamp(stamp+=20);QCoreApplication::sendEvent(overlay,&event);QCoreApplication::processEvents();
    };
    const auto penStart=studio.runtime()->radialCenter();
    pen(QEvent::TabletPress,penStart);pen(QEvent::TabletMove,penStart+QPointF(20,10));pen(QEvent::TabletMove,penStart+QPointF(80,45));pen(QEvent::TabletRelease,penStart+QPointF(80,45));
    expect(QLineF(studio.runtime()->radialCenter(),penStart+QPointF(80,45)).length()<1&&!overlay->property("menuOpen").toBool(),"stylus drags the center without opening settings");
    auto* testFinger=QTest::createTouchDevice();
    const auto touchStart=studio.runtime()->radialCenter().toPoint();
    QTest::touchEvent(overlay,testFinger).press(1,touchStart,overlay);
    QTest::touchEvent(overlay,testFinger).move(1,touchStart+QPoint(20,10),overlay);
    QTest::touchEvent(overlay,testFinger).move(1,touchStart+QPoint(65,35),overlay);
    QTest::touchEvent(overlay,testFinger).release(1,touchStart+QPoint(65,35),overlay);
    expect(QLineF(studio.runtime()->radialCenter(),touchStart+QPointF(65,35)).length()<1&&!overlay->property("menuOpen").toBool(),"touch drags the center without opening settings");
    studio.runtime()->moveRadialCenter(450,350);
    const auto longDrag=studio.runtime()->radialCenter();sendMouse(QEvent::MouseButtonPress,longDrag);
    for(int i=1;i<=35;++i){const auto at=longDrag+QPointF(i*4,i*2);sendMouse(QEvent::MouseMove,at);if(i>3)expect(studio.runtime()->dragging()&&QLineF(studio.runtime()->radialCenter(),at).length()<1,"long drag tracks every sample without cumulative jumps");}
    sendMouse(QEvent::MouseButtonRelease,longDrag+QPointF(140,70));
    expect(!studio.runtime()->dragging()&&!overlay->property("menuOpen").toBool(),"ending a long drag restores idle mode without a tap");
    studio.runtime()->moveRadialCenter(-999,-999);expect(studio.runtime()->radialCenter()==QPointF(104,104),"drag placement keeps the radial on screen near the edge");
    auto* tip=overlay->findChild<QQuickWindow*>("centerHoverTip");expect(tip&&tip!=overlay,"tooltip has its own native window outside the overlay mask");
    expect(tip->flags().testFlag(Qt::WindowStaysOnTopHint)&&tip->flags().testFlag(Qt::WindowDoesNotAcceptFocus)&&tip->flags().testFlag(Qt::WindowTransparentForInput),"tooltip is topmost without taking focus or pen input");
    studio.runtime()->setRadialCenter(600,450);
    QMetaObject::invokeMethod(tip,"reposition");
    const auto centerGlobal=floatingHub->mapToGlobal({38,38});
    expect(tip->y()+tip->height()<=centerGlobal.y()-101-8,"center tooltip sits fully above the radial outer edge with a gap");
    overlay->setPosition({0,0});studio.runtime()->moveRadialCenter(600,104);
    QMetaObject::invokeMethod(tip,"reposition");
    const auto edgeGlobal=floatingHub->mapToGlobal({38,38});
    expect(tip->y()>=edgeGlobal.y()+101+8,"top-edge placement puts the tooltip below the entire wheel instead of across buttons");
    studio.runtime()->setRadialCenter(600,450);
    QMetaObject::invokeMethod(center,"clicked",Qt::DirectConnection);
    auto* ring=qobject_cast<RadialSurface*>(floatingOptions);
    expect(ring&&ring->isVisible()&&ring->buttons().size()==6&&!surface->isVisible(),"center replaces outer tools with six settings");
    expect(ring->bronzeTheme()&&!surface->bronzeTheme(),"bronze settings contrast with the tool palette");
    expect(overlay->property("bloomProgress").toDouble()==1,"software rendering switches settings immediately");
    QVariantMap editOption;
    for(const auto& value:ring->buttons())if(value.toMap()["id"]=="edit")editOption=value.toMap();
    const double ex=editOption["cx"].toDouble(),ey=editOption["cy"].toDouble();
    ring->pressed(31,ex,ey);ring->released(31,ex,ey);
    expect(overlay->property("editingButtons").toBool()&&surface->isVisible()&&!ring->isVisible(),"Edit asks for a button without dispatching it");
    const auto selected=studio.runtime()->buttons().at(2).toMap();
    const double bx=selected["cx"].toDouble(),by=selected["cy"].toDouble();
    surface->pressed(32,bx,by);surface->released(32,bx,by);
    expect(studio.selectedId()==selected["id"].toString()&&!overlay->isVisible()&&editor->isVisible(),"button editing opens that exact button in the editor");
    overlay->show();
    studio.runtime()->toggleDock();
    QMetaObject::invokeMethod(railToggle,"clicked",Qt::DirectConnection);
    const int previousHide=hide;
    QMetaObject::invokeMethod(actionItem("hide"),"clicked",Qt::DirectConnection);
    expect(hide==previousHide+1&&!overlay->isVisible(),"rail Hide dismisses the overlay");
    engine.rootContext()->setContextProperty("desktop",static_cast<QObject*>(nullptr));
}
}
int main(int argc,char** argv){
    if(argc==5&&std::strcmp(argv[1],"--wait-for-pid")==0&&!qEnvironmentVariableIsEmpty("TABLETPRO_TEST_INSTALL_RECEIPT")){
        QSaveFile receipt(qEnvironmentVariable("TABLETPRO_TEST_INSTALL_RECEIPT"));QJsonArray args;for(int i=1;i<argc;++i)args.append(QString::fromLocal8Bit(argv[i]));
        return receipt.open(QIODevice::WriteOnly)&&receipt.write(QJsonDocument(args).toJson())>0&&receipt.commit()?0:1;
    }
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);app.setOrganizationName("TabletProTests");app.setApplicationName("RadialUpdates");app.setApplicationVersion(TABLETPRO_VERSION);QQuickStyle::setStyle("Basic");
    QTemporaryDir settings;QStandardPaths::setTestModeEnabled(true);QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    try{
        if(app.arguments().contains("--density-only")){
            displayDensityTests();wedgeClippingTests();
        }else if(app.arguments().contains("--drag-ui-only")){
            qmlRegisterType<RadialSurface>("TabletPro",1,0,"RadialSurface");
            qmlRegisterUncreatableType<MenuSession>("TabletPro",1,0,"MenuSession","Provided");
            qmlRegisterUncreatableType<StylusInput>("TabletPro",1,0,"StylusInput","Provided");
            editorDragUiTests();
        }else{parseTests();networkTests();trayTests();inPlaceParentUiTests();textureReuseTests();dockLandingTests();displayDensityTests();wedgeClippingTests();installHandoffUiTests();editorPenTriggerUiTests();selectionDismissalUiTests();parentPointerUiTests();sideButtonModeUiTests();editorDragUiTests();whatsNewTests();}
        std::cout<<checks<<" tray/update checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}
}
