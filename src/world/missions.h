#pragma once

// Where things are in a mission on its map (docs/decisions/0046): the
// player's entry, the ships' routes, how the enemy fighters come in. The
// mission's words (name, briefing, rewards, how many enemies) are in
// meta/catalog; this is the map side. Platform-neutral.

#include <cstdint>

#include "math/vec3.h"

namespace world {

struct ShipRoute {
    int count = 0;           // waypoints, a closed loop
    math::Vec3 points[6];    // x, z (y ignored), m; at least 500 m clear of every rock
};

struct MissionLayout {
    math::Vec3 playerStart{0.0f, 1500.0f, 14000.0f};
    float playerHeading = 0.0f;   // rad, 0 = -Z (towards the map centre from the south)
    float playerSpeed = 250.0f;   // m/s
    int ships = 0;                // how many routes are used (the mission's ground targets, <= 3)
    ShipRoute routes[3];
    bool lowLevel = false;        // enemy fighters come in low, under the radar horizon
};

// The layout of `mission` of `location`; a default one (entry from the south)
// for anything without its own.
const MissionLayout& missionLayout(int location, int mission);
// Free flight / training: the map centre at 900 m.
const MissionLayout& trainingLayout();

}  // namespace world
