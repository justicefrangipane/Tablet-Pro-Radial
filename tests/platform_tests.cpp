#include "platform/WindowPlatform.h"
#include <QGuiApplication>
#include <QEventLoop>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <cstdlib>
#include <iostream>

static void require(bool condition, const char* name) {
    if (!condition) { std::cerr << "FAILED: " << name << '\n'; std::exit(1); }
}

static void runEventLoop(int milliseconds = 60) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    tabletpro::WindowPlatform platform;
    require(!platform.nativeAvailable(), "this test is for the non-Windows simulation");
    for (const auto& key : {"A", "z", "0", "9", "F1", "F24", "Escape", "Space", "PageUp", "Left", "ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown"})
        require(platform.shortcutSupported(key, {"Ctrl", "Shift"}), key);
    for (const auto& key : {"", "F0", "F25", "F01", "F+1", "Ctrl+Z", ";", "cmd.exe", "Return"})
        require(!platform.shortcutSupported(key, {}), key);
    require(!platform.shortcutSupported("Z", {"Ctrl", "Control"}), "duplicate alias rejected");
    require(!platform.shortcutSupported("Z", {"Hyper"}), "unknown modifier rejected");
    require(platform.shortcutSupported("Z", {"Control", "Win"}), "known aliases accepted");
    for (const auto& key : {"Minus", "Equal", "BracketLeft", "BracketRight", "Backslash", "Semicolon", "Quote", "Comma", "Period", "Slash", "Backquote"})
        require(!platform.shortcutSupported(key, {}), "unsupported OEM shortcut rejected");
    int shortcuts = 0, texts = 0, losses = 0;
    QString receivedText;
    QObject::connect(&platform, &tabletpro::WindowPlatform::shortcutSimulated,
        [&](const QString& key, const QStringList& mods) {
            ++shortcuts;
            require(key == "Z" && mods == QStringList{"Ctrl"}, "canonical shortcut");
        });
    QObject::connect(&platform, &tabletpro::WindowPlatform::textSimulated,
        [&](const QString& text) { ++texts; receivedText = text; });
    QObject::connect(&platform, &tabletpro::WindowPlatform::targetLost, [&] { ++losses; });
    require(!platform.executeShortcut("Z", {"Ctrl"}, 0), "no captured target blocks action");
    auto target = platform.captureForegroundTarget();
    require(target && platform.targetUnchanged(target), "capture usable");
    require(platform.executeShortcut("z", {"control"}, target), "valid action succeeds");
    require(shortcuts == 1 && !platform.targetUnchanged(target), "action is one-shot");
    require(!platform.executeShortcut("Z", {"Ctrl"}, target), "consumed target blocks replay");
    target = platform.captureForegroundTarget();
    require(!platform.executeText("", target), "empty text rejected");
    require(!platform.executeText(QString(4097, QChar('a')), target), "oversize text rejected");
    require(!platform.executeText(QString(QChar(0)), target), "NUL text rejected");
    require(!platform.executeText(QString(QChar(0xD800)), target), "unpaired high surrogate rejected");
    require(!platform.executeText(QString(QChar(0xDC00)), target), "unpaired low surrogate rejected");
    require(texts == 0, "invalid text emits nothing");
    const QString unicode = QString::fromUtf8("Hello, 世界 🌊\n");
    require(platform.executeText(unicode, target), "valid Unicode text succeeds");
    require(texts == 1 && receivedText == unicode, "Unicode text preserved");
    target = platform.captureForegroundTarget();
    require(platform.executeText(QString(4096, QChar('a')), target), "boundary text succeeds");
    target = platform.captureForegroundTarget();
    platform.releaseTarget();
    require(!platform.targetUnchanged(target), "explicit cancellation invalidates capture");
    require(!platform.registerToggleHotkey(), "simulation never registers native hotkey");
    QQuickWindow window;
    require(platform.configureOverlay(&window), "overlay configuration succeeds");
    require(window.flags().testFlag(Qt::WindowDoesNotAcceptFocus), "overlay refuses focus");
    require(window.flags().testFlag(Qt::WindowStaysOnTopHint), "overlay topmost hint");
    require(!window.isVisible(), "configuration does not unexpectedly show window");
    require(losses == 2, "failed target actions reported");

    // The live menu must distinguish accepted gestures from delivered actions.
    // Exercise the real timer/target lifecycle without injecting native input.
    tabletpro::WindowPlatform queued;
    int deliveries = 0, completions = 0, failedCompletions = 0;
    QString queuedPayload;
    QObject::connect(&queued, &tabletpro::WindowPlatform::shortcutSimulated,
        [&](const QString& key, const QStringList& mods) {
            ++deliveries;
            queuedPayload = mods.join('+') + '+' + key;
        });
    QObject::connect(&queued, &tabletpro::WindowPlatform::textSimulated,
        [&](const QString& text) { ++deliveries; queuedPayload = text; });
    QObject::connect(&queued, &tabletpro::WindowPlatform::actionDispatchFinished,
        [&](bool success, const QString& error) {
            ++completions;
            require(!queued.hasQueuedAction(), "completion cannot retain a pending action");
            require(success == error.isEmpty(), "completion error agrees with success");
            if (!success) ++failedCompletions;
        });

    target = queued.captureForegroundTarget();
    require(!queued.queueShortcut("Unknown", {}, target), "invalid queued key rejected immediately");
    require(!queued.queueText(QString(QChar(0xD800)), target), "invalid queued Unicode rejected immediately");
    require(!queued.hasQueuedAction() && completions == 0, "invalid requests are never queued");
    require(queued.queueShortcut("z", {"control"}, target), "shortcut queue accepts valid action");
    require(queued.hasQueuedAction() && deliveries == 0 && completions == 0, "acceptance is asynchronous, not delivery");
    require(!queued.queueText("must not replace", target), "a second action cannot replace a pending action");
    require(!queued.executeText("must not bypass", target), "synchronous input cannot bypass a pending action");
    runEventLoop();
    require(deliveries == 1 && completions == 1 && queuedPayload == "Ctrl+Z", "queued shortcut delivers exactly once");
    require(!queued.targetUnchanged(target), "queued completion consumes target capture");
    runEventLoop();
    require(deliveries == 1 && completions == 1, "completed timer cannot repeat input");

    target = queued.captureForegroundTarget();
    require(queued.queueText(unicode, target), "valid text can be queued");
    runEventLoop();
    require(deliveries == 2 && completions == 2 && queuedPayload == unicode, "queued Unicode payload preserved");

    target = queued.captureForegroundTarget();
    require(queued.queueText("canceled", target), "queue before explicit cancellation");
    queued.cancelQueuedAction();
    require(!queued.hasQueuedAction(), "explicit cancellation clears queue immediately");
    runEventLoop();
    require(deliveries == 2 && completions == 2, "canceled action never delivers or emits completion");
    require(queued.queueText("released target", target), "queue before target release");
    queued.releaseTarget();
    runEventLoop();
    require(deliveries == 2 && completions == 2 && !queued.hasQueuedAction(), "target release discards pending action");

    target = queued.captureForegroundTarget();
    require(queued.queueText("obsolete gesture", target), "queue before replacement capture");
    queued.captureForegroundTarget();
    runEventLoop();
    require(deliveries == 2 && completions == 2 && !queued.hasQueuedAction(), "a new capture cancels the older gesture even for the same target");

    QQuickWindow queuedOverlay;
    require(queued.configureOverlay(&queuedOverlay), "queued overlay configuration");
    queuedOverlay.show();
    target = queued.captureForegroundTarget();
    require(queued.queueText("hidden", target), "queue before overlay hide");
    queuedOverlay.hide();
    runEventLoop();
    require(deliveries == 2 && completions == 2 && !queued.hasQueuedAction(), "hiding the overlay discards pending action");

    target = queued.captureForegroundTarget();
    require(queued.queueText("expired", target), "queue before event-loop stall");
    // A late timer may never replay an action after the 700 ms release deadline.
    QThread::msleep(720);
    runEventLoop();
    require(deliveries == 2 && completions == 3 && failedCompletions == 1, "expired action reports failure without sending input");
    require(!queued.targetUnchanged(target) && !queued.hasQueuedAction(), "timeout consumes and clears pending target");
    runEventLoop();
    require(deliveries == 2 && completions == 3, "timed-out action cannot execute later");
    std::cout << "Platform simulation: parser, Unicode bounds, one-shot capture, asynchronous delivery, cancellation, hide, and deadline passed.\n";
}
