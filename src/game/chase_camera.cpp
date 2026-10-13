#include "game/chase_camera.h"

#include <algorithm>
#include <cmath>

namespace game {

using math::approach;
using math::clamp;
using math::Quat;
using math::Vec3;

namespace {
constexpr float kRotationLag = 4.0f;     // 1/s
constexpr float kHeight = 4.0f;          // m above the aircraft axis
constexpr float kLookAhead = 25.0f;      // m ahead of the aircraft
constexpr float kMinEyeAltitude = 3.0f;  // never dip under the waves
}  // namespace

Quat ChaseCamera::reference(const FlightState& s) const {
    // Blend from the nose to the flight path as AoA grows. The path frame
    // keeps the wings' axis, so it stays defined at 90 deg AoA.
    float w = math::smoothstep(0.35f, 1.0f, std::fabs(s.alpha));
    if (w <= 0.0f) return s.orientation;
    Vec3 pathUp = math::cross(s.orientation.right(), s.pathDir);
    if (math::length(pathUp) < 0.2f) return s.orientation;
    return math::nlerp(s.orientation, math::lookRotation(s.pathDir, pathUp), w);
}

void ChaseCamera::reset(const FlightState& s) {
    orient_ = reference(s);
    distance_ = 20.0f;
    fov_ = 60.0f;
    rumble_ = kick_ = crashLift_ = 0.0f;
    place(s);
}

void ChaseCamera::impulse(float amplitude) { kick_ = std::max(kick_, amplitude); }

void ChaseCamera::update(const FlightState& s, float dt) {
    time_ += dt;
    orient_ = math::nlerp(orient_, reference(s), approach(kRotationLag, dt));

    float fast = clamp((s.speed - 150.0f) / 400.0f, 0.0f, 1.0f);
    float boost = s.afterburner ? 1.0f : 0.0f;
    distance_ += (19.0f + 5.0f * fast + 3.0f * boost - distance_) * approach(2.0f, dt);
    fov_ += (58.0f + 12.0f * fast + 4.0f * boost - fov_) * approach(2.0f, dt);

    // Continuous rumble: heavy G, transonic buffet, post-stall flow breakdown.
    float target = 0.05f * math::smoothstep(6.0f, 11.0f, s.loadFactor) +
                   0.05f * math::smoothstep(0.9f, 0.97f, s.mach) * (1.0f - math::smoothstep(1.0f, 1.04f, s.mach)) +
                   0.04f * math::smoothstep(0.5f, 0.9f, std::fabs(s.alpha));
    rumble_ += (target - rumble_) * approach(6.0f, dt);
    kick_ *= std::exp(-5.0f * dt);
    // After a crash, rise above the splash instead of sitting in the waves.
    crashLift_ = s.crashed ? std::min(crashLift_ + 14.0f * dt, 22.0f) : 0.0f;
    place(s);
}

void ChaseCamera::place(const FlightState& s) {
    pose_.eye = s.position + orient_.rotate(Vec3{0.0f, kHeight, distance_});  // +Z is behind
    pose_.target = s.position + orient_.rotate(Vec3{0.0f, kHeight * 0.5f, -kLookAhead});
    pose_.up = orient_.up();

    // Shake: incommensurate sines (no RNG, deterministic), low frequencies
    // only - fast jitter turns into smear on the PSP-3000 LCD.
    float a = rumble_ + kick_;
    if (a > 1e-4f) {
        float t = time_;
        float sx = std::sin(t * 23.0f) * 0.6f + std::sin(t * 37.0f + 1.3f) * 0.4f;
        float sy = std::sin(t * 29.0f + 0.7f) * 0.6f + std::sin(t * 41.0f + 2.1f) * 0.4f;
        Vec3 offset = orient_.rotate(Vec3{sx * a, sy * a, 0.0f});
        pose_.eye += offset;
        pose_.target += offset * 0.4f;  // mostly rotational feel
    }
    pose_.eye.y = std::max(pose_.eye.y, kMinEyeAltitude) + crashLift_;
    pose_.fovDeg = fov_ + 30.0f * kick_;
}

}  // namespace game
