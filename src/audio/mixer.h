#pragma once

#include <cstdint>

#include "audio/sound.h"

namespace audio {

constexpr int kOutputRate = 44100;  // the PSP's sceAudio channels run at this rate only
constexpr int kMaxBlock = 1024;     // frames per mix() call, at most

// Fixed channel layout (docs/decisions/0024): every sound has its own slot,
// so the game just re-targets loops each frame - no voice allocation.
enum Channel : uint8_t {
    kEngineRoar,     // jet mixing noise, the bulk of what is heard behind a jet
    kEngineWhine,    // turbine tones, follow spool speed
    kAfterburner,    // roar + crackle
    kWind,           // airflow over the airframe, grows with dynamic pressure
    kBuffet,         // low rumble: high AoA, G, transonic buffet
    kOneShot0,       // flares, light-up, explosions, launches, hits (round robin)
    kOneShot1,
    kOneShot2,
    kOneShot3,
    kGun,            // cannon burst loop while the trigger is held (not ducked)
    kTone,           // missile seeker / lock tone in the headset (not ducked)
    kRwr,            // radar warning receiver: lock / missile tones (not ducked)
    kVoice,          // voice warning system (RI-65), one message at a time
    kUi,             // menu clicks: not muted by the pause duck
    kChannelCount
};
constexpr int kOneShotCount = 4;

// Master volumes (the settings screen, docs/decisions/0038): one gain per
// group of channels, on top of every channel's own level.
enum VolumeGroup : uint8_t {
    kVolumeSfx,    // everything in the world: engines, wind, weapons, warnings tones
    kVolumeVoice,  // kVoice, the RI-65 informer
    kVolumeUi,     // kUi, menu clicks
    kVolumeGroupCount
};
// The setting's 0..10 steps as a gain: 8 (the default) is the level the
// sounds were authored at, 10 is +4 dB for a quiet PSP speaker (the limiter
// catches the peaks), 0 is silence; in between it is a square law.
float volumeGain(int step);

// Software mixer: any number of mono clips, resampled (linear interpolation),
// gain/pan-ramped per block, summed into interleaved stereo with a peak limiter.
// Loops are ducked while the voice channel speaks.
// Threading: the game thread only calls the "game side" methods, which push
// commands into a single-producer/single-consumer ring; the audio thread
// drains it at the start of mix(). Nothing else is shared, so no locks
// (the Allegrex has no LL/SC anyway). Host tests call both sides on one thread.
class Mixer {
public:
    // --- Game side.
    // Starts `sound` on `channel` from the beginning (replacing what played there).
    void play(int channel, const Sound* sound, float gain, float pitch = 1.0f, float pan = 0.0f,
              bool loop = false);
    // Re-targets a playing channel; the change is ramped over one block (no zipper noise).
    void set(int channel, float gain, float pitch, float pan = 0.0f);
    void stop(int channel);
    // Gain of every channel except kUi (pause menu ducking), ramped.
    void setWorldGain(float gain);
    // Master gain of a group of channels (see VolumeGroup), ramped; 1 = as authored.
    void setVolume(VolumeGroup group, float gain);
    // Next one-shot slot, round robin.
    int nextOneShot();

    // --- Audio side.
    // Writes `frames` (<= kMaxBlock) interleaved stereo int16 frames.
    void mix(int16_t* out, int frames);
    // For tests and the debug overlay: is the channel producing sound?
    bool active(int channel) const { return voices_[channel].sound != nullptr; }

private:
    enum Op : uint8_t { kPlay, kSet, kStop, kWorldGain, kVolume };
    struct Command {
        Op op;
        uint8_t channel;
        bool loop;
        const Sound* sound;
        float gain, pitch, pan;
    };
    struct Voice {
        const Sound* sound = nullptr;
        uint32_t pos = 0;      // 20.12 fixed point, frames
        uint32_t step = 0;     // 20.12, per output frame
        bool loop = false;
        bool snap = false;     // jump to the target gain instead of ramping
        int32_t left = 0, right = 0;  // current gains, Q14
        float gain = 0.0f, pitch = 1.0f, pan = 0.0f;  // targets
    };

    void push(const Command& c);
    void apply(const Command& c);
    void mixVoice(Voice& v, int channel, float duck, int frames);

    static constexpr unsigned kRing = 64;  // power of two
    Command ring_[kRing];
    volatile unsigned head_ = 0;  // written by the game thread only
    volatile unsigned tail_ = 0;  // written by the audio thread only

    Voice voices_[kChannelCount];
    float worldGain_ = 1.0f;
    float volume_[kVolumeGroupCount] = {1.0f, 1.0f, 1.0f};
    int32_t limiter_ = 1 << 14;  // Q14
    int32_t acc_[kMaxBlock * 2];
    int oneShot_ = 0;  // game side
};

// The game's single mixer, fed by platform/psp_audio on the PSP.
Mixer& mixer();

}  // namespace audio
