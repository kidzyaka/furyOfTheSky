#include "meta/catalog.h"

// Content tables (docs/decisions/0037). Plain static data: nothing here is
// allocated, so the tables are usable from any scene without setup.

namespace meta {

namespace {

// Livery order must match the game's texture order (scenes/assets.cpp).
const char* const kSu35Liveries[] = {"VKS GREY", "901 DIGITAL"};
const char* const kSu57Liveries[] = {"STANDARD"};
const char* const kKa52Liveries[] = {"STANDARD"};

// Figures: Wikipedia "Specifications" tables of each type (Su-35S, Su-57E,
// Ka-52), rounded; range = practical range without drop tanks. Sources in ADR 0037.
const VehicleInfo kVehicles[kVehicleCount] = {
    {VehicleId::Su35S, "SU-35S", "SU-35S FLANKER-E", "AIR SUPERIORITY FIGHTER", VehicleType::Plane,
     0, true, 2, kSu35Liveries, 2400, 18000, 9.0f, 3600, 0},
    {VehicleId::Su57, "SU-57", "SU-57 FELON", "STEALTH MULTIROLE FIGHTER", VehicleType::Plane,
     900000, false, 1, kSu57Liveries, 2100, 18800, 9.0f, 3500, 1},
    {VehicleId::Ka52, "KA-52", "KA-52 ALLIGATOR", "ATTACK HELICOPTER", VehicleType::Helicopter,
     650000, false, 1, kKa52Liveries, 300, 5500, 3.0f, 460, 2},
};

const char* const kWeatherNames[] = {"CLEAR", "CLOUDY", "OVERCAST", "FOG", "RAIN", "SNOW"};
const char* const kTimeNames[] = {"DAWN", "DAY", "DUSK", "NIGHT"};
const char* const kDifficultyNames[] = {"EASY", "NORMAL", "HARD"};

// Air enemies grow 1..6 through the campaign, rewards 15000..70000 on NORMAL.
// Every mission has a plane variant; some also have a helicopter one (ground
// targets, fewer or no aircraft). The setting is fictional: "the enemy".
// Variant layout: {type, airEnemies, groundTargets, objective}.

// --- NORTHERN SEA: the ocean map with its sea stacks (docs/decisions/0046;
// where things are on the map: world/missions.cpp).
const Mission kNorthSea[] = {
    {"NS-01", "FIRST CONTACT",
     "A lone hostile fighter is probing our patrol zone over the stacks.\nIntercept and destroy it.",
     Weather::Clear, TimeOfDay::Day, 15000, 1,
     {{VehicleType::Plane, 1, 0, "DESTROY 1 FIGHTER"}}},
    {"NS-02", "SEA WOLF",
     "Two raiders hug the waves among the sea stacks to stay under our radar.\nGo down after them.",
     Weather::Cloudy, TimeOfDay::Dawn, 19000, 1,
     {{VehicleType::Plane, 2, 0, "DESTROY 2 FIGHTERS"}}},
    {"NS-03", "ICEBREAKER",
     "Patrol ships guard the islands - one hides in the Sound.\nThey fire on anything flying low. Use the cliffs for cover.",
     Weather::Overcast, TimeOfDay::Day, 22000, 2,
     {{VehicleType::Plane, 2, 3, "DESTROY 2 FIGHTERS, 3 SHIPS"},
      {VehicleType::Helicopter, 0, 3, "DESTROY 3 PATROL BOATS"}}},
    {"NS-04", "STORM FRONT",
     "A hostile squadron hunts our patrols inside a storm front.\nRain, low cloud and lightning. Keep off the waves.",
     Weather::Rain, TimeOfDay::Dusk, 26000, 1,
     {{VehicleType::Plane, 3, 0, "DESTROY 3 FIGHTERS"}}},
};

// --- VOLCANIC ISLES.
const Mission kVolcanicIsles[] = {
    {"VI-01", "ASH CLOUD",
     "Volcanic ash hides an enemy flight over the isles.\nFlush it out and bring it down.",
     Weather::Cloudy, TimeOfDay::Day, 30000, 1,
     {{VehicleType::Plane, 3, 0, "DESTROY 3 FIGHTERS"}}},
    {"VI-02", "SMOKE SCREEN",
     "Smoke from the crater blankets the island chain at first light.\nHostile fighters use it as cover. Stay sharp.",
     Weather::Fog, TimeOfDay::Dawn, 33000, 1,
     {{VehicleType::Plane, 3, 0, "DESTROY 3 FIGHTERS"}}},
    {"VI-03", "CALDERA RUN",
     "The enemy holds an airfield inside the caldera.\nDestroy what flies and what stands before dark.",
     Weather::Clear, TimeOfDay::Dusk, 37000, 2,
     {{VehicleType::Plane, 4, 0, "DESTROY 4 FIGHTERS"},
      {VehicleType::Helicopter, 0, 4, "DESTROY 4 RADAR SITES"}}},
    {"VI-04", "EMBERFALL",
     "Night over the isles, and the lava glow lights the sky.\nHostile fighters scramble to meet you.",
     Weather::Clear, TimeOfDay::Night, 41000, 1,
     {{VehicleType::Plane, 4, 0, "DESTROY 4 FIGHTERS"}}},
};

// --- RED CANYONS.
const Mission kRedCanyons[] = {
    {"RC-01", "DUST DEVIL",
     "Enemy interceptors patrol the canyon rim.\nUse the clear air to engage them early.",
     Weather::Clear, TimeOfDay::Day, 44000, 1,
     {{VehicleType::Plane, 4, 0, "DESTROY 4 FIGHTERS"}}},
    {"RC-02", "CANYON RUN",
     "Fighters and supply columns use the canyons for cover at first light.\nFlush them out.",
     Weather::Clear, TimeOfDay::Dawn, 48000, 2,
     {{VehicleType::Plane, 4, 0, "DESTROY 4 FIGHTERS"},
      {VehicleType::Helicopter, 0, 5, "DESTROY 5 VEHICLES"}}},
    {"RC-03", "SANDSTORM",
     "A sandstorm cuts visibility to a few kilometers.\nThe enemy is flying blind too. Trust your radar.",
     Weather::Fog, TimeOfDay::Day, 52000, 1,
     {{VehicleType::Plane, 5, 0, "DESTROY 5 FIGHTERS"}}},
    {"RC-04", "RED SUNSET",
     "The enemy launches its strike group into the setting sun.\nStop it before it reaches the coast.",
     Weather::Clear, TimeOfDay::Dusk, 55000, 2,
     {{VehicleType::Plane, 5, 0, "DESTROY 5 FIGHTERS"},
      {VehicleType::Helicopter, 1, 6, "DESTROY 6 TANKS AND 1 JET"}}},
};

// --- GLACIER RANGE.
const Mission kGlacierRange[] = {
    {"GR-01", "WHITE OUT",
     "Snow turns the world white.\nAn enemy flight is somewhere ahead. Find it by radar.",
     Weather::Snow, TimeOfDay::Day, 59000, 1,
     {{VehicleType::Plane, 5, 0, "DESTROY 5 FIGHTERS"}}},
    {"GR-02", "FROZEN PEAKS",
     "Fog fills the valleys between the peaks.\nHostile fighters ambush from the ridges. Keep your speed up.",
     Weather::Fog, TimeOfDay::Dawn, 63000, 1,
     {{VehicleType::Plane, 5, 0, "DESTROY 5 FIGHTERS"}}},
    {"GR-03", "COLD FRONT",
     "A snow front rolls in at dusk and the enemy uses it for cover.\nShoot down the raiders before dark.",
     Weather::Snow, TimeOfDay::Dusk, 66000, 1,
     {{VehicleType::Plane, 6, 0, "DESTROY 6 FIGHTERS"}}},
    {"GR-04", "ZERO HOUR",
     "The enemy's last air group takes off in a night blizzard.\nThis ends here, pilot.",
     Weather::Snow, TimeOfDay::Night, 70000, 2,
     {{VehicleType::Plane, 6, 0, "DESTROY 6 FIGHTERS"},
      {VehicleType::Helicopter, 2, 8, "DESTROY 8 BUNKERS AND 2 JETS"}}},
};

#define META_COUNT(a) static_cast<int>(sizeof(a) / sizeof((a)[0]))

// Artwork exists under assets/ui/loc_<id>.ftx for each id.
const Location kLocations[] = {
    {"north_sea", "NORTHERN SEA", "COLD WATERS AND LONG PATROLS", META_COUNT(kNorthSea), kNorthSea},
    {"volcanic_isles", "VOLCANIC ISLES", "ASH, SMOKE AND SHORT RUNWAYS", META_COUNT(kVolcanicIsles), kVolcanicIsles},
    {"red_canyons", "RED CANYONS", "DUST, HEAT AND NARROW VALLEYS", META_COUNT(kRedCanyons), kRedCanyons},
    {"glacier_range", "GLACIER RANGE", "SNOW, FOG AND THIN AIR", META_COUNT(kGlacierRange), kGlacierRange},
};
constexpr int kLocationTotal = META_COUNT(kLocations);

#undef META_COUNT

static_assert(kLocationTotal <= kMaxLocations, "location table exceeds the save capacity");

}  // namespace

const VehicleInfo& vehicle(VehicleId id) {
    int i = static_cast<int>(id);
    if (i < 0 || i >= kVehicleCount) i = 0;  // a bad id falls back to the starter aircraft
    return kVehicles[i];
}

const char* weatherName(Weather w) {
    int i = static_cast<int>(w);
    return (i >= 0 && i < 6) ? kWeatherNames[i] : "UNKNOWN";
}

const char* timeName(TimeOfDay t) {
    int i = static_cast<int>(t);
    return (i >= 0 && i < 4) ? kTimeNames[i] : "UNKNOWN";
}

const char* difficultyName(Difficulty d) {
    int i = static_cast<int>(d);
    return (i >= 0 && i < kDifficultyCount) ? kDifficultyNames[i] : "UNKNOWN";
}

int locationCount() { return kLocationTotal; }

const Location& location(int index) {
    if (index < 0 || index >= kLocationTotal) index = 0;
    return kLocations[index];
}

const MissionVariant* missionVariant(const Mission& m, VehicleType type) {
    for (int i = 0; i < m.variantCount && i < 2; ++i) {
        if (m.variants[i].type == type) return &m.variants[i];
    }
    return nullptr;
}

}  // namespace meta

namespace meta {

namespace {

// Art: tools/textures/ui.py EMBLEMS (same order). Unlocks: docs/decisions/0039.
constexpr EmblemInfo kEmblems[] = {
    {"WINGED STAR", UnlockKind::Default, 0},
    {"CHEVRONS", UnlockKind::Default, 0},
    {"LIGHTNING", UnlockKind::Default, 0},
    {"COMPASS ROSE", UnlockKind::Default, 0},
    {"FIRST BLOOD", UnlockKind::AirKills, 1},
    {"MARKSMAN", UnlockKind::AirKills, 10},
    {"HAWK", UnlockKind::AirKills, 50},
    {"ACE OF SPADES", UnlockKind::AirKills, 100},
    {"WINGS", UnlockKind::Sorties, 25},
    {"IRON WINGS", UnlockKind::Sorties, 100},
    {"ANCHOR", UnlockKind::LocationCleared, 0},
    {"VOLCANO", UnlockKind::LocationCleared, 1},
    {"MESA", UnlockKind::LocationCleared, 2},
    {"SNOWFLAKE", UnlockKind::LocationCleared, 3},
    {"STORM", UnlockKind::LocationHard, 0},
    {"FLAME", UnlockKind::LocationHard, 1},
    {"DESERT SUN", UnlockKind::LocationHard, 2},
    {"ICE CRYSTAL", UnlockKind::LocationHard, 3},
    {"LAUREL STAR", UnlockKind::Rank, 2},
    {"WREATH", UnlockKind::Rank, 3},
    {"EAGLE", UnlockKind::Rank, 5},
    {"GENERAL STAR", UnlockKind::Rank, 6},
    {"SHIELD", UnlockKind::HardMedals, 8},
    {"TRIPLE CROWN", UnlockKind::HardMedals, 16},
    {"SHARK", UnlockKind::MissionsWon, 25},
    {"DAYBREAK", UnlockKind::MissionsWon, 50},
    {"ARROWHEAD", UnlockKind::MpWins, 1},
    {"SQUADRON", UnlockKind::MpWins, 10},
};
constexpr int kEmblemTotal = sizeof kEmblems / sizeof kEmblems[0];

}  // namespace

int emblemCount() { return kEmblemTotal; }

const EmblemInfo& emblem(int index) {
    if (index < 0 || index >= kEmblemTotal) index = 0;
    return kEmblems[index];
}

}  // namespace meta
