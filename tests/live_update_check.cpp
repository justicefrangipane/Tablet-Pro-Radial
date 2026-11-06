// Opt-in release acceptance check. Never installs; uses temporary settings.
#include "app/UpdateChecker.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>

static bool waitFor(tabletpro::UpdateChecker& checker) {
    if (!checker.busy()) return true;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&checker, &tabletpro::UpdateChecker::changed, &loop,
                     [&] { if (!checker.busy()) loop.quit(); });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(180000);
    loop.exec();
    return !checker.busy();
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 4 || args.size() > 5 || (args.size() == 5 && args[4] != "--download")) {
        std::cerr << "Usage: radial_live_update_check OWNER/REPO CURRENT_VERSION EXPECTED_VERSION [--download]\n";
        return 2;
    }
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 3;
    tabletpro::UpdateChecker checker(nullptr, temporary.path() + "/settings.ini", nullptr, args[2]);
    if (!checker.setRepository(args[1])) return 4;
    checker.check();
    if (!waitFor(checker) || checker.release().version != args[3]) {
        std::cerr << checker.status().toStdString() << '\n';
        return 5;
    }
    const bool expectUpdate = tabletpro::UpdateChecker::newer(args[3], args[2]);
    if (checker.available() != expectUpdate || checker.release().download.isEmpty()) return 6;
    std::cout << checker.status().toStdString() << '\n';
    std::cout << "Asset bytes: " << checker.release().size << "; SHA-256: "
              << checker.release().sha256.constData() << '\n';
    if (args.contains("--download")) {
        if (!expectUpdate) return 7;
        checker.download();
        if (!waitFor(checker) || !checker.ready() || checker.progress() != 100) {
            std::cerr << checker.status().toStdString() << '\n';
            return 8;
        }
        std::cout << "Live download verified by the application updater. Nothing installed.\n";
    }
    return 0;
}
