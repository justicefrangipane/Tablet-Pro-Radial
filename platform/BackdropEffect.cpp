#include "BackdropEffect.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QScopedValueRollback>
#include <QTransform>
#include <QLibrary>
#include <QCoreApplication>
#include <vector>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

namespace tabletpro {
namespace {
#ifdef Q_OS_WIN
constexpr DWORD systemBackdropAttribute = 38; // DWMWA_SYSTEMBACKDROP_TYPE
constexpr int noBackdrop = 1;                 // DWMSBT_NONE
constexpr wchar_t glassClass[] = L"TabletProRadialGlass032";

struct CompositionApi {
    QLibrary library{QCoreApplication::applicationDirPath()+"/lib/TabletProGlass.dll"};
    using Create=void*(__cdecl*)(HWND,const RECT*,unsigned,int,int,float,HRESULT*);
    using Update=HRESULT(__cdecl*)(void*,const RECT*,unsigned,int,int,float);
    using Destroy=void(__cdecl*)(void*);
    using Strength=HRESULT(__cdecl*)(void*,float);
    Create create=nullptr;Update update=nullptr;Destroy destroy=nullptr;Strength strength=nullptr;
    CompositionApi(){
        library.setLoadHints(QLibrary::PreventUnloadHint);
        if(library.load()){
            create=reinterpret_cast<Create>(library.resolve("TPGlass_CreateV2"));
            update=reinterpret_cast<Update>(library.resolve("TPGlass_UpdateV2"));
            destroy=reinterpret_cast<Destroy>(library.resolve("TPGlass_Destroy"));
            strength=reinterpret_cast<Strength>(library.resolve("TPGlass_SetStrength"));
        }
    }
    bool ready()const{return create&&update&&destroy&&strength;}
};
CompositionApi& compositionApi(){static CompositionApi api;return api;}

// No focus, input, taskbar entry, or command dispatch belongs to this window.
LRESULT CALLBACK glassWindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        BeginPaint(hwnd, &paint);
        EndPaint(hwnd, &paint);
        return 0;
    }
    default: return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

HWND createGlassWindow(HWND overlay)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = glassWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = glassClass;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return nullptr;
    // Share the editor owner, when there is one. Making the overlay itself the
    // owner would force this decoration ABOVE it and obscure the menu artwork.
    return CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
                           glassClass, L"Tablet Pro Radial Glass", WS_POPUP,
                           0, 0, 1, 1, GetWindow(overlay, GW_OWNER), nullptr, wc.hInstance, nullptr);
}

HRGN nativeRegion(const QRegion& region)
{
    HRGN combined = CreateRectRgn(0, 0, 0, 0);
    if (!combined)
        return nullptr;
    for (const QRect& rect : region) {
        const HRGN part = CreateRectRgn(rect.x(), rect.y(),
                                       rect.x() + rect.width(), rect.y() + rect.height());
        if (!part || CombineRgn(combined, combined, part, RGN_OR) == ERROR) {
            if (part) DeleteObject(part);
            DeleteObject(combined);
            return nullptr;
        }
        DeleteObject(part);
    }
    return combined;
}
#endif
} // namespace

BackdropEffect::BackdropEffect(QObject* parent) : QObject(parent) {}

BackdropEffect::~BackdropEffect()
{
    if (m_window) {
        m_window->removeEventFilter(this);
        disconnect(m_window, nullptr, this, nullptr);
    }
    clear();
}

void BackdropEffect::configure(QQuickWindow* window)
{
    if (m_window == window) {
        refresh();
        return;
    }
    clear();
    if (m_window) {
        m_window->removeEventFilter(this);
        disconnect(m_window, nullptr, this, nullptr);
    }
    m_window = window;
    if (!m_window)
        return;
    m_window->installEventFilter(this);
    connect(m_window, &QWindow::visibleChanged, this, [this] { refresh(); });
    connect(m_window, &QWindow::xChanged, this, [this] { refresh(); });
    connect(m_window, &QWindow::yChanged, this, [this] { refresh(); });
    connect(m_window, &QWindow::widthChanged, this, [this] { refresh(); });
    connect(m_window, &QWindow::heightChanged, this, [this] { refresh(); });
    connect(m_window, &QWindow::screenChanged, this, [this] { refresh(); });
    connect(m_window, &QObject::destroyed, this, [this] { clear(); });
    connect(m_window, &QQuickWindow::sceneGraphInitialized, this,
            [this] { refresh(); }, Qt::QueuedConnection);
    connect(m_window, &QQuickWindow::sceneGraphInvalidated, this,
            [this] { clear(); refresh(); }, Qt::QueuedConnection);
    refresh();
}

void BackdropEffect::setEnabled(bool enabled)
{
    m_enabled = enabled;
    refresh(); // Also updates mask geometry while enabled.
}

void BackdropEffect::setDiffusion(qreal distance)
{
    const qreal next = qBound(qreal(0), distance, qreal(100));
    if (qFuzzyCompare(next + 1, m_diffusion + 1)) return;
    m_diffusion = next;
    refresh();
}
void BackdropEffect::setVisualRegion(const QRegion& region)
{
    setVisualRegion(region,m_diffusion);
}
void BackdropEffect::setStrength(qreal strength)
{
    if(!std::isfinite(strength))return;
    const auto next=qBound(qreal(0),strength,qreal(1));
    if(qFuzzyCompare(next+1,m_strength+1))return;
    m_strength=next;refresh();
}
void BackdropEffect::setVisualRegion(const QRegion& region,qreal diffusion)
{
    const qreal next=qBound(qreal(0),diffusion,qreal(100));
    if(m_visualRegion && *m_visualRegion==region && qFuzzyCompare(next+1,m_diffusion+1))return;
    m_visualRegion=region;m_diffusion=next;refresh();
}
void BackdropEffect::clearVisualRegion()
{
    if(!m_visualRegion)return;
    m_visualRegion.reset();refresh();
}

bool BackdropEffect::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_window) {
        if (event->type() == QEvent::PlatformSurface) {
            const auto* surface = static_cast<QPlatformSurfaceEvent*>(event);
            if (surface->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
                clear();
            // Never call winId() during surface creation.
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        else if (event->type() == QEvent::DevicePixelRatioChange)
            refresh();
#endif
    }
    return QObject::eventFilter(watched, event);
}

void BackdropEffect::refresh()
{
    if (m_refreshing)
        return;
    QScopedValueRollback<bool> guard(m_refreshing, true);
    if (!m_window || !m_enabled || !m_window->isVisible()) {
        clear();
        return;
    }
    const QRegion viewport(QRect(QPoint(), m_window->size()));
    const QRegion shape = visualRegion().intersected(viewport);
    // An empty Qt mask means the entire window. Never make that a glass sheet,
    // including on first show or during a full-screen animation/input envelope.
    if (shape.isEmpty() || shape == viewport) {
        clear();
        return;
    }
#ifdef Q_OS_WIN
    if (QGuiApplication::platformName() != QStringLiteral("windows")) {
        clear();
        return;
    }
    const HWND overlay = reinterpret_cast<HWND>(m_window->winId());
    if (!overlay || !IsWindow(overlay)) {
        clear();
        return;
    }
    // Explicitly keep the Qt render window transparent. Acrylic and the alpha
    // swapchain must not compete over this monitor-sized HWND.
    DwmSetWindowAttribute(overlay, systemBackdropAttribute, &noBackdrop, sizeof(noBackdrop));
    BOOL composition = FALSE;
    if (FAILED(DwmIsCompositionEnabled(&composition)) || !composition) {
        clear();
        return;
    }
    const qreal scale = m_window->devicePixelRatio();
    // A translated shape keeps its cached contour, also at fractional DPI.
    const auto logicalOrigin=shape.boundingRect().topLeft();
    const QRegion pixels = QTransform::fromScale(scale, scale).map(shape.translated(-logicalOrigin))
        .translated(qRound(logicalOrigin.x()*scale),qRound(logicalOrigin.y()*scale));
    const qreal diffusion = m_diffusion * scale;
    const int padding = qCeil(diffusion);
    const QRect bounds = pixels.boundingRect().adjusted(-padding,-padding,padding,padding);
    POINT origin{0, 0};
    if (bounds.isEmpty() || !ClientToScreen(overlay, &origin)) {
        clear();
        return;
    }
    const QRegion clip = pixels.translated(-bounds.topLeft());
    const QRect geometry(QPoint(origin.x + bounds.x(), origin.y + bounds.y()), bounds.size());
    HWND glass = reinterpret_cast<HWND>(m_nativeHandle);
    if (!glass) {
        glass = createGlassWindow(overlay);
        if (!glass)
            return;
        m_nativeHandle = reinterpret_cast<quintptr>(glass);
    }
    const bool shapeChanged=!m_requestAccepted||clip!=m_nativeRegion||!qFuzzyCompare(diffusion+1,m_nativeDiffusion+1);
    // The HWND region limits hit testing. The compositor path below is what
    // actually clips the blur: a native region alone does not clip DWM acrylic.
    if (shapeChanged) {
        // The decoration is input-transparent even throughout its diffusion.
        // Composition clips each fading contour; Qt keeps the exact body mask.
        const HRGN clipHandle = nativeRegion(padding ? QRegion(QRect(QPoint(),bounds.size())) : clip);
        if (!clipHandle) {
            clear();
            return;
        }
        if (!SetWindowRgn(glass, clipHandle, TRUE)) {
            DeleteObject(clipHandle); // Windows takes ownership only on success.
            clear();
            return;
        }
        m_nativeRegion = clip;
    }
    auto& api=compositionApi();
    if(!api.ready()){clear();return;}
    if(shapeChanged){
        ++m_shapeBuildCount;
        std::vector<RECT> rectangles;
        for(const QRect& r:clip)rectangles.push_back({r.x(),r.y(),r.x()+r.width(),r.y()+r.height()});
        HRESULT result=S_OK;
        if(!m_composition)m_composition=api.create(glass,rectangles.data(),unsigned(rectangles.size()),bounds.width(),bounds.height(),float(diffusion),&result);
        else result=api.update(m_composition,rectangles.data(),unsigned(rectangles.size()),bounds.width(),bounds.height(),float(diffusion));
        if(FAILED(result)||!m_composition){
            clear(); // Unsupported systems retain the painted translucent fill.
            return;
        }
    }
    if(FAILED(api.strength(m_composition,float(m_strength)))){clear();return;}
    // hWndInsertAfter puts the glass immediately BELOW the drawing/input HWND.
    // Neither showing nor repositioning the material may steal drawing focus.
    if (!SetWindowPos(glass, overlay, geometry.x(), geometry.y(), geometry.width(), geometry.height(),
                      SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        clear();
        return;
    }
    m_nativeGeometry = geometry;
    m_nativeDiffusion = diffusion;
    m_requestAccepted = true;
    RedrawWindow(glass, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
#else
    clear();
#endif
}

void BackdropEffect::clear()
{
#ifdef Q_OS_WIN
    if(m_composition){compositionApi().destroy(m_composition);m_composition=nullptr;}
    if (m_nativeHandle) {
        const HWND glass = reinterpret_cast<HWND>(m_nativeHandle);
        if (IsWindow(glass))
            DestroyWindow(glass);
    }
#endif
    m_nativeHandle = 0;
    m_nativeRegion = {};
    m_nativeGeometry = {};
    m_nativeDiffusion = -1;
    m_requestAccepted = false;
}

} // namespace tabletpro
