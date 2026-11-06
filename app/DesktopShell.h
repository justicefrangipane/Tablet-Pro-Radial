#pragma once
#include "UpdateChecker.h"
#include <QSystemTrayIcon>
#include <QMenu>
#include <QPointer>
#include <QIcon>
class QDialog;
class QQuickWindow;
namespace tabletpro {
class StudioController;
class DesktopShell : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)
public:
    DesktopShell(StudioController*,QQuickWindow* overlay,QObject* parent=nullptr,bool testMode=false);
    ~DesktopShell();
    bool trayAvailable() const { return m_trayAvailable; }
    static QIcon icon(bool update=false);
    Q_INVOKABLE void showUpdates();
    Q_INVOKABLE void showUpdateLog();
    void showWhatsNewIfNeeded();
    Q_INVOKABLE void requestQuit();
    Q_INVOKABLE void cancelQuit() { m_installPending=false; }
    Q_INVOKABLE void finishQuit();
    Q_INVOKABLE void toggleOverlay();
    QMenu* menu() { return &m_menu; }
signals:
    void quitRequested();
    void exitApproved();
private:
    QDialog* showReleaseNotes(bool whatsNew);
    StudioController* m_studio;
    QQuickWindow* m_overlay;
    UpdateChecker m_updates;
    QSystemTrayIcon m_tray;
    QMenu m_menu;
    QPointer<QDialog> m_dialog;
    QPointer<QDialog> m_history;
    QPointer<QDialog> m_whatsNew;
    QTimer m_checkTimer;
    bool m_trayAvailable=false,m_installPending=false;
};
}
