#pragma once

#include <cstdint>

#include "game/armament.h"
#include "game/combat.h"
#include "game/gun.h"
#include "game/missile.h"
#include "game/radar.h"

namespace game {

// An enemy surface ship (docs/decisions/0045): a ground target of the
// missions that have them. It sails a route, turns and speeds up when
// attacked, and defends itself - but only against aircraft that come down
// to it: below kPatrolShip.ceiling and within its guns' and missiles' reach.
// Fighters dogfighting above are left alone (a gameplay rule, sized after
// real small-ship air defence: a 30 mm AK-630 is effective to ~4 km slant,
// MANPADS-class naval launchers to ~5-6 km).
struct ShipParams {
    const char* name;      // HUD: "SHIP"
    HitShape hull;         // the hull box (bodyHalf), no wing; ship axes (x right, y up, -z bow)
    float cruise, dash;    // m/s on the route / under attack
    float turnRate;        // rad/s
    float health;          // 100 = what a fighter has
    float shellFactor;     // share of a 30 mm round's damage it takes
    float ceiling;         // m: engages only aircraft below this
    float gunRange;        // m slant
    float samRange, samMinRange;  // m slant
    int sams;              // missiles aboard
    float samInterval;     // s between launches
    math::Vec3 gunMount, samMount, radarMount;  // ship axes, m
};

extern const ShipParams kPatrolShip;  // ~56 m missile/patrol corvette class
extern const GunParams kAK630;        // 30 mm six-barrel close-in gun
extern const MissileParams kIglaN;    // naval MANPADS-class IR missile (Igla family)
extern const RadarParams kShipRadar;  // its search / fire-control radar

class Ship {
public:
    static constexpr int kMaxMissiles = 4;
    static constexpr int kMaxWaypoints = 8;
    static constexpr float kSinkTime = 30.0f;  // s from "destroyed" to under the waves

    explicit Ship(const ShipParams& params = kPatrolShip);

    // Afloat at (x, z) heading `heading` (rad, 0 = -Z, + = towards +X), sailing
    // the closed route of `count` waypoints (x, z; y ignored).
    void reset(float x, float z, float heading, const math::Vec3* route, int count, uint32_t seed);
    // `targets[targetIndex]` is the aircraft it may engage (with the terrain);
    // its hits on it go to `hits`. `threatened`: a missile is coming at it.
    void update(const TargetList& targets, int targetIndex, bool threatened, HitQueue& hits, float dt);
    void hit(const HitEvent& e);

    const ShipParams& params() const { return p_; }
    bool afloat() const { return sink_ < kSinkTime; }      // still drawn
    bool alive() const { return health_ > 0.0f; }
    float sinkProgress() const { return sink_ / kSinkTime; }
    float health() const { return health_ / p_.health * 100.0f; }  // 0..100
    // For sensors and weapons: position (waterline), heading, velocity;
    // `crashed` once it is destroyed (radars and seekers drop it).
    const FlightState& state() const { return state_; }
    // For drawing: list (roll, + = to starboard) and trim (+ = bow up), rad, and
    // how far it has settled (m).
    float roll() const { return roll_; }
    float trim() const { return trim_; }
    float settle() const { return settle_; }
    bool engaging() const { return engaging_; }
    WeaponView weapons() const { return {missiles_, kMaxMissiles, &events_, &gun_}; }
    const Missile* missiles() const { return missiles_; }
    const Gun& gun() const { return gun_; }
    const Radar& radar() const { return radar_; }
    // Events (true once): it was destroyed this step.
    bool takeDestroyed() {
        bool d = destroyed_;
        destroyed_ = false;
        return d;
    }

private:
    void sail(bool threatened, float dt);
    void defend(const TargetList& targets, int target, HitQueue& hits, float dt);
    float random();  // -1..1, deterministic
    FlightState mount(const math::Vec3& at, const math::Vec3& dir) const;

    const ShipParams& p_;
    FlightState state_;
    math::Vec3 route_[kMaxWaypoints];
    int routeCount_ = 0, next_ = 0;
    float heading_ = 0.0f, speed_ = 0.0f;
    float weave_ = 0.0f, time_ = 0.0f;
    float health_ = 0.0f;
    float sink_ = 0.0f, roll_ = 0.0f, trim_ = 0.0f, settle_ = 0.0f, listSide_ = 1.0f;
    bool destroyed_ = false;
    bool engaging_ = false;
    Radar radar_;
    Gun gun_;
    float burst_ = 0.0f, pause_ = 0.0f;
    math::Vec3 aimError_;
    Missile missiles_[kMaxMissiles];
    int samsLeft_ = 0;
    float samTimer_ = 0.0f;
    WeaponEvents events_;
    uint32_t rng_ = 0x5417u;
};

}  // namespace game
