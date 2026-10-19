#include "game/autopilot.h"

#include <algorithm>
#include <cmath>

namespace game {

using math::clamp;
using math::kGravity;
using math::Vec3;

ControlInput autopilot(const FlightState& s, const AircraftParams& p, const PilotDemand& d) {
    ControlInput in;
    if (s.crashed) return in;
    const Vec3 v = s.pathDir;

    // What the wings must produce: the demanded turn plus 1 g holding the path
    // up against gravity, normal to the path.
    Vec3 lift = d.turn + Vec3{0.0f, kGravity, 0.0f};
    lift -= v * math::dot(lift, v);
    const float need = math::length(lift);

    // Body "up" and "right" seen across the path (the FCS lifts along that up).
    Vec3 up = s.orientation.up();
    up -= v * math::dot(up, v);
    up = math::normalize(up);
    const Vec3 right = math::cross(v, up);

    // Roll the lift vector onto the demand. Bank error -> roll stick, with a
    // soft gain so the roll rate (200 deg/s) doesn't overshoot.
    float err = 0.0f;
    if (need > 0.2f) {
        const Vec3 dir = lift / need;
        err = std::atan2(math::dot(dir, right), math::dot(dir, up));
    }
    in.roll = clamp(err * 1.4f, -1.0f, 1.0f);

    // Pull: the neutral stick holds the trim (the share of 1 g along "up"),
    // full stick the G/AoA limiter. Only the part of the demand the lift
    // vector already points at is pulled for - no pulling while rolling over.
    const float q = p.liftK * s.speed * s.speed;
    const float gAvail = std::min(p.maxG, q * liftCoefficient(p, p.alphaMax) / kGravity);
    const float gTrim = up.y;
    const float gWant = std::min(need / kGravity, d.maxG) * std::max(std::cos(err), 0.0f);
    in.pitch = clamp((gWant - gTrim) / std::max(gAvail - gTrim, 0.3f), -0.3f, 1.0f);

    if (d.limiterOff) {
        // Full aft stick once the lift vector is roughly on the demand.
        in.psm = true;
        in.pitch = std::cos(err) > 0.5f ? 1.0f : 0.15f;
    }

    // Speed: the same buttons the player has (cruise power when neither).
    in.throttleUp = s.speed < d.speed - 8.0f;
    in.throttleDown = s.speed > d.speed + 25.0f;
    return in;
}

Vec3 turnTowards(const FlightState& s, const Vec3& dir, float g) {
    const Vec3 v = s.pathDir;
    Vec3 perp = dir - v * math::dot(dir, v);
    float len = math::length(perp);
    if (len < 1e-4f) {
        if (math::dot(dir, v) > 0.0f) return {};
        perp = s.orientation.up();  // straight behind: pull through
        len = 1.0f;
    }
    // Full demand from ~30 deg off; proportional closer in, so it settles.
    const float angle = std::atan2(len, math::dot(dir, v));
    return perp * (clamp(angle / 0.5f, 0.0f, 1.0f) * g * kGravity / len);
}

}  // namespace game
