#include "app/UpdateChecker.h"
#include "app/DesktopShell.h"
#include "app/StudioController.h"
#include "ui/RadialSurface.h"
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
    studio.runtime()->setReducedMotion(true);
    studio.runtime()->toggleDock();
    overlay->show();
    auto* rail=overlay->findChild<QQuickItem*>("overlayRail");
    auto* settings=overlay->findChild<QQuickItem*>("railSettingsPanel");
    auto* railToggle=overlay->findChild<QObject*>("railSettingsButton");
    auto* surface=overlay->findChild<RadialSurface*>("runtimeSurface");
    auto* floatingHub=overlay->findChild<QQuickItem*>("overlayToolbar");
    auto* floatingOptions=overlay->findChild<QQuickItem*>("overlayMorePanel");
    expect(rail&&settings&&railToggle&&surface&&floatingHub&&floatingOptions,"continuous rail controls exist");
    expect(rail->isVisible()&&!floatingHub->isVisible()&&surface->isVisible(),"docked rail replaces the circular hub");
    for(const auto height:{480,768,1080}){
        overlay->resize(720,height);QCoreApplication::processEvents();
        expect(rail->width()==44&&rail->x()==13&&rail->y()>=0&&rail->y()+rail->height()<=height,"rail stays inside the viewport at supported desktop sizes");
        for(const auto& value:studio.runtime()->buttons()){
            const auto b=value.toMap();expect(b["dockX"].toDouble()-22==rail->x()&&b["dockX"].toDouble()+22==rail->x()+rail->width(),"tool and rail edges align exactly");
        }
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
        expect(action->property("iconColor").value<QColor>()!=overlay->findChild<QObject*>("dockModeButton")->property("iconColor").value<QColor>(),"settings icons are distinct from the drawing-control palette");
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
    studio.runtime()->toggleDock();
    QMetaObject::invokeMethod(railToggle,"clicked",Qt::DirectConnection);
    const int previousHide=hide;
    QMetaObject::invokeMethod(actionItem("hide"),"clicked",Qt::DirectConnection);
    expect(hide==previousHide+1&&!overlay->isVisible(),"rail Hide dismisses the overlay");
    engine.rootContext()->setContextProperty("desktop",static_cast<QObject*>(nullptr));
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);app.setOrganizationName("TabletProTests");app.setApplicationName("RadialUpdates");app.setApplicationVersion("0.3.3");QQuickStyle::setStyle("Basic");
    QTemporaryDir settings;QStandardPaths::setTestModeEnabled(true);QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    try{parseTests();networkTests();trayTests();std::cout<<checks<<" tray/update checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}
}
