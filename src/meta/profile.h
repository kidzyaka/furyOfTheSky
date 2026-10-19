#pragma once

// The player's persistent state: pilot card, campaign progress, hangar,
// statistics and settings (docs/decisions/0037, save format docs/formats/profile.md).
// Platform-neutral: no PSP headers (host-tested). Saving to the memory stick
// is platform/psp_save.*.

#include <cstddef>
#include <cstdint>

#include "meta/catalog.h"

namespace meta {

constexpr int kNicknameBytes = 24;   // UTF-8, NUL-terminated (the PSP system nickname fits)
constexpr int kMaxVehicles = 16;     // save slots; VehicleId indexes them
constexpr int kVolumeSteps = 10;     // volume settings run 0..10

struct Settings {
    bool invertPitch = false;   // false: stick back = nose up
    uint8_t sfxVolume = 8;      // world sounds (engines, weapons), 0..10
    uint8_t voiceVolume = 8;    // RI-65 voice informer
    uint8_t uiVolume = 7;       // menu clicks
    bool cameraShake = true;
    bool debugInfo = true;      // FPS / build id / S-L-G corner
};

struct VehicleProgress {
    bool owned = false;
    uint8_t livery = 0;
    uint8_t loadout = 0x1F;     // bit per pylon pair, see meta/sortie.h (all pairs loaded)
    uint32_t sorties = 0;       // flights in it: decides the favourite
};

// Bit per difficulty a mission was cleared on (bit 0 = Easy).
using MedalBits = uint8_t;

struct Profile {
    char nickname[kNicknameBytes] = {};
    uint8_t avatar = 0;                     // emblem index (meta::emblem), must be unlocked
    int32_t balance = 0;                    // credits
    MedalBits medals[kMaxLocations][kMaxMissions] = {};
    VehicleProgress vehicles[kMaxVehicles];
    VehicleId lastVehicle = VehicleId::Su35S;  // stands in the hangar on the main menu
    uint8_t lastLocation = 0;               // campaign carousel opens here
    uint32_t sorties = 0;                   // every sortie, training included
    uint32_t airKills = 0;                  // all sorties
    uint32_t groundKills = 0;
    uint32_t missionsWon = 0;
    uint32_t mpKills = 0, mpDeaths = 0, mpWins = 0, mpMatches = 0;  // multiplayer (not in the game yet)
    Settings settings;
};

// A fresh pilot: the Su-35S, the starting balance, nothing cleared.
// `nickname` = the PSP's system nickname (null/empty: "PILOT").
void resetProfile(Profile& p, const char* nickname);

// --- Campaign progression (docs/decisions/0037).
// Missions of a location unlock one after another; a location unlocks once
// the previous location's last mission is cleared. The first is always open.
bool locationUnlocked(const Profile& p, int location);
bool missionUnlocked(const Profile& p, int location, int mission);
bool missionCleared(const Profile& p, int location, int mission);  // on any difficulty
int clearedCount(const Profile& p, int location);

struct MissionReward {
    int credits = 0;
    bool firstClear = false;      // first time on this difficulty
    int rankBefore = 0, rankAfter = 0;
};
// A won mission: medal, credits (full on a first clear of that difficulty,
// a fifth on replays), missionsWon. Kills are NOT counted here: recordSortie
// sees every sortie, won ones included, so counting them in both would double
// them (airKills/groundKills are accepted for the call site and ignored).
// A bad location/mission/difficulty changes nothing and returns an empty reward;
// the balance saturates at 999,999,999.
// Credits a win would pay now (the briefing shows it before the flight).
int missionPayout(const Profile& p, int location, int mission, Difficulty d);
MissionReward completeMission(Profile& p, int location, int mission, Difficulty d, int airKills, int groundKills);
// Every sortie (training, a lost or aborted mission too): sortie counters and
// kills; also marks the vehicle as the last flown (the favoriteVehicle tie-break).
void recordSortie(Profile& p, VehicleId v, int airKills, int groundKills);

// --- Rank: from how many missions were cleared and how hard (best
// difficulty per mission: Easy 1, Normal 2, Hard 3 points).
int rankScore(const Profile& p);
int rankIndex(const Profile& p);           // 0 = LIEUTENANT .. kRankCount - 1
constexpr int kRankCount = 7;
int rankThreshold(int rank);               // score needed for it
const char* rankName(int rank);            // "SENIOR LIEUTENANT"

// --- Emblems: unlocked by achievements computed from the profile itself.
bool emblemUnlocked(const Profile& p, int emblem);
int unlockedEmblemCount(const Profile& p);
// "SHOOT DOWN 10 ENEMY AIRCRAFT" / "CLEAR GLACIER RANGE ON HARD" / "AVAILABLE FROM THE START".
void describeUnlock(const EmblemInfo& e, char* out, int size);
// 0..1 towards the unlock (for a progress bar); 1 when unlocked.
float unlockProgress(const Profile& p, int emblem);

// --- Hangar.
VehicleId favoriteVehicle(const Profile& p);  // most sorties; ties: the last flown
bool canBuy(const Profile& p, VehicleId v);
bool buyVehicle(Profile& p, VehicleId v);     // spends credits, marks owned

// --- Save format (docs/formats/profile.md): versioned, CRC-checked.
constexpr size_t kProfileSaveBytes = 1024;    // fixed buffer, the format fits with room to grow
size_t serialize(const Profile& p, uint8_t* out, size_t capacity);  // bytes written, 0 on error
// false: bad magic/version/size/CRC -> `p` untouched. Trailing bytes (a padded
// file) are ignored; values that are out of range despite a good CRC (indexes,
// volumes, balance) are clamped rather than rejected.
bool deserialize(Profile& p, const uint8_t* data, size_t size);

}  // namespace meta
