#pragma once

// GE side of the visual effects (docs/decisions/0016). Every function builds
// its geometry camera-relative on the CPU (docs/decisions/0013) into the
// display list, sets the blend/depth state it needs and restores the defaults
// (blend off, depth writes on, culling on, model matrix = render space).

#include <cstdint>

#include "fx/aircraft_fx.h"
#include "fx/precipitation.h"
#include "game/armament.h"
#include "game/countermeasures.h"
#include "math/quat.h"
#include "render/texture.h"

namespace gfx {

struct FxTextures {
    const Texture* glow;
    const Texture* smoke;
    const Texture* flame;
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
// Transonic condensation (docs/decisions/0020): a collar around the fuselage
// and sheets on the wing tops, each ending sharply at its shock.
void drawTransonicVapor(const fx::AircraftEffects& fx, const math::Vec3& pos, const math::Quat& rot,
                        const FxTextures& tex);
// Ribbons: wingtip vortices, contrails, flare smoke.
void drawTrails(const fx::Trail* const* trails, int count, const FxTextures& tex);
void drawParticles(const fx::ParticleSystem& particles, const FxTextures& tex);
void drawFlares(const game::Countermeasures& cm, float time, const FxTextures& tex);
void drawSpeedStreaks(const fx::AircraftEffects& fx, const math::Vec3& pathDir, float speed, const FxTextures& tex);
// Marks lying on the sea (docs/decisions/0044): foam patches and spreading
// rings, flat just above the waves, additive. `foam`: the sea_foam texture.
void drawWaterMarks(const fx::WaterMarks& marks, const Texture& foam, const FxTextures& tex);
// Surf round a rock (docs/decisions/0042): a foam ring along its waterline,
// `radii[k]` at azimuth 2 pi k / count (theta = atan2(z, x)), `width` m wide,
// additive with `strength` 0..1, crawling outwards with time.
void drawSurfRing(const math::Vec3& centre, int count, const float* radii, float width, float strength, float time,
                  const Texture& foam);
// Rain streaks / snowflakes around the camera (docs/decisions/0041), drawn
// motion-blurred by their speed relative to the camera; `tint` is their
// colour and opacity (lit by the scene's light).
void drawPrecipitation(const fx::Precipitation& p, const math::Vec3& cameraVelocity, uint32_t tint,
                       const FxTextures& tex);
// Weapons (docs/decisions/0026): rocket-motor flames of the missiles in
// flight, 30 mm tracers, the cannon's muzzle flash.
void drawMissileMotors(const game::Missile* missiles, int count, const FxTextures& tex);
void drawTracers(const game::Gun& gun, const FxTextures& tex);
void drawMuzzleFlash(const math::Vec3& muzzle, const math::Vec3& dir, float intensity, const FxTextures& tex);
// Blob shadow of the aircraft on the sea, fading out with altitude.
// `strength` 0..1: the shadow softens away under an overcast.
void drawShadow(const math::Vec3& pos, const math::Quat& rot, const math::Vec3& dirToSun, const FxTextures& tex,
                float strength = 1.0f);

// Screen-space (2D) overlays.
void drawScreenTint(uint32_t abgr);  // full-screen alpha-blended quad (inside a cloud, pause)
void drawLensFlare(float sunX, float sunY, float visibility, const FxTextures& tex);

}  // namespace gfx
