#include "gesture.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace radial {
namespace {
constexpr double Pi = std::numbers::pi;
constexpr double Epsilon = 1e-8;
constexpr double HalfSector = GestureEngine::ChildHalfAngleDegrees * Pi / 180.0;
constexpr double Hysteresis = 3.0 * Pi / 180.0;
constexpr double CenterCorridor = 6.0 * Pi / 180.0;
constexpr double GateHalfWidth = 20.0;
constexpr double MaximumFanOriginAdjustment = 32.0;
Point add(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
Point sub(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
Point scale(Point a, double v) { return {a.x * v, a.y * v}; }
Point unit(double angle) { return {std::cos(angle), std::sin(angle)}; }
double dot(Point a, Point b) { return a.x * b.x + a.y * b.y; }
double length(Point a) { return std::hypot(a.x, a.y); }
bool finite(Point point) { return std::isfinite(point.x) && std::isfinite(point.y); }
double wrap(double angle) { return std::remainder(angle, 2.0 * Pi); }
Point interpolate(Point a, Point b, double t) { return add(a, scale(sub(b, a), t)); }

// Exact segment entry into an outward gate. Clipping, rather than per-frame
// testing, catches the same gate for both a single coalesced pen sample and many
// samples along that straight path. The gate is fixed in gesture coordinates.
std::optional<double> gateEntry(Point from, Point to, Point center, double axis) {
    const Point u = unit(axis), v = unit(axis + Pi / 2.0);
    const Point a = sub(from, center), b = sub(to, center);
    const double x = dot(a, u), dx = dot(sub(b, a), u);
    const double y = dot(a, v), dy = dot(sub(b, a), v);
    double first = 0.0, last = 1.0;
    auto lower = [&](double value, double delta, double boundary) {
        if (std::abs(delta) < Epsilon) return value >= boundary - Epsilon;
        const double t = (boundary - value) / delta;
        if (delta > 0) first = std::max(first, t);
        else last = std::min(last, t);
        return first <= last + Epsilon;
    };
    if (!lower(x, dx, GestureEngine::OpenThreshold) ||
        !lower(y, dy, -GateHalfWidth) || !lower(-y, -dy, -GateHalfWidth) ||
        first > 1.0 + Epsilon || last < -Epsilon) return std::nullopt;
    return std::clamp(first, 0.0, 1.0);
}

std::optional<double> backtrackEntry(Point from, Point to, Point origin, double axis) {
    const Point u = unit(axis);
    const double a = dot(sub(from, origin), u);
    const double b = dot(sub(to, origin), u);
    const double boundary = -GestureEngine::BacktrackThreshold;
    if (a < boundary - Epsilon) return 0.0;
    if (b >= boundary - Epsilon || b >= a) return std::nullopt;
    return std::clamp((boundary - a) / (b - a), 0.0, 1.0);
}
}

bool GestureEngine::begin(const Profile& profile, const std::string& nodeId, Point origin,
                          double outwardAxisRadians, std::int64_t pointerId,
                          std::optional<Point> contactPoint, std::optional<Bounds> viewport) {
    if (active_ || !finite(origin) || !std::isfinite(outwardAxisRadians) ||
        (contactPoint && !finite(*contactPoint)) ||
        (viewport && (!finite({viewport->left, viewport->top}) ||
                      !finite({viewport->right, viewport->bottom}) ||
                      viewport->right <= viewport->left || viewport->bottom <= viewport->top)) ||
        !profile.nodes.contains(nodeId) || !validate(profile).empty()) return false;
    snapshot_ = profile;
    rootNodeId_ = nodeId;
    rootOrigin_ = origin;
    rootContact_ = previous_ = contactPoint.value_or(origin);
    rootAxis_ = wrap(outwardAxisRadians);
    pointerId_ = pointerId;
    viewport_ = viewport;
    active_ = true;
    frames_.clear();
    rootLeafEligible_ = snapshot_.nodes.at(nodeId).kind == NodeKind::Action;
    return true;
}

Bounds GestureEngine::childBounds(Point origin, double axisRadians, double scale) {
    Bounds result{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                  -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    auto include = [&](double angle) {
        for (const double radius : {ChildInnerRadius * scale, ChildOuterRadius * scale}) {
            const auto p = add(origin, radial::scale(unit(angle), radius));
            result.left = std::min(result.left, p.x); result.right = std::max(result.right, p.x);
            result.top = std::min(result.top, p.y); result.bottom = std::max(result.bottom, p.y);
        }
    };
    include(axisRadians-HalfSector); include(axisRadians+HalfSector);
    for (const double angle : {0.0, Pi/2, Pi, -Pi/2})
        if (std::abs(wrap(angle-axisRadians)) <= HalfSector) include(angle);
    return result;
}

Bounds GestureEngine::fanBounds(Point origin, double axisRadians, double scale) {
    auto result = childBounds(origin, axisRadians+ChildAnglesDegrees[0]*Pi/180.0, scale);
    for (std::size_t slot=1; slot<SlotCount; ++slot) {
        const auto bounds = childBounds(origin, axisRadians+ChildAnglesDegrees[slot]*Pi/180.0, scale);
        result.left = std::min(result.left, bounds.left); result.right = std::max(result.right, bounds.right);
        result.top = std::min(result.top, bounds.top); result.bottom = std::max(result.bottom, bounds.bottom);
    }
    return result;
}

void GestureEngine::openGroup(const std::string& id, Point center, double axis) {
    auto origin = add(center, scale(unit(axis), OpenThreshold));
    if (viewport_) {
        const auto bounds = fanBounds(origin, axis, ChildVisualOvershoot);
        if (bounds.right-bounds.left <= viewport_->right-viewport_->left &&
            bounds.bottom-bounds.top <= viewport_->bottom-viewport_->top) {
            const Point correction{
                bounds.left < viewport_->left ? viewport_->left-bounds.left :
                    (bounds.right > viewport_->right ? viewport_->right-bounds.right : 0.0),
                bounds.top < viewport_->top ? viewport_->top-bounds.top :
                    (bounds.bottom > viewport_->bottom ? viewport_->bottom-bounds.bottom : 0.0)};
            // A small, deterministic inset fits the end rows of a dock. Large
            // relocations would invalidate learned gestures, so the session
            // cancels an oversized branch instead of moving it across the view.
            if (length(correction) <= MaximumFanOriginAdjustment) origin = add(origin, correction);
        }
    }
    frames_.push_back({id, center, origin, axis, {}});
    rootLeafEligible_ = false;
}

void GestureEngine::updateEligibility(Point point) {
    if (frames_.empty()) {
        rootLeafEligible_ = snapshot_.nodes.at(rootNodeId_).kind == NodeKind::Action &&
            length(sub(point, rootContact_)) <= RootTapRadius;
        return;
    }
    auto& frame = frames_.back();
    const auto& node = snapshot_.nodes.at(frame.groupId);
    const auto delta = sub(point, frame.origin);
    const auto radius = length(delta);
    const double angle = wrap(std::atan2(delta.y, delta.x) - frame.axis);
    if (radius < SelectionMinRadius || radius > SelectionMaxRadius ||
        std::abs(angle) < CenterCorridor) {
        frame.eligible.reset();
        return;
    }
    if (frame.eligible && node.children[std::size_t(*frame.eligible)]) {
        const auto center = ChildAnglesDegrees[std::size_t(*frame.eligible)] * Pi / 180.0;
        if (std::abs(wrap(angle - center)) <= HalfSector + Hysteresis) return;
    }
    frame.eligible.reset();
    for (std::size_t slot = 0; slot < SlotCount; ++slot) {
        if (!node.children[slot]) continue; // Empty slots retain their angular space.
        const double center = ChildAnglesDegrees[slot] * Pi / 180.0;
        if (std::abs(wrap(angle - center)) <= HalfSector) {
            frame.eligible = int(slot);
            return;
        }
    }
}

void GestureEngine::updateAlongSegment(Point from, Point to) {
    if (frames_.empty()) { updateEligibility(to); return; }
    const auto& frame = frames_.back();
    const Point a = sub(from, frame.origin), d = sub(to, from);
    std::vector<double> crossings{0.0, 1.0};
    auto addCrossing = [&](double t) {
        if (std::isfinite(t) && t > Epsilon && t < 1.0 - Epsilon) crossings.push_back(t);
    };
    auto rayBoundary = [&](double angle) {
        const auto normal = unit(angle + Pi / 2.0);
        const auto denominator = dot(d, normal);
        if (std::abs(denominator) > Epsilon) addCrossing(-dot(a, normal) / denominator);
    };
    for (const auto degrees : ChildAnglesDegrees) {
        const double angle = frame.axis + degrees * Pi / 180.0;
        rayBoundary(angle - HalfSector); rayBoundary(angle + HalfSector);
        rayBoundary(angle - HalfSector - Hysteresis); rayBoundary(angle + HalfSector + Hysteresis);
    }
    rayBoundary(frame.axis - CenterCorridor); rayBoundary(frame.axis + CenterCorridor);
    const double aa = dot(d, d), bb = 2.0 * dot(a, d);
    if (aa > Epsilon) for (const auto radius : {SelectionMinRadius, SelectionMaxRadius}) {
        const double cc = dot(a, a) - radius * radius;
        const double discriminant = bb * bb - 4.0 * aa * cc;
        if (discriminant >= 0.0) {
            const double root = std::sqrt(discriminant);
            addCrossing((-bb - root) / (2.0 * aa));
            addCrossing((-bb + root) / (2.0 * aa));
        }
    }
    std::sort(crossings.begin(), crossings.end());
    crossings.erase(std::unique(crossings.begin(), crossings.end(), [](double x, double y) {
        return std::abs(x - y) < Epsilon;
    }), crossings.end());
    // Hit state can only change at these boundaries. Sampling each open interval
    // makes angular hysteresis independent of OS event coalescing along a line.
    for (std::size_t i = 1; i < crossings.size(); ++i)
        updateEligibility(interpolate(from, to, (crossings[i - 1] + crossings[i]) / 2.0));
    updateEligibility(to);
}

void GestureEngine::processSegment(Point from, Point to) {
    // Each transition consumes an exact segment intersection. Four group levels
    // bound ordinary traversal; this guard also protects against numeric stalls.
    for (int transitionCount = 0; transitionCount < 32; ++transitionCount) {
        if (frames_.empty()) {
            const auto& root = snapshot_.nodes.at(rootNodeId_);
            if (root.kind != NodeKind::Group) { updateEligibility(to); return; }
            const auto entry = gateEntry(from, to, rootOrigin_, rootAxis_);
            if (!entry) { updateEligibility(to); return; }
            from = interpolate(from, to, *entry);
            openGroup(rootNodeId_, rootOrigin_, rootAxis_);
            if (length(sub(to, from)) < Epsilon) { updateEligibility(to); return; }
            continue;
        }

        const auto frame = frames_.back();
        auto earliest = backtrackEntry(from, to, frame.origin, frame.axis);
        bool backtrack = earliest.has_value();
        std::optional<std::size_t> childToOpen;
        Point childCenter;
        double childAxis = 0;
        const auto& group = snapshot_.nodes.at(frame.groupId);
        if (frames_.size() < MaximumGroupDepth) {
            for (std::size_t slot = 0; slot < SlotCount; ++slot) {
                if (!group.children[slot]) continue;
                const auto& child = snapshot_.nodes.at(*group.children[slot]);
                if (child.kind != NodeKind::Group) continue;
                const double axis = frame.axis + ChildAnglesDegrees[slot] * Pi / 180.0;
                const auto center = add(frame.origin, scale(unit(axis), ChildRadius));
                const auto entry = gateEntry(from, to, center, axis);
                if (!entry || (earliest && *entry >= *earliest - Epsilon)) continue;
                // Entry must lie within the parent fan. A far-away path through
                // an infinite gate must never open an unrelated child group.
                const auto delta = sub(interpolate(from, to, *entry), frame.origin);
                const auto radius = length(delta);
                const auto relativeAngle = wrap(std::atan2(delta.y, delta.x) - axis);
                const double gateRadius = std::hypot(ChildRadius+OpenThreshold, GateHalfWidth);
                if (radius > gateRadius + Epsilon || radius < SelectionMinRadius ||
                    std::abs(relativeAngle) > HalfSector + Epsilon) continue;
                earliest = entry; backtrack = false; childToOpen = slot;
                childCenter = center; childAxis = axis;
            }
        }
        if (!earliest) { updateAlongSegment(from, to); return; }
        const auto crossing = interpolate(from, to, *earliest);
        updateAlongSegment(from, crossing);
        from = crossing;
        if (backtrack) frames_.pop_back();
        else if (childToOpen) openGroup(*group.children[*childToOpen], childCenter, childAxis);
        // Eligibility at the new origin is neutral. Any remaining stroke is
        // evaluated in the new fixed coordinate frame before the next sample.
        updateEligibility(from);
        if (length(sub(to, from)) < Epsilon) { updateEligibility(to); return; }
    }
    // A malformed/extreme path cannot leave an accidentally armed selection.
    if (!frames_.empty()) frames_.back().eligible.reset();
    rootLeafEligible_ = false;
}

void GestureEngine::move(std::int64_t pointerId, Point position) {
    if (!active_ || pointerId != pointerId_) return;
    if (!finite(position)) { cancel(); return; }
    processSegment(previous_, position);
    previous_ = position;
}

std::optional<Action> GestureEngine::release(std::int64_t pointerId, Point position) {
    if (!active_ || pointerId != pointerId_) return std::nullopt;
    move(pointerId, position);
    if (!active_) return std::nullopt;
    const Node* selected = nullptr;
    if (frames_.empty() && rootLeafEligible_) selected = &snapshot_.nodes.at(rootNodeId_);
    else if (!frames_.empty() && frames_.back().eligible) {
        const auto& frame = frames_.back();
        const auto& group = snapshot_.nodes.at(frame.groupId);
        const auto& id = group.children[std::size_t(*frame.eligible)];
        // Hysteresis steadies highlighting during motion, but a release in a
        // visible gutter must not execute an invisible extension of the wedge.
        const auto delta = sub(position, frame.origin);
        const double axis = frame.axis + ChildAnglesDegrees[std::size_t(*frame.eligible)]*Pi/180.0;
        const double angle = std::abs(wrap(std::atan2(delta.y, delta.x)-axis));
        if (id && length(delta) >= ChildInnerRadius && length(delta) <= ChildOuterRadius &&
            angle <= HalfSector) selected = &snapshot_.nodes.at(*id);
    }
    std::optional<Action> result;
    if (selected && selected->kind == NodeKind::Action && selected->action.kind != ActionKind::None)
        result = selected->action;
    cancel();
    return result;
}

void GestureEngine::cancel() {
    active_ = false;
    rootLeafEligible_ = false;
    frames_.clear();
    snapshot_ = {};
    rootNodeId_.clear();
}

GestureView GestureEngine::view() const {
    GestureView result;
    result.active = active_;
    if (!active_) return result;
    result.rootNodeId = rootNodeId_;
    result.depth = frames_.size();
    result.origin = rootOrigin_;
    result.axisRadians = rootAxis_;
    result.neutral = !rootLeafEligible_;
    if (frames_.empty()) return result;
    const auto& frame = frames_.back();
    result.origin = frame.origin;
    result.axisRadians = frame.axis;
    result.eligibleSlot = frame.eligible;
    result.neutral = !frame.eligible.has_value();
    const auto& group = snapshot_.nodes.at(frame.groupId);
    for (std::size_t slot = 0; slot < SlotCount; ++slot) {
        if (!group.children[slot]) continue;
        const double axis = frame.axis + ChildAnglesDegrees[slot] * Pi / 180.0;
        result.children.push_back({int(slot), *group.children[slot],
            add(frame.origin, scale(unit(axis), ChildRadius)), axis,
            frame.eligible && std::size_t(*frame.eligible) == slot});
    }
    return result;
}

} // namespace radial
