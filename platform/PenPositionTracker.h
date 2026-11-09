#pragma once
#include <QObject>
#include <QPointF>
#include <QRectF>
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
    void observeNative(QPointF position);
    void clearPosition();
    static bool isPenMessage(quintptr extraInfo);
    std::optional<QPointF> position() const;
    static std::optional<QPointF> mapHidPosition(QPointF raw, QRectF logicalRange, QRectF display, int rotation);
signals:
    void contactObserved(quintptr source, bool down, bool inOwnApplication);
    void deviceLost();
    void proximityLeft();
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
