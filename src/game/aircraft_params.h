#pragma once

// Per-aircraft tuning for the flight model (docs/decisions/0014).
// Values are *gameplay* numbers, loosely anchored to the real aircraft;
// forces are per unit mass (m/s^2), so no mass or wing area is needed.

namespace game {

struct AircraftParams {
    const char* name;

    // Engine. Thrust at throttle t is t * thrustMil, or thrustAB with afterburner.
    float thrustMil;        // m/s^2
    float thrustAB;         // m/s^2
    float cruiseThrottle;   // throttle with no button held
    float idleThrottle;     // throttle while braking
    float throttleRate;     // 1/s, spool-up/down responsiveness

    // Aerodynamics: acceleration = q * coefficient, q = liftK * v^2.
    float liftK;            // 1/m (rho * S / 2m, plus arcade generosity)
    float clSlope;          // lift per rad of AoA near zero
    float clMax;            // asymptote of the attached-flow curve (tanh)
    float clPlate;          // separated flow behaves like a plate: clPlate * sin(2a)
    float alphaStall;       // rad, attached -> separated blend starts here
    float sideForce;        // side force per rad of sideslip
    float cd0;              // parasitic drag
    float cdInduced;        // * CL^2
    float cdPlate;          // * sin^2(AoA): the airframe broadside to the flow (cobra)
    float cdWave;           // transonic drag rise at its Mach 1 peak
    float airbrakeDrag;     // extra multiple of cd0 with the airbrake out
    float speedOfSound;     // m/s, constant (no atmosphere model)
    float maxSpeed;         // m/s, hard cap (dives)

    // Flight control system, normal law: the stick commands AoA up to the
    // limiter, the FCS trims for 1 g and suppresses sideslip.
    float alphaMax;         // rad, AoA limiter
    float alphaMin;         // rad (negative)
    float maxG;             // G-limiter
    float minG;             // negative G limit (as a positive number)
    float overrideG;        // circle held above PSM speed: limiter override
    float authoritySpeed;   // m/s, control surfaces reach full authority here
    float maxPitchRate;     // rad/s from the control surfaces at full authority
    float maxRollRate;      // rad/s
    float sideslipMax;      // rad at full rudder
    float tvcRate;          // rad/s of extra pitch authority from thrust vectoring at MIL
    float alphaGain;        // 1/s, AoA tracking loop
    float betaGain;         // 1/s, sideslip tracking loop
    float rateResponse;     // 1/s, body rates follow the command (actuators + inertia)

    // Post-stall manoeuvring (circle held, FCS limiter off): the stick
    // commands body rates directly, thrust vectoring supplies the moment.
    float psmMaxSpeed;      // m/s; above it circle means G override instead
    float psmPitchRate;     // rad/s
    float psmYawRate;       // rad/s
    float psmRollRate;      // rad/s
    float psmResponse;      // 1/s
    float recoveryRate;     // rad/s, FCS pitch authority bringing the nose back after PSM
};

// Su-35S: cruise ~920 km/h, MIL stays subsonic, the afterburner punches
// through the transonic drag hump in a few seconds and tops out ~1880 km/h;
// ~31 deg/s at 720 km/h corner speed (11 g); cobra and kulbit below 650 km/h.
constexpr AircraftParams kSu35S = {
    "Su-35S",
    /*thrustMil*/ 7.0f,
    /*thrustAB*/ 15.5f,
    /*cruiseThrottle*/ 0.45f,
    /*idleThrottle*/ 0.05f,
    /*throttleRate*/ 2.4f,
    /*liftK*/ 2.48e-3f,
    /*clSlope*/ 3.0f,
    /*clMax*/ 1.8f,
    /*clPlate*/ 1.1f,
    /*alphaStall*/ 0.55f,
    /*sideForce*/ 0.9f,
    /*cd0*/ 0.0196f,
    /*cdInduced*/ 0.118f,
    /*cdPlate*/ 1.6f,
    /*cdWave*/ 0.022f,
    /*airbrakeDrag*/ 2.8f,
    /*speedOfSound*/ 340.0f,
    /*maxSpeed*/ 690.0f,
    /*alphaMax*/ 0.42f,
    /*alphaMin*/ -0.17f,
    /*maxG*/ 11.0f,
    /*minG*/ 3.0f,
    /*overrideG*/ 13.0f,
    /*authoritySpeed*/ 200.0f,
    /*maxPitchRate*/ 0.80f,
    /*maxRollRate*/ 3.5f,
    /*sideslipMax*/ 0.14f,
    /*tvcRate*/ 1.0f,
    /*alphaGain*/ 6.0f,
    /*betaGain*/ 5.0f,
    /*rateResponse*/ 8.0f,
    /*psmMaxSpeed*/ 180.0f,
    /*psmPitchRate*/ 1.75f,
    /*psmYawRate*/ 0.9f,
    /*psmRollRate*/ 2.5f,
    /*psmResponse*/ 5.0f,
    /*recoveryRate*/ 1.3f,
};

}  // namespace game
