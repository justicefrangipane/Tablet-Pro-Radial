#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QSoundEffect>
#include "SoundTheme.h"

namespace tabletpro {
class AudioFeedback : public QObject {
    Q_OBJECT
public:
    explicit AudioFeedback(QObject* parent=nullptr);
    void setEnabled(bool value);
    void setTheme(const QString& theme);
    QString theme() const { return theme_; }
    void preview(const QString& cue = "open");
signals:
    void cuePlayed(const QString& theme, const QString& cue, bool preview);
public slots:
    void play(const QString& cue);
private:
    void stop();
    void playCue(const QString& cue, bool preview);
    QHash<QString,QSoundEffect*> effects_;
    QElapsedTimer limiter_;
    QString theme_=defaultSoundTheme();
    bool enabled_=false;
};
}
