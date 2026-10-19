#pragma once

#include <cstdint>

#include "math/vec3.h"
#include "render/grid_mesh.h"
#include "render/texture.h"
#include "world/environment.h"

namespace gfx {

// The overcast deck (docs/decisions/0041): one continuous cloud layer that can
// be flown through. Under it - a grey ceiling at the base; above it - a sunlit
// sea of cloud at the top; inside - nothing is drawn (the scene whites out
// with the fog). Same LOD rings as the sea, drawn opaque, so whatever it hides
// (the sea above it, the sky below it) is skipped by the caller.
class CloudDeck {
public:
    static constexpr float kTile0 = 8.0f;   // m, finest ring; the first ring drawn follows the height over the layer
    static constexpr float kRepeat = 640.0f;      // m per period of the top's texture
    static constexpr float kBaseRepeat = 2800.0f; // m per period of the base's cells
    static constexpr float kScudRepeat = 1600.0f; // m per period of the scud layer
    static constexpr float kScudDrop = 170.0f;    // m: the scud hangs this far under the base

    void init();
    void setup(const world::Environment& env);
    bool enabled() const { return env_.deck; }
    // Is the camera under / above the deck (nothing to draw inside it)?
    bool below(float altitude) const { return env_.deck && altitude < env_.deckBase - 40.0f; }
    bool above(float altitude) const { return env_.deck && altitude > env_.deckTop + 40.0f; }
    // From below (docs/decisions/0048): the base with the weather's texture
    // (stratocumulus cells or nimbostratus), a faster ragged scud layer under
    // it, the sun's bright patch; from above: the sunlit top.
    struct Textures {
        const Texture* top;     // deck.ftx: soft lumps, seen from above
        const Texture* base;    // deck_base.ftx: stratocumulus cells
        const Texture* storm;   // deck_storm.ftx: nimbostratus
        const Texture* scud;    // deck_scud.ftx: torn fractus, alpha
        const Texture* glow;    // a soft round glow (fx)
    };
    void draw(const math::Vec3& eye, const Textures& tex, float time) const;

private:
    void drawPlane(const math::Vec3& eye, float y, const Texture& texture, float repeat, float du, float dv,
                   uint32_t colour) const;
    struct Vertex {
        float u, v;
        float x, y, z;
    };
    GridMesh grid_;
    alignas(16) Vertex vertices_[world::kGridVerts];
    world::Environment env_;
    int levels_ = 6;
};

}  // namespace gfx
