#include "platform/BackdropEffect.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTransform>
#include <iostream>
#include <stdexcept>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace tabletpro {
class BackdropEffectTestAccess {
public:
    static quintptr handle(const BackdropEffect& effect) { return effect.m_nativeHandle; }
};
}
using namespace tabletpro;
namespace {
int checks = 0;
void check(bool ok, const char* reason) {
    ++checks;
    if (!ok) throw std::runtime_error(reason);
}
void settle() { QGuiApplication::processEvents(); }
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    try {
        QQuickWindow window;
        window.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        window.setColor(Qt::transparent);
        window.setGeometry(70, 80, 640, 480);
        BackdropEffect glass;
        glass.configure(&window);
        check(!glass.requestAccepted(), "hidden window must not acquire glass");
        window.show();
        settle();
        glass.setEnabled(true);
        check(!glass.requestAccepted(), "empty mask must not acquire monitor-sized glass");
        window.setMask(QRegion(QRect(0, 0, 640, 480)));
        glass.setEnabled(true);
        check(!glass.requestAccepted(), "full-window motion mask must not acquire glass");

        QRegion shape(QRect(60, 70, 160, 160), QRegion::Ellipse);
        shape -= QRegion(QRect(110, 120, 60, 60), QRegion::Ellipse);
        shape += QRect(270, 90, 45, 90);
        window.setMask(shape);
#ifdef Q_OS_WIN
        const HWND foreground = GetForegroundWindow();
#endif
        glass.setEnabled(true);
        settle();
#ifdef Q_OS_WIN
        // Establish support independently on a temporary non-visible window.
        const HWND probe = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1,
                                           nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        const int acrylic = 3;
        const bool supported = probe &&
            SUCCEEDED(DwmSetWindowAttribute(probe, 38, &acrylic, sizeof(acrylic)));
        if (probe) DestroyWindow(probe);
        if (supported) {
            check(glass.requestAccepted(), "supported desktop must create clipped glass");
            const HWND decoration = reinterpret_cast<HWND>(BackdropEffectTestAccess::handle(glass));
            const HWND overlay = reinterpret_cast<HWND>(window.winId());
            check(decoration && decoration != overlay, "glass must not be hosted by the overlay");
            int backdrop = 0;
            check(SUCCEEDED(DwmGetWindowAttribute(overlay, 38, &backdrop, sizeof(backdrop))) && backdrop == 1,
                  "Qt overlay must explicitly have no system backdrop");
            check(GetForegroundWindow() == foreground, "showing glass must not activate a window");
            const auto style = GetWindowLongPtrW(decoration, GWL_EXSTYLE);
            check((style & WS_EX_NOACTIVATE) && (style & WS_EX_TRANSPARENT) && !(style & WS_EX_LAYERED),
                  "glass host must be non-activating, input-transparent, and non-layered");
            check(SendMessageW(decoration, WM_NCHITTEST, 0, 0) == HTTRANSPARENT,
                  "glass host must reject hit testing");
            const QRegion physical = QTransform::fromScale(window.devicePixelRatio(), window.devicePixelRatio()).map(shape);
            const QRect bounds = physical.boundingRect();
            RECT rect{};
            check(GetWindowRect(decoration, &rect) && rect.right-rect.left == bounds.width() &&
                  rect.bottom-rect.top == bounds.height(), "native glass bounds must match scaled menu bounds");
            const HRGN native = CreateRectRgn(0,0,0,0);
            check(GetWindowRgn(decoration, native) != ERROR, "native visual region must exist");
            const QRegion local = physical.translated(-bounds.topLeft());
            bool matches = true;
            for (int y=0; y<bounds.height(); y+=3)
                for (int x=0; x<bounds.width(); x+=3)
                    if (bool(PtInRegion(native,x,y)) != local.contains(QPoint(x,y))) matches=false;
            DeleteObject(native);
            check(matches, "native region must preserve ring hole and gaps, not a rectangular sheet");
            const RECT before = rect;
            window.setPosition(window.position() + QPoint(25, 30));
            settle();
            check(GetWindowRect(decoration, &rect) && rect.left != before.left && rect.top != before.top,
                  "glass must follow overlay movement");
            window.setMask(QRegion(QRect(300,200,50,80)));
            glass.setEnabled(true);
            check(GetWindowRect(decoration,&rect) && rect.right-rect.left <= 50*window.devicePixelRatio()+2,
                  "glass must shrink when dock/menu geometry changes");
            const HWND old = decoration;
            glass.setEnabled(false);
            check(!IsWindow(old) && !glass.requestAccepted(), "Glass off must remove the native surface");
            glass.setEnabled(true);
            check(glass.requestAccepted(), "Glass on must rebuild the clipped surface");
            const HWND beforeHide = reinterpret_cast<HWND>(BackdropEffectTestAccess::handle(glass));
            window.hide();
            settle();
            check(!IsWindow(beforeHide), "hidden overlay must not leave orphan glass");
            window.show();
            settle();
            glass.setEnabled(true);
            check(glass.requestAccepted(), "reopening overlay must restore glass");
            const HWND beforeDestroy = reinterpret_cast<HWND>(BackdropEffectTestAccess::handle(glass));
            window.destroy();
            settle();
            check(!IsWindow(beforeDestroy), "surface destruction must remove glass");
        } else {
            check(!glass.requestAccepted() && !BackdropEffectTestAccess::handle(glass),
                  "unsupported Windows must fall back without an opaque helper");
        }
#else
        check(!glass.requestAccepted(), "non-Windows must retain translucent fallback");
#endif
        glass.configure(nullptr);
        check(!BackdropEffectTestAccess::handle(glass), "disconnect must release glass");
        std::cout << checks << " backdrop checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Backdrop regression: " << e.what() << '\n';
        return 1;
    }
}
