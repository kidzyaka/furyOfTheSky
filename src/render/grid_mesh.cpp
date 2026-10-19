#include "render/grid_mesh.h"

#include <pspkernel.h>

namespace gfx {

namespace {

constexpr int kRow = world::kGridN + 1;

// Strips over the drawn tiles: per row, each run of consecutive drawn tiles
// as (z,x0), (z+1,x0), (z,x0+1), (z+1,x0+1)... - triangles (a, c, b) and
// (b, c, d) of each tile, counter-clockwise seen from above (+Y), as the
// list was; runs joined by a degenerate pair (even run lengths keep the
// winding parity).
int buildStrips(uint16_t* out, const world::GridLevel& g) {
    int n = 0;
    for (int z = 0; z < world::kGridN; ++z) {
        int x = 0;
        while (x < world::kGridN) {
            while (x < world::kGridN && !world::gridTileDrawn(g, x, z)) ++x;
            if (x >= world::kGridN) break;
            int end = x;
            while (end < world::kGridN && world::gridTileDrawn(g, end, z)) ++end;
            const uint16_t first = static_cast<uint16_t>(z * kRow + x);
            if (n > 0) {
                out[n] = out[n - 1];
                ++n;
                out[n++] = first;
            }
            for (int i = x; i <= end; ++i) {
                out[n++] = static_cast<uint16_t>(z * kRow + i);
                out[n++] = static_cast<uint16_t>((z + 1) * kRow + i);
            }
            x = end;
        }
    }
    return n;
}

}  // namespace

void GridMesh::build() {
    world::GridLevel g;
    g.holeX = g.holeZ = -1;
    fullCount = buildStrips(full, g);
    for (int v = 0; v < 4; ++v) {
        g.holeX = 4 + (v & 1);
        g.holeZ = 4 + (v >> 1);
        ringCount[v] = buildStrips(ring[v], g);
    }
    sceKernelDcacheWritebackRange(full, sizeof full);
    sceKernelDcacheWritebackRange(ring, sizeof ring);
}

}  // namespace gfx
