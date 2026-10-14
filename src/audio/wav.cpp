#include "audio/sound.h"

#include <cstring>

namespace audio {
namespace {

uint32_t u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

}  // namespace

bool parseWav(const uint8_t* data, size_t size, Sound& out) {
    out = Sound{};
    if (!data || size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
        return false;
    bool haveFormat = false;
    uint32_t rate = 0;
    // Walk the chunks: "fmt " must precede "data"; anything else (LIST, fact) is skipped.
    for (size_t at = 12; at + 8 <= size;) {
        const uint8_t* chunk = data + at;
        const uint32_t len = u32(chunk + 4);
        const size_t body = at + 8;
        if (len > size - body) return false;
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (len < 16) return false;
            const uint16_t format = u16(chunk + 8), channels = u16(chunk + 10), bits = u16(chunk + 22);
            rate = u32(chunk + 12);
            if (format != 1 || channels != 1 || bits != 16 || rate < 8000 || rate > 48000) return false;
            haveFormat = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            // The mixer reads int16 directly from the blob: the samples must be 2-aligned.
            if (!haveFormat || (body & 1) != 0 || len < 4) return false;
            out.samples = reinterpret_cast<const int16_t*>(data + body);
            out.frames = len / 2;
            out.rate = rate;
            return true;
        }
        at = body + len + (len & 1);  // chunks are padded to even length
    }
    return false;
}

}  // namespace audio
