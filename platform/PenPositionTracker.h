#pragma once
#include <QObject>
#include <QPointF>
#include <QElapsedTimer>
#include <memory>
#include <optional>

namespace tabletpro {
// Retains only the latest pen location in memory. Mouse and touch cannot replace it.
class PenPositionTracker final : public QObject {
    Q_OBJECT
public:
    explicit PenPositionTracker(QObject* parent=nullptr);
    ~PenPositionTracker();
    bool start();
    void stop();
    void observe(QPointF globalPosition);
    static bool isPenMessage(quintptr extraInfo);
    std::optional<QPointF> position() const;
protected:
    bool eventFilter(QObject* watched,QEvent* event) override;
private:
    struct Native;
    std::unique_ptr<Native> native_;
    std::optional<QPointF> position_;
    QElapsedTimer age_;
    bool started_=false;
};
}
