#include "render/sky.h"

#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>

#include <cmath>

#include "world/sea.h"
#include "render/view.h"

namespace gfx {
namespace {

constexpr float kRadius = 1000.0f;  // inside the far plane; size is irrelevant without depth
constexpr float kElevationsDeg[] = {-30, -6, 0, 4, 10, 20, 35, 55, 75, 90};

uint32_t skyColor(const world::Environment& env, const math::Vec3& dir, const math::Vec3& sun) {
    world::Rgb c = env.haze;
    if (dir.y > 0.0f) c = world::skyGradient(env, dir.y);
    // Broad warm brightening around the sun; the disc itself is a sprite.
    // Low suns spread it along the horizon (weighted towards elevation 0).
    float s = math::clamp(math::dot(dir, sun), 0.0f, 1.0f);
    float glow = env.glowStrength * (0.6f * std::pow(s, 6.0f) + 0.4f * std::pow(s, 24.0f));
    glow *= 1.0f - 0.5f * math::smoothstep(0.0f, 0.5f, dir.y) * math::smoothstep(0.3f, 0.0f, sun.y);
    c = world::mix(c, env.glow, math::clamp(glow, 0.0f, 1.0f));
    return world::packAbgr(c);
}

}  // namespace

void Sky::init(const world::Environment& env) {
    sun_ = math::normalize(env.sunDir);
    sunColor_ = world::packAbgr(env.sunTint);
    sunSize_ = env.sunDiscSize;
    sunDisc_ = env.sunDisc;
    for (int ring = 0; ring < kRings; ++ring) {
        float el = math::radians(kElevationsDeg[ring]);
        for (int seg = 0; seg < kSegments; ++seg) {
            float az = 2.0f * math::kPi * seg / kSegments;
            math::Vec3 dir{std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az)};
            Vertex& v = vertices_[ring * kSegments + seg];
            v.color = skyColor(env, dir, sun_);
            v.x = dir.x * kRadius;
            v.y = dir.y * kRadius;
            v.z = dir.z * kRadius;
        }
    }
    int k = 0;
    for (int ring = 0; ring + 1 < kRings; ++ring) {
        for (int seg = 0; seg < kSegments; ++seg) {
            uint16_t a = ring * kSegments + seg;
            uint16_t b = ring * kSegments + (seg + 1) % kSegments;
            uint16_t c = a + kSegments, d = b + kSegments;
            indices_[k++] = a; indices_[k++] = b; indices_[k++] = c;
            indices_[k++] = b; indices_[k++] = d; indices_[k++] = c;
        }
    }
    sceKernelDcacheWritebackRange(vertices_, sizeof vertices_);
    sceKernelDcacheWritebackRange(indices_, sizeof indices_);
}

void Sky::draw(const math::Vec3& eye, const math::Vec3& camRight, const math::Vec3& camUp, const Texture& sunTexture,
               float sunVisible) const {
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);  // no depth writes
    disableLighting();
    disableFog();
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_CULL_FACE);  // seen from inside

    setModelTranslation(eye);
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
                    kIndexCount, indices_, vertices_);

    // Sun: one screen-aligned sprite, added on top of the sky.
    const float disc = sunDisc_ * sunVisible;
    if (sun_.y > -0.05f && disc > 0.02f) {
        struct SpriteVertex {
            float u, v;
            uint32_t color;
            float x, y, z;
        };
        auto* sv = static_cast<SpriteVertex*>(sceGuGetMemory(2 * sizeof(SpriteVertex)));
        math::Vec3 c = sun_ * (kRadius * 0.9f);
        float size = kRadius * 0.14f * sunSize_;
        const uint32_t color = (sunColor_ & 0x00FFFFFFu) | (static_cast<uint32_t>(disc * 255.0f) << 24);
        math::Vec3 tl = c - camRight * size + camUp * size;
        math::Vec3 br = c + camRight * size - camUp * size;
        sv[0] = {0.0f, 0.0f, color, tl.x, tl.y, tl.z};
        sv[1] = {1.0f, 1.0f, color, br.x, br.y, br.z};
        sunTexture.bind(false, true);
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFF);  // dst + src * alpha
        sceGumDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                        2, nullptr, sv);
        sceGuDisable(GU_BLEND);
        sceGuDisable(GU_TEXTURE_2D);
    }

    sceGuEnable(GU_CULL_FACE);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_DEPTH_TEST);
}

}  // namespace gfx
