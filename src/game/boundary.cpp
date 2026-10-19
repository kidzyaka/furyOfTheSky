#include "game/boundary.h"

#include <cmath>

namespace game {

void Boundary::reset() {
    zone_ = Zone::Inside;
    warnTime_ = authority_ = bearing_ = 0.0f;
}

float Boundary::update(const FlightState& s, const ::world::Arena& arena, float dt) {
    const float dx = s.position.x - arena.centreX, dz = s.position.z - arena.centreZ;
    const float dist = std::sqrt(dx * dx + dz * dz);
    // Heading and the way home, on the horizontal plane.
    math::Vec3 head{s.pathDir.x, 0.0f, s.pathDir.z};
    const float hl = math::length(head);
    head = hl > 1e-3f ? head / hl : s.orientation.forward();
    const math::Vec3 home = dist > 1.0f ? math::Vec3{-dx / dist, 0.0f, -dz / dist} : head;
    const float c = math::clamp(math::dot(head, home), -1.0f, 1.0f);
    const float side = math::cross(head, home).y;  // + : home is to the left
    bearing_ = -std::atan2(side, c);                 // + : home is to the right
    const float off = std::acos(c);                  // rad between the heading and home

    if (s.crashed) {
        reset();
        return 0.0f;
    }
    switch (zone_) {
        case Zone::Returning:
            // Hand the aircraft back once it points home and is inside the outer edge.
            if (off < kReleaseAngle && dist < arena.forceRadius) {
                zone_ = dist > arena.warnRadius ? Zone::Warning : Zone::Inside;
                warnTime_ = 0.0f;
            }
            break;
        case Zone::Warning:
            if (dist < arena.warnRadius - kHysteresis) {
                zone_ = Zone::Inside;
                warnTime_ = 0.0f;
            } else if (dist >= arena.forceRadius) {
                zone_ = Zone::Returning;
            } else {
                // The clock runs while it keeps going out; turning home winds it back.
                warnTime_ = off > 1.2f ? warnTime_ + dt : std::fmax(0.0f, warnTime_ - 2.0f * dt);
                if (warnTime_ >= kGrace) zone_ = Zone::Returning;
            }
            break;
        case Zone::Inside:
            if (dist >= arena.forceRadius)
                zone_ = Zone::Returning;
            else if (dist >= arena.warnRadius)
                zone_ = Zone::Warning;
            break;
    }

    authority_ = zone_ == Zone::Returning ? std::fmin(1.0f, authority_ + dt / kRampTime)
                                          : std::fmax(0.0f, authority_ - dt / kRampTime);
    if (authority_ <= 0.0f) return 0.0f;
    // Turn the short way, never past home.
    const float step = std::fmin(kTurnRate * authority_ * dt, off);
    return side >= 0.0f ? step : -step;
}

}  // namespace game
