#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QRegion>
#include <QRect>

namespace tabletpro {

// Glass is a host-backdrop visual with a CompositionGeometricClip in a separate
// native window behind Qt. The HWND region only limits native hit testing.
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
    bool m_refreshing = false;
    bool m_enabled = true;
    bool m_requestAccepted = false;
};

} // namespace tabletpro
