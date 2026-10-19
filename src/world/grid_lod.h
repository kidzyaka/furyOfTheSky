#pragma once

// Geometric LOD rings for flat world-space grids: the sea and the overcast
// cloud deck (docs/decisions/0040). Platform-neutral: the layout is checked
// against the GE's near plane and guard band on the host (tests/world_test.cpp).
//
// Level 0 is a full kGridN x kGridN grid of `tile0` tiles around the camera;
// level l > 0 is kGridN x kGridN tiles of tile0 * 2^l with the
// kGridHole x kGridHole block that level l-1 covers left out. Every level's
// origin snaps to twice its tile, so level l-1 fits level l's hole exactly
// and the hole starts at tile 4 or 5. The camera then sits at least 3 tiles
// of level l away from any of its tiles: a tile is never bigger than a third
// of its distance. Big triangles near the camera are what the GE drops whole
// (docs/psp/gotchas.md #19) - this keeps every dropped one off screen.
//
// The grid moves only in whole steps of twice a tile, so a texture whose
// period divides 2 * tile0 never swims; the vertices themselves stay put in
// the world between steps.

#include <cmath>

namespace world {

constexpr int kGridN = 16;              // tiles per side of a level
constexpr int kGridHole = kGridN / 2;   // tiles per side of a level's hole
constexpr int kGridVerts = (kGridN + 1) * (kGridN + 1);
constexpr int kGridMaxLevels = 11;

struct GridLevel {
    float x0 = 0.0f, z0 = 0.0f;  // world position of vertex (0, 0)
    float tile = 1.0f;           // m
    int holeX = -1, holeZ = -1;  // first tile of the hole (-1: level 0, no hole)
};

inline void gridLayout(float camX, float camZ, float tile0, int levels, GridLevel* out) {
    for (int l = 0; l < levels; ++l) {
        const float s = tile0 * static_cast<float>(1 << l);
        const float snap = 2.0f * s;
        GridLevel& g = out[l];
        g.tile = s;
        g.x0 = std::floor((camX - kGridN * 0.5f * s) / snap) * snap;
        g.z0 = std::floor((camZ - kGridN * 0.5f * s) / snap) * snap;
        if (l == 0) {
            g.holeX = g.holeZ = -1;
        } else {
            // Level l-1 covers [x0', x0' + 16 * s/2]: whole tiles of this level.
            g.holeX = static_cast<int>(std::lround((out[l - 1].x0 - g.x0) / s));
            g.holeZ = static_cast<int>(std::lround((out[l - 1].z0 - g.z0) / s));
        }
    }
}

// Levels needed so the outermost one reaches `distance` from the camera in
// every direction: its reach is at least 6 tiles (8 per half side, the
// camera up to 2 off centre).
inline int gridLevelsFor(float distance, float tile0) {
    int l = 0;
    while (l + 1 < kGridMaxLevels && 6.0f * tile0 * static_cast<float>(1 << l) < distance) ++l;
    return l + 1;
}

// Is tile (x, z) of a level drawn (not in its hole)?
inline bool gridTileDrawn(const GridLevel& g, int x, int z) {
    return g.holeX < 0 || x < g.holeX || x >= g.holeX + kGridHole || z < g.holeZ || z >= g.holeZ + kGridHole;
}

}  // namespace world
