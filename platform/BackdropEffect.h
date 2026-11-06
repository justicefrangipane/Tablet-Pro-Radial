#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QRegion>
#include <QRect>

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
    // Disable before expanding the overlay mask for a docking transition.
    // Restore the small mask before enabling again. Repeated calls refresh.
    void setEnabled(bool enabled);
    // Logical pixels outside the body; this never enlarges the input mask.
    void setDiffusion(qreal distance);
    bool enabled() const { return m_enabled; }

    // DWM accepted the request; this is not proof of the displayed appearance.
    // Windows accessibility, energy policy, and the renderer can affect it.
    bool requestAccepted() const { return m_requestAccepted; }

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
    QRect m_nativeGeometry;
    qreal m_diffusion = 0;
    qreal m_nativeDiffusion = -1;
    bool m_refreshing = false;
    bool m_enabled = true;
    bool m_requestAccepted = false;
};

} // namespace tabletpro
