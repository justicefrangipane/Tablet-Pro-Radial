#include "app/ProfileCodec.h"
#include "app/StudioController.h"
#include "platform/WindowPlatform.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

using namespace tabletpro;

namespace {
int checks = 0;
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
void soundTests(const QString& temporaryPath) {
    StudioController studio(nullptr,temporaryPath+"/sound-config",true);
    check(studio.soundTheme()=="crystal-whisper"&&!studio.soundEnabled(),"Crystal whisper defaults without changing mute");
    auto* audio=studio.findChild<AudioFeedback*>("audioFeedback");
    check(audio&&audio->findChildren<QSoundEffect*>().size()==36,"all twelve complete sound sets are embedded");
    check(soundThemeIds().size()==soundThemeNames().size()&&soundThemeIds().first()==defaultSoundTheme()&&soundThemeIds()[1]=="watch-tick","default style is first and Watch tick is second");
    for(const auto& excluded:{"paper-key","oiled-gear","dew-click"})check(!soundThemeIds().contains(excluded),"rejected auditions are not offered");
    QElapsedTimer deadline;deadline.start();
    auto ready=[&]{for(auto* effect:audio->findChildren<QSoundEffect*>())if(effect->status()!=QSoundEffect::Ready)return false;return true;};
    while(!ready()&&deadline.elapsed()<4000){QEventLoop loop;QTimer::singleShot(25,&loop,&QEventLoop::quit);loop.exec();}
    check(ready(),"Qt decodes every packaged WAV before playback");
    int played=0;QString actualTheme,actualCue;bool wasPreview=false;
    QObject::connect(audio,&AudioFeedback::cuePlayed,[&](const QString& theme,const QString& cue,bool preview){++played;actualTheme=theme;actualCue=cue;wasPreview=preview;});
    studio.playMenuSound("open");check(played==0,"muted interaction is silent");
    for(const auto& theme:soundThemeIds()){
        studio.setSoundTheme(theme);studio.previewSound();
        check(actualTheme==theme&&actualCue=="open"&&wasPreview&&!studio.soundEnabled(),"explicit preview plays selected style while preserving mute");
    }
    studio.setSoundTheme("watch-tick");studio.setSoundTheme("unknown");check(studio.soundTheme()=="watch-tick","unknown theme cannot erase selected style");
    studio.setSoundEnabled(true);
    for(const auto& cue:{"press","open","close"}){studio.playMenuSound(cue);check(actualCue==cue&&!wasPreview,"interaction maps to matching press/open/close clip");}
    studio.playMenuSound("back");check(actualCue=="close","parent Back uses the closing sound");
    studio.setSoundEnabled(false);const int muted=played;studio.playMenuSound("commit");check(played==muted,"Sound off silences all action cues");
    for(auto* effect:audio->findChildren<QSoundEffect*>())check(!effect->isPlaying(),"muting stops a current voice");
    studio.setSoundTheme("watch-tick");
    {StudioController restored(nullptr,temporaryPath+"/sound-config",true);check(restored.soundTheme()=="watch-tick"&&!restored.soundEnabled(),"restart preserves Watch tick independently of mute");}
    QSettings().setValue("soundTheme","unavailable");
    {StudioController restored(nullptr,temporaryPath+"/sound-config",true);check(restored.soundTheme()=="crystal-whisper","invalid saved style falls back to Crystal whisper");}
    studio.setSoundTheme(defaultSoundTheme());
}
QVariantMap button(const MenuSession& session, const QString& id, bool child = false) {
    for (const auto& item : child ? (session.parentMenuOpen() ? session.parentButtons() : session.childButtons()) : session.buttons()) {
        const auto entry = item.toMap();
        if (entry.value("id").toString() == id) return entry;
    }
    throw std::runtime_error("Missing session button: " + id.toStdString());
}
void writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "write test profile");
}
void modeTransitionTests(){
    const auto wait=[](int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();};
    MenuSession session(true,nullptr);session.setDurationMs(400);
    for(int cycle=0;cycle<4;++cycle){
        const auto start=session.dockProgress();session.toggleDock();
        check(session.transitioning()&&session.dockProgress()==start,"every mode change starts at the visible frame");
        wait(110);const auto middle=session.dockProgress();
        check(session.transitioning()&&middle>0&&middle<1,"repeated dock and radial changes both animate");
        check(session.docked()?middle>start:middle<start,"mode animation travels in the requested direction");
        wait(400);
        check(!session.transitioning()&&session.dockProgress()==(session.docked()?1:0),"each mode animation reaches its endpoint");
    }
    session.toggleDock();wait(150);const auto before=session.dockProgress();session.toggleDock();
    check(before>0&&before<1&&session.dockProgress()==before,"mid-flight reversal keeps the currently visible progress");
    wait(35);check(session.dockProgress()<before,"reversed transition travels back immediately");
    wait(450);check(!session.transitioning()&&session.dockProgress()==0,"reversed transition settles to radial");
    session.setReducedMotion(true);session.toggleDock();
    check(!session.transitioning()&&session.dockProgress()==1,"Reduced motion still changes modes immediately");
}
void parentMenuTests(const QString& directory){
    const auto wait=[](int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();};
    MenuSession session(true,nullptr);session.setProfile(radial::legacyDefaultProfile());session.setViewport(1000,800);session.setReducedMotion(true);session.toggleDock();
    const auto root=button(session,"text");const auto x=root["dockX"].toDouble(),y=root["dockY"].toDouble();
    int reports=0;QObject::connect(&session,&MenuSession::actionReported,[&]{++reports;});
    session.pointerPressed(1,x,y);wait(90);session.pointerReleased(1,x,y);wait(330);
    check(!session.parentMenuOpen()&&!session.active()&&reports==0,"short parent tap cancels the delayed opening");
    session.pointerPressed(1,x,y);wait(230);check(!session.parentMenuOpen(),"parent does not open before the 300 ms hold");
    wait(110);check(session.parentMenuOpen()&&session.parentCenter()==QPointF(500,400),"300 ms hold opens at the preview pen tip");
    auto bold=button(session,"text.bold",true);
    session.parentPointerPressed(-1,bold["cx"].toDouble(),bold["cy"].toDouble());
    session.pointerReleased(1,bold["cx"].toDouble(),bold["cy"].toDouble());
    check(!session.parentMenuOpen()&&!session.active()&&reports==0,"launching finger lift closes the child menu without choosing a child");
    session.parentPointerReleased(-1,bold["cx"].toDouble(),bold["cy"].toDouble());
    check(!session.active()&&reports==0,"late pen lift after parent release cannot select a hidden child");
    session.pointerPressed(1,x,y);wait(330);bold=button(session,"text.bold",true);
    session.parentPointerPressed(-1,bold["cx"].toDouble(),bold["cy"].toDouble());
    session.parentPointerReleased(-1,bold["cx"].toDouble(),bold["cy"].toDouble());
    session.pointerReleased(1,x,y);
    check(!session.active()&&reports==1,"pen can choose a child while the launching finger stays held");
    session.pointerPressed(2,x,y);session.pointerMoved(2,x+42,y);wait(330);
    check(!session.parentMenuOpen()&&session.childButtons().size()==6,"slide opens the original fan and disarms delayed pen relocation");
    session.pointerReleased(2,x+42,y);check(!session.active()&&reports==1,"neutral fan release closes without an action");
    session.pointerPressed(3,x,y);session.cancel();wait(330);check(!session.active(),"cancel removes pending 300 ms timer");
    session.pointerPressed(3,x,y);session.setViewport(1100,800);wait(330);check(!session.active(),"viewport change removes pending hold");
    session.setProfile(radial::defaultProfile());const auto tools=button(session,"tools");
    session.pointerPressed(4,tools["dockX"].toDouble(),tools["dockY"].toDouble());wait(330);
    auto text=button(session,"text",true);session.parentPointerPressed(5,text["cx"].toDouble(),text["cy"].toDouble());session.parentPointerReleased(5,text["cx"].toDouble(),text["cy"].toDouble());
    check(session.parentLabel()=="Text"&&session.parentButtons().size()==6,"existing nested parents open their own radial by tapping");
    session.parentBack();check(session.parentButtons().size()==4,"Back returns to the previous parent menu");
    session.pointerReleased(4,tools["dockX"].toDouble(),tools["dockY"].toDouble());
    check(!session.active(),"the original parent release still closes children after navigating Back");
    session.pointerPressed(6,tools["dockX"].toDouble(),tools["dockY"].toDouble());
    session.pointerMoved(6,tools["dockX"].toDouble()+42,tools["dockY"].toDouble());
    text=button(session,"text",true);session.pointerMoved(6,text["cx"].toDouble(),text["cy"].toDouble());wait(330);
    check(session.parentMenuOpen()&&session.parentLabel()=="Text"&&session.parentCenter()==QPointF(550,400),"holding a nested fan parent opens its radial at the pen tip");session.cancel();
    session.toggleDock();const auto floatingTools=button(session,"tools");session.pointerPressed(7,floatingTools["cx"].toDouble(),floatingTools["cy"].toDouble());wait(330);
    check(session.parentMenuOpen(),"floating parent supports the same 300 ms hold");session.cancel();
    auto withModifier=radial::defaultProfile();
    withModifier.nodes.at("tools").children[4]="modifier.Alt";withModifier.rootSlots[2].reset();
    session.setProfile(withModifier);session.toggleDock();const auto modifierParent=button(session,"tools");
    const auto openModifierParent=[&]{session.pointerPressed(8,modifierParent["dockX"].toDouble(),modifierParent["dockY"].toDouble());wait(330);};
    openModifierParent();auto alt=button(session,"modifier.Alt",true);
    session.parentPointerPressed(9,500,400);session.parentPointerReleased(9,alt["cx"].toDouble(),alt["cy"].toDouble());
    check(!session.active()&&session.heldModifiers().contains("Alt"),"coalesced release into a child modifier arms the next pen stroke");
    session.pointerReleased(8,modifierParent["dockX"].toDouble(),modifierParent["dockY"].toDouble());
    check(session.heldModifiers().contains("Alt"),"releasing the parent after a completed modifier selection preserves the selected hold");
    session.finishNextStroke();check(session.heldModifiers().isEmpty(),"child modifier releases after its pen stroke");
    openModifierParent();alt=button(session,"modifier.Alt",true);
    session.parentPointerPressed(9,alt["cx"].toDouble(),alt["cy"].toDouble());session.parentPointerReleased(9,alt["cx"].toDouble(),alt["cy"].toDouble());
    check(!session.active()&&session.heldModifiers().contains("Alt"),"child modifier tap uses floating one-stroke behavior even when launched from the dock");session.cancel();
    PenPositionTracker pen;check(!pen.position(),"pen position starts unknown");
    const QRectF penRange{100,200,1000,2000},leftDisplay{-1920,0,1920,1080};
    check(PenPositionTracker::mapHidPosition({100,200},penRange,leftDisplay,1)==std::optional<QPointF>{{-1920,0}},"HID coordinates respect nonzero logical minima and negative screen origin");
    check(PenPositionTracker::mapHidPosition({1100,2200},penRange,leftDisplay,1)==std::optional<QPointF>{{-1,1079}},"HID maximum stays inside the mapped display");
    check(PenPositionTracker::mapHidPosition({100,200},penRange,leftDisplay,2)==std::optional<QPointF>{{-1,0}},"portrait rotation maps pen coordinates clockwise");
    check(!PenPositionTracker::mapHidPosition({99,200},penRange,leftDisplay,1)&&!PenPositionTracker::mapHidPosition({100,200},{},leftDisplay,1),"invalid pen ranges and out-of-range samples cannot move the menu");
    check(PenPositionTracker::isPenMessage(0xff515701)&&!PenPositionTracker::isPenMessage(0xff515781)&&!PenPositionTracker::isPenMessage(0),"pen signature excludes touch and ordinary mouse input");
    pen.observe({350,450});pen.observe({std::numeric_limits<double>::quiet_NaN(),10});
    check(pen.position()==std::optional<QPointF>{{350,450}},"invalid pen coordinates cannot replace the latest position");pen.stop();check(!pen.position(),"stopping tracking clears its location");
    StudioController editor(nullptr,directory+"/parent-editor",true);editor.selectNode("brush");const auto original=editor.draft();
    editor.makeParent();const auto first=*editor.draft().nodes.at("brush").children[0];
    check(editor.selectedKind()=="group"&&editor.draft().nodes.at(first).action==original.nodes.at("brush").action,"Make parent retains the existing shortcut");
    editor.addNextButton(false);const auto second=editor.selectedId();editor.addNextButton(false);
    check(editor.selectedParentId()=="brush"&&editor.draft().nodes.at("brush").children[2].has_value(),"repeated Add child stays within the same parent");
    const auto three=editor.draft();editor.addChild(0,false);check(editor.selectedId()==QString::fromStdString(first)&&editor.draft()==three,"occupied child slot opens the existing child without replacing it");
    editor.undo();check(editor.selectedId()==second&&!editor.draft().nodes.at("brush").children[2],"Undo restores the prior child and editor selection");editor.redo();
    editor.applyDraft();check(editor.error().isEmpty(),"new parent and children apply as a valid menu");
    StudioController reopened(nullptr,directory+"/parent-editor",true);check(reopened.draft()==editor.draft(),"new parent structure survives save and reopen");
    editor.selectNode("");const auto full=editor.draft();editor.addNextButton(true);check(editor.draft()==full&&!editor.canAddParent(),"full main menu cannot overwrite an existing button");
    editor.selectNode("eraser");editor.removeSelected();editor.selectNode("");editor.addNextButton(true);const auto parent=editor.selectedId();editor.addNextButton(false);
    check(editor.selectedParentId()==parent&&radial::validate(editor.draft()).empty(),"Add parent and Add child build a valid new radial in an empty main slot");
    editor.makeParent();editor.addNextButton(true);editor.addNextButton(true);
    check(!editor.canAddParent(),"editor prevents a fifth parent level");
}
void inPlaceParentTests(){
    MenuSession session(true,nullptr);session.setProfile(radial::defaultProfile());session.setViewport(1200,900);session.setRadialCenter(460,420);
    const auto center=session.radialCenter();const auto tools=button(session,"tools");
    const QPointF at(tools["cx"].toDouble(),tools["cy"].toDouble());const double angle=tools["angle"].toDouble();
    int reports=0;QObject::connect(&session,&MenuSession::actionReported,[&]{++reports;});
    session.pointerPressed(1,at.x(),at.y());session.pointerReleased(1,at.x(),at.y());
    check(session.parentInPlace()&&session.parentCenter()==center&&session.childButtons().isEmpty(),"floating parent tap replaces the existing ring at its exact center");
    const auto text=button(session,"text",true);const QPointF textAt(text["cx"].toDouble(),text["cy"].toDouble());
    session.pointerPressed(1,textAt.x(),textAt.y());session.pointerReleased(1,textAt.x(),textAt.y());
    check(session.parentLabel()=="Text"&&session.parentCenter()==center&&reports==0,"reusing the pointer can tap a nested parent without dismissing or dispatching");
    session.parentBack();check(session.parentLabel()=="Tools"&&session.parentCenter()==center,"Back restores the parent ring in place");
    check(session.beginRadialDrag(),"in-place child menu allows center dragging");session.moveRadialCenter(600,470);session.endRadialDrag();
    check(session.parentCenter()==QPointF(600,470)&&session.radialCenter()==session.parentCenter(),"children and root move together without changing navigation");
    session.parentBack();check(!session.active()&&session.radialCenter()==QPointF(600,470),"returning to the root preserves its dragged position");
    const auto movedTools=button(session,"tools");const QPointF start(movedTools["cx"].toDouble(),movedTools["cy"].toDouble());
    session.pointerPressed(2,start.x(),start.y());const auto out=start+QPointF(45*std::cos(angle),45*std::sin(angle));session.pointerMoved(2,out.x(),out.y());
    check(session.parentInPlace()&&session.parentCenter()==session.radialCenter()&&session.childButtons().isEmpty(),"outward pull replaces the floating ring instead of opening a side fan");
    session.pointerReleased(2,out.x(),out.y());check(session.parentInPlace()&&reports==0,"opening slide release leaves children available without selecting one");
    const auto child=button(session,"text",true);const double a=child["angle"].toDouble();const QPointF p(child["cx"].toDouble(),child["cy"].toDouble());
    session.pointerPressed(2,p.x(),p.y());session.pointerReleased(2,p.x()+45*std::cos(a),p.y()+45*std::sin(a));
    check(session.parentInPlace()&&session.parentLabel()=="Text"&&reports==0,"coalesced nested slide opens in place and consumes only its opening contact");
    const auto bold=button(session,"text.bold",true);session.pointerPressed(2,bold["cx"].toDouble(),bold["cy"].toDouble());session.pointerReleased(2,bold["cx"].toDouble(),bold["cy"].toDouble());
    check(!session.active()&&reports==1&&session.status()=="Preview: Ctrl+B","a subsequent child tap dispatches exactly once");
}
void reparentTests(const QString& directory){
    StudioController editor(nullptr,directory+"/reparent",true);
    const auto initial=editor.draft();
    check(editor.reparentProblem("brush","tools").isEmpty(),"root action can become a child of an existing parent");
    check(editor.reparentButton("brush","tools"),"move root into parent");
    const auto moved=editor.draft();
    check(!moved.rootSlots[0]&&moved.nodes.at("tools").children[4]=="brush","move uses first free child slot and vacates source slot without shifting others");
    check(moved.nodes.at("brush")==initial.nodes.at("brush")&&moved.nodes.size()==initial.nodes.size(),"move preserves exact action and IDs without copying or deleting nodes");
    check(editor.runtime()->buttons().size()==6&&editor.selectedParentId()=="tools","move changes the draft and inspector only until Apply");
    check(radial::validate(moved).empty(),"reparented draft is valid");
    check(!editor.reparentButton("brush","tools")&&editor.draft()==moved,"same-parent drop is a no-op");
    editor.undo();check(editor.draft()==initial,"single Undo restores the entire move despite a rejected drop");
    editor.redo();check(editor.draft()==moved,"Redo restores parent and child slots");
    editor.applyDraft();check(editor.error().isEmpty()&&editor.runtime()->buttons().size()==5,"Apply activates reparented layout");
    StudioController reopened(nullptr,directory+"/reparent",true);check(reopened.draft()==moved,"moved tree survives save and reopen");
    check(!editor.reparentButton("eraser","text")&&editor.error().contains("six"),"full parent refuses drop without replacing children");
    check(!editor.reparentButton("tools","text.more")&&editor.error().contains("own children"),"ancestor cannot move inside its descendant");
    check(!editor.reparentButton("tools","tools"),"self drop refused");
    check(!editor.reparentButton("eraser","brush"),"ordinary action is not silently converted to parent");
    check(!editor.reparentButton("missing","tools")&&!editor.reparentButton("brush","missing")&&editor.draft()==moved,"missing or invalid drop leaves the tree unchanged");
    editor.useDefaultMenu();editor.selectNode("brush");editor.makeParent();
    const auto beforeBranch=editor.draft();
    check(editor.reparentButton("tools","brush"),"whole parent subtree can move when four-level depth fits");
    check(editor.draft().nodes.at("tools")==beforeBranch.nodes.at("tools")&&radial::validate(editor.draft()).empty(),"nested parent move retains every descendant and shortcut");
    editor.selectNode("eraser");editor.makeParent();const auto beforeDeep=editor.draft();
    check(!editor.reparentButton("brush","eraser")&&editor.error().contains("four")&&editor.draft()==beforeDeep,"moving a deep subtree cannot exceed the parent limit");
    editor.useDefaultMenu();editor.selectNode("eraser");editor.makeParent();
    const auto child=*editor.draft().nodes.at("eraser").children[0];const auto beforeLast=editor.draft();
    check(editor.reparentButton(QString::fromStdString(child),"tools")&&editor.status().contains("is empty"),"last-child move keeps the old parent and explains how to finish it");
    check(editor.draft().nodes.contains("eraser")&&editor.draft().nodes.at("eraser").kind==radial::NodeKind::Group,"empty source parent is not silently deleted");
    editor.undo();check(editor.draft()==beforeLast,"Undo restores the last child to its source parent");
}
void codecTests() {
    const auto original = radial::defaultProfile();
    const auto encoded = encodeProfile(original);
    const auto decoded = decodeProfile(encoded);
    check(decoded.profile && decoded.error.isEmpty(), "valid profile decodes");
    check(*decoded.profile == original, "codec roundtrip preserves full model including empty slots");
    auto document = QJsonDocument::fromJson(encoded).object();
    auto nodes = document["nodes"].toArray();
    nodes.append(nodes.first()); document["nodes"] = nodes;
    check(!decodeProfile(QJsonDocument(document).toJson()).profile, "duplicate node IDs rejected before map insertion");
    document = QJsonDocument::fromJson(encoded).object(); document["schemaVersion"] = 2;
    check(!decodeProfile(QJsonDocument(document).toJson()).profile, "future schema rejected");
    document["schemaVersion"] = 1.5;
    check(!decodeProfile(QJsonDocument(document).toJson()).profile, "fractional schema rejected");
    document = QJsonDocument::fromJson(encoded).object();
    document["rootSlots"] = QJsonArray{"brush", "eraser"};
    check(!decodeProfile(QJsonDocument(document).toJson()).profile, "slot count cannot silently compact");
    document = QJsonDocument::fromJson(encoded).object(); nodes = document["nodes"].toArray();
    for (int i = 0; i < nodes.size(); ++i) {
        auto node = nodes[i].toObject();
        if (node["id"] != "brush") continue;
        auto action = node["action"].toObject(); action["modifiers"] = QJsonArray{"Ctrl", "Ctrl"};
        node["action"] = action; nodes[i] = node;
    }
    document["nodes"] = nodes;
    check(!decodeProfile(QJsonDocument(document).toJson()).profile, "duplicate modifier rejected");
    check(!decodeProfile("{broken").profile, "malformed JSON rejected");
    check(!decodeProfile(QByteArray(1024 * 1024 + 1, ' ')).profile, "oversize profile rejected");
    auto shared = original;
    shared.nodes.at("settings").children[3] = "text.bold";
    check(!decodeProfile(encodeProfile(shared)).profile, "shared descendant rejected before destructive tree edits");
    document = QJsonDocument::fromJson(encoded).object(); nodes = document["nodes"].toArray();
    for (int i = 0; i < nodes.size(); ++i) {
        auto node = nodes[i].toObject();
        if (node["id"] == "text") { node["action"] = QJsonObject{}; nodes[i] = node; }
    }
    document["nodes"] = nodes;
    check(!decodeProfile(QJsonDocument(document).toJson()).profile, "group cannot silently discard action payload");
    document = QJsonDocument::fromJson(encoded).object(); nodes = document["nodes"].toArray();
    for (int i = 0; i < nodes.size(); ++i) {
        auto node = nodes[i].toObject();
        if (node["id"] == "brush") {
            node["slots"] = QJsonArray{"text.bold", QJsonValue::Null, QJsonValue::Null,
                                        QJsonValue::Null, QJsonValue::Null, QJsonValue::Null};
            nodes[i] = node;
        }
    }
    document["nodes"] = nodes;
    check(!decodeProfile(QJsonDocument(document).toJson()).profile, "action cannot silently discard child slots");
}

void testDocumentCodecTests() {
    const auto profile=radial::defaultProfile();
    const RadialSettings settings{true,true,1111,false};
    const auto encoded=encodeTestDocument(profile,settings);
    const auto decoded=decodeRadialDocument(encoded);
    check(decoded.profile&&*decoded.profile==profile&&decoded.settings==settings&&decoded.error.isEmpty(),
          "test document roundtrip preserves complete menu and all four settings");
    const auto plain=decodeRadialDocument(encodeProfile(profile));
    check(plain.profile&&*plain.profile==profile&&!plain.settings,"plain schema 1 remains readable without imported settings");
    auto root=QJsonDocument::fromJson(encoded).object();
    auto legacy=root;auto legacyPrefs=legacy["settings"].toObject();legacyPrefs.remove("soundTheme");legacy["settings"]=legacyPrefs;
    const auto legacyResult=decodeRadialDocument(QJsonDocument(legacy).toJson());
    check(legacyResult.settings&&legacyResult.settings->soundTheme=="crystal-whisper","older documents without a style use Crystal whisper");
    for(const auto& invalid:{QJsonValue("unknown"),QJsonValue(4),QJsonValue(false),QJsonValue(QJsonValue::Null)}){
        auto bad=root;auto values=bad["settings"].toObject();values["soundTheme"]=invalid;bad["settings"]=values;
        check(!decodeRadialDocument(QJsonDocument(bad).toJson()).profile,"invalid imported sound style rejects the document");
    }
    root.remove("settings");
    check(decodeRadialDocument(QJsonDocument(root).toJson()).profile&&!decodeRadialDocument(QJsonDocument(root).toJson()).settings,
          "test document may omit settings without inventing defaults");
    for(const auto& badValue: {QJsonValue("slow"),QJsonValue(249),QJsonValue(2401),QJsonValue(1000.5),QJsonValue(true)}){
        root=QJsonDocument::fromJson(encoded).object();auto prefs=root["settings"].toObject();prefs["durationMs"]=badValue;root["settings"]=prefs;
        const auto result=decodeRadialDocument(QJsonDocument(root).toJson());
        check(!result.profile&&!result.settings&&!result.error.isEmpty(),"invalid animation duration rejects whole test document");
    }
    for(const auto& field:{"soundEnabled","reducedMotion","backdropEnabled"}){
        root=QJsonDocument::fromJson(encoded).object();auto prefs=root["settings"].toObject();prefs[field]="true";root["settings"]=prefs;
        check(!decodeRadialDocument(QJsonDocument(root).toJson()).profile,"setting strings cannot masquerade as boolean values");
    }
    root=QJsonDocument::fromJson(encoded).object();auto prefs=root["settings"].toObject();prefs.remove("backdropEnabled");root["settings"]=prefs;
    check(!decodeRadialDocument(QJsonDocument(root).toJson()).profile,"partial settings cannot silently invent values");
    prefs["backdropEnabled"]=true;prefs["enableInput"]=true;root["settings"]=prefs;
    check(!decodeRadialDocument(QJsonDocument(root).toJson()).profile,"unknown imported setting rejected");
    root=QJsonDocument::fromJson(encoded).object();root["schemaVersion"]=1.5;
    check(!decodeRadialDocument(QJsonDocument(root).toJson()).profile,"fractional document schema rejected");
    root["schemaVersion"]=2;
    check(!decodeRadialDocument(QJsonDocument(root).toJson()).profile,"future document schema rejected");
    root=QJsonDocument::fromJson(encoded).object();root["startupAction"]="run";
    check(!decodeRadialDocument(QJsonDocument(root).toJson()).profile,"unknown envelope field rejected");
    root=QJsonDocument::fromJson(encoded).object();root["profile"]=QJsonObject{{"format","tabletpro.radial.test"}};
    check(!decodeRadialDocument(QJsonDocument(root).toJson()).profile,"nested document envelope cannot substitute for profile");
    check(!decodeRadialDocument(QByteArray(1024*1024+1,' ')).profile,"oversize test document rejected");
}

void controllerTests(const QString& temporaryPath) {
    StudioController controller(nullptr, temporaryPath + "/config");
    controller.setSoundEnabled(false);
    controller.setReducedMotion(true);
    const auto initial = controller.draft();
    check(!controller.dirty(), "initial editor is clean");
    check(!controller.canUndo() && !controller.canRedo(), "initial undo history empty");
    controller.selectNode("brush");
    controller.setShortcut("K", {"Ctrl", "Shift"});
    check(controller.selectedKey() == "K", "selected shortcut updated");
    check(controller.selectedModifiers() == QStringList{"Ctrl", "Shift"}, "typed modifiers retained");
    check(controller.dirty() && controller.canUndo(), "editing creates reversible draft");
    controller.undo();
    check(controller.draft() == initial && !controller.dirty() && controller.canRedo(), "undo restores full draft");
    controller.redo();
    check(controller.selectedKey() == "K" && controller.dirty(), "redo restores changed shortcut");
    controller.selectNode("eraser");
    controller.removeSelected();
    check(!controller.draft().rootSlots[1], "removing a tool preserves its empty slot");
    check(controller.draft().rootSlots[2] == initial.rootSlots[2], "removal does not shift later slots");
    check(controller.draft().nodes.size() + 1 == initial.nodes.size(), "removing leaf affects only that leaf");
    controller.undo();
    check(controller.draft().rootSlots[1] == initial.rootSlots[1], "undo restores removed tool in same slot");
    controller.selectNode("brush");
    controller.renameSelected("Test brush");
    const auto edited = controller.draft();
    const auto savePath = temporaryPath + "/artist.radial.json";
    controller.saveFile(QUrl::fromLocalFile(savePath));
    check(controller.error().isEmpty() && QFile::exists(savePath), "valid draft saves atomically");
    check(controller.dirty(), "exporting does not apply draft");
    controller.applyDraft();
    check(controller.error().isEmpty() && !controller.dirty(), ("apply publishes valid draft: " + controller.error()).toStdString().c_str());
    check(button(*controller.runtime(), "brush")["label"] == "Test brush", "runtime receives applied revision");
    {
        StudioController restored(nullptr, temporaryPath + "/config");
        restored.setSoundEnabled(false);
        check(restored.draft() == edited && !restored.dirty(), "applied revision reloads on new controller");
    }
    controller.renameSelected("Unsaved brush");
    controller.loadFile(QUrl::fromLocalFile(savePath));
    check(controller.draft() == edited && controller.dirty() && controller.hasPendingSettings(), "load restores saved menu and stages its settings as a draft");
    controller.undo();
    check(controller.selectedLabel() == "Unsaved brush", "opening profile is undoable");
    controller.redo();
    controller.applyDraft();

    const auto beforeInvalid = controller.draft();
    const auto badPath = temporaryPath + "/invalid.radial.json";
    auto badJson = QJsonDocument::fromJson(encodeProfile(initial)).object(); badJson["schemaVersion"] = 99;
    writeBytes(badPath, QJsonDocument(badJson).toJson());
    controller.loadFile(QUrl::fromLocalFile(badPath));
    check(!controller.error().isEmpty() && controller.draft() == beforeInvalid, "invalid load preserves current draft");
    controller.selectNode("brush");
    controller.setShortcut("NotAKey", {"Ctrl"});
    controller.applyDraft();
    check(!controller.error().isEmpty() && controller.dirty(), "invalid shortcut cannot apply");
    check(button(*controller.runtime(), "brush")["label"] == "Test brush", "failed apply retains runtime revision");
    controller.undo();

    controller.selectNode("brush");
    controller.setKind("group");
    check(radial::validate(controller.draft()).empty() && controller.draft().nodes.at("brush").children[0].has_value(), "making a parent preserves the original action as a valid child");
    controller.undo();
    check(controller.draft() == edited, "undo incomplete group restores original command");

    controller.selectNode("brush");
    controller.renameSelected("Recoverable change");
    controller.discardDraft();
    check(!controller.dirty() && controller.draft() == edited, "discard restores applied revision");
    controller.undo();
    check(controller.selectedLabel() == "Recoverable change", "discard itself is undoable");
}

void testDocumentControllerTests(const QString& temporaryPath) {
    StudioController controller(nullptr, temporaryPath + "/config");
    controller.setSoundTheme("tiny-gears");
    controller.setSoundEnabled(true);controller.setReducedMotion(true);controller.setBackdropEnabled(false);controller.preview()->setDurationMs(1111);
    const auto appliedLabel=button(*controller.runtime(),"brush")["label"];
    controller.selectNode("brush");controller.renameSelected("Saved test brush");
    const auto expectedProfile=controller.draft();
    const auto target=QUrl::fromLocalFile(temporaryPath+"/Test.radial.json");
    check(controller.suggestedTestFile().fileName()=="Test.radial.json","native Save As proposes requested test filename");
    controller.saveTestFile(target);
    check(controller.error().isEmpty()&&controller.currentFile()==target&&controller.dirty(),"test save sets current file but leaves changes unapplied");
    check(button(*controller.runtime(),"brush")["label"]==appliedLabel,"test save does not publish or execute actions");
    QFile saved(target.toLocalFile());check(saved.open(QIODevice::ReadOnly),"saved test document exists");
    const auto result=decodeRadialDocument(saved.readAll());saved.close();
    check(result.profile&&*result.profile==expectedProfile&&result.settings==RadialSettings{true,true,1111,false,"tiny-gears"},"saved file contains edited menu plus current settings");

    controller.setSoundEnabled(false);controller.setSoundTheme("felt-click");controller.setReducedMotion(false);controller.setBackdropEnabled(true);controller.preview()->setDurationMs(1640);
    controller.renameSelected("Recover before open");controller.loadFile(target);
    check(controller.error().isEmpty()&&controller.hasPendingSettings()&&controller.draft()==expectedProfile,"opening stages test document");
    check(controller.soundTheme()=="felt-click","opening a document leaves its sound style pending until Apply");
    check(!controller.soundEnabled()&&!controller.reducedMotion()&&controller.backdropEnabled()&&controller.preview()->durationMs()==1640,
          "opening test document leaves live preferences untouched until Apply");
    check(controller.runtime()->durationMs()==1640&&button(*controller.runtime(),"brush")["label"]==appliedLabel,
          "opening test file leaves runtime profile and timing untouched");
    controller.undo();
    check(!controller.hasPendingSettings()&&controller.selectedLabel()=="Recover before open","undo restores draft before opening including pending settings state");
    controller.redo();check(controller.hasPendingSettings()&&controller.draft()==expectedProfile,"redo restores imported settings alongside menu");
    const auto copied=QUrl::fromLocalFile(temporaryPath+"/Test-copy.radial.json");controller.saveTestFile(copied);
    QFile copy(copied.toLocalFile());check(copy.open(QIODevice::ReadOnly),"test copy opens");const auto copiedResult=decodeRadialDocument(copy.readAll());copy.close();
    check(copiedResult.settings==result.settings,"saving pending document preserves imported settings without applying them");
    const auto failedPath=QUrl::fromLocalFile(temporaryPath);
    controller.saveTestFile(failedPath);
    check(!controller.error().isEmpty()&&controller.currentFile()==copied,"failed save does not switch current file");
    const auto invalid=QUrl::fromLocalFile(temporaryPath+"/invalid-settings.radial.json");
    auto bad=QJsonDocument::fromJson(encodeTestDocument(expectedProfile,*result.settings)).object();bad["settings"]=QJsonObject{{"soundEnabled",true}};
    writeBytes(invalid.toLocalFile(),QJsonDocument(bad).toJson());controller.loadFile(invalid);
    check(!controller.error().isEmpty()&&controller.draft()==expectedProfile&&controller.hasPendingSettings()&&controller.currentFile()==copied,
          "invalid settings reject entire load without changing draft, imported settings or file");
    controller.selectNode("brush");controller.setShortcut("NotAKey",{});controller.applyDraft();
    check(!controller.error().isEmpty()&&controller.hasPendingSettings()&&!controller.soundEnabled(),"failed Apply cannot partially publish imported settings");
    controller.undo();controller.discardDraft();
    check(!controller.dirty()&&!controller.hasPendingSettings()&&!controller.soundEnabled(),"discard cancels imported preferences without applying them");
    controller.undo();check(controller.hasPendingSettings()&&controller.draft()==expectedProfile,"discard remains undoable for both menu and settings");
    controller.applyDraft();
    check(controller.soundTheme()=="tiny-gears","Apply restores imported sound style");
    check(controller.error().isEmpty()&&!controller.dirty()&&!controller.hasPendingSettings(),"Apply publishes complete test document");
    check(controller.soundEnabled()&&controller.reducedMotion()&&!controller.backdropEnabled()&&controller.preview()->durationMs()==1111&&controller.runtime()->durationMs()==1111,
          "Apply activates all imported appearance preferences");
    check(button(*controller.runtime(),"brush")["label"]=="Saved test brush","Apply publishes imported menu to runtime");
    controller.selectNode("brush");controller.renameSelected("Save current edit");controller.saveCurrentFile();
    QFile current(copied.toLocalFile());check(current.open(QIODevice::ReadOnly),"current test file opens");auto currentResult=decodeRadialDocument(current.readAll());current.close();
    check(currentResult.profile&&currentResult.profile->nodes.at("brush").label=="Save current edit"&&controller.dirty(),"Save current updates chosen file without applying latest edit");
    controller.saveTestFile(QUrl::fromLocalFile(temporaryPath+"/config/applied.radial.json"));
    check(!controller.error().isEmpty()&&controller.currentFile()==copied,"test save cannot replace private applied profile");
    StudioController restored(nullptr, temporaryPath + "/config");
    check(restored.draft()==expectedProfile&&restored.soundEnabled()&&restored.reducedMotion()&&!restored.backdropEnabled()&&restored.preview()->durationMs()==1111,
          "restart restores only applied menu and its imported settings");
    restored.setSoundEnabled(false);
}

void sessionTests() {
    MenuSession session(true, nullptr);
    session.setProfile(radial::legacyDefaultProfile());
    session.setViewport(1100, 1100);
    session.setReducedMotion(true);
    session.toggleDock();
    check(session.docked() && session.dockProgress() == 1, "reduced motion docks synchronously");
    int reports = 0;
    QString lastReport;
    QObject::connect(&session, &MenuSession::actionReported, [&reports, &lastReport](const QString& report) {
        ++reports; lastReport = report;
    });
    auto text = button(session, "text");
    const double x = text["dockX"].toDouble(), y = text["dockY"].toDouble();
    session.pointerPressed(77, x, y);
    session.pointerMoved(77, x + 150, y);
    check(session.active() && session.childButtons().size() == 6 && !session.parentMenuOpen(), "coalesced outward move opens fan children");
    check(session.selectedId().isEmpty(), "opening contact stays neutral");
    session.pointerReleased(77, x + 150, y);
    check(reports == 0 && !session.active(), "neutral fan release closes without an action");
    session.cancel();

    session.pointerPressed(77, x, y);
    session.pointerMoved(77, x + 42, y);
    const auto bold = button(session, "text.bold", true);
    session.pointerMoved(88, bold["cx"].toDouble(), bold["cy"].toDouble());
    check(session.selectedId().isEmpty(), "foreign pointer cannot select a child");
    session.pointerReleased(88, bold["cx"].toDouble(), bold["cy"].toDouble());
    check(session.active() && reports == 0, "foreign release cannot terminate owner");
    session.pointerReleased(77, bold["cx"].toDouble(), bold["cy"].toDouble());
    check(reports == 1 && lastReport == "Preview: Ctrl+B", "coalesced final selection commits exactly once in preview");
    session.pointerReleased(77, bold["cx"].toDouble(), bold["cy"].toDouble());
    check(reports == 1, "duplicate session release ignored");

    session.pointerPressed(77, x, y);
    session.pointerMoved(77, x + 42, y);
    auto changed = radial::legacyDefaultProfile();
    changed.nodes.at("text.bold").action.key = "Q";
    changed.nodes.at("text.bold").label = "Updated bold";
    session.setProfile(changed);
    const auto oldBold = button(session, "text.bold", true);
    check(oldBold["label"] == "Bold", "active gesture renders frozen profile metadata");
    session.pointerReleased(77, oldBold["cx"].toDouble(), oldBold["cy"].toDouble());
    check(lastReport == "Preview: Ctrl+B" && reports == 2, "active gesture uses original command revision");
    session.pointerPressed(77, x, y); session.pointerMoved(77, x + 42, y);
    const auto newBold = button(session, "text.bold", true);
    check(newBold["label"] == "Updated bold", "pending revision arrives on next gesture");
    session.pointerReleased(77, newBold["cx"].toDouble(), newBold["cy"].toDouble());
    check(lastReport == "Preview: Ctrl+Q" && reports == 3, "next gesture commits updated command");
    session.pointerPressed(77, x, y); session.pointerMoved(77, x + 42, y);
    session.setViewport(1200, 1100);
    check(!session.active(), "viewport change cancels frozen geometry safely");
    session.pointerReleased(77, newBold["cx"].toDouble(), newBold["cy"].toDouble());
    check(reports == 3, "resize cancellation cannot later commit");
    session.setViewport(150, 1100);
    const auto narrowText = button(session, "text");
    const double nx = narrowText["dockX"].toDouble(), ny = narrowText["dockY"].toDouble();
    session.pointerPressed(77, nx, ny);
    const double radians = -15.0 * std::numbers::pi / 180.0;
    session.pointerReleased(77, nx + 42 + radial::GestureEngine::ChildRadius * std::cos(radians), ny + radial::GestureEngine::ChildRadius * std::sin(radians));
    check(!session.active() && reports == 3, "coalesced release cancels a branch that cannot fit viewport");
}

void invocationGeometryTests() {
    MenuSession session(true, nullptr);
    session.setProfile(radial::legacyDefaultProfile());
    session.setViewport(1200, 900);
    check(session.radialCenter() == QPointF(600,450), "preview defaults to viewport center");
    session.setViewport(1400, 1000);
    check(session.radialCenter() == QPointF(700,500), "unanchored preview follows viewport resize");
    session.setRadialCenter(330,380);
    const auto brush = button(session,"brush");
    const auto originalCenter = session.radialCenter();
    int reports = 0;
    QObject::connect(&session,&MenuSession::actionReported,[&](const QString&){++reports;});
    session.pointerPressed(1,700,430);
    check(!session.active(), "old frame center has no stale floating hit target");
    session.pointerPressed(1,brush["cx"].toDouble(),brush["cy"].toDouble());
    check(session.active(), "floating action hit target follows explicit invocation anchor");
    session.pointerReleased(1,brush["cx"].toDouble(),brush["cy"].toDouble());
    check(reports == 1 && session.status() == "Preview: B", "anchored floating leaf commits original action");
    session.setReducedMotion(true);
    session.toggleDock();
    check(session.docked() && session.radialCenter() == originalCenter && button(session,"brush")["cx"] == brush["cx"] && button(session,"brush")["cy"] == brush["cy"], "docking retains floating source anchor without frame-centering drift");
    const auto dockBrush = button(session,"brush");
    check(dockBrush["dockX"].toDouble() == 35, "rail stays at viewport left edge independently of invocation center");
    session.pointerPressed(2,dockBrush["dockX"].toDouble(),dockBrush["dockY"].toDouble());
    session.pointerReleased(2,dockBrush["dockX"].toDouble(),dockBrush["dockY"].toDouble());
    check(reports == 2, "edge dock accepts actions after anchored transition");
    session.toggleDock();
    check(session.radialCenter() == originalCenter, "undocking returns to same floating anchor");
    session.pointerPressed(3,brush["cx"].toDouble(),brush["cy"].toDouble());
    session.setRadialCenter(420,380);
    session.pointerReleased(3,brush["cx"].toDouble(),brush["cy"].toDouble());
    check(!session.active() && reports == 2, "moving invocation anchor cancels gesture without stale commit");
    session.setRadialCenter(-200,5000);
    check(session.radialCenter() == QPointF(257,743), "invocation near edge leaves room for first-level fan");
    auto text = button(session,"text");
    const auto radians = text["angle"].toDouble();
    const auto tx=text["cx"].toDouble(),ty=text["cy"].toDouble();
    session.pointerPressed(4,tx,ty);
    session.pointerMoved(4,tx+43*std::cos(radians),ty+43*std::sin(radians));
    check(session.parentInPlace() && session.parentButtons().size() == 6 && session.parentCenter()==session.radialCenter(), "edge-clamped invocation replaces the Text ring in place");
    session.setViewport(300,240);
    check(!session.active() && session.radialCenter() == QPointF(150,120), "small viewport clamps anchor and cancels active fan");
    session.setViewport(std::numeric_limits<qreal>::quiet_NaN(),900);
    session.setRadialCenter(std::numeric_limits<qreal>::infinity(),500);
    check(session.radialCenter() == QPointF(150,120), "invalid geometry cannot poison anchor or hit targets");
}

void compactDockFanTests() {
    MenuSession session(true,nullptr);
    auto profile = radial::legacyDefaultProfile();
    // Exercise every row with the same populated group, including the extreme
    // top/bottom positions that previously clipped or canceled their children.
    profile.rootSlots.fill(std::nullopt);
    session.setViewport(680,500);
    session.setReducedMotion(true);
    session.toggleDock();
    int reports=0;
    QObject::connect(&session,&MenuSession::actionReported,[&](const QString&){++reports;});
    for (int slot=0;slot<6;++slot) {
        profile.rootSlots.fill(std::nullopt);
        profile.rootSlots[slot]="text";
        session.setProfile(profile);
        const auto root=button(session,"text");
        const auto x=root["dockX"].toDouble(), y=root["dockY"].toDouble();
        session.pointerPressed(90,x,y);
        session.pointerMoved(90,x+42,y);
        check(session.active() && session.childButtons().size()==6,"all dock rows open six fan children in compact viewport");
        const auto before=session.childButtons();
        for(const auto& value:before){
            const auto child=value.toMap();
            const auto bounds=radial::GestureEngine::childBounds({child["fromX"].toDouble(),child["fromY"].toDouble()},child["axis"].toDouble(),radial::GestureEngine::ChildVisualOvershoot);
            check(bounds.left>=4 && bounds.right<=676 && bounds.top>=4 && bounds.bottom<=496,"every animated fan wedge stays inside the compact viewport");
        }
        const auto bold=button(session,"text.bold",true);
        session.pointerMoved(90,bold["cx"].toDouble(),bold["cy"].toDouble());
        check(button(session,"text.bold",true)["cx"]==bold["cx"]&&button(session,"text.bold",true)["cy"]==bold["cy"],"opened fan stays fixed while selecting a child");
        session.pointerReleased(90,bold["cx"].toDouble(),bold["cy"].toDouble());
        check(!session.active() && reports==slot+1,"visible compact dock button commits once");
    }
}

void modifierTests(const QString& directory) {
    const auto profile=radial::defaultProfile();
    check(radial::validate(profile).empty(),"new default has valid ownership and depth");
    for(const auto* key:{"Alt","Ctrl","Shift"}){
        const auto id=std::string("modifier.")+key;
        check(std::find(profile.rootSlots.begin(),profile.rootSlots.end(),id)!=profile.rootSlots.end(),"modifier belongs to main default menu");
        check(profile.nodes.at(id).label==key,"default modifier has explicit key label");
    }
    auto invalid=profile;invalid.nodes.at("modifier.Alt").action.key="Meta";
    check(!radial::validate(invalid).empty(),"unsupported held key rejected");
    MenuSession session(true,nullptr);session.setProfile(profile);session.setReducedMotion(true);session.toggleDock();
    const auto alt=button(session,"modifier.Alt");const double x=alt["dockX"].toDouble(),y=alt["dockY"].toDouble();
    const auto press=[&]{session.pointerPressed(11,x,y);};
    const auto lift=[&]{session.pointerReleased(11,x,y);};
    const auto state=[&]{return button(session,"modifier.Alt")["modifierState"].toString();};
    press();check(session.heldModifiers().contains("Alt")&&state()=="held","press immediately holds modifier");
    session.pointerReleased(99,x,y);check(session.active(),"another pointer cannot release modifier");
    lift();check(session.heldModifiers().isEmpty()&&!session.active(),"single lift releases modifier");
    press();lift();check(session.heldModifiers().contains("Alt")&&state()=="latched"&&!session.active(),"double tap latches after lift");
    press();check(session.heldModifiers().isEmpty(),"next press releases latch immediately");lift();
    press();session.pointerMoved(11,x+90,y);check(state()=="hover"&&session.heldModifiers().contains("Alt"),"drag-out locks the modifier until the pen leaves hover");
    session.pointerReleased(11,x+90,y);check(session.heldModifiers().contains("Alt")&&state()=="hover","drag lock survives the selecting contact lift");
    session.finishNextStroke();check(session.heldModifiers().contains("Alt"),"pen-tip up does not end a hover lock");
    session.finishPenHover();check(session.heldModifiers().isEmpty()&&state().isEmpty(),"leaving hover releases the dragged modifier");
    press();session.pointerMoved(11,x+90,y);session.finishPenHover();session.pointerReleased(11,x+90,y);
    check(session.heldModifiers().isEmpty(),"leaving hover before the selecting finger lifts cannot rearm the modifier");
    press();lift();press();lift();
    const auto ctrl=button(session,"modifier.Ctrl");const double cx=ctrl["dockX"].toDouble(),cy=ctrl["dockY"].toDouble();
    session.pointerPressed(12,cx,cy);check(session.heldModifiers().size()==2,"latched modifier can combine with another held modifier");
    session.pointerReleased(12,cx,cy);check(session.heldModifiers()==QStringList{"Alt"},"lifting second modifier preserves latched first key");
    session.cancel();check(session.heldModifiers().isEmpty(),"cancel releases latched keys");
    press();session.setViewport(720,600);check(session.heldModifiers().isEmpty(),"viewport interruption releases held key");
    session.cancel();
    const auto tools=button(session,"tools");const double tx=tools["dockX"].toDouble(),ty=tools["dockY"].toDouble();
    session.pointerPressed(13,tx,ty);session.pointerMoved(13,tx+42,ty);
    check(session.childButtons().size()==4,"new default Tools opens retained groups");
    const auto text=button(session,"text",true);const double ax=text["axis"].toDouble();
    session.pointerMoved(13,text["cx"].toDouble(),text["cy"].toDouble());
    session.pointerMoved(13,text["cx"].toDouble()+42*std::cos(ax),text["cy"].toDouble()+42*std::sin(ax));
    check(session.childButtons().size()==6,"new default retains nested Text commands");
    session.cancel();
    session.toggleDock();
    const auto tapKey=[&](const char* key){const auto b=button(session,QString("modifier.")+key);const auto px=b["cx"].toDouble(),py=b["cy"].toDouble();session.pointerPressed(51,px,py);session.pointerReleased(51,px,py);};
    tapKey("Alt");
    check(session.heldModifiers()==QStringList{"Alt"}&&state()=="armed","floating tap arms modifier after selection lift");
    tapKey("Alt");check(session.heldModifiers().isEmpty(),"second floating tap cancels instead of latching indefinitely");
    tapKey("Alt");tapKey("Ctrl");tapKey("Shift");
    check(session.heldModifiers().size()==3,"floating modifiers can combine for one stroke");
    session.finishNextStroke();check(session.heldModifiers().isEmpty()&&!session.active(),"pen-up ends all next-stroke modifiers");
    const auto floatingAlt=button(session,"modifier.Alt");const auto fx=floatingAlt["cx"].toDouble(),fy=floatingAlt["cy"].toDouble();
    session.pointerPressed(52,fx,fy);session.pointerMoved(52,fx+60,fy);session.pointerReleased(52,fx+60,fy);
    tapKey("Ctrl");session.finishNextStroke();check(session.heldModifiers()==QStringList{"Alt"},"one-stroke release preserves a separate hover-locked modifier");
    session.finishPenHover();check(session.heldModifiers().isEmpty(),"floating drag lock releases on hover exit");
    tapKey("Alt");session.cancel();check(session.heldModifiers().isEmpty(),"hide and cancellation release armed modifiers");
    tapKey("Ctrl");session.toggleDock();check(session.heldModifiers().isEmpty(),"docking releases next-stroke modifiers");
    const auto legacy=radial::legacyDefaultProfile();
    QDir().mkpath(directory+"/legacy");writeBytes(directory+"/legacy/applied.radial.json",encodeProfile(legacy));
    StudioController upgraded(nullptr,directory+"/legacy",true);
    check(upgraded.draft()==profile&&!upgraded.dirty(),"untouched old defaults migrate to new defaults");
    auto custom=legacy;custom.nodes.at("brush").label="My brush";
    QDir().mkpath(directory+"/custom");writeBytes(directory+"/custom/applied.radial.json",encodeProfile(custom));
    StudioController preserved(nullptr,directory+"/custom",true);
    check(preserved.draft()==custom,"customized menus are preserved");
    preserved.useDefaultMenu();check(preserved.draft()==profile&&preserved.dirty(),"new defaults can be loaded as a reviewable draft");
    preserved.undo();check(preserved.draft()==custom,"default replacement is undoable");
    preserved.selectNode("brush");preserved.setKind("modifier");preserved.setModifierKey("Shift");
    check(preserved.selectedKind()=="modifier"&&preserved.selectedKey()=="Shift"&&preserved.selectedLabel()=="Shift","editor creates labeled modifier action");
    const auto decoded=decodeProfile(encodeProfile(preserved.draft()));
    check(decoded.profile&&*decoded.profile==preserved.draft(),"configured modifier persists through save and load");
}
void queuedSessionTests() {
#ifndef Q_OS_WIN
    WindowPlatform platform;
    MenuSession session(false,&platform);
    session.setProfile(radial::defaultProfile());
    session.setReducedMotion(true);
    session.toggleDock();
    int reports=0, deliveries=0;
    QObject::connect(&session,&MenuSession::actionReported,[&](const QString&){++reports;});
    QObject::connect(&platform,&WindowPlatform::shortcutSimulated,[&](const QString&,const QStringList&){++deliveries;});
    const auto brush=button(session,"brush");
    const auto x=brush["dockX"].toDouble(),y=brush["dockY"].toDouble();
    auto tap=[&] {session.pointerPressed(1,x,y);session.pointerReleased(1,x,y);};
    auto drain=[] {QEventLoop loop;QTimer::singleShot(70,&loop,&QEventLoop::quit);loop.exec();};
    tap();
    check(session.active() && platform.hasQueuedAction() && reports==0 && deliveries==0,
          "runtime remains busy while accepted command waits for release");
    session.toggleDock();
    check(session.docked(),"pending command prevents geometry changes");
    tap();
    drain();
    check(!session.active() && !platform.hasQueuedAction() && reports==1 && deliveries==1 && session.status()=="Preview: B",
          "queued session delivers and reports only once after release gate");
    tap();
    session.cancel();
    drain();
    check(!session.active() && reports==1 && deliveries==1,"cancel discards queued session without late action");
    tap();
    auto updated=radial::defaultProfile();updated.nodes.at("brush").label="Next revision";
    session.setProfile(updated);
    check(button(session,"brush")["label"]=="Brush","pending command retains its profile revision");
    drain();
    check(reports==2 && deliveries==2 && button(session,"brush")["label"]=="Next revision",
          "queued completion applies pending revision after its one delivery");
    tap();
    session.setViewport(680,600);
    drain();
    check(!session.active() && reports==2 && deliveries==2,"viewport cancellation cannot replay pending input");
#endif
}
}

int main(int argc, char** argv) {
    QTemporaryDir temporary;
    if (!temporary.isValid()) return EXIT_FAILURE;
    qputenv("XDG_CONFIG_HOME", (temporary.path() + "/config").toUtf8());
    qputenv("XDG_DATA_HOME", (temporary.path() + "/data").toUtf8());
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName("TabletProTests");
    QCoreApplication::setApplicationName("radial-app-tests-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path() + "/settings");
    int result = EXIT_SUCCESS;
    try {
        soundTests(temporary.path());modeTransitionTests();
        inPlaceParentTests();
        parentMenuTests(temporary.path()); reparentTests(temporary.path()); modifierTests(temporary.path()); codecTests(); testDocumentCodecTests(); controllerTests(temporary.path()); testDocumentControllerTests(temporary.path()); sessionTests(); invocationGeometryTests(); compactDockFanTests(); queuedSessionTests();
        std::cout << "PASS: " << checks << " application integration checks\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        result = EXIT_FAILURE;
    }
    return result;
}
