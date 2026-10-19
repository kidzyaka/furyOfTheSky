#pragma once

// Marks lying flat on the sea (docs/decisions/0044): foam where something
// hit the water or a jet/rotor blast stirs it, rings spreading from splashes
// and downwash, foam streaks of a wake. Platform-neutral like the particles;
// render/fx_draw.cpp draws them as flat quads just above the waves.

#include <cstdint>

namespace fx {

enum class MarkKind : uint8_t {
    Ring,  // a spreading ring of disturbed water (fx_ring texture)
    Foam,  // a patch of white water (sea_foam texture)
};

struct WaterMark {
    float x = 0.0f, z = 0.0f;   // world, m
    float dirX = 1.0f, dirZ = 0.0f;  // long axis (unit)
    float r0 = 1.0f, r1 = 1.0f; // half size at birth / at the end of its life, m
    float stretch = 1.0f;       // length / width along dir
    float age = 0.0f, life = 1.0f;
    float alpha = 1.0f;         // peak opacity 0..1
    MarkKind kind = MarkKind::Foam;

    float t() const { return age / life; }
};

// Fixed pool; when full the oldest mark is replaced (they are faint by then).
class WaterMarks {
public:
    static constexpr int kMax = 48;

    void clear() { n_ = 0; }
    void add(const WaterMark& m);
    void update(float dt);
    int count() const { return n_; }
    const WaterMark& operator[](int i) const { return m_[i]; }

private:
    WaterMark m_[kMax];
    int n_ = 0;
};

}  // namespace fx
