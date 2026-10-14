#pragma once

#include <algorithm>

#include "game/flight_model.h"

namespace game {

// Ground proximity, shared by the HUD and the voice warning system so the
// picture and the voice never disagree (docs/decisions/0024).
enum class GroundWarning { None, Altitude, PullUp };

inline GroundWarning groundWarning(const FlightState& s) {
    const float vs = s.pathDir.y * s.speed;  // m/s, + up
    if (s.crashed || vs > -20.0f) return GroundWarning::None;
    const float toImpact = s.position.y / std::max(-vs, 1.0f);  // s at the current sink rate
    if (toImpact < 4.5f && s.position.y < 900.0f) return GroundWarning::PullUp;
    if (toImpact < 9.0f && s.position.y < 600.0f) return GroundWarning::Altitude;
    return GroundWarning::None;
}

}  // namespace game
