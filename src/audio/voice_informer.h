#pragma once

#include <cstdint>

#include "game/flight_model.h"

namespace audio {

// Messages of the voice warning system. Wording is the RI-65 ("Rita") set as
// flown on Su-27SM-class aircraft (docs/decisions/0024); ordered by priority,
// highest first.
enum class Message : uint8_t {
    PullUp,      // "Переведи в набор"
    Altitude,    // "Высота"
    GLimit,      // "Предельная перегрузка"
    AoaLimit,    // "Предельный угол атаки"
    SpeedLimit,  // "Скорость предельная"
    Count,
    None = Count,
};
constexpr int kMessageCount = static_cast<int>(Message::Count);

// Asset name of each message (assets/sfx/<name>.wav).
const char* messageFile(Message m);

// Voice warning logic: watches the flight state, decides which message to say
// and when. One message at a time, highest priority first; a message repeats
// while its condition holds (kRepeat after it finished); conditions have
// hysteresis and a short dwell so a limit grazed for one frame stays silent.
// Platform-neutral and deterministic (host-tested).
class VoiceInformer {
public:
    static constexpr float kRepeat = 2.5f;  // s of silence before the same message again
    static constexpr float kGap = 0.35f;    // s between two different messages
    static constexpr float kDwell = 0.25f;  // s a condition must hold before it is spoken

    explicit VoiceInformer(const game::AircraftParams& p) : p_(p) {}

    void reset();
    // Clip lengths, so the informer knows when it has finished speaking
    // without asking the audio thread.
    void setDuration(Message m, float seconds) { duration_[static_cast<int>(m)] = seconds; }
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
};

}  // namespace audio
