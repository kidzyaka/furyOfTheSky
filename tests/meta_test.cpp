// Host-side tests for the meta layer (docs/decisions/0037): content tables,
// campaign progression, rank, hangar, the profile save format and loadouts.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "game/armament.h"
#include "game/missile.h"
#include "meta/catalog.h"
#include "meta/profile.h"
#include "meta/sortie.h"

namespace {

int g_failures = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ++g_failures;                                                   \
            std::printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                       \
            std::printf("\n");                                              \
        }                                                                   \
    } while (0)

using namespace meta;

constexpr size_t kSaveBytes = 272;  // 8 header + 260 payload + 4 CRC (docs/formats/profile.md)

// Byte offsets of the version-1 layout (docs/formats/profile.md), kept here
// as an independent copy so a silent change of the format fails the test.
constexpr size_t kOffVersion = 4;
constexpr size_t kOffPayloadSize = 6;
constexpr size_t kOffNickname = 8;
constexpr size_t kOffAvatar = 32;
constexpr size_t kOffLastVehicle = 33;
constexpr size_t kOffLastLocation = 34;
constexpr size_t kOffBalance = 36;
constexpr size_t kOffSorties = 40;
constexpr size_t kOffSettings = 72;
constexpr size_t kOffMedals = 76;
constexpr size_t kOffVehicles = 140;
constexpr size_t kOffCrc = 268;

// Reference CRC-32 (IEEE 802.3 / zlib), written independently of profile.cpp.
uint32_t refCrc32(const uint8_t* d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= d[i];
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
    }
    return c ^ 0xFFFFFFFFu;
}

void fixCrc(uint8_t* buf) {
    uint32_t c = refCrc32(buf, kOffCrc);
    for (int i = 0; i < 4; ++i) buf[kOffCrc + i] = static_cast<uint8_t>((c >> (8 * i)) & 0xFF);
}

size_t strLen(const char* s) { return std::strlen(s); }

bool isUpperText(const char* s) {
    // Menu text is drawn with the uppercase font: letters, digits, space and a little punctuation.
    for (const char* c = s; *c; ++c) {
        bool ok = (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == ' ' || *c == '-' || *c == '.' || *c == ',';
        if (!ok) return false;
    }
    return true;
}

// UTF-8 well-formedness for the shapes the tests use (1-4 byte characters).
bool validUtf8(const char* s) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
    while (*p) {
        int extra = 0;
        if (*p < 0x80) extra = 0;
        else if ((*p & 0xE0) == 0xC0) extra = 1;
        else if ((*p & 0xF0) == 0xE0) extra = 2;
        else if ((*p & 0xF8) == 0xF0) extra = 3;
        else return false;
        ++p;
        for (int i = 0; i < extra; ++i, ++p) {
            if ((*p & 0xC0) != 0x80) return false;
        }
    }
    return true;
}

int countChar(const char* s, char ch) {
    int n = 0;
    for (; *s; ++s) n += (*s == ch);
    return n;
}

// Clears every mission in campaign order on one difficulty.
void clearAll(Profile& p, Difficulty d) {
    for (int l = 0; l < locationCount(); ++l) {
        for (int m = 0; m < location(l).missionCount; ++m) completeMission(p, l, m, d, 0, 0);
    }
}

// Field-by-field comparison: never memcmp a struct (docs/psp/gotchas.md #18).
bool sameProfile(const Profile& a, const Profile& b, const char** why) {
#define SAME(f)                      \
    if (!(a.f == b.f)) {             \
        *why = #f;                   \
        return false;                \
    }
    if (std::strcmp(a.nickname, b.nickname) != 0) {
        *why = "nickname";
        return false;
    }
    SAME(avatar) SAME(balance) SAME(lastVehicle) SAME(lastLocation) SAME(sorties) SAME(airKills)
    SAME(groundKills) SAME(missionsWon) SAME(mpKills) SAME(mpDeaths) SAME(mpWins) SAME(mpMatches)
    SAME(settings.invertPitch) SAME(settings.sfxVolume) SAME(settings.voiceVolume)
    SAME(settings.uiVolume) SAME(settings.cameraShake) SAME(settings.debugInfo)
    for (int l = 0; l < kMaxLocations; ++l) {
        for (int m = 0; m < kMaxMissions; ++m) SAME(medals[l][m])
    }
    for (int i = 0; i < kMaxVehicles; ++i) {
        SAME(vehicles[i].owned) SAME(vehicles[i].livery) SAME(vehicles[i].loadout) SAME(vehicles[i].sorties)
    }
#undef SAME
    return true;
}

bool sameOrReport(const Profile& a, const Profile& b, const char* what) {
    const char* why = "";
    bool same = sameProfile(a, b, &why);
    if (!same) std::printf("  FAIL %s: field `%s` differs\n", what, why);
    if (!same) ++g_failures;
    return same;
}

// A profile with every field away from its default, for round trips.
Profile busyProfile() {
    Profile p;
    resetProfile(p, "Egor \xD0\x95\xD0\xB3\xD0\xBE\xD1\x80");  // "Egor Егор"
    p.avatar = 2;
    p.balance = 123456;
    p.lastLocation = 2;
    p.lastVehicle = VehicleId::Su57;
    p.sorties = 41;
    p.airKills = 77;
    p.groundKills = 5;
    p.missionsWon = 9;
    p.mpKills = 7;
    p.mpDeaths = 3;
    p.mpWins = 2;
    p.mpMatches = 4;
    p.settings.invertPitch = true;
    p.settings.cameraShake = false;
    p.settings.debugInfo = false;
    p.settings.sfxVolume = 3;
    p.settings.voiceVolume = 4;
    p.settings.uiVolume = 5;
    p.vehicles[0].livery = 1;
    p.vehicles[0].loadout = 0x0B;
    p.vehicles[0].sorties = 40;
    p.vehicles[1].owned = true;
    p.vehicles[1].sorties = 1;
    p.vehicles[2].owned = true;
    p.vehicles[15].sorties = 0xFFFFFFFEu;  // unused slot: kept verbatim
    completeMission(p, 0, 0, Difficulty::Easy, 0, 0);
    completeMission(p, 0, 0, Difficulty::Hard, 0, 0);
    completeMission(p, 0, 1, Difficulty::Normal, 0, 0);
    completeMission(p, 0, 2, Difficulty::Easy, 0, 0);
    p.balance = 123456;  // completeMission paid out: pin the value again
    return p;
}

// --- Content ---------------------------------------------------------------

void testVehicles() {
    for (int i = 0; i < kVehicleCount; ++i) {
        const VehicleInfo& v = vehicle(static_cast<VehicleId>(i));
        CHECK(static_cast<int>(v.id) == i, "vehicle %d: id matches its index", i);
        CHECK(v.name && v.fullName && v.role && v.name[0] && v.fullName[0] && v.role[0], "vehicle %d: texts", i);
        CHECK(strLen(v.name) <= 10 && strLen(v.fullName) <= 24 && strLen(v.role) <= 28, "vehicle %d: text length", i);
        CHECK(isUpperText(v.name) && isUpperText(v.fullName) && isUpperText(v.role), "vehicle %d: uppercase", i);
        CHECK(v.liveryCount >= 1 && v.liveryNames, "vehicle %d: liveries", i);
        for (int k = 0; k < v.liveryCount; ++k) CHECK(v.liveryNames[k] && v.liveryNames[k][0], "vehicle %d livery %d", i, k);
        CHECK(v.maxSpeedKmh > 0 && v.ceilingM > 0 && v.maxG > 0.0f && v.rangeKm > 0, "vehicle %d: figures", i);
        CHECK(v.schematicRow == i, "vehicle %d: schematic row %d", i, v.schematicRow);
        CHECK(v.price >= 0, "vehicle %d: price", i);
    }
    const VehicleInfo& su35 = vehicle(VehicleId::Su35S);
    CHECK(std::strcmp(su35.name, "SU-35S") == 0, "Su-35S name");
    CHECK(su35.type == VehicleType::Plane && su35.available && su35.price == 0, "Su-35S: plane, available, free");
    CHECK(su35.liveryCount == 2 && std::strcmp(su35.liveryNames[0], "VKS GREY") == 0 &&
              std::strcmp(su35.liveryNames[1], "901 DIGITAL") == 0,
          "Su-35S liveries keep the texture order");
    const VehicleInfo& su57 = vehicle(VehicleId::Su57);
    CHECK(su57.type == VehicleType::Plane && !su57.available && su57.price == 900000 && su57.liveryCount == 1,
          "Su-57: plane, not available, 900000");
    const VehicleInfo& ka52 = vehicle(VehicleId::Ka52);
    CHECK(ka52.type == VehicleType::Helicopter && !ka52.available && ka52.price == 650000 && ka52.liveryCount == 1,
          "Ka-52: helicopter, not available, 650000");
    CHECK(&vehicle(VehicleId::Count) == &vehicle(VehicleId::Su35S), "a bad id falls back to the Su-35S");
}

void testNames() {
    CHECK(std::strcmp(weatherName(Weather::Overcast), "OVERCAST") == 0, "weather name");
    CHECK(std::strcmp(weatherName(Weather::Clear), "CLEAR") == 0 && std::strcmp(weatherName(Weather::Snow), "SNOW") == 0,
          "weather ends");
    CHECK(std::strcmp(timeName(TimeOfDay::Dusk), "DUSK") == 0 && std::strcmp(timeName(TimeOfDay::Dawn), "DAWN") == 0 &&
              std::strcmp(timeName(TimeOfDay::Day), "DAY") == 0 && std::strcmp(timeName(TimeOfDay::Night), "NIGHT") == 0,
          "time names");
    CHECK(std::strcmp(difficultyName(Difficulty::Easy), "EASY") == 0 &&
              std::strcmp(difficultyName(Difficulty::Normal), "NORMAL") == 0 &&
              std::strcmp(difficultyName(Difficulty::Hard), "HARD") == 0,
          "difficulty names");
    for (int w = 0; w <= static_cast<int>(Weather::Snow); ++w) CHECK(isUpperText(weatherName(static_cast<Weather>(w))), "weather %d", w);
}

void testCampaignContent() {
    CHECK(locationCount() == 4, "4 locations: %d", locationCount());
    static const char* const ids[] = {"north_sea", "volcanic_isles", "red_canyons", "glacier_range"};
    static const char* const names[] = {"NORTHERN SEA", "VOLCANIC ISLES", "RED CANYONS", "GLACIER RANGE"};
    static const char* const prefixes[] = {"NS", "VI", "RC", "GR"};
    int total = 0, heliVariants = 0, lastReward = 0, lastAir = 0, maxAir = 0;
    bool weatherSeen[6] = {}, timeSeen[4] = {};
    const char* codes[64];
    int codeCount = 0;

    for (int l = 0; l < locationCount(); ++l) {
        const Location& loc = location(l);
        CHECK(std::strcmp(loc.id, ids[l]) == 0, "location %d id %s", l, loc.id);
        CHECK(std::strcmp(loc.name, names[l]) == 0, "location %d name %s", l, loc.name);
        CHECK(loc.subtitle && loc.subtitle[0] && strLen(loc.subtitle) <= 40 && isUpperText(loc.subtitle), "location %d subtitle", l);
        CHECK(strLen(loc.name) <= 16, "location %d name length", l);
        CHECK(loc.missionCount == 4 && loc.missionCount <= kMaxMissions, "location %d: %d missions", l, loc.missionCount);
        CHECK(loc.missions != nullptr, "location %d missions", l);

        for (int m = 0; m < loc.missionCount; ++m) {
            const Mission& ms = loc.missions[m];
            ++total;
            char expect[8];
            std::snprintf(expect, sizeof(expect), "%s-%02d", prefixes[l], m + 1);
            CHECK(std::strcmp(ms.code, expect) == 0, "code %s, expected %s", ms.code, expect);
            for (int i = 0; i < codeCount; ++i) CHECK(std::strcmp(codes[i], ms.code) != 0, "duplicate code %s", ms.code);
            if (codeCount < 64) codes[codeCount++] = ms.code;

            CHECK(ms.name && ms.name[0] && strLen(ms.name) <= 18 && isUpperText(ms.name), "%s: name", ms.code);
            CHECK(ms.briefing && strLen(ms.briefing) >= 20 && strLen(ms.briefing) <= 150, "%s: briefing length %d", ms.code,
                  ms.briefing ? static_cast<int>(strLen(ms.briefing)) : -1);
            CHECK(countChar(ms.briefing, '\n') <= 2, "%s: briefing lines", ms.code);
            CHECK(static_cast<int>(ms.weather) <= static_cast<int>(Weather::Snow), "%s: weather", ms.code);
            CHECK(static_cast<int>(ms.time) <= static_cast<int>(TimeOfDay::Night), "%s: time", ms.code);
            weatherSeen[static_cast<int>(ms.weather)] = true;
            timeSeen[static_cast<int>(ms.time)] = true;

            CHECK(ms.reward >= 15000 && ms.reward <= 70000 && ms.reward % 1000 == 0, "%s: reward %d", ms.code, ms.reward);
            CHECK(ms.reward >= lastReward, "%s: rewards grow (%d after %d)", ms.code, ms.reward, lastReward);
            lastReward = ms.reward;

            CHECK(ms.variantCount == 1 || ms.variantCount == 2, "%s: variantCount %d", ms.code, ms.variantCount);
            const MissionVariant* plane = missionVariant(ms, VehicleType::Plane);
            const MissionVariant* heli = missionVariant(ms, VehicleType::Helicopter);
            CHECK(plane != nullptr, "%s: every mission has a plane variant", ms.code);
            CHECK((plane != nullptr) + (heli != nullptr) == ms.variantCount, "%s: variants consistent", ms.code);
            for (int v = 0; v < ms.variantCount; ++v) {
                const MissionVariant& var = ms.variants[v];
                CHECK(var.objective && var.objective[0] && strLen(var.objective) <= 30 && isUpperText(var.objective),
                      "%s: objective", ms.code);
                CHECK(var.airEnemies + var.groundTargets > 0, "%s: variant %d has nothing to destroy", ms.code, v);
                // The objective line names the counts the flight will track.
                if (var.airEnemies) {
                    CHECK(std::strchr(var.objective, static_cast<char>('0' + var.airEnemies)) != nullptr,
                          "%s: objective '%s' mentions %d aircraft", ms.code, var.objective, var.airEnemies);
                }
                if (var.groundTargets) {
                    CHECK(std::strchr(var.objective, static_cast<char>('0' + var.groundTargets)) != nullptr,
                          "%s: objective '%s' mentions %d targets", ms.code, var.objective, var.groundTargets);
                }
            }
            if (plane) {
                CHECK(plane->airEnemies >= 1 && plane->airEnemies <= 6, "%s: plane air enemies %d", ms.code, plane->airEnemies);
                // Ships on maps that have them (docs/decisions/0045), never more than the map's 3 slots.
                CHECK(plane->groundTargets <= 3, "%s: plane ground targets %d", ms.code, plane->groundTargets);
                CHECK(plane->airEnemies >= lastAir, "%s: opposition grows", ms.code);
                lastAir = plane->airEnemies;
                if (plane->airEnemies > maxAir) maxAir = plane->airEnemies;
            }
            if (heli) {
                ++heliVariants;
                CHECK(heli->groundTargets >= 3 && heli->groundTargets <= 8, "%s: heli ground targets %d", ms.code, heli->groundTargets);
                CHECK(heli->airEnemies <= 2, "%s: heli air enemies %d", ms.code, heli->airEnemies);
                CHECK(plane && std::strcmp(heli->objective, plane->objective) != 0, "%s: own objective line", ms.code);
            }
            CHECK(missionVariant(ms, VehicleType::Plane) == plane, "%s: lookup is stable", ms.code);
            if (ms.variantCount == 1) CHECK(heli == nullptr, "%s: no helicopter variant expected", ms.code);
        }
    }
    CHECK(total == 16, "16 missions: %d", total);
    CHECK(heliVariants >= 4 && heliVariants <= 6, "about 5 helicopter variants: %d", heliVariants);
    CHECK(location(0).missions[0].reward == 15000 && location(3).missions[3].reward == 70000, "reward range");
    CHECK(location(0).missions[0].variants[0].airEnemies == 1 && maxAir == 6, "opposition runs 1..6");
    int weathers = 0;
    for (bool b : weatherSeen) weathers += b;
    CHECK(weathers >= 5, "weather varies: %d kinds", weathers);
    for (int t = 0; t < 4; ++t) CHECK(timeSeen[t], "time of day %d is used", t);
    // The setting fits the place: snow in the glacier range, no snow on the red canyons.
    bool glacierSnow = false, canyonSnow = false;
    for (int m = 0; m < 4; ++m) {
        glacierSnow = glacierSnow || location(3).missions[m].weather == Weather::Snow;
        canyonSnow = canyonSnow || location(2).missions[m].weather == Weather::Snow;
    }
    CHECK(glacierSnow && !canyonSnow, "weather fits the location");
    CHECK(&location(-1) == &location(0) && &location(99) == &location(0), "a bad location index falls back to the first");
    // A mission that has only a plane variant must not offer a helicopter one.
    CHECK(missionVariant(location(0).missions[0], VehicleType::Helicopter) == nullptr, "NS-01 has no helicopter variant");
}

// --- Profile ---------------------------------------------------------------

void testReset() {
    Profile p;
    p.sorties = 99;
    p.airKills = 99;
    p.balance = 7;
    p.medals[1][1] = 7;
    p.vehicles[2].owned = true;
    p.settings.sfxVolume = 0;
    resetProfile(p, "MAVERICK");
    CHECK(std::strcmp(p.nickname, "MAVERICK") == 0, "nickname");
    CHECK(p.avatar == 0 && p.balance == 50000, "avatar 0, balance 50000: %d", static_cast<int>(p.balance));
    CHECK(p.vehicles[0].owned && p.vehicles[0].livery == 0 && p.vehicles[0].loadout == kLoadoutFull, "Su-35S owned, full loadout");
    CHECK(!p.vehicles[1].owned && !p.vehicles[2].owned, "only the Su-35S is owned");
    CHECK(p.sorties == 0 && p.airKills == 0 && p.groundKills == 0 && p.missionsWon == 0, "stats zero");
    CHECK(p.mpKills == 0 && p.mpDeaths == 0 && p.mpWins == 0 && p.mpMatches == 0, "multiplayer stats zero");
    CHECK(p.medals[1][1] == 0 && p.lastLocation == 0 && p.lastVehicle == VehicleId::Su35S, "progress reset");
    CHECK(!p.settings.invertPitch && p.settings.sfxVolume == 8 && p.settings.voiceVolume == 8 &&
              p.settings.uiVolume == 7 && p.settings.cameraShake && p.settings.debugInfo,
          "default settings");
    for (int i = 0; i < kMaxVehicles; ++i) CHECK(p.vehicles[i].sorties == 0, "slot %d sorties", i);
}

// RESET PROGRESS passes the profile's own callsign back in.
void testResetKeepsOwnNickname() {
    Profile p;
    resetProfile(p, "VIPER");
    p.balance = 12345;
    resetProfile(p, p.nickname);
    CHECK(std::strcmp(p.nickname, "VIPER") == 0, "nickname became '%s'", p.nickname);
    CHECK(p.balance != 12345, "progress not reset");
}

void testNickname() {
    Profile p;
    resetProfile(p, nullptr);
    CHECK(std::strcmp(p.nickname, "PILOT") == 0, "null -> PILOT");
    resetProfile(p, "");
    CHECK(std::strcmp(p.nickname, "PILOT") == 0, "empty -> PILOT");
    resetProfile(p, "ACE");
    CHECK(std::strcmp(p.nickname, "ACE") == 0, "short name kept");

    resetProfile(p, "12345678901234567890123");  // 23 bytes: fits exactly
    CHECK(strLen(p.nickname) == 23, "23 bytes fit: %d", static_cast<int>(strLen(p.nickname)));
    resetProfile(p, "123456789012345678901234567890");
    CHECK(strLen(p.nickname) == 23 && std::strncmp(p.nickname, "12345678901234567890123", 23) == 0, "ASCII cut at 23");
    CHECK(p.nickname[kNicknameBytes - 1] == 0, "always NUL-terminated");

    // Cyrillic: 2 bytes per letter, 23 bytes available -> 11 letters (22 bytes), never a half letter.
    const char* cyr = "\xD0\x95\xD0\xB3\xD0\xBE\xD1\x80\xD0\x9B\xD0\xB8\xD1\x85\xD0\xB0\xD1\x87\xD0\xB5\xD0\xB2"  // ЕгорЛихачев
                      "\xD0\x95\xD0\xB3\xD0\xBE\xD1\x80\xD0\x9B\xD0\xB8\xD1\x85\xD0\xB0\xD1\x87\xD0\xB5\xD0\xB2";
    resetProfile(p, cyr);
    CHECK(strLen(p.nickname) == 22, "Cyrillic cut on a letter boundary: %d bytes", static_cast<int>(strLen(p.nickname)));
    CHECK(validUtf8(p.nickname), "Cyrillic result is valid UTF-8");
    CHECK(std::strncmp(p.nickname, cyr, 22) == 0, "Cyrillic prefix kept");
    CHECK(p.nickname[22] == 0 && p.nickname[23] == 0, "rest zeroed");

    // 4-byte characters (emoji): 23 bytes -> 5 characters (20 bytes).
    const char* emoji = "\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xF0\x9F\x98\x80"
                        "\xF0\x9F\x98\x80\xF0\x9F\x98\x80";
    resetProfile(p, emoji);
    CHECK(strLen(p.nickname) == 20 && validUtf8(p.nickname), "4-byte characters: %d bytes", static_cast<int>(strLen(p.nickname)));
    // 3-byte characters (Japanese): 23 bytes -> 7 characters (21 bytes).
    const char* jp = "\xE3\x83\x91\xE3\x82\xA4\xE3\x83\xAD\xE3\x83\x83\xE3\x83\x88\xE3\x83\x91\xE3\x82\xA4\xE3\x83\xAD\xE3\x83\x83\xE3\x83\x88";
    resetProfile(p, jp);
    CHECK(strLen(p.nickname) == 21 && validUtf8(p.nickname), "3-byte characters: %d bytes", static_cast<int>(strLen(p.nickname)));
    // Exactly at the limit with a 2-byte letter straddling it: 22 ASCII + "Ж" (2 bytes) = 24 -> the letter is dropped.
    resetProfile(p, "1234567890123456789012\xD0\x96");
    CHECK(strLen(p.nickname) == 22 && validUtf8(p.nickname), "straddling letter dropped");
}

void testProgression() {
    Profile p;
    resetProfile(p, "T");
    CHECK(locationUnlocked(p, 0) && !locationUnlocked(p, 1) && !locationUnlocked(p, 3), "only location 0 is open");
    CHECK(missionUnlocked(p, 0, 0), "first mission open");
    CHECK(!missionUnlocked(p, 0, 1) && !missionUnlocked(p, 0, 3) && !missionUnlocked(p, 1, 0), "the rest are locked");
    CHECK(!missionUnlocked(p, -1, 0) && !missionUnlocked(p, 0, 4) && !missionUnlocked(p, 9, 0) && !locationUnlocked(p, 9),
          "out of range is locked");
    CHECK(clearedCount(p, 0) == 0 && !missionCleared(p, 0, 0), "nothing cleared");

    int start = p.balance;
    MissionReward r = completeMission(p, 0, 0, Difficulty::Normal, 3, 0);
    CHECK(r.firstClear && r.credits == 15000, "first clear pays full: %d", r.credits);
    CHECK(p.balance == start + 15000, "balance grows");
    CHECK(p.medals[0][0] == 0x02, "Normal sets bit 1: %02x", p.medals[0][0]);
    CHECK(missionCleared(p, 0, 0) && clearedCount(p, 0) == 1, "cleared");
    CHECK(missionUnlocked(p, 0, 1) && !missionUnlocked(p, 0, 2), "clearing unlocks the next one only");
    CHECK(p.missionsWon == 1, "missionsWon");
    CHECK(p.airKills == 0 && p.groundKills == 0 && p.sorties == 0, "completeMission does not count kills or sorties");
    CHECK(missionUnlocked(p, 0, 0), "a cleared mission stays open for replays");

    // Replay: a fifth.
    start = p.balance;
    r = completeMission(p, 0, 0, Difficulty::Normal, 1, 0);
    CHECK(!r.firstClear && r.credits == 3000, "replay pays a fifth: %d", r.credits);
    CHECK(p.balance == start + 3000 && p.missionsWon == 2, "replay balance and wins");

    // Difficulty scaling: x0.6 / x1.0 / x1.5, first clear of each difficulty is full.
    Profile q;
    resetProfile(q, "T");
    CHECK(completeMission(q, 0, 0, Difficulty::Easy, 0, 0).credits == 9000, "Easy first clear x0.6");
    CHECK(completeMission(q, 0, 0, Difficulty::Hard, 0, 0).credits == 22500, "Hard first clear x1.5");
    CHECK(completeMission(q, 0, 0, Difficulty::Normal, 0, 0).credits == 15000, "Normal still pays full after Easy/Hard");
    CHECK(q.medals[0][0] == 0x07, "all three medals: %02x", q.medals[0][0]);
    CHECK(completeMission(q, 0, 0, Difficulty::Easy, 0, 0).credits == 1800, "Easy replay");
    CHECK(completeMission(q, 0, 0, Difficulty::Hard, 0, 0).credits == 4500, "Hard replay");
    // Rounded to 100: NS-02 pays 19000 -> Easy replay 11400 / 5 = 2280 -> 2300.
    CHECK(completeMission(q, 0, 1, Difficulty::Easy, 0, 0).credits == 11400, "NS-02 Easy");
    CHECK(completeMission(q, 0, 1, Difficulty::Easy, 0, 0).credits == 2300, "NS-02 Easy replay rounds to 100");
    CHECK(completeMission(q, 0, 1, Difficulty::Hard, 0, 0).credits == 28500, "NS-02 Hard");
    CHECK(completeMission(q, 0, 1, Difficulty::Hard, 0, 0).credits == 5700, "NS-02 Hard replay");
    for (int l = 0; l < locationCount(); ++l) {
        for (int m = 0; m < location(l).missionCount; ++m) {
            Profile z;
            resetProfile(z, "T");
            for (int d = 0; d < kDifficultyCount; ++d) {
                int credits = completeMission(z, l, m, static_cast<Difficulty>(d), 0, 0).credits;
                CHECK(credits % 100 == 0 && credits > 0, "%s: credits %d round to 100", location(l).missions[m].code, credits);
            }
        }
    }

    // A location opens after the previous location's last mission.
    Profile c;
    resetProfile(c, "T");
    for (int m = 0; m < 3; ++m) completeMission(c, 0, m, Difficulty::Normal, 0, 0);
    CHECK(!locationUnlocked(c, 1) && !missionUnlocked(c, 1, 0), "location 2 stays locked until the last mission is cleared");
    CHECK(missionUnlocked(c, 0, 3), "the last mission is open");
    completeMission(c, 0, 3, Difficulty::Easy, 0, 0);
    CHECK(locationUnlocked(c, 1) && missionUnlocked(c, 1, 0) && !missionUnlocked(c, 1, 1), "location 2 opens, mission 1 only");
    CHECK(!locationUnlocked(c, 2), "location 3 still locked");
    CHECK(clearedCount(c, 0) == 4 && clearedCount(c, 1) == 0 && clearedCount(c, 9) == 0, "cleared counts");

    // Whole campaign on Normal pays the sum of the rewards.
    Profile full;
    resetProfile(full, "T");
    int sum = 0;
    for (int l = 0; l < locationCount(); ++l) {
        for (int m = 0; m < location(l).missionCount; ++m) {
            CHECK(missionUnlocked(full, l, m), "%s opens in order", location(l).missions[m].code);
            sum += completeMission(full, l, m, Difficulty::Normal, 0, 0).credits;
        }
    }
    CHECK(full.balance == 50000 + sum && sum == 680000, "campaign pays %d", sum);
    CHECK(full.missionsWon == 16, "16 wins");

    // Bad arguments change nothing.
    Profile bad;
    resetProfile(bad, "T");
    Profile before = bad;
    MissionReward none = completeMission(bad, 7, 0, Difficulty::Normal, 0, 0);
    CHECK(none.credits == 0 && !none.firstClear, "bad location: no reward");
    none = completeMission(bad, 0, 9, Difficulty::Normal, 0, 0);
    CHECK(none.credits == 0, "bad mission: no reward");
    none = completeMission(bad, 0, 0, Difficulty::Count, 0, 0);
    CHECK(none.credits == 0, "bad difficulty: no reward");
    sameOrReport(before, bad, "bad arguments");

    // The balance saturates instead of overflowing.
    Profile rich;
    resetProfile(rich, "T");
    rich.balance = 999999000;
    completeMission(rich, 0, 0, Difficulty::Hard, 0, 0);
    CHECK(rich.balance == 999999999, "balance capped: %d", static_cast<int>(rich.balance));
    rich.balance = 2147483000;
    completeMission(rich, 0, 1, Difficulty::Hard, 0, 0);
    CHECK(rich.balance == 999999999, "an out-of-range balance is capped too");
}

// The briefing's figure must be exactly what a win then pays.
void testPayoutMatchesReward() {
    Profile p;
    resetProfile(p, "PAYOUT");
    for (int d = 0; d < kDifficultyCount; ++d) {
        const Difficulty diff = static_cast<Difficulty>(d);
        for (int round = 0; round < 2; ++round) {  // first clear, then a replay
            const int shown = missionPayout(p, 0, 0, diff);
            const MissionReward r = completeMission(p, 0, 0, diff, 0, 0);
            CHECK(shown == r.credits, "difficulty %d round %d: shown %d, paid %d", d, round, shown, r.credits);
            CHECK(shown > 0, "difficulty %d round %d: zero payout", d, round);
        }
    }
    CHECK(missionPayout(p, 99, 0, Difficulty::Normal) == 0, "bad location pays");
}

void testEmblems() {
    Profile p;
    resetProfile(p, "E");
    CHECK(emblemCount() >= 20, "emblem count %d", emblemCount());
    CHECK(unlockedEmblemCount(p) == 4, "fresh pilot unlocks %d", unlockedEmblemCount(p));
    for (int i = 0; i < emblemCount(); ++i) {
        CHECK(emblem(i).name && emblem(i).name[0], "emblem %d unnamed", i);
        char text[64];
        describeUnlock(emblem(i), text, sizeof text);
        CHECK(text[0] != 0, "emblem %d no unlock text", i);
        if (i < 4) CHECK(emblemUnlocked(p, i) && unlockProgress(p, i) == 1.0f, "default %d locked", i);
    }
    // Kills: FIRST BLOOD at 1, MARKSMAN at 10.
    p.airKills = 9;
    CHECK(emblemUnlocked(p, 4) && !emblemUnlocked(p, 5), "kill thresholds");
    CHECK(unlockProgress(p, 5) > 0.89f && unlockProgress(p, 5) < 0.91f, "progress %.2f", unlockProgress(p, 5));
    p.airKills = 10;
    CHECK(emblemUnlocked(p, 5), "marksman at 10");
    // Location cleared / on hard.
    for (int m = 0; m < location(0).missionCount; ++m) completeMission(p, 0, m, Difficulty::Normal, 0, 0);
    CHECK(emblemUnlocked(p, 10) && !emblemUnlocked(p, 14), "northern sea cleared, not on hard");
    for (int m = 0; m < location(0).missionCount; ++m) completeMission(p, 0, m, Difficulty::Hard, 0, 0);
    CHECK(emblemUnlocked(p, 14), "northern sea on hard");
    // A locked emblem in a save is put back to the first one.
    Profile q;
    resetProfile(q, "Q");
    q.avatar = 7;  // ACE OF SPADES: 100 kills
    uint8_t buf[kProfileSaveBytes];
    const size_t n = serialize(q, buf, sizeof buf);
    Profile r;
    CHECK(deserialize(r, buf, n) && r.avatar == 0, "locked emblem kept: %d", r.avatar);
}

void testSorties() {
    Profile p;
    resetProfile(p, "T");
    recordSortie(p, VehicleId::Su35S, 2, 1);
    recordSortie(p, VehicleId::Su35S, 0, 0);
    recordSortie(p, VehicleId::Su35S, -5, -5);  // never subtracts
    CHECK(p.sorties == 3 && p.vehicles[0].sorties == 3, "sorties counted");
    CHECK(p.airKills == 2 && p.groundKills == 1, "kills counted: %u/%u", static_cast<unsigned>(p.airKills),
          static_cast<unsigned>(p.groundKills));
    // A won mission: the flight reports its kills once, through recordSortie.
    completeMission(p, 0, 0, Difficulty::Normal, 3, 0);
    recordSortie(p, VehicleId::Su35S, 3, 0);
    CHECK(p.airKills == 5 && p.sorties == 4, "kills of a won mission counted once: %u", static_cast<unsigned>(p.airKills));
    recordSortie(p, VehicleId::Count, 1, 0);  // bad vehicle: totals only
    CHECK(p.sorties == 5 && p.airKills == 6 && p.lastVehicle == VehicleId::Su35S, "bad vehicle id");
    p.sorties = 0xFFFFFFFFu;
    recordSortie(p, VehicleId::Su35S, 0, 0);
    CHECK(p.sorties == 0xFFFFFFFFu, "counters saturate");
}

void testRank() {
    CHECK(kRankCount == 7, "7 ranks");
    CHECK(rankThreshold(0) == 0, "first rank needs nothing");
    for (int i = 1; i < kRankCount; ++i) CHECK(rankThreshold(i) > rankThreshold(i - 1), "threshold %d grows", i);
    static const char* const names[kRankCount] = {"LIEUTENANT", "SENIOR LIEUTENANT", "CAPTAIN",       "MAJOR",
                                                  "LIEUTENANT COLONEL", "COLONEL", "MAJOR GENERAL"};
    for (int i = 0; i < kRankCount; ++i) CHECK(std::strcmp(rankName(i), names[i]) == 0, "rank %d name %s", i, rankName(i));
    CHECK(rankThreshold(-3) == 0 && rankThreshold(99) == rankThreshold(kRankCount - 1), "threshold clamps");
    CHECK(std::strcmp(rankName(-1), "LIEUTENANT") == 0 && std::strcmp(rankName(50), "MAJOR GENERAL") == 0, "name clamps");

    Profile p;
    resetProfile(p, "T");
    CHECK(rankScore(p) == 0 && rankIndex(p) == 0, "a new pilot is a lieutenant");

    // Rank-up reported by completeMission: 2 + 2 = 4 points >= 3.
    MissionReward r = completeMission(p, 0, 0, Difficulty::Normal, 0, 0);
    CHECK(rankScore(p) == 2 && r.rankBefore == 0 && r.rankAfter == 0, "2 points: still a lieutenant");
    r = completeMission(p, 0, 1, Difficulty::Normal, 0, 0);
    CHECK(rankScore(p) == 4 && r.rankBefore == 0 && r.rankAfter == 1, "4 points: rank-up to senior lieutenant");

    // Best difficulty per mission, not a sum.
    Profile q;
    resetProfile(q, "T");
    completeMission(q, 0, 0, Difficulty::Easy, 0, 0);
    CHECK(rankScore(q) == 1, "Easy = 1");
    completeMission(q, 0, 0, Difficulty::Normal, 0, 0);
    CHECK(rankScore(q) == 2, "Normal replaces Easy: %d", rankScore(q));
    completeMission(q, 0, 0, Difficulty::Hard, 0, 0);
    CHECK(rankScore(q) == 3, "Hard = 3: %d", rankScore(q));
    completeMission(q, 0, 0, Difficulty::Easy, 0, 0);
    CHECK(rankScore(q) == 3, "a replay on Easy changes nothing");

    // Whole-campaign examples.
    Profile e, n, h;
    resetProfile(e, "T");
    resetProfile(n, "T");
    resetProfile(h, "T");
    clearAll(e, Difficulty::Easy);
    clearAll(n, Difficulty::Normal);
    clearAll(h, Difficulty::Hard);
    CHECK(rankScore(e) == 16 && rankIndex(e) == 3, "all Easy: %d points, rank %d (MAJOR)", rankScore(e), rankIndex(e));
    CHECK(rankScore(n) == 32 && rankIndex(n) == 5, "all Normal: %d points, rank %d (COLONEL)", rankScore(n), rankIndex(n));
    CHECK(rankScore(h) == 48 && rankIndex(h) == 6, "all Hard: %d points, rank %d (MAJOR GENERAL)", rankScore(h), rankIndex(h));
    // Nearly everything on Hard: 12 of 16 is enough, 11 is not.
    for (int hard = 10; hard <= 13; ++hard) {
        Profile m;
        resetProfile(m, "T");
        int done = 0;
        for (int l = 0; l < locationCount(); ++l) {
            for (int k = 0; k < location(l).missionCount; ++k, ++done) {
                completeMission(m, l, k, done < hard ? Difficulty::Hard : Difficulty::Normal, 0, 0);
            }
        }
        CHECK(rankScore(m) == 32 + hard, "mixed score %d", rankScore(m));
        CHECK((rankIndex(m) == 6) == (hard >= 12), "%d Hard missions: rank %d", hard, rankIndex(m));
    }

    // The rank never falls while the campaign is played in order.
    Profile w;
    resetProfile(w, "T");
    int last = 0;
    for (int l = 0; l < locationCount(); ++l) {
        for (int k = 0; k < location(l).missionCount; ++k) {
            completeMission(w, l, k, Difficulty::Normal, 0, 0);
            CHECK(rankIndex(w) >= last, "rank does not fall");
            last = rankIndex(w);
        }
    }
    // Garbage in unused slots does not count.
    Profile g;
    resetProfile(g, "T");
    g.medals[7][7] = 7;
    g.medals[0][5] = 7;
    CHECK(rankScore(g) == 0, "medals outside the catalog are ignored");
}

void testFavorite() {
    Profile p;
    resetProfile(p, "T");
    CHECK(favoriteVehicle(p) == VehicleId::Su35S, "no sorties -> last vehicle");
    p.lastVehicle = VehicleId::Ka52;
    CHECK(favoriteVehicle(p) == VehicleId::Ka52, "no sorties -> last vehicle (Ka-52)");

    resetProfile(p, "T");
    for (int i = 0; i < 3; ++i) recordSortie(p, VehicleId::Su35S, 0, 0);
    recordSortie(p, VehicleId::Ka52, 0, 0);
    CHECK(favoriteVehicle(p) == VehicleId::Su35S, "most sorties wins");
    CHECK(p.lastVehicle == VehicleId::Ka52, "recordSortie remembers the last vehicle");
    recordSortie(p, VehicleId::Ka52, 0, 0);
    recordSortie(p, VehicleId::Ka52, 0, 0);  // 3 : 3, Ka-52 flown last
    CHECK(favoriteVehicle(p) == VehicleId::Ka52, "tie -> last flown");
    p.lastVehicle = VehicleId::Su35S;
    CHECK(favoriteVehicle(p) == VehicleId::Su35S, "tie -> last flown (Su-35S)");
    // The last flown is not among the leaders: the lowest id of the tied ones.
    for (int i = 0; i < 3; ++i) recordSortie(p, VehicleId::Su57, 0, 0);
    recordSortie(p, VehicleId::Su57, 0, 0);  // Su-57 4, others 3
    CHECK(favoriteVehicle(p) == VehicleId::Su57, "clear leader");
    recordSortie(p, VehicleId::Su35S, 0, 0);
    recordSortie(p, VehicleId::Su35S, 0, 0);  // Su-35S 5
    recordSortie(p, VehicleId::Ka52, 0, 0);
    recordSortie(p, VehicleId::Ka52, 0, 0);   // Ka-52 5, Su-57 4, last = Ka-52
    CHECK(favoriteVehicle(p) == VehicleId::Ka52, "tie at the top -> last flown");
    p.lastVehicle = VehicleId::Su57;           // last flown is behind the leaders
    CHECK(favoriteVehicle(p) == VehicleId::Su35S, "last flown trails -> lowest id of the leaders");
}

void testBuying() {
    Profile p;
    resetProfile(p, "T");
    p.balance = 5000000;
    CHECK(!canBuy(p, VehicleId::Su57) && !buyVehicle(p, VehicleId::Su57), "not available: cannot buy at any balance");
    CHECK(!canBuy(p, VehicleId::Ka52) && !buyVehicle(p, VehicleId::Ka52), "Ka-52 not available");
    CHECK(p.balance == 5000000 && !p.vehicles[1].owned && !p.vehicles[2].owned, "failed purchases change nothing");
    CHECK(!canBuy(p, VehicleId::Su35S) && !buyVehicle(p, VehicleId::Su35S), "already owned");
    CHECK(!canBuy(p, VehicleId::Count), "bad id");

    // The rule itself, for every vehicle: available, not owned, enough credits.
    // (Today nothing is both available and unowned with a price, so this also
    // covers the shop the day a vehicle goes on sale.)
    for (int i = 0; i < kVehicleCount; ++i) {
        VehicleId id = static_cast<VehicleId>(i);
        const VehicleInfo& info = vehicle(id);
        const int balances[] = {0, info.price > 0 ? info.price - 1 : 0, info.price, info.price + 1};
        for (int owned = 0; owned < 2; ++owned) {
            for (int b : balances) {
                Profile t;
                resetProfile(t, "T");
                t.vehicles[i].owned = owned != 0;
                t.balance = b;
                bool expect = info.available && !owned && b >= info.price;
                CHECK(canBuy(t, id) == expect, "vehicle %d owned=%d balance=%d", i, owned, b);
                int before = t.balance;
                bool bought = buyVehicle(t, id);
                CHECK(bought == expect, "buy vehicle %d owned=%d balance=%d", i, owned, b);
                CHECK(t.balance == (expect ? before - info.price : before), "balance after buying vehicle %d", i);
                CHECK(t.vehicles[i].owned == (owned != 0 || expect), "ownership after buying vehicle %d", i);
            }
        }
    }
    // A free available vehicle that is not owned can always be taken.
    Profile f;
    resetProfile(f, "T");
    f.vehicles[0].owned = false;
    f.balance = 0;
    CHECK(canBuy(f, VehicleId::Su35S) && buyVehicle(f, VehicleId::Su35S) && f.vehicles[0].owned && f.balance == 0,
          "free vehicle");
}

// --- Save format -----------------------------------------------------------

void testCrcReference() {
    // The check value of CRC-32/ISO-HDLC (zlib): the reference below must be the same algorithm as the format's.
    const char* s = "123456789";
    CHECK(refCrc32(reinterpret_cast<const uint8_t*>(s), 9) == 0xCBF43926u, "reference CRC-32 check value");
}

void testSerializeRoundTrip() {
    Profile p = busyProfile();
    uint8_t buf[kProfileSaveBytes];
    std::memset(buf, 0xAA, sizeof(buf));
    size_t n = serialize(p, buf, sizeof(buf));
    CHECK(n == kSaveBytes && n <= kProfileSaveBytes, "serialized size %u", static_cast<unsigned>(n));
    CHECK(buf[n] == 0xAA, "nothing written past the end");
    CHECK(std::memcmp(buf, "FPRF", 4) == 0, "magic FPRF");
    CHECK(buf[kOffVersion] == 1 && buf[kOffVersion + 1] == 0, "version 1, little-endian");
    CHECK(buf[kOffPayloadSize] == 0x04 && buf[kOffPayloadSize + 1] == 0x01, "payload size 260 = 0x0104");
    uint32_t crc = refCrc32(buf, kOffCrc);
    uint32_t stored = static_cast<uint32_t>(buf[kOffCrc]) | (static_cast<uint32_t>(buf[kOffCrc + 1]) << 8) |
                      (static_cast<uint32_t>(buf[kOffCrc + 2]) << 16) | (static_cast<uint32_t>(buf[kOffCrc + 3]) << 24);
    CHECK(crc == stored, "stored CRC matches the reference: %08x vs %08x", static_cast<unsigned>(crc), static_cast<unsigned>(stored));

    // Spot checks of the documented layout.
    CHECK(std::strcmp(reinterpret_cast<const char*>(buf + kOffNickname), p.nickname) == 0, "nickname at its offset");
    CHECK(buf[kOffAvatar] == 2 && buf[kOffLastVehicle] == 1 && buf[kOffLastLocation] == 2, "avatar / last vehicle / last location");
    CHECK(buf[kOffBalance] == 0x40 && buf[kOffBalance + 1] == 0xE2 && buf[kOffBalance + 2] == 0x01 && buf[kOffBalance + 3] == 0,
          "balance 123456 little-endian");
    CHECK(buf[kOffSorties] == 41, "sorties");
    CHECK(buf[kOffSettings] == 0x01 && buf[kOffSettings + 1] == 3 && buf[kOffSettings + 2] == 4 && buf[kOffSettings + 3] == 5,
          "settings: invert only, volumes 3/4/5: %02x", buf[kOffSettings]);
    CHECK(buf[kOffMedals] == 0x05 && buf[kOffMedals + 1] == 0x02 && buf[kOffMedals + 2] == 0x01, "medals row 0: %02x %02x %02x",
          buf[kOffMedals], buf[kOffMedals + 1], buf[kOffMedals + 2]);
    CHECK(buf[kOffVehicles] == 1 && buf[kOffVehicles + 1] == 1 && buf[kOffVehicles + 2] == 0x0B && buf[kOffVehicles + 4] == 40,
          "vehicle 0 record");
    CHECK(buf[kOffVehicles + 8] == 1 && buf[kOffVehicles + 16] == 1, "vehicles 1 and 2 owned");

    Profile q;
    resetProfile(q, "SOMEONE ELSE");
    q.balance = 1;
    CHECK(deserialize(q, buf, n), "round trip loads");
    sameOrReport(p, q, "round trip");
    CHECK(q.vehicles[15].sorties == 0xFFFFFFFEu, "unused slot kept verbatim");

    // Same bytes every time; trailing padding (a fixed-size file) is fine.
    uint8_t again[kProfileSaveBytes];
    CHECK(serialize(p, again, sizeof(again)) == n && std::memcmp(buf, again, n) == 0, "deterministic output");
    uint8_t padded[kProfileSaveBytes];
    std::memset(padded, 0, sizeof(padded));
    std::memcpy(padded, buf, n);
    Profile r;
    CHECK(deserialize(r, padded, sizeof(padded)), "padded buffer loads");
    sameOrReport(p, r, "padded round trip");

    // A fresh profile round-trips too.
    Profile fresh, loaded;
    resetProfile(fresh, "FRESH");
    resetProfile(loaded, "OTHER");
    loaded.airKills = 5;
    n = serialize(fresh, buf, sizeof(buf));
    CHECK(n == kSaveBytes && deserialize(loaded, buf, n), "fresh profile loads");
    sameOrReport(fresh, loaded, "fresh round trip");

    // Buffer rules.
    CHECK(serialize(p, buf, kSaveBytes - 1) == 0, "capacity too small");
    CHECK(serialize(p, nullptr, 1024) == 0, "no buffer");
    CHECK(serialize(p, buf, kSaveBytes) == kSaveBytes, "exact capacity");

    // Bytes after the nickname's NUL are never written out.
    Profile dirty;
    resetProfile(dirty, "AB");
    std::memcpy(dirty.nickname + 3, "junk", 4);
    serialize(dirty, buf, sizeof(buf));
    bool clean = true;
    for (size_t i = kOffNickname + 2; i < kOffNickname + kNicknameBytes; ++i) clean = clean && buf[i] == 0;
    CHECK(clean, "stale bytes after the NUL are not saved");
    // An unterminated nickname is saved terminated.
    Profile full;
    resetProfile(full, "X");
    std::memset(full.nickname, 'Z', kNicknameBytes);
    serialize(full, buf, sizeof(buf));
    CHECK(buf[kOffNickname + kNicknameBytes - 1] == 0, "unterminated nickname is saved terminated");
}

void testRejects() {
    Profile good = busyProfile();
    uint8_t buf[kProfileSaveBytes];
    size_t n = serialize(good, buf, sizeof(buf));
    CHECK(n == kSaveBytes, "setup");

    // The target must come out untouched on every failure.
    Profile target;
    resetProfile(target, "KEEP");
    target.balance = 777;
    target.airKills = 12;
    target.medals[2][1] = 3;
    Profile snapshot = target;

    auto expectRejected = [&](const uint8_t* data, size_t size, const char* what) {
        bool ok = deserialize(target, data, size);
        CHECK(!ok, "%s: must be rejected", what);
        sameOrReport(snapshot, target, what);
    };

    uint8_t t[kProfileSaveBytes];
    std::memcpy(t, buf, n);
    expectRejected(nullptr, n, "null data");
    expectRejected(t, 0, "empty");
    expectRejected(t, 5, "5 bytes");
    expectRejected(t, 11, "header only");
    expectRejected(t, 100, "truncated mid-payload");
    expectRejected(t, n - 1, "one byte short");
    expectRejected(t, n - 4, "CRC missing");

    std::memcpy(t, buf, n);
    t[kOffCrc] ^= 0x01;
    expectRejected(t, n, "bad CRC");

    std::memcpy(t, buf, n);
    t[kOffBalance] ^= 0x10;
    expectRejected(t, n, "payload bit flipped");

    std::memcpy(t, buf, n);
    t[kOffMedals + 3] ^= 0x01;
    expectRejected(t, n, "medal bit flipped");

    std::memcpy(t, buf, n);
    t[0] = 'G';
    fixCrc(t);
    expectRejected(t, n, "wrong magic (CRC valid)");

    std::memcpy(t, buf, n);
    t[kOffVersion] = 2;
    fixCrc(t);
    expectRejected(t, n, "unknown newer version (CRC valid)");

    std::memcpy(t, buf, n);
    t[kOffVersion] = 0;
    fixCrc(t);
    expectRejected(t, n, "version 0 (CRC valid)");

    std::memcpy(t, buf, n);
    t[kOffPayloadSize] = 0x05;
    fixCrc(t);
    expectRejected(t, n, "wrong payload size (CRC valid)");

    std::memcpy(t, buf, n);
    t[kOffNickname + kNicknameBytes - 1] = 'X';
    fixCrc(t);
    expectRejected(t, n, "unterminated nickname (CRC valid)");

    // Control: the untouched bytes still load into the same target.
    std::memcpy(t, buf, n);
    CHECK(deserialize(target, t, n), "control: good bytes load");
    sameOrReport(good, target, "control");
}

void testSanitising() {
    Profile good = busyProfile();
    uint8_t buf[kProfileSaveBytes];
    size_t n = serialize(good, buf, sizeof(buf));
    // A valid CRC proves integrity, not sense: indexes are brought back in range.
    buf[kOffAvatar] = 200;
    buf[kOffLastVehicle] = 99;
    buf[kOffLastLocation] = 77;
    buf[kOffBalance + 3] = 0xFF;  // negative
    buf[kOffSettings + 1] = 200;
    buf[kOffSettings + 2] = 11;
    buf[kOffSettings + 3] = 10;
    buf[kOffMedals + 8 * 3 + 2] = 0xFF;
    buf[kOffVehicles + 1] = 9;          // livery of vehicle 0 (two liveries)
    buf[kOffVehicles + 2] = 0xFF;       // loadout of vehicle 0
    buf[kOffVehicles + 8 + 1] = 5;      // livery of vehicle 1 (one livery)
    buf[kOffVehicles] = 0;              // the starter aircraft cannot be lost
    fixCrc(buf);
    Profile p;
    resetProfile(p, "T");
    CHECK(deserialize(p, buf, n), "out-of-range values still load");
    CHECK(p.avatar == 0 && p.lastVehicle == VehicleId::Su35S && p.lastLocation == 0, "indexes back in range");
    CHECK(p.balance == 0, "negative balance -> 0: %d", static_cast<int>(p.balance));
    CHECK(p.settings.sfxVolume == kVolumeSteps && p.settings.voiceVolume == kVolumeSteps && p.settings.uiVolume == 10,
          "volumes clamp to 10");
    CHECK(p.medals[3][2] == 0x07, "medal bits masked to 3");
    CHECK(p.vehicles[0].livery == 0 && p.vehicles[0].loadout == kLoadoutFull, "livery and loadout clamp");
    CHECK(p.vehicles[1].livery == 0, "livery of a one-livery vehicle");
    CHECK(p.vehicles[0].owned, "the Su-35S stays owned");

    // An empty nickname becomes PILOT.
    n = serialize(good, buf, sizeof(buf));
    std::memset(buf + kOffNickname, 0, kNicknameBytes);
    fixCrc(buf);
    CHECK(deserialize(p, buf, n) && std::strcmp(p.nickname, "PILOT") == 0, "empty nickname -> PILOT");
}

// --- Loadouts --------------------------------------------------------------

void testLoadouts() {
    CHECK(stationMask(0) == 0, "no pairs");
    CHECK(stationMask(1u << kPairWingtip) == 0x003 && stationMask(1u << kPairOuter) == 0x00C &&
              stationMask(1u << kPairMiddle) == 0x030 && stationMask(1u << kPairInner) == 0x0C0 &&
              stationMask(1u << kPairIntake) == 0x300,
          "pair i -> bits 2i and 2i+1");
    CHECK(stationMask(kLoadoutFull) == 0x3FF, "full = all 10 stations");
    CHECK(stationMask(0xE0) == 0 && stationMask(0xFF) == 0x3FF, "bits above the 5 pairs are ignored");

    CHECK(loadoutPresetCount() >= 4, "at least 4 presets");
    CHECK(std::strcmp(loadoutPreset(0).name, "BALANCED") == 0 && loadoutPreset(0).mask == kLoadoutFull, "BALANCED");
    CHECK(std::strcmp(loadoutPreset(1).name, "DOGFIGHT") == 0, "DOGFIGHT");
    CHECK(std::strcmp(loadoutPreset(2).name, "INTERCEPT") == 0, "INTERCEPT");
    CHECK(std::strcmp(loadoutPreset(3).name, "LIGHT") == 0, "LIGHT");
    CHECK(loadoutPreset(1).mask == ((1u << kPairWingtip) | (1u << kPairOuter) | (1u << kPairInner)), "DOGFIGHT pairs");
    CHECK(loadoutPreset(2).mask == ((1u << kPairWingtip) | (1u << kPairMiddle) | (1u << kPairInner) | (1u << kPairIntake)),
          "INTERCEPT pairs");
    CHECK(loadoutPreset(3).mask == ((1u << kPairWingtip) | (1u << kPairInner)), "LIGHT pairs");

    // Missile counts by type, read from the real station table.
    static const int expectR73[] = {4, 4, 2, 2};
    static const int expectR77[] = {6, 2, 6, 2};
    for (int i = 0; i < 4; ++i) {
        uint16_t stations = stationMask(loadoutPreset(i).mask);
        int r73 = 0, r77 = 0;
        for (int s = 0; s < game::kStationCount; ++s) {
            if (!(stations & (1u << s))) continue;
            if (game::kSu35SStations[s].type == &game::kR73) ++r73;
            if (game::kSu35SStations[s].type == &game::kR77) ++r77;
        }
        CHECK(r73 == expectR73[i] && r77 == expectR77[i], "%s: %dxR-73 + %dxR-77", loadoutPreset(i).name, r73, r77);
    }
    for (int i = 0; i < loadoutPresetCount(); ++i) {
        const LoadoutPreset& a = loadoutPreset(i);
        CHECK(a.name && a.name[0] && strLen(a.name) <= 12 && isUpperText(a.name), "preset %d name", i);
        CHECK(a.mask != 0 && (a.mask & ~kLoadoutFull) == 0, "preset %d mask %02x", i, a.mask);
        for (int k = i + 1; k < loadoutPresetCount(); ++k) {
            CHECK(a.mask != loadoutPreset(k).mask && std::strcmp(a.name, loadoutPreset(k).name) != 0, "presets %d and %d differ", i, k);
        }
    }
    CHECK(&loadoutPreset(-1) == &loadoutPreset(0) && &loadoutPreset(99) == &loadoutPreset(0), "bad index falls back to the first");

    CHECK(missionEnemy(Difficulty::Easy) == game::BotMode::Easy, "Easy -> bot Easy");
    CHECK(missionEnemy(Difficulty::Normal) == game::BotMode::Medium, "Normal -> bot Medium");
    CHECK(missionEnemy(Difficulty::Hard) == game::BotMode::Hard, "Hard -> bot Hard");
    for (int d = 0; d < kDifficultyCount; ++d) CHECK(game::isCombat(missionEnemy(static_cast<Difficulty>(d))), "mission bots shoot back");

    SortieConfig cfg;
    CHECK(cfg.mode == SortieMode::FreeFlight && cfg.loadout == kLoadoutFull && cfg.mission == -1, "SortieConfig defaults");
}

}  // namespace

int main() {
    testVehicles();
    testNames();
    testCampaignContent();
    testReset();
    testNickname();
    testResetKeepsOwnNickname();
    testProgression();
    testPayoutMatchesReward();
    testEmblems();
    testSorties();
    testRank();
    testFavorite();
    testBuying();
    testCrcReference();
    testSerializeRoundTrip();
    testRejects();
    testSanitising();
    testLoadouts();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all meta checks passed\n");
    return 0;
}
