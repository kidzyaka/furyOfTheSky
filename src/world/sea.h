#pragma once

// The sea's look and motion (docs/decisions/0040), platform-neutral: the
// renderer calls these per vertex every frame, the effects ask the swell for
// the surface height, the host tests check the shading is sane.
//
// The GE has no pixel shaders, so the water is lit on the CPU per vertex of
// the LOD rings: the body colour seen straight down, the sky reflected by the
// Fresnel law (2% looking down, most of it at grazing angles), the sun's broad
// reflection on a rough sea, slow brighter/darker wind patches. A detail
// texture modulates it, a second additive pass adds whitecaps and glitter.

#include <cstdint>

#include "math/vec3.h"
#include "world/environment.h"

namespace world {

// Swell near the camera: a few deep-water sine waves (omega^2 = g k). Same
// function of world position and time everywhere, so neighbouring vertices
// and rings agree.
class Swell {
public:
    static constexpr int kWaves = 4;

    // `height`: significant wave height, m; `windAngle`: direction the waves run (rad).
    void init(float height, float windAngle);
    // Surface height and its slope (dh/dx, dh/dz) at (x, z), time t (s), seen
    // from `distance` m away: each wave fades out by 1.6 of its length.
    float sample(float x, float z, float t, float distance, float& slopeX, float& slopeZ) const;
    float height(float x, float z, float t) const;  // full, as at the camera
    float amplitude() const { return amplitude_; }
    // Beyond this distance the surface is flat (the longest wave has faded).
    float reach() const { return 1.6f * waves_[0].length; }

private:
    struct Wave {
        float kx, kz;    // wave vector, rad/m
        float omega;     // rad/s
        float amp;       // m
        float phase;
        float length;    // m
    };
    Wave waves_[kWaves];
    float amplitude_ = 0.0f;
};

// Fast sine on a table (the swell runs per vertex).
float fastSin(float x);
inline float fastCos(float x) { return fastSin(x + 1.5707963f); }

// Slow wind patches ("cat's paws"): a brightness factor ~0.88..1.12 from a
// periodic value noise with ~160 m lumps, drifting. Deterministic, a table lookup.
float seaPatches(float x, float z, float t);

class SeaShader {
public:
    // Builds the lookup tables for this environment. `textureMean`: mean of
    // the detail texture that modulates the result (the colour is divided by it).
    void init(const Environment& env, float textureMean);
    // How much of the sun reaches the sea now (0 under a deck): scales the reflection.
    void setSunLight(float k) { sunLight_ = k; }
    // Colour of a vertex. `toEye` = eye - vertex (not normalised), `normal` unit,
    // `patch` the wind-patch factor. Returns ABGR, alpha 255. `glitter`, if
    // given, gets the sun's narrow lobe 0..1 there (lights the whitecaps).
    uint32_t shade(const math::Vec3& toEye, const math::Vec3& normal, float patch, float* glitter = nullptr) const;
    const math::Vec3& sun() const { return sun_; }

private:
    static constexpr int kFresnel = 64, kSky = 32, kSpec = 128;
    float fresnel_[kFresnel + 1];
    Rgb sky_[kSky + 1];        // reflected sky by sin(elevation) of the reflection
    float spec_[kSpec + 1];    // broad lobe by cos(angle to the sun)
    float glitter_[kSpec + 1]; // narrow lobe
    Rgb body_, sunColour_;
    math::Vec3 sun_;
    float sunLight_ = 1.0f;
    float gain_ = 1.0f;
};

// Sky gradient of the dome without the sun's halo, by sin(elevation) >= 0
// (the sky dome and the sea reflection use the same one).
Rgb skyGradient(const Environment& env, float sinElevation);

}  // namespace world
