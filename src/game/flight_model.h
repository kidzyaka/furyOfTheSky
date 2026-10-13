#pragma once

#include "game/aircraft_params.h"
#include "game/controls.h"
#include "math/quat.h"

namespace game {

// Complete simulation state of one aircraft. Plain data: copyable, comparable,
// serialisable - what a multiplayer snapshot will carry.
struct FlightState {
    math::Vec3 position;              // m, world
    math::Quat orientation;           // body -> world
    math::Vec3 pathDir{0, 0, -1};     // unit flight-path (velocity) direction
    float speed = 0.0f;               // m/s along pathDir
    math::Vec3 bodyRates;             // rad/s: x = pitch (nose up), y = yaw (nose left), z = roll (left)
    float throttle = 0.0f;
    bool afterburner = false;
    bool airbrake = false;
    float loadFactor = 1.0f;          // g, for HUD and induced drag
    bool stalled = false;
    bool crashed = false;

    math::Vec3 velocity() const { return pathDir * speed; }
};

// Arcade flight model (docs/decisions/0010): the flight path follows the nose
// with a lag, speed is an energy budget (thrust, drag, gravity along the path),
// turn rate is capped by control authority at low speed and by G at high speed.
class FlightModel {
public:
    explicit FlightModel(const AircraftParams& params) : p_(params) {}

    void reset(const math::Vec3& position, const math::Quat& orientation, float speed);
    void step(const ControlInput& in, float dt);

    const FlightState& state() const { return s_; }
    const AircraftParams& params() const { return p_; }

private:
    const AircraftParams& p_;
    FlightState s_;
};

}  // namespace game
