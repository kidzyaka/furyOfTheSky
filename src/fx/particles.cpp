#include "fx/particles.h"

namespace fx {

uint32_t lerpColor(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        float ca = static_cast<float>((a >> shift) & 0xFF), cb = static_cast<float>((b >> shift) & 0xFF);
        out |= static_cast<uint32_t>(ca + (cb - ca) * t + 0.5f) << shift;
    }
    return out;
}

uint32_t scaleAlpha(uint32_t c, float k) {
    float a = static_cast<float>(c >> 24) * math::clamp(k, 0.0f, 1.0f);
    return (c & 0x00FFFFFFu) | (static_cast<uint32_t>(a + 0.5f) << 24);
}

void ParticleSystem::update(float dt) {
    for (int i = 0; i < n_;) {
        Particle& p = p_[i];
        p.age += dt;
        if (p.age >= p.life) {
            p = p_[--n_];  // swap-remove; order does not matter (sorted at draw)
            continue;
        }
        p.vel *= 1.0f / (1.0f + p.drag * dt);
        p.vel.y += p.lift * dt;
        p.pos += p.vel * dt;
        ++i;
    }
}

void Trail::clear() {
    head_ = -1;
    n_ = 0;
    timer_ = 0.0f;
    emitting_ = false;
}

void Trail::update(const math::Vec3& emitter, float intensity, float width, float dt) {
    for (int i = 0; i < n_; ++i) pts_[(head_ - i + kMaxPoints) % kMaxPoints].age += dt;
    while (n_ > 0 && point(n_ - 1).age >= life) --n_;  // oldest are at the tail

    emitter_ = emitter;
    width_ = width;
    intensity_ = intensity;
    bool on = intensity > 0.01f;
    timer_ += dt;
    if (on && (!emitting_ || timer_ >= spacing)) {
        timer_ = 0.0f;
        head_ = (head_ + 1) % kMaxPoints;
        pts_[head_] = {emitter, 0.0f, width, intensity};
        if (n_ < kMaxPoints) ++n_;
    }
    emitting_ = on;
}

float Trail::pointWidth(int i) const {
    const TrailPoint& p = point(i);
    return p.width * (1.0f + (growth - 1.0f) * (p.age / life));
}

float Trail::pointAlpha(int i) const {
    const TrailPoint& p = point(i);
    float t = p.age / life;
    return p.alpha * (1.0f - t) * (1.0f - t);
}

}  // namespace fx
