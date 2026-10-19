#pragma once

#include "game/combat.h"

namespace game {

// Pulse-Doppler fire-control radar (docs/decisions/0027).
struct RadarParams {
    float searchAz;     // rad, half-width of the search volume (electronic scan)
    float searchEl;     // rad, half-height
    float trackLimit;   // rad off the nose a designated target can still be held
                        // (the antenna is also turned mechanically)
    float rangeRef;     // m, detection range against 3 m^2
    float scanPeriod;   // s, one pass over the search volume (track-while-scan)
    float notchSpeed;   // m/s, radial ground speed below which a target is lost in clutter
    float clutterAngle; // rad, look-down: LOS below this elevation has sea behind the target
    float memory;       // s a track is extrapolated after the last return
};

// Su-35S N035 Irbis-E.
extern const RadarParams kIrbisE;

struct Track {
    bool valid = false;
    math::Vec3 pos, vel;     // estimate, world (extrapolated between returns)
    float sinceReturn = 0.0f;  // s since the last return; > 0 means coasting on memory
    bool notched = false;    // the last look found it hidden in the clutter
    bool masked = false;     // the last look was blocked by terrain: short memory (kTerrainMemory)
};

// A target gone behind a rock or an island is let go after this, not after
// the full memory: coasting on a prediction, the HUD box would sit on the
// rock for seconds (author's feedback). Long enough not to blink passing a
// thin stack.
constexpr float kTerrainMemory = 0.5f;  // s

// Track-while-scan over the search volume, plus a continuous track on the
// designated target (STT-like: every step, out to the antenna limit).
// One track slot per target in the TargetList. A look succeeds if the target
// is in the beam's reach, within the detection range for its aspect-dependent
// RCS, and not in the Doppler notch: a target crossing the beam with no
// radial ground speed, seen against the sea, looks like sea clutter and is
// filtered out - "notching" a pulse-Doppler radar works here as it does for real.
class Radar {
public:
    explicit Radar(const RadarParams& params = kIrbisE) : p_(params) {}

    void reset();
    void update(const FlightState& own, const TargetList& targets, float dt);

    const Track& track(int i) const { return tracks_[i]; }
    // Designated target (gets the continuous track and the missiles), -1 none.
    int designated() const { return designated_; }
    // Next valid track, ordered by angle off the nose (the "target" button).
    void cycle(const FlightState& own);
    // A ship's fire-control radar (docs/decisions/0045) designates only what
    // it engages: no automatic designation, explicit lock and release.
    void setAutoDesignate(bool on) { auto_ = on; }
    void designate(int i) { designated_ = i >= 0 && i < kMaxTargets && tracks_[i].valid ? i : -1; }
    void release() { designated_ = -1; }
    const RadarParams& params() const { return p_; }
    // Where the search volume is in the scan cycle, 0..1 (for the HUD sweep).
    float scanPhase() const { return scanTimer_ / p_.scanPeriod; }

    // Would a look at `t` from `own` see it now? `designated` widens the reach
    // to the antenna limit. Public for the tests and the missile's datalink.
    // `surface`: a ship - sea-surface mode: its big echo, no clutter notch (docs/decisions/0045).
    bool sees(const FlightState& own, const FlightState& t, bool designated, bool* notched = nullptr,
              bool surface = false) const;
    // Does its beam reach `t` at all (antenna coverage and one-way range)? What a
    // radar warning receiver on `t` hears - no RCS, no Doppler notch involved:
    // a notching target still gets illuminated, the radar just can't see it.
    bool beamReaches(const FlightState& own, const FlightState& t, bool designated) const;

private:
    void autoDesignate(const FlightState& own);

    const RadarParams& p_;
    Track tracks_[kMaxTargets];
    int designated_ = -1;
    float scanTimer_ = 0.0f;
    bool auto_ = true;
};

}  // namespace game
