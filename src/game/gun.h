#pragma once

#include <cstdint>

#include "game/combat.h"

namespace game {

// Aircraft cannon (docs/decisions/0026).
struct GunParams {
    math::Vec3 muzzle;     // body axes, m
    float muzzleVelocity;  // m/s
    float rate;            // rounds/s
    int rounds;            // magazine
    float dispersion;      // rad, 1-sigma-ish cone half-angle
    float dragK;           // 1/m: deceleration = dragK * v^2
    float life;            // s until the round self-destructs
    float damage;          // per hit (100 = a kill)
};

// GSh-30-1, 30 x 165 mm, in the right LERX root of the Su-35S.
extern const GunParams kGSh301;

struct Shell {
    math::Vec3 pos, vel;
    float age = 0.0f;
    bool live = false;
};

// Rounds in flight: ballistic (drag + gravity), checked against every
// airframe each step as swept segments, so nothing tunnels at 1000 m/s.
class Gun {
public:
    static constexpr int kMaxShells = 96;

    explicit Gun(const GunParams& params = kGSh301) : p_(params) {}

    void reset();
    // `trigger` held: fires at the cyclic rate while rounds last.
    // Hits go to `hits`; rounds that hit the sea are reported via splash().
    void update(const FlightState& aircraft, bool trigger, const TargetList& targets, HitQueue& hits, float dt);

    int rounds() const { return rounds_; }
    void reload() { rounds_ = p_.rounds; }
    bool firing() const { return firing_; }
    // Shots fired this step (muzzle flash, sound).
    int fired() const { return fired_; }
    const Shell& shell(int i) const { return shells_[i]; }
    // Rounds that went into the sea or a rock this step (for splashes and
    // dust; a rock hit is above the waterline): count + positions.
    int splashes() const { return splashCount_; }
    const math::Vec3& splash(int i) const { return splash_[i]; }
    const GunParams& params() const { return p_; }

    // Where a round fired now leaves to (world unit direction): the bore plus
    // the aircraft's own velocity - the gun cross of the HUD.
    math::Vec3 shellDirection(const FlightState& aircraft) const;
    // Lead solution against a target moving at constant velocity: the
    // direction the rounds must leave in to meet it (gravity drop included).
    // Returns false if it's out of reach (beyond the rounds' life).
    bool lead(const FlightState& aircraft, const math::Vec3& targetPos, const math::Vec3& targetVel,
              math::Vec3& outDir, float& outTime) const;

private:
    void fire(const FlightState& aircraft);

    const GunParams& p_;
    Shell shells_[kMaxShells];
    int next_ = 0;
    int rounds_ = 0;
    float timer_ = 0.0f;
    bool firing_ = false;
    int fired_ = 0;
    static constexpr int kMaxSplash = 8;
    math::Vec3 splash_[kMaxSplash];
    int splashCount_ = 0;
    uint32_t rng_ = 0x6511301u;
};

}  // namespace game
