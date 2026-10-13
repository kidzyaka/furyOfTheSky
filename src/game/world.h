#pragma once

// Environment constants shared by simulation and rendering (docs/decisions/0011).

#include "math/vec3.h"

namespace game::world {

// Direction *towards* the sun: behind-right of the start heading, ~33 deg up,
// so the player's aircraft starts front-lit and the sun shows up in turns.
inline math::Vec3 sunDirection() { return math::normalize({0.50f, 0.55f, 0.67f}); }

// Haze: everything fades into this colour; the sky below the horizon uses it too,
// so the ocean's far edge melts into the sky.
constexpr unsigned char kHazeRGB[3] = {186, 204, 220};
constexpr float kFogNear = 1500.0f;   // m
constexpr float kFogFar = 5000.0f;    // m

constexpr float kStartAltitude = 900.0f;  // m
constexpr float kStartSpeed = 250.0f;     // m/s (900 km/h)

}  // namespace game::world
