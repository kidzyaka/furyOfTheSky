#pragma once

#include "game/flight_model.h"

namespace game {

struct CameraPose {
    math::Vec3 eye;
    math::Vec3 target;
    math::Vec3 up{0, 1, 0};
    float fovDeg = 60.0f;
};

// Third-person camera on a "leash": it shares the aircraft's orientation with
// a lag, so turns and rolls reveal the aircraft's attitude, and it widens the
// field of view with speed for a sense of velocity. At high AoA it follows the
// flight path instead of the nose, so a cobra is seen from behind, not from
// below. Shake is deterministic (sums of sines) and deliberately small: the
// PSP-3000 LCD smears fine jitter (docs/psp/gotchas.md #21).
class ChaseCamera {
public:
    void reset(const FlightState& s);
    void update(const FlightState& s, float dt);
    // Short kick (afterburner light-up). Amplitude in metres.
    void impulse(float amplitude);
    const CameraPose& pose() const { return pose_; }

private:
    math::Quat reference(const FlightState& s) const;
    void place(const FlightState& s);

    math::Quat orient_;
    float distance_ = 20.0f;
    float fov_ = 60.0f;
    float rumble_ = 0.0f;  // continuous shake, m
    float kick_ = 0.0f;    // decaying impulse, m
    float time_ = 0.0f;
    float crashLift_ = 0.0f;  // m the camera has risen since a crash
    CameraPose pose_;
};

}  // namespace game
