#pragma once
#include <QObject>
#include <memory>
#include <optional>
#include <QSet>

namespace tabletpro {
// Observes contact only. Never redirects, consumes, records or synthesizes pen input.
class PenStrokeMonitor final : public QObject {
    Q_OBJECT
public:
    explicit PenStrokeMonitor(QObject* parent=nullptr);
    ~PenStrokeMonitor();
    bool start();
    void stop();
    // Normalized input boundary shared by raw HID and Windows pen mouse promotion.
    void contact(quintptr source, bool down, bool inOwnApplication);
    void contactAtCursor(quintptr source, bool down);
signals:
    void strokeFinished();
private:
    struct Native;
    std::unique_ptr<Native> native_;
    QSet<quintptr> contactSources_;
    bool running_=false;
};
}
