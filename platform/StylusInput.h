#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tabletpro {

// Values copied from the existing DLL's sixteen-int32, little-endian packet.
// Never retains or frees DLL memory.
struct StylusButtons {
    bool barrel = false;
    bool invert = false;
    bool inRange = false;
    static std::optional<StylusButtons> decode(std::span<const std::byte> packet);
};

class StylusButtonEdges {
public:
    void reset() { haveBaseline_ = false; previous_ = {}; }
    // First in-range sample is a baseline; out-of-range cancels that baseline.
    // Returns selected button 1/2 on a rising edge, otherwise zero.
    int update(const StylusButtons& buttons, int selectedSideButton);
private:
    bool haveBaseline_ = false;
    StylusButtons previous_;
};

// The legacy HID DLL has no unregister export. Only a disposable helper process
// loads it; the editor/overlay process receives normalized values over stdout.
class StylusInput final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int selectedSideButton READ selectedSideButton WRITE setSelectedSideButton NOTIFY selectedSideButtonChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    explicit StylusInput(QObject* parent = nullptr);
    ~StylusInput() override;
    bool enabled() const { return enabled_; }
    int selectedSideButton() const { return selectedSideButton_; }
    QString status() const { return status_; }
    void setEnabled(bool enabled);
    void setSelectedSideButton(int button);
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    // Called for --pen-helper BEFORE constructing QGuiApplication.
    static int runHelper();

signals:
    void enabledChanged();
    void selectedSideButtonChanged();
    void statusChanged();
    void settingsChanged();
    void sideButtonPressed();

private:
    void setStatus(const QString& status);
    void readOutput();
    void failProtocol(const QString& reason);
    void stopHelper();
    QProcess process_;
    QTimer startupTimer_;
    QByteArray pending_;
    StylusButtonEdges edges_;
    std::optional<StylusButtons> latestButtons_;
    QString status_ = QStringLiteral("Pen side button is off.");
    quint64 sequence_ = 0;
    bool enabled_ = false;
    bool stopping_ = false;
    bool ready_ = false;
    bool started_ = false;
    int selectedSideButton_ = 1;
};

} // namespace tabletpro
