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
// field of view with speed for a sense of velocity.
class ChaseCamera {
public:
    void reset(const FlightState& s);
    void update(const FlightState& s, float dt);
    const CameraPose& pose() const { return pose_; }

private:
    void place(const FlightState& s);

    math::Quat orient_;
    float distance_ = 20.0f;
    float fov_ = 60.0f;
    CameraPose pose_;
};

}  // namespace game
