#pragma once

#include <cstdint>

#include "math/vec3.h"
#include "render/texture.h"
#include "world/environment.h"

namespace gfx {

// Vertex-coloured sky dome that travels with the camera, plus an additive
// sun (moon) sprite. Drawn first, without depth writes, so it is always
// "behind". Colours from the sortie's weather and time (docs/decisions/0041).
class Sky {
public:
    void init(const world::Environment& env);
    // `sunVisible` 0..1: the disc fades under a cloud deck.
    void draw(const math::Vec3& eye, const math::Vec3& camRight, const math::Vec3& camUp, const Texture& sunTexture,
              float sunVisible) const;

private:
    static constexpr int kSegments = 24;
    static constexpr int kRings = 10;
    static constexpr int kIndexCount = kSegments * (kRings - 1) * 6;

    struct Vertex {
        uint32_t color;
        float x, y, z;
    };

    alignas(16) Vertex vertices_[kSegments * kRings];
    alignas(16) uint16_t indices_[kIndexCount];
    math::Vec3 sun_;
    uint32_t sunColor_ = 0xFFFFFFFF;
    float sunSize_ = 1.0f, sunDisc_ = 1.0f;
};

}  // namespace gfx
