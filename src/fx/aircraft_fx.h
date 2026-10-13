#pragma once

#include "fx/particles.h"
#include "game/countermeasures.h"
#include "game/flight_model.h"

namespace fx {

// A lifting surface's root and tip in body axes, for effects lying on it.
struct WingSpan {
    float x, y, zLE, chord;  // y: upper surface height; zLE: leading edge (+Z aft)
};

// Where effects attach to the airframe: body axes (+X right, +Y up, +Z aft), m.
// Right side only where symmetric; the left side mirrors X.
struct AttachPoints {
    static constexpr int kVaporPuffs = 7;
    math::Vec3 nozzleExit;       // right engine, neutral nozzle
    math::Vec3 wingtip;          // right wingtip trailing edge
    math::Vec3 vapor[kVaporPuffs];  // condensation over the LERX and wing root
    float vaporSize[kVaporPuffs];   // sprite half size, m
    float vaporOnset[kVaporPuffs];  // vapor intensity at which this puff shows
    WingSpan wingRoot, wingTip;  // right wing, for the transonic condensation sheet
    float canopyZ;               // where the shock collar starts (expansion over the canopy)
};
extern const AttachPoints kSu35SPoints;

struct EngineFx {
    float afterburner = 0.0f;  // flame length factor: 0..1, overshoots briefly at light-up
    float ignition = 0.0f;     // 1 at light-up, decays: the flash
    float heat = 0.0f;         // 0..1 nozzle glow at dry power
    float flicker = 0.0f;      // 0..1, new every step
};

// Effect "director" for one aircraft (docs/decisions/0016, 0020): turns the
// flight state into flame levels, condensation, trails, the transonic shock
// collar, flare smoke, spray and speed streaks. Runs in the fixed step.
class AircraftEffects {
public:
    static constexpr int kStreaks = 28;

    explicit AircraftEffects(const AttachPoints& points = kSu35SPoints) : points_(points) {}

    void reset();
    void update(const game::FlightState& s, const game::Countermeasures& cm, const math::Vec3& eye,
                const math::Vec3& camForward, float dt);
    // True once per Mach 1 crossing. Nothing is *seen* at that moment in real
    // life (docs/decisions/0020); the event is kept for audio (the boom is
    // heard by observers, never by the pilot) and for the smoke-test camera.
    bool takeMachOne() {
        bool b = machOne_;
        machOne_ = false;
        return b;
    }

    const AttachPoints& points() const { return points_; }
    const EngineFx& engine() const { return engine_; }
    float vapor() const { return vapor_; }
    // Transonic condensation 0..1 and where its terminating shock sits:
    // 0 = mid-airframe (M 0.88), 1 = at the trailing edges (M 1.0),
    // > 1 = swept off the back as the flow goes supersonic.
    float transonic() const { return transonic_; }
    float shockPosition() const { return shock_; }
    float time() const { return time_; }
    const ParticleSystem& particles() const { return particles_; }
    const Trail& wingtipTrail(int side) const { return tips_[side]; }
    const Trail& contrail(int side) const { return contrails_[side]; }
    const Trail& flareTrail(int i) const { return flareTrails_[i]; }
    const math::Vec3& streak(int i) const { return streaks_[i]; }
    float streakAlpha() const { return streakAlpha_; }

private:
    math::Vec3 attach(const game::FlightState& s, math::Vec3 body, int side) const;
    void explode(const game::FlightState& s);
    void spray(const game::FlightState& s, float dt);
    void updateStreaks(const game::FlightState& s, const math::Vec3& eye, const math::Vec3& fwd);

    const AttachPoints& points_;
    EngineFx engine_;
    float abLevel_ = 0.0f;
    bool prevAb_ = false;
    float vapor_ = 0.0f, transonic_ = 0.0f, shock_ = 0.0f;
    float prevMach_ = 0.0f;
    bool machOne_ = false;
    bool wasCrashed_ = false;
    float sprayAcc_ = 0.0f;
    float time_ = 0.0f;
    ParticleSystem particles_;
    Trail tips_[2], contrails_[2];
    Trail flareTrails_[game::Countermeasures::kMaxFlares];
    math::Vec3 streaks_[kStreaks];
    bool streaksLive_ = false;
    float streakAlpha_ = 0.0f;
    Rng rng_;
};

}  // namespace fx
