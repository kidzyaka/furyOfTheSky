#include "fx/precipitation.h"

#include <cmath>

namespace fx {

namespace {

// Terminal fall speeds: raindrops of ~2 mm fall at ~6.5 m/s, heavy rain up to
// 9 m/s; snowflakes ~1 m/s (docs/decisions/0041).
constexpr float kRainFall = 8.0f;
constexpr float kSnowFall = 1.1f;

float wrap(float v, float centre, float half) {
    const float span = 2.0f * half;
    // Floor without libm (a call per drop and axis on the Allegrex).
    const float t = (v - (centre - half)) / span;
    int k = static_cast<int>(t);
    if (t < static_cast<float>(k)) --k;
    return v - static_cast<float>(k) * span;
}

}  // namespace

void Precipitation::reset() {
    placed_ = false;
    active_ = 0;
    time_ = 0.0f;
}

math::Vec3 Precipitation::velocity(int i) const {
    if (snow_ > rain_) {
        const float t = time_ * 0.9f + phase_[i];
        return wind_ + math::Vec3{0.6f * std::sin(t), -kSnowFall, 0.6f * std::cos(t * 1.3f)};
    }
    return wind_ + math::Vec3{0.0f, -kRainFall, 0.0f};
}

void Precipitation::update(const math::Vec3& eye, float rain, float snow, const math::Vec3& wind, float dt) {
    rain_ = rain;
    snow_ = snow;
    wind_ = wind;
    time_ += dt;
    active_ = static_cast<int>(kDrops * math::clamp(rain > snow ? rain : snow, 0.0f, 1.0f));
    if (active_ == 0) return;
    if (!placed_) {
        for (int i = 0; i < kDrops; ++i) {
            pos_[i] = eye + math::Vec3{rng_.range(-kHalf, kHalf), rng_.range(-kHalf, kHalf), rng_.range(-kHalf, kHalf)};
            phase_[i] = rng_.range(0.0f, 6.28f);
        }
        placed_ = true;
    }
    for (int i = 0; i < active_; ++i) {
        math::Vec3& p = pos_[i];
        p += velocity(i) * dt;
        p.x = wrap(p.x, eye.x, kHalf);
        p.y = wrap(p.y, eye.y, kHalf);
        p.z = wrap(p.z, eye.z, kHalf);
    }
}

}  // namespace fx
