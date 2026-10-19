#include "world/sea.h"

#include <cmath>

namespace world {

namespace {

constexpr float kTwoPi = 6.28318531f;
constexpr int kSinTable = 1024;

struct SinTable {
    float v[kSinTable + 1];
    SinTable() {
        for (int i = 0; i <= kSinTable; ++i) v[i] = std::sin(kTwoPi * static_cast<float>(i) / kSinTable);
    }
};
const SinTable g_sin;

uint32_t hash2(int x, int z) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float lattice(int x, int z) { return static_cast<float>(hash2(x, z) & 0xFFFF) * (1.0f / 65535.0f); }

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace

float fastSin(float x) {
    // No std::floor: on the Allegrex it is a libm call, and this runs ~10k
    // times a frame. Truncate, fix negatives, wrap with a mask.
    const float t = x * (kSinTable / kTwoPi);
    int i = static_cast<int>(t);
    if (t < static_cast<float>(i)) --i;
    const float f = t - static_cast<float>(i);
    i &= kSinTable - 1;
    return g_sin.v[i] + (g_sin.v[i + 1] - g_sin.v[i]) * f;
}

void Swell::init(float height, float windAngle) {
    // A wind sea: the dominant wave ~12x the significant height long (a
    // young sea, steepness ~1/12), never under 32 m - shorter ripples are the
    // detail texture's job (a 4 m vertex grid can't carry them). Shorter waves
    // spread +-35 deg around the wind; amplitudes sum to ~2/3 of the height.
    const float base = std::fmax(height * 12.0f, 32.0f);
    const struct {
        float lengthK, angle, ampK, phase;
    } spec[kWaves] = {
        {1.00f, 0.00f, 0.30f, 0.0f},
        {0.72f, 0.45f, 0.18f, 1.7f},
        {0.55f, -0.60f, 0.12f, 4.1f},
        {0.42f, 0.25f, 0.08f, 2.6f},
    };
    amplitude_ = 0.0f;
    for (int i = 0; i < kWaves; ++i) {
        Wave& w = waves_[i];
        w.length = base * spec[i].lengthK;
        const float k = kTwoPi / w.length;
        const float a = windAngle + spec[i].angle;
        w.kx = k * std::sin(a);
        w.kz = -k * std::cos(a);
        w.omega = std::sqrt(9.81f * k);
        w.amp = height * spec[i].ampK;
        w.phase = spec[i].phase;
        amplitude_ += w.amp;
    }
}

float Swell::sample(float x, float z, float t, float distance, float& slopeX, float& slopeZ) const {
    // Each wave fades out between 0.8 and 1.6 of its length from the camera:
    // further out the LOD tiles grow past a quarter wavelength and it would
    // alias. The fade depends on the distance only, so rings agree on shared vertices.
    float h = 0.0f;
    slopeX = slopeZ = 0.0f;
    for (const Wave& w : waves_) {
        const float f = 1.0f - math::smoothstep(0.8f * w.length, 1.6f * w.length, distance);
        if (f <= 0.0f) continue;
        const float th = w.kx * x + w.kz * z - w.omega * t + w.phase;
        const float a = w.amp * f;
        h += a * fastSin(th);
        const float c = a * fastCos(th);
        slopeX += c * w.kx;
        slopeZ += c * w.kz;
    }
    return h;
}

float Swell::height(float x, float z, float t) const {
    float sx, sz;
    return sample(x, z, t, 0.0f, sx, sz);
}

namespace {

// Wind patches: a 64 x 64 periodic table (80 m cells, 5.1 km period) of two
// octaves of smoothed value noise, built once; sampled bilinearly at run time.
constexpr int kPatchN = 64;
constexpr float kPatchCell = 80.0f;
struct PatchTable {
    float v[kPatchN * kPatchN];
    PatchTable() {
        auto noise = [](float u, float w, int period, int seed) {
            const int iu = static_cast<int>(u), iw = static_cast<int>(w);
            float a = u - iu, b = w - iw;
            a = a * a * (3.0f - 2.0f * a);
            b = b * b * (3.0f - 2.0f * b);
            auto l = [&](int x, int z) { return lattice((x % period) + seed, (z % period) + seed * 7); };
            const float l0 = l(iu, iw) + (l(iu + 1, iw) - l(iu, iw)) * a;
            const float l1 = l(iu, iw + 1) + (l(iu + 1, iw + 1) - l(iu, iw + 1)) * a;
            return l0 + (l1 - l0) * b;
        };
        for (int z = 0; z < kPatchN; ++z)
            for (int x = 0; x < kPatchN; ++x) {
                // ~160 m and ~55 m lumps (2 and ~0.7 cells), periodic over the table.
                const float n = 0.65f * noise(x * 0.5f, z * 0.5f, kPatchN / 2, 11) +
                                0.35f * noise(x * 1.45f, z * 1.45f, 93, 23);
                v[z * kPatchN + x] = 0.88f + 0.24f * n;
            }
    }
};
const PatchTable g_patches;

}  // namespace

float seaPatches(float x, float z, float t) {
    // Drifting with the wind at ~1.5 m/s.
    const float u = (x + t * 0.9f) * (1.0f / kPatchCell), w = (z - t * 1.2f) * (1.0f / kPatchCell);
    int iu = static_cast<int>(u), iw = static_cast<int>(w);
    if (u < static_cast<float>(iu)) --iu;
    if (w < static_cast<float>(iw)) --iw;
    const float a = u - static_cast<float>(iu), b = w - static_cast<float>(iw);
    const int x0 = iu & (kPatchN - 1), z0 = iw & (kPatchN - 1);
    const int x1 = (x0 + 1) & (kPatchN - 1), z1 = (z0 + 1) & (kPatchN - 1);
    const float* r0 = g_patches.v + z0 * kPatchN;
    const float* r1 = g_patches.v + z1 * kPatchN;
    const float top = r0[x0] + (r0[x1] - r0[x0]) * a;
    const float bot = r1[x0] + (r1[x1] - r1[x0]) * a;
    return top + (bot - top) * b;
}

Rgb skyGradient(const Environment& env, float sinElevation) {
    const float e = std::asin(clamp01(sinElevation)) / (kTwoPi * 0.25f);  // 0 horizon .. 1 zenith
    auto smooth = [](float e0, float e1, float v) {
        const float s = clamp01((v - e0) / (e1 - e0));
        return s * s * (3.0f - 2.0f * s);
    };
    Rgb c = mix(env.horizon, env.mid, smooth(0.0f, 0.22f, e));
    return mix(c, env.zenith, smooth(0.2f, 1.0f, e));
}

void SeaShader::init(const Environment& env, float textureMean) {
    // Fresnel for water, n = 1.34: F0 = 0.021 (Schlick). A rough sea never
    // reaches a mirror at grazing angles - the facets tilt - so it is capped.
    const float f0 = 0.021f;
    const float cap = 1.0f - 0.45f * env.seaRoughness;
    for (int i = 0; i <= kFresnel; ++i) {
        const float c = static_cast<float>(i) / kFresnel;
        const float m = 1.0f - c;
        const float m2 = m * m;
        fresnel_[i] = f0 + (cap - f0) * m2 * m2 * m;
    }
    // What the water reflects: the sky above, or the underside of the deck.
    for (int i = 0; i <= kSky; ++i) {
        const float s = static_cast<float>(i) / kSky;
        Rgb c = skyGradient(env, s);
        if (env.deck) {
            const float k = clamp01(s / 0.12f);
            c = mix(env.haze, env.deckBelow, k * k * (3.0f - 2.0f * k));
        }
        sky_[i] = c;
    }
    // The sun on a rough sea: a broad glow where facets tilt towards it,
    // half width ~19 deg at 5 m/s of wind, ~26 deg at 10 m/s (Cox & Munk,
    // docs/decisions/0040): cos^n with n = ln 0.5 / ln cos(half width).
    const float halfWidth = math::radians(14.0f + 14.0f * env.seaRoughness);
    const float n = std::log(0.5f) / std::log(std::cos(halfWidth));
    for (int i = 0; i <= kSpec; ++i) {
        const float c = static_cast<float>(i) / kSpec;
        spec_[i] = std::pow(c, n);
        glitter_[i] = std::pow(c, n * 4.0f);
    }
    body_ = env.seaDeep;
    sunColour_ = scale(env.key, 0.75f * env.glint);
    sun_ = math::normalize(env.sunDir);
    gain_ = 1.0f / textureMean;
}

uint32_t SeaShader::shade(const math::Vec3& toEye, const math::Vec3& normal, float patch, float* glitter) const {
    const float d2 = math::dot(toEye, toEye);
    const float inv = d2 > 1e-6f ? 1.0f / std::sqrt(d2) : 1.0f;
    const math::Vec3 v = toEye * inv;
    float c = math::dot(v, normal);
    c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    const float f = fresnel_[static_cast<int>(c * kFresnel)];
    // Reflected view ray.
    const math::Vec3 r = normal * (2.0f * c) - v;
    const Rgb& sky = sky_[static_cast<int>(clamp01(r.y) * kSky)];
    const float s = math::dot(r, sun_);
    const int si = s > 0.0f ? static_cast<int>(s * kSpec) : -1;
    const float spec = si >= 0 ? spec_[si] * sunLight_ : 0.0f;
    if (glitter) *glitter = si >= 0 ? glitter_[si] * sunLight_ : 0.0f;
    const float k = gain_ * patch;
    const float bodyK = (1.0f - f) * k, skyK = f * k;
    Rgb out{body_.r * bodyK + sky.r * skyK + sunColour_.r * spec, body_.g * bodyK + sky.g * skyK + sunColour_.g * spec,
            body_.b * bodyK + sky.b * skyK + sunColour_.b * spec};
    return packAbgr(out);
}

}  // namespace world
