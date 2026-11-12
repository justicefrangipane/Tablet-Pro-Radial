#pragma once
#include <QStringList>

namespace tabletpro {
inline QStringList soundThemeIds() { return {"felt-click", "walnut-tap", "bamboo-ticks", "tiny-gears", "crystal-whisper"}; }
inline QStringList soundThemeNames() { return {"Felt click", "Walnut tap", "Bamboo ticks", "Tiny gears", "Crystal whisper"}; }
inline QString defaultSoundTheme() { return "crystal-whisper"; }
}
