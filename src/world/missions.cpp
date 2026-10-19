#include "world/missions.h"

#include <initializer_list>

namespace world {

namespace {

MissionLayout makeTraining() {
    MissionLayout l;
    l.playerStart = {0.0f, 900.0f, 0.0f};
    return l;
}

// Ship routes (docs/assets/rocks.md has the map): one escort works up and
// down the Sound under the cliffs of Conachair and Mykines - its missiles
// can't see past them, so it is reached low along the water or over the
// cliffs; one patrols the lee of Foula; one the eastern islets.
ShipRoute route(std::initializer_list<math::Vec3> pts) {
    ShipRoute r;
    for (const math::Vec3& p : pts)
        if (r.count < 6) r.points[r.count++] = p;
    return r;
}

MissionLayout makeNorthSea(int mission) {
    MissionLayout l;
    switch (mission) {
        case 0:  // NS-01 FIRST CONTACT: in from the south at 1500 m, the islands ahead.
            l.playerStart = {0.0f, 1500.0f, 11500.0f};
            break;
        case 1:  // NS-02 SEA WOLF: dawn, two raiders hugging the waves among the stacks.
            l.playerStart = {-2500.0f, 900.0f, 10500.0f};
            l.playerHeading = 0.15f;
            l.lowLevel = true;
            break;
        case 2:  // NS-03 ICEBREAKER: patrol ships in the Sound, off Foula and in the east, fighters above.
            l.playerStart = {0.0f, 1200.0f, 12000.0f};
            l.ships = 3;
            l.routes[0] = route({{-3100.0f, 0.0f, -4400.0f}, {-1400.0f, 0.0f, -4300.0f}, {100.0f, 0.0f, -4150.0f},
                                 {1600.0f, 0.0f, -4350.0f}, {100.0f, 0.0f, -4150.0f}, {-1400.0f, 0.0f, -4300.0f}});
            l.routes[1] = route({{5900.0f, 0.0f, 2200.0f}, {5900.0f, 0.0f, -500.0f}});
            l.routes[2] = route({{8800.0f, 0.0f, 3000.0f}, {9300.0f, 0.0f, 1000.0f}});
            break;
        default:  // NS-04 STORM FRONT: in under the cloud base, into the rain.
            l.playerStart = {0.0f, 500.0f, 11500.0f};
            break;
    }
    return l;
}

}  // namespace

const MissionLayout& missionLayout(int location, int mission) {
    static const MissionLayout kNorthSea[4] = {makeNorthSea(0), makeNorthSea(1), makeNorthSea(2), makeNorthSea(3)};
    static const MissionLayout kDefault{};
    if (location == 0 && mission >= 0 && mission < 4) return kNorthSea[mission];
    return kDefault;
}

const MissionLayout& trainingLayout() {
    static const MissionLayout kTraining = makeTraining();
    return kTraining;
}

}  // namespace world
