// Offline audio preview (docs/workflow.md): flies a scripted sortie through the
// real flight model, effects director, sound director, voice informer and
// mixer - the same code the PSP runs - and writes what the player would hear
// to build/audio_preview.wav. Listen on the Mac before deploying.
//
//   cmake --build build/host && build/host/audio_preview
//
// Mirrors the audio part of scenes::FlightScene::stepSimulation; keep in sync.

#include <cstdio>
#include <cstring>
#include <vector>

#include "audio/flight_audio.h"
#include "audio/mixer.h"
#include "audio/sound.h"
#include "audio/voice_informer.h"
#include "fx/aircraft_fx.h"
#include "game/countermeasures.h"
#include "game/flight_model.h"

namespace {

std::vector<std::vector<uint8_t>> g_files;

bool load(audio::Sound& s, const char* name) {
    char path[256];
    std::snprintf(path, sizeof path, "assets/sfx/%s.wav", name);
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s (run from the repo root)\n", path);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    g_files.emplace_back(static_cast<size_t>(size));
    size_t got = std::fread(g_files.back().data(), 1, static_cast<size_t>(size), f);
    std::fclose(f);
    if (got != static_cast<size_t>(size) || !audio::parseWav(g_files.back().data(), got, s)) {
        std::fprintf(stderr, "bad wav %s\n", path);
        return false;
    }
    return true;
}

void writeWav(const char* path, const std::vector<int16_t>& stereo) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    const uint32_t bytes = static_cast<uint32_t>(stereo.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(audio::kOutputRate);
    u32(audio::kOutputRate * 4);
    u16(4);
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(bytes);
    std::fwrite(stereo.data(), 2, stereo.size(), f);
    std::fclose(f);
}

}  // namespace

int main() {
    audio::Sound roar, whine, ab, wind, buffet, abLight, flare, explosion;
    audio::Sound voice[audio::kMessageCount];
    bool ok = load(roar, "engine_roar") && load(whine, "engine_whine") && load(ab, "afterburner") &&
              load(wind, "wind") && load(buffet, "buffet") && load(abLight, "ab_light") && load(flare, "flare") &&
              load(explosion, "explosion");
    for (int i = 0; ok && i < audio::kMessageCount; ++i)
        ok = load(voice[i], audio::messageFile(static_cast<audio::Message>(i)));
    if (!ok) return 1;

    constexpr float kStep = 1.0f / 60.0f;
    game::FlightModel fm(game::kSu35S);
    game::Countermeasures cm;
    fx::AircraftEffects fx;
    audio::FlightAudio director;
    audio::VoiceInformer informer(game::kSu35S);
    for (int i = 0; i < audio::kMessageCount; ++i)
        informer.setDuration(static_cast<audio::Message>(i), voice[i].seconds());
    audio::Mixer& mixer = audio::mixer();

    auto restartAt = [&](float altitude, float speed) {
        fm.reset({0, altitude, 0}, math::Quat{}, speed);
        fx.reset();
        director.reset();
        informer.reset();
    };
    restartAt(900.0f, 250.0f);
    cm.reset();
    mixer.play(audio::kEngineRoar, &roar, 0, 1, 0, true);
    mixer.play(audio::kEngineWhine, &whine, 0, 1, 0, true);
    mixer.play(audio::kAfterburner, &ab, 0, 1, 0, true);
    mixer.play(audio::kWind, &wind, 0, 1, 0, true);
    mixer.play(audio::kBuffet, &buffet, 0, 1, 0, true);

    // The sortie, in seconds: what each stretch should let you hear.
    struct Leg {
        float until;
        const char* what;
    };
    const Leg legs[] = {{3, "cruise 900 km/h"},
                        {9, "afterburner light-up and run"},
                        {13, "13 g pull with the limiter off: buffet, G-limit voice"},
                        {16, "flare salvos"},
                        {17, "throttle back"},
                        {21, "slow, full aft stick: AoA limit voice"},
                        {25, "cobra (PSM)"},
                        {30, "dive at the sea: altitude danger, impact"},
                        {34, "after the crash"}};
    std::vector<int16_t> out;
    int16_t block[audio::kMaxBlock * 2];
    double audioTime = 0.0;
    bool wasCrashed = false, prevAb = false;
    int leg = -1;
    const int steps = static_cast<int>(34.0f / kStep);
    for (int step = 0; step < steps; ++step) {
        const float t = step * kStep;
        int l = 0;
        while (legs[l].until <= t) ++l;
        if (l != leg) {
            leg = l;
            std::printf("%5.1f s  %s\n", t, legs[l].what);
            if (l == 5) restartAt(2500.0f, 120.0f);
            if (l == 6) restartAt(2500.0f, 120.0f);
            if (l == 7) restartAt(600.0f, 260.0f);
        }
        game::ControlInput in;
        switch (l) {
            case 1: in.throttleUp = true; break;
            case 2: in.throttleUp = true, in.pitch = 1.0f, in.psm = true; break;
            case 3: in.flares = true; break;
            case 4: in.throttleDown = true; break;
            case 5: in.pitch = 1.0f; break;
            case 6: in.pitch = 1.0f, in.psm = true; break;
            case 7: in.pitch = -0.7f, in.throttleUp = true; break;
            default: break;
        }

        fm.step(in, kStep);
        const game::FlightState& s = fm.state();
        cm.update(s, in.flares, kStep);
        fx.update(s, cm, s.position + math::Vec3{0, 4, 20}, math::kForward, kStep);
        if (s.afterburner && !prevAb) mixer.play(mixer.nextOneShot(), &abLight, 0.9f);
        prevAb = s.afterburner;
        director.update(s, fx.engine(), fx.transonic(), kStep);
        const int released = cm.justReleased();
        if (released >= 0) {
            float side = math::dot(cm.flare(released).pos - s.position, s.orientation.right());
            mixer.play(mixer.nextOneShot(), &flare, 0.7f, 1.0f, side > 0 ? 0.45f : -0.45f);
        }
        if (s.crashed && !wasCrashed) {
            mixer.play(mixer.nextOneShot(), &explosion, 1.0f);
            mixer.stop(audio::kVoice);
        }
        wasCrashed = s.crashed;
        const audio::Message say = informer.update(s, kStep);
        if (say != audio::Message::None) {
            std::printf("%5.1f s    voice: %s\n", t, audio::messageFile(say));
            mixer.play(audio::kVoice, &voice[static_cast<int>(say)], 1.0f);
        }
        const audio::FlightSoundMix& x = director.mix();
        mixer.set(audio::kEngineRoar, x.roar.gain, x.roar.pitch);
        mixer.set(audio::kEngineWhine, x.whine.gain, x.whine.pitch);
        mixer.set(audio::kAfterburner, x.afterburner.gain, x.afterburner.pitch);
        mixer.set(audio::kWind, x.wind.gain, x.wind.pitch);
        mixer.set(audio::kBuffet, x.buffet.gain, x.buffet.pitch);

        // The PSP's audio thread pulls 1024-frame blocks at its own pace.
        while (audioTime < t + kStep) {
            mixer.mix(block, audio::kMaxBlock);
            out.insert(out.end(), block, block + audio::kMaxBlock * 2);
            audioTime += static_cast<double>(audio::kMaxBlock) / audio::kOutputRate;
        }
    }
    writeWav("build/audio_preview.wav", out);
    std::printf("wrote build/audio_preview.wav (%.1f s)\n", out.size() / 2.0 / audio::kOutputRate);
    return 0;
}
