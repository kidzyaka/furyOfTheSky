#pragma once

#include <cstdint>

#include "game/flight_model.h"
#include "world/terrain.h"

namespace audio {

// Messages of the voice warning system: real RI-65 ("Rita") recordings
// (docs/decisions/0025), each opened by the informer's 1 kHz attention tone.
// Ordered by priority, highest first.
enum class Message : uint8_t {
    AltitudeDanger,  // "Высота опасная"
    // Missile warning with its direction (RWR, docs/decisions/0034), by
    // quadrant around the aircraft and above/below the horizon.
    MissileFrontHigh,  // "Ракета впереди выше"
    MissileFrontLow,
    MissileRearHigh,
    MissileRearLow,
    MissileLeftHigh,
    MissileLeftLow,
    MissileRightHigh,
    MissileRightLow,
    FireLeft,        // "Пожар левого двигателя" (battle damage)
    FireRight,
    GLimit,          // "Предельная перегрузка"
    AoaLimit,        // "Предельный угол атаки"
    SpeedLimit,      // "Скорость предельная"
    LaunchAuthorized,  // "Пуск разрешён": locked and in the launch zone (said once per entry)
    Count,
    None = Count,
};
constexpr int kMessageCount = static_cast<int>(Message::Count);

// Asset name of each message (assets/sfx/<name>.wav).
const char* messageFile(Message m);

// Voice warning logic: watches the flight state, decides which message to say
// and when. One message at a time, highest priority first; a warning repeats
// while its condition holds (kRepeat after it finished), an advisory ("пуск
// разрешён") is said once each time its condition comes on; conditions have
// hysteresis and a short dwell so a limit grazed for one frame stays silent.
// Platform-neutral and deterministic (host-tested).
class VoiceInformer {
public:
    static constexpr float kRepeat = 2.5f;  // s of silence before the same message again
    static constexpr float kGap = 0.35f;    // s between two different messages
    static constexpr float kDwell = 0.25f;  // s a condition must hold before it is spoken
    static constexpr float kRearm = 1.5f;   // s an advisory must be off before it may be said again

    explicit VoiceInformer(const game::AircraftParams& p) : p_(p) {}

    void reset();
    // Clip lengths, so the informer knows when it has finished speaking
    // without asking the audio thread.
    void setDuration(Message m, float seconds) { duration_[static_cast<int>(m)] = seconds; }
    // Weapon state for "пуск разрешён" (set before update()).
    void setLaunchAuthorized(bool on) { launchOk_ = on; }
    // RWR: direction of the most urgent missile (world unit vector), or none.
    void setMissileWarning(bool on, const math::Vec3& dir) {
        missileOn_ = on;
        missileDir_ = dir;
    }
    // Battle damage: an engine on fire (-1 left, +1 right, 0 none).
    void setEngineFire(int side) { fire_ = side; }
    // Rocks on the flight path count for "высота опасная" (same rule as the HUD's PULL UP).
    void setTerrain(const ::world::Terrain* terrain) { terrain_ = terrain; }
    // Returns the message to start speaking now, or Message::None.
    Message update(const game::FlightState& s, float dt);

    bool condition(Message m) const { return active_[static_cast<int>(m)]; }
    bool speaking() const { return speakLeft_ > 0.0f; }

private:
    void evaluate(const game::FlightState& s, float dt);

    const game::AircraftParams& p_;
    bool active_[kMessageCount] = {};
    float held_[kMessageCount] = {};       // s the raw condition has held
    float quiet_[kMessageCount] = {};      // s until this message may repeat
    float duration_[kMessageCount] = {};
    float speakLeft_ = 0.0f;               // s of the current message (plus gap) left
    Message speaking_ = Message::None;
    bool launchOk_ = false;
    bool missileOn_ = false;
    math::Vec3 missileDir_;
    int fire_ = 0;
    bool said_[kMessageCount] = {};        // one-shot messages: spoken for this activation
    float off_[kMessageCount] = {};        // s the condition has been off (re-arms a one-shot)
    const ::world::Terrain* terrain_ = nullptr;
};

}  // namespace audio
