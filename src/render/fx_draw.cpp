#include "render/fx_draw.h"

#include <pspgu.h>
#include <pspgum.h>

#include <algorithm>
#include <cmath>

#include "render/gu_context.h"
#include "render/view.h"

namespace gfx {

using math::Quat;
using math::smoothstep;
using math::Vec3;

namespace {

struct FxVertex {
    float u, v;
    uint32_t color;
    float x, y, z;
};
constexpr int kFx3D = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D;
constexpr int kFx2D = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D;

FxVertex* alloc(int n) { return static_cast<FxVertex*>(sceGuGetMemory(n * sizeof(FxVertex))); }

unsigned byte(float x) { return static_cast<unsigned>(math::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); }

void begin(bool additive, const Texture& tex, bool repeat = false) {
    setModelRenderSpace();
    disableLighting();
    tex.bind(repeat, true);
    sceGuEnable(GU_BLEND);
    if (additive)
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFF);  // dst + src * alpha
    else
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    setAdditiveFog(additive);
    sceGuDepthMask(GU_TRUE);  // test against the world, never occlude
    sceGuDisable(GU_CULL_FACE);
}

void end() {
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_CULL_FACE);
    setAdditiveFog(false);
}

// Camera-facing sprite: GU_SPRITES takes two opposite corners.
FxVertex* sprite(FxVertex* v, const Vec3& rel, float size, uint32_t color) {
    const CameraBasis& c = camera();
    Vec3 tl = rel - c.right * size + c.up * size;
    Vec3 br = rel + c.right * size - c.up * size;
    v[0] = {0.0f, 0.0f, color, tl.x, tl.y, tl.z};
    v[1] = {1.0f, 1.0f, color, br.x, br.y, br.z};
    return v + 2;
}

// Arbitrary quad as two triangles: centre c, half axes a (u) and b (v).
FxVertex* quad(FxVertex* v, const Vec3& c, const Vec3& a, const Vec3& b, uint32_t color) {
    Vec3 p00 = c - a - b, p10 = c + a - b, p11 = c + a + b, p01 = c - a + b;
    v[0] = {0, 0, color, p00.x, p00.y, p00.z};
    v[1] = {1, 0, color, p10.x, p10.y, p10.z};
    v[2] = {1, 1, color, p11.x, p11.y, p11.z};
    v[3] = {0, 0, color, p00.x, p00.y, p00.z};
    v[4] = {1, 1, color, p11.x, p11.y, p11.z};
    v[5] = {0, 1, color, p01.x, p01.y, p01.z};
    return v + 6;
}

void perpendiculars(const Vec3& axis, Vec3& p1, Vec3& p2) {
    p1 = math::normalize(math::cross(axis, std::fabs(axis.y) < 0.9f ? math::kUp : math::kRight));
    p2 = math::cross(axis, p1);
}

// Lathed shell around `axis`: profile of (distance along axis, radius, v).
template <int kRings>
FxVertex* lathe(FxVertex* v, const Vec3& base, const Vec3& axis, const float (&z)[kRings],
                const float (&r)[kRings], const float (&tv)[kRings], int segments, float uRepeat, float uOffset,
                uint32_t color) {
    Vec3 p1, p2;
    perpendiculars(axis, p1, p2);
    auto point = [&](int ring, int seg) {
        float a = seg * (2.0f * math::kPi / segments);
        return base + axis * z[ring] + (p1 * std::cos(a) + p2 * std::sin(a)) * r[ring];
    };
    for (int ring = 0; ring + 1 < kRings; ++ring) {
        for (int seg = 0; seg < segments; ++seg) {
            Vec3 a = point(ring, seg), b = point(ring, seg + 1), c = point(ring + 1, seg + 1),
                 d = point(ring + 1, seg);
            float u0 = uOffset + uRepeat * seg / segments, u1 = uOffset + uRepeat * (seg + 1) / segments;
            float v0 = tv[ring], v1 = tv[ring + 1];
            v[0] = {u0, v0, color, a.x, a.y, a.z};
            v[1] = {u1, v0, color, b.x, b.y, b.z};
            v[2] = {u1, v1, color, c.x, c.y, c.z};
            v[3] = {u0, v0, color, a.x, a.y, a.z};
            v[4] = {u1, v1, color, c.x, c.y, c.z};
            v[5] = {u0, v1, color, d.x, d.y, d.z};
            v += 6;
        }
    }
    return v;
}

}  // namespace

void drawEngineFlames(const fx::EngineFx& e, const NozzleFrame nozzles[2], const FxTextures& tex) {
    const Vec3& eye = camera().eye;
    const float ab = e.afterburner;
    const float abClamped = std::min(ab, 1.0f);
    const float dry = e.heat * (1.0f - abClamped);

    // --- Jet cones (flame texture: shock diamonds are bands along v).
    struct Cone {
        float length, radius;
        uint32_t color;
    };
    Cone cones[3];
    int coneCount = 0;
    if (ab > 0.02f) {
        float flick = 0.92f + 0.16f * e.flicker;
        cones[coneCount++] = {6.5f * ab * flick, 0.40f, abgr(255, 150, 110, byte(0.85f * abClamped))};
        cones[coneCount++] = {2.8f * ab * (0.95f + 0.1f * e.flicker), 0.27f, abgr(255, 236, 200, byte(abClamped))};
    }
    if (dry > 0.08f)  // dry power: a faint, short, bluish heat plume
        cones[coneCount++] = {1.6f + 1.6f * e.heat, 0.33f, abgr(150, 170, 255, byte(0.3f * dry))};

    constexpr int kSeg = 10;
    constexpr int kRings = 6;
    if (coneCount > 0) {
        const int perCone = kSeg * (kRings - 1) * 6;
        FxVertex* v = alloc(perCone * coneCount * 2);
        FxVertex* w = v;
        for (int i = 0; i < 2; ++i) {
            Vec3 base = nozzles[i].exit - eye;
            for (int c = 0; c < coneCount; ++c) {
                float z[kRings], r[kRings], tv[kRings];
                for (int k = 0; k < kRings; ++k) {
                    float t = k / float(kRings - 1);
                    z[k] = t * cones[c].length;
                    // Slight bulge after the exit, then a taper to a point.
                    r[k] = cones[c].radius * std::pow(1.0f - t, 0.7f) * (1.0f + 0.18f * std::sin(t * math::kPi)) +
                           0.03f;
                    tv[k] = t;
                }
                w = lathe(w, base, nozzles[i].axis, z, r, tv, kSeg, 2.0f, 0.0f, cones[c].color);
            }
        }
        begin(true, *tex.flame, true);
        sceGuTexWrap(GU_REPEAT, GU_CLAMP);
        sceGumDrawArray(GU_TRIANGLES, kFx3D, static_cast<int>(w - v), nullptr, v);
        end();
    }

    // --- Glows: hot nozzle interior, Mach diamonds (they read from behind,
    // where the cone is foreshortened), and the light-up flash.
    FxVertex* v = alloc(2 * 2 * 7);
    FxVertex* w = v;
    float pulse = 0.75f + 0.25f * e.flicker;
    for (int i = 0; i < 2; ++i) {
        const Vec3 exit = nozzles[i].exit - eye;
        const Vec3& axis = nozzles[i].axis;
        if (e.heat > 0.05f || ab > 0.02f) {
            uint32_t hot = fx::lerpColor(abgr(255, 90, 36, byte(0.7f * e.heat)), abgr(255, 214, 165, 255), abClamped);
            w = sprite(w, exit - axis * 0.15f, 0.5f + 0.55f * abClamped, hot);
        }
        if (ab > 0.05f) {
            float length = 6.5f * ab;
            const float at[4] = {0.15f, 0.29f, 0.43f, 0.56f};
            for (int k = 0; k < 4; ++k)
                w = sprite(w, exit + axis * (at[k] * length), 0.45f * (1.0f - 0.16f * k),
                           abgr(255, 222, 168, byte(0.8f * abClamped * pulse)));
        }
        if (e.ignition > 0.05f)
            w = sprite(w, exit + axis * 1.2f, 3.2f * e.ignition, abgr(255, 196, 140, byte(e.ignition)));
    }
    if (w != v) {
        begin(true, *tex.glow);
        sceGumDrawArray(GU_SPRITES, kFx3D, static_cast<int>(w - v), nullptr, v);
        end();
    }
}

void drawLerxVapor(const fx::AircraftEffects& fx, const Vec3& pos, const Quat& rot, const FxTextures& tex) {
    const float k = fx.vapor();
    if (k < 0.02f) return;
    const fx::AttachPoints& pts = fx.points();
    const CameraBasis& cam = camera();
    struct Puff {
        Vec3 rel;
        float depth, size;
        uint32_t color;
    };
    Puff puffs[2 * fx::AttachPoints::kVaporPuffs];
    int n = 0;
    for (int side = 0; side < 2; ++side) {
        for (int i = 0; i < fx::AttachPoints::kVaporPuffs; ++i) {
            float on = smoothstep(pts.vaporOnset[i], pts.vaporOnset[i] + 0.3f, k);
            if (on < 0.02f) continue;
            Vec3 b = pts.vapor[i];
            if (side) b.x = -b.x;
            Vec3 rel = pos + rot.rotate(b) - cam.eye;
            // Slow breathing, not flicker: fast changes smear on the PSP-3000 LCD.
            float wob = 0.8f + 0.2f * std::sin(fx.time() * 7.0f + i * 1.7f + side * 0.9f);
            puffs[n++] = {rel, math::dot(rel, cam.forward), pts.vaporSize[i] * (0.85f + 0.3f * k),
                          abgr(240, 244, 250, byte(0.6f * on * wob))};
        }
    }
    // Far to near. Insertion sort: 14 items (and std::sort trips GCC 15's
    // -Warray-bounds on a fixed array this small).
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0 && puffs[j].depth > puffs[j - 1].depth; --j) std::swap(puffs[j], puffs[j - 1]);
    FxVertex* v = alloc(n * 2);
    FxVertex* w = v;
    for (int i = 0; i < n; ++i) w = sprite(w, puffs[i].rel, puffs[i].size, puffs[i].color);
    begin(false, *tex.smoke);
    sceGumDrawArray(GU_SPRITES, kFx3D, n * 2, nullptr, v);
    end();
}

void drawTransonicVapor(const fx::AircraftEffects& fx, const Vec3& pos, const Quat& rot, const FxTextures& tex) {
    const float k = fx.transonic();
    if (k < 0.02f) return;
    const fx::AttachPoints& pts = fx.points();
    const Vec3 eye = camera().eye;
    // The shock "breathes" (transonic buffet): a small, slow wobble of its
    // position - not flicker, the PSP-3000 LCD smears that.
    const float wobble = 0.03f * std::sin(fx.time() * 11.0f) + 0.02f * std::sin(fx.time() * 17.0f + 1.0f);
    const float shock = fx.shockPosition() + wobble;
    const Vec3 right = rot.rotate(math::kRight), up = rot.rotate(math::kUp), aft = rot.rotate(Vec3{0, 0, 1});
    auto body = [&](float x, float y, float z) { return pos + right * x + up * y + aft * z - eye; };

    // --- 1. Shock collar around the fuselage: starts thin at the canopy,
    // densest just ahead of the terminating shock, then ends abruptly. The
    // density profile lives in the vertex alpha; the texture adds streaks.
    constexpr int kSeg = 16, kRings = 6;
    const float zFront = pts.canopyZ;
    // The rim moves aft as Mach grows; past M 1 (shock > 1) it keeps sliding
    // off the back while the cloud thins out - it never balloons.
    const float zShock = zFront + 2.5f + 7.0f * math::clamp(shock, 0.0f, 1.0f) + 3.0f * math::clamp(shock - 1.0f, 0.0f, 1.0f);
    const float rShock = 2.2f + 3.1f * math::clamp(shock, 0.0f, 1.0f);
    const float zr[kRings] = {zFront, zFront + (zShock - zFront) * 0.35f, zFront + (zShock - zFront) * 0.7f,
                              zShock - 0.35f, zShock, zShock + 0.12f};
    const float rr[kRings] = {0.9f, 0.9f + (rShock - 0.9f) * 0.5f, 0.9f + (rShock - 0.9f) * 0.85f,
                              rShock * 0.98f, rShock, rShock * 1.01f};
    const float ar[kRings] = {0.0f, 0.35f, 0.75f, 1.0f, 1.0f, 0.0f};
    const float vr[kRings] = {0.0f, 0.3f, 0.6f, 0.9f, 0.97f, 1.0f};
    const float layers[2] = {1.0f, 0.62f};  // outer skin + an inner one for volume
    FxVertex* v = alloc(2 * kSeg * (kRings - 1) * 6 + 4 * 3 * 3 * 6);
    FxVertex* w = v;
    for (float layer : layers) {
        for (int ring = 0; ring + 1 < kRings; ++ring) {
            for (int seg = 0; seg < kSeg; ++seg) {
                Vec3 p[4];
                uint32_t c[4];
                const int ri[4] = {ring, ring, ring + 1, ring + 1}, si[4] = {seg, seg + 1, seg + 1, seg};
                for (int i = 0; i < 4; ++i) {
                    float a = si[i] * (2.0f * math::kPi / kSeg);
                    float ca = std::cos(a), sa = std::sin(a);
                    float r = rr[ri[i]] * layer;
                    p[i] = body(ca * r, 0.15f + sa * r * 0.85f, zr[ri[i]]);
                    // Sunlit top, greyer underside.
                    float shade = 0.84f + 0.16f * sa;
                    unsigned g = byte(shade);
                    c[i] = abgr(g, g, std::min(255u, g + 6u), byte(ar[ri[i]] * k * (layer < 1.0f ? 0.7f : 1.0f)));
                }
                float u0 = 3.0f * seg / kSeg, u1 = 3.0f * (seg + 1) / kSeg;
                float v0 = vr[ring], v1 = vr[ring + 1];
                *w++ = {u0, v0, c[0], p[0].x, p[0].y, p[0].z};
                *w++ = {u1, v0, c[1], p[1].x, p[1].y, p[1].z};
                *w++ = {u1, v1, c[2], p[2].x, p[2].y, p[2].z};
                *w++ = {u0, v0, c[0], p[0].x, p[0].y, p[0].z};
                *w++ = {u1, v1, c[2], p[2].x, p[2].y, p[2].z};
                *w++ = {u0, v1, c[3], p[3].x, p[3].y, p[3].z};
            }
        }
    }

    // --- 2. Condensation sheets on the wing tops, ending at the wing shock
    // (from ~45% chord at M 0.88 to the trailing edge at M 1). From the chase
    // camera this is what reads best.
    const float fShock = 0.45f + 0.52f * math::clamp(shock, 0.0f, 1.0f);
    const float fRows[4] = {0.10f, fShock - 0.10f, fShock, fShock + 0.03f};
    const float aRows[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    const float sCols[4] = {0.12f, 0.4f, 0.68f, 0.9f};
    // Two layers (just above the skin and a little higher) give the sheet body.
    for (int sheet = 0; sheet < 4; ++sheet) {
        const int side = sheet & 1;
        const float lift = sheet < 2 ? 0.12f : 0.4f, layerAlpha = sheet < 2 ? 1.0f : 0.55f;
        float mirror = side ? -1.0f : 1.0f;
        auto at = [&](float sp, float f) {
            const fx::WingSpan &a = pts.wingRoot, &b = pts.wingTip;
            float x = a.x + (b.x - a.x) * sp, y = a.y + (b.y - a.y) * sp + lift;
            float zle = a.zLE + (b.zLE - a.zLE) * sp, chord = a.chord + (b.chord - a.chord) * sp;
            return body(mirror * x, y, zle + chord * f);
        };
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                Vec3 p00 = at(sCols[col], fRows[row]), p10 = at(sCols[col + 1], fRows[row]);
                Vec3 p11 = at(sCols[col + 1], fRows[row + 1]), p01 = at(sCols[col], fRows[row + 1]);
                // Thinner towards the tip, where the shock is weaker.
                float t0 = (1.0f - 0.35f * sCols[col]) * layerAlpha, t1 = (1.0f - 0.35f * sCols[col + 1]) * layerAlpha;
                uint32_t c00 = abgr(244, 246, 250, byte(aRows[row] * k * t0));
                uint32_t c10 = abgr(244, 246, 250, byte(aRows[row] * k * t1));
                uint32_t c11 = abgr(244, 246, 250, byte(aRows[row + 1] * k * t1));
                uint32_t c01 = abgr(244, 246, 250, byte(aRows[row + 1] * k * t0));
                float u0 = sCols[col] * 2.0f, u1 = sCols[col + 1] * 2.0f, v0 = fRows[row], v1 = fRows[row + 1];
                *w++ = {u0, v0, c00, p00.x, p00.y, p00.z};
                *w++ = {u1, v0, c10, p10.x, p10.y, p10.z};
                *w++ = {u1, v1, c11, p11.x, p11.y, p11.z};
                *w++ = {u0, v0, c00, p00.x, p00.y, p00.z};
                *w++ = {u1, v1, c11, p11.x, p11.y, p11.z};
                *w++ = {u0, v1, c01, p01.x, p01.y, p01.z};
            }
        }
    }
    begin(false, *tex.vapor, true);
    sceGumDrawArray(GU_TRIANGLES, kFx3D, static_cast<int>(w - v), nullptr, v);
    end();
}

void drawTrails(const fx::Trail* const* trails, int count, const FxTextures& tex) {
    // One triangle strip for all ribbons, joined by degenerate triangles.
    int capacity = 0;
    for (int t = 0; t < count; ++t) capacity += 2 * (trails[t]->count() + 1) + 2;
    if (capacity <= 2) return;
    FxVertex* v = alloc(capacity);
    FxVertex* w = v;
    const Vec3& eye = camera().eye;

    for (int t = 0; t < count; ++t) {
        const fx::Trail& tr = *trails[t];
        // Point list: live head first (if emitting), then the stored points.
        constexpr int kMax = fx::Trail::kMaxPoints + 1;
        Vec3 pos[kMax];
        float width[kMax], alpha[kMax];
        int m = 0;
        if (tr.emitting()) {
            pos[m] = tr.emitter() - eye;
            width[m] = tr.emitterWidth();
            alpha[m++] = tr.emitterAlpha();
        }
        for (int i = 0; i < tr.count(); ++i) {
            pos[m] = tr.point(i).pos - eye;
            width[m] = tr.pointWidth(i);
            alpha[m++] = tr.pointAlpha(i);
        }
        if (m < 2) continue;
        // Degenerate bridge from the previous ribbon: repeat its last vertex
        // and this ribbon's first one (culling is off, parity is irrelevant).
        const bool bridge = w != v;
        FxVertex* start = bridge ? w + 2 : w;
        FxVertex* q = start;
        for (int i = 0; i < m; ++i) {
            Vec3 along = pos[i > 0 ? i - 1 : 0] - pos[i + 1 < m ? i + 1 : m - 1];
            Vec3 side = math::normalize(math::cross(along, pos[i])) * (0.5f * width[i]);
            uint32_t c = fx::scaleAlpha(tr.color, alpha[i]);
            float tv = i * 0.25f;
            Vec3 l = pos[i] + side, r = pos[i] - side;
            *q++ = {0.0f, tv, c, l.x, l.y, l.z};
            *q++ = {1.0f, tv, c, r.x, r.y, r.z};
        }
        if (bridge) {
            w[0] = w[-1];
            w[1] = start[0];
        }
        w = q;
    }
    if (w - v < 4) return;
    begin(false, *tex.ribbon);
    sceGumDrawArray(GU_TRIANGLE_STRIP, kFx3D, static_cast<int>(w - v), nullptr, v);
    end();
}

void drawParticles(const fx::ParticleSystem& ps, const FxTextures& tex) {
    const int n = ps.count();
    if (n == 0) return;
    const CameraBasis& cam = camera();
    static int order[fx::ParticleSystem::kMax];
    static float depth[fx::ParticleSystem::kMax];
    int alphaCount = 0, addCount = 0;
    for (int i = 0; i < n; ++i) {
        if (ps[i].blend == fx::Blend::Alpha) {
            order[alphaCount++] = i;
            depth[i] = math::dot(ps[i].pos - cam.eye, cam.forward);
        } else {
            ++addCount;
        }
    }
    if (alphaCount) {
        std::sort(order, order + alphaCount, [](int a, int b) { return depth[a] > depth[b]; });
        FxVertex* v = alloc(alphaCount * 2);
        FxVertex* w = v;
        for (int k = 0; k < alphaCount; ++k) {
            const fx::Particle& p = ps[order[k]];
            w = sprite(w, p.pos - cam.eye, p.size(), p.color());
        }
        begin(false, *tex.smoke);
        sceGumDrawArray(GU_SPRITES, kFx3D, alphaCount * 2, nullptr, v);
        end();
    }
    if (addCount) {
        FxVertex* v = alloc(addCount * 2);
        FxVertex* w = v;
        for (int i = 0; i < n; ++i)
            if (ps[i].blend == fx::Blend::Additive) w = sprite(w, ps[i].pos - cam.eye, ps[i].size(), ps[i].color());
        begin(true, *tex.glow);
        sceGumDrawArray(GU_SPRITES, kFx3D, addCount * 2, nullptr, v);
        end();
    }
}

void drawFlares(const game::Countermeasures& cm, float time, const FxTextures& tex) {
    FxVertex* v = alloc(game::Countermeasures::kMaxFlares * 4);
    FxVertex* w = v;
    const Vec3& eye = camera().eye;
    for (int i = 0; i < game::Countermeasures::kMaxFlares; ++i) {
        const game::Flare& f = cm.flare(i);
        if (!f.lit()) continue;
        float h = std::sin(time * 41.0f + i * 12.9f) * 43758.5f;
        float flick = 0.8f + 0.4f * (h - std::floor(h));
        float fade = 1.0f - smoothstep(3.3f, 4.0f, f.age);
        Vec3 rel = f.pos - eye;
        w = sprite(w, rel, 3.2f * fade, abgr(255, 196, 140, byte(0.3f * fade)));  // halo
        w = sprite(w, rel, 1.1f * flick * fade + 0.2f, abgr(255, 250, 236, 255));  // core
    }
    if (w == v) return;
    begin(true, *tex.glow);
    sceGumDrawArray(GU_SPRITES, kFx3D, static_cast<int>(w - v), nullptr, v);
    end();
}

void drawSpeedStreaks(const fx::AircraftEffects& fx, const Vec3& pathDir, float speed, const FxTextures& tex) {
    const float alpha = fx.streakAlpha();
    if (alpha < 0.02f) return;
    const Vec3& eye = camera().eye;
    const float half = 0.5f * math::clamp(speed * 0.045f, 4.0f, 22.0f);
    FxVertex* v = alloc(fx::AircraftEffects::kStreaks * 6);
    FxVertex* w = v;
    for (int i = 0; i < fx::AircraftEffects::kStreaks; ++i) {
        Vec3 rel = fx.streak(i) - eye;
        float d = math::length(rel);
        float fade = smoothstep(8.0f, 20.0f, d) * smoothstep(110.0f, 70.0f, d);
        if (fade < 0.02f) continue;
        Vec3 side = math::normalize(math::cross(pathDir, rel)) * (d * 0.006f);  // ~1.5 px wide at any range
        w = quad(w, rel, side, pathDir * half, abgr(235, 242, 255, byte(0.4f * alpha * fade)));
    }
    if (w == v) return;
    begin(true, *tex.glow);
    sceGumDrawArray(GU_TRIANGLES, kFx3D, static_cast<int>(w - v), nullptr, v);
    end();
}

void drawShadow(const Vec3& pos, const Quat& rot, const Vec3& sun, const FxTextures& tex) {
    constexpr float kFadeAltitude = 160.0f;
    if (pos.y > kFadeAltitude || sun.y < 0.1f) return;
    float k = 1.0f - pos.y / kFadeAltitude;
    // Project the airframe along the sun direction onto the sea plane: the
    // shadow stretches and narrows with bank and pitch like the real one.
    auto onSea = [&](const Vec3& d) { return d - sun * (d.y / sun.y); };
    Vec3 centre = pos - sun * (pos.y / sun.y);
    centre.y = 0.15f;
    Vec3 right = onSea(rot.right()) * 11.0f;
    Vec3 back = onSea(-rot.forward()) * 11.0f;  // texture v grows from the nose aft
    FxVertex* v = alloc(6);
    quad(v, centre - camera().eye, right, back, abgr(255, 255, 255, byte(0.5f * k * k)));
    begin(false, *tex.shadow);
    sceGuDisable(GU_DEPTH_TEST);  // drawn right after the sea, before anything above it
    sceGumDrawArray(GU_TRIANGLES, kFx3D, 6, nullptr, v);
    sceGuEnable(GU_DEPTH_TEST);
    end();
}

void drawScreenTint(uint32_t color) {
    if ((color >> 24) < 2) return;
    struct V2 {
        uint32_t color;
        int16_t x, y, z;
    };
    auto* v = static_cast<V2*>(sceGuGetMemory(2 * sizeof(V2)));
    v[0] = {color, 0, 0, 0};
    v[1] = {color, kScreenW, kScreenH, 0};
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDisable(GU_FOG);
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, nullptr, v);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
}

void drawLensFlare(float sx, float sy, float visibility, const FxTextures& tex) {
    if (visibility < 0.02f) return;
    // Fade out towards the screen edges (the sun is about to leave the frame).
    float edge = smoothstep(0.0f, 60.0f, std::min(std::min(sx, kScreenW - sx), std::min(sy, kScreenH - sy)) + 30.0f);
    float k = visibility * edge;
    if (k < 0.02f) return;
    const float cx = kScreenW * 0.5f, cy = kScreenH * 0.5f;
    struct Ghost {
        float at, size;
        bool ring;
        uint32_t color;
    };
    // Ghosts sit on the line from the sun through the screen centre.
    const Ghost ghosts[] = {
        {0.0f, 70.0f, false, abgr(255, 236, 200, byte(0.30f * k))},   // bloom around the sun
        {0.45f, 12.0f, false, abgr(255, 220, 170, byte(0.22f * k))},
        {0.8f, 26.0f, true, abgr(170, 255, 200, byte(0.16f * k))},
        {1.15f, 9.0f, false, abgr(150, 190, 255, byte(0.24f * k))},
        {1.45f, 34.0f, true, abgr(255, 170, 130, byte(0.12f * k))},
        {1.9f, 16.0f, false, abgr(200, 170, 255, byte(0.16f * k))},
    };
    FxVertex* v = alloc(2 * (sizeof ghosts / sizeof ghosts[0]) + 2);
    FxVertex* w = v;
    for (const Ghost& g : ghosts) {
        float x = sx + (cx - sx) * g.at, y = sy + (cy - sy) * g.at;
        // Through-mode UVs are in texels: both textures are 64 x 64.
        w[0] = {0.0f, 0.0f, g.color, x - g.size, y - g.size, 0.0f};
        w[1] = {64.0f, 64.0f, g.color, x + g.size, y + g.size, 0.0f};
        w += 2;
    }
    // Anamorphic streak through the sun.
    w[0] = {0.0f, 0.0f, abgr(255, 230, 200, byte(0.22f * k)), sx - 150.0f, sy - 3.0f, 0.0f};
    w[1] = {64.0f, 64.0f, abgr(255, 230, 200, byte(0.22f * k)), sx + 150.0f, sy + 3.0f, 0.0f};

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_FOG);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFF);
    // Glow ghosts + streak, then the ring ghosts.
    tex.glow->bind(false, true);
    for (int i = 0; i < 6; ++i)
        if (!ghosts[i].ring) sceGuDrawArray(GU_SPRITES, kFx2D, 2, nullptr, v + 2 * i);
    sceGuDrawArray(GU_SPRITES, kFx2D, 2, nullptr, v + 12);
    tex.ring->bind(false, true);
    for (int i = 0; i < 6; ++i)
        if (ghosts[i].ring) sceGuDrawArray(GU_SPRITES, kFx2D, 2, nullptr, v + 2 * i);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_DEPTH_TEST);
}

}  // namespace gfx
