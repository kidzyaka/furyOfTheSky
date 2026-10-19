#pragma once

#include "fx/particles.h"
#include "fx/water_marks.h"
#include "game/armament.h"
#include "game/combat.h"

namespace fx {

// Effects of the weapons (docs/decisions/0026): rocket-motor smoke, warhead
// bursts, 30 mm strikes and splashes, launch and ejection puffs, the muzzle
// flash, smoke from a damaged target. Simulation only, like AircraftEffects;
// render/fx_draw turns it into GE work. Physically sized: a missile's smoke
// only while its motor burns, a warhead burst a few metres across.
class WeaponEffects {
public:
    static constexpr int kSmokeTrails = 16;  // more than missile slots: old smoke fades while new missiles fly

    void reset();
    // `weapons`: the shooter's missiles, events and gun (an aircraft's
    // Armament::view(), a ship's). `target` / `targetHealth`: the drone, for
    // its damage smoke (null: none).
    void update(const game::WeaponView& weapons, const game::HitQueue& hits, const game::FlightState& aircraft,
                const game::FlightState* target, float targetHealth, float dt);
    // The target jumped (respawn): its smoke ribbon must not stretch across the sky.
    void targetMoved() { damageSmoke_.clear(); }

    const ParticleSystem& particles() const { return particles_; }
    const WaterMarks& waterMarks() const { return marks_; }
    const Trail& smoke(int i) const { return smoke_[i]; }
    const Trail& damageSmoke() const { return damageSmoke_; }
    float muzzleFlash() const { return muzzle_; }  // 0..1, flickers while firing

private:
    void burst(const math::Vec3& pos, const game::MissileParams* type);
    void splash(const math::Vec3& pos, float scale);
    void rockHit(const math::Vec3& pos, float scale);
    void strike(const math::Vec3& pos, const math::Vec3& vel);
    void puff(const math::Vec3& pos, const math::Vec3& vel, float size, uint32_t color, float life);

    ParticleSystem particles_;
    WaterMarks marks_;
    Trail smoke_[kSmokeTrails];
    int trailOf_[game::Armament::kMaxMissiles];
    float prevAge_[game::Armament::kMaxMissiles];
    int nextTrail_ = 0;
    Trail damageSmoke_;
    float muzzle_ = 0.0f;
    Rng rng_{0x5EED0042u};
};

}  // namespace fx
