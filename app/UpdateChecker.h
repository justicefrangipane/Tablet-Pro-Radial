#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QSaveFile>
#include <QCryptographicHash>
#include <memory>

namespace tabletpro {
struct UpdateRelease {
    QString version, notes;
    QUrl page, download;
    QByteArray sha256;
    qint64 size = 0;
};
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    static constexpr qint64 MaxInstallerBytes = 512LL * 1024 * 1024;
    static constexpr int MaxMetadataBytes = 256 * 1024;
    explicit UpdateChecker(QObject* parent=nullptr, const QString& settingsFile={},
                           QNetworkAccessManager* network=nullptr, const QString& version={});
    static QString normalizeRepository(const QString& input);
    static bool parseRelease(const QByteArray&, const QString& repository, UpdateRelease&, QString& error);
    static bool newer(const QString& candidate, const QString& current);
    static bool allowedRedirect(const QUrl&);
    QString repository() const { return m_repository; }
    QString currentVersion() const { return m_version; }
    QString status() const { return m_status; }
    QString lastChecked() const;
    const UpdateRelease& release() const { return m_release; }
    bool automatic() const { return m_automatic; }
    bool busy() const { return !m_reply.isNull(); }
    bool available() const { return newer(m_release.version,m_version); }
    bool ready() const { return !m_readyFile.isEmpty(); }
    bool installing() const { return m_installStarted; }
    int progress() const { return m_release.size ? int(100*m_received/m_release.size) : 0; }
    bool setRepository(const QString&);
    void setAutomatic(bool);
    void check(bool manual=true);
    void download();
    void cancel();
    bool launchInstaller(const QString& relaunchMode="radial");
signals:
    void changed();
    void updateFound();
private:
    std::unique_ptr<QSettings> m_settings;
    QNetworkAccessManager* m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_deadline;
    QString m_repository,m_version,m_status,m_failure,m_readyFile,m_downloadDirectory;
    bool m_automatic=true,m_downloading=false,m_installStarted=false;
    UpdateRelease m_release;
    QByteArray m_body;
    std::unique_ptr<QSaveFile> m_file;
    std::unique_ptr<QCryptographicHash> m_hash;
    qint64 m_received=0;
    int m_redirects=0;
    QNetworkReply* request(const QUrl&,bool binary);
    void requestDownload(const QUrl&);
    void consumeDownload();
    void fail(const QString&);
};
}
