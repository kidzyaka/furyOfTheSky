#pragma once

#include <cstdint>

#include "math/quat.h"

namespace gfx {

// Camera-relative rendering (docs/decisions/0013): the GE receives matrix
// elements as 24-bit floats (pspgu shifts every float right by 8 bits), so a
// world position of 8 km is quantised to ~0.25 m and the aircraft shakes.
// setCamera() makes the camera the render-space origin; every world-space
// position below is shifted by it on the CPU in full float precision, so
// matrices only ever carry small numbers.

void setProjection(float fovDeg, float nearZ, float farZ);
void setCamera(const math::Vec3& eye, const math::Vec3& target, const math::Vec3& up);
const math::Vec3& renderOrigin();  // world position of the camera this frame

// World-space placement (origin subtracted internally).
void setModel(const math::Vec3& worldPos, const math::Quat& rot);
void setModelTranslation(const math::Vec3& worldPos);
// Identity in render space: for vertices the CPU already made camera-relative.
void setModelRenderSpace();

// One directional sun light + ambient; material white so textures show as-is.
void enableSunLight(const math::Vec3& dirToSun);
void disableLighting();

void enableFog(float nearZ, float farZ, uint32_t abgr);
void disableFog();

// Colour helpers: GU colours are 0xAABBGGRR.
constexpr uint32_t abgr(unsigned r, unsigned g, unsigned b, unsigned a = 255) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

}  // namespace gfx
