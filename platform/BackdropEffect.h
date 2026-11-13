#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QRegion>
#include <QRect>
#include <optional>

namespace tabletpro {

// Glass is a host-backdrop visual with a fading opacity mask in a separate
// input-transparent native window behind Qt. Qt keeps the body-only hit region.
// A system backdrop must never be attached to the monitor-sized Qt window:
// DWMSBT_TRANSIENTWINDOW paints its entire bounds, including transparent pixels.
// Unsupported systems retain the surface's translucent painted fill.
class BackdropEffect final : public QObject {
public:
    explicit BackdropEffect(QObject* parent = nullptr);
    ~BackdropEffect() override;

    void configure(QQuickWindow* window);
    // Glass geometry is independent of the overlay's temporary input envelope.
    void setEnabled(bool enabled);
    void setVisualRegion(const QRegion& region);
    void setVisualRegion(const QRegion& region,qreal diffusion);
    void clearVisualRegion();
    // Logical pixels outside the body; this never enlarges the input mask.
    void setDiffusion(qreal distance);
    void setStrength(qreal strength);
    qreal strength() const { return m_strength; }
    bool enabled() const { return m_enabled; }

    // DWM accepted the request; this is not proof of the displayed appearance.
    // Windows accessibility, energy policy, and the renderer can affect it.
    bool requestAccepted() const { return m_requestAccepted; }
    quint64 shapeBuildCount() const { return m_shapeBuildCount; }
    QRegion visualRegion() const { return m_visualRegion.value_or(m_window ? m_window->mask() : QRegion()); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class BackdropEffectTestAccess;
    void refresh();
    void clear();

    QPointer<QQuickWindow> m_window;
    // The decoration HWND, not the overlay HWND. Owned and destroyed here.
    quintptr m_nativeHandle = 0;
    void* m_composition = nullptr;
    QRegion m_nativeRegion;
    std::optional<QRegion> m_visualRegion;
    quint64 m_shapeBuildCount = 0;
    QRect m_nativeGeometry;
    qreal m_diffusion = 0;
    qreal m_strength = 1;
    qreal m_nativeDiffusion = -1;
    bool m_refreshing = false;
    bool m_enabled = true;
    bool m_requestAccepted = false;
};

} // namespace tabletpro
