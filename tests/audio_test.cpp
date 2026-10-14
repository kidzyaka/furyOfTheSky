// Host-side tests for the audio layer (docs/decisions/0024): the WAV subset,
// the mixer's arithmetic and the voice informer's rules.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "audio/flight_audio.h"
#include "audio/mixer.h"
#include "audio/sound.h"
#include "audio/voice_informer.h"
#include "game/flight_model.h"

using audio::Message;
using audio::Mixer;
using audio::Sound;
using game::kSu35S;

namespace {

int g_failures = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            ++g_failures;                                                   \
            std::printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                       \
            std::printf("\n");                                              \
        }                                                                   \
    } while (0)

constexpr float kDt = 1.0f / 60.0f;

void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
void tag(std::vector<uint8_t>& b, const char* t) { b.insert(b.end(), t, t + 4); }

std::vector<uint8_t> makeWav(uint16_t channels, uint32_t rate, int frames, bool listChunk) {
    std::vector<uint8_t> b;
    tag(b, "RIFF");
    put32(b, 0);  // size: not checked
    tag(b, "WAVE");
    if (listChunk) {  // what ffmpeg writes before the data
        tag(b, "LIST");
        put32(b, 6);
        tag(b, "INFO");
        put16(b, 0);
    }
    tag(b, "fmt ");
    put32(b, 16);
    put16(b, 1);
    put16(b, channels);
    put32(b, rate);
    put32(b, rate * 2 * channels);
    put16(b, static_cast<uint16_t>(2 * channels));
    put16(b, 16);
    tag(b, "data");
    put32(b, static_cast<uint32_t>(frames * 2 * channels));
    for (int i = 0; i < frames * channels; ++i) put16(b, static_cast<uint16_t>(i * 7));
    return b;
}

void testWav() {
    std::printf("wav subset\n");
    Sound s;
    auto ok = makeWav(1, 22050, 100, false);
    CHECK(audio::parseWav(ok.data(), ok.size(), s) && s.frames == 100 && s.rate == 22050 && s.samples[3] == 21,
          "frames %u rate %u", s.frames, s.rate);
    auto list = makeWav(1, 44100, 50, true);
    CHECK(audio::parseWav(list.data(), list.size(), s) && s.frames == 50, "LIST chunk must be skipped");
    auto stereo = makeWav(2, 22050, 10, false);
    CHECK(!audio::parseWav(stereo.data(), stereo.size(), s) && !s, "stereo must be rejected");
    auto cut = makeWav(1, 22050, 100, false);
    cut.resize(cut.size() - 10);
    CHECK(!audio::parseWav(cut.data(), cut.size(), s), "truncated data chunk must be rejected");
    const uint8_t junk[16] = {'R', 'I', 'F', 'X'};
    CHECK(!audio::parseWav(junk, sizeof junk, s), "not RIFF");
}

// A mono clip of constant value: makes the arithmetic easy to check.
struct Dc {
    std::vector<int16_t> data;
    Sound sound;
    Dc(int16_t value, uint32_t frames, uint32_t rate) : data(frames, value) {
        sound.samples = data.data();
        sound.frames = frames;
        sound.rate = rate;
    }
};

int16_t g_out[audio::kMaxBlock * 2];

void block(Mixer& m, int frames = 1024) { m.mix(g_out, frames); }
bool allNear(int ch, int from, int to, int value, int tol) {
    for (int i = from; i < to; ++i)
        if (std::abs(g_out[i * 2 + ch] - value) > tol) return false;
    return true;
}

void testMixer() {
    std::printf("mixer\n");
    {
        Mixer m;
        block(m);
        CHECK(allNear(0, 0, 1024, 0, 0) && allNear(1, 0, 1024, 0, 0), "silence without voices");
    }
    {
        Mixer m;
        Dc dc(10000, 4000, 22050);
        m.play(audio::kWind, &dc.sound, 1.0f, 1.0f, 0.0f, true);
        block(m);  // ramps in
        CHECK(g_out[0] == 0 && std::abs(g_out[2046] - 10000) < 40, "loop fades in over a block: %d .. %d", g_out[0],
              g_out[2046]);
        block(m);
        CHECK(allNear(0, 0, 1024, 10000, 2) && allNear(1, 0, 1024, 10000, 2), "steady loop at unity gain");
        m.set(audio::kWind, 0.5f, 1.0f, 1.0f);  // half gain, hard right
        block(m);
        block(m);
        CHECK(allNear(0, 0, 1024, 0, 0) && allNear(1, 0, 1024, 5000, 2), "pan right, half gain: L %d R %d",
              g_out[0], g_out[1]);
        m.stop(audio::kWind);
        block(m);
        block(m);
        CHECK(!m.active(audio::kWind) && allNear(1, 0, 1024, 0, 0), "stop fades out and frees the voice");
    }
    {
        Mixer m;
        Dc dc(8000, 100, 44100);
        m.play(m.nextOneShot(), &dc.sound, 1.0f);
        block(m);
        CHECK(std::abs(g_out[0] - 8000) <= 1, "one-shot starts at full level (keeps its attack): %d", g_out[0]);
        CHECK(allNear(0, 0, 99, 8000, 1) && allNear(0, 100, 1024, 0, 0), "one-shot plays its 100 frames, then silence");
        CHECK(!m.active(audio::kOneShot0), "one-shot frees itself");
        CHECK(m.nextOneShot() == audio::kOneShot1, "one-shot slots go round robin");
    }
    {
        // 22.05 kHz at pitch 2 reads one source frame per output frame.
        Mixer m;
        Dc dc(1000, 1024, 22050);
        m.play(audio::kOneShot0, &dc.sound, 1.0f, 2.0f);
        block(m, 1023);
        CHECK(m.active(audio::kOneShot0), "pitch 2 at half rate: 1023 frames in, still playing");
        block(m, 64);
        CHECK(!m.active(audio::kOneShot0), "pitch 2 at half rate: done after 1024 frames");
    }
    {
        Mixer m;
        Dc dc(6000, 4000, 22050);
        m.play(audio::kWind, &dc.sound, 1.0f, 1.0f, 0.0f, true);
        m.play(audio::kUi, &dc.sound, 1.0f, 1.0f, 0.0f, true);
        block(m);
        block(m);
        m.setWorldGain(0.0f);
        block(m);
        block(m);
        CHECK(allNear(0, 0, 1024, 6000, 2), "pause ducks the world, not the menu: %d", g_out[0]);
    }
    {
        Mixer m;
        Dc loop(10000, 4000, 22050), speech(1, 8000, 22050);  // near-silent "voice": measure the loop
        m.play(audio::kEngineRoar, &loop.sound, 1.0f, 1.0f, 0.0f, true);
        block(m);
        block(m);
        m.play(audio::kVoice, &speech.sound, 1.0f);
        block(m);
        block(m);
        CHECK(allNear(0, 0, 1024, 5501, 3), "loops step back under a voice message: %d", g_out[0]);
        for (int i = 0; i < 18; ++i) block(m);  // 8000 frames at 22.05 kHz: ~16 blocks
        CHECK(allNear(0, 0, 1024, 10000, 3), "and come back after it: %d", g_out[0]);
    }
    {
        // Five loud loops would sum to 5 x 20000: the limiter keeps it in range
        // instead of wrapping around.
        Mixer m;
        Dc dc(20000, 4000, 22050);
        for (int c = 0; c < 5; ++c) m.play(c, &dc.sound, 1.0f, 1.0f, 0.0f, true);
        for (int i = 0; i < 4; ++i) block(m);
        int lo = 32767, hi = -32768;
        for (int i = 0; i < 2048; ++i) lo = std::min<int>(lo, g_out[i]), hi = std::max<int>(hi, g_out[i]);
        CHECK(lo > 25000 && hi <= 32767, "limited, not wrapped: %d..%d", lo, hi);
    }
}

game::FlightState level(float speed, float altitude = 1000.0f) {
    game::FlightState s;
    s.position = {0, altitude, 0};
    s.speed = speed;
    s.pathDir = {0, 0, -1};
    s.throttle = 0.6f;
    return s;
}

// Runs the informer for `seconds` on a fixed state; returns what it said, in order.
std::vector<Message> listen(audio::VoiceInformer& vi, const game::FlightState& s, float seconds) {
    std::vector<Message> said;
    for (float t = 0.0f; t < seconds; t += kDt) {
        Message m = vi.update(s, kDt);
        if (m != Message::None) said.push_back(m);
    }
    return said;
}

audio::VoiceInformer makeInformer() {
    audio::VoiceInformer vi(kSu35S);
    for (int i = 0; i < audio::kMessageCount; ++i) vi.setDuration(static_cast<Message>(i), 1.2f);
    return vi;
}

void testInformer() {
    std::printf("voice informer\n");
    {
        auto vi = makeInformer();
        CHECK(listen(vi, level(250.0f), 10.0f).empty(), "cruise: not a word");
    }
    {
        auto vi = makeInformer();
        game::FlightState s = level(120.0f);
        s.alpha = kSu35S.alphaMax;
        auto said = listen(vi, s, 0.2f);
        CHECK(said.empty(), "the dwell: a limit touched for 0.2 s stays silent");
        said = listen(vi, s, 0.2f);
        CHECK(said.size() == 1 && said[0] == Message::AoaLimit, "AoA limit spoken once held");
        said = listen(vi, s, 3.0f);
        CHECK(said.empty(), "no repeat before message + %.1f s", audio::VoiceInformer::kRepeat);
        said = listen(vi, s, 1.0f);
        CHECK(said.size() == 1, "repeats while the condition holds");
    }
    {
        // G and AoA together: G first (priority), AoA after it finished.
        auto vi = makeInformer();
        game::FlightState s = level(200.0f);
        s.alpha = kSu35S.alphaMax;
        s.loadFactor = kSu35S.maxG + 1.0f;
        auto said = listen(vi, s, 2.5f);
        CHECK(said.size() == 2 && said[0] == Message::GLimit && said[1] == Message::AoaLimit,
              "priority order: %zu messages", said.size());
    }
    {
        // Terrain cuts a lower message short.
        auto vi = makeInformer();
        game::FlightState s = level(200.0f);
        s.speed = 400.0f;
        auto said = listen(vi, s, 0.5f);
        CHECK(said.size() == 1 && said[0] == Message::SpeedLimit, "speed limit above 1400 km/h");
        s.position.y = 300.0f;
        s.pathDir = math::normalize({0.0f, -0.6f, -1.0f});
        said = listen(vi, s, 0.4f);
        CHECK(!said.empty() && said[0] == Message::AltitudeDanger, "'высота опасная' interrupts");
    }
    {
        auto vi = makeInformer();
        game::FlightState s = level(120.0f);
        s.alpha = 1.2f;
        s.crashed = true;
        CHECK(listen(vi, s, 3.0f).empty(), "silent after the crash");
    }
    {
        // Real flying: a full pull at low speed reaches the AoA limiter and the
        // informer says so; a 9 g pull at speed stays inside the envelope.
        game::FlightModel fm(kSu35S);
        fm.reset({0, 3000, 0}, math::Quat{}, 110.0f);
        auto vi = makeInformer();
        game::ControlInput in;
        in.pitch = 1.0f;
        bool aoa = false;
        for (int i = 0; i < 180; ++i) {
            fm.step(in, kDt);
            if (vi.update(fm.state(), kDt) == Message::AoaLimit) aoa = true;
        }
        CHECK(aoa, "full aft stick at 400 km/h -> 'предельный угол атаки' (alpha %.2f)", fm.state().alpha);
    }
}

void testFlightAudio() {
    std::printf("flight audio\n");
    audio::FlightAudio fa;
    fx::EngineFx engine;
    game::FlightState s = level(250.0f);
    s.throttle = 0.3f;
    for (int i = 0; i < 60; ++i) fa.update(s, engine, 0.0f, kDt);
    const float idleRoar = fa.mix().roar.gain, idleWhine = fa.mix().whine.pitch;
    CHECK(fa.mix().afterburner.gain < 0.01f, "dry: no afterburner roar");
    s.throttle = 1.0f;
    fa.update(s, engine, 0.0f, kDt);
    CHECK(fa.rpm() < 0.9f, "the engine spools up, it does not jump: rpm %.2f", fa.rpm());
    engine.afterburner = 1.0f;
    for (int i = 0; i < 300; ++i) fa.update(s, engine, 0.0f, kDt);
    CHECK(fa.mix().roar.gain > idleRoar + 0.2f && fa.mix().whine.pitch > idleWhine + 0.1f,
          "full power is louder and higher: roar %.2f whine pitch %.2f", fa.mix().roar.gain, fa.mix().whine.pitch);
    CHECK(fa.mix().afterburner.gain > 0.8f, "afterburner roar %.2f", fa.mix().afterburner.gain);

    float lastWind = -1.0f;
    bool rising = true;
    for (float v = 50.0f; v <= 450.0f; v += 50.0f) {
        audio::FlightAudio w;
        game::FlightState sv = level(v);
        for (int i = 0; i < 120; ++i) w.update(sv, engine, 0.0f, kDt);
        rising = rising && w.mix().wind.gain > lastWind;
        lastWind = w.mix().wind.gain;
    }
    CHECK(rising, "wind grows with airspeed");

    game::FlightState slow = level(120.0f);
    slow.alpha = 0.6f;
    audio::FlightAudio b;
    for (int i = 0; i < 120; ++i) b.update(slow, engine, 0.0f, kDt);
    CHECK(b.mix().buffet.gain > 0.5f, "high AoA buffets: %.2f", b.mix().buffet.gain);

    s.crashed = true;
    for (int i = 0; i < 60; ++i) fa.update(s, engine, 0.0f, kDt);
    CHECK(fa.mix().roar.gain < 0.01f && fa.mix().wind.gain < 0.01f, "crashed: the engine falls silent");
}

}  // namespace

int main() {
    testWav();
    testMixer();
    testInformer();
    testFlightAudio();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all audio checks passed\n");
    return 0;
}
