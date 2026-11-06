#pragma once

#include <QAbstractNativeEventFilter>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <optional>
#include "PenStrokeMonitor.h"
#include "PenPositionTracker.h"

namespace tabletpro {

// GUI-thread adapter. The editor must be a separate, normally focused window.
// This class never raises another process, installs a keyboard hook, or runs a shell.
class WindowPlatform final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(bool nativeAvailable READ nativeAvailable CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    explicit WindowPlatform(QObject* parent = nullptr);
    ~WindowPlatform() override;

    bool nativeAvailable() const;
    QString lastError() const { return m_lastError; }

    // Call before the runtime window's first show. Never apply to the editor.
    bool configureOverlay(QQuickWindow* window);
    // Capture before showing the overlay or starting a gesture. Zero means invalid.
    Q_INVOKABLE quintptr captureForegroundTarget();
    Q_INVOKABLE bool targetUnchanged(quintptr target) const;
    Q_INVOKABLE void releaseTarget();
    Q_INVOKABLE bool shortcutSupported(const QString& key,
                                       const QStringList& modifiers) const;
    Q_INVOKABLE bool executeShortcut(const QString& key,
                                     const QStringList& modifiers,
                                     quintptr capturedTarget);
    Q_INVOKABLE bool executeText(const QString& text, quintptr capturedTarget);
    // Runtime actions wait briefly for the invocation keys / pen mouse buttons
    // to be released. Acceptance is not delivery; completion is asynchronous.
    Q_INVOKABLE bool queueShortcut(const QString& key, const QStringList& modifiers,
                                   quintptr capturedTarget);
    Q_INVOKABLE bool queueText(const QString& text, quintptr capturedTarget);
    Q_INVOKABLE bool hasQueuedAction() const { return m_queuedAction.has_value(); }
    // Silent cancellation. releaseTarget(), a new capture, and hiding the overlay
    // also discard queued input. A canceled action can never execute later.
    Q_INVOKABLE void cancelQueuedAction();
    bool setModifierHeld(const QString& key, bool held, quintptr target = 0);
    void releaseModifiers();
    bool armNextStroke(const QString& key);
    std::optional<QPointF> penPositionInOverlay() const;
    QStringList heldModifiers() const { return m_heldModifiers; }
    bool registerToggleHotkey();
    void unregisterToggleHotkey();

    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override;

signals:
    void lastErrorChanged();
    void toggleRequested(quintptr capturedTarget);
    void targetLost();
    void shortcutSimulated(const QString& key, const QStringList& modifiers);
    void textSimulated(const QString& text);
    void actionDispatchFinished(bool success, const QString& error);
    void modifiersReleased();
    void strokeModifiersReleased();

private:
    struct QueuedAction {
        QString key;
        QStringList modifiers;
        QString text;
        quintptr target = 0;
        quint16 mainKey = 0;
        bool isText = false;
    };
    bool fail(const QString& message);
    void clearError();
    bool applyOverlayStyle();
    bool cleanupOwnedKeys();
    bool dispatchEvents(const QVector<quint32>& events, quintptr target, quint16 mainKey = 0);
    bool queueAction(QueuedAction action);
    void pollQueuedAction();

    QPointer<QQuickWindow> m_overlay;
    QTimer m_targetWatch;
    QTimer m_releaseGate;
    QElapsedTimer m_releaseWait;
    qint64 m_releaseReadyAt = -1;
    std::optional<QueuedAction> m_queuedAction;
    QString m_lastError;
    quintptr m_overlayHandle = 0;
    quintptr m_capturedTarget = 0;
    quint32 m_targetProcess = 0;
    quint32 m_targetThread = 0;
    // Only key-downs successfully inserted by this adapter may enter this ledger.
    QVector<quint32> m_ownedKeys;
    QStringList m_heldModifiers;
    QStringList m_strokeModifiers;
    PenStrokeMonitor m_penStroke;
    PenPositionTracker m_penPosition;
    quint64 m_strokeGeneration=0;
    quintptr m_modifierTarget = 0;
    quint32 m_modifierProcess = 0, m_modifierThread = 0;
    bool m_hotkeyRegistered = false;
    bool m_dispatching = false;
};

} // namespace tabletpro
