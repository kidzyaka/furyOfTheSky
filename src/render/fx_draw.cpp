#include "render/fx_draw.h"

#include <pspgu.h>
#include <pspgum.h>

#include <algorithm>
#include <cmath>

#include "render/gu_context.h"
#include "render/view.h"
#include "world/sea.h"

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
        return base + axis * z[ring] + (p1 * world::fastCos(a) + p2 * world::fastSin(a)) * r[ring];
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

namespace {

// Soft condensation puffs (smoke texture, alpha), drawn far to near. The
// same look for the LERX vapor and the transonic cloud.
struct Puff {
    Vec3 rel;  // camera-relative
    float depth, size;
    uint32_t color;
};

void drawPuffs(Puff* puffs, int n, const Texture& smoke) {
    if (n == 0) return;
    // Insertion sort: a few dozen items (and std::sort trips GCC 15's
    // -Warray-bounds on small fixed arrays, docs/psp/gotchas.md #25).
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0 && puffs[j].depth > puffs[j - 1].depth; --j) std::swap(puffs[j], puffs[j - 1]);
    FxVertex* v = alloc(n * 2);
    FxVertex* w = v;
    for (int i = 0; i < n; ++i) w = sprite(w, puffs[i].rel, puffs[i].size, puffs[i].color);
    begin(false, smoke);
    sceGumDrawArray(GU_SPRITES, kFx3D, n * 2, nullptr, v);
    end();
}

// Slow breathing, not flicker: fast changes smear on the PSP-3000 LCD.
float breathe(float time, float phase) { return 0.8f + 0.2f * std::sin(time * 7.0f + phase); }

}  // namespace

void drawLerxVapor(const fx::AircraftEffects& fx, const Vec3& pos, const Quat& rot, const FxTextures& tex) {
    const float k = fx.vapor();
    if (k < 0.02f) return;
    const fx::AttachPoints& pts = fx.points();
    const CameraBasis& cam = camera();
    Puff puffs[2 * fx::AttachPoints::kVaporPuffs];
    int n = 0;
    for (int side = 0; side < 2; ++side) {
        for (int i = 0; i < fx::AttachPoints::kVaporPuffs; ++i) {
            float on = smoothstep(pts.vaporOnset[i], pts.vaporOnset[i] + 0.3f, k);
            if (on < 0.02f) continue;
            Vec3 b = pts.vapor[i];
            if (side) b.x = -b.x;
            Vec3 rel = pos + rot.rotate(b) - cam.eye;
            puffs[n++] = {rel, math::dot(rel, cam.forward), pts.vaporSize[i] * (0.85f + 0.3f * k),
                          abgr(240, 244, 250, byte(0.6f * on * breathe(fx.time(), i * 1.7f + side * 0.9f)))};
        }
    }
    drawPuffs(puffs, n, *tex.smoke);
}

void drawTransonicVapor(const fx::AircraftEffects& fx, const Vec3& pos, const Quat& rot, const FxTextures& tex) {
    const float k = fx.transonic();
    if (k < 0.02f) return;
    const fx::AttachPoints& pts = fx.points();
    const CameraBasis& cam = camera();
    const float time = fx.time();
    // The shock "breathes" (transonic buffet): a small, slow wobble.
    const float shock = fx.shockPosition() + 0.03f * std::sin(time * 11.0f) + 0.02f * std::sin(time * 17.0f + 1.0f);
    const float s1 = math::clamp(shock, 0.0f, 1.0f), past = math::clamp(shock - 1.0f, 0.0f, 1.0f);
    auto body = [&](float x, float y, float z) { return pos + rot.rotate(Vec3{x, y, z}) - cam.eye; };

    constexpr int kRings = 5;
    const int counts[kRings] = {6, 8, 10, 13, 16};
    Puff puffs[6 + 8 + 10 + 13 + 16 + 2 * 4 * 2];
    int n = 0;

    // --- Collar: rings of puffs around the fuselage, sparse and small behind
    // the canopy, densest at the terminating shock - and nothing behind it, so
    // the cloud ends there. The rim slides aft as Mach grows, then off the back.
    const float zFront = pts.canopyZ + 0.8f;
    const float zShock = pts.canopyZ + 2.5f + 7.0f * s1 + 3.0f * past;
    const float rShock = 2.3f + 2.7f * s1;
    const float ringT[kRings] = {0.2f, 0.45f, 0.7f, 0.88f, 1.0f};
    const float ringAlpha[kRings] = {0.25f, 0.42f, 0.6f, 0.78f, 0.9f};
    for (int ring = 0; ring < kRings; ++ring) {
        const float t = ringT[ring];
        const float z = zFront + (zShock - zFront) * t;
        const float r = 1.0f + (rShock - 1.0f) * std::pow(t, 0.8f);
        const float size = (0.9f + 1.4f * t) * (0.9f + 0.2f * k);
        for (int i = 0; i < counts[ring]; ++i) {
            float a = (i + 0.5f * (ring & 1)) * (2.0f * math::kPi / counts[ring]);
            float ca = world::fastCos(a), sa = world::fastSin(a);
            Vec3 rel = body(ca * r, 0.2f + sa * r * 0.8f, z);
            // Sunlit top, grey underside: the shading gives the cloud volume and
            // keeps it readable against the bright haze.
            unsigned g = static_cast<unsigned>(214.0f + 38.0f * sa);
            float alpha = ringAlpha[ring] * k * breathe(time, ring * 1.3f + i * 2.1f);
            puffs[n++] = {rel, math::dot(rel, cam.forward), size, abgr(g, g, std::min(255u, g + 6u), byte(alpha))};
        }
    }

    // --- Wings: puffs over the upper surface up to the wing shock (45% chord
    // at M 0.88, the trailing edge at M 1). Seen best from the chase camera.
    const float fShock = 0.45f + 0.52f * s1;
    const float spans[4] = {0.15f, 0.38f, 0.6f, 0.8f};
    const float chords[2] = {0.2f + (fShock - 0.2f) * 0.4f, fShock - 0.05f};
    const float rowAlpha[2] = {0.5f, 0.8f};
    for (int side = 0; side < 2; ++side) {
        const float mirror = side ? -1.0f : 1.0f;
        for (int si = 0; si < 4; ++si) {
            const fx::WingSpan &a = pts.wingRoot, &b = pts.wingTip;
            const float sp = spans[si];
            const float x = a.x + (b.x - a.x) * sp, y = a.y + (b.y - a.y) * sp + 0.35f;
            const float zle = a.zLE + (b.zLE - a.zLE) * sp, chord = a.chord + (b.chord - a.chord) * sp;
            for (int row = 0; row < 2; ++row) {
                Vec3 rel = body(mirror * x, y, zle + chord * chords[row]);
                float alpha = rowAlpha[row] * k * (1.0f - 0.4f * sp) * breathe(time, si * 1.9f + row + side * 0.7f);
                puffs[n++] = {rel, math::dot(rel, cam.forward), (2.1f - 1.0f * sp) * (0.9f + 0.2f * k),
                              abgr(242, 245, 250, byte(alpha))};
            }
        }
    }
    drawPuffs(puffs, n, *tex.smoke);
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
        float h = world::fastSin(time * 41.0f + i * 12.9f) * 43758.5f;
        float flick = 0.8f + 0.4f * (h - static_cast<float>(static_cast<int>(h)) + (h < 0.0f ? 1.0f : 0.0f));
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

void drawWaterMarks(const fx::WaterMarks& marks, const Texture& foam, const FxTextures& tex) {
    const int n = marks.count();
    if (n == 0) return;
    const Vec3& eye = camera().eye;
    // Two batches (two textures); flat quads at 0.35 m.
    for (int pass = 0; pass < 2; ++pass) {
        const fx::MarkKind kind = pass == 0 ? fx::MarkKind::Foam : fx::MarkKind::Ring;
        FxVertex* v = alloc(n * 6);
        FxVertex* w = v;
        for (int i = 0; i < n; ++i) {
            const fx::WaterMark& m = marks[i];
            if (m.kind != kind) continue;
            const float t = m.t();
            const float r = m.r0 + (m.r1 - m.r0) * std::sqrt(t);
            const float fade = (1.0f - t) * (1.0f - t) * math::smoothstep(0.0f, 0.06f, t);
            const unsigned a = byte(m.alpha * fade);
            if (a < 3) continue;
            const Vec3 c{m.x - eye.x, 0.35f - eye.y, m.z - eye.z};
            const Vec3 along = Vec3{m.dirX, 0.0f, m.dirZ} * (r * m.stretch);
            const Vec3 across = Vec3{-m.dirZ, 0.0f, m.dirX} * r;
            w = quad(w, c, across, along, abgr(255, 255, 255, a));
        }
        if (w == v) continue;
        begin(true, pass == 0 ? foam : *tex.ring);
        sceGumDrawArray(GU_TRIANGLES, kFx3D, static_cast<int>(w - v), nullptr, v);
        end();
    }
}

void drawSurfRing(const Vec3& centre, int count, const float* radii, float width, float strength, float time,
                  const Texture& foam) {
    constexpr int kMaxSurfSegments = 384;
    if (count < 3 || strength < 0.02f) return;
    if (count > kMaxSurfSegments) count = kMaxSurfSegments;
    const Vec3 c = centre - camera().eye;
    const float crawl = time * 0.35f - std::floor(time * 0.35f);  // v scrolls: foam drifts outwards
    float perimeter = 0.0f;
    for (int k = 0; k < count; ++k) perimeter += radii[k];
    perimeter *= 6.2831853f / count;
    const float uRep = std::max(1.0f, std::round(perimeter / 40.0f));  // ~40 m per texture period
    // Directions once for both bands, from the table (libm sin/cos is slow
    // on the Allegrex, and a big island's coast has hundreds of points).
    float dirX[kMaxSurfSegments + 1], dirZ[kMaxSurfSegments + 1];
    for (int k = 0; k <= count; ++k) {
        const float th = 6.2831853f * k / count;
        dirX[k] = world::fastCos(th);
        dirZ[k] = world::fastSin(th);
    }
    // Only the runs of coast in view: from low over the water a big island's
    // ring is mostly behind the camera or off the sides, and the GE would
    // transform every vertex of it (docs/decisions/0050). Adjacent visible
    // runs are drawn as one strip.
    constexpr int kRun = 16;
    int runStart[kMaxSurfSegments / kRun + 1], runEnd[kMaxSurfSegments / kRun + 1];
    int runs = 0, vertices = 0;
    for (int k0 = 0; k0 < count; k0 += kRun) {
        const int k1 = std::min(k0 + kRun, count);
        float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
        for (int k = k0; k <= k1; ++k) {
            const float r = radii[k % count];
            x0 = std::min(x0, dirX[k] * r);
            x1 = std::max(x1, dirX[k] * r);
            z0 = std::min(z0, dirZ[k] * r);
            z1 = std::max(z1, dirZ[k] * r);
        }
        const float hx = 0.5f * (x1 - x0), hz = 0.5f * (z1 - z0);
        const Vec3 mid{centre.x + 0.5f * (x0 + x1), centre.y, centre.z + 0.5f * (z0 + z1)};
        if (!sphereVisible(mid, std::sqrt(hx * hx + hz * hz) + width + 2.0f, 1e6f)) continue;
        if (runs > 0 && runEnd[runs - 1] == k0) {
            vertices += (k1 - k0) * 2;
            runEnd[runs - 1] = k1;
        } else {
            vertices += (k1 - k0 + 1) * 2;
            runStart[runs] = k0;
            runEnd[runs++] = k1;
        }
    }
    if (runs == 0) return;
    // Two bands: the breaking white right at the rock, the spreading foam outside.
    FxVertex* v = alloc(vertices * 2);
    for (int band = 0; band < 2; ++band) {
        FxVertex* w = v + band * vertices;
        const float r0 = band == 0 ? -1.5f : 0.35f * width, r1 = band == 0 ? 0.4f * width : width;
        const unsigned a0 = byte(strength * (band == 0 ? 0.95f : 0.6f)), a1 = byte(band == 0 ? strength * 0.6f : 0.0f);
        for (int j = 0; j < runs; ++j)
            for (int k = runStart[j]; k <= runEnd[j]; ++k) {
                const int i = k % count;
                const float cs = dirX[k], sn = dirZ[k];
                const float ri = radii[i] + r0, ro = radii[i] + r1;
                const float u = uRep * k / count;
                *w++ = {u, crawl, abgr(255, 255, 255, a0), c.x + cs * ri, c.y + 0.3f, c.z + sn * ri};
                *w++ = {u, crawl + 0.35f, abgr(255, 255, 255, a1), c.x + cs * ro, c.y + 0.3f, c.z + sn * ro};
            }
    }
    begin(true, foam, true);
    for (int band = 0; band < 2; ++band) {
        const FxVertex* w = v + band * vertices;
        for (int j = 0; j < runs; ++j) {
            const int n = (runEnd[j] - runStart[j] + 1) * 2;
            sceGumDrawArray(GU_TRIANGLE_STRIP, kFx3D, n, nullptr, w);
            w += n;
        }
    }
    end();
}

void drawPrecipitation(const fx::Precipitation& p, const Vec3& cameraVelocity, uint32_t tint, const FxTextures& tex) {
    const int n = p.count();
    if (n == 0) return;
    const CameraBasis& cam = camera();
    const bool snow = p.snow() > p.rain();
    // A ~1/30 s exposure: at 250 m/s a raindrop near the camera is a 8 m streak.
    constexpr float kExposure = 1.0f / 30.0f;
    FxVertex* v = alloc(n * 6);
    FxVertex* w = v;
    const unsigned baseA = tint >> 24;
    for (int i = 0; i < n; ++i) {
        const Vec3 rel = p.drop(i) - cam.eye;
        const float depth = math::dot(rel, cam.forward);
        if (depth < 1.5f) continue;
        const float d = math::length(rel);
        // Fade in front of the camera and towards the edge of the cube.
        const float fade = smoothstep(1.5f, 5.0f, depth) * smoothstep(fx::Precipitation::kHalf, 15.0f, d);
        if (fade < 0.03f) continue;
        Vec3 motion = (p.velocity(i) - cameraVelocity) * kExposure;
        const float len = math::length(motion);
        const float minLen = snow ? 0.06f : 0.35f;
        if (len < minLen) motion = len > 1e-4f ? motion * (minLen / len) : Vec3{0.0f, -minLen, 0.0f};
        // ~1.5 px flakes, ~1 px streaks at any range.
        const float width = snow ? std::max(0.06f, d * 0.008f) : std::max(0.02f, d * 0.0055f);
        const Vec3 side = math::normalize(math::cross(motion, rel)) * width;
        const uint32_t c = (tint & 0x00FFFFFFu) | (byte(fade * baseA / 255.0f) << 24);
        w = quad(w, rel, side, motion * 0.5f, c);
    }
    if (w == v) return;
    begin(false, *tex.glow);
    sceGumDrawArray(GU_TRIANGLES, kFx3D, static_cast<int>(w - v), nullptr, v);
    end();
}

void drawMissileMotors(const game::Missile* missiles, int count, const FxTextures& tex) {
    // A solid motor's exhaust is a white-hot core and an orange flame a few
    // metres long. It is far brighter than the sky, so it stays visible as a
    // point of light long after the missile itself is sub-pixel: sprites never
    // shrink below ~1.5 px.
    const CameraBasis& cam = camera();
    const int kMax = count;
    FxVertex* v = alloc(kMax * (4 + 6));
    FxVertex* sprites = v;
    FxVertex* w = v;
    int burning = 0;
    for (int i = 0; i < kMax; ++i) {
        const game::Missile& m = missiles[i];
        if (!m.live() || !m.burning()) continue;
        const Vec3 fwd = m.orientation.forward();
        const Vec3 tail = m.pos - fwd * (0.5f * m.type->length) - cam.eye;
        const float d = math::length(tail);
        const float px = d * 0.004f;  // ~1 px at this distance
        const float flicker = 0.9f + 0.1f * std::sin(m.age * 53.0f + i);
        w = sprite(w, tail - fwd * 0.6f, std::max(1.8f, 1.5f * px) * flicker, abgr(255, 170, 90, 150));
        w = sprite(w, tail - fwd * 0.2f, std::max(0.5f, 1.2f * px), abgr(255, 250, 230, 255));
        ++burning;
    }
    if (burning == 0) return;
    FxVertex* flames = w;
    for (int i = 0; i < kMax; ++i) {
        const game::Missile& m = missiles[i];
        if (!m.live() || !m.burning()) continue;
        const Vec3 fwd = m.orientation.forward();
        const Vec3 tail = m.pos - fwd * (0.5f * m.type->length) - cam.eye;
        const float len = 2.8f * (0.9f + 0.1f * std::sin(m.age * 61.0f + i * 2.0f));
        const Vec3 centre = tail - fwd * (0.5f * len);
        Vec3 side = math::cross(fwd, centre);
        side = math::normalize(side) * std::max(0.28f, math::length(centre) * 0.002f);
        w = quad(w, centre, side, fwd * (-0.5f * len), abgr(255, 190, 110, 200));
    }
    begin(true, *tex.glow);
    sceGumDrawArray(GU_SPRITES, kFx3D, static_cast<int>(flames - sprites), nullptr, sprites);
    sceGumDrawArray(GU_TRIANGLES, kFx3D, static_cast<int>(w - flames), nullptr, flames);
    end();
}

void drawTracers(const game::Gun& gun, const FxTextures& tex) {
    // Every 30 mm round as a short glowing streak along its velocity (the
    // eye's persistence over ~1/40 s). Kept ~1.5 px wide at any range.
    const Vec3& eye = camera().eye;
    const float life = gun.params().life;
    FxVertex* v = alloc(game::Gun::kMaxShells * 6);
    FxVertex* w = v;
    for (int i = 0; i < game::Gun::kMaxShells; ++i) {
        const game::Shell& s = gun.shell(i);
        if (!s.live) continue;
        const Vec3 rel = s.pos - eye;
        const float d = math::length(rel);
        const float speed = math::length(s.vel);
        if (d < 4.0f || speed < 1.0f) continue;
        const Vec3 dir = s.vel / speed;
        const float len = std::min(speed * 0.025f, s.age * speed);  // not behind the muzzle
        const Vec3 centre = rel - dir * (0.5f * len);
        const Vec3 side = math::normalize(math::cross(dir, rel)) * std::max(0.12f, d * 0.003f);
        const float fade = 1.0f - smoothstep(life - 0.6f, life, s.age);
        w = quad(w, centre, side, dir * (0.5f * len), abgr(255, 196, 110, byte(0.9f * fade)));
    }
    if (w == v) return;
    begin(true, *tex.glow);
    sceGumDrawArray(GU_TRIANGLES, kFx3D, static_cast<int>(w - v), nullptr, v);
    end();
}

void drawMuzzleFlash(const Vec3& muzzle, const Vec3& dir, float k, const FxTextures& tex) {
    if (k < 0.05f) return;
    const Vec3 rel = muzzle - camera().eye;
    FxVertex* v = alloc(2 * 2 + 6);
    FxVertex* w = sprite(v, rel + dir * 0.8f, 1.6f * k, abgr(255, 220, 150, byte(0.9f * k)));
    w = sprite(w, rel + dir * 0.3f, 0.6f * k, abgr(255, 250, 230, 255));
    FxVertex* q = w;
    const Vec3 side = math::normalize(math::cross(dir, rel)) * (0.45f * k);
    w = quad(w, rel + dir * 2.0f, side, dir * (1.8f * k), abgr(255, 200, 120, byte(0.7f * k)));
    begin(true, *tex.glow);
    sceGumDrawArray(GU_SPRITES, kFx3D, static_cast<int>(q - v), nullptr, v);
    sceGumDrawArray(GU_TRIANGLES, kFx3D, static_cast<int>(w - q), nullptr, q);
    end();
}

void drawShadow(const Vec3& pos, const Quat& rot, const Vec3& sun, const FxTextures& tex, float strength) {
    constexpr float kFadeAltitude = 160.0f;
    if (pos.y > kFadeAltitude || sun.y < 0.1f || strength < 0.05f) return;
    float k = (1.0f - pos.y / kFadeAltitude) * std::sqrt(strength);
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
