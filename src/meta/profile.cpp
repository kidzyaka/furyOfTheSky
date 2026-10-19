#include "meta/profile.h"

#include <cstdio>
#include <cstring>

#include "meta/sortie.h"

// Player profile: progression rules, rank, hangar and the save format
// (docs/decisions/0037, docs/formats/profile.md).

namespace meta {

namespace {

constexpr int kStartBalance = 50000;
constexpr int32_t kMaxBalance = 999999999;  // fits the 9-digit field on the cards

// Credits multiplier per difficulty, percent (Easy, Normal, Hard).
constexpr int kRewardPercent[kDifficultyCount] = {60, 100, 150};

// Rank thresholds in score points: clearing a mission gives 1/2/3 points for
// its best difficulty (Easy/Normal/Hard), so the campaign tops out at 48.
// All 16 on Normal = 32 -> COLONEL; MAJOR GENERAL needs 44 of 48.
constexpr int kRankThresholds[kRankCount] = {0, 3, 8, 15, 22, 30, 44};
const char* const kRankNames[kRankCount] = {
    "LIEUTENANT", "SENIOR LIEUTENANT", "CAPTAIN", "MAJOR", "LIEUTENANT COLONEL", "COLONEL", "MAJOR GENERAL",
};

constexpr MedalBits kMedalMask = 0x07;  // one bit per difficulty

bool validMission(int loc, int mission) {
    return loc >= 0 && loc < locationCount() && mission >= 0 && mission < location(loc).missionCount;
}

int roundTo100(int v) { return (v + 50) / 100 * 100; }

uint32_t addSaturated(uint32_t a, int add) {
    if (add <= 0) return a;
    uint32_t sum = a + static_cast<uint32_t>(add);
    return sum < a ? 0xFFFFFFFFu : sum;  // wrapped: stay at the maximum
}

// Nickname into a kNicknameBytes field: whole UTF-8 characters only, so a
// long Cyrillic name is cut between letters, never inside one.
void copyNickname(char* dst, const char* src) {
    std::memset(dst, 0, kNicknameBytes);
    size_t n = src ? std::strlen(src) : 0;
    if (n > static_cast<size_t>(kNicknameBytes - 1)) {
        n = kNicknameBytes - 1;
        // src[n] is the first byte left out: if it is a continuation byte
        // (10xxxxxx) the cut falls inside a character, step back to its start.
        while (n > 0 && (static_cast<uint8_t>(src[n]) & 0xC0) == 0x80) --n;
    }
    if (n == 0) {
        std::strcpy(dst, "PILOT");
        return;
    }
    std::memcpy(dst, src, n);
}

// --- Save format helpers. Field-by-field little-endian: no struct is ever
// copied as bytes (docs/psp/gotchas.md #18), and uint32_t/int32_t are not int
// on PSP (#17, #27), so nothing here depends on their width beyond 32 bits.
constexpr uint8_t kMagic[4] = {'F', 'P', 'R', 'F'};
constexpr uint16_t kVersion = 1;
constexpr size_t kHeaderBytes = 8;   // magic, u16 version, u16 payload size
constexpr size_t kVehicleBytes = 8;  // flags, livery, loadout, reserved, u32 sorties
constexpr size_t kPayloadBytes = kNicknameBytes + 4 + 4 + 8 * 4 + 4 + kMaxLocations * kMaxMissions +
                                 kMaxVehicles * kVehicleBytes;
constexpr size_t kCrcBytes = 4;
constexpr size_t kTotalBytes = kHeaderBytes + kPayloadBytes + kCrcBytes;
static_assert(kPayloadBytes == 260, "payload layout changed: bump the version and update docs/formats/profile.md");
static_assert(kTotalBytes <= kProfileSaveBytes, "profile does not fit the save buffer");

// Settings flag bits.
constexpr uint8_t kFlagInvertPitch = 1u << 0;
constexpr uint8_t kFlagCameraShake = 1u << 1;
constexpr uint8_t kFlagDebugInfo = 1u << 2;
// Vehicle flag bits.
constexpr uint8_t kFlagOwned = 1u << 0;

uint32_t crc32(const uint8_t* data, size_t n) {
    // IEEE 802.3, reflected, the same as zlib's crc32(); bitwise: a table is not worth it for ~270 bytes.
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= data[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

struct Writer {
    uint8_t* p;
    size_t pos = 0;
    void u8(uint8_t v) { p[pos++] = v; }
    void u16(uint16_t v) {
        u8(static_cast<uint8_t>(v & 0xFF));
        u8(static_cast<uint8_t>(v >> 8));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) u8(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
};

struct Reader {
    const uint8_t* p;
    size_t pos = 0;
    uint8_t u8() { return p[pos++]; }
    uint16_t u16() {
        uint16_t lo = u8();
        uint16_t hi = u8();
        return static_cast<uint16_t>(lo | (hi << 8));
    }
    uint32_t u32() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(u8()) << (8 * i);
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }
};

}  // namespace

void resetProfile(Profile& p, const char* nickname) {
    // `nickname` may be p.nickname itself (RESET PROGRESS keeps the callsign):
    // take a copy before the profile is wiped.
    char keep[kNicknameBytes];
    copyNickname(keep, nickname);
    p = Profile();
    std::memcpy(p.nickname, keep, sizeof keep);
    p.balance = kStartBalance;
    p.vehicles[static_cast<int>(VehicleId::Su35S)].owned = true;
    p.lastVehicle = VehicleId::Su35S;
}

// --- Campaign progression.

bool missionCleared(const Profile& p, int loc, int mission) {
    return validMission(loc, mission) && (p.medals[loc][mission] & kMedalMask) != 0;
}

bool locationUnlocked(const Profile& p, int loc) {
    if (loc < 0 || loc >= locationCount()) return false;
    if (loc == 0) return true;
    return missionCleared(p, loc - 1, location(loc - 1).missionCount - 1);
}

bool missionUnlocked(const Profile& p, int loc, int mission) {
    if (!validMission(loc, mission) || !locationUnlocked(p, loc)) return false;
    return mission == 0 || missionCleared(p, loc, mission - 1);
}

int clearedCount(const Profile& p, int loc) {
    if (loc < 0 || loc >= locationCount()) return 0;
    int n = 0;
    for (int m = 0; m < location(loc).missionCount; ++m) {
        if (missionCleared(p, loc, m)) ++n;
    }
    return n;
}

int missionPayout(const Profile& p, int loc, int mission, Difficulty d) {
    const int di = static_cast<int>(d);
    if (!validMission(loc, mission) || di < 0 || di >= kDifficultyCount) return 0;
    const bool first = (p.medals[loc][mission] & (1u << di)) == 0;
    const int credits = roundTo100(location(loc).missions[mission].reward * kRewardPercent[di] / 100);
    return first ? credits : roundTo100(credits / 5);
}

MissionReward completeMission(Profile& p, int loc, int mission, Difficulty d, int /*airKills*/, int /*groundKills*/) {
    // Kills are not counted here: recordSortie sees every sortie, won ones included.
    MissionReward r;
    r.rankBefore = rankIndex(p);
    r.rankAfter = r.rankBefore;
    int di = static_cast<int>(d);
    if (!validMission(loc, mission) || di < 0 || di >= kDifficultyCount) return r;

    MedalBits bitMask = static_cast<MedalBits>(1u << di);
    r.firstClear = (p.medals[loc][mission] & bitMask) == 0;
    const int credits = missionPayout(p, loc, mission, d);
    r.credits = credits;

    p.medals[loc][mission] = static_cast<MedalBits>((p.medals[loc][mission] | bitMask) & kMedalMask);
    // Compared in 64 bits so a balance near the cap cannot overflow int32_t.
    long long balance = static_cast<long long>(p.balance) + credits;
    p.balance = balance > kMaxBalance ? kMaxBalance : static_cast<int32_t>(balance);
    p.missionsWon = addSaturated(p.missionsWon, 1);

    r.rankAfter = rankIndex(p);
    return r;
}

void recordSortie(Profile& p, VehicleId v, int airKills, int groundKills) {
    p.sorties = addSaturated(p.sorties, 1);
    p.airKills = addSaturated(p.airKills, airKills);
    p.groundKills = addSaturated(p.groundKills, groundKills);
    int vi = static_cast<int>(v);
    if (vi < 0 || vi >= kVehicleCount) return;
    p.vehicles[vi].sorties = addSaturated(p.vehicles[vi].sorties, 1);
    p.lastVehicle = v;  // the tie-break of favoriteVehicle
}

// --- Emblems.

namespace {

int hardCount(const Profile& p, int loc) {
    int n = 0;
    for (int m = 0; m < location(loc).missionCount; ++m) n += (p.medals[loc][m] >> static_cast<int>(Difficulty::Hard)) & 1;
    return n;
}

// Progress towards an emblem as (have, need).
void emblemProgress(const Profile& p, const EmblemInfo& e, long long& have, long long& need) {
    have = 0;
    need = e.value > 0 ? e.value : 1;
    switch (e.unlock) {
        case UnlockKind::Default: have = need; break;
        case UnlockKind::AirKills: have = p.airKills; break;
        case UnlockKind::Sorties: have = p.sorties; break;
        case UnlockKind::MissionsWon: have = p.missionsWon; break;
        case UnlockKind::MpWins: have = p.mpWins; break;
        case UnlockKind::LocationCleared:
            if (e.value >= 0 && e.value < locationCount()) {
                have = clearedCount(p, e.value);
                need = location(e.value).missionCount;
            }
            break;
        case UnlockKind::LocationHard:
            if (e.value >= 0 && e.value < locationCount()) {
                have = hardCount(p, e.value);
                need = location(e.value).missionCount;
            }
            break;
        case UnlockKind::Rank:
            have = rankScore(p);
            need = rankThreshold(e.value);
            if (need <= 0) need = 1, have = 1;
            break;
        case UnlockKind::HardMedals:
            for (int l = 0; l < locationCount(); ++l) have += hardCount(p, l);
            break;
    }
}

}  // namespace

bool emblemUnlocked(const Profile& p, int index) {
    if (index < 0 || index >= emblemCount()) return false;
    long long have, need;
    emblemProgress(p, emblem(index), have, need);
    return have >= need;
}

int unlockedEmblemCount(const Profile& p) {
    int n = 0;
    for (int i = 0; i < emblemCount(); ++i) n += emblemUnlocked(p, i);
    return n;
}

float unlockProgress(const Profile& p, int index) {
    if (index < 0 || index >= emblemCount()) return 0.0f;
    long long have, need;
    emblemProgress(p, emblem(index), have, need);
    if (have >= need) return 1.0f;
    return need > 0 ? static_cast<float>(have) / static_cast<float>(need) : 0.0f;
}

void describeUnlock(const EmblemInfo& e, char* out, int size) {
    const char* loc = e.value >= 0 && e.value < locationCount() ? location(e.value).name : "";
    switch (e.unlock) {
        case UnlockKind::Default: std::snprintf(out, size, "AVAILABLE FROM THE START"); break;
        case UnlockKind::AirKills:
            std::snprintf(out, size, e.value == 1 ? "SHOOT DOWN AN ENEMY AIRCRAFT" : "SHOOT DOWN %d ENEMY AIRCRAFT",
                          e.value);
            break;
        case UnlockKind::Sorties: std::snprintf(out, size, "FLY %d SORTIES", e.value); break;
        case UnlockKind::MissionsWon: std::snprintf(out, size, "WIN %d MISSIONS", e.value); break;
        case UnlockKind::LocationCleared: std::snprintf(out, size, "CLEAR %s", loc); break;
        case UnlockKind::LocationHard: std::snprintf(out, size, "CLEAR %s ON HARD", loc); break;
        case UnlockKind::Rank: std::snprintf(out, size, "REACH %s", rankName(e.value)); break;
        case UnlockKind::HardMedals: std::snprintf(out, size, "CLEAR %d MISSIONS ON HARD", e.value); break;
        case UnlockKind::MpWins:
            std::snprintf(out, size, e.value == 1 ? "WIN A MULTIPLAYER MATCH" : "WIN %d MULTIPLAYER MATCHES", e.value);
            break;
    }
}

// --- Rank.

int rankScore(const Profile& p) {
    int score = 0;
    for (int l = 0; l < locationCount(); ++l) {
        for (int m = 0; m < location(l).missionCount; ++m) {
            MedalBits bits = p.medals[l][m];
            // The best difficulty counts, not the sum: replays on Easy never lower or inflate it.
            if (bits & 0x04) score += 3;
            else if (bits & 0x02) score += 2;
            else if (bits & 0x01) score += 1;
        }
    }
    return score;
}

int rankIndex(const Profile& p) {
    int score = rankScore(p);
    int rank = 0;
    for (int i = 1; i < kRankCount; ++i) {
        if (score >= kRankThresholds[i]) rank = i;
    }
    return rank;
}

int rankThreshold(int rank) {
    if (rank < 0) rank = 0;
    if (rank >= kRankCount) rank = kRankCount - 1;
    return kRankThresholds[rank];
}

const char* rankName(int rank) {
    if (rank < 0) rank = 0;
    if (rank >= kRankCount) rank = kRankCount - 1;
    return kRankNames[rank];
}

// --- Hangar.

VehicleId favoriteVehicle(const Profile& p) {
    int last = static_cast<int>(p.lastVehicle);
    if (last < 0 || last >= kVehicleCount) last = 0;
    uint32_t best = 0;
    for (int i = 0; i < kVehicleCount; ++i) {
        if (p.vehicles[i].sorties > best) best = p.vehicles[i].sorties;
    }
    if (best == 0) return static_cast<VehicleId>(last);
    // Ties go to the aircraft flown last, then to the lowest id.
    if (p.vehicles[last].sorties == best) return static_cast<VehicleId>(last);
    for (int i = 0; i < kVehicleCount; ++i) {
        if (p.vehicles[i].sorties == best) return static_cast<VehicleId>(i);
    }
    return static_cast<VehicleId>(last);
}

bool canBuy(const Profile& p, VehicleId v) {
    int vi = static_cast<int>(v);
    if (vi < 0 || vi >= kVehicleCount) return false;
    const VehicleInfo& info = vehicle(v);
    return info.available && !p.vehicles[vi].owned && p.balance >= info.price;
}

bool buyVehicle(Profile& p, VehicleId v) {
    if (!canBuy(p, v)) return false;
    int vi = static_cast<int>(v);
    p.balance -= vehicle(v).price;
    p.vehicles[vi].owned = true;
    return true;
}

// --- Save format.

size_t serialize(const Profile& p, uint8_t* out, size_t capacity) {
    if (!out || capacity < kTotalBytes) return 0;

    Writer w{out};
    for (int i = 0; i < 4; ++i) w.u8(kMagic[i]);
    w.u16(kVersion);
    w.u16(static_cast<uint16_t>(kPayloadBytes));

    // Nickname: bytes up to the NUL, zero padded, the last byte always NUL.
    bool ended = false;
    for (int i = 0; i < kNicknameBytes; ++i) {
        uint8_t c = (!ended && i < kNicknameBytes - 1) ? static_cast<uint8_t>(p.nickname[i]) : 0;
        if (c == 0) ended = true;
        w.u8(c);
    }
    w.u8(p.avatar);
    w.u8(static_cast<uint8_t>(p.lastVehicle));
    w.u8(p.lastLocation);
    w.u8(0);  // reserved
    w.i32(p.balance);
    w.u32(p.sorties);
    w.u32(p.airKills);
    w.u32(p.groundKills);
    w.u32(p.missionsWon);
    w.u32(p.mpKills);
    w.u32(p.mpDeaths);
    w.u32(p.mpWins);
    w.u32(p.mpMatches);

    uint8_t flags = 0;
    if (p.settings.invertPitch) flags |= kFlagInvertPitch;
    if (p.settings.cameraShake) flags |= kFlagCameraShake;
    if (p.settings.debugInfo) flags |= kFlagDebugInfo;
    w.u8(flags);
    w.u8(p.settings.sfxVolume);
    w.u8(p.settings.voiceVolume);
    w.u8(p.settings.uiVolume);

    for (int l = 0; l < kMaxLocations; ++l) {
        for (int m = 0; m < kMaxMissions; ++m) w.u8(p.medals[l][m]);
    }
    for (int i = 0; i < kMaxVehicles; ++i) {
        const VehicleProgress& v = p.vehicles[i];
        w.u8(v.owned ? kFlagOwned : 0);
        w.u8(v.livery);
        w.u8(v.loadout);
        w.u8(0);  // reserved
        w.u32(v.sorties);
    }

    w.u32(crc32(out, w.pos));  // magic .. payload
    return w.pos;
}

bool deserialize(Profile& p, const uint8_t* data, size_t size) {
    if (!data || size < kHeaderBytes + kCrcBytes) return false;

    Reader r{data};
    for (int i = 0; i < 4; ++i) {
        if (r.u8() != kMagic[i]) return false;
    }
    if (r.u16() != kVersion) return false;  // unknown (older or newer) layout
    if (r.u16() != kPayloadBytes) return false;
    if (size < kTotalBytes) return false;   // truncated; trailing padding is fine

    uint32_t stored = Reader{data, kHeaderBytes + kPayloadBytes}.u32();
    if (crc32(data, kHeaderBytes + kPayloadBytes) != stored) return false;

    // Parse into a scratch profile so a failure part-way leaves `p` untouched.
    Profile t;
    for (int i = 0; i < kNicknameBytes; ++i) t.nickname[i] = static_cast<char>(r.u8());
    if (t.nickname[kNicknameBytes - 1] != 0) return false;  // not terminated
    if (t.nickname[0] == 0) copyNickname(t.nickname, nullptr);

    t.avatar = r.u8();
    uint8_t lastVehicle = r.u8();
    t.lastLocation = r.u8();
    r.u8();  // reserved
    t.balance = r.i32();
    t.sorties = r.u32();
    t.airKills = r.u32();
    t.groundKills = r.u32();
    t.missionsWon = r.u32();
    t.mpKills = r.u32();
    t.mpDeaths = r.u32();
    t.mpWins = r.u32();
    t.mpMatches = r.u32();

    uint8_t flags = r.u8();
    t.settings.invertPitch = (flags & kFlagInvertPitch) != 0;
    t.settings.cameraShake = (flags & kFlagCameraShake) != 0;
    t.settings.debugInfo = (flags & kFlagDebugInfo) != 0;
    t.settings.sfxVolume = r.u8();
    t.settings.voiceVolume = r.u8();
    t.settings.uiVolume = r.u8();

    for (int l = 0; l < kMaxLocations; ++l) {
        for (int m = 0; m < kMaxMissions; ++m) t.medals[l][m] = static_cast<MedalBits>(r.u8() & kMedalMask);
    }
    for (int i = 0; i < kMaxVehicles; ++i) {
        VehicleProgress& v = t.vehicles[i];
        v.owned = (r.u8() & kFlagOwned) != 0;
        v.livery = r.u8();
        v.loadout = r.u8() & kLoadoutFull;
        r.u8();  // reserved
        v.sorties = r.u32();
    }

    // A valid CRC proves the bytes are intact, not that the values make sense
    // (hand-edited file, a bug in an older build): keep every index in range.
    if (!emblemUnlocked(t, t.avatar)) t.avatar = 0;
    if (t.lastLocation >= locationCount()) t.lastLocation = 0;
    t.lastVehicle = lastVehicle < kVehicleCount ? static_cast<VehicleId>(lastVehicle) : VehicleId::Su35S;
    if (t.balance < 0) t.balance = 0;
    if (t.balance > kMaxBalance) t.balance = kMaxBalance;
    if (t.settings.sfxVolume > kVolumeSteps) t.settings.sfxVolume = kVolumeSteps;
    if (t.settings.voiceVolume > kVolumeSteps) t.settings.voiceVolume = kVolumeSteps;
    if (t.settings.uiVolume > kVolumeSteps) t.settings.uiVolume = kVolumeSteps;
    for (int i = 0; i < kVehicleCount; ++i) {
        if (t.vehicles[i].livery >= vehicle(static_cast<VehicleId>(i)).liveryCount) t.vehicles[i].livery = 0;
    }
    t.vehicles[static_cast<int>(VehicleId::Su35S)].owned = true;  // the starter aircraft is never lost

    p = t;
    return true;
}

}  // namespace meta
