#include "meta/sortie.h"

namespace meta {

namespace {

// Bit i of a mask = pylon pair i (kPairWingtip..kPairIntake). Wingtip and outer
// pairs carry R-73, the rest R-77 (docs/assets/su35s.md "Подвеска").
constexpr uint8_t bit(LoadoutPair pair) { return static_cast<uint8_t>(1u << pair); }

const LoadoutPreset kPresets[] = {
    {"BALANCED", kLoadoutFull},                                                   // 4xR-73 + 6xR-77
    {"DOGFIGHT", bit(kPairWingtip) | bit(kPairOuter) | bit(kPairInner)},          // 4xR-73 + 2xR-77
    {"INTERCEPT", bit(kPairWingtip) | bit(kPairMiddle) | bit(kPairInner) | bit(kPairIntake)},  // 2xR-73 + 6xR-77
    {"LIGHT", bit(kPairWingtip) | bit(kPairInner)},                               // 2xR-73 + 2xR-77
};
constexpr int kPresetTotal = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

}  // namespace

int loadoutPresetCount() { return kPresetTotal; }

const LoadoutPreset& loadoutPreset(int index) {
    if (index < 0 || index >= kPresetTotal) index = 0;
    return kPresets[index];
}

uint16_t stationMask(uint8_t pairMask) {
    // kSu35SStations lists right/left per pair: pair i owns stations 2i and 2i+1.
    uint16_t stations = 0;
    for (int i = 0; i < kPairCount; ++i) {
        if (pairMask & (1u << i)) stations = static_cast<uint16_t>(stations | (3u << (2 * i)));
    }
    return stations;
}

game::BotMode missionEnemy(Difficulty d) {
    switch (d) {
        case Difficulty::Easy: return game::BotMode::Easy;
        case Difficulty::Hard: return game::BotMode::Hard;
        default: return game::BotMode::Medium;
    }
}

}  // namespace meta
