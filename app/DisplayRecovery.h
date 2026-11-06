#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QPointer>
#include <QTimer>
#include <QQuickWindow>

class QScreen;
namespace tabletpro {
class MenuSession;
class BackdropEffect;

// Coalesces screen, DPI and compositor resets without changing display settings.
class DisplayRecovery final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    DisplayRecovery(QQuickWindow* window, MenuSession* session, BackdropEffect* backdrop, QObject* parent=nullptr);
    ~DisplayRecovery() override;
    void requestRecovery();
    bool pending() const { return m_timer.isActive() || m_recovering; }
    bool nativeEventFilter(const QByteArray&, void*, qintptr*) override;
signals:
    void refreshMask();
private:
    void watchScreen(QScreen* screen);
    void recover();
    QPointer<QQuickWindow> m_window;
    MenuSession* m_session;
    BackdropEffect* m_backdrop;
    QTimer m_timer;
    bool m_recovering=false;
};
}
