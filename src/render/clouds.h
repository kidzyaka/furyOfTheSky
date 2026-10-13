#pragma once

#include <cstdint>

#include "game/chase_camera.h"
#include "render/texture.h"

namespace gfx {

// Cumulus field made of camera-facing puff sprites. Clouds live in a square
// cell that repeats around the camera, so the field is endless; puffs are
// sorted back to front every frame for alpha blending.
class Clouds {
public:
    void init(uint32_t seed, const math::Vec3& dirToSun);
    void draw(const game::CameraPose& cam, const Texture& texture);
    // 0..1: how deep inside a puff the camera was at the last draw (whiteout).
    float immersion() const { return immersion_; }

private:
    static constexpr int kClouds = 42;
    static constexpr int kMaxPuffsPerCloud = 9;
    static constexpr int kMaxPuffs = kClouds * kMaxPuffsPerCloud;

    struct Puff {
        math::Vec3 pos;  // world, inside the base cell
        float size;      // half extent, m
        uint8_t shade;   // 0..255 grey, darker at cloud bases
    };
    struct Visible {
        math::Vec3 rel;  // position relative to the camera
        float size;
        float dist;
        uint32_t color;
    };

    Puff puffs_[kMaxPuffs];
    int puffCount_ = 0;
    Visible visible_[kMaxPuffs];
    float immersion_ = 0.0f;
};

}  // namespace gfx
