#pragma once

// Per-aircraft tuning for the arcade flight model (docs/decisions/0010).
// Values are *gameplay* numbers, loosely anchored to the real aircraft;
// accelerations are in m/s^2 so no mass is needed.

namespace game {

struct AircraftParams {
    const char* name;

    // Engine. Thrust at throttle t is t * thrustMil, or thrustAB with afterburner.
    float thrustMil;        // m/s^2
    float thrustAB;         // m/s^2
    float cruiseThrottle;   // throttle with no button held
    float idleThrottle;     // throttle while braking
    float throttleRate;     // 1/s, spool-up/down responsiveness

    // Drag: dragCoef * v^2 (x airbrakeFactor when braking) + inducedDrag * n^2.
    float dragCoef;         // 1/m
    float inducedDrag;      // m/s^2 per g^2 of load factor
    float airbrakeFactor;

    // Speeds, m/s.
    float stallSpeed;       // below this lift fades, nose drops
    float cornerSpeed;      // full control authority from here up
    float maxSpeed;         // hard cap (dives)

    // Handling.
    float maxG;             // structural/G-limiter, caps pitch rate at speed
    float maxPitchRate;     // rad/s
    float maxRollRate;      // rad/s
    float maxYawRate;       // rad/s
    float rateResponse;     // 1/s, how fast body rates follow the stick
    float alignRate;        // 1/s, how fast the flight path follows the nose
    float stallNoseDrop;    // 1/s, nose falls toward the flight path when stalled
};

// Su-35S: cruise ~920 km/h, level top speed ~1860 km/h with afterburner,
// best turn ~35 deg/s around 630 km/h, 11 g arcade limiter.
constexpr AircraftParams kSu35S = {
    "Su-35S",
    /*thrustMil*/ 7.0f,
    /*thrustAB*/ 13.0f,
    /*cruiseThrottle*/ 0.45f,
    /*idleThrottle*/ 0.05f,
    /*throttleRate*/ 1.6f,
    /*dragCoef*/ 4.85e-5f,
    /*inducedDrag*/ 0.11f,
    /*airbrakeFactor*/ 3.8f,
    /*stallSpeed*/ 55.0f,
    /*cornerSpeed*/ 200.0f,
    /*maxSpeed*/ 690.0f,
    /*maxG*/ 11.0f,
    /*maxPitchRate*/ 0.80f,
    /*maxRollRate*/ 3.5f,
    /*maxYawRate*/ 0.25f,
    /*rateResponse*/ 5.0f,
    /*alignRate*/ 2.6f,
    /*stallNoseDrop*/ 1.2f,
};

}  // namespace game
