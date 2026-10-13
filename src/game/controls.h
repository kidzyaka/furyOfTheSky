#pragma once

namespace game {

// Pilot intent for one simulation step, already mapped from the pad.
// Platform-neutral so the same struct can come from a network peer or a test.
struct ControlInput {
    float pitch = 0.0f;  // -1..1, +1 = nose up
    float roll = 0.0f;   // -1..1, +1 = roll right (right wing down)
    float yaw = 0.0f;    // -1..1, +1 = nose right
    bool throttleUp = false;    // hold: full power, afterburner once spooled up
    bool throttleDown = false;  // hold: idle + airbrake
    bool psm = false;           // hold: FCS limiter off (post-stall manoeuvring / G override)
    bool flares = false;        // hold: release flare salvos
};

}  // namespace game
