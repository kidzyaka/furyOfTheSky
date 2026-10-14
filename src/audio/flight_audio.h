#pragma once

#include "fx/aircraft_fx.h"
#include "game/flight_model.h"

namespace audio {

struct LoopLevel {
    float gain = 0.0f;
    float pitch = 1.0f;
};

// What the aircraft's continuous sounds should be doing this frame.
struct FlightSoundMix {
    LoopLevel roar, whine, afterburner, wind, buffet;
};

// Sound "director" for one aircraft, heard from the chase camera
// (docs/decisions/0024). Pure mapping from the flight state to loop levels;
// the scene hands them to the mixer. Platform-neutral: host-tested.
class FlightAudio {
public:
    void reset();
    // `transonic` is the effects director's transonic intensity (0..1).
    void update(const game::FlightState& s, const fx::EngineFx& engine, float transonic, float dt);

    const FlightSoundMix& mix() const { return mix_; }
    // Engine spool, ~0.62 at idle to 1 at full power (N2 fraction). Lags the throttle.
    float rpm() const { return rpm_; }

private:
    FlightSoundMix mix_;
    float rpm_ = 0.0f;
};

}  // namespace audio
