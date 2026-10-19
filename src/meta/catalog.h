#pragma once

// Game content tables: vehicles, campaign locations and missions
// (docs/decisions/0037). Platform-neutral: no PSP headers (host-tested).

#include <cstdint>

#include "world/environment.h"

namespace meta {

enum class VehicleType : uint8_t { Plane, Helicopter };

// Order is part of the save format (docs/formats/profile.md): append only.
enum class VehicleId : uint8_t { Su35S, Su57, Ka52, Count };
constexpr int kVehicleCount = static_cast<int>(VehicleId::Count);

struct VehicleInfo {
    VehicleId id;
    const char* name;      // short, on cards: "SU-35S"
    const char* fullName;  // "SU-35S FLANKER-E"
    const char* role;      // "AIR SUPERIORITY FIGHTER"
    VehicleType type;
    int price;             // credits in the shop (0 = issued at the start)
    bool available;        // false: in development - shown CLASSIFIED, cannot be bought or flown
    int liveryCount;
    const char* const* liveryNames;
    // Shown on the hangar/shop cards (real published figures, rounded).
    int maxSpeedKmh;
    int ceilingM;
    float maxG;
    int rangeKm;
    int schematicRow;      // cell row in assets/ui/schematics.ftx (docs/assets/ui-art.md)
};
const VehicleInfo& vehicle(VehicleId id);

// Defined with the rest of the sortie's environment (docs/decisions/0041).
using Weather = world::Weather;
using TimeOfDay = world::TimeOfDay;
enum class Difficulty : uint8_t { Easy, Normal, Hard, Count };
constexpr int kDifficultyCount = static_cast<int>(Difficulty::Count);
const char* weatherName(Weather w);     // "OVERCAST"
const char* timeName(TimeOfDay t);      // "DUSK"
const char* difficultyName(Difficulty d);  // "NORMAL"

// A mission can be flown in an aircraft, a helicopter, or either - each
// variant has its own task and opposition.
struct MissionVariant {
    VehicleType type;
    uint8_t airEnemies;     // enemy aircraft to destroy (they come one after another for now)
    uint8_t groundTargets;  // ground targets to destroy (not in the game yet: 0 for plane variants)
    const char* objective;  // one line: "DESTROY 3 FIGHTERS"
};

struct Mission {
    const char* code;      // "NS-01"
    const char* name;      // "FIRST CONTACT"
    const char* briefing;  // 2-4 short sentences, '\n' allowed
    Weather weather;
    TimeOfDay time;
    int reward;            // credits for the first clear on NORMAL (scaled by difficulty)
    int variantCount;
    MissionVariant variants[2];
};

// Fixed capacities: part of the save format (progress is stored per slot).
constexpr int kMaxLocations = 8;
constexpr int kMaxMissions = 8;  // per location

struct Location {
    const char* id;        // "north_sea": image assets/ui/loc_<id>.ftx
    const char* name;      // "NORTHERN SEA"
    const char* subtitle;  // one line under the name on the card
    int missionCount;
    const Mission* missions;
};
int locationCount();
const Location& location(int index);

// --- Pilot emblems (docs/decisions/0039). Unlocks are achievements derived
// from what the profile already records (kills, sorties, medals, rank), so
// they need no save fields. Order is part of the save format (Profile::avatar).
enum class UnlockKind : uint8_t {
    Default,          // from the start
    AirKills,         // value = enemy aircraft shot down (all sorties)
    Sorties,          // value = sorties flown
    MissionsWon,      // value = missions won (replays count)
    LocationCleared,  // value = location index: every mission on any difficulty
    LocationHard,     // value = location index: every mission on HARD
    Rank,             // value = rank index reached
    HardMedals,       // value = missions cleared on HARD
    MpWins,           // value = multiplayer wins (not in the game yet)
};
struct EmblemInfo {
    const char* name;  // "MARKSMAN"
    UnlockKind unlock;
    int value;
};
int emblemCount();
const EmblemInfo& emblem(int index);

// Does the mission have a variant for this vehicle type? Returns it or null.
const MissionVariant* missionVariant(const Mission& m, VehicleType type);

}  // namespace meta
