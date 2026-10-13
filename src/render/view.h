#pragma once

#include <cstdint>

#include "math/quat.h"

namespace gfx {

void setProjection(float fovDeg, float nearZ, float farZ);
void setView(const math::Vec3& eye, const math::Vec3& target, const math::Vec3& up);

void setModelIdentity();
void setModelTranslation(const math::Vec3& pos);
void setModel(const math::Vec3& pos, const math::Quat& rot);

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
