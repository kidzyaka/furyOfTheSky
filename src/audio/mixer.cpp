#include "audio/mixer.h"

#include <algorithm>
#include <atomic>

namespace audio {
namespace {

constexpr int kFrac = 12;  // fractional bits of the sample position
constexpr int32_t kUnity = 1 << 14;
constexpr float kDuck = 0.55f;  // loop gain under a voice message

// Single core, in-order CPU: ordering only has to survive the compiler.
// (std::atomic_signal_fence is a pure compiler barrier - no libatomic calls,
// which the Allegrex could not service: it has no LL/SC.)
inline void barrier() { std::atomic_signal_fence(std::memory_order_seq_cst); }

int32_t toQ14(float g) { return static_cast<int32_t>(std::clamp(g, 0.0f, 2.0f) * kUnity); }

Mixer g_mixer;

}  // namespace

Mixer& mixer() { return g_mixer; }

float volumeGain(int step) {
    if (step <= 0) return 0.0f;
    const float x = std::min(step, 10) * 0.125f;  // 8 steps = unity
    return x * x;
}

// --- Game side ----------------------------------------------------------------

void Mixer::push(const Command& c) {
    const unsigned head = head_;
    if (head - tail_ >= kRing) return;  // full: drop (loops are re-targeted next frame anyway)
    ring_[head & (kRing - 1)] = c;
    barrier();  // the slot must be written before the consumer can see it
    head_ = head + 1;
}

void Mixer::play(int channel, const Sound* sound, float gain, float pitch, float pan, bool loop) {
    if (!sound || !*sound || channel < 0 || channel >= kChannelCount) return;
    push({kPlay, static_cast<uint8_t>(channel), loop, sound, gain, pitch, pan});
}

void Mixer::set(int channel, float gain, float pitch, float pan) {
    if (channel < 0 || channel >= kChannelCount) return;
    push({kSet, static_cast<uint8_t>(channel), false, nullptr, gain, pitch, pan});
}

void Mixer::stop(int channel) {
    if (channel < 0 || channel >= kChannelCount) return;
    push({kStop, static_cast<uint8_t>(channel), false, nullptr, 0.0f, 1.0f, 0.0f});
}

void Mixer::setWorldGain(float gain) { push({kWorldGain, 0, false, nullptr, gain, 1.0f, 0.0f}); }

void Mixer::setVolume(VolumeGroup group, float gain) {
    if (group >= kVolumeGroupCount) return;
    push({kVolume, static_cast<uint8_t>(group), false, nullptr, gain, 1.0f, 0.0f});
}

int Mixer::nextOneShot() {
    int ch = kOneShot0 + oneShot_;
    oneShot_ = (oneShot_ + 1) % kOneShotCount;
    return ch;
}

// --- Audio side ---------------------------------------------------------------

void Mixer::apply(const Command& c) {
    Voice& v = voices_[c.channel];
    switch (c.op) {
        case kPlay:
            v.sound = c.sound;
            v.pos = 0;
            v.loop = c.loop;
            // Loops fade in over a block (no click); one-shots start at full
            // level so a cartridge pop keeps its attack.
            v.left = v.right = 0;
            v.snap = !c.loop;
            [[fallthrough]];
        case kSet:
            v.gain = c.gain;
            v.pitch = c.pitch;
            v.pan = c.pan;
            break;
        case kStop:
            v.gain = 0.0f;  // ramps out over the next block, then the voice frees itself
            v.loop = false;
            if (v.left == 0 && v.right == 0) v.sound = nullptr;
            break;
        case kWorldGain:
            worldGain_ = c.gain;
            break;
        case kVolume:
            volume_[c.channel] = std::max(c.gain, 0.0f);
            break;
    }
}

void Mixer::mixVoice(Voice& v, int channel, float duck, int frames) {
    const Sound& s = *v.sound;
    // The pause duck holds the world still but not the menu clicks; the
    // master volume depends on the group the channel belongs to.
    const bool world = channel != kUi;
    const float master = volume_[channel == kUi ? kVolumeUi : channel == kVoice ? kVolumeVoice : kVolumeSfx];
    const float g = v.gain * duck * master * (world ? worldGain_ : 1.0f);
    const float pan = std::clamp(v.pan, -1.0f, 1.0f);
    const int32_t tl = toQ14(g * std::min(1.0f, 1.0f - pan));
    const int32_t tr = toQ14(g * std::min(1.0f, 1.0f + pan));
    const float pitch = std::clamp(v.pitch, 0.05f, 4.0f);
    v.step = static_cast<uint32_t>(static_cast<float>(s.rate) * pitch * (1 << kFrac) / kOutputRate);

    // A stopped voice that has faded out is done. One held silent by the pause
    // (or a loop turned down to nothing) stands still - but not by a master
    // volume of 0: those sounds play out unheard instead of waiting for it to
    // be raised and then blurting out stale.
    const bool faded = v.left == 0 && v.right == 0;
    if (faded && v.gain <= 0.0f && !v.loop) {
        v.sound = nullptr;
        return;
    }
    if (faded && tl == 0 && tr == 0 && master > 0.0f && (!v.loop || v.gain <= 0.0f)) return;

    const int16_t* d = s.samples;
    const uint32_t n = s.frames;
    const uint32_t end = n << kFrac;
    if (v.snap) {
        v.left = tl;
        v.right = tr;
        v.snap = false;
    }
    int32_t l = v.left, r = v.right;
    const int32_t dl = (tl - l) / frames, dr = (tr - r) / frames;
    uint32_t pos = v.pos;
    int32_t* acc = acc_;
    bool finished = false;
    for (int i = 0; i < frames; ++i) {
        const uint32_t idx = pos >> kFrac;
        const int32_t a = d[idx];
        const int32_t b = idx + 1 < n ? d[idx + 1] : (v.loop ? d[0] : a);
        const int32_t x = a + (((b - a) * static_cast<int32_t>(pos & ((1 << kFrac) - 1))) >> kFrac);
        acc[0] += (x * l) >> 14;
        acc[1] += (x * r) >> 14;
        acc += 2;
        l += dl;
        r += dr;
        pos += v.step;
        if (pos >= end) {
            if (!v.loop) {
                finished = true;
                break;
            }
            while (pos >= end) pos -= end;
        }
    }
    v.pos = pos;
    v.left = tl;
    v.right = tr;
    if (finished) v.sound = nullptr;
}

void Mixer::mix(int16_t* out, int frames) {
    frames = std::clamp(frames, 1, kMaxBlock);
    // Drain the commands the game queued since the last block.
    for (unsigned tail = tail_; tail != head_; ++tail) {
        barrier();
        apply(ring_[tail & (kRing - 1)]);
        barrier();
        tail_ = tail + 1;
    }

    std::fill(acc_, acc_ + frames * 2, 0);
    // While the informer speaks, the continuous loops step back ~5 dB (a
    // headset voice sits on top of the engine in a real cockpit too).
    // (A voice turned down to nothing does not duck them either.)
    const float duck = voices_[kVoice].sound && volume_[kVolumeVoice] > 0.0f ? kDuck : 1.0f;
    for (int c = 0; c < kChannelCount; ++c)
        if (voices_[c].sound) mixVoice(voices_[c], c, c < kOneShot0 ? duck : 1.0f, frames);

    // Peak limiter: instant attack (per block), ~0.4 s release. Keeps an
    // explosion over the afterburner from hard-clipping into a buzz.
    int32_t peak = 1;
    for (int i = 0; i < frames * 2; ++i) peak = std::max(peak, acc_[i] < 0 ? -acc_[i] : acc_[i]);
    const int32_t want = peak > 32000 ? static_cast<int32_t>((32000LL << 14) / peak) : kUnity;
    const int32_t from = limiter_;
    limiter_ = want < limiter_ ? want : limiter_ + (kUnity - limiter_) / 16;
    const int32_t step = (limiter_ - from) / frames;
    int32_t gain = from;
    for (int i = 0; i < frames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            int32_t x = static_cast<int32_t>((static_cast<int64_t>(acc_[i * 2 + ch]) * gain) >> 14);
            out[i * 2 + ch] = static_cast<int16_t>(std::clamp<int32_t>(x, -32768, 32767));
        }
        gain += step;
    }
}

}  // namespace audio
