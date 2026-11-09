#include "StudioController.h"
#include "ProfileCodec.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QSet>
#include <algorithm>

namespace tabletpro {
namespace {
QString q(const std::string& value) { return QString::fromStdString(value); }
std::string newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(); }
}
StudioController::StudioController(QObject* parent,const QString& configDirectory,bool runtimePreview):QObject(parent),configDirectory_(configDirectory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation):configDirectory),platform_(this),stylus_(this),preview_(true,&platform_,this),runtime_(runtimePreview,&platform_,this),audio_(this),draft_(radial::defaultProfile()),applied_(draft_) {
    QFile previous(appliedPath());
    if(previous.open(QIODevice::ReadOnly)) { const auto result=decodeProfile(previous.readAll());if(result.profile)draft_=applied_=*result.profile;else error_="Saved profile could not be loaded: "+result.error; }
    if(draft_==radial::legacyDefaultProfile()) draft_=applied_=radial::defaultProfile();
    for(const auto& slot:draft_.rootSlots)if(slot){selected_=q(*slot);break;}
    QSettings settings;setSoundEnabled(settings.value("sound",false).toBool());setReducedMotion(settings.value("reducedMotion",false).toBool());
    setBackdropEnabled(settings.value("backdrop",true).toBool());
    preview_.setProfile(draft_);runtime_.setProfile(applied_);
    const int duration=settings.value("durationMs",1640).toInt();preview_.setDurationMs(duration);runtime_.setDurationMs(duration);
    connect(&preview_,&MenuSession::preferencesChanged,this,[this]{runtime_.setDurationMs(preview_.durationMs());QSettings().setValue("durationMs",preview_.durationMs());});
    for(auto* session:{&preview_,&runtime_}) { connect(session,&MenuSession::cue,&audio_,&AudioFeedback::play);connect(session,&MenuSession::actionReported,this,[this](const QString& result){status_=result;emit changed();}); }
    connect(&platform_,&WindowPlatform::toggleRequested,this,[this](quintptr target){runtime_.releaseModifiers();emit showRuntimeRequested(target,true);});
    connect(&platform_,&WindowPlatform::lastErrorChanged,this,&StudioController::changed);
    stylus_.setSelectedSideButton(settings.value("penSideButton",1).toInt());
    stylus_.setEnabled(settings.value("penInputEnabled",true).toBool());
    connect(&stylus_,&StylusInput::settingsChanged,this,[this]{
        QSettings prefs;prefs.setValue("penInputEnabled",stylus_.enabled());prefs.setValue("penSideButton",stylus_.selectedSideButton());
    });
    connect(&stylus_,&StylusInput::sideButtonPressed,this,[this]{runtime_.releaseModifiers();emit showRuntimeRequested(platform_.captureForegroundTarget(),true);});
    connect(&stylus_,&StylusInput::tipChanged,&platform_,&WindowPlatform::observeStylusTip);
    connect(&stylus_,&StylusInput::proximityLeft,&platform_,&WindowPlatform::finishPenHover);
    connect(&stylus_,&StylusInput::inputLost,&runtime_,&MenuSession::releaseModifiers);
}
const radial::Node* StudioController::selectedNode() const {const auto i=draft_.nodes.find(selected_.toStdString());return i==draft_.nodes.end()?nullptr:&i->second;}
radial::Node* StudioController::selectedNode() {const auto i=draft_.nodes.find(selected_.toStdString());return i==draft_.nodes.end()?nullptr:&i->second;}
QString StudioController::selectedLabel() const {auto*n=selectedNode();return n?q(n->label):QString{};}
QString StudioController::selectedIcon() const {auto*n=selectedNode();return n?q(n->icon):QString{};}
QString StudioController::selectedKind() const {auto*n=selectedNode();return !n?QString{}:n->kind==radial::NodeKind::Group?"group":n->action.kind==radial::ActionKind::Shortcut?"shortcut":n->action.kind==radial::ActionKind::Text?"text":n->action.kind==radial::ActionKind::Modifier?"modifier":"empty";}
std::string StudioController::parentOf(const std::string& id) const {
    for(const auto& [key,node]:draft_.nodes) for(const auto& child:node.children)
        if(child && *child==id)return key;
    return {};
}
QString StudioController::selectedParentId() const {return q(parentOf(selected_.toStdString()));}
std::string StudioController::insertionParent() const {
    const auto* node=selectedNode();
    return node && node->kind==radial::NodeKind::Group ? node->id : parentOf(selected_.toStdString());
}
const radial::SlotArray& StudioController::insertionSlots() const {
    const auto id=insertionParent();return id.empty()?draft_.rootSlots:draft_.nodes.at(id).children;
}
int StudioController::groupDepth(const std::string& id) const {
    int depth=0;auto current=id;QSet<QString> visited;
    while(!current.empty() && draft_.nodes.contains(current) && !visited.contains(q(current))){
        visited.insert(q(current));if(draft_.nodes.at(current).kind==radial::NodeKind::Group)++depth;
        current=parentOf(current);
    }
    return depth;
}
QString StudioController::insertionLabel() const {
    const auto id=insertionParent();return id.empty()?"Main menu":q(draft_.nodes.at(id).label);
}
QVariantList StudioController::childSlots() const {
    QVariantList result;const auto& entries=insertionSlots();
    for(int i=0;i<6;++i){
        const auto node=entries[i]?draft_.nodes.find(*entries[i]):draft_.nodes.end();
        result.append(QVariantMap{{"slot",i},{"empty",node==draft_.nodes.end()},
            {"id",node==draft_.nodes.end()?QString{}:q(node->second.id)},
            {"label",node==draft_.nodes.end()?"Empty":q(node->second.label)},
            {"parent",node!=draft_.nodes.end()&&node->second.kind==radial::NodeKind::Group}});
    }
    return result;
}
bool StudioController::canAddButton() const {const auto& entries=insertionSlots();return std::any_of(entries.begin(),entries.end(),[](const auto& slot){return !slot;});}
bool StudioController::canAddParent() const {return canAddButton() && groupDepth(insertionParent())<int(radial::MaximumGroupDepth);}
bool StudioController::canMakeParent() const {return selectedNode() && selectedNode()->kind!=radial::NodeKind::Group && groupDepth(selected_.toStdString())<int(radial::MaximumGroupDepth);}
void StudioController::makeParent(){
    if(!canMakeParent())return;
    edit([&]{
        auto* node=selectedNode();auto child=*node;const bool keep=child.action.kind!=radial::ActionKind::None;
        child.id=newId();node->kind=radial::NodeKind::Group;node->action={};node->children={};
        if(keep){node->children[0]=child.id;draft_.nodes.emplace(child.id,std::move(child));}
    });
}
QString StudioController::selectedKey() const {auto*n=selectedNode();return n?q(n->action.key):QString{};}
QString StudioController::reparentProblem(const QString& buttonId,const QString& parentId) const {
    const auto source=buttonId.toStdString(),destination=parentId.toStdString();
    if(!draft_.nodes.contains(source)||!draft_.nodes.contains(destination))return "Drop onto a parent button";
    if(source==destination)return "A button cannot be its own parent";
    const auto& parent=draft_.nodes.at(destination);
    if(parent.kind!=radial::NodeKind::Group)return "Choose a parent button, or use Make parent first";
    if(parentOf(source)==destination)return "Already a child of this parent";
    for(auto ancestor=destination;!ancestor.empty();ancestor=parentOf(ancestor))
        if(ancestor==source)return "A parent cannot move inside its own children";
    if(std::none_of(parent.children.begin(),parent.children.end(),[](const auto& slot){return !slot;}))return "This parent already has six children";
    std::function<int(const std::string&)> depth=[&](const std::string& id){
        const auto& node=draft_.nodes.at(id);int childDepth=0;
        for(const auto& child:node.children)if(child)childDepth=std::max(childDepth,depth(*child));
        return childDepth+(node.kind==radial::NodeKind::Group?1:0);
    };
    if(groupDepth(destination)+depth(source)>int(radial::MaximumGroupDepth))return "A menu can have up to four parent levels";
    return {};
}
bool StudioController::reparentButton(const QString& buttonId,const QString& parentId){
    const auto problem=reparentProblem(buttonId,parentId);
    if(!problem.isEmpty()){error_=problem;emit changed();return false;}
    const auto source=buttonId.toStdString(),destination=parentId.toStdString(),oldParent=parentOf(source);
    const auto label=q(draft_.nodes.at(source).label),parentLabel=q(draft_.nodes.at(destination).label);
    edit([&]{
        auto& previous=oldParent.empty()?draft_.rootSlots:draft_.nodes.at(oldParent).children;
        for(auto& slot:previous)if(slot&&*slot==source)slot.reset();
        auto& children=draft_.nodes.at(destination).children;
        *std::find(children.begin(),children.end(),std::nullopt)=source;
        selected_=buttonId;
    });
    status_=label+" moved into "+parentLabel+" · Apply draft to use it";
    if(!oldParent.empty()){
        const auto& previous=draft_.nodes.at(oldParent);
        if(std::none_of(previous.children.begin(),previous.children.end(),[](const auto& slot){return bool(slot);}))
            status_+=". "+q(previous.label)+" is empty; add a child or remove that parent before applying.";
    }
    emit changed();return true;
}
QString StudioController::selectedText() const {auto*n=selectedNode();return n?q(n->action.text):QString{};}
QStringList StudioController::selectedModifiers() const {QStringList r;auto*n=selectedNode();if(!n)return r;for(const auto&pair:{std::pair{radial::Ctrl,"Ctrl"},{radial::Shift,"Shift"},{radial::Alt,"Alt"},{radial::Meta,"Meta"}})if(n->action.modifiers&pair.first)r<<pair.second;return r;}
QVariantList StudioController::outline() const {
    QVariantList result;QSet<QString> visited;
    std::function<void(const radial::SlotArray&,int)> visit=[&](const radial::SlotArray& entries,int depth){
        for(int i=0;i<6;++i){if(!entries[i])continue;const auto id=q(*entries[i]);if(visited.contains(id))continue;visited.insert(id);auto found=draft_.nodes.find(*entries[i]);if(found==draft_.nodes.end())continue;const auto&n=found->second;
            result.append(QVariantMap{{"id",id},{"label",q(n.label)},{"kind",n.kind==radial::NodeKind::Group?"group":"action"},{"depth",depth},{"slot",i}});
            if(n.kind==radial::NodeKind::Group)visit(n.children,depth+1);
        }
    };visit(draft_.rootSlots,0);return result;
}
void StudioController::refresh(){if(!selectedNode()){selected_.clear();for(const auto&slot:draft_.rootSlots)if(slot){selected_=q(*slot);break;}}preview_.cancel();preview_.setProfile(draft_);emit changed();}
void StudioController::edit(const std::function<void()>& mutation){const Snapshot before{draft_,selected_,pendingSettings_,currentFile_};mutation();if(draft_==before.profile&&pendingSettings_==before.settings)return;undo_.push_back(before);if(undo_.size()>100)undo_.erase(undo_.begin());redo_.clear();error_.clear();status_="Draft changed · Apply publishes it to the overlay";refresh();}
void StudioController::selectNode(const QString&id){if(!draft_.nodes.contains(id.toStdString())&& !id.isEmpty())return;selected_=id;emit changed();}
void StudioController::renameSelected(const QString&label){if(!selectedNode())return;edit([&]{selectedNode()->label=label.left(80).toStdString();});}
void StudioController::setIcon(const QString&icon){const QStringList allowed{"brush","eraser","select","palette","text","settings","bold","italic","copy","paste","undo","redo","group","paintbrush","lasso-select","type","settings-2","clipboard-paste","scissors","underline","text-select","arrow-left-to-line","arrow-right-to-line","ellipsis","undo-2","redo-2","save","x","trash-2"};if(!selectedNode()||!allowed.contains(icon))return;edit([&]{selectedNode()->icon=icon.toStdString();});}
void StudioController::eraseBranch(const std::string&id){auto it=draft_.nodes.find(id);if(it==draft_.nodes.end())return;const auto children=it->second.children;draft_.nodes.erase(it);for(const auto&child:children)if(child)eraseBranch(*child);}
void StudioController::setKind(const QString&kind){if(!selectedNode()||!QStringList{"group","shortcut","text","empty","modifier"}.contains(kind))return;if(selectedKind()==kind)return;if(kind=="group"){makeParent();return;}edit([&]{auto*n=selectedNode();const auto children=n->children;for(const auto&id:children)if(id)eraseBranch(*id);n=selectedNode();n->children={};n->kind=radial::NodeKind::Action;n->action={};n->action.id=newId();n->action.kind=kind=="shortcut"?radial::ActionKind::Shortcut:kind=="text"?radial::ActionKind::Text:kind=="modifier"?radial::ActionKind::Modifier:radial::ActionKind::None;if(kind=="shortcut")n->action.key="A";if(kind=="modifier")n->action.key="Ctrl";if(kind=="text")n->action.text="Text";});}
void StudioController::setShortcut(const QString&key,const QStringList&mods){
    if(!selectedNode()||selectedKind()!="shortcut")return;
    QSet<QString> seen;
    for(const auto&m:mods){if(!QStringList{"Ctrl","Shift","Alt","Meta"}.contains(m)||seen.contains(m)){error_="Unknown or duplicate shortcut modifier: "+m;emit changed();return;}seen.insert(m);}
    edit([&]{auto&a=selectedNode()->action;a.key=key.trimmed().toStdString();a.modifiers=0;for(const auto&m:mods)a.modifiers|=m=="Ctrl"?radial::Ctrl:m=="Shift"?radial::Shift:m=="Alt"?radial::Alt:m=="Meta"?radial::Meta:0;});
}
void StudioController::setModifierKey(const QString& key){
    if(selectedKind()!="modifier" || !QStringList{"Alt","Ctrl","Shift"}.contains(key))return;
    edit([&]{auto*n=selectedNode();n->action.key=key.toStdString();n->label=n->action.key;n->icon=n->action.key;});
}
void StudioController::editButton(const QString& id){
    if(!draft_.nodes.contains(id.toStdString()))return;
    selectNode(id);showEditor();
}
void StudioController::useDefaultMenu(){edit([&]{draft_=radial::defaultProfile();selected_="brush";});}
void StudioController::setText(const QString&text){if(!selectedNode()||selectedKind()!="text")return;edit([&]{selectedNode()->action.text=text.left(1024).toStdString();});}
void StudioController::addChild(int slot,bool group){
    if(slot<0||slot>=6)return;const auto parent=insertionParent();
    if(insertionSlots()[slot]){selectNode(q(*insertionSlots()[slot]));return;}
    if(group && groupDepth(parent)>=int(radial::MaximumGroupDepth)){error_="A menu can have up to four parent levels. Add an action here instead.";emit changed();return;}
    edit([&]{
        radial::Node added;added.id=newId();added.label=group?"New parent":"New button";added.icon=group?"group":"brush";added.kind=group?radial::NodeKind::Group:radial::NodeKind::Action;
        if(!group){added.action.id=newId();added.action.kind=radial::ActionKind::Shortcut;added.action.key="B";}
        auto& entries=parent.empty()?draft_.rootSlots:draft_.nodes.at(parent).children;
        entries[slot]=added.id;selected_=q(added.id);draft_.nodes.emplace(added.id,std::move(added));
    });
}
void StudioController::addNextButton(bool parent){
    const auto& entries=insertionSlots();for(int i=0;i<6;++i)if(!entries[i]){addChild(i,parent);return;}
    error_="All six slots are filled. Select a button and choose Make parent to add children without losing its action.";emit changed();
}
void StudioController::removeSelected(){if(!selectedNode())return;edit([&]{const auto id=selected_.toStdString();for(auto&entry:draft_.rootSlots)if(entry&&*entry==id)entry.reset();for(auto&[_,n]:draft_.nodes)for(auto&entry:n.children)if(entry&&*entry==id)entry.reset();eraseBranch(id);selected_.clear();});}
void StudioController::moveSelected(int delta){if(!selectedNode()||(delta!=-1&&delta!=1))return;edit([&]{auto move=[&](radial::SlotArray& entries){for(int i=0;i<6;++i)if(entries[i]&&*entries[i]==selected_.toStdString()){const int next=i+delta;if(next>=0&&next<6)std::swap(entries[i],entries[next]);return true;}return false;};if(move(draft_.rootSlots))return;for(auto&[_,n]:draft_.nodes)if(move(n.children))return;});}
void StudioController::undo(){if(undo_.empty())return;redo_.push_back({draft_,selected_,pendingSettings_,currentFile_});auto s=std::move(undo_.back());undo_.pop_back();draft_=std::move(s.profile);selected_=s.selected;pendingSettings_=s.settings;currentFile_=s.file;error_.clear();refresh();}
void StudioController::redo(){if(redo_.empty())return;undo_.push_back({draft_,selected_,pendingSettings_,currentFile_});auto s=std::move(redo_.back());redo_.pop_back();draft_=std::move(s.profile);selected_=s.selected;pendingSettings_=s.settings;currentFile_=s.file;error_.clear();refresh();}
bool StudioController::validDraft(){const auto issues=radial::validate(draft_);if(issues.empty()){error_.clear();return true;}QStringList errors;for(std::size_t i=0;i<std::min<std::size_t>(issues.size(),3);++i)errors<<q(issues[i].path+": "+issues[i].message);error_=errors.join("\n");emit changed();return false;}
QString StudioController::appliedPath()const{return configDirectory_+"/applied.radial.json";}
bool StudioController::write(const QString&path,const QByteArray&bytes){if(path.isEmpty()){error_="Choose a local file.";emit changed();return false;}if(bytes.size()>1024*1024){error_="Radial document exceeds the 1 MiB limit.";emit changed();return false;}QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){error_="Could not save: "+file.errorString();emit changed();return false;}return true;}
void StudioController::applyDraft(){
    if(!validDraft())return;
    for(const auto&[_,node]:draft_.nodes){
        if(node.action.kind==radial::ActionKind::Text && node.action.text.find('\0')!=std::string::npos){error_="Text in "+q(node.label)+" contains a NUL character. Remove it before applying.";emit changed();return;}
        if(node.kind!=radial::NodeKind::Action||node.action.kind!=radial::ActionKind::Shortcut)continue;
        QStringList mods;
        if(node.action.modifiers&radial::Ctrl)mods<<"Ctrl";if(node.action.modifiers&radial::Shift)mods<<"Shift";
        if(node.action.modifiers&radial::Alt)mods<<"Alt";if(node.action.modifiers&radial::Meta)mods<<"Meta";
        if(!platform_.shortcutSupported(q(node.action.key),mods)){error_="Unsupported shortcut in "+q(node.label)+": "+q(node.action.key)+". Layout-dependent punctuation keys are deferred; use a text action for literal punctuation.";emit changed();return;}
    }
    if(!write(appliedPath(),encodeProfile(draft_)))return;
    applied_=draft_;runtime_.setProfile(applied_);
    if(pendingSettings_){const auto preferences=*pendingSettings_;pendingSettings_.reset();setSoundEnabled(preferences.soundEnabled);setReducedMotion(preferences.reducedMotion);setBackdropEnabled(preferences.backdropEnabled);preview_.setDurationMs(preferences.durationMs);}
    status_="Applied · the next gesture uses this revision";emit changed();
}
void StudioController::discardDraft(){if(!dirty())return;edit([&]{draft_=applied_;pendingSettings_.reset();});status_="Draft restored to the applied menu · Undo can recover it";emit changed();}
RadialSettings StudioController::currentSettings()const{return {soundEnabled_,reducedMotion_,preview_.durationMs(),backdropEnabled_};}
QUrl StudioController::suggestedTestFile()const{return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/Test.radial.json");}
void StudioController::saveFile(const QUrl&url){saveTestFile(url);}
void StudioController::saveCurrentFile(){if(currentFile_.isEmpty()){error_="Choose Save test radial… to create a file.";emit changed();return;}saveTestFile(currentFile_);}
void StudioController::saveTestFile(const QUrl&url){
    if(!url.isLocalFile()||url.toLocalFile().isEmpty()){error_="Save to a local file.";emit changed();return;}
    // A test save must never overwrite the private published revision. Only
    // Apply is allowed to change what the desktop overlay uses next.
    if(QFileInfo(url.toLocalFile()).absoluteFilePath()==QFileInfo(appliedPath()).absoluteFilePath()){
        error_="Choose a test file outside the private applied profile. Use Apply draft to publish.";emit changed();return;
    }
    if(validDraft()&&write(url.toLocalFile(),encodeTestDocument(draft_,pendingSettings_.value_or(currentSettings())))){
        currentFile_=url;status_="Saved "+QFileInfo(url.toLocalFile()).fileName()+" · menu and settings; Apply still controls the overlay";emit changed();
    }
}
void StudioController::loadFile(const QUrl&url){
    if(!url.isLocalFile()){error_="Open a local radial document.";emit changed();return;}
    QFile file(url.toLocalFile());if(!file.open(QIODevice::ReadOnly)){error_=file.errorString();emit changed();return;}
    if(file.size()>1024*1024){error_="Radial document exceeds 1 MiB.";emit changed();return;}
    const auto result=decodeRadialDocument(file.readAll());if(!result.profile){error_=result.error;emit changed();return;}
    edit([&]{draft_=*result.profile;pendingSettings_=result.settings;currentFile_=url;selected_.clear();});
    error_.clear();refresh();status_=result.settings?"Opened menu and file settings as a draft · Apply to use both":"Opened menu as a draft · current settings unchanged";emit changed();
}
void StudioController::setSoundEnabled(bool value){soundEnabled_=value;audio_.setEnabled(value);QSettings().setValue("sound",value);emit changed();}
void StudioController::setBackdropEnabled(bool value){backdropEnabled_=value;QSettings().setValue("backdrop",value);emit changed();}
void StudioController::setReducedMotion(bool value){reducedMotion_=value;preview_.setReducedMotion(value);runtime_.setReducedMotion(value);QSettings().setValue("reducedMotion",value);emit changed();}
void StudioController::showOverlay(){status_=platform_.nativeAvailable()?"Use your pen side button or Ctrl+Alt+Space from your drawing app.":"Overlay simulation · native shortcut delivery is available in the Windows build";emit showRuntimeRequested(platform_.captureForegroundTarget(),false);emit changed();}
void StudioController::hideOverlay(){runtime_.cancel();emit dismissRuntimeRequested();}
void StudioController::showEditor(){runtime_.cancel();emit hideRuntimeRequested();}
}
