#include "MenuSession.h"
#include "platform/WindowPlatform.h"
#include <QtMath>
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace tabletpro {
namespace {
const QStringList colors{"#749ac6","#9b8bbb","#72b3ac","#c59e7f","#899ac5","#b28ea9"};
constexpr double Pi=std::numbers::pi;
constexpr qreal FanViewportMargin = 4.0;
QStringList modifiers(const radial::Action& a) {
    QStringList result;
    if (a.modifiers & radial::Ctrl) result << "Ctrl";
    if (a.modifiers & radial::Shift) result << "Shift";
    if (a.modifiers & radial::Alt) result << "Alt";
    if (a.modifiers & radial::Meta) result << "Meta";
    return result;
}
}
MenuSession::MenuSession(bool simulation, WindowPlatform* platform, QObject* parent)
    : QObject(parent), platform_(platform), simulation_(simulation) {
    modifierClock_.start();
    parentHoldTimer_.setSingleShot(true);parentHoldTimer_.setInterval(ParentHoldMs);parentHoldTimer_.setTimerType(Qt::PreciseTimer);
    connect(&parentHoldTimer_,&QTimer::timeout,this,[this]{
        if(!parentPress_)return;
        if(!simulation_ && platform_ && !platform_->targetUnchanged(gestureTarget_)){cancel();return;}
        const auto press=*parentPress_;
        const auto pen=(!simulation_&&platform_)?platform_->penPositionInOverlay():std::optional<QPointF>{QPointF(width_/2,height_/2)};
        openParent(press.id,pen.value_or(press.point),true,press.pointer);
        if(!pen){status_="Menu opened here · move your pen near the screen before holding to open at its tip";emit stateChanged();}
    });
    if(!simulation_ && platform_) connect(platform_,&WindowPlatform::modifiersReleased,this,[this]{
        heldModifiers_.clear();latchedModifiers_.clear();armedModifiers_.clear();modifierPointer_.reset();lastTapKey_.clear();
        emit geometryChanged();emit stateChanged();
    });
    if(!simulation_ && platform_)connect(platform_,&WindowPlatform::strokeModifiersReleased,this,&MenuSession::finishNextStroke);
    connect(&animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) { progress_ = value.toReal(); emit geometryChanged(); });
    connect(&animation_, &QVariantAnimation::finished, this, [this] { status_ = "Hold a parent for 300 ms · slide out to open at your finger"; emit stateChanged(); emit cue("snap"); });
    if (!simulation_ && platform_) connect(platform_, &WindowPlatform::targetLost, this, [this] { if (active() || !heldModifiers_.isEmpty()) { cancel(); status_ = "Canceled: the target application changed."; emit stateChanged(); } });
    if (!simulation_ && platform_) connect(platform_, &WindowPlatform::actionDispatchFinished, this, [this](bool success, const QString& error) {
        if (!pendingDispatch_) return;
        status_ = success ? (platform_->nativeAvailable() ? "Sent: " : "Preview: ") + pendingDescription_ : error;
        emit actionReported(status_);
        if (success) emit cue("commit");
        pendingDispatch_ = false;
        finish();
    });
}
void MenuSession::setProfile(const radial::Profile& profile) {
    if (active()) { pending_ = profile; return; }
    releaseModifiers(); profile_ = profile; emit geometryChanged(); emit stateChanged();
}
void MenuSession::setViewport(qreal width, qreal height) {
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 || (qFuzzyCompare(width_, width) && qFuzzyCompare(height_, height))) return;
    if (active()) cancel();
    width_ = width; height_ = height;
    if (radialAnchor_) radialAnchor_ = boundedRadialCenter(*radialAnchor_, !draggedAnchor_);
    emit geometryChanged();
}
QPointF MenuSession::radialCenter() const {
    return radialAnchor_.value_or(QPointF(width_/2, height_/2));
}
QPointF MenuSession::boundedRadialCenter(QPointF center, bool leaveFanRoom) const {
    // Leave room for a root, the opening gate, a first-level child, and its hit area.
    const qreal fanMargin = leaveFanRoom ? std::ceil(70 + radial::GestureEngine::OpenThreshold +
        radial::GestureEngine::ChildOuterRadius*radial::GestureEngine::ChildVisualOvershoot + FanViewportMargin) : 104;
    const qreal marginX = std::min(fanMargin, width_/2);
    const qreal marginY = std::min(fanMargin, height_/2);
    return {std::clamp(center.x(), marginX, width_-marginX),
            std::clamp(center.y(), marginY, height_-marginY)};
}
void MenuSession::setRadialCenter(qreal x, qreal y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    const auto center = boundedRadialCenter({x,y});
    const bool changed = center != radialCenter();
    if (changed && active()) cancel();
    radialAnchor_ = center;
    draggedAnchor_ = false;
    if (changed) emit geometryChanged();
}
void MenuSession::moveRadialCenter(qreal x, qreal y) {
    if(active() || docked_ || transitioning() || !std::isfinite(x) || !std::isfinite(y))return;
    const auto center=boundedRadialCenter({x,y},false);
    const bool changed=center!=radialCenter();radialAnchor_=center;draggedAnchor_=true;
    if(changed)emit geometryChanged();
}
void MenuSession::setDurationMs(int value) { durationMs_ = std::clamp(value, 250, 2400); emit preferencesChanged(); }
void MenuSession::setReducedMotion(bool reduced) {
    reduced_ = reduced;
    if (reduced_ && animation_.state() == QAbstractAnimation::Running) { animation_.stop(); progress_ = docked_ ? 1 : 0; emit geometryChanged(); emit stateChanged(); }
}
QVariantList MenuSession::buttons() const {
    QVariantList result;
    const auto center = radialCenter();
    for (int i = 0; i < 6; ++i) {
        if (!profile_.rootSlots[i]) continue;
        const auto found = profile_.nodes.find(*profile_.rootSlots[i]);
        if (found == profile_.nodes.end()) continue;
        const auto& node = found->second;
        double angle = -Pi/2 + i * Pi / 3;
        result.append(QVariantMap{{"id", QString::fromStdString(node.id)}, {"icon", QString::fromStdString(node.action.kind==radial::ActionKind::Modifier?node.action.key:node.icon)}, {"modifierState",modifierState(node)}, {"label", QString::fromStdString(node.label)}, {"color", colors[i]}, {"slot", i},
            {"cx", center.x() + std::cos(angle)*70}, {"cy", center.y() + std::sin(angle)*70}, {"angle", angle}, {"dockX", 35.0}, {"dockY", height_/2 + (i-2.5)*51}});
    }
    return result;
}
QVariantList MenuSession::childButtons() const {
    QVariantList result;
    const auto view=engine_.view();
    for (const auto& child : view.children) {
        auto found = profile_.nodes.find(child.nodeId);
        if (found == profile_.nodes.end()) continue;
        result.append(QVariantMap{{"id", QString::fromStdString(child.nodeId)}, {"icon", QString::fromStdString(found->second.action.kind==radial::ActionKind::Modifier?found->second.action.key:found->second.icon)}, {"modifierState",modifierState(found->second)}, {"label", QString::fromStdString(found->second.label)}, {"color", colors[child.slot]}, {"cx", child.center.x}, {"cy", child.center.y}, {"fromX",view.origin.x},{"fromY",view.origin.y},{"selected", child.selected}, {"slot", child.slot},
            {"axis",child.axisRadians}, {"innerRadius",radial::GestureEngine::ChildInnerRadius},
            {"outerRadius",radial::GestureEngine::ChildOuterRadius}, {"halfAngle",radial::GestureEngine::ChildHalfAngleDegrees*Pi/180.0},
            {"group",found->second.kind == radial::NodeKind::Group}});
    }
    return result;
}
QString MenuSession::selectedId() const {
    if(modifierPointer_)return modifierNode_;
    for (const auto& child : engine_.view().children) if (child.selected) return QString::fromStdString(child.nodeId);
    return active() && engine_.view().children.empty() && !engine_.view().neutral ? QString::fromStdString(engine_.view().rootNodeId) : QString{};
}
void MenuSession::toggleDock() {
    if (active() || animation_.state() == QAbstractAnimation::Running) return;
    releaseModifiers();
    docked_ = !docked_;
    if (reduced_) { progress_ = docked_ ? 1 : 0; status_ = docked_ ? "Docked" : "Floating"; emit geometryChanged(); emit stateChanged(); return; }
    animation_.setStartValue(progress_); animation_.setEndValue(docked_ ? 1.0 : 0.0); animation_.setDuration(durationMs_);
    animation_.setEasingCurve(QEasingCurve::Linear); animation_.start();
    status_ = docked_ ? "Quiet Gravity · attracting" : "Quiet Gravity · releasing";
    emit stateChanged(); emit cue("crackle");
}
void MenuSession::replayDock() { if (active()) return; animation_.stop(); progress_ = 0; docked_ = false; toggleDock(); }
void MenuSession::pointerPressed(int pointerId, qreal x, qreal y) {
    if(parentMenuOpen()){parentPointerPressed(pointerId,x,y);return;}
    if (active() || animation_.state() != QAbstractAnimation::Stopped) return;
    const auto center = radialCenter();
    for (const auto& value : buttons()) {
        const auto b = value.toMap();
        const double cx = b[docked_?"dockX":"cx"].toDouble(), cy = b[docked_?"dockY":"cy"].toDouble();
        double angle = b["angle"].toDouble(); bool hit = false;
        if (docked_) hit = std::abs(x-cx) <= 22 && std::abs(y-cy) <= 22;
        else {
            const double r = std::hypot(x-center.x(), y-center.y());
            const double delta = std::remainder(std::atan2(y-center.y(),x-center.x())-angle, 2*Pi);
            hit = r >= 44 && r <= 96 && std::abs(delta) < Pi/6-.035;
        }
        if (!hit) continue;
        if (!simulation_ && platform_) {
            gestureTarget_ = platform_->captureForegroundTarget();
            if (platform_->nativeAvailable() && !gestureTarget_) { status_ = platform_->lastError(); emit stateChanged(); return; }
        }
        const auto& node=profile_.nodes.at(b["id"].toString().toStdString());
        if(node.action.kind==radial::ActionKind::Modifier){beginModifier(node,pointerId,{x,y});return;}
        lastTapKey_.clear();
        if (engine_.begin(profile_, b["id"].toString().toStdString(), {cx,cy}, docked_ ? 0 : angle, pointerId, radial::Point{x,y},
                          radial::Bounds{FanViewportMargin,FanViewportMargin,width_-FanViewportMargin,height_-FanViewportMargin})) {
            previousSelection_.clear(); previousDepth_ = 0; status_ = "Pull outward to open · release a highlighted action"; updateView();
            if(node.kind==radial::NodeKind::Group){beginParentPress(b["id"].toString(),pointerId,{x,y});status_="Hold 300 ms for the pen tip · slide out to open here";emit stateChanged();}
        }
        return;
    }
}
bool MenuSession::viewFits() const {
    const auto view = engine_.view();
    for (const auto& item : view.children) {
        const auto bounds = radial::GestureEngine::childBounds(view.origin, item.axisRadians, radial::GestureEngine::ChildVisualOvershoot);
        if (bounds.left < FanViewportMargin-1e-7 || bounds.right > width_-FanViewportMargin+1e-7 ||
            bounds.top < FanViewportMargin-1e-7 || bounds.bottom > height_-FanViewportMargin+1e-7) return false;
    }
    return true;
}
void MenuSession::updateView() {
    if (!viewFits()) { cancel(); status_ = "Not enough room for this branch. Enlarge the preview or use the dock."; emit stateChanged(); return; }
    const auto view = engine_.view(); const QString selection = selectedId();
    if (view.depth > previousDepth_) emit cue("open");
    else if (view.depth < previousDepth_) emit cue("back");
    else if (selection != previousSelection_ && !selection.isEmpty()) emit cue("tick");
    previousDepth_ = view.depth; previousSelection_ = selection;
    if (!view.children.empty()) status_ = selection.isEmpty() ? "Neutral · steer into a child, or return to cancel" : QString::fromStdString(profile_.nodes.at(selection.toStdString()).label) + " · release to choose";
    emit stateChanged();
}
void MenuSession::pointerMoved(int pointerId, qreal x, qreal y) {
    if(!std::isfinite(x)||!std::isfinite(y)){cancel();return;}
    if(modifierPointer_){moveModifierPointer(pointerId,x,y);return;}
    if(parentMenuOpen()){parentPointerMoved(pointerId,x,y);return;}
    if(parentPress_){
        if(parentPress_->pointer!=pointerId)return;
        if(!simulation_ && platform_ && !platform_->targetUnchanged(gestureTarget_)){cancel();return;}
        const auto distance=QLineF(parentPress_->point,{x,y}).length();
        if(distance>12)parentHoldTimer_.stop();
        if(distance>=radial::GestureEngine::OpenThreshold){const auto press=*parentPress_;openParent(press.id,{x,y},false,pointerId);}
        return;
    }
    if (!engine_.active()) return;
    if (!simulation_ && platform_ && !platform_->targetUnchanged(gestureTarget_)) { cancel(); status_ = "Canceled: target changed."; emit stateChanged(); return; }
    engine_.move(pointerId,{x,y}); updateView();
    const auto selected=selectedId();
    const auto node=profile_.nodes.find(selected.toStdString());
    if(node!=profile_.nodes.end() && node->second.action.kind==radial::ActionKind::Modifier)
        beginModifier(node->second,pointerId,{x,y},true);
}
void MenuSession::pointerReleased(int pointerId, qreal x, qreal y) {
    if(ignoredParentPointer_ && *ignoredParentPointer_==pointerId){ignoredParentPointer_.reset();return;}
    if(parentMenuOpen()){parentPointerReleased(pointerId,x,y);return;}
    if(parentPress_){
        if(parentPress_->pointer!=pointerId)return;
        pointerMoved(pointerId,x,y);
        if(parentMenuOpen()){parentPointerReleased(pointerId,x,y);return;}
        parentHoldTimer_.stop();parentPress_.reset();engine_.cancel();status_="Hold a parent for 300 ms, or slide out to open its menu";finish();return;
    }
    if(modifierPointer_){
        if(*modifierPointer_!=pointerId)return;
        pointerMoved(pointerId,x,y);
        if(!modifierPointer_)return;
        const auto now=modifierClock_.elapsed();
        const bool tap=!modifierDragged_ && !releasingLatch_ && now-pressedAt_<=(modifierFloating_?450:250);
        if(modifierFloating_ && tap){
            lastTapKey_.clear();
            if(simulation_ || (platform_ && platform_->armNextStroke(modifierKey_))){
                if(!armedModifiers_.contains(modifierKey_))armedModifiers_.append(modifierKey_);
                status_=modifierKey_+" · next pen stroke · tap again to cancel";
            }else{
                const auto error=platform_->lastError();holdModifier(modifierKey_,false);status_=error;emit actionReported(error);
            }
            modifierPointer_.reset();engine_.cancel();emit geometryChanged();finish();return;
        }
        const bool twice=tap && lastTapKey_==modifierKey_ && pressedAt_-lastTapAt_<=400;
        if(twice){latchedModifiers_.append(modifierKey_);lastTapKey_.clear();status_=modifierKey_+" stays on · tap to release";}
        else {
            const bool released=holdModifier(modifierKey_,false);
            lastTapKey_=tap&&released?modifierKey_:QString{};lastTapAt_=now;
            if(released)status_=modifierKey_+" released";
            else if(!latchedModifiers_.contains(modifierKey_))latchedModifiers_.append(modifierKey_);
        }
        modifierPointer_.reset();engine_.cancel();emit geometryChanged();finish();return;
    }
    if (!engine_.active()) return;
    if (!simulation_ && platform_ && !platform_->targetUnchanged(gestureTarget_)) { cancel(); return; }
    engine_.move(pointerId,{x,y});
    if (!viewFits()) { cancel(); status_ = "Canceled: the branch would extend beyond the available space."; emit stateChanged(); return; }
    auto action = engine_.release(pointerId,{x,y});
    if (engine_.active()) return;
    dispatchAction(action);
}
void MenuSession::dispatchAction(const std::optional<radial::Action>& action) {
    if (action && action->kind != radial::ActionKind::None) {
        const auto mods = modifiers(*action);
        QString description = action->kind == radial::ActionKind::Text ? "Insert text: " + QString::fromStdString(action->text).left(60) : (mods.isEmpty()?QString{}:mods.join('+')+'+') + QString::fromStdString(action->key);
        if (!simulation_ && platform_) {
            pendingDispatch_ = true;
            pendingDescription_ = description;
            const bool queued = action->kind == radial::ActionKind::Shortcut ?
                platform_->queueShortcut(QString::fromStdString(action->key),mods,gestureTarget_) :
                platform_->queueText(QString::fromStdString(action->text),gestureTarget_);
            if (queued) {
                status_ = "Waiting for pen button release";
                emit stateChanged();
                return;
            }
            pendingDispatch_ = false;
            status_ = platform_->lastError();
            emit actionReported(status_);
            finish();
            return;
        }
        status_ = "Preview: " + description;
        emit actionReported(status_); emit cue("commit");
    } else status_ = "Canceled · no action sent";
    finish();
}
void MenuSession::finish() {
    parentHoldTimer_.stop();parentPress_.reset();parentStack_.clear();parentPointer_.reset();ignoredParentPointer_.reset();parentSelected_.clear();
    if (!simulation_ && platform_) platform_->releaseTarget();
    gestureTarget_ = 0; previousSelection_.clear(); previousDepth_ = 0;
    pendingDescription_.clear();
    if (pending_) { releaseModifiers(); profile_ = *pending_; pending_.reset(); emit geometryChanged(); }
    emit stateChanged();
}
QString MenuSession::modifierState(const radial::Node& node) const {
    if(node.action.kind!=radial::ActionKind::Modifier)return {};
    const auto key=QString::fromStdString(node.action.key);
    return armedModifiers_.contains(key)?"armed":latchedModifiers_.contains(key)?"latched":heldModifiers_.contains(key)?(modifierDragged_ && modifierKey_==key?"dragged":"held"):"";
}
bool MenuSession::holdModifier(const QString& key, bool held) {
    if(!simulation_ && platform_ && !platform_->setModifierHeld(key,held,gestureTarget_)){
        status_=platform_->lastError();emit actionReported(status_);return false;
    }
    if(held){if(!heldModifiers_.contains(key))heldModifiers_.append(key);}
    else {heldModifiers_.removeAll(key);latchedModifiers_.removeAll(key);armedModifiers_.removeAll(key);}
    return true;
}
bool MenuSession::beginModifier(const radial::Node& node, int pointerId, QPointF point, bool fromFan) {
    modifierFloating_=!docked_||parentMenuOpen();
    modifierKey_=QString::fromStdString(node.action.key);modifierNode_=QString::fromStdString(node.id);
    releasingLatch_=latchedModifiers_.contains(modifierKey_)||armedModifiers_.contains(modifierKey_);
    if(!holdModifier(modifierKey_,!releasingLatch_)){engine_.cancel();finish();return false;}
    modifierPointer_=pointerId;modifierStart_=point;pressedAt_=modifierClock_.elapsed();modifierDragged_=fromFan;
    status_=modifierKey_+(releasingLatch_?" released":modifierFloating_?" held · tap to use for the next pen stroke":" held · drag out to lock until lift");
    emit geometryChanged();emit stateChanged();return true;
}
void MenuSession::moveModifierPointer(int pointerId,qreal x,qreal y){
    if(!modifierPointer_||*modifierPointer_!=pointerId)return;
    if(!simulation_&&platform_&&!platform_->targetUnchanged(gestureTarget_)){cancel();return;}
    if(!modifierDragged_&&QLineF(modifierStart_,{x,y}).length()>28){modifierDragged_=true;lastTapKey_.clear();status_=modifierKey_+" locked for this contact · lift to release";emit geometryChanged();emit stateChanged();}
}
void MenuSession::releaseModifiers() {
    modifierPointer_.reset();lastTapKey_.clear();armedModifiers_.clear();
    if(!simulation_ && platform_){
        platform_->releaseModifiers();heldModifiers_=platform_->heldModifiers();
        latchedModifiers_=heldModifiers_;
    } else {heldModifiers_.clear();latchedModifiers_.clear();}
    emit geometryChanged();emit stateChanged();
}
void MenuSession::finishNextStroke() {
    if(simulation_){for(const auto& key:armedModifiers_)heldModifiers_.removeAll(key);}
    else if(platform_)heldModifiers_=platform_->heldModifiers();
    for(const auto& key:armedModifiers_)if(heldModifiers_.contains(key)&&!latchedModifiers_.contains(key))latchedModifiers_.append(key);
    armedModifiers_.clear();status_=heldModifiers_.isEmpty()?"Pen stroke finished · modifiers released":"Tap the modifier to retry release";
    emit geometryChanged();emit stateChanged();
}
void MenuSession::cancel() {
    parentHoldTimer_.stop();parentPress_.reset();parentStack_.clear();parentPointer_.reset();ignoredParentPointer_.reset();parentSelected_.clear();
    releaseModifiers();
    engine_.cancel();
    pendingDispatch_ = false;
    if (!simulation_ && platform_) platform_->cancelQueuedAction();
    status_ = "Canceled · no action sent";
    finish();
}
QVariantList MenuSession::parentButtons() const {
    QVariantList result;if(!parentMenuOpen())return result;
    const auto& group=profile_.nodes.at(parentStack_.last().toStdString());
    for(int i=0;i<6;++i){
        if(!group.children[i])continue;const auto& node=profile_.nodes.at(*group.children[i]);const double angle=-Pi/2+i*Pi/3;
        result.append(QVariantMap{{"id",QString::fromStdString(node.id)},{"label",QString::fromStdString(node.label)},
            {"icon",QString::fromStdString(node.action.kind==radial::ActionKind::Modifier?node.action.key:node.icon)},
            {"caption",QString::fromStdString(node.label).left(13)},{"group",node.kind==radial::NodeKind::Group},
            {"modifierState",modifierState(node)},{"slot",i},{"color",colors[i]},{"angle",angle},
            {"cx",parentCenter_.x()+70*std::cos(angle)},{"cy",parentCenter_.y()+70*std::sin(angle)}});
    }
    return result;
}
QString MenuSession::parentLabel() const {return parentMenuOpen()?QString::fromStdString(profile_.nodes.at(parentStack_.last().toStdString()).label):QString{};}
QString MenuSession::parentHit(QPointF point) const {
    const auto delta=point-parentCenter_;const double radius=std::hypot(delta.x(),delta.y());
    if(radius<44 || radius>96)return {};
    for(const auto& value:parentButtons()){
        const auto b=value.toMap();if(std::abs(std::remainder(std::atan2(delta.y(),delta.x())-b["angle"].toDouble(),2*Pi))<Pi/6-.035)return b["id"].toString();
    }
    return {};
}
void MenuSession::beginParentPress(const QString& id,int pointer,QPointF point){parentPress_=ParentPress{id,pointer,point};parentHoldTimer_.start();}
void MenuSession::openParent(const QString& id,QPointF point,bool hold,int pointer){
    if(!std::isfinite(point.x())||!std::isfinite(point.y())||width_<208||height_<208){cancel();status_="Not enough room for this radial. Enlarge the preview.";emit stateChanged();return;}
    const auto node=profile_.nodes.find(id.toStdString());
    if(node==profile_.nodes.end()||node->second.kind!=radial::NodeKind::Group)return;
    const bool nested=parentMenuOpen();
    if(nested && parentStack_.last()!=id && parentStack_.size()>=int(radial::MaximumGroupDepth))return;
    parentHoldTimer_.stop();parentPress_.reset();engine_.cancel();parentSelected_.clear();
    parentCenterPressed_=false;
    if(!nested || parentStack_.last()!=id)parentStack_.append(id);
    const qreal marginX=std::min(qreal(104),width_/2),marginY=std::min(qreal(104),height_/2);
    parentCenter_={std::clamp(point.x(),marginX,width_-marginX),std::clamp(point.y(),marginY,height_-marginY)};
    if(hold){ignoredParentPointer_=pointer;parentPointer_.reset();}
    else {ignoredParentPointer_.reset();parentPointer_=pointer;}
    status_="Choose a child · center goes back · Esc closes";emit cue("open");emit stateChanged();
}
void MenuSession::parentPointerPressed(int pointer,qreal x,qreal y){
    if(!std::isfinite(x)||!std::isfinite(y)){cancel();return;}
    if(!parentMenuOpen() || pendingDispatch_ || parentPointer_ || (ignoredParentPointer_&&*ignoredParentPointer_==pointer))return;
    if(!simulation_&&platform_&&!platform_->targetUnchanged(gestureTarget_)){cancel();return;}
    parentPointer_=pointer;parentSelected_=parentHit({x,y});
    parentCenterPressed_=QLineF(parentCenter_,{x,y}).length()<=40;
    const auto node=profile_.nodes.find(parentSelected_.toStdString());
    if(node!=profile_.nodes.end()&&node->second.kind==radial::NodeKind::Group)beginParentPress(parentSelected_,pointer,{x,y});
    if(node!=profile_.nodes.end()&&node->second.action.kind==radial::ActionKind::Modifier)beginModifier(node->second,pointer,{x,y},false);
    emit stateChanged();
}
void MenuSession::parentPointerMoved(int pointer,qreal x,qreal y){
    if(!parentPointer_ || *parentPointer_!=pointer)return;
    if(!std::isfinite(x)||!std::isfinite(y)){cancel();return;}
    if(modifierPointer_){moveModifierPointer(pointer,x,y);return;}
    if(!simulation_&&platform_&&!platform_->targetUnchanged(gestureTarget_)){cancel();return;}
    if(parentPress_){
        const auto distance=QLineF(parentPress_->point,{x,y}).length();if(distance>12)parentHoldTimer_.stop();
        if(distance>=radial::GestureEngine::OpenThreshold){const auto press=*parentPress_;openParent(press.id,{x,y},false,pointer);return;}
    }
    const auto selected=parentHit({x,y});if(selected!=parentSelected_){parentSelected_=selected;if(!selected.isEmpty())emit cue("tick");emit stateChanged();}
    const auto node=profile_.nodes.find(selected.toStdString());
    if(node!=profile_.nodes.end()&&node->second.kind==radial::NodeKind::Group&&!parentPress_)beginParentPress(selected,pointer,{x,y});
    if(node!=profile_.nodes.end()&&node->second.action.kind==radial::ActionKind::Modifier)beginModifier(node->second,pointer,{x,y},true);
}
void MenuSession::parentPointerReleased(int pointer,qreal x,qreal y){
    if(ignoredParentPointer_ && *ignoredParentPointer_==pointer){ignoredParentPointer_.reset();return;}
    if(!parentPointer_ || *parentPointer_!=pointer)return;
    if(!simulation_&&platform_&&!platform_->targetUnchanged(gestureTarget_)){cancel();return;}
    if(modifierPointer_){parentStack_.clear();pointerReleased(pointer,x,y);return;}
    const auto id=parentHit({x,y});parentHoldTimer_.stop();parentPress_.reset();parentPointer_.reset();parentSelected_.clear();
    const auto node=profile_.nodes.find(id.toStdString());
    if(node!=profile_.nodes.end()){
        if(node->second.kind==radial::NodeKind::Group){openParent(id,parentCenter_,true,pointer);ignoredParentPointer_.reset();return;}
        if(node->second.action.kind==radial::ActionKind::Modifier){
            // A release can carry the first position inside this child when moves are coalesced.
            if(!beginModifier(node->second,pointer,{x,y},false))return;
            parentStack_.clear();pointerReleased(pointer,x,y);return;
        }
        const auto action=node->second.action;parentStack_.clear();dispatchAction(action);return;
    }
    if(QLineF(parentCenter_,{x,y}).length()<=40){if(parentCenterPressed_)parentBack();else emit stateChanged();return;}
    cancel();
}
void MenuSession::parentBack(){
    if(!parentMenuOpen())return;
    parentHoldTimer_.stop();parentPress_.reset();parentPointer_.reset();ignoredParentPointer_.reset();parentSelected_.clear();
    if(parentStack_.size()>1){parentStack_.removeLast();emit cue("back");emit stateChanged();}
    else cancel();
}
}
