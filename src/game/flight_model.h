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
    // Shot down: the airframe is gone (same end state as hitting the sea).
    void destroy() { s_.crashed = true; }
    // Swing the heading by `angle` rad about the world vertical (+ = left):
    // the attitude and the flight path turn together, speed, bank and climb
    // stay. The mission-area edge uses it to bring an aircraft back
    // (docs/decisions/0043).
    void turnHeading(float angle) {
        if (angle == 0.0f || s_.crashed) return;
        const math::Quat q = math::Quat::axisAngle(math::kUp, angle);
        s_.orientation = (q * s_.orientation).normalized();
        s_.pathDir = math::normalize(q.rotate(s_.pathDir));
    }
    // What hangs on the pylons, relative to the full missile load the
    // parameters are tuned for (docs/decisions/0038): 1, 1 = that load, the
    // model behaves exactly as without the call. `massRatio` = mass / full-load
    // mass: thrust and aerodynamic forces are per unit mass, so a lighter
    // aircraft accelerates, climbs and turns a little better. `dragRatio`
    // scales the zero-lift drag coefficient. Call every step (it changes as
    // missiles leave and are reloaded).
    void setStores(float massRatio, float dragRatio);

    const FlightState& state() const { return s_; }
    const AircraftParams& params() const { return p_; }

private:
    float trimAlpha(const math::Vec3& liftDir, float q, const math::Vec3& otherForces) const;

    const AircraftParams& p_;
    FlightState s_;
    float massInv_ = 1.0f;   // 1 / mass ratio: every force except gravity is per unit mass
    float dragScale_ = 1.0f;
};

}  // namespace game
