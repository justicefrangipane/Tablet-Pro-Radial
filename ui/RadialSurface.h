#pragma once

#include <QQuickPaintedItem>
#include <QVariantList>
#include <QVariantAnimation>
#include <QRectF>
#include <memory>

// Rendering and pointer capture only. Menu meaning, gesture decisions and action
// dispatch belong to the portable core / controller, not this surface.
class RadialSurface : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QVariantList buttons READ buttons WRITE setButtons NOTIFY buttonsChanged)
    Q_PROPERTY(QVariantList childButtons READ childButtons WRITE setChildButtons NOTIFY childButtonsChanged)
    Q_PROPERTY(QString selectedId READ selectedId WRITE setSelectedId NOTIFY selectedIdChanged)
    Q_PROPERTY(qreal dockProgress READ dockProgress WRITE setDockProgress NOTIFY dockProgressChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)
    Q_PROPERTY(bool glassEnabled READ glassEnabled WRITE setGlassEnabled NOTIFY glassEnabledChanged)
    Q_PROPERTY(bool continuousRail READ continuousRail WRITE setContinuousRail NOTIFY continuousRailChanged)
    // Conservative bounds of current and retiring children, including motion.
    Q_PROPERTY(QRectF visualBounds READ visualBounds NOTIFY visualBoundsChanged)
    Q_PROPERTY(bool childAnimating READ childAnimating NOTIFY visualBoundsChanged)
public:
    explicit RadialSurface(QQuickItem *parent = nullptr);
    ~RadialSurface() override;
    QVariantList buttons() const { return m_buttons; }
    QVariantList childButtons() const { return m_children; }
    QString selectedId() const { return m_selectedId; }
    qreal dockProgress() const { return m_dockProgress; }
    bool reducedMotion() const { return m_reducedMotion; }
    bool glassEnabled() const { return m_glassEnabled; }
    bool continuousRail() const { return m_continuousRail; }
    QRectF visualBounds() const;
    bool childAnimating() const { return m_childAnimation.state()==QAbstractAnimation::Running || m_retireAnimation.state()==QAbstractAnimation::Running; }
    void setButtons(const QVariantList &value);
    void setChildButtons(const QVariantList &value);
    void setSelectedId(const QString &value);
    void setDockProgress(qreal value);
    void setReducedMotion(bool value);
    void setGlassEnabled(bool value);
    void setContinuousRail(bool value);
    Q_INVOKABLE QString hitRoot(qreal x, qreal y) const;
    void paint(QPainter *painter) override;
signals:
    void buttonsChanged();
    void childButtonsChanged();
    void selectedIdChanged();
    void dockProgressChanged();
    void reducedMotionChanged();
    void glassEnabledChanged();
    void continuousRailChanged();
    void visualBoundsChanged();
    void pressed(int pointerId, qreal x, qreal y);
    void moved(int pointerId, qreal x, qreal y);
    void released(int pointerId, qreal x, qreal y);
    void canceled();
protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void touchEvent(QTouchEvent *event) override;
    void touchUngrabEvent() override;
private:
    void cancelPointer();
    bool prepareGeometry();
    struct State;
    std::unique_ptr<State> m_state;
    QVariantList m_buttons, m_children, m_retiringChildren;
    QString m_selectedId;
    qreal m_dockProgress = 0;
    qreal m_childReveal = 1, m_retireProgress = 1, m_retireReveal = 1;
    QVariantAnimation m_childAnimation, m_retireAnimation;
    bool m_reducedMotion = false, m_dirty = true, m_pointerActive = false;
    bool m_glassEnabled = false;
    bool m_continuousRail = false;
    int m_pointerId = -1;
};
