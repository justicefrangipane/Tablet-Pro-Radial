#include "AudioFeedback.h"
#include <QUrl>

// Keep embedded sounds linked into executables using the static library.
static void initializeSounds() { Q_INIT_RESOURCE(radial_sounds); }

namespace tabletpro {
AudioFeedback::AudioFeedback(QObject* parent):QObject(parent) {
    initializeSounds();setObjectName("audioFeedback");limiter_.start();
    for (const auto& theme : soundThemeIds()) for (const auto& cue : {"press", "open", "close", "modifier-lock"}) {
        auto* effect=new QSoundEffect(this);
        const auto key=theme+"/"+cue;
        effect->setObjectName(key);
        effect->setSource(QUrl("qrc:/sounds/"+theme+"-"+cue+".wav"));
        effect->setVolume(.45f);effect->setLoopCount(1);effects_[key]=effect;
    }
}
void AudioFeedback::stop() { for(auto* effect:effects_)if(effect->isPlaying())effect->stop(); }
void AudioFeedback::setEnabled(bool value) { enabled_=value;if(!value)stop(); }
void AudioFeedback::setTheme(const QString& theme) {
    if(!soundThemeIds().contains(theme)||theme_==theme)return;
    stop();theme_=theme;
}
void AudioFeedback::preview(const QString& cue) { playCue(cue,true); }
void AudioFeedback::play(const QString& cue) { playCue(cue,false); }
void AudioFeedback::playCue(const QString& cue,bool preview) {
    if(!enabled_&&!preview)return;
    QString sample=cue;
    if(cue=="tick"||cue=="commit"||cue=="crackle")sample="press";
    else if(cue=="back")sample="close";
    else if(cue=="snap")sample="open";
    auto* effect=effects_.value(theme_+"/"+sample,nullptr);
    if(!effect||effect->status()!=QSoundEffect::Ready)return;
    if(cue=="tick"&&limiter_.elapsed()<65)return;
    const auto* lock=effects_.value(theme_+"/modifier-lock",nullptr);
    if(cue=="tick"&&lock&&lock->isPlaying())return;
    limiter_.restart();
    // One brief voice; hover ticks are quieter than a deliberate press.
    stop();effect->setVolume(cue=="tick"?.24f:.45f);effect->play();
    emit cuePlayed(theme_,sample,preview);
}
}
