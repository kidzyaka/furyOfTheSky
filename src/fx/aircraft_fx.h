#pragma once

#include "fx/particles.h"
#include "game/countermeasures.h"
#include "game/flight_model.h"

namespace fx {

// Where effects attach to the airframe: body axes (+X right, +Y up, +Z aft), m.
// Right side only where symmetric; the left side mirrors X.
struct AttachPoints {
    static constexpr int kVaporPuffs = 7;
    math::Vec3 nozzleExit;       // right engine, neutral nozzle
    math::Vec3 wingtip;          // right wingtip trailing edge
    math::Vec3 vapor[kVaporPuffs];  // condensation over the LERX and wing root
    float vaporSize[kVaporPuffs];   // sprite half size, m
    float vaporOnset[kVaporPuffs];  // vapor intensity at which this puff shows
};
extern const AttachPoints kSu35SPoints;

struct EngineFx {
    float afterburner = 0.0f;  // flame length factor: 0..1, overshoots briefly at light-up
    float ignition = 0.0f;     // 1 at light-up, decays: the flash
    float heat = 0.0f;         // 0..1 nozzle glow at dry power
    float flicker = 0.0f;      // 0..1, new every step
};

// Shock collar of the boom: expands around the aircraft and travels with it
// (the real shock cone is attached to the airframe), so it reads from behind.
struct ShockRing {
    float age = -1.0f;  // < 0: unused
};

// Effect "director" for one aircraft (docs/decisions/0016): turns the flight
// state into flame levels, condensation, trails, the sonic boom, flare smoke,
// spray and speed streaks. Runs in the fixed simulation step.
class AircraftEffects {
public:
    static constexpr int kStreaks = 28;
    static constexpr int kRings = 3;
    static constexpr float kRingLife = 0.9f;

    explicit AircraftEffects(const AttachPoints& points = kSu35SPoints) : points_(points) {}

    void reset();
    void update(const game::FlightState& s, const game::Countermeasures& cm, const math::Vec3& eye,
                const math::Vec3& camForward, float dt);
    // True once per Mach 1 crossing (camera kick, flash).
    bool takeSonicBoom() {
        bool b = boom_;
        boom_ = false;
        return b;
    }

    const AttachPoints& points() const { return points_; }
    const EngineFx& engine() const { return engine_; }
    float vapor() const { return vapor_; }
    float vaporCone() const { return cone_; }
    float flash() const { return flash_; }
    float time() const { return time_; }
    const ParticleSystem& particles() const { return particles_; }
    const Trail& wingtipTrail(int side) const { return tips_[side]; }
    const Trail& contrail(int side) const { return contrails_[side]; }
    const Trail& flareTrail(int i) const { return flareTrails_[i]; }
    const ShockRing& ring(int i) const { return rings_[i]; }
    const math::Vec3& streak(int i) const { return streaks_[i]; }
    float streakAlpha() const { return streakAlpha_; }

private:
    math::Vec3 attach(const game::FlightState& s, math::Vec3 body, int side) const;
    void boom(const game::FlightState& s);
    void explode(const game::FlightState& s);
    void spray(const game::FlightState& s, float dt);
    void updateStreaks(const game::FlightState& s, const math::Vec3& eye, const math::Vec3& fwd);

    const AttachPoints& points_;
    EngineFx engine_;
    float abLevel_ = 0.0f;
    bool prevAb_ = false;
    float vapor_ = 0.0f, cone_ = 0.0f, flash_ = 0.0f;
    float prevMach_ = 0.0f, boomCooldown_ = 0.0f;
    bool boom_ = false;
    bool wasCrashed_ = false;
    float sprayAcc_ = 0.0f;
    float time_ = 0.0f;
    ParticleSystem particles_;
    Trail tips_[2], contrails_[2];
    Trail flareTrails_[game::Countermeasures::kMaxFlares];
    ShockRing rings_[kRings];
    math::Vec3 streaks_[kStreaks];
    bool streaksLive_ = false;
    float streakAlpha_ = 0.0f;
    Rng rng_;
};

}  // namespace fx
