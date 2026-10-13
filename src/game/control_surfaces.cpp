#include "game/control_surfaces.h"

#include <algorithm>
#include <cmath>

namespace game {

using math::clamp;
using math::radians;
using math::smoothstep;

namespace {
// Su-35S travel limits and actuator speeds (approximate public figures).
const float kStabPitch = radians(18), kStabRoll = radians(8);
const float kFlapRoll = radians(20), kFlapDroop = radians(15);
const float kSlatMax = radians(28);
const float kRudderYaw = radians(25), kRudderBrake = radians(30);
const float kNozzleMax = radians(15), kNozzleRoll = radians(6);
const float kSurfaceRate = radians(150);  // rad/s
const float kNozzleRate = radians(80);
}  // namespace

void ControlSurfaces::reset() {
    for (auto& a : angle_) a[0] = a[1] = 0.0f;
}

void ControlSurfaces::update(const FlightState& s, const ControlInput& in, float dt) {
    if (s.crashed) return;
    const float pitch = clamp(in.pitch, -1.0f, 1.0f);
    const float roll = clamp(in.roll, -1.0f, 1.0f);
    const float yaw = clamp(in.yaw, -1.0f, 1.0f);

    float target[kCount][2];
    auto pair = [&](Surface surface, float sym, float diff) {
        // Right roll: right side TE up (less lift), left side TE down.
        target[static_cast<int>(surface)][0] = sym - diff;
        target[static_cast<int>(surface)][1] = sym + diff;
    };
    // Pulling raises the stabilator trailing edges; roll adds a differential.
    pair(Surface::Stabilator, -pitch * kStabPitch, roll * kStabRoll);
    // Flaperons: roll, plus a landing-style droop when slow.
    pair(Surface::Flaperon, smoothstep(130.0f, 80.0f, s.speed) * kFlapDroop, roll * kFlapRoll);
    // Leading-edge flaps follow AoA (and droop a little when slow).
    float slat = std::min(kSlatMax, smoothstep(0.08f, 0.40f, std::fabs(s.alpha)) * radians(25) +
                                        smoothstep(140.0f, 90.0f, s.speed) * radians(10));
    pair(Surface::Slat, slat, 0.0f);
    // Su-35S has no dorsal airbrake: the rudders splay outward instead.
    // Yaw right moves both trailing edges right: outboard on the right fin.
    pair(Surface::Rudder, s.airbrake ? kRudderBrake : 0.0f, -yaw * kRudderYaw);
    // Thrust vectoring works hardest when the control surfaces run out of
    // dynamic pressure, and always in PSM. Nose up = exits up (negative).
    float tvc = s.psm ? 1.0f : clamp(1.0f - s.speed / 250.0f, 0.25f, 1.0f);
    pair(Surface::Nozzle, -pitch * kNozzleMax * tvc, roll * kNozzleRoll * tvc);

    for (int i = 0; i < kCount; ++i) {
        float rate = (i == static_cast<int>(Surface::Nozzle) ? kNozzleRate : kSurfaceRate) * dt;
        float limit = i == static_cast<int>(Surface::Nozzle) ? kNozzleMax : radians(40);
        for (int side = 0; side < 2; ++side) {
            float t = clamp(target[i][side], -limit, limit);
            angle_[i][side] += clamp(t - angle_[i][side], -rate, rate);
        }
    }
}

}  // namespace game
