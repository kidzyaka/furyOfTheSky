#pragma once

#include <cstdint>

#include "math/vec3.h"
#include "render/texture.h"

namespace gfx {

// Infinite sea (docs/decisions/0011, 0019): two grids that travel with the
// camera in whole texture periods (so the tiled texture never swims), faded
// into the haze by fog, with moving wave highlights and sun glitter on top.
//  - fine patch: 20 x 20 tiles of 16 m right under the camera. The GE does not
//    clip triangles against the near plane, it drops them; small tiles keep
//    the dropped ones out of view when flying low (docs/psp/gotchas.md #19);
//  - coarse ring: 31 x 31 tiles of 320 m with the centre tile missing - the
//    fine patch fills it exactly. Spans +-4.96 km.
class Ocean {
public:
    static constexpr float kTexRepeat = 64.0f;  // m covered by one texture tile
    static constexpr int kFine = 20;
    static constexpr float kFineSize = 16.0f;
    static constexpr int kCoarse = 31;          // odd: one centre tile
    static constexpr float kCoarseSize = kFine * kFineSize;  // 320 m

    void init();
    // Base sea, then a scrolling additive layer of wave highlights near the
    // camera (fades out with distance: fog colour black for the additive pass).
    void draw(const math::Vec3& eye, const Texture& texture, const Texture& waves, float time) const;
    // Sun glitter: an additive billboard at the sun's mirror direction,
    // stretched towards the viewer like the real sun path on water.
    void drawGlint(const math::Vec3& dirToSun, const Texture& glint, float time) const;

private:
    static constexpr int kFineVerts = (kFine + 1) * (kFine + 1);
    static constexpr int kFineIndices = kFine * kFine * 6;
    static constexpr int kCoarseVerts = (kCoarse + 1) * (kCoarse + 1);
    static constexpr int kCoarseIndices = (kCoarse * kCoarse - 1) * 6;
    static constexpr int kWaveRings = 3;  // coarse rings around the patch that get wave highlights

    struct Vertex {
        float u, v;
        float x, y, z;
    };

    static void fillGrid(Vertex* v, int n, float tile);
    void drawGrids(const math::Vec3& eye, bool wavesOnly) const;

    alignas(16) Vertex fine_[kFineVerts];
    alignas(16) uint16_t fineIndices_[kFineIndices];
    alignas(16) Vertex coarse_[kCoarseVerts];
    alignas(16) uint16_t coarseIndices_[kCoarseIndices];  // sorted by ring, innermost first
    int innerCoarseIndices_ = 0;
};

}  // namespace gfx
