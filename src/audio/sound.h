#pragma once

#include <cstddef>
#include <cstdint>

namespace audio {

// Mono 16-bit PCM clip. Does not own the samples: they live in the file blob
// the clip was parsed from (loaded once at boot, never freed).
struct Sound {
    const int16_t* samples = nullptr;
    uint32_t frames = 0;
    uint32_t rate = 0;  // Hz

    explicit operator bool() const { return samples != nullptr && frames > 1; }
    float seconds() const { return rate ? static_cast<float>(frames) / static_cast<float>(rate) : 0.0f; }
};

// Accepts the WAV subset written by tools/audio (docs/formats/wav.md): RIFF/WAVE,
// PCM (format 1), mono, 16 bit, any rate 8000..48000, little-endian host.
// `out` points into `data`. Returns false on anything else.
bool parseWav(const uint8_t* data, size_t size, Sound& out);

}  // namespace audio
