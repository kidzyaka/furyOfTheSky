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

// Camera of the current frame, for CPU-built geometry (sprites, ribbons) and
// screen-space overlays (HUD, lens flare).
struct CameraBasis {
    math::Vec3 eye, right, up, forward;
    float tanHalfX = 1.0f, tanHalfY = 1.0f;
};
const CameraBasis& camera();
// Screen pixels of a world point / of a direction at infinity; false if behind
// the camera (the coordinates are still filled, mirrored, for edge clamping).
bool projectPoint(const math::Vec3& world, float& sx, float& sy);
bool projectDirection(const math::Vec3& dir, float& sx, float& sy);
// Could a sphere (world centre, radius) be on screen? Cheap cone test against
// the current camera, conservative; also false beyond `maxDistance`.
bool sphereVisible(const math::Vec3& centre, float radius, float maxDistance);

// World-space placement (origin subtracted internally).
void setModel(const math::Vec3& worldPos, const math::Quat& rot);
// The same with a scale along the model's own axes first (16-bit vertices
// carry positions as fractions of it, docs/formats/ftr2.md).
void setModel(const math::Vec3& worldPos, const math::Quat& rot, const math::Vec3& scale);
void setModelTranslation(const math::Vec3& worldPos);
// Translation and a per-axis scale (LOD ring grids stored in tile units).
void setModelTranslationScale(const math::Vec3& worldPos, float sx, float sy, float sz);
// Identity in render space: for vertices the CPU already made camera-relative.
void setModelRenderSpace();

// Outdoor light rig (docs/decisions/0019): directional sun with specular,
// blue sky fill from above, dim sea bounce from below, low ambient. Material
// white so textures show as-is.
void enableSunLight(const math::Vec3& dirToSun);
// Light 3 as a point light (afterburner glow on the tail); strength 0 = off.
void setPointLight(const math::Vec3& worldPos, uint32_t color, float strength);
void disableLighting();
// Indoor rig for the menu hangar (docs/assets/hangar.md): a key light (door
// daylight), a fill (overhead lamps), a floor bounce and ambient - matched
// to the hangar's baked lighting.
struct LightRig {
    math::Vec3 keyDir, fillDir, bounceDir;  // directions TOWARDS the lights
    uint32_t key, keySpecular, fill, bounce, ambient;
};
void enableLightRig(const LightRig& rig);
// Distant objects (docs/decisions/0032): sun diffuse + ambient only - the GE
// lights every vertex per light, and a few-pixel aircraft doesn't need the
// sky/sea fill, the specular or the afterburner point light.
// Defaults: the clear-day sun (255, 242, 222) and ambient (92, 106, 124), ABGR.
void enableDistantLight(const math::Vec3& dirToSun, uint32_t key = 0xFFDEF2FFu, uint32_t ambient = 0xFF7C6A5Cu);

void enableFog(float nearZ, float farZ, uint32_t abgr);
// The fog set by the last enableFog (to stretch it for one layer and restore).
void currentFog(float& nearZ, float& farZ, uint32_t& abgr);
void disableFog();
// Additive effects must fade to *black* in the distance, not to the haze
// colour (fog is applied before blending): swap the fog colour around them.
void setAdditiveFog(bool additive);

// Colour helpers: GU colours are 0xAABBGGRR.
constexpr uint32_t abgr(unsigned r, unsigned g, unsigned b, unsigned a = 255) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

}  // namespace gfx
