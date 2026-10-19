#pragma once

#include <cstdint>

#include "game/chase_camera.h"
#include "render/texture.h"
#include "world/environment.h"

namespace gfx {

// Clouds made of pre-rendered volumetric elements (docs/decisions/0041). The
// texture (tools/textures/env.py) holds 4 heaped and 4 flat torn shapes, each
// ray-marched through a warped fractal density twice: lit from behind the
// viewer (bright tops, shadowed undersides) and with the sun behind it
// (glowing thin edges, dark core); each element cross-fades the two by the
// angle between the view and the sun, and is tinted by where it sits in its
// cloud. No spheres anywhere: the outlines are ragged at every scale. Layers:
//  - cumulus: heaps of 10-18 puffs with a flat, darker base, in a 12 km cell
//    that repeats around the camera (an endless field);
//  - the top of an overcast deck: big flattened billows half out of it, the
//    relief of the "sea of cloud" (drawn while above the deck);
//  - the base of the deck: dark ragged lumps hanging under it (drawn below).
// Puffs are sorted back to front every frame for alpha blending.
class Clouds {
public:
    void init(uint32_t seed, const world::Environment& env);
    void draw(const game::CameraPose& cam, const Texture& texture);
    // 0..1: how deep inside a puff the camera was at the last draw (whiteout).
    float immersion() const { return immersion_; }
    int drawn() const { return drawn_; }  // quads last frame (debug corner)

private:
    static constexpr int kMaxPuffs = 1100;
    // Quads drawn at most: each is a full-size alpha blend - fill rate, and 6
    // vertices written by the CPU (docs/decisions/0041).
    static constexpr int kBudget = 180;

    enum Layer : uint8_t { kCumulus, kDeckTop, kDeckBase };
    static constexpr uint8_t kWisp = 8;  // shape flag: a flat wisp (atlas rows 2-3), else a heaped element
    struct Puff {
        math::Vec3 pos;  // world, inside its layer's base cell
        float size;      // half extent, m
        uint32_t color;  // tint (alpha filled per frame)
        uint8_t shape;   // bits 0-1 atlas column, bit 2 mirrored, kWisp
        Layer layer;
    };
    struct Visible {
        math::Vec3 rel;  // position relative to the camera
        float size;
        float dist;
        uint32_t color;  // tint with this frame's alpha
        float u0, u1, v0;
    };

    void addCumulus(uint32_t& rng, const world::Environment& env, const math::Vec3& sun);

    Puff puffs_[kMaxPuffs];
    int puffCount_ = 0;
    Visible visible_[kMaxPuffs * 2];  // up to two sprites per puff (the cross-fade)
    float immersion_ = 0.0f;
    int drawn_ = 0;
    float maxDist_ = 5200.0f;  // m: just past full fog
    math::Vec3 sun_{0.0f, 1.0f, 0.0f};
    bool deck_ = false;
    float deckBase_ = 0.0f, deckTop_ = 0.0f;
};

}  // namespace gfx
