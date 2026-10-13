#pragma once

#include "math/vec3.h"

namespace math {

// Unit quaternion for orientations. Hamilton convention, (w, x, y, z).
struct Quat {
    float w = 1.0f, x = 0.0f, y = 0.0f, z = 0.0f;

    constexpr Quat() = default;
    constexpr Quat(float w_, float x_, float y_, float z_) : w(w_), x(x_), y(y_), z(z_) {}

    static Quat axisAngle(const Vec3& axis, float angle) {
        Vec3 a = normalize(axis);
        float s = std::sin(angle * 0.5f);
        return {std::cos(angle * 0.5f), a.x * s, a.y * s, a.z * s};
    }

    // Rotation by the vector's length (radians) around its direction - the
    // exact integral of a constant angular velocity over one step.
    static Quat fromRotationVector(const Vec3& r) {
        float angle = length(r);
        if (angle < 1e-8f) return {1.0f, r.x * 0.5f, r.y * 0.5f, r.z * 0.5f};
        return axisAngle(r / angle, angle);
    }

    Quat operator*(const Quat& q) const {
        return {w * q.w - x * q.x - y * q.y - z * q.z, w * q.x + x * q.w + y * q.z - z * q.y,
                w * q.y - x * q.z + y * q.w + z * q.x, w * q.z + x * q.y - y * q.x + z * q.w};
    }

    Quat conjugate() const { return {w, -x, -y, -z}; }

    Quat normalized() const {
        float n = std::sqrt(w * w + x * x + y * y + z * z);
        return n > 1e-8f ? Quat{w / n, x / n, y / n, z / n} : Quat{};
    }

    Vec3 rotate(const Vec3& v) const {
        // v' = v + 2w(q x v) + 2 q x (q x v)
        Vec3 q{x, y, z};
        Vec3 t = cross(q, v) * 2.0f;
        return v + t * w + cross(q, t);
    }

    Vec3 right() const { return rotate(kRight); }
    Vec3 up() const { return rotate(kUp); }
    Vec3 forward() const { return rotate(kForward); }
};

inline float dot(const Quat& a, const Quat& b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }

// Normalised lerp along the shorter arc: cheap and good enough for smoothing.
inline Quat nlerp(const Quat& a, Quat b, float t) {
    if (dot(a, b) < 0.0f) b = {-b.w, -b.x, -b.y, -b.z};
    return Quat{a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}
        .normalized();
}

// Shortest rotation taking unit vector `from` onto unit vector `to`.
inline Quat rotationBetween(const Vec3& from, const Vec3& to) {
    float d = dot(from, to);
    if (d < -0.9999f) {
        Vec3 axis = cross(kUp, from);
        if (length(axis) < 1e-4f) axis = cross(kRight, from);
        return Quat::axisAngle(axis, kPi);
    }
    Vec3 c = cross(from, to);
    return Quat{1.0f + d, c.x, c.y, c.z}.normalized();
}

// Orientation whose -Z looks along `forward` with +Y as close to `up` as possible.
inline Quat lookRotation(const Vec3& forward, const Vec3& up) {
    Vec3 f = normalize(forward);
    Vec3 r = normalize(cross(f, up));
    Vec3 u = cross(r, f);
    // Columns of the rotation matrix are r, u, -f.
    float m00 = r.x, m01 = u.x, m02 = -f.x;
    float m10 = r.y, m11 = u.y, m12 = -f.y;
    float m20 = r.z, m21 = u.z, m22 = -f.z;
    float trace = m00 + m11 + m22;
    Quat q;
    if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {0.25f * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s};
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {(m21 - m12) / s, 0.25f * s, (m01 + m10) / s, (m02 + m20) / s};
    } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m02 - m20) / s, (m01 + m10) / s, 0.25f * s, (m12 + m21) / s};
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25f * s};
    }
    return q.normalized();
}

}  // namespace math
