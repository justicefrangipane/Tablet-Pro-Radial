#pragma once

#include "model.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace radial {

struct Point {
    double x = 0;
    double y = 0;
    bool operator==(const Point&) const = default;
};

struct Bounds {
    double left = 0;
    double top = 0;
    double right = 0;
    double bottom = 0;
};

struct VisibleChild {
    int slot = 0;
    std::string nodeId;
    Point center;
    double axisRadians = 0;
    bool selected = false;
};

struct GestureView {
    bool active = false;
    std::string rootNodeId;
    std::vector<VisibleChild> children;
    std::optional<int> eligibleSlot;
    std::size_t depth = 0;
    Point origin;
    double axisRadians = 0;
    bool neutral = true;
};

class GestureEngine {
public:
    static constexpr double OpenThreshold = 42.0;
    // The renderer and hit testing share one annular-sector geometry. Six
    // 24-degree wedges leave six-degree gutters at their stable 30-degree slots.
    static constexpr double ChildRadius = 112.0;
    static constexpr double ChildInnerRadius = 88.0;
    static constexpr double ChildOuterRadius = 136.0;
    static constexpr double ChildHalfAngleDegrees = 12.0;
    static constexpr double ChildVisualOvershoot = 1.035;
    static constexpr double SelectionMinRadius = ChildInnerRadius;
    static constexpr double SelectionMaxRadius = ChildOuterRadius;
    static constexpr double BacktrackThreshold = 18.0;
    static constexpr double RootTapRadius = 28.0;
    static constexpr std::array<double, SlotCount> ChildAnglesDegrees{
        -75.0, -45.0, -15.0, 15.0, 45.0, 75.0};

    // A second press cannot replace an active gesture. The profile is copied here;
    // later editor changes cannot move targets or change the eventual command.
    bool begin(const Profile& profile, const std::string& nodeId, Point origin,
               double outwardAxisRadians, std::int64_t pointerId,
               std::optional<Point> contactPoint = std::nullopt,
               std::optional<Bounds> viewport = std::nullopt);
    static Bounds childBounds(Point origin, double axisRadians, double scale = 1.0);
    static Bounds fanBounds(Point origin, double axisRadians, double scale = 1.0);
    void move(std::int64_t pointerId, Point position);
    // Only an eligible leaf commits. A release terminates the owning gesture even
    // when neutral. Non-owning pointers are ignored, including their releases.
    std::optional<Action> release(std::int64_t pointerId, Point position);
    void cancel();
    void captureLost() { cancel(); }
    GestureView view() const;
    bool active() const { return active_; }

private:
    struct Frame {
        std::string groupId;
        Point nodeCenter;
        Point origin;
        double axis = 0;
        std::optional<int> eligible;
    };
    Profile snapshot_;
    std::string rootNodeId_;
    Point rootOrigin_;
    Point rootContact_;
    double rootAxis_ = 0;
    Point previous_;
    std::int64_t pointerId_ = 0;
    bool active_ = false;
    bool rootLeafEligible_ = false;
    std::optional<Bounds> viewport_;
    std::vector<Frame> frames_;

    void processSegment(Point from, Point to);
    void updateAlongSegment(Point from, Point to);
    void updateEligibility(Point point);
    void openGroup(const std::string& id, Point center, double axis);
};

} // namespace radial
