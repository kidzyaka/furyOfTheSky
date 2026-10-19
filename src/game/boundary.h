#pragma once

#include <cstdint>

#include "game/flight_model.h"
#include "world/terrain.h"

namespace game {

// The edge of the mission area (docs/decisions/0043). The arena is wide -
// 24 km from its centre, more than any fight needs - so nobody runs into it
// by accident. Past that the HUD says to turn back; whoever keeps going (for
// kGrace seconds, or out to the outer radius) has the nose turned for them:
// the heading swings back towards the centre at up to kTurnRate, nothing else
// of the flight is touched, the stick still flies. Platform-neutral, the same
// rule for the player and the bots.
class Boundary {
public:
    enum class Zone : uint8_t { Inside, Warning, Returning };
    static constexpr float kGrace = 12.0f;        // s of warning before the game takes over
    static constexpr float kTurnRate = 0.26f;     // rad/s (~15 deg/s) at full authority
    static constexpr float kRampTime = 1.5f;      // s to reach it (no snap)
    static constexpr float kReleaseAngle = 0.35f; // rad: heading within this of home ends the turn
    static constexpr float kHysteresis = 400.0f;  // m inside the warning radius before the warning clears

    void reset();
    // Returns the heading change (rad about +Y, + = left) to apply this step.
    float update(const FlightState& s, const ::world::Arena& arena, float dt);

    Zone zone() const { return zone_; }
    // Seconds left before the game turns the aircraft (Warning zone).
    float countdown() const { return kGrace - warnTime_; }
    // Direction home relative to the heading: rad, + = to the right.
    float homeBearing() const { return bearing_; }

private:
    Zone zone_ = Zone::Inside;
    float warnTime_ = 0.0f;
    float authority_ = 0.0f;
    float bearing_ = 0.0f;
};

}  // namespace game
