#pragma once

// Rain and snow around the camera (docs/decisions/0041). Platform-neutral:
// render/fx_draw.cpp draws it. The drops live in a cube that wraps around the
// camera, so they stay put in the world (and stream past at flying speed)
// while the cube follows.

#include <cstdint>

#include "fx/particles.h"
#include "math/vec3.h"

namespace fx {

class Precipitation {
public:
    static constexpr int kDrops = 180;
    static constexpr float kHalf = 45.0f;  // m, half side of the cube

    void reset();
    // `rain`, `snow`: 0..1 intensity; `wind`: m/s the air carries them.
    void update(const math::Vec3& eye, float rain, float snow, const math::Vec3& wind, float dt);

    float rain() const { return rain_; }
    float snow() const { return snow_; }
    int count() const { return active_; }
    const math::Vec3& drop(int i) const { return pos_[i]; }
    // Velocity of a drop through the world (fall + wind + snow's sway).
    math::Vec3 velocity(int i) const;

private:
    math::Vec3 pos_[kDrops];
    float phase_[kDrops];
    int active_ = 0;
    float rain_ = 0.0f, snow_ = 0.0f;
    float time_ = 0.0f;
    math::Vec3 wind_;
    bool placed_ = false;
    Rng rng_;
};

}  // namespace fx
