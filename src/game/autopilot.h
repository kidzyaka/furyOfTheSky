#pragma once

#include "game/controls.h"
#include "game/flight_model.h"

namespace game {

// What an AI pilot wants the flight path to do: the kinematic acceleration
// perpendicular to the path (world, m/s^2, gravity NOT included - "turn the
// path this way this hard") and the airspeed to hold.
struct PilotDemand {
    math::Vec3 turn;
    float speed = 250.0f;  // m/s
    float maxG = 9.0f;     // never pull harder than this
    // Limiter off (the player's "L + full pull"): post-stall nose pointing when
    // slow, the 13 g override when fast. For snap shots and last-ditch breaks.
    bool limiterOff = false;
};

// Flies an aircraft through the same FlightModel and FCS as the player, with
// pad-like inputs: bank to put the lift vector where the demand (plus 1 g
// against gravity) needs it, pull for the G it takes, hold speed with the
// throttle buttons. No cheating physics - the drone turns, bleeds energy and
// stalls like the player does (docs/decisions/0029).
ControlInput autopilot(const FlightState& s, const AircraftParams& p, const PilotDemand& d);

// Demand that steers the path towards `dir` (unit, world): proportional to the
// angle off, saturating at `g` (in g). Useful for "fly there" behaviours.
math::Vec3 turnTowards(const FlightState& s, const math::Vec3& dir, float g);

}  // namespace game
