#pragma once

// GE side of the visual effects (docs/decisions/0016). Every function builds
// its geometry camera-relative on the CPU (docs/decisions/0013) into the
// display list, sets the blend/depth state it needs and restores the defaults
// (blend off, depth writes on, culling on, model matrix = render space).

#include <cstdint>

#include "fx/aircraft_fx.h"
#include "game/countermeasures.h"
#include "math/quat.h"
#include "render/texture.h"

namespace gfx {

struct FxTextures {
    const Texture* glow;
    const Texture* smoke;
    const Texture* flame;
    const Texture* vapor;
    const Texture* ring;
    const Texture* ribbon;
    const Texture* shadow;
};

// One engine's nozzle exit, after thrust-vectoring deflection.
struct NozzleFrame {
    math::Vec3 exit;  // world
    math::Vec3 axis;  // unit, along the jet (aft)
};

void drawEngineFlames(const fx::EngineFx& engine, const NozzleFrame nozzles[2], const FxTextures& tex);
// Condensation puffs over the LERX/wing roots, attached to the airframe.
void drawLerxVapor(const fx::AircraftEffects& fx, const math::Vec3& pos, const math::Quat& rot,
                   const FxTextures& tex);
// Transonic "shock collar" around the airframe, aligned with the flight path.
void drawVaporCone(float intensity, const math::Vec3& pos, const math::Vec3& pathDir, float time,
                   const FxTextures& tex);
// Sonic boom: expanding rings around the aircraft.
void drawShockRings(const fx::AircraftEffects& fx, const math::Vec3& pos, const math::Vec3& pathDir,
                    const FxTextures& tex);
// Ribbons: wingtip vortices, contrails, flare smoke.
void drawTrails(const fx::Trail* const* trails, int count, const FxTextures& tex);
void drawParticles(const fx::ParticleSystem& particles, const FxTextures& tex);
void drawFlares(const game::Countermeasures& cm, float time, const FxTextures& tex);
void drawSpeedStreaks(const fx::AircraftEffects& fx, const math::Vec3& pathDir, float speed, const FxTextures& tex);
// Blob shadow of the aircraft on the sea, fading out with altitude.
void drawShadow(const math::Vec3& pos, const math::Quat& rot, const math::Vec3& dirToSun, const FxTextures& tex);

// Screen-space (2D) overlays.
void drawScreenTint(uint32_t abgr);  // full-screen alpha-blended quad (boom flash, inside a cloud)
void drawLensFlare(float sunX, float sunY, float visibility, const FxTextures& tex);

}  // namespace gfx
