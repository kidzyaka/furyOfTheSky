#include "render/clouds.h"

#include <pspgu.h>
#include <pspgum.h>

#include <algorithm>
#include <cmath>

#include "render/view.h"

namespace gfx {
namespace {

constexpr float kCell = 12000.0f;      // m, the cumulus field repeats with this period
constexpr float kDeckCell = 6000.0f;   // m, the deck's billows and lumps repeat faster
constexpr float kFadeBand = 1200.0f;   // m, alpha fade before the far limit

// Tiny deterministic LCG: the cloud field must be identical on every boot
// (and on every peer in a future multiplayer match).
float next(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return (s >> 8) * (1.0f / 16777216.0f);
}
float range(uint32_t& s, float a, float b) { return a + (b - a) * next(s); }

// No std::round: a libm call on the Allegrex, and this runs twice per puff
// (hundreds) a frame.
float wrapNear(float value, float around, float cell) {
    const float t = (around - value) / cell;
    const int k = static_cast<int>(t + (t >= 0.0f ? 0.5f : -0.5f));
    return value + static_cast<float>(k) * cell;
}

uint32_t tint(const world::Rgb& shade, const world::Rgb& lit, float k) {
    return world::packAbgr(world::mix(shade, lit, math::clamp(k, 0.0f, 1.0f)), 0.0f);
}

}  // namespace

void Clouds::addCumulus(uint32_t& rng, const world::Environment& env, const math::Vec3& sun) {
    // Cumulus humilis/mediocris: a flat base of wide, darker puffs, a heap of
    // rounder ones above it - smaller and brighter the higher they sit.
    const math::Vec3 c{range(rng, 0.0f, kCell), range(rng, env.cloudBase, env.cloudTop), range(rng, 0.0f, kCell)};
    const float scale = range(rng, 0.7f, 1.45f) * env.cloudScale;
    const int base = 3 + static_cast<int>(next(rng) * 3.0f);
    const int heap = 7 + static_cast<int>(next(rng) * 7.0f);
    const float height = range(rng, 0.7f, 1.4f) * 160.0f * scale;
    for (int i = 0; i < base + heap && puffCount_ < kMaxPuffs; ++i) {
        Puff& p = puffs_[puffCount_++];
        const bool isBase = i < base;
        const float h = isBase ? 0.0f : next(rng);  // 0 at the base .. 1 at the top
        const float spread = (1.0f - 0.55f * h) * scale;
        p.pos = c + math::Vec3{range(rng, -190.0f, 190.0f) * spread, h * height + (isBase ? 20.0f * scale : 60.0f * scale),
                               range(rng, -140.0f, 140.0f) * spread};
        p.size = (isBase ? range(rng, 120.0f, 170.0f) : range(rng, 75.0f, 135.0f) * (1.0f - 0.3f * h)) * scale;
        const math::Vec3 out = math::normalize(p.pos - c + math::Vec3{0.0f, 30.0f * scale, 0.0f});
        const float lit = isBase ? 0.25f + 0.15f * math::dot(out, sun) : 0.5f + 0.35f * h + 0.25f * math::dot(out, sun);
        p.color = tint(env.cloudShade, env.cloudLit, lit);
        p.shape = static_cast<uint8_t>((static_cast<int>(next(rng) * 8.0f) & 7) | (isBase ? kWisp : 0));
        p.layer = kCumulus;
    }
}

void Clouds::init(uint32_t seed, const world::Environment& env) {
    sun_ = math::normalize(env.sunDir);
    maxDist_ = env.maxVisibility() + 200.0f;
    deck_ = env.deck;
    deckBase_ = env.deckBase;
    deckTop_ = env.deckTop;
    uint32_t rng = seed;
    puffCount_ = 0;
    for (int c = 0; c < env.cloudCount && puffCount_ < kMaxPuffs - 20; ++c) addCumulus(rng, env, sun_);
    if (!env.deck) return;
    // The top of the deck: billows half out of it, lit by the sun from the
    // side it comes from, darker in the troughs.
    const float sunUp = math::clamp(sun_.y * 2.0f, 0.3f, 1.0f);
    for (int i = 0; i < 100 && puffCount_ < kMaxPuffs; ++i) {
        Puff& p = puffs_[puffCount_++];
        p.size = range(rng, 180.0f, 380.0f);
        p.pos = {range(rng, 0.0f, kDeckCell), env.deckTop - p.size * range(rng, 0.25f, 0.6f), range(rng, 0.0f, kDeckCell)};
        p.color = tint(world::scale(env.deckAbove, 0.62f + 0.2f * sunUp), env.deckAbove, range(rng, 0.45f, 1.0f));
        p.shape = static_cast<uint8_t>((static_cast<int>(next(rng) * 8.0f) & 7) | (next(rng) < 0.3f ? kWisp : 0));
        p.layer = kDeckTop;
    }
    // The base: dark ragged lumps hanging under it (a fog bank has none - its
    // base is the sea).
    // Fewer and bigger than the scud layer's patches (render/cloud_deck.cpp):
    // the near lumps give the base its 3D relief.
    for (int i = 0; i < 45 && env.deckBase > 0.0f && puffCount_ < kMaxPuffs; ++i) {
        Puff& p = puffs_[puffCount_++];
        p.size = range(rng, 160.0f, 320.0f);
        p.pos = {range(rng, 0.0f, kDeckCell), env.deckBase - p.size * range(rng, 0.15f, 0.45f), range(rng, 0.0f, kDeckCell)};
        p.color = tint(world::scale(env.deckBelow, 0.5f), world::scale(env.deckBelow, 0.78f), next(rng));
        p.shape = static_cast<uint8_t>((static_cast<int>(next(rng) * 8.0f) & 7) | kWisp);
        p.layer = kDeckBase;
    }
}

void Clouds::draw(const game::CameraPose& cam, const Texture& texture) {
    // Corners are written camera-relative (docs/decisions/0013). The quads
    // stand level with the horizon, not with the screen: GU_SPRITES are
    // screen-aligned rectangles, so in a banked turn the clouds - lit tops,
    // dark bases, flat wisps - rolled with the camera.
    const CameraBasis& basis = camera();
    const math::Vec3 fwd = math::normalize(cam.target - cam.eye);
    math::Vec3 right = math::cross(fwd, math::kUp);
    if (math::length(right) < 0.05f) right = math::cross(fwd, cam.up);  // looking straight up or down
    right = math::normalize(right);
    const math::Vec3 up = math::cross(right, fwd);
    // The deck's layers: its top only seen from above it, its base from below.
    const bool showTop = deck_ && cam.eye.y > deckBase_;
    const bool showBase = deck_ && cam.eye.y < deckTop_ && deckBase_ > 0.0f;

    int n = 0;
    float nearest = 1e9f;  // distance / size of the closest puff
    for (int i = 0; i < puffCount_; ++i) {
        const Puff& p = puffs_[i];
        if ((p.layer == kDeckTop && !showTop) || (p.layer == kDeckBase && !showBase)) continue;
        const float cell = p.layer == kCumulus ? kCell : kDeckCell;
        const math::Vec3 pos{wrapNear(p.pos.x, cam.eye.x, cell), p.pos.y, wrapNear(p.pos.z, cam.eye.z, cell)};
        const math::Vec3 d = pos - cam.eye;
        const float dist = math::length(d);
        nearest = std::min(nearest, dist / p.size);
        const float depth = math::dot(d, basis.forward);
        if (depth < p.size * 0.6f || dist > maxDist_) continue;  // behind us, too close, or in the haze
        // Off screen? (Pad by the half diagonal: the quad turns with the horizon.)
        const float pad = p.size * 1.42f;
        if (std::fabs(math::dot(d, basis.right)) > depth * basis.tanHalfX + pad ||
            std::fabs(math::dot(d, basis.up)) > depth * basis.tanHalfY + pad)
            continue;
        const float a = math::clamp((maxDist_ - dist) / kFadeBand, 0.0f, 1.0f) *
                        math::clamp((dist - p.size * 0.6f) / p.size, 0.0f, 1.0f);  // fly-through fade
        if (a <= 0.01f) continue;
        // Front-lit row or sun-behind row; both, cross-faded, only in a narrow
        // band of angles to the sun (each quad is fill rate).
        const float back = math::smoothstep(0.3f, 0.55f, math::dot(d, sun_) / dist);
        const int shape = p.shape & 3;
        float u0 = shape * 0.25f, u1 = u0 + 0.25f;
        if (p.shape & 4) std::swap(u0, u1);
        const float row = (p.shape & kWisp) ? 0.5f : 0.0f;
        const uint32_t rgb = p.color & 0x00FFFFFFu;
        if (back < 0.98f)
            visible_[n++] = {d, p.size, dist, rgb | (static_cast<uint32_t>(a * (1.0f - back) * 255.0f) << 24), u0, u1,
                             row};
        if (back > 0.02f)
            visible_[n++] = {d, p.size, dist - 0.01f, rgb | (static_cast<uint32_t>(a * back * 255.0f) << 24), u0, u1,
                             row + 0.25f};
    }
    immersion_ = math::smoothstep(1.05f, 0.5f, nearest);
    drawn_ = 0;
    if (n == 0) return;
    std::sort(visible_, visible_ + n, [](const Visible& a, const Visible& b) { return a.dist > b.dist; });
    // Over budget: the farthest go (they are the faintest, in the haze).
    const int skip = n > kBudget ? n - kBudget : 0;
    const int count = n - skip;

    struct QuadVertex {
        float u, v;
        uint32_t color;
        float x, y, z;
    };
    auto* qv = static_cast<QuadVertex*>(sceGuGetMemory(count * 6 * sizeof(QuadVertex)));
    for (int i = 0; i < count; ++i) {
        const Visible& p = visible_[skip + i];
        const math::Vec3 r = right * p.size, u = up * p.size;
        const math::Vec3 tl = p.rel - r + u, tr = p.rel + r + u, bl = p.rel - r - u, br = p.rel + r - u;
        const float v1 = p.v0 + 0.25f;
        QuadVertex* q = qv + 6 * i;
        q[0] = {p.u0, p.v0, p.color, tl.x, tl.y, tl.z};
        q[1] = {p.u0, v1, p.color, bl.x, bl.y, bl.z};
        q[2] = {p.u1, p.v0, p.color, tr.x, tr.y, tr.z};
        q[3] = q[2];
        q[4] = q[1];
        q[5] = {p.u1, v1, p.color, br.x, br.y, br.z};
    }
    drawn_ = count;

    setModelRenderSpace();
    disableLighting();
    texture.bind(false, true);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDepthMask(GU_TRUE);  // test against the world, but don't occlude each other
    sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                    count * 6, nullptr, qv);
    sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_CULL_FACE);
}

}  // namespace gfx
