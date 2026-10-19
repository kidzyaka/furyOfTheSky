#pragma once

#include <cstdint>

#include "game/countermeasures.h"
#include "game/flight_model.h"
#include "world/terrain.h"

namespace game {

constexpr int kMaxTargets = 4;

struct HitShape;

// Another aircraft - or a ship (docs/decisions/0045) - as sensors and weapons
// see it: its state, the decoys it has in the air, what it is. Pointers into
// the scene's objects, valid for one step.
struct Target {
    const FlightState* state = nullptr;
    const Countermeasures* flares = nullptr;
    // A ship: a huge radar echo and hot stacks from every side; the radars and
    // the R-77's seeker hold it in their sea-surface mode, where a slow target
    // isn't lost in the clutter notch (a gameplay compromise, ADR 0045); fuzes
    // and shells test its hull, not a Flanker's.
    bool surface = false;
    const HitShape* shape = nullptr;  // null: kFlankerShape
};

struct TargetList {
    Target items[kMaxTargets];
    int count = 0;
    // The rocks between (docs/decisions/0042): they block radar and seeker
    // lines of sight and stop missiles and shells. Null: open sea.
    const ::world::Terrain* terrain = nullptr;
    bool visible(const math::Vec3& from, const math::Vec3& to) const {
        return !terrain || terrain->lineOfSight(from, to);
    }
    void add(const FlightState* s, const Countermeasures* cm) {
        if (count < kMaxTargets) items[count++] = {s, cm, false, nullptr};
    }
    void addSurface(const FlightState* s, const HitShape* shape) {
        if (count < kMaxTargets) items[count++] = {s, nullptr, true, shape};
    }
};

// Something hit something this step: for the drone's damage tally, the HUD
// ("HIT"), sound and effects. Produced by the gun and the missiles.
enum class HitKind : uint8_t { Shell, Missile };
struct HitEvent {
    HitKind kind;
    int target;        // index in the TargetList
    math::Vec3 pos;    // world, where it struck / detonated
    float damage;      // 0..100+ (100 = a kill on a healthy fighter)
};

// Small fixed queue of this step's events (no allocation in the sim).
struct HitQueue {
    static constexpr int kMax = 16;
    HitEvent items[kMax];
    int count = 0;
    void clear() { count = 0; }
    void push(const HitEvent& e) {
        if (count < kMax) items[count++] = e;
    }
};

// Where an aircraft can be hit: two ellipsoids in body axes (fuselage with
// nacelles, wing with LERX) around the reference point. Su-35S sized.
struct HitShape {
    math::Vec3 bodyHalf{1.9f, 1.4f, 10.5f};
    math::Vec3 wingHalf{7.4f, 0.5f, 3.4f};
    math::Vec3 wingCentre{0.0f, 0.0f, 1.6f};
};
inline constexpr HitShape kFlankerShape{};
inline const HitShape& shapeOf(const Target& t) { return t.shape ? *t.shape : kFlankerShape; }

// Signatures of a target as a radar / an IR seeker sees it (docs/decisions/
// 0027; ships 0045). A 50-60 m patrol ship: an echo of ~1000 m^2, exhaust
// stacks about as bright as a fighter's dry-power tailpipes, from every side.
float targetRcs(const Target& t, const math::Vec3& toObserver);
float targetIr(const Target& t, const math::Vec3& toObserver);

// Closest point of the segment p0->p1 (world) to the airframe: returns the
// segment fraction of the first contact, or -1 if it misses.
float segmentHit(const FlightState& s, const HitShape& shape, const math::Vec3& p0, const math::Vec3& p1);

}  // namespace game
