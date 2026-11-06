#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QSoundEffect>
#include <QTemporaryDir>

namespace tabletpro {
class AudioFeedback : public QObject {
    Q_OBJECT
public:
    explicit AudioFeedback(QObject* parent=nullptr);
    void setEnabled(bool value) { enabled_=value; if (!value) for (auto* effect : effects_) effect->stop(); }
public slots:
    void play(const QString& cue);
private:
    QTemporaryDir directory_;
    QHash<QString,QSoundEffect*> effects_;
    QElapsedTimer limiter_;
    bool enabled_=false;
};
}
