#include "render/cloud_deck.h"

#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>

#include <cmath>

#include "render/view.h"
#include "world/grid_lod.h"

namespace gfx {

namespace {
float frac(float v) { return v - std::floor(v); }
}  // namespace

void CloudDeck::init() {
    grid_.build();
    // One grid in tile units; each ring scales and places it (model matrix)
    // and scales the texture (sceGuTexScale) - nothing is rebuilt per frame.
    for (int j = 0; j <= world::kGridN; ++j)
        for (int i = 0; i <= world::kGridN; ++i)
            vertices_[j * (world::kGridN + 1) + i] = {static_cast<float>(i), static_cast<float>(j),
                                                      static_cast<float>(i), 0.0f, static_cast<float>(j)};
    sceKernelDcacheWritebackRange(vertices_, sizeof vertices_);
}

void CloudDeck::setup(const world::Environment& env) {
    env_ = env;
    levels_ = world::gridLevelsFor(env.maxVisibility() * 1.05f, kTile0);
}

void CloudDeck::drawPlane(const math::Vec3& eye, float y, const Texture& texture, float repeat, float du, float dv,
                          uint32_t colour) const {
    // As the sea (render/ocean.cpp): the finest ring no coarser than a third
    // of the height over the layer - skimming the top of a fog bank 40 m up
    // must not drop triangles (docs/psp/gotchas.md #19) - and no finer.
    const float h = std::fabs(eye.y - y);
    int first = 0;
    while (first + 1 < levels_ && 3.0f * kTile0 * static_cast<float>(2 << first) <= h) ++first;
    const int levels = levels_ - first;
    world::GridLevel lv[world::kGridMaxLevels];
    world::gridLayout(eye.x, eye.z, kTile0 * static_cast<float>(1 << first), levels, lv);
    sceGuColor(colour);
    for (int l = 0; l < levels; ++l) {
        const world::GridLevel& g = lv[l];
        const float k = g.tile / repeat;
        sceGuTexScale(k, k);
        sceGuTexOffset(frac((g.x0 + du) / repeat), frac((g.z0 + dv) / repeat));
        setModelTranslationScale({g.x0, y, g.z0}, g.tile, 1.0f, g.tile);
        sceGumDrawArray(GU_TRIANGLE_STRIP, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
                        grid_.count(g), grid_.indices(g), vertices_);
    }
    (void)texture;
}

void CloudDeck::draw(const math::Vec3& eye, const Textures& tex, float time) const {
    const bool fromBelow = below(eye.y) && env_.deckBase > 0.0f;
    if (!fromBelow && !above(eye.y)) return;
    disableLighting();
    sceGuDisable(GU_CULL_FACE);  // the same triangles are seen from below and above
    // No depth writes: the billows sitting half in the deck (render/clouds.cpp)
    // blend into it instead of being cut off along a hard line.
    sceGuDepthMask(GU_TRUE);
    if (!fromBelow) {
        // The top drifts with the wind, slowly.
        tex.top->bind(true, false);
        drawPlane(eye, env_.deckTop, *tex.top, kRepeat, time * 4.0f, time * -2.5f, world::packAbgr(env_.deckAbove));
    } else {
        // The base: thick dark lumps and thinner lighter gaps; towards the
        // horizon the GE fog melts it into the (lighter) haze - the usual
        // overcast sky, darker overhead than at the horizon.
        const bool storm = env_.weather == world::Weather::Rain;
        const Texture& base = storm ? *tex.storm : *tex.base;
        // The base is seen much further than things down in the haze (the
        // murk is thickest near the sea): stretch the fog for it, or its
        // pattern washes out a kilometre away.
        float fogNear, fogFar;
        uint32_t fogColour;
        currentFog(fogNear, fogFar, fogColour);
        enableFog(fogNear * 1.8f, fogFar * 1.6f, world::packAbgr(env_.horizonGlow));
        base.bind(true, false);
        drawPlane(eye, env_.deckBase, base, kBaseRepeat, time * 4.0f, time * -2.5f, world::packAbgr(env_.deckBelow));
        // The sun through the deck: a soft brighter patch where it stands
        // (thinner in a stratocumulus deck than in nimbostratus).
        const math::Vec3 sun = env_.sunDir;
        if (sun.y > 0.08f && tex.glow) {
            const float up = env_.deckBase - eye.y;
            const math::Vec3 p{sun.x * up / sun.y, up - 2.0f, sun.z * up / sun.y};  // camera-relative
            const float half = 0.5f * math::length(p);
            const float a = (storm ? 0.16f : 0.34f) * math::smoothstep(0.08f, 0.3f, sun.y);
            struct V {
                float u, v;
                uint32_t c;
                float x, y, z;
            };
            const uint32_t c = world::packAbgr(env_.sunTint, a * 255.0f);
            auto* q = static_cast<V*>(sceGuGetMemory(6 * sizeof(V)));
            q[0] = {0.0f, 0.0f, c, p.x - half, p.y, p.z - half};
            q[1] = {0.0f, 1.0f, c, p.x - half, p.y, p.z + half};
            q[2] = {1.0f, 0.0f, c, p.x + half, p.y, p.z - half};
            q[3] = q[2];
            q[4] = q[1];
            q[5] = {1.0f, 1.0f, c, p.x + half, p.y, p.z + half};
            setModelRenderSpace();
            tex.glow->bind(false, true);
            sceGuTexScale(1.0f, 1.0f);
            sceGuTexOffset(0.0f, 0.0f);
            sceGuEnable(GU_BLEND);
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFF);  // dst + src * alpha
            setAdditiveFog(true);
            sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 6,
                            nullptr, q);
            setAdditiveFog(false);
            sceGuDisable(GU_BLEND);
        }
        // Scud: torn fractus a couple of hundred metres under the base,
        // darker and moving faster - flying under it, the two layers slide
        // past each other and the ceiling gets depth.
        const float scudY = env_.deckBase - kScudDrop;
        if (eye.y < scudY - 40.0f) {
            tex.scud->bind(true, true);
            sceGuEnable(GU_BLEND);
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
            drawPlane(eye, scudY, *tex.scud, kScudRepeat, time * 13.0f, time * -8.0f,
                      world::packAbgr(world::scale(env_.deckBelow, storm ? 0.62f : 0.7f)));
            sceGuDisable(GU_BLEND);
        }
        enableFog(fogNear, fogFar, fogColour);
    }
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_CULL_FACE);
    sceGuColor(0xFFFFFFFF);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
}

}  // namespace gfx
