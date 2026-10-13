#pragma once

// Platform-neutral: compiled for the PSP and for host-side tests.

#include <cmath>

namespace math {

constexpr float kPi = 3.14159265358979f;
constexpr float kGravity = 9.81f;  // m/s^2

inline float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float radians(float deg) { return deg * (kPi / 180.0f); }
inline float degrees(float rad) { return rad * (180.0f / kPi); }

// Frame-rate independent exponential approach factor: fraction of the
// remaining distance covered in `dt` at `rate` (1/s).
inline float approach(float rate, float dt) { return 1.0f - std::exp(-rate * dt); }

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

constexpr Vec3 operator*(float s, const Vec3& v) { return v * s; }

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    float len = length(v);
    return len > 1e-8f ? v / len : Vec3{0.0f, 0.0f, -1.0f};
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

// Game axes (docs/decisions/0007): +X right, +Y up, -Z forward.
constexpr Vec3 kRight{1.0f, 0.0f, 0.0f};
constexpr Vec3 kUp{0.0f, 1.0f, 0.0f};
constexpr Vec3 kForward{0.0f, 0.0f, -1.0f};

}  // namespace math
