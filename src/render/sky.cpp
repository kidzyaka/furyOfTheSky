#include "render/sky.h"

#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>

#include <cmath>

#include "game/world.h"
#include "render/view.h"

namespace gfx {
namespace {

constexpr float kRadius = 1000.0f;  // inside the far plane; size is irrelevant without depth
constexpr float kElevationsDeg[] = {-30, -6, 0, 4, 10, 20, 35, 55, 75, 90};

struct Rgb {
    float r, g, b;
};
Rgb mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
float smooth(float e0, float e1, float x) {
    float t = math::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

uint32_t skyColor(const math::Vec3& dir, const math::Vec3& sun) {
    using game::world::kHazeRGB;
    const Rgb haze{float(kHazeRGB[0]), float(kHazeRGB[1]), float(kHazeRGB[2])};
    const Rgb mid{96, 146, 208};
    const Rgb zenith{38, 82, 158};
    Rgb c = haze;
    if (dir.y > 0.0f) {
        float e = std::asin(dir.y) / (math::kPi * 0.5f);  // 0 at horizon, 1 at zenith
        c = mix(haze, mid, smooth(0.0f, 0.22f, e));
        c = mix(c, zenith, smooth(0.2f, 1.0f, e));
    }
    // Broad warm brightening around the sun; the disc itself is a sprite.
    float s = math::clamp(math::dot(dir, sun), 0.0f, 1.0f);
    float glow = 0.35f * std::pow(s, 6.0f) + 0.25f * std::pow(s, 24.0f);
    c = mix(c, Rgb{255, 238, 205}, glow);
    return abgr(unsigned(c.r), unsigned(c.g), unsigned(c.b));
}

}  // namespace

void Sky::init(const math::Vec3& dirToSun) {
    sun_ = math::normalize(dirToSun);
    for (int ring = 0; ring < kRings; ++ring) {
        float el = math::radians(kElevationsDeg[ring]);
        for (int seg = 0; seg < kSegments; ++seg) {
            float az = 2.0f * math::kPi * seg / kSegments;
            math::Vec3 dir{std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az)};
            Vertex& v = vertices_[ring * kSegments + seg];
            v.color = skyColor(dir, sun_);
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

void Sky::draw(const math::Vec3& eye, const math::Vec3& camRight, const math::Vec3& camUp,
               const Texture& sunTexture) const {
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
    if (math::dot(sun_, math::Vec3{0, 1, 0}) > -0.05f) {
        struct SpriteVertex {
            float u, v;
            uint32_t color;
            float x, y, z;
        };
        auto* sv = static_cast<SpriteVertex*>(sceGuGetMemory(2 * sizeof(SpriteVertex)));
        math::Vec3 c = sun_ * (kRadius * 0.9f);
        float size = kRadius * 0.14f;
        math::Vec3 tl = c - camRight * size + camUp * size;
        math::Vec3 br = c + camRight * size - camUp * size;
        sv[0] = {0.0f, 0.0f, 0xFFFFFFFF, tl.x, tl.y, tl.z};
        sv[1] = {1.0f, 1.0f, 0xFFFFFFFF, br.x, br.y, br.z};
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
