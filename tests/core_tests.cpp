#include "core/gesture.h"
#include "core/model.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using namespace radial;

namespace {
int checks = 0;
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool hasIssue(const Profile& p, const std::string& code) {
    for (const auto& issue : validate(p)) if (issue.code == code) return true;
    return false;
}
Point pointAt(Point origin, double degrees, double radius) {
    const double angle = degrees * std::numbers::pi / 180.0;
    return {origin.x + radius * std::cos(angle), origin.y + radius * std::sin(angle)};
}
bool near(Point a, Point b, double epsilon = 1e-7) {
    return std::hypot(a.x - b.x, a.y - b.y) < epsilon;
}
void openText(GestureEngine& engine, const Profile& p, std::int64_t pointer = 7,
              Point origin = {}, double axis = 0) {
    check(engine.begin(p, "text", origin, axis, pointer), "start valid Text gesture");
    engine.move(pointer, {origin.x + 42 * std::cos(axis), origin.y + 42 * std::sin(axis)});
    check(engine.view().depth == 1 && engine.view().neutral, "opening is neutral");
}

void validationCases() {
    auto p = defaultProfile();
    check(validate(p).empty(), "default profile validates");
    check(p.rootSlots.size() == 6, "six root slots");
    check(!p.nodes.at("text.more").children[2], "empty fixed slot preserved");
    for (auto key : {"A", "9", "F1", "F24", "Space", "ArrowLeft", "BracketRight"})
        check(isValidKey(key), "valid canonical key");
    for (auto key : {"", "a", "Ctrl+B", "F0", "F25", "F01", "SUPER", ";"})
        check(!isValidKey(key), "invalid key rejected");
    auto changed = p;
    changed.schemaVersion = 999;
    check(hasIssue(changed, "schema_version"), "future schema rejected explicitly");
    changed = p; changed.nodes.at("brush").action.modifiers = 16;
    check(hasIssue(changed, "invalid_modifiers"), "unknown modifier rejected");
    changed = p; changed.nodes.at("brush").action.key = "ctrl+b";
    check(hasIssue(changed, "invalid_key"), "untyped shortcut rejected");
    changed = p; changed.nodes.at("eraser").id = "brush";
    check(hasIssue(changed, "duplicate_id"), "duplicate node id rejected");
    changed = p; changed.nodes.at("eraser").action.id = changed.nodes.at("brush").action.id;
    check(hasIssue(changed, "duplicate_action_id"), "duplicate command id rejected");
    changed = p; changed.nodes.at("text").children.fill(std::nullopt);
    check(hasIssue(changed, "empty_group"), "empty group rejected");
    changed = p; changed.nodes.at("text").children[0] = "does-not-exist";
    check(hasIssue(changed, "missing_node"), "dangling node reference rejected");
    changed = p; changed.nodes.at("text").children[0] = "text.more";
    check(hasIssue(changed, "duplicate_slot"), "same node cannot occupy two sibling slots");
    changed = p; changed.nodes.at("settings").children[3] = "text.bold";
    check(hasIssue(changed, "multiple_parents"), "shared child across groups rejected");
    changed = p; changed.nodes.at("settings").children[3] = "brush";
    check(hasIssue(changed, "multiple_parents"), "root plus child reference rejected");
    changed = p; changed.nodes.at("text.more").children[0] = "text";
    check(hasIssue(changed, "cycle"), "cycle rejected");
    changed = p;
    for (int n = 4; n >= 1; --n) {
        Node group;
        group.id = "deep." + std::to_string(n); group.kind = NodeKind::Group;
        group.children[0] = n == 4 ? "deep.leaf" : "deep." + std::to_string(n + 1);
        changed.nodes.emplace(group.id, group);
    }
    auto leaf = changed.nodes.at("brush"); leaf.id = "deep.leaf"; leaf.action.id = "command.deep.leaf";
    changed.nodes.emplace(leaf.id, leaf);
    check(!hasIssue(changed, "maximum_depth"), "four group levels allowed");
    changed.nodes.at("text.more").children[0] = "deep.1";
    check(hasIssue(changed, "maximum_depth"), "five or more group levels rejected");
    GestureEngine engine;
    check(!engine.begin(changed, "brush", {}, 0, 1), "invalid profile cannot begin a gesture");
}

void ownershipAndLifecycle() {
    const auto p = defaultProfile();
    GestureEngine engine;
    check(engine.begin(p, "brush", {100, 100}, 0, 9), "begin brush");
    check(!engine.begin(p, "eraser", {}, 0, 10), "second press cannot replace owner");
    engine.move(10, {10000, 10000});
    check(!engine.release(10, {100, 100}), "foreign release cannot commit");
    check(engine.active(), "foreign release does not cancel owner");
    auto action = engine.release(9, {100, 100});
    check(action && action->id == "command.brush", "owner tap commits once");
    check(!engine.release(9, {100, 100}), "duplicate release cannot commit twice");
    check(!engine.active(), "released engine inactive");
    check(engine.begin(p, "brush", {}, 0, 9), "begin cancellation test");
    engine.captureLost();
    check(!engine.release(9, {}), "capture loss cancels without committing");
    check(engine.begin(p, "brush", {}, 0, 9), "begin drag-out test");
    check(!engine.release(9, {29, 0}), "leaf drag out cancels tap");
    check(engine.begin(p, "brush", {}, 0, 9), "begin invalid pointer sample test");
    engine.move(9, {std::numeric_limits<double>::quiet_NaN(), 0});
    check(!engine.active(), "non-finite owning input cancels safely");
    check(!engine.begin(p, "brush", {}, std::numeric_limits<double>::infinity(), 1),
          "non-finite axis rejected");
    check(engine.begin(p, "brush", {100, 100}, 0, 9, Point{135, 100}), "off-center wedge press begins");
    check(engine.release(9, {135, 100}).has_value(), "stationary wedge-edge tap commits");
}

void fixedSlotsAndSelection() {
    const auto p = defaultProfile();
    for (int slot = 0; slot < 5; ++slot) {
        GestureEngine engine;
        openText(engine, p);
        const auto view = engine.view();
        check(view.children.size() == 6, "six text children visible");
        const auto child = view.children[std::size_t(slot)];
        engine.move(7, child.center);
        check(engine.view().eligibleSlot == slot, "fixed direction selects corresponding slot");
        check(engine.active(), "moving cannot commit");
        const auto action = engine.release(7, child.center);
        check(action && action->id == p.nodes.at(child.nodeId).action.id, "leaf release commits selected action");
    }
    GestureEngine engine;
    openText(engine, p);
    const auto origin = engine.view().origin;
    engine.move(7, {origin.x + GestureEngine::ChildRadius, origin.y});
    check(engine.view().neutral, "center corridor is neutral");
    check(!engine.release(7, {origin.x + GestureEngine::ChildRadius, origin.y}), "straight outward cannot trigger a child");
    openText(engine, p);
    engine.move(7, pointAt(engine.view().origin, -45, GestureEngine::ChildInnerRadius-1));
    check(engine.view().neutral, "inner deadzone neutral");
    engine.move(7, pointAt(engine.view().origin, -45, GestureEngine::ChildOuterRadius+1));
    check(engine.view().neutral, "outside selection radius neutral");
    engine.cancel();

    openText(engine, p);
    auto fixed = engine.view();
    engine.move(7, pointAt(fixed.origin, -45, GestureEngine::ChildRadius));
    engine.move(7, pointAt(fixed.origin, -31, GestureEngine::ChildRadius));
    check(engine.view().eligibleSlot == 1, "small angular hysteresis retains slot at edge");
    engine.move(7, pointAt(fixed.origin, -28, GestureEngine::ChildRadius));
    check(engine.view().neutral, "leaving hysteresis reaches sector gap");
    engine.move(7, pointAt(fixed.origin, -15, GestureEngine::ChildRadius));
    engine.move(7, pointAt(fixed.origin, -2, GestureEngine::ChildRadius));
    check(engine.view().neutral, "center corridor overrides hysteresis");
    for (std::size_t i = 0; i < fixed.children.size(); ++i)
        check(near(fixed.children[i].center, engine.view().children[i].center), "origins never chase the stylus");
}

void nestedAndBacktrack() {
    const auto p = defaultProfile();
    GestureEngine engine;
    openText(engine, p);
    const auto parent = engine.view();
    engine.move(7, pointAt(parent.origin, 75, GestureEngine::ChildRadius+GestureEngine::OpenThreshold));
    auto deeper = engine.view();
    check(deeper.depth == 2 && deeper.neutral, "nested outward pull opens neutral branch");
    check(deeper.children.size() == 4, "empty slots not collapsed into new positions");
    check(deeper.children[2].slot == 4, "empty slots preserve original angular index");
    const auto destination = deeper.children[0];
    engine.move(7, destination.center);
    auto action = engine.release(7, destination.center);
    check(action && action->id == "command.text.underline", "nested leaf commits on release");
    openText(engine, p);
    engine.move(7, pointAt(engine.view().origin, 75, GestureEngine::ChildRadius+GestureEngine::OpenThreshold));
    engine.move(7, parent.children[5].center);
    check(engine.view().depth == 1, "backtrack pops one branch");
    engine.move(7, {0, 0});
    check(engine.view().depth == 0 && engine.view().neutral, "return to root cancels branch selection");
    check(!engine.release(7, {0, 0}), "release after backtrack commits nothing");
    openText(engine, p);
    engine.move(7, parent.children[5].center);
    check(!engine.release(7, parent.children[5].center), "group release cannot execute command");
}

void frozenSnapshot() {
    auto p = defaultProfile();
    GestureEngine engine;
    openText(engine, p);
    const auto destination = engine.view().children[0];
    p.nodes.at("text.bold").action.key = "Q";
    p.nodes.at("text").children[0] = "brush";
    engine.move(7, destination.center);
    const auto action = engine.release(7, destination.center);
    check(action && action->key == "B" && action->id == "command.text.bold",
          "editor changes cannot change a command during active stroke");
}

void visibleWedgeSelection() {
    const auto profile = defaultProfile();
    // A highlight may steady itself at an edge, but the painted gutter never
    // sends a command. This covers a common accidental release during steering.
    GestureEngine engine;
    openText(engine, profile);
    const auto origin = engine.view().origin;
    engine.move(7, pointAt(origin, -45, 112));
    engine.move(7, pointAt(origin, -31, 112));
    check(engine.view().eligibleSlot == 1, "edge hysteresis steadies highlight");
    check(!engine.release(7, pointAt(origin, -31, 112)), "release in visible gutter sends nothing");
    for (const double radius : {60.0, 87.0, 137.0, 155.0}) {
        openText(engine, profile);
        check(!engine.release(7, pointAt(engine.view().origin,-45,radius)),
              "unpainted annulus cannot send an action");
    }
    for (const double radius : {89.0, 112.0, 135.0}) {
        openText(engine, profile);
        const auto action = engine.release(7, pointAt(engine.view().origin,-45,radius));
        check(action && action->id == "command.text.italic", "entire visible wedge is usable");
    }
    const Bounds viewport{4,4,676,496};
    check(engine.begin(profile,"text",{35,122.5},0,7,std::nullopt,viewport), "begin end-row bounded fan");
    engine.move(7,{77,122.5});
    const auto opened = engine.view();
    check(opened.depth == 1 && opened.neutral, "bounded fan opens neutral without accidental backtrack");
    check(opened.origin.y > 122.5 && opened.origin.y < 155, "end-row origin has only a small inward inset");
    for (const auto& child : opened.children) {
        const auto bounds = GestureEngine::childBounds(opened.origin,child.axisRadians,GestureEngine::ChildVisualOvershoot);
        check(bounds.left >= viewport.left-1e-7 && bounds.top >= viewport.top-1e-7 &&
              bounds.right <= viewport.right+1e-7 && bounds.bottom <= viewport.bottom+1e-7,
              "entire animated fan fits the viewport");
    }
    engine.move(7,opened.children[0].center);
    check(engine.view().origin == opened.origin, "bounded origin remains fixed while steering");
    check(engine.release(7,opened.children[0].center).has_value(), "inset end-row fan still commits its visible button");
}

void samplingAndRotation() {
    const auto p = defaultProfile();
    // Same polyline delivered in one sample per edge and dense OS samples must
    // produce the same branch, slot and commit, including hysteresis boundaries.
    for (double axis : {0.0, std::numbers::pi, -std::numbers::pi / 2.0}) {
        for (int trial = 0; trial < 20; ++trial) {
            const Point root{250, 300};
            auto transform = [&](Point point) {
                return Point{root.x + point.x * std::cos(axis) - point.y * std::sin(axis),
                             root.y + point.x * std::sin(axis) + point.y * std::cos(axis)};
            };
            const double angle = -75.0 + (trial % 5) * 30.0;
            std::vector<Point> path{{0, 0}, {42, 0}, pointAt({42, 0}, angle, GestureEngine::ChildRadius),
                pointAt({42, 0}, angle + (trial % 2 ? 14 : 0), GestureEngine::ChildRadius)};
            GestureEngine coarse, dense;
            check(coarse.begin(p, "text", root, axis, 1), "coarse begin");
            check(dense.begin(p, "text", root, axis, 1), "dense begin");
            for (std::size_t edge = 1; edge < path.size(); ++edge) {
                coarse.move(1, transform(path[edge]));
                for (int step = 1; step <= 31; ++step) {
                    const double t = step / 31.0;
                    dense.move(1, transform({path[edge - 1].x + (path[edge].x - path[edge - 1].x) * t,
                                            path[edge - 1].y + (path[edge].y - path[edge - 1].y) * t}));
                }
                check(coarse.view().depth == dense.view().depth, "coalescing preserves branch depth");
                check(coarse.view().eligibleSlot == dense.view().eligibleSlot, "coalescing preserves angular hysteresis");
            }
            const auto endpoint = transform(path.back());
            check(coarse.release(1, endpoint) == dense.release(1, endpoint), "coalescing preserves action");
        }
    }
    GestureEngine engine;
    check(engine.begin(p, "text", {}, 0, 1), "fast stroke begin");
    engine.move(1, {500, 0});
    check(engine.view().depth == 1 && engine.view().neutral, "long sample crosses opening gate without selecting center");
    engine.move(1, {0, 0});
    check(engine.view().depth == 0, "long return crosses backtracking plane");
}
}

int main() {
    try {
        validationCases(); ownershipAndLifecycle(); fixedSlotsAndSelection();
        nestedAndBacktrack(); frozenSnapshot(); visibleWedgeSelection(); samplingAndRotation();
        std::cout << "PASS: " << checks << " core invariant checks\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
