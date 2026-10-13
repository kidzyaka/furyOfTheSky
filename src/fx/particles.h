#pragma once

// Visual-effect simulation: particles and ribbon trails (docs/decisions/0016).
// Platform-neutral (no PSP headers) - render/fx_draw.* turns it into GE work.

#include <cstdint>

#include "math/vec3.h"

namespace fx {

enum class Sprite : uint8_t { Glow, Smoke };
enum class Blend : uint8_t { Additive, Alpha };

// ABGR colour helpers (the GE's byte order).
constexpr uint32_t rgba(unsigned r, unsigned g, unsigned b, unsigned a = 255) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}
uint32_t lerpColor(uint32_t a, uint32_t b, float t);
uint32_t scaleAlpha(uint32_t c, float k);

// Tiny deterministic LCG: effects must not depend on libc rand().
struct Rng {
    uint32_t s = 0x1234567u;
    float next() {
        s = s * 1664525u + 1013904223u;
        return (s >> 8) * (1.0f / 16777216.0f);
    }
    float range(float a, float b) { return a + (b - a) * next(); }
};

struct Particle {
    math::Vec3 pos, vel;        // world, m and m/s
    float age = 0.0f, life = 1.0f;
    float size0 = 1.0f, size1 = 1.0f;  // half extent over the lifetime, m
    uint32_t color0 = 0xFFFFFFFF, color1 = 0x00FFFFFF;
    float drag = 0.0f;          // 1/s, relative to still air
    float lift = 0.0f;          // m/s^2 upward (negative = falls)
    Sprite sprite = Sprite::Glow;
    Blend blend = Blend::Additive;

    float t() const { return age / life; }
    float size() const { return size0 + (size1 - size0) * t(); }
    uint32_t color() const { return lerpColor(color0, color1, t()); }
};

// Fixed pool. When full, new particles are dropped (the effect thins out)
// rather than recycling live ones (that would pop).
class ParticleSystem {
public:
    static constexpr int kMax = 320;

    void clear() { n_ = 0; }
    Particle* spawn() { return n_ < kMax ? &(p_[n_++] = Particle{}) : nullptr; }
    void update(float dt);
    int count() const { return n_; }
    const Particle& operator[](int i) const { return p_[i]; }

private:
    Particle p_[kMax];
    int n_ = 0;
};

struct TrailPoint {
    math::Vec3 pos;
    float age;
    float width;   // m, at emission
    float alpha;   // 0..1, at emission
};

// Ribbon behind a moving emitter: a point is dropped every `spacing` seconds,
// the live head follows the emitter so the ribbon never lags behind it.
class Trail {
public:
    static constexpr int kMaxPoints = 40;

    float life = 2.0f;        // s a point lives
    float spacing = 0.06f;    // s between points
    float growth = 1.0f;      // width multiplier at the end of life
    uint32_t color = 0xFFFFFFFF;

    void clear();
    // intensity 0 stops emitting; the old ribbon keeps fading out.
    void update(const math::Vec3& emitter, float intensity, float width, float dt);

    int count() const { return n_; }
    // 0 = newest. Age-faded width/alpha already applied.
    const TrailPoint& point(int i) const { return pts_[(head_ - i + kMaxPoints) % kMaxPoints]; }
    float pointWidth(int i) const;
    float pointAlpha(int i) const;
    bool emitting() const { return emitting_; }
    const math::Vec3& emitter() const { return emitter_; }
    float emitterWidth() const { return width_; }
    float emitterAlpha() const { return intensity_; }

private:
    TrailPoint pts_[kMaxPoints];
    int head_ = -1, n_ = 0;
    float timer_ = 0.0f;
    bool emitting_ = false;
    math::Vec3 emitter_;
    float width_ = 0.0f, intensity_ = 0.0f;
};

}  // namespace fx
