#include "render/clouds.h"

#include <pspgu.h>
#include <pspgum.h>

#include <algorithm>
#include <cmath>

#include "game/world.h"
#include "render/view.h"

namespace gfx {
namespace {

constexpr float kCell = 12000.0f;     // m, the field repeats with this period
constexpr float kMaxDist = 5200.0f;   // m, just past full fog
constexpr float kFadeBand = 1200.0f;  // m, alpha fade before kMaxDist

// Tiny deterministic LCG: the cloud field must be identical on every boot
// (and on every peer in a future multiplayer match).
struct Rng {
    uint32_t s;
    float next() {
        s = s * 1664525u + 1013904223u;
        return (s >> 8) * (1.0f / 16777216.0f);
    }
    float range(float a, float b) { return a + (b - a) * next(); }
};

float wrapNear(float value, float around) { return value + std::round((around - value) / kCell) * kCell; }

}  // namespace

void Clouds::init(uint32_t seed) {
    Rng rng{seed};
    puffCount_ = 0;
    for (int c = 0; c < kClouds; ++c) {
        math::Vec3 centre{rng.range(0.0f, kCell), rng.range(1100.0f, 2400.0f), rng.range(0.0f, kCell)};
        float scale = rng.range(0.7f, 1.4f);
        int puffs = 5 + static_cast<int>(rng.next() * (kMaxPuffsPerCloud - 4));
        for (int p = 0; p < puffs; ++p) {
            // A flattened heap: wide along x/z, puffs higher up are smaller and brighter.
            float h = rng.next();
            Puff& puff = puffs_[puffCount_++];
            puff.pos = centre + math::Vec3{rng.range(-170.0f, 170.0f) * scale, h * 110.0f * scale,
                                           rng.range(-120.0f, 120.0f) * scale};
            puff.size = rng.range(80.0f, 140.0f) * scale * (1.0f - 0.35f * h);
            puff.shade = static_cast<uint8_t>(205 + 50 * h);
        }
    }
}

void Clouds::draw(const game::CameraPose& cam, const Texture& texture) {
    math::Vec3 fwd = math::normalize(cam.target - cam.eye);
    math::Vec3 right = math::normalize(math::cross(fwd, cam.up));
    math::Vec3 up = math::cross(right, fwd);

    int n = 0;
    for (int i = 0; i < puffCount_; ++i) {
        const Puff& p = puffs_[i];
        math::Vec3 pos{wrapNear(p.pos.x, cam.eye.x), p.pos.y, wrapNear(p.pos.z, cam.eye.z)};
        math::Vec3 d = pos - cam.eye;
        float depth = math::dot(d, fwd);
        if (depth < p.size * 0.6f) continue;  // behind us, or so close the sprite would explode
        float dist = math::length(d);
        if (dist > kMaxDist) continue;
        float a = math::clamp((kMaxDist - dist) / kFadeBand, 0.0f, 1.0f) *
                  math::clamp((dist - p.size * 0.6f) / p.size, 0.0f, 1.0f);  // fly-through fade
        if (a <= 0.01f) continue;
        visible_[n++] = {pos, p.size, dist, abgr(p.shade, p.shade, p.shade, unsigned(a * 255))};
    }
    if (n == 0) return;
    std::sort(visible_, visible_ + n, [](const Visible& a, const Visible& b) { return a.dist > b.dist; });

    struct SpriteVertex {
        float u, v;
        uint32_t color;
        float x, y, z;
    };
    auto* sv = static_cast<SpriteVertex*>(sceGuGetMemory(n * 2 * sizeof(SpriteVertex)));
    for (int i = 0; i < n; ++i) {
        const Visible& p = visible_[i];
        math::Vec3 tl = p.pos - right * p.size + up * p.size;
        math::Vec3 br = p.pos + right * p.size - up * p.size;
        sv[2 * i] = {0.0f, 0.0f, p.color, tl.x, tl.y, tl.z};
        sv[2 * i + 1] = {1.0f, 1.0f, p.color, br.x, br.y, br.z};
    }

    setModelIdentity();
    disableLighting();
    texture.bind(false, true);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDepthMask(GU_TRUE);  // test against the world, but don't occlude each other
    sceGumDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                    n * 2, nullptr, sv);
    sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_BLEND);
}

}  // namespace gfx
