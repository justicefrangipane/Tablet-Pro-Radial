#pragma once
#include "core/gesture.h"
#include <QObject>
#include <QPointF>
#include <QVariantList>
#include <QVariantAnimation>
#include <optional>

namespace tabletpro {
class WindowPlatform;
class MenuSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY geometryChanged)
    Q_PROPERTY(QVariantList childButtons READ childButtons NOTIFY stateChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY stateChanged)
    Q_PROPERTY(qreal dockProgress READ dockProgress NOTIFY geometryChanged)
    Q_PROPERTY(bool docked READ docked NOTIFY geometryChanged)
    Q_PROPERTY(QPointF radialCenter READ radialCenter NOTIFY geometryChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool transitioning READ transitioning NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(int durationMs READ durationMs WRITE setDurationMs NOTIFY preferencesChanged)
public:
    explicit MenuSession(bool simulation, WindowPlatform* platform, QObject* parent = nullptr);
    void setProfile(const radial::Profile& profile);
    void setReducedMotion(bool reduced);
    void setInvocationTarget(quintptr target) { invocationTarget_ = target; }
    QVariantList buttons() const;
    QVariantList childButtons() const;
    QString selectedId() const;
    qreal dockProgress() const { return progress_; }
    bool docked() const { return docked_; }
    QPointF radialCenter() const;
    bool active() const { return engine_.active() || pendingDispatch_; }
    bool transitioning() const { return animation_.state() == QAbstractAnimation::Running; }
    QString status() const { return status_; }
    int durationMs() const { return durationMs_; }
    void setDurationMs(int value);
    Q_INVOKABLE void setViewport(qreal width, qreal height);
    Q_INVOKABLE void setRadialCenter(qreal x, qreal y);
    Q_INVOKABLE void toggleDock();
    Q_INVOKABLE void replayDock();
    Q_INVOKABLE void pointerPressed(int pointerId, qreal x, qreal y);
    Q_INVOKABLE void pointerMoved(int pointerId, qreal x, qreal y);
    Q_INVOKABLE void pointerReleased(int pointerId, qreal x, qreal y);
    Q_INVOKABLE void cancel();
signals:
    void geometryChanged();
    void stateChanged();
    void preferencesChanged();
    void cue(const QString& name);
    void actionReported(const QString& description);
private:
    radial::Profile profile_;
    std::optional<radial::Profile> pending_;
    radial::GestureEngine engine_;
    WindowPlatform* platform_;
    QVariantAnimation animation_;
    bool simulation_, docked_ = false, reduced_ = false;
    int durationMs_ = 1640;
    qreal width_ = 680, height_ = 540, progress_ = 0;
    std::optional<QPointF> radialAnchor_;
    QString status_ = "Press a tool. Pull a group outward, steer, then release.";
    quintptr invocationTarget_ = 0, gestureTarget_ = 0;
    QString previousSelection_;
    bool pendingDispatch_ = false;
    QString pendingDescription_;
    std::size_t previousDepth_ = 0;
    void updateView();
    void finish();
    bool viewFits() const;
    QPointF boundedRadialCenter(QPointF center) const;
};
}
