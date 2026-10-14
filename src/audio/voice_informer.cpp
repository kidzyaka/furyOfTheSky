#include "audio/voice_informer.h"

#include <cmath>

#include "game/warnings.h"

namespace audio {
namespace {

// Su-35S never-exceed speed near the ground: 1400 km/h IAS. The flight model
// has no atmosphere (IAS = TAS), so this holds at any altitude - a known
// simplification, see docs/decisions/0024.
constexpr float kSpeedLimit = 1400.0f / 3.6f;  // m/s
constexpr float kSpeedClear = 1370.0f / 3.6f;

const char* const kFiles[kMessageCount] = {
    "ri_pull_up", "ri_altitude", "ri_g_limit", "ri_aoa_limit", "ri_speed_limit",
};

}  // namespace

const char* messageFile(Message m) { return m < Message::Count ? kFiles[static_cast<int>(m)] : ""; }

void VoiceInformer::reset() {
    for (int i = 0; i < kMessageCount; ++i) active_[i] = false, held_[i] = quiet_[i] = 0.0f;
    speakLeft_ = 0.0f;
    speaking_ = Message::None;
}

void VoiceInformer::evaluate(const game::FlightState& s, float dt) {
    bool raw[kMessageCount] = {};
    if (!s.crashed) {
        const game::GroundWarning ground = game::groundWarning(s);
        raw[static_cast<int>(Message::PullUp)] = ground == game::GroundWarning::PullUp;
        raw[static_cast<int>(Message::Altitude)] = ground == game::GroundWarning::Altitude;

        // Limits with hysteresis: once on, a message stays on until the value
        // is clearly back inside the envelope.
        const float n = s.loadFactor;
        const bool gOn = active_[static_cast<int>(Message::GLimit)];
        raw[static_cast<int>(Message::GLimit)] =
            gOn ? (n > p_.maxG - 0.5f || n < -(p_.minG - 0.5f)) : (n > p_.maxG + 0.2f || n < -(p_.minG + 0.2f));

        // AoA: the FCS holds alphaMax, so the warning means "you are at the
        // limiter" - or past it, in PSM or a stall (the AUASP's job on older jets).
        const bool aOn = active_[static_cast<int>(Message::AoaLimit)];
        const float a = s.alpha;
        raw[static_cast<int>(Message::AoaLimit)] =
            s.stalled || (aOn ? a > 0.88f * p_.alphaMax : a > 0.96f * p_.alphaMax);

        const bool vOn = active_[static_cast<int>(Message::SpeedLimit)];
        raw[static_cast<int>(Message::SpeedLimit)] = s.speed > (vOn ? kSpeedClear : kSpeedLimit);
    }
    for (int i = 0; i < kMessageCount; ++i) {
        held_[i] = raw[i] ? held_[i] + dt : 0.0f;
        active_[i] = raw[i] && (active_[i] || held_[i] >= kDwell);
    }
}

Message VoiceInformer::update(const game::FlightState& s, float dt) {
    evaluate(s, dt);
    for (float& q : quiet_) q -= dt;
    speakLeft_ -= dt;

    for (int i = 0; i < kMessageCount; ++i) {
        if (!active_[i] || quiet_[i] > 0.0f) continue;
        const Message m = static_cast<Message>(i);
        // One at a time - except that terrain cuts anything else short.
        if (speakLeft_ > 0.0f && !(m == Message::PullUp && speaking_ != Message::PullUp)) return Message::None;
        speaking_ = m;
        speakLeft_ = duration_[i] + kGap;
        quiet_[i] = duration_[i] + kRepeat;
        return m;
    }
    if (speakLeft_ <= 0.0f) speaking_ = Message::None;
    return Message::None;
}

}  // namespace audio
