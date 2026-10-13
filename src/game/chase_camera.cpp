#include "game/chase_camera.h"

#include <algorithm>

namespace game {

using math::approach;
using math::clamp;
using math::Vec3;

namespace {
constexpr float kRotationLag = 4.0f;     // 1/s
constexpr float kHeight = 4.0f;          // m above the aircraft axis
constexpr float kLookAhead = 25.0f;      // m ahead of the aircraft
constexpr float kMinEyeAltitude = 3.0f;  // never dip under the waves
}  // namespace

void ChaseCamera::reset(const FlightState& s) {
    orient_ = s.orientation;
    distance_ = 20.0f;
    fov_ = 60.0f;
    place(s);
}

void ChaseCamera::update(const FlightState& s, float dt) {
    orient_ = math::nlerp(orient_, s.orientation, approach(kRotationLag, dt));

    float fast = clamp((s.speed - 150.0f) / 400.0f, 0.0f, 1.0f);
    float boost = s.afterburner ? 1.0f : 0.0f;
    distance_ += (19.0f + 5.0f * fast + 3.0f * boost - distance_) * approach(2.0f, dt);
    fov_ += (58.0f + 12.0f * fast + 4.0f * boost - fov_) * approach(2.0f, dt);
    place(s);
}

void ChaseCamera::place(const FlightState& s) {
    pose_.eye = s.position + orient_.rotate(Vec3{0.0f, kHeight, distance_});  // +Z is behind
    pose_.eye.y = std::max(pose_.eye.y, kMinEyeAltitude);
    pose_.target = s.position + orient_.rotate(Vec3{0.0f, kHeight * 0.5f, -kLookAhead});
    pose_.up = orient_.up();
    pose_.fovDeg = fov_;
}

}  // namespace game
