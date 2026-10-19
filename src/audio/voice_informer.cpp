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
    "ri_altitude_danger",
    "ri_missile_front_high", "ri_missile_front_low", "ri_missile_rear_high", "ri_missile_rear_low",
    "ri_missile_left_high", "ri_missile_left_low", "ri_missile_right_high", "ri_missile_right_low",
    "ri_fire_left", "ri_fire_right",
    "ri_g_limit", "ri_aoa_limit", "ri_speed_limit", "ri_launch_authorized",
};

// Advisories are said once per activation; warnings repeat (docs/decisions/0026).
bool oneShot(int i) {
    return i == static_cast<int>(Message::LaunchAuthorized) || i == static_cast<int>(Message::FireLeft) ||
           i == static_cast<int>(Message::FireRight);
}

// Quadrant (front/rear/left/right) and above/below of a direction in body axes.
Message missileMessage(const game::FlightState& s, const math::Vec3& dir) {
    const math::Vec3 b = s.orientation.conjugate().rotate(dir);  // -Z forward, +X right
    const bool high = dir.y >= 0.0f;                              // above/below the horizon
    const int base = static_cast<int>(Message::MissileFrontHigh);
    int quadrant;
    if (std::fabs(b.z) >= std::fabs(b.x))
        quadrant = b.z < 0.0f ? 0 : 1;  // front / rear
    else
        quadrant = b.x < 0.0f ? 2 : 3;  // left / right
    return static_cast<Message>(base + quadrant * 2 + (high ? 0 : 1));
}

}  // namespace

const char* messageFile(Message m) { return m < Message::Count ? kFiles[static_cast<int>(m)] : ""; }

void VoiceInformer::reset() {
    for (int i = 0; i < kMessageCount; ++i) active_[i] = said_[i] = false, held_[i] = quiet_[i] = off_[i] = 0.0f;
    speakLeft_ = 0.0f;
    speaking_ = Message::None;
}

void VoiceInformer::evaluate(const game::FlightState& s, float dt) {
    bool raw[kMessageCount] = {};
    if (!s.crashed) {
        const game::GroundWarning ground = game::groundWarning(s, terrain_);
        // Both tiers: with the attention tone the phrase takes ~2 s, so it has
        // to start before the HUD's PULL UP (the Altitude tier, < 9 s to impact).
        raw[static_cast<int>(Message::AltitudeDanger)] = ground != game::GroundWarning::None;

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
        raw[static_cast<int>(Message::LaunchAuthorized)] = launchOk_;
        if (missileOn_) raw[static_cast<int>(missileMessage(s, missileDir_))] = true;
        if (fire_ < 0) raw[static_cast<int>(Message::FireLeft)] = true;
        if (fire_ > 0) raw[static_cast<int>(Message::FireRight)] = true;
    }
    for (int i = 0; i < kMessageCount; ++i) {
        held_[i] = raw[i] ? held_[i] + dt : 0.0f;
        active_[i] = raw[i] && (active_[i] || held_[i] >= kDwell);
        // A launch zone flickering at its edge must not make her repeat herself.
        off_[i] = active_[i] ? 0.0f : off_[i] + dt;
        if (off_[i] > kRearm) said_[i] = false;
    }
}

Message VoiceInformer::update(const game::FlightState& s, float dt) {
    evaluate(s, dt);
    for (float& q : quiet_) q -= dt;
    speakLeft_ -= dt;

    for (int i = 0; i < kMessageCount; ++i) {
        if (!active_[i] || quiet_[i] > 0.0f || said_[i]) continue;
        const Message m = static_cast<Message>(i);
        // One at a time - except that terrain and missiles cut lower messages short.
        const bool urgent = m <= Message::MissileRightLow;
        if (speakLeft_ > 0.0f && !(urgent && speaking_ > m)) return Message::None;
        speaking_ = m;
        said_[i] = oneShot(i);
        speakLeft_ = duration_[i] + kGap;
        quiet_[i] = duration_[i] + kRepeat;
        return m;
    }
    if (speakLeft_ <= 0.0f) speaking_ = Message::None;
    return Message::None;
}

}  // namespace audio
