#include "AudioFeedback.h"
#include <QDataStream>
#include <QFile>
#include <QUrl>
#include <cmath>
#include <numbers>

namespace tabletpro {
AudioFeedback::AudioFeedback(QObject* parent):QObject(parent) {
    limiter_.start();
    if (!directory_.isValid()) return;
    const QStringList cues{"tick","open","back","commit","snap","crackle"};
    for (const auto& cue : cues) {
        const int rate=24000, count=cue=="crackle"?2400:cue=="snap"?2200:1200;
        QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly);out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF",4);out<<quint32(36+count*2);out.writeRawData("WAVEfmt ",8);out<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(rate)<<quint32(rate*2)<<quint16(2)<<quint16(16);out.writeRawData("data",4);out<<quint32(count*2);
        quint32 random=93017; double phase=0;
        for(int i=0;i<count;++i) {
            const double t=double(i)/count, env=std::min(1.0,t*35)*std::exp(-t*7)*(1-t);
            const double frequency=cue=="commit"?900:cue=="back"?410:cue=="snap"?700:1250;
            phase+=2*std::numbers::pi*(frequency*(1-.65*t))/rate;
            random=random*1664525+1013904223;
            const double noise=(double(random)/4294967295.0-.5)*(i%271<21?1:.08);
            const double sample=(cue=="crackle"?noise:std::sin(phase))*env*.38;
            out<<qint16(sample*32767);
        }
        const QString path=directory_.filePath(cue+".wav");QFile file(path);if(!file.open(QIODevice::WriteOnly))continue;file.write(bytes);file.close();
        auto* effect=new QSoundEffect(this);effect->setSource(QUrl::fromLocalFile(path));effect->setVolume(.35f);effect->setLoopCount(1);effects_[cue]=effect;
    }
}
void AudioFeedback::play(const QString& cue) {
    if(!enabled_||!effects_.contains(cue))return;
    if(cue=="tick"&&limiter_.elapsed()<45)return;
    limiter_.restart();
    // Bound the voice count: one short cue replaces the previous cue.
    for(auto* effect:effects_)if(effect->isPlaying())effect->stop();
    effects_[cue]->play();
}
}
