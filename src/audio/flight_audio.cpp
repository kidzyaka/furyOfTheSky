#include "audio/flight_audio.h"

#include <algorithm>
#include <cmath>

namespace audio {
namespace {

using math::approach;
using math::smoothstep;

constexpr float kIdleRpm = 0.62f;
constexpr float kSpoolUp = 1.1f;    // 1/s: a turbofan winds up in a few seconds
constexpr float kSpoolDown = 0.8f;  // 1/s: and runs down a little slower
constexpr float kLevelRate = 12.0f; // 1/s: smoothing of the loop levels

void track(LoopLevel& l, float gain, float pitch, float k) {
    l.gain += (gain - l.gain) * k;
    l.pitch += (pitch - l.pitch) * k;
}

}  // namespace

void FlightAudio::reset() {
    mix_ = FlightSoundMix{};
    rpm_ = 0.0f;
}

void FlightAudio::update(const game::FlightState& s, const fx::EngineFx& engine, float transonic, float dt) {
    const bool alive = !s.crashed;
    // Engine spool lags the throttle lever; the very first update snaps to it
    // (the sortie starts in cruise, not from a cold engine).
    const float rpmTarget = alive ? kIdleRpm + (1.0f - kIdleRpm) * std::clamp(s.throttle, 0.0f, 1.0f) : 0.0f;
    if (rpm_ <= 0.0f && alive)
        rpm_ = rpmTarget;
    else
        rpm_ += (rpmTarget - rpm_) * approach(rpmTarget > rpm_ ? kSpoolUp : kSpoolDown, dt);
    const float k = approach(kLevelRate, dt);
    if (!alive) {
        for (LoopLevel* l : {&mix_.roar, &mix_.whine, &mix_.afterburner, &mix_.wind, &mix_.buffet})
            track(*l, 0.0f, l->pitch, approach(20.0f, dt));
        return;
    }
    const float r = rpm_;
    const float v = s.speed;

    // Behind the jet the broadband mixing noise dominates and rises steeply
    // with thrust (Lighthill: ~ jet velocity^8, heavily compressed here); the
    // turbine whine radiates mostly forward, so from the chase camera it is a
    // thinner layer on top.
    track(mix_.roar, 0.22f + 0.58f * r * r * r, 0.84f + 0.24f * r, k);
    track(mix_.whine, 0.10f + 0.20f * r, 0.45f + 0.70f * r, k);
    // Afterburner: follows the flame (light-up overshoot included).
    track(mix_.afterburner, 0.95f * std::min(engine.afterburner, 1.25f), 1.0f, k);

    // Airflow noise grows with dynamic pressure (~v^2), pitch with speed.
    const float vRel = std::min(v / 400.0f, 1.3f);
    track(mix_.wind, 0.55f * std::min(std::pow(vRel, 1.6f), 1.2f), 0.6f + 0.6f * vRel, k);

    // Buffet: separated flow at high AoA (only with enough airspeed to shake
    // the airframe), high G, the transonic shock oscillation, a stall.
    const float q = smoothstep(40.0f, 150.0f, v);
    const float aoa = smoothstep(0.28f, 0.55f, std::fabs(s.alpha)) * q;
    const float g = 0.6f * smoothstep(7.0f, 12.0f, std::fabs(s.loadFactor));
    const float shake = std::max({aoa, g, 0.7f * transonic, s.stalled ? 0.5f : 0.0f});
    track(mix_.buffet, 0.85f * shake, 0.85f + 0.3f * std::min(v / 300.0f, 1.0f), k);
}

}  // namespace audio
