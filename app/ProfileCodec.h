#pragma once
#include "core/model.h"
#include <QByteArray>
#include <QString>
#include <optional>

namespace tabletpro {
struct DecodedProfile { std::optional<radial::Profile> profile; QString error; };
// Portable document preferences deliberately exclude device permissions, pen
// bindings and any live target window. Loading never grants input authority.
struct RadialSettings {
    bool soundEnabled = false;
    bool reducedMotion = false;
    int durationMs = 1640;
    bool backdropEnabled = true;
    bool operator==(const RadialSettings&) const = default;
};
struct DecodedRadialDocument {
    std::optional<radial::Profile> profile;
    std::optional<RadialSettings> settings;
    QString error;
};
QByteArray encodeProfile(const radial::Profile& profile);
DecodedProfile decodeProfile(const QByteArray& bytes);
QByteArray encodeTestDocument(const radial::Profile& profile, const RadialSettings& settings);
DecodedRadialDocument decodeRadialDocument(const QByteArray& bytes);
}
