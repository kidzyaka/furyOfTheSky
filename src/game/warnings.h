#pragma once

#include <algorithm>

#include "game/flight_model.h"
#include "world/terrain.h"

namespace game {

// Ground proximity, shared by the HUD and the voice warning system so the
// picture and the voice never disagree (docs/decisions/0024).
enum class GroundWarning { None, Altitude, PullUp };

// With `terrain`, a rock on the flight path counts too (docs/decisions/0042):
// the same tiers by time to impact, looked up along the velocity.
inline GroundWarning groundWarning(const FlightState& s, const ::world::Terrain* terrain = nullptr) {
    if (s.crashed) return GroundWarning::None;
    if (terrain && s.position.y < terrain->maxHeight() + 300.0f && s.speed > 30.0f) {
        const float f = terrain->segment(s.position, s.position + s.velocity() * 9.0f);
        if (f >= 0.0f) return f * 9.0f < 4.5f ? GroundWarning::PullUp : GroundWarning::Altitude;
    }
    const float vs = s.pathDir.y * s.speed;  // m/s, + up
    if (vs > -20.0f) return GroundWarning::None;
    const float toImpact = s.position.y / std::max(-vs, 1.0f);  // s at the current sink rate
    if (toImpact < 4.5f && s.position.y < 900.0f) return GroundWarning::PullUp;
    if (toImpact < 9.0f && s.position.y < 600.0f) return GroundWarning::Altitude;
    return GroundWarning::None;
}

}  // namespace game
