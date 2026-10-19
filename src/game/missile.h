#pragma once

#include <cstdint>

#include "game/combat.h"
#include "game/radar.h"
#include "math/quat.h"

namespace game {

enum class Guidance : uint8_t { Infrared, ActiveRadar };

// One missile type (docs/decisions/0026: sources for every number).
// Aerodynamics per unit mass: an accel of q*C, q = refK * v^2, C the force
// coefficient on the body's cross-section - the same bookkeeping as the
// aircraft's liftK. Drag = q * (cd0 + cn^2 / cnAlpha): turning costs energy.
struct MissileParams {
    const char* name;     // HUD
    Guidance guidance;
    float length, diameter;  // m
    float burnTime;       // s
    float boostAccel;     // m/s^2, mean thrust / mass during the burn
    float refK;           // 1/m, rho * S / (2 m)
    float cd0;            // zero-lift drag (with the base drag of a burnt-out motor)
    float cnAlpha;        // normal force per rad of AoA
    float cnMax;          // largest normal force the body and fins make
    float maxG;           // autopilot / structural limit
    float tvc;            // share of the thrust usable sideways while the motor burns (gas vanes)
    float response;       // 1/s, achieved acceleration follows the command
    float navGain;        // proportional navigation constant
    float guidanceDelay;  // s after the motor lights before it manoeuvres (clear of the launcher)
    // Seeker.
    float gimbal;         // rad off the missile axis (also the pre-launch slew limit)
    float fov;            // rad half-angle: IR instantaneous field / radar acquisition cone
    float seekerRange;    // m. IR: lock range on 1 unit of IR intensity (signal ~ I / r^2).
                          //    Radar: goes active this close to the predicted target.
    float trackRate;      // rad/s, fastest line-of-sight rate the head can follow
    float flareRejection; // 0..1, share of a flare's signal the IR counter-countermeasures discount
    float memory;         // s it flies on the last estimate after losing the target
    // Fuze and warhead.
    float armTime;        // s from launch (minimum range)
    float fuzeRadius;     // m, proximity fuze trigger
    float lethalRadius;   // m, a kill inside; the damage falls off to the fuze radius
    float maxTime;        // s, self-destruct
    float minSpeed;       // m/s, below it the fins can't hold it any more: self-destruct
    // On the pylon (docs/decisions/0038).
    float mass;           // kg, launch mass
    float carryDragArea;  // m^2, Cd*A it adds to the carrier while hanging (divide by the wing area for CD0)
    // Before launch (docs/decisions/0050).
    float lockTime;       // s from cueing to a lock: the head (IR) / the radar (R-77) acquires gradually
};

extern const MissileParams kR73;   // short range, IR, all-aspect
extern const MissileParams kR77;   // medium range, inertial + datalink, active radar terminal

enum class Launcher : uint8_t {
    Rail,     // motor lights on the rail and drives it off forwards (APU-73, APU-170)
    Ejector,  // kicked clear downwards, motor lights a moment later (AKU-170)
};

// What the seeker is following.
enum class SeekerTrack : uint8_t { None, Aircraft, Flare };

struct Missile {
    const MissileParams* type = nullptr;  // null: free slot
    math::Vec3 pos, vel;
    math::Quat orientation;  // for drawing: nose roughly along the velocity, AoA towards the turn
    math::Vec3 lateral;      // achieved manoeuvre acceleration, world
    float age = 0.0f;
    float ignition = 0.0f;   // s after launch the motor lights (ejected missiles)
    int target = -1;         // designated at launch (datalink), -1 none
    // Seeker / estimate of what it homes on.
    math::Vec3 aimPos, aimVel;
    bool active = false;     // radar seeker transmitting (always on for IR)
    SeekerTrack tracking = SeekerTrack::None;
    int trackIndex = -1;     // target index (aircraft or the flare's owner)
    float lost = 0.0f;       // s without a guidance source

    bool live() const { return type != nullptr; }
    bool burning() const { return age >= ignition && age < ignition + type->burnTime; }
};

// Why a missile's flight ended (for effects, sound and the HUD).
enum class MissileEnd : uint8_t { None, Proximity, Water, SelfDestruct, Terrain };

struct MissileResult {
    MissileEnd end = MissileEnd::None;
    const MissileParams* type = nullptr;
    math::Vec3 pos;    // where it detonated
    int target = -1;   // whose airframe the fuze fired on
    int aimedAt = -1;  // the target it was launched at (-1: none) - a miss is "MISS" only then
    float miss = 0.0f; // m, distance at detonation
    float damage = 0.0f;
};

// A fresh missile leaving `launcher` from the body-axes point `attach`.
Missile launchMissile(const MissileParams& type, Launcher launcher, const FlightState& aircraft,
                      const math::Vec3& attach, int target, const Track* datalink);

// One fixed step of flight: seeker, guidance, aerodynamics, fuze.
// `datalink`: the launcher's radar track of the missile's target, while it
// still has one (null otherwise).
MissileResult stepMissile(Missile& m, const TargetList& targets, const Track* datalink, float dt);

// IR seeker on the rail: looking along `cue` (world unit), can it lock on?
// The missile axis is the aircraft's nose. Returns the target index or -1;
// `outDir` gets the direction the head ends up looking.
int infraredLock(const MissileParams& type, const FlightState& aircraft, const math::Vec3& cue,
                 const TargetList& targets, math::Vec3& outDir);

// Warhead damage at miss distance `d`.
float missileDamage(const MissileParams& type, float d);

}  // namespace game
