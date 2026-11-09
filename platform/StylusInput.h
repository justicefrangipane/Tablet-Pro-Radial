#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <array>
#include <memory>
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

struct StylusSample {
    std::array<qint32,16> values{};
    bool eraser() const { return values[0]; }
    bool invert() const { return values[1]; }
    bool barrel() const { return values[2]; }
    bool tip() const { return values[3]; }
    bool inRange() const { return values[4]; }
    StylusButtons buttons() const { return {barrel(),invert(),inRange()}; }
    static std::optional<StylusSample> decode(std::span<const std::byte> packet);
    static std::optional<StylusSample> fromHex(const QByteArray& packet);
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
    Q_PROPERTY(QVariantList readings READ readings NOTIFY readingsChanged)
public:
    explicit StylusInput(QObject* parent = nullptr);
    ~StylusInput() override;
    bool enabled() const { return enabled_; }
    int selectedSideButton() const { return selectedSideButton_; }
    QString status() const { return status_; }
    QVariantList readings() const;
    void setEnabled(bool enabled);
    void setSelectedSideButton(int button);
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    // Called for --pen-helper BEFORE constructing QGuiApplication.
    static int runHelper(bool generic = false);

signals:
    void enabledChanged();
    void selectedSideButtonChanged();
    void statusChanged();
    void settingsChanged();
    void sideButtonPressed();
    void readingsChanged();
    void tipChanged(quintptr source, bool down);
    void proximityLeft();
    void sampleReceived(int source, const tabletpro::StylusSample& sample);
    void inputLost();

private:
    void setStatus(const QString& status);
    struct Channel;
    void readOutput(Channel& channel);
    void failProtocol(Channel& channel, const QString& reason);
    void refreshStatus();
    void stopChannel(Channel& channel);
    void stopHelper();
    std::array<std::unique_ptr<Channel>,2> channels_;
    QTimer readingsTimer_;
    QString status_ = QStringLiteral("Pen side button is off.");
    bool enabled_ = false;
    bool started_ = false;
    int selectedSideButton_ = 1;
};

} // namespace tabletpro
