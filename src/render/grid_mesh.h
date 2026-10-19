#pragma once

#include <cstdint>

#include "world/grid_lod.h"

namespace gfx {

// Index strips of the LOD rings (world/grid_lod.h) over a (kGridN + 1)^2
// vertex grid: the full level 0 and the four ring variants (the finer
// level's hole starts at tile 4 or 5 on each axis). Shared by the sea and the
// cloud deck (docs/decisions/0040). Drawn as GU_TRIANGLE_STRIP: each row of
// tiles (each run of tiles beside the hole) is a strip, the runs joined by
// repeating the last and the next first index - the same triangles and
// winding as a list, ~2.7x fewer indices; the GE transforms every index it
// is sent (docs/psp/gotchas.md #31).
struct GridMesh {
    static constexpr int kMaxIndices = 640;  // full grid: 574; a ring: ~480

    alignas(16) uint16_t full[kMaxIndices];
    alignas(16) uint16_t ring[4][kMaxIndices];
    int fullCount = 0;
    int ringCount[4] = {};

    void build();  // fills and writes back the cache
    const uint16_t* indices(const world::GridLevel& g) const {
        return g.holeX < 0 ? full : ring[(g.holeX - 4) + 2 * (g.holeZ - 4)];
    }
    int count(const world::GridLevel& g) const {
        return g.holeX < 0 ? fullCount : ringCount[(g.holeX - 4) + 2 * (g.holeZ - 4)];
    }
};

}  // namespace gfx
