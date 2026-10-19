#include "render/ocean.h"

#include <pspgu.h>
#include <pspgum.h>

#include <cmath>

#include "render/view.h"

namespace gfx {

namespace {

constexpr int kN = world::kGridN;
constexpr int kRow = kN + 1;

float frac(float v) { return v - std::floor(v); }

}  // namespace

void Ocean::init() { grid_.build(); }

void Ocean::setup(const world::Environment& env) {
    env_ = env;
    shader_.init(env, kDetailMean);
    // The swell runs with the wind; the wind blows from behind-left of the start
    // heading (any fixed direction will do - it only orients the waves).
    const float wind = 0.6f;
    swell_.init(env.waveHeight, wind);
    windDir_ = {std::sin(wind), 0.0f, -std::cos(wind)};
    levels_ = world::gridLevelsFor(env.maxVisibility() * 1.05f, kTile0);
}

void Ocean::draw(const math::Vec3& eye, const Texture& detail, const Texture& foam, float time, float sunLight) {
    // The finest ring as coarse as the height allows: no sea is nearer than
    // eye.y, so tiles up to a third of it keep every GE-dropped triangle off
    // screen (docs/decisions/0040). At 900 m that is 4 rings instead of 10 -
    // and the vertices are shaded on the CPU.
    int first = 0;
    while (first + 1 < levels_ && 3.0f * kTile0 * static_cast<float>(2 << first) <= eye.y) ++first;
    const float tile0 = kTile0 * static_cast<float>(1 << first);
    const int levels = levels_ - first;
    world::GridLevel lv[world::kGridMaxLevels];
    world::gridLayout(eye.x, eye.z, tile0, levels, lv);
    shader_.setSunLight(sunLight);
    const float reach = swell_.reach();
    const float amp = swell_.amplitude() > 0.01f ? swell_.amplitude() : 0.01f;
    // Pass 2 only for a sea with breaking crests (calm and clear: nothing to add).
    const int foamRings = env_.whitecaps < 0.03f ? 0 : (levels < kFoamRings ? levels : kFoamRings);
    // Whitecaps: the sea state's share, concentrated on the crests near the
    // camera; glitter: warm white where the sun's narrow lobe falls.
    const float caps = env_.whitecaps;
    const float glint = env_.glint * sunLight;
    const world::Rgb sunTint = env_.sunTint;

    // Per ring: heights first (the borders get stitched to the coarser ring),
    // then colours. The pass-1 and pass-2 vertices share positions exactly,
    // so the second pass passes the GEQUAL depth test on the first.
    Vertex* base[world::kGridMaxLevels];
    Vertex* top[world::kGridMaxLevels];
    float h[world::kGridVerts];
    float sx[world::kGridVerts], sz[world::kGridVerts];
    for (int l = 0; l < levels; ++l) {
        const world::GridLevel& g = lv[l];
        const float s = g.tile;
        // Does any of this ring reach into the swell? (Nearest point ~3 tiles
        // out.) High up even the first ring is far too coarse for it: flat,
        // and shaded at every other vertex like the rest.
        const bool waves = 3.0f * s < reach;
        for (int j = 0; j <= kN; ++j)
            for (int i = 0; i <= kN; ++i) {
                const int n = j * kRow + i;
                h[n] = sx[n] = sz[n] = 0.0f;
                if (!waves) continue;
                const float x = g.x0 + i * s, z = g.z0 + j * s;
                const float dx = x - eye.x, dz = z - eye.z;
                const float r = std::sqrt(dx * dx + dz * dz);
                if (r < reach) h[n] = swell_.sample(x, z, time, r, sx[n], sz[n]);
            }
        // T-junctions: on the outer border every other vertex has no partner
        // in the coarser ring; put it on the coarse edge so no crack opens.
        if (waves && l + 1 < levels) {
            for (int i = 1; i < kN; i += 2) {
                h[i] = 0.5f * (h[i - 1] + h[i + 1]);
                h[kN * kRow + i] = 0.5f * (h[kN * kRow + i - 1] + h[kN * kRow + i + 1]);
                h[i * kRow] = 0.5f * (h[(i - 1) * kRow] + h[(i + 1) * kRow]);
                h[i * kRow + kN] = 0.5f * (h[(i - 1) * kRow + kN] + h[(i + 1) * kRow + kN]);
            }
        }

        Vertex* v = static_cast<Vertex*>(sceGuGetMemory(world::kGridVerts * sizeof(Vertex)));
        Vertex* f = l < foamRings ? static_cast<Vertex*>(sceGuGetMemory(world::kGridVerts * sizeof(Vertex))) : nullptr;
        base[l] = v;
        top[l] = f;
        // A ring without swell shades smoothly (only the view angle and the
        // wind patches change across it): shade every other vertex both ways
        // and blend the rest - a quarter of the shading, the CPU's biggest
        // cost in the frame.
        const int stride = waves ? 1 : 2;
        const auto inHole = [&](int i, int j) {
            return g.holeX >= 0 && i > g.holeX && i < g.holeX + world::kGridHole && j > g.holeZ &&
                   j < g.holeZ + world::kGridHole;
        };
        // Colours also kept here: v and f live in the display list
        // (uncached), too slow to read back for the blending below.
        uint32_t cv[world::kGridVerts], cfv[world::kGridVerts];
        const auto shadeAt = [&](int i, int j) {
            const int n = j * kRow + i;
            const float lx = i * s, lz = j * s;
            const float x = g.x0 + lx, z = g.z0 + lz;
            const math::Vec3 toEye{eye.x - x, eye.y - h[n], eye.z - z};
            const bool flat = sx[n] == 0.0f && sz[n] == 0.0f;
            const math::Vec3 normal = flat ? math::kUp : math::normalize({-sx[n], 1.0f, -sz[n]});
            float glitter = 0.0f;
            const uint32_t c = shader_.shade(toEye, normal, world::seaPatches(x, z, time), f ? &glitter : nullptr);
            cv[n] = c;
            v[n] = {static_cast<float>(i), static_cast<float>(j), c, lx, h[n], lz};
            if (f) {
                // Whitecaps ride the crests near the camera; the sun's
                // path lights them up warm.
                const float crest = waves ? math::smoothstep(0.25f * amp, 0.8f * amp, h[n]) : 0.35f;
                const float foamK = caps * (0.4f + 1.6f * crest);
                const float a = math::clamp(foamK * (1.0f + 2.5f * glint * glitter), 0.0f, 1.0f);
                const world::Rgb col = world::mix({255, 255, 255}, sunTint, math::clamp(glint * glitter, 0.0f, 1.0f));
                cfv[n] = world::packAbgr(col, a * 255.0f);
                f[n] = {static_cast<float>(i), static_cast<float>(j), cfv[n], lx, h[n], lz};
            }
        };
        for (int j = 0; j <= kN; j += stride)
            for (int i = 0; i <= kN; i += stride) {
                const int n = j * kRow + i;
                // Vertices strictly inside the hole are never referenced.
                if (inHole(i, j)) {
                    cv[n] = cfv[n] = 0;
                    v[n] = {static_cast<float>(i), static_cast<float>(j), 0, i * s, 0.0f, j * s};
                    if (f) f[n] = v[n];
                    continue;
                }
                shadeAt(i, j);
            }
        if (stride == 2) {
            // The skipped vertices: the mean of the shaded ones beside them
            // (two or four) - or shaded themselves next to the hole.
            const auto avg2 = [](uint32_t a, uint32_t b) { return ((a >> 1) & 0x7F7F7F7Fu) + ((b >> 1) & 0x7F7F7F7Fu); };
            for (int j = 0; j <= kN; ++j)
                for (int i = 0; i <= kN; ++i) {
                    if (!((i | j) & 1)) continue;
                    const int n = j * kRow + i;
                    if (inHole(i, j)) {
                        v[n] = {static_cast<float>(i), static_cast<float>(j), 0, i * s, 0.0f, j * s};
                        if (f) f[n] = v[n];
                        continue;
                    }
                    int a, b, c2 = -1, d2 = -1;  // neighbours (even both ways) to blend
                    if ((i & 1) && (j & 1)) {
                        a = n - kRow - 1, b = n - kRow + 1, c2 = n + kRow - 1, d2 = n + kRow + 1;
                    } else if (i & 1) {
                        a = n - 1, b = n + 1;
                    } else {
                        a = n - kRow, b = n + kRow;
                    }
                    const auto holeAt = [&](int m) { return inHole(m % kRow, m / kRow); };
                    if (holeAt(a) || holeAt(b) || (c2 >= 0 && (holeAt(c2) || holeAt(d2)))) {
                        shadeAt(i, j);
                        continue;
                    }
                    uint32_t c = avg2(cv[a], cv[b]), cf = 0;
                    if (c2 >= 0) c = avg2(c, avg2(cv[c2], cv[d2]));
                    if (f) {
                        cf = avg2(cfv[a], cfv[b]);
                        if (c2 >= 0) cf = avg2(cf, avg2(cfv[c2], cfv[d2]));
                    }
                    v[n] = {static_cast<float>(i), static_cast<float>(j), c, i * s, 0.0f, j * s};
                    if (f) f[n] = {static_cast<float>(i), static_cast<float>(j), cf, i * s, 0.0f, j * s};
                }
        }
    }

    constexpr int kFormat = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D;
    disableLighting();

    // Pass 1: the shaded sea under the ripple texture, sliding downwind at
    // about the speed of a 24 m wave (~6 m/s).
    detail.bind(true, false);
    const float drift = time * 6.1f;
    for (int l = 0; l < levels; ++l) {
        const world::GridLevel& g = lv[l];
        const float k = g.tile / kDetailRepeat;
        sceGuTexScale(k, k);
        sceGuTexOffset(frac((g.x0 - windDir_.x * drift) / kDetailRepeat), frac((g.z0 - windDir_.z * drift) / kDetailRepeat));
        setModelTranslation({g.x0, 0.0f, g.z0});
        sceGumDrawArray(GU_TRIANGLE_STRIP, kFormat, grid_.count(g), grid_.indices(g), base[l]);
    }

    // Pass 2: whitecaps and glitter, added; black fog so it fades out with distance.
    if (foamRings > 0) {
        foam.bind(true, true);
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFF);
        sceGuDepthMask(GU_TRUE);
        setAdditiveFog(true);
        // Across the wind, slower: the two patterns never line up twice.
        const math::Vec3 across{-windDir_.z, 0.0f, windDir_.x};
        const float drift2 = time * 2.3f;
        for (int l = 0; l < foamRings; ++l) {
            const world::GridLevel& g = lv[l];
            const float k = g.tile / kFoamRepeat;
            sceGuTexScale(k, k);
            sceGuTexOffset(frac((g.x0 + across.x * drift2 - windDir_.x * drift2 * 0.5f) / kFoamRepeat),
                           frac((g.z0 + across.z * drift2 - windDir_.z * drift2 * 0.5f) / kFoamRepeat));
            setModelTranslation({g.x0, 0.0f, g.z0});
            sceGumDrawArray(GU_TRIANGLE_STRIP, kFormat, grid_.count(g), grid_.indices(g), top[l]);
        }
        setAdditiveFog(false);
        sceGuDepthMask(GU_FALSE);
        sceGuDisable(GU_BLEND);
    }
    sceGuColor(0xFFFFFFFF);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
}

void Ocean::drawGlint(const math::Vec3& eye, const Texture& glint, float time, float sunLight) const {
    // The sun's glitter path, lying on the water: facets tilted up to ~s
    // from level reflect the sun between elevations e+s and e-s below the
    // horizon, i.e. from h/tan(e+s) to h/tan(e-s) ahead (to the horizon when
    // e < s), widening with distance (Cox & Munk, docs/decisions/0040).
    const math::Vec3& sun = shader_.sun();
    const float strength = env_.glint * sunLight;
    if (sun.y < 0.02f || strength < 0.02f) return;
    const float h = eye.y > 1.0f ? eye.y : 1.0f;
    const float e = std::asin(sun.y);
    const float spread = math::radians(5.0f + 9.0f * env_.seaRoughness);
    const float farLimit = env_.maxVisibility();
    const float dNear = h / std::tan(std::fmin(e + spread, 1.45f));
    const float dFar = e > spread ? std::fmin(h / std::tan(e - spread), farLimit) : farLimit;
    if (dFar <= dNear) return;
    const math::Vec3 fwd = math::normalize({sun.x, 0.0f, sun.z});
    const math::Vec3 side{-fwd.z, 0.0f, fwd.x};
    const float halfAngle = std::tan(spread * 1.3f);
    struct V {
        float u, v;
        uint32_t color;
        float x, y, z;
    };
    // Sparkles rearrange a few times a second: glitter, not crawling noise.
    const float jump = std::floor(time * 6.0f) * 0.37f;
    const float u0 = jump - std::floor(jump);
    const world::Rgb tint = world::mix(env_.sunTint, {255, 255, 255}, 0.3f);
    const uint32_t col = world::packAbgr(tint, 220.0f * math::clamp(strength, 0.0f, 1.0f));
    // Render space: the camera at the origin, the sea plane at -h.
    const math::Vec3 down{0.0f, -h, 0.0f};
    const math::Vec3 n0 = down + fwd * dNear, n1 = down + fwd * dFar;
    const math::Vec3 a0 = side * (dNear * halfAngle), a1 = side * (dFar * halfAngle);
    const float rep = (dFar - dNear) / (dNear * halfAngle * 2.0f + 1.0f);  // keep the sparkles square-ish
    const float vRep = math::clamp(rep, 1.0f, 24.0f);
    auto* v = static_cast<V*>(sceGuGetMemory(6 * sizeof(V)));
    const math::Vec3 p00 = n0 - a0, p10 = n0 + a0, p11 = n1 + a1, p01 = n1 - a1;
    v[0] = {u0, 0.0f, col, p00.x, p00.y, p00.z};
    v[1] = {u0 + 1.0f, 0.0f, col, p10.x, p10.y, p10.z};
    v[2] = {u0 + 1.0f, vRep, col, p11.x, p11.y, p11.z};
    v[3] = v[0];
    v[4] = v[2];
    v[5] = {u0, vRep, col, p01.x, p01.y, p01.z};

    setModelRenderSpace();
    disableLighting();
    glint.bind(true, true);
    sceGuDisable(GU_DEPTH_TEST);  // drawn right after the sea, under everything else
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFF);
    setAdditiveFog(true);  // fades with the haze like the sea under it
    sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 6, nullptr, v);
    setAdditiveFog(false);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_CULL_FACE);
    sceGuEnable(GU_DEPTH_TEST);
}

}  // namespace gfx
