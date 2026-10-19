#pragma once

// Start conditions of a sortie. The sky, sun, haze and sea are per sortie
// now: world::Environment (docs/decisions/0041).

#include "math/vec3.h"

namespace game::world {

constexpr float kStartAltitude = 900.0f;  // m
constexpr float kStartSpeed = 250.0f;     // m/s (900 km/h)

}  // namespace game::world
