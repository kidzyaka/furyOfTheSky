#pragma once

#include <cstdint>

#include "math/vec3.h"
#include "render/texture.h"

namespace gfx {

// Infinite sea: a fixed grid of tiles re-centred under the camera in whole-tile
// steps (so the tiled texture never swims), faded into the haze by fog.
// Tiles are kept moderate because the GE handles triangles crossing the near
// plane poorly (docs/decisions/0011).
class Ocean {
public:
    static constexpr int kTiles = 32;          // per side
    static constexpr float kTileSize = 320.0f; // m -> grid spans +-5.1 km
    static constexpr float kTexRepeat = 64.0f; // m covered by one texture tile

    void init();
    void draw(const math::Vec3& eye, const Texture& texture) const;

private:
    static constexpr int kVerts = (kTiles + 1) * (kTiles + 1);
    static constexpr int kIndexCount = kTiles * kTiles * 6;

    struct Vertex {
        float u, v;
        float x, y, z;
    };

    alignas(16) Vertex vertices_[kVerts];
    alignas(16) uint16_t indices_[kIndexCount];
};

}  // namespace gfx
