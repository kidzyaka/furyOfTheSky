#pragma once

#include <cstdint>

// One rock's or island's shape (docs/decisions/0042): a cylindrical height
// map and a cap. The data is generated with the mesh by tools/models/rocks.py
// into world/rock_shapes.cpp, so the collision matches what is drawn.
//
// In the shape's frame (game axes, azimuth theta = atan2(z, x)):
//  - walls: ring j at y = yBottom + j * ringStep has radius[j * sectors + i]
//    at theta = 2 pi i / sectors; a sector's walls end at wallTop[i] (rings
//    above it repeat the radius at the top). Between rings - linear, the ring
//    above clamped to the wall top; between sectors - linear;
//  - cap: over the wall top, a polar height field: ring k at capRho[k] of the
//    top ring's radius (1 at the edge -> 0 at the centre) has height
//    capY[k * sectors + i]; linear between rings and sectors.
//  - column tops: a square grid of gridW x gridW cells of gridCell metres from
//    (gridX0, gridX0) in the shape's frame. topMax: the highest rock in the
//    cell (rounded up, with a margin), topMin: the height the whole cell is
//    solid to from the sea bed (rounded down); kNoRock where there is none.

namespace world {

constexpr int16_t kNoRock = -32768;

struct RockShape {
    const char* name;       // assets/models/rock_<name>.ftr
    int sectors, rings;
    float yBottom, ringStep;  // m
    int capCount;
    const float* radius;    // [rings][sectors], m
    const float* wallTop;   // [sectors], m
    const float* capRho;    // [capCount], 1 -> 0
    const float* capY;      // [capCount][sectors], m
    float maxRadius;        // m, the widest the rock gets
    float apex;             // m, its highest point
    float gridX0, gridCell; // m
    int gridW;
    const int16_t* topMax;  // [gridW][gridW] (row = z), m
    const int16_t* topMin;  // [gridW][gridW], m
};

}  // namespace world
