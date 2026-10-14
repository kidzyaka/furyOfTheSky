#include "scenes/scene.h"

#include <cstdio>

namespace scenes {

bool Assets::load() {
    struct TextureFile {
        gfx::Texture* texture;
        const char* path;
    };
    const TextureFile textures[] = {
        {&liveries[0], "assets/models/su35s_vks.ftx"},
        {&liveries[1], "assets/models/su35s_901.ftx"},
        {&ocean, "assets/textures/ocean.ftx"},
        {&cloud, "assets/textures/cloud.ftx"},
        {&sun, "assets/textures/sun.ftx"},
        {&glow, "assets/textures/fx_glow.ftx"},
        {&smoke, "assets/textures/fx_smoke.ftx"},
        {&flame, "assets/textures/fx_flame.ftx"},
        {&ring, "assets/textures/fx_ring.ftx"},
        {&glint, "assets/textures/fx_glint.ftx"},
        {&shadow, "assets/textures/fx_shadow.ftx"},
        {&line, "assets/textures/fx_line.ftx"},
        {&ribbon, "assets/textures/fx_ribbon.ftx"},
        {&waves, "assets/textures/fx_waves.ftx"},
    };
    if (!su35s.load("assets/models/su35s.fsm")) {
        error = "assets/models/su35s.fsm";
        return false;
    }
    for (const auto& t : textures) {
        if (!t.texture->load(t.path)) {
            error = t.path;
            return false;
        }
    }
    struct SoundFile {
        audio::Sound* sound;
        const char* name;
    };
    const SoundFile sounds[] = {
        {&roar, "engine_roar"}, {&whine, "engine_whine"}, {&afterburner, "afterburner"},
        {&wind, "wind"},        {&buffet, "buffet"},      {&abLight, "ab_light"},
        {&flare, "flare"},      {&explosion, "explosion"}, {&uiMove, "ui_move"},
        {&uiSelect, "ui_select"},
    };
    for (const auto& f : sounds)
        if (!loadSound(*f.sound, f.name)) return false;
    for (int i = 0; i < audio::kMessageCount; ++i)
        if (!loadSound(voice[i], audio::messageFile(static_cast<audio::Message>(i)))) return false;
    return true;
}

bool Assets::loadSound(audio::Sound& sound, const char* name) {
    std::snprintf(soundError_, sizeof soundError_, "assets/sfx/%s.wav", name);
    if (soundCount_ < kMaxSounds) {
        core::Blob& blob = soundData_[soundCount_];
        blob = core::loadFile(soundError_);
        if (blob && audio::parseWav(blob.data, blob.size, sound)) {
            ++soundCount_;
            return true;
        }
    }
    error = soundError_;
    return false;
}

}  // namespace scenes
