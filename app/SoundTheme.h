#pragma once
#include <QStringList>

namespace tabletpro {
inline QStringList soundThemeIds() { return {"crystal-whisper", "watch-tick", "felt-click", "walnut-tap", "bamboo-ticks", "tiny-gears", "linen-tick", "cork-touch", "cedar-knock", "bamboo-bead", "porcelain-touch", "crystal-hush"}; }
inline QStringList soundThemeNames() { return {"Crystal whisper", "Watch tick", "Felt click", "Walnut tap", "Bamboo ticks", "Tiny gears", "Linen tick", "Cork touch", "Cedar knock", "Bamboo bead", "Porcelain touch", "Crystal hush"}; }
inline QString defaultSoundTheme() { return "crystal-whisper"; }
}
