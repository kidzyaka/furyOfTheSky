#pragma once

#include "game/aircraft_params.h"
#include "game/controls.h"
#include "math/quat.h"

namespace game {

// Complete simulation state of one aircraft. Plain data: copyable, comparable,
// serialisable - what a multiplayer snapshot will carry.
struct FlightState {
    math::Vec3 position;              // m, world
    math::Quat orientation;           // body -> world (where the nose points)
    math::Vec3 pathDir{0, 0, -1};     // unit flight-path (velocity) direction
    float speed = 0.0f;               // m/s along pathDir
    math::Vec3 bodyRates;             // rad/s: x = pitch (nose up), y = yaw (nose left), z = roll (left)
    float throttle = 0.0f;
    bool afterburner = false;
    bool airbrake = false;
    bool psm = false;                 // post-stall manoeuvring: FCS limiter off
    float alpha = 0.0f;               // rad, angle of attack (nose above the path)
    float beta = 0.0f;                // rad, sideslip (nose left of the path)
    float mach = 0.0f;
    float loadFactor = 1.0f;          // g along the body up axis (what the pilot feels)
    bool stalled = false;             // too slow to hold 1 g, or departed past the stall AoA
    bool crashed = false;

    math::Vec3 velocity() const { return pathDir * speed; }
};

// Lift coefficient over the full circle of AoA: attached flow (tanh up to
// clMax) blending into flat-plate separated flow past alphaStall.
float liftCoefficient(const AircraftParams& p, float alpha);
// Inverse of the attached-flow part: AoA giving `cl` (used by the FCS).
float alphaForLift(const AircraftParams& p, float cl);
// Transonic drag rise, 0..1 (1 at Mach 1).
float waveDragFactor(float mach);

// Arcade-physical flight model (docs/decisions/0014): point mass with lift,
// drag and thrust from the angle of attack, the nose driven by a fly-by-wire
// AoA/G command law - or directly by the stick in post-stall manoeuvring.
class FlightModel {
public:
    explicit FlightModel(const AircraftParams& params) : p_(params) {}

    // `orientation` is the attitude of the flight path; the model raises the
    // nose to the trim AoA so the aircraft starts in steady 1 g flight.
    void reset(const math::Vec3& position, const math::Quat& orientation, float speed);
    void step(const ControlInput& in, float dt);

    const FlightState& state() const { return s_; }
    const AircraftParams& params() const { return p_; }

private:
    float trimAlpha(const math::Vec3& liftDir, float q, const math::Vec3& otherForces) const;

    const AircraftParams& p_;
    FlightState s_;
};

}  // namespace game
