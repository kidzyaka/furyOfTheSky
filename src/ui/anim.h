#pragma once

// Animation helpers for the menus (docs/decisions/0035): "crisp but smooth" -
// short exponential approaches (no overshoot, no linear tweens that stop dead).
// Platform-neutral.

#include <cmath>

namespace ui {

// Moves `current` towards `target`, covering ~63 % of the gap every 1/rate s.
// Frame-rate independent. rate 18 settles in ~0.2 s.
inline float approach(float current, float target, float rate, float dt) {
    return target + (current - target) * std::exp(-rate * dt);
}

inline float clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }
inline float easeOutCubic(float t) {
    t = 1.0f - clamp01(t);
    return 1.0f - t * t * t;
}
inline float easeInOut(float t) {
    t = clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

}  // namespace ui
