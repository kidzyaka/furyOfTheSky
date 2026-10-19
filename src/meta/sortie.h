#pragma once

// What the menus hand to the flight scene, and what comes back
// (docs/decisions/0037, 0038). Platform-neutral.

#include <cstdint>

#include "game/bot.h"
#include "meta/catalog.h"

namespace meta {

enum class SortieMode : uint8_t {
    FreeFlight,      // no enemy
    TargetPractice,  // unarmed training target (game::BotMode Straight..Evade)
    Dogfight,        // one armed bot (Easy/Medium/Hard), respawns
    Mission,         // campaign: destroy `airEnemies` one after another; shot down = failed
};

// Pylon pairs of the Su-35S (docs/assets/su35s.md "Подвеска"), outboard first;
// bit i of a loadout mask = pair i loaded. game::kSu35SStations lists the
// stations right/left per pair: pair i = stations 2i and 2i+1.
enum LoadoutPair : uint8_t { kPairWingtip, kPairOuter, kPairMiddle, kPairInner, kPairIntake, kPairCount };
constexpr uint8_t kLoadoutFull = 0x1F;

struct LoadoutPreset {
    const char* name;  // "BALANCED"
    uint8_t mask;
};
int loadoutPresetCount();
const LoadoutPreset& loadoutPreset(int index);
uint16_t stationMask(uint8_t pairMask);  // 10 station bits for game::Armament

struct SortieConfig {
    SortieMode mode = SortieMode::FreeFlight;
    VehicleId vehicle = VehicleId::Su35S;
    uint8_t livery = 0;
    uint8_t loadout = kLoadoutFull;
    game::BotMode enemy = game::BotMode::Weave;  // TargetPractice / Dogfight
    Difficulty difficulty = Difficulty::Normal;  // Mission
    int location = -1, mission = -1;             // Mission
    int airEnemies = 0;                          // Mission: how many to destroy
};

// The bot a mission difficulty puts up.
game::BotMode missionEnemy(Difficulty d);

enum class SortieOutcome : uint8_t { Aborted, Success, Failed };

struct SortieResult {
    SortieOutcome outcome = SortieOutcome::Aborted;
    int airKills = 0;
    int groundKills = 0;
    int timesShotDown = 0;
    float seconds = 0.0f;
};

}  // namespace meta
