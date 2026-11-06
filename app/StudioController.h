#pragma once
#include "MenuSession.h"
#include "AudioFeedback.h"
#include "ProfileCodec.h"
#include "platform/WindowPlatform.h"
#include "platform/StylusInput.h"
#include <QUrl>
#include <QStringList>
#include <functional>
#include <vector>

namespace tabletpro {
class StudioController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList outline READ outline NOTIFY changed)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY changed)
    Q_PROPERTY(QString selectedLabel READ selectedLabel NOTIFY changed)
    Q_PROPERTY(QString selectedIcon READ selectedIcon NOTIFY changed)
    Q_PROPERTY(QString selectedKind READ selectedKind NOTIFY changed)
    Q_PROPERTY(QString selectedParentId READ selectedParentId NOTIFY changed)
    Q_PROPERTY(QString insertionLabel READ insertionLabel NOTIFY changed)
    Q_PROPERTY(QVariantList childSlots READ childSlots NOTIFY changed)
    Q_PROPERTY(bool canAddButton READ canAddButton NOTIFY changed)
    Q_PROPERTY(bool canAddParent READ canAddParent NOTIFY changed)
    Q_PROPERTY(bool canMakeParent READ canMakeParent NOTIFY changed)
    Q_PROPERTY(QString selectedKey READ selectedKey NOTIFY changed)
    Q_PROPERTY(QString selectedText READ selectedText NOTIFY changed)
    Q_PROPERTY(QStringList selectedModifiers READ selectedModifiers NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool hasPendingSettings READ hasPendingSettings NOTIFY changed)
    Q_PROPERTY(QUrl currentFile READ currentFile NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(bool soundEnabled READ soundEnabled WRITE setSoundEnabled NOTIFY changed)
    Q_PROPERTY(bool backdropEnabled READ backdropEnabled WRITE setBackdropEnabled NOTIFY changed)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString platformStatus READ platformStatus NOTIFY changed)
    Q_PROPERTY(tabletpro::MenuSession* preview READ preview CONSTANT)
    Q_PROPERTY(tabletpro::MenuSession* runtime READ runtime CONSTANT)
    Q_PROPERTY(tabletpro::StylusInput* stylus READ stylus CONSTANT)
public:
    explicit StudioController(QObject* parent=nullptr, const QString& configDirectory={}, bool runtimePreview=false);
    QVariantList outline() const;
    QString selectedId() const { return selected_; }
    QString selectedLabel() const;
    QString selectedIcon() const;
    QString selectedKind() const;
    QString selectedParentId() const;
    QString insertionLabel() const;
    QVariantList childSlots() const;
    bool canAddButton() const;
    bool canAddParent() const;
    bool canMakeParent() const;
    QString selectedKey() const;
    QString selectedText() const;
    QStringList selectedModifiers() const;
    bool dirty() const { return draft_ != applied_ || pendingSettings_.has_value(); }
    bool hasPendingSettings() const { return pendingSettings_.has_value(); }
    QUrl currentFile() const { return currentFile_; }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool soundEnabled() const { return soundEnabled_; }
    bool backdropEnabled() const { return backdropEnabled_; }
    bool reducedMotion() const { return reducedMotion_; }
    QString status() const { return status_; }
    QString error() const { return error_; }
    QString platformStatus() const { return platform_.lastError(); }
    MenuSession* preview() { return &preview_; }
    MenuSession* runtime() { return &runtime_; }
    WindowPlatform* platform() { return &platform_; }
    StylusInput* stylus() { return &stylus_; }
    const radial::Profile& draft() const { return draft_; }
    void setSoundEnabled(bool value);
    void setBackdropEnabled(bool value);
    void setReducedMotion(bool value);
    Q_INVOKABLE void selectNode(const QString& id);
    Q_INVOKABLE void renameSelected(const QString& label);
    Q_INVOKABLE void setIcon(const QString& icon);
    Q_INVOKABLE void setKind(const QString& kind);
    Q_INVOKABLE void setShortcut(const QString& key,const QStringList& modifiers);
    Q_INVOKABLE void setText(const QString& text);
    Q_INVOKABLE void setModifierKey(const QString& key);
    Q_INVOKABLE void editButton(const QString& id);
    Q_INVOKABLE void useDefaultMenu();
    Q_INVOKABLE void addChild(int slot,bool group);
    Q_INVOKABLE void addNextButton(bool parent);
    Q_INVOKABLE void makeParent();
    Q_INVOKABLE QString reparentProblem(const QString& buttonId,const QString& parentId) const;
    Q_INVOKABLE bool reparentButton(const QString& buttonId,const QString& parentId);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void moveSelected(int delta);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void applyDraft();
    Q_INVOKABLE void discardDraft();
    Q_INVOKABLE void saveFile(const QUrl& file);
    Q_INVOKABLE void saveTestFile(const QUrl& file);
    Q_INVOKABLE void saveCurrentFile();
    Q_INVOKABLE QUrl suggestedTestFile() const;
    Q_INVOKABLE void loadFile(const QUrl& file);
    Q_INVOKABLE void showOverlay();
    Q_INVOKABLE void hideOverlay();
    Q_INVOKABLE void showEditor();
signals:
    void changed();
    void showRuntimeRequested(quintptr target, bool toggle);
    void hideRuntimeRequested();
    void dismissRuntimeRequested();
private:
    const QString configDirectory_;
    struct Snapshot { radial::Profile profile; QString selected; std::optional<RadialSettings> settings; QUrl file; };
    WindowPlatform platform_;
    StylusInput stylus_;
    MenuSession preview_,runtime_;
    AudioFeedback audio_;
    radial::Profile draft_,applied_;
    std::optional<RadialSettings> pendingSettings_;
    QUrl currentFile_;
    QString selected_,status_="Draft preview · actions stay inside the editor",error_;
    std::vector<Snapshot> undo_,redo_;
    bool soundEnabled_=false,reducedMotion_=false,backdropEnabled_=true;
    const radial::Node* selectedNode() const;
    radial::Node* selectedNode();
    std::string parentOf(const std::string& id) const;
    std::string insertionParent() const;
    int groupDepth(const std::string& id) const;
    const radial::SlotArray& insertionSlots() const;
    void edit(const std::function<void()>& mutation);
    void refresh();
    void eraseBranch(const std::string& id);
    bool validDraft();
    bool write(const QString& path,const QByteArray& bytes);
    RadialSettings currentSettings() const;
    QString appliedPath() const;
};
}
