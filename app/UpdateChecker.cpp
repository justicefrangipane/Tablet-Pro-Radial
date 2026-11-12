#include "UpdateChecker.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>
#include <cmath>

namespace tabletpro {
namespace {
bool validVersion(const QString& value) {
    return QRegularExpression("\\A(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\z").match(value).hasMatch();
}
}
QString UpdateChecker::normalizeRepository(const QString& input) {
    auto value=input.trimmed();
    if(value.startsWith("https://github.com/"))value.remove(0,19);
    if(value.endsWith('/'))value.chop(1);
    if(value.endsWith(".git"))value.chop(4);
    if(!QRegularExpression("\\A[A-Za-z0-9](?:[A-Za-z0-9-]{0,38})/[A-Za-z0-9][A-Za-z0-9_.-]{0,99}\\z").match(value).hasMatch())return {};
    return value;
}
bool UpdateChecker::newer(const QString& candidate,const QString& current) {
    return validVersion(candidate)&&validVersion(current)&&QVersionNumber::fromString(candidate)>QVersionNumber::fromString(current);
}
bool UpdateChecker::allowedRedirect(const QUrl& url) {
    return url.scheme()=="https"&&url.userInfo().isEmpty()&&url.port()==-1&&url.fragment().isEmpty()&&
        (url.host()=="release-assets.githubusercontent.com"||url.host()=="objects.githubusercontent.com");
}
bool UpdateChecker::parseRelease(const QByteArray& bytes,const QString& repository,UpdateRelease& output,QString& error) {
    output={};error="GitHub returned invalid release information.";
    if(bytes.size()>MaxMetadataBytes||repository.isEmpty()||normalizeRepository(repository)!=repository)return false;
    const auto doc=QJsonDocument::fromJson(bytes);
    if(!doc.isObject())return false;
    const auto root=doc.object();
    if(!root["draft"].isBool()||root["draft"].toBool()||!root["prerelease"].isBool()||root["prerelease"].toBool())return false;
    const auto tag=root["tag_name"].toString();
    if(!tag.startsWith('v')||!validVersion(tag.mid(1)))return false;
    const auto version=tag.mid(1);
    const QString page="https://github.com/"+repository+"/releases/tag/"+tag;
    if(root["html_url"].toString()!=page||!root["assets"].isArray())return false;
    const auto name="Tablet-Pro-Radial-"+version+"-Windows-x64-Setup.exe";
    const auto url="https://github.com/"+repository+"/releases/download/"+tag+"/"+name;
    bool found=false;
    for(const auto& item:root["assets"].toArray()) {
        const auto asset=item.toObject();if(asset["name"].toString()!=name)continue;
        if(found)return false;found=true;
        const auto digest=asset["digest"].toString();const auto size=asset["size"].toDouble(-1);
        if(asset["state"].toString()!="uploaded"||asset["browser_download_url"].toString()!=url||
           !QRegularExpression("\\Asha256:[0-9a-f]{64}\\z").match(digest).hasMatch()||
           !std::isfinite(size)||size<1024||size>MaxInstallerBytes||std::floor(size)!=size)return false;
        output.download=QUrl(url);output.sha256=digest.mid(7).toLatin1();output.size=qint64(size);
    }
    output.version=version;output.page=QUrl(page);output.notes=root["body"].toString().left(16000);
    error.clear();return true;
}
UpdateChecker::UpdateChecker(QObject* parent,const QString& settingsFile,QNetworkAccessManager* network,const QString& version)
    :QObject(parent),m_settings(settingsFile.isEmpty()?std::make_unique<QSettings>():std::make_unique<QSettings>(settingsFile,QSettings::IniFormat)),
      m_network(network?network:new QNetworkAccessManager(this)),m_version(version.isEmpty()?QCoreApplication::applicationVersion():version) {
    m_network->setStrictTransportSecurityEnabled(true);
    m_downloadDirectory=settingsFile.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/updates":QFileInfo(settingsFile).absolutePath()+"/downloads";
    m_repository=normalizeRepository(m_settings->value("updates/repository").toString());
    if(m_repository.isEmpty()) {
        QFile config(QCoreApplication::applicationDirPath()+"/update-source.json");
        if(config.open(QIODevice::ReadOnly)&&config.size()<4096)
            m_repository=normalizeRepository(QJsonDocument::fromJson(config.readAll()).object()["repository"].toString());
    }
    m_automatic=m_settings->value("updates/automatic",true).toBool();
    m_status=m_repository.isEmpty()?"Set the Radial GitHub repository to enable updates.":"Updates have not been checked yet.";
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this]{
        if(m_reply){m_failure="The update request timed out. Check your connection and try again.";m_reply->abort();}
    });
}
QString UpdateChecker::lastChecked() const {
    const auto date=m_settings->value("updates/lastChecked").toDateTime();
    return date.isValid()?"Last checked: "+QLocale().toString(date.toLocalTime(),QLocale::ShortFormat):QString{};
}
bool UpdateChecker::setRepository(const QString& input) {
    if(busy())return false;
    const auto repo=normalizeRepository(input);
    if(!input.trimmed().isEmpty()&&repo.isEmpty()){m_status="Enter a GitHub repository as owner/name or its https://github.com URL.";emit changed();return false;}
    if(repo==m_repository)return true;
    m_repository=repo;m_release={};m_readyFile.clear();m_received=0;
    m_settings->remove("updates/lastAttempt");m_settings->remove("updates/lastChecked");
    m_settings->setValue("updates/repository",repo);
    m_status=repo.isEmpty()?"Set the Radial GitHub repository to enable updates.":"Ready to check GitHub.";
    emit changed();return true;
}
void UpdateChecker::setAutomatic(bool value) { m_automatic=value;m_settings->setValue("updates/automatic",value);emit changed(); }
QNetworkReply* UpdateChecker::request(const QUrl& url,bool binary) {
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute,QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute,QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::AuthenticationReuseAttribute,QNetworkRequest::Manual);
    req.setTransferTimeout(30000);
    req.setRawHeader("User-Agent","TabletProRadial/"+m_version.toLatin1());
    req.setRawHeader("Accept",binary?"application/octet-stream":"application/vnd.github+json");
    req.setRawHeader("Accept-Encoding","identity");
    if(!binary)req.setRawHeader("X-GitHub-Api-Version","2022-11-28");
    auto* reply=m_network->get(req);reply->setReadBufferSize(256*1024);m_reply=reply;
    m_deadline.start(binary?60000:20000);return reply;
}
void UpdateChecker::check(bool manual) {
    if(busy()||ready()||(!manual&&!m_automatic))return;
    if(m_repository.isEmpty()){m_status="Set the Radial GitHub repository to enable updates.";emit changed();return;}
    const auto now=QDateTime::currentDateTimeUtc();const auto last=m_settings->value("updates/lastAttempt").toDateTime();
    const auto interval=m_settings->value("updates/lastSucceeded",false).toBool()?21600:1800;
    if(!manual&&last.isValid()&&last<=now&&last.secsTo(now)<interval)return;
    m_settings->setValue("updates/lastAttempt",now);m_settings->setValue("updates/lastSucceeded",false);
    m_release={};m_failure.clear();m_body.clear();m_downloading=false;m_status="Checking GitHub for updates...";
    auto* reply=request(QUrl("https://api.github.com/repos/"+m_repository+"/releases/latest"),false);
    const auto consume=[this,reply]{
        if(reply!=m_reply||!m_failure.isEmpty())return;
        m_body+=reply->read(MaxMetadataBytes+1-m_body.size());
        if(m_body.size()>MaxMetadataBytes){m_failure="The update response was too large.";reply->abort();}
    };
    connect(reply,&QNetworkReply::readyRead,this,consume);
    connect(reply,&QNetworkReply::finished,this,[this,reply,consume]{
        if(reply!=m_reply)return;consume();m_deadline.stop();
        const int http=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;UpdateRelease release;
        if(!m_failure.isEmpty())error=m_failure;
        else if(http==404)error="No published release was found. Check the repository name and public access.";
        else if(http==403||http==429)error="GitHub's request limit was reached. Try again later.";
        else if(reply->error()!=QNetworkReply::NoError||http!=200)error="Could not check GitHub. Check your connection and try again.";
        else if(!parseRelease(m_body,m_repository,release,error)){}
        m_reply.clear();reply->deleteLater();m_body.clear();
        if(!error.isEmpty()){fail(error);return;}
        m_release=release;m_settings->setValue("updates/lastChecked",QDateTime::currentDateTimeUtc());m_settings->setValue("updates/lastSucceeded",true);
        m_status=available()?"Version "+release.version+" is available.":"You're up to date ("+m_version+").";
        if(available()&&release.download.isEmpty())m_status+=" This release has no verified Windows x64 installer yet.";
        emit changed();if(available())emit updateFound();
    });
    emit changed();
}
void UpdateChecker::download() {
    if(busy()||ready()||!available()||m_release.download.isEmpty())return;
    const auto folder=m_downloadDirectory;
    if(!QDir().mkpath(folder)){fail("Could not create the update download folder.");return;}
    m_file=std::make_unique<QSaveFile>(folder+"/Tablet-Pro-Radial-"+m_release.version+"-"+QString::fromLatin1(m_release.sha256.left(12))+"-Setup.exe");
    m_file->setDirectWriteFallback(false);
    if(!m_file->open(QIODevice::WriteOnly)){fail("Could not save the update. Check available disk space.");return;}
    m_hash=std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_received=0;m_redirects=0;m_failure.clear();m_downloading=true;m_status="Downloading the update...";
    requestDownload(m_release.download);
}
void UpdateChecker::consumeDownload() {
    auto* reply=m_reply.data();if(!reply||!m_failure.isEmpty()||reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200)return;
    const auto size=reply->header(QNetworkRequest::ContentLengthHeader);
    if(size.isValid()&&size.toLongLong()!=m_release.size){m_failure="The installer size did not match GitHub's release.";reply->abort();return;}
    while(reply->bytesAvailable()>0) {
        const auto bytes=reply->read(64*1024);if(bytes.isEmpty())break;
        if(m_received+bytes.size()>m_release.size||!m_file||m_file->write(bytes)!=bytes.size()){
            m_failure="The download exceeded its expected size or could not be saved.";reply->abort();return;
        }
        m_received+=bytes.size();m_hash->addData(bytes);
    }
    m_deadline.start(60000);emit changed();
}
void UpdateChecker::requestDownload(const QUrl& url) {
    auto* reply=request(url,true);
    connect(reply,&QNetworkReply::readyRead,this,[this,reply]{if(reply==m_reply)consumeDownload();});
    connect(reply,&QNetworkReply::finished,this,[this,reply]{
        if(reply!=m_reply)return;
        if(reply->error()==QNetworkReply::NoError)consumeDownload();
        m_deadline.stop();const int http=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto next=reply->url().resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
        const auto networkError=reply->error();m_reply.clear();reply->deleteLater();
        if(!m_failure.isEmpty()){fail(m_failure);return;}
        if(http==301||http==302||http==303||http==307||http==308) {
            if(++m_redirects<=3&&allowedRedirect(next)){requestDownload(next);return;}
            fail("GitHub redirected the download to an unexpected address.");return;
        }
        if(http!=200||networkError!=QNetworkReply::NoError){fail("The update download failed. Try again when you're online.");return;}
        if(m_received!=m_release.size||m_hash->result().toHex()!=m_release.sha256){fail("The download checksum did not match. Nothing was installed.");return;}
        const auto path=m_file->fileName();
        if(!m_file->commit()){fail("Could not save the verified update.");return;}
        m_readyFile=path;m_file.reset();m_hash.reset();m_downloading=false;
        m_status="Update verified. Install update will close Radial, install, and reopen it automatically.";emit changed();
    });
    emit changed();
}
void UpdateChecker::fail(const QString& reason) {
    m_deadline.stop();m_status=reason;m_file.reset();m_hash.reset();m_downloading=false;m_readyFile.clear();emit changed();
}
void UpdateChecker::cancel() { if(m_reply){m_failure="Update request cancelled.";m_reply->abort();} }
bool UpdateChecker::launchInstaller(const QString& relaunchMode) {
    if(!ready()||busy()||m_installStarted)return false;
    if(relaunchMode!="radial"&&relaunchMode!="docked"&&relaunchMode!="editor")return false;
    QFile file(m_readyFile);QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!file.open(QIODevice::ReadOnly)||file.size()!=m_release.size||!hash.addData(&file)||hash.result().toHex()!=m_release.sha256){fail("The downloaded installer changed. Download it again.");return false;}
    file.close();
    if(!QProcess::startDetached(m_readyFile,{"--wait-for-pid",QString::number(QCoreApplication::applicationPid()),"--relaunch-mode",relaunchMode})){
        m_status="Could not start the installer. Radial is still running.";emit changed();return false;
    }
    m_installStarted=true;m_status="Closing Radial to install the update. It will reopen automatically.";emit changed();
    return true;
}
}
