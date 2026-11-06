#include "WindowPlatform.h"

#include <QCoreApplication>
#include <QThread>
#include <algorithm>
#include <array>
#include <optional>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace tabletpro {
namespace {

constexpr quint32 unicodeToken = 0x10000;
constexpr quint32 upToken = 0x20000;
constexpr qint64 releaseWaitLimitMs = 700;
constexpr qint64 releaseQuietMs = 16;

struct Shortcut {
    quint16 key = 0;
    QVector<quint16> modifiers;
    QString canonicalKey;
    QStringList canonicalModifiers;
};

// Explicit virtual-key codes keep validation identical on simulation platforms.
// Punctuation intentionally waits for a keyboard-layout-aware action format.
std::optional<Shortcut> parseShortcut(const QString& rawKey, const QStringList& rawModifiers)
{
    if (rawKey.size() > 16 || rawModifiers.size() > 4)
        return std::nullopt;
    Shortcut result;
    const QString key = rawKey.trimmed().toUpper();
    if (key.size() == 1 && ((key[0] >= QLatin1Char('A') && key[0] <= QLatin1Char('Z'))
                         || (key[0] >= QLatin1Char('0') && key[0] <= QLatin1Char('9')))) {
        result.key = key[0].unicode();
        result.canonicalKey = key;
    } else if (key.startsWith(QLatin1Char('F'))) {
        bool ok = false;
        const int n = key.mid(1).toInt(&ok);
        if (!ok || n < 1 || n > 24 || key != QStringLiteral("F%1").arg(n))
            return std::nullopt;
        result.key = static_cast<quint16>(0x70 + n - 1);
        result.canonicalKey = key;
    } else {
        struct NamedKey { const char* name; quint16 code; const char* canonical; };
        static constexpr NamedKey named[] = {
            {"ESCAPE", 0x1B, "Escape"}, {"ENTER", 0x0D, "Enter"},
            {"SPACE", 0x20, "Space"}, {"TAB", 0x09, "Tab"},
            {"BACKSPACE", 0x08, "Backspace"}, {"DELETE", 0x2E, "Delete"},
            {"INSERT", 0x2D, "Insert"}, {"HOME", 0x24, "Home"},
            {"END", 0x23, "End"}, {"PAGEUP", 0x21, "PageUp"},
            {"PAGEDOWN", 0x22, "PageDown"}, {"ARROWLEFT", 0x25, "ArrowLeft"},
            {"ARROWUP", 0x26, "ArrowUp"}, {"ARROWRIGHT", 0x27, "ArrowRight"},
            {"ARROWDOWN", 0x28, "ArrowDown"}, {"LEFT", 0x25, "ArrowLeft"},
            {"UP", 0x26, "ArrowUp"}, {"RIGHT", 0x27, "ArrowRight"}, {"DOWN", 0x28, "ArrowDown"}
        };
        for (const auto& entry : named) {
            if (key == QLatin1String(entry.name)) {
                result.key = entry.code;
                result.canonicalKey = QLatin1String(entry.canonical);
                break;
            }
        }
        if (!result.key)
            return std::nullopt;
    }
    std::array<bool, 4> present{};
    for (const auto& raw : rawModifiers) {
        if (raw.size() > 16)
            return std::nullopt;
        const QString modifier = raw.trimmed().toUpper();
        int index = -1;
        if (modifier == QStringLiteral("CTRL") || modifier == QStringLiteral("CONTROL")) index = 0;
        else if (modifier == QStringLiteral("SHIFT")) index = 1;
        else if (modifier == QStringLiteral("ALT")) index = 2;
        else if (modifier == QStringLiteral("META") || modifier == QStringLiteral("WIN")) index = 3;
        if (index < 0 || present[static_cast<size_t>(index)])
            return std::nullopt;
        present[static_cast<size_t>(index)] = true;
    }
    // Use generic left-side Ctrl/Shift/Alt virtual keys and the left Windows key.
    static constexpr quint16 codes[] = {0x11, 0x10, 0x12, 0x5B};
    static constexpr const char* names[] = {"Ctrl", "Shift", "Alt", "Meta"};
    for (size_t i = 0; i < present.size(); ++i) {
        if (present[i]) {
            result.modifiers.append(codes[i]);
            result.canonicalModifiers.append(QLatin1String(names[i]));
        }
    }
    return result;
}

QString textValidationError(const QString& text)
{
    if (text.isEmpty() || text.size() > 4096)
        return QStringLiteral("Text actions must contain 1 to 4096 UTF-16 code units.");
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar unit = text[i];
        if (unit.isNull())
            return QStringLiteral("Text actions cannot contain a NUL character.");
        if (unit.isHighSurrogate()) {
            if (i + 1 >= text.size() || !text[i + 1].isLowSurrogate())
                return QStringLiteral("The text action contains invalid Unicode.");
            ++i;
        } else if (unit.isLowSurrogate()) {
            return QStringLiteral("The text action contains invalid Unicode.");
        }
    }
    return {};
}

#ifdef Q_OS_WIN
constexpr int toggleHotkeyId = 0x5450;
constexpr ULONG_PTR inputTag = 0x54505232; // TPR2; diagnostic only, not a trust marker.

bool isExtended(quint16 key)
{
    return (key >= VK_PRIOR && key <= VK_DOWN) || key == VK_INSERT
        || key == VK_DELETE || key == VK_LWIN;
}

INPUT keyboardEvent(quint32 token)
{
    INPUT event{};
    event.type = INPUT_KEYBOARD;
    const auto key = static_cast<quint16>(token & 0xFFFF);
    if (token & unicodeToken) {
        event.ki.wScan = key;
        event.ki.dwFlags = KEYEVENTF_UNICODE;
    } else {
        event.ki.wVk = key;
        event.ki.dwFlags = isExtended(key) ? KEYEVENTF_EXTENDEDKEY : 0;
    }
    if (token & upToken)
        event.ki.dwFlags |= KEYEVENTF_KEYUP;
    event.ki.dwExtraInfo = inputTag;
    return event;
}

bool conflictingKeyDown(quint16 key)
{
    // Never release a user's pre-existing modifiers to make a shortcut work.
    // The high bit is current state; the unreliable low bit is deliberately ignored.
    static constexpr int modifiers[] = {
        VK_SHIFT, VK_CONTROL, VK_MENU, VK_LSHIFT, VK_RSHIFT, VK_LCONTROL,
        VK_RCONTROL, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN
    };
    for (int modifier : modifiers) {
        if ((GetAsyncKeyState(modifier) & 0x8000) != 0)
            return true;
    }
    return key && (GetAsyncKeyState(key) & 0x8000) != 0;
}

bool invocationInputDown(quint16 mainKey)
{
    if (conflictingKeyDown(mainKey))
        return true;
    // The driver fallback is Ctrl+Alt+Space. The pen tip/barrel may also still be
    // represented by mouse buttons while its Qt release event is being handled.
    // Observe only; never synthesize releases for input this adapter did not own.
    static constexpr int invocationKeys[] = {
        VK_SPACE, VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2
    };
    for (int key : invocationKeys)
        if ((GetAsyncKeyState(key) & 0x8000) != 0)
            return true;
    return false;
}
#endif
} // namespace

WindowPlatform::WindowPlatform(QObject* parent) : QObject(parent)
{
    m_releaseGate.setInterval(8);
    m_releaseGate.setTimerType(Qt::PreciseTimer);
    connect(&m_releaseGate, &QTimer::timeout, this, &WindowPlatform::pollQueuedAction);
    m_targetWatch.setInterval(30);
    m_targetWatch.setTimerType(Qt::PreciseTimer);
    connect(&m_targetWatch, &QTimer::timeout, this, [this] {
        if (m_capturedTarget && !targetUnchanged(m_capturedTarget)) {
            releaseTarget();
            fail(QStringLiteral("The target application changed. Start a new gesture."));
            emit targetLost();
        }
    });
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installNativeEventFilter(this);
}

WindowPlatform::~WindowPlatform()
{
    cancelQueuedAction();
    m_targetWatch.stop();
    unregisterToggleHotkey();
    // Cleanup is attempted synchronously with a partial insertion. Do not retry a
    // stale ledger here: a user may since have physically pressed the same key.
    if (QCoreApplication::instance())
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool WindowPlatform::nativeAvailable() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool WindowPlatform::fail(const QString& message)
{
    if (m_lastError != message) {
        m_lastError = message;
        emit lastErrorChanged();
    }
    return false;
}

void WindowPlatform::clearError()
{
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit lastErrorChanged();
    }
}

bool WindowPlatform::configureOverlay(QQuickWindow* window)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!window)
        return fail(QStringLiteral("No runtime overlay window was supplied."));
    if (m_overlay && m_overlay != window)
        disconnect(m_overlay, nullptr, this, nullptr);
    const bool newlyConfigured = m_overlay != window;
    m_overlay = window;
    m_overlayHandle = 0;
    window->setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                     | Qt::WindowDoesNotAcceptFocus);
    if (newlyConfigured) {
        connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
            if (visible)
                applyOverlayStyle();
            else {
                const bool queued = hasQueuedAction();
                releaseTarget();
                if (queued)
                    emit targetLost();
            }
        });
    }
    if (!applyOverlayStyle())
        return false;
    clearError();
    return true;
}

bool WindowPlatform::applyOverlayStyle()
{
#ifdef Q_OS_WIN
    if (!m_overlay)
        return fail(QStringLiteral("The runtime overlay no longer exists."));
    const HWND hwnd = reinterpret_cast<HWND>(m_overlay->winId());
    if (!hwnd)
        return fail(QStringLiteral("The runtime overlay has no native window."));
    m_overlayHandle = reinterpret_cast<quintptr>(hwnd);
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!style && GetLastError() != ERROR_SUCCESS)
        return fail(QStringLiteral("Could not read the runtime window style."));
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
        (style | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW) & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW));
    if (!previous && GetLastError() != ERROR_SUCCESS)
        return fail(QStringLiteral("Could not apply the non-activating window style."));
    if (!SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED))
        return fail(QStringLiteral("Could not place the runtime above application windows."));
#endif
    return true;
}

quintptr WindowPlatform::captureForegroundTarget()
{
    Q_ASSERT(QThread::currentThread() == thread());
    releaseTarget();
#ifdef Q_OS_WIN
    const HWND hwnd = GetForegroundWindow();
    DWORD process = 0;
    const DWORD targetThread = hwnd ? GetWindowThreadProcessId(hwnd, &process) : 0;
    if (!hwnd || !targetThread || !process || process == GetCurrentProcessId()) {
        fail(QStringLiteral("Choose the drawing application before starting a gesture."));
        return 0;
    }
    m_capturedTarget = reinterpret_cast<quintptr>(hwnd);
    m_targetProcess = process;
    m_targetThread = targetThread;
    m_targetWatch.start();
#else
    // Simulation deliberately does not inspect or control other applications.
    m_capturedTarget = 1;
#endif
    clearError();
    return m_capturedTarget;
}

bool WindowPlatform::targetUnchanged(quintptr target) const
{
    if (!target || target != m_capturedTarget)
        return false;
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(target);
    if (!IsWindow(hwnd) || GetForegroundWindow() != hwnd)
        return false;
    DWORD process = 0;
    const DWORD targetThread = GetWindowThreadProcessId(hwnd, &process);
    return targetThread == m_targetThread && process == m_targetProcess;
#else
    return true;
#endif
}

void WindowPlatform::releaseTarget()
{
    cancelQueuedAction();
    m_targetWatch.stop();
    m_capturedTarget = 0;
    m_targetProcess = 0;
    m_targetThread = 0;
}

bool WindowPlatform::shortcutSupported(const QString& key, const QStringList& modifiers) const
{
    return parseShortcut(key, modifiers).has_value();
}

bool WindowPlatform::cleanupOwnedKeys()
{
#ifdef Q_OS_WIN
    // A partial batch may leave only this adapter's inserted key-downs unmatched.
    // Release those in reverse order; never send a blanket Ctrl/Alt/Shift reset.
    for (int attempt = 0; attempt < 3 && !m_ownedKeys.isEmpty(); ++attempt) {
        QVector<INPUT> releases;
        releases.reserve(m_ownedKeys.size());
        for (auto it = m_ownedKeys.crbegin(); it != m_ownedKeys.crend(); ++it)
            releases.append(keyboardEvent(*it | upToken));
        const UINT inserted = SendInput(static_cast<UINT>(releases.size()), releases.data(), sizeof(INPUT));
        for (UINT i = 0; i < inserted && !m_ownedKeys.isEmpty(); ++i)
            m_ownedKeys.removeLast();
        if (!inserted)
            break;
    }
#endif
    return m_ownedKeys.isEmpty();
}

bool WindowPlatform::executeShortcut(const QString& key, const QStringList& modifiers,
                                      quintptr capturedTarget)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto parsed = parseShortcut(key, modifiers);
    if (!parsed)
        return fail(QStringLiteral("This key or modifier is not supported."));
    QVector<quint32> events;
    events.reserve(2 * parsed->modifiers.size() + 2);
    for (quint16 modifier : parsed->modifiers)
        events.append(modifier);
    events.append(parsed->key);
    events.append(parsed->key | upToken);
    for (auto it = parsed->modifiers.crbegin(); it != parsed->modifiers.crend(); ++it)
        events.append(*it | upToken);
    if (!dispatchEvents(events, capturedTarget, parsed->key))
        return false;
#ifndef Q_OS_WIN
    emit shortcutSimulated(parsed->canonicalKey, parsed->canonicalModifiers);
#endif
    return true;
}

bool WindowPlatform::executeText(const QString& text, quintptr capturedTarget)
{
    Q_ASSERT(QThread::currentThread() == thread());
    // Validate the full payload before inserting any input. QString can contain
    // malformed UTF-16; a JSON/text editor should not be able to send half a pair.
    if (const auto error = textValidationError(text); !error.isEmpty())
        return fail(error);
    QVector<quint32> events;
    events.reserve(text.size() * 2);
    for (const QChar unit : text) {
        const quint32 down = unicodeToken | unit.unicode();
        events.append(down);
        events.append(down | upToken);
    }
    if (!dispatchEvents(events, capturedTarget))
        return false;
#ifndef Q_OS_WIN
    emit textSimulated(text);
#endif
    return true;
}

bool WindowPlatform::queueShortcut(const QString& key, const QStringList& modifiers,
                                    quintptr capturedTarget)
{
    Q_ASSERT(QThread::currentThread() == thread());
    const auto parsed = parseShortcut(key, modifiers);
    if (!parsed)
        return fail(QStringLiteral("This key or modifier is not supported."));
    return queueAction({parsed->canonicalKey, parsed->canonicalModifiers, {},
                        capturedTarget, parsed->key, false});
}

bool WindowPlatform::queueText(const QString& text, quintptr capturedTarget)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (const auto error = textValidationError(text); !error.isEmpty())
        return fail(error);
    return queueAction({{}, {}, text, capturedTarget, 0, true});
}

bool WindowPlatform::queueAction(QueuedAction action)
{
    if (m_queuedAction || m_dispatching)
        return fail(QStringLiteral("An action is already waiting to be sent."));
    if (!targetUnchanged(action.target)) {
        releaseTarget();
        emit targetLost();
        return fail(QStringLiteral("The target application changed. No input was sent."));
    }
    if (!m_ownedKeys.isEmpty())
        return fail(QStringLiteral("A previous input batch could not be released. Release any stuck keys and restart the menu."));
    m_queuedAction = std::move(action);
    m_releaseReadyAt = -1;
    m_releaseWait.start();
    // Even an already released input waits for a subsequent event-loop turn.
    // MenuSession can reliably display pending state before any completion signal.
    m_releaseGate.start();
    clearError();
    return true;
}

void WindowPlatform::cancelQueuedAction()
{
    m_releaseGate.stop();
    m_queuedAction.reset();
    m_releaseWait.invalidate();
    m_releaseReadyAt = -1;
}

void WindowPlatform::pollQueuedAction()
{
    if (!m_queuedAction) {
        m_releaseGate.stop();
        return;
    }
    if (!targetUnchanged(m_queuedAction->target)) {
        releaseTarget();
        fail(QStringLiteral("The target application changed. No input was sent."));
        emit targetLost();
        return;
    }
    const qint64 elapsed = m_releaseWait.elapsed();
    // Check the deadline before readiness, so a stalled GUI event loop cannot
    // unexpectedly execute an old gesture when it resumes much later.
    if (elapsed >= releaseWaitLimitMs) {
        releaseTarget();
        const QString error = QStringLiteral("Release the pen button and keyboard keys, then try again. No input was sent.");
        fail(error);
        emit actionDispatchFinished(false, error);
        return;
    }
#ifdef Q_OS_WIN
    if (invocationInputDown(m_queuedAction->mainKey)) {
        m_releaseReadyAt = -1;
        return;
    }
#endif
    if (m_releaseReadyAt < 0) {
        m_releaseReadyAt = elapsed;
        return;
    }
    if (elapsed - m_releaseReadyAt < releaseQuietMs)
        return;
    const QueuedAction action = *m_queuedAction;
    cancelQueuedAction();
    const bool success = action.isText
        ? executeText(action.text, action.target)
        : executeShortcut(action.key, action.modifiers, action.target);
    emit actionDispatchFinished(success, success ? QString{} : m_lastError);
}

bool WindowPlatform::dispatchEvents(const QVector<quint32>& tokens, quintptr target, quint16 mainKey)
{
    if (m_dispatching || m_queuedAction)
        return fail(QStringLiteral("An action is already being dispatched."));
    if (!targetUnchanged(target)) {
        releaseTarget();
        emit targetLost();
        return fail(QStringLiteral("The target application changed. No input was sent."));
    }
    if (!m_ownedKeys.isEmpty()) {
        // Refuse all future dispatch after an incomplete cleanup. An explicit restart
        // is preferable to releasing a potentially user-held key on a later gesture.
        return fail(QStringLiteral("A previous input batch could not be released. Release any stuck keys and restart the menu."));
    }
#ifdef Q_OS_WIN
    if (conflictingKeyDown(mainKey))
        return fail(QStringLiteral("Release the keyboard modifiers and shortcut key, then try again."));
    QVector<INPUT> events;
    events.reserve(tokens.size());
    for (quint32 token : tokens)
        events.append(keyboardEvent(token));
    // Final foreground check immediately before one contiguous insertion. SendInput
    // is global input, not a targeted message API; there is no atomic focus+send API.
    if (!targetUnchanged(target)) {
        releaseTarget();
        emit targetLost();
        return fail(QStringLiteral("The target application changed. No input was sent."));
    }
    m_dispatching = true;
    SetLastError(ERROR_SUCCESS);
    const UINT inserted = SendInput(static_cast<UINT>(events.size()), events.data(), sizeof(INPUT));
    const DWORD sendError = GetLastError();
    // SendInput inserts the array serially. Track only its successfully inserted prefix.
    for (UINT i = 0; i < inserted && i < static_cast<UINT>(events.size()); ++i) {
        const auto token = tokens[static_cast<qsizetype>(i)];
        if (token & upToken)
            m_ownedKeys.removeOne(token & ~upToken);
        else
            m_ownedKeys.append(token);
    }
    const bool complete = inserted == static_cast<UINT>(events.size());
    const bool released = cleanupOwnedKeys();
    m_dispatching = false;
    releaseTarget();
    if (!complete) {
        return fail(QStringLiteral("Windows inserted %1 of %2 input events (error %3). %4")
            .arg(inserted).arg(events.size()).arg(sendError)
            .arg(released ? QStringLiteral("Owned keys were released. Elevated apps may block input.")
                          : QStringLiteral("Owned keys could not all be released. Release any stuck keys and restart the menu.")));
    }
#else
    Q_UNUSED(tokens)
    Q_UNUSED(mainKey)
    releaseTarget();
#endif
    clearError();
    return true;
}

bool WindowPlatform::registerToggleHotkey()
{
    Q_ASSERT(QThread::currentThread() == thread());
#ifdef Q_OS_WIN
    if (m_hotkeyRegistered)
        return true;
    if (!RegisterHotKey(nullptr, toggleHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_SPACE))
        return fail(QStringLiteral("Ctrl+Alt+Space is already in use or could not be registered."));
    m_hotkeyRegistered = true;
    clearError();
    return true;
#else
    return fail(QStringLiteral("Global hotkeys are implemented only on Windows in this build."));
#endif
}

void WindowPlatform::unregisterToggleHotkey()
{
#ifdef Q_OS_WIN
    if (m_hotkeyRegistered)
        UnregisterHotKey(nullptr, toggleHotkeyId);
#endif
    m_hotkeyRegistered = false;
}

bool WindowPlatform::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if ((eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") || !message)
        return false;
    const auto* msg = static_cast<MSG*>(message);
    if (m_overlay && m_overlayHandle && msg->hwnd == reinterpret_cast<HWND>(m_overlayHandle)) {
        if (msg->message == WM_MOUSEACTIVATE) {
            if (result)
                *result = MA_NOACTIVATE;
            return true;
        }
#ifdef WM_POINTERACTIVATE
        if (msg->message == WM_POINTERACTIVATE) {
            if (result)
                *result = PA_NOACTIVATE;
            return true;
        }
#endif
    }
    if (m_hotkeyRegistered && msg->message == WM_HOTKEY
        && msg->wParam == static_cast<WPARAM>(toggleHotkeyId)) {
        const auto target = captureForegroundTarget();
        emit toggleRequested(target);
        if (result)
            *result = 0;
        return true;
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return false;
}

} // namespace tabletpro
