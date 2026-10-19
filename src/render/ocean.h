#pragma once

#include <cstdint>

#include "math/vec3.h"
#include "render/grid_mesh.h"
#include "render/texture.h"
#include "world/environment.h"
#include "world/grid_lod.h"
#include "world/sea.h"

namespace gfx {

// The sea (docs/decisions/0040, replacing the two grids of 0011/0019).
//  - Geometry: LOD rings around the camera (world/grid_lod.h) - a 4 m grid
//    under the camera, every ring twice as coarse, as many as the haze needs.
//    No tile is bigger than a third of its distance, so no triangle the GE
//    drops for crossing the near plane or the guard band is ever on screen
//    (the holes of gotchas #19, proven on the host: tests/world_test.cpp).
//  - Light: every vertex is shaded on the CPU each frame (world::SeaShader):
//    body colour, Fresnel sky reflection, the sun's broad reflection, wind
//    patches. Near the camera the swell lifts and tilts the vertices.
//  - Pass 1: a ripple texture modulates it, sliding downwind.
//  - Pass 2 (inner rings, only when crests break): additive whitecaps on the
//    crests, lit warm in the sun's path, another texture sliding across the wind.
class Ocean {
public:
    static constexpr float kTile0 = 4.0f;          // m, the finest ring's tile
    static constexpr float kDetailRepeat = 24.0f;   // m per period of the ripple texture
    static constexpr float kFoamRepeat = 56.0f;     // m per period of the foam texture
    static constexpr int kFoamRings = 5;            // pass 2 on the inner rings (out to ~0.8 km)
    static constexpr float kDetailMean = 0.80f;     // mean of the ripple texture (assets: tools/textures/env.py)

    void init();
    // Sea state, colours and how far the rings reach for this sortie.
    void setup(const world::Environment& env);
    // `sunLight`: 0..1 direct sun at the camera (the deck takes it away).
    void draw(const math::Vec3& eye, const Texture& detail, const Texture& foam, float time, float sunLight);
    // Sun glitter: an additive quad lying on the water along the sun's
    // azimuth, from where the steepest facets catch it to the horizon.
    void drawGlint(const math::Vec3& eye, const Texture& glint, float time, float sunLight) const;
    // Surface height near the camera (spray, splashes sit on it).
    float height(float x, float z, float time) const { return swell_.height(x, z, time); }

private:
    struct Vertex {
        float u, v;
        uint32_t color;
        float x, y, z;
    };
    GridMesh grid_;
    world::Swell swell_;
    world::SeaShader shader_;
    world::Environment env_;
    int levels_ = 9;
    math::Vec3 windDir_{0.0f, 0.0f, -1.0f};
};

}  // namespace gfx
