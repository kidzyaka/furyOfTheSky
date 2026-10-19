#include "game/combat.h"

#include "game/signature.h"

#include <cmath>

namespace game {

namespace {

// First t in [0, 1] where a + t*d enters the unit sphere, or -1.
float unitSphere(const math::Vec3& a, const math::Vec3& d) {
    const float A = math::dot(d, d), B = 2.0f * math::dot(a, d), C = math::dot(a, a) - 1.0f;
    if (C <= 0.0f) return 0.0f;  // starts inside
    if (A < 1e-12f) return -1.0f;
    const float disc = B * B - 4.0f * A * C;
    if (disc < 0.0f) return -1.0f;
    const float t = (-B - std::sqrt(disc)) / (2.0f * A);
    return t >= 0.0f && t <= 1.0f ? t : -1.0f;
}

math::Vec3 scaled(const math::Vec3& v, const math::Vec3& half) { return {v.x / half.x, v.y / half.y, v.z / half.z}; }

}  // namespace

float segmentHit(const FlightState& s, const HitShape& shape, const math::Vec3& p0, const math::Vec3& p1) {
    if (s.crashed) return -1.0f;
    const math::Quat toBody = s.orientation.conjugate();
    const math::Vec3 a = toBody.rotate(p0 - s.position);
    const math::Vec3 b = toBody.rotate(p1 - s.position);
    float best = -1.0f;
    const float tb = unitSphere(scaled(a, shape.bodyHalf), scaled(b - a, shape.bodyHalf));
    if (tb >= 0.0f) best = tb;
    const math::Vec3 aw = a - shape.wingCentre;
    const float tw = unitSphere(scaled(aw, shape.wingHalf), scaled(b - a, shape.wingHalf));
    if (tw >= 0.0f && (best < 0.0f || tw < best)) best = tw;
    return best;
}

float targetRcs(const Target& t, const math::Vec3& toObserver) {
    return t.surface ? 1000.0f : radarCrossSection(*t.state, toObserver);
}

float targetIr(const Target& t, const math::Vec3& toObserver) {
    if (t.state->crashed) return 0.0f;
    return t.surface ? 1.5f : infraredIntensity(*t.state, toObserver);
}

}  // namespace game
