#pragma once

#include "audio/sound.h"
#include "audio/voice_informer.h"
#include "core/file.h"
#include "platform/psp_input.h"
#include "render/fx_draw.h"
#include "render/mesh.h"
#include "render/texture.h"

namespace scenes {

constexpr int kLiveryCount = 2;
constexpr const char* kLiveryNames[kLiveryCount] = {"VKS grey", "901 digital"};

// Everything loaded once at boot and shared by scenes.
struct Assets {
    gfx::Mesh su35s;
    gfx::Texture liveries[kLiveryCount];
    gfx::Texture ocean, cloud, sun;
    // Effects and HUD (tools/textures/fx.py).
    gfx::Texture glow, smoke, flame, ring, glint, shadow, line, ribbon, waves;
    // Sounds (tools/audio): loops, one-shots and the voice warning messages.
    audio::Sound roar, whine, afterburner, wind, buffet;
    audio::Sound abLight, flare, explosion, uiMove, uiSelect;
    audio::Sound voice[audio::kMessageCount];
    const char* error = nullptr;  // path of the first file that failed

    bool load();
    gfx::FxTextures fx() const { return {&glow, &smoke, &flame, &ring, &ribbon, &shadow}; }

private:
    bool loadSound(audio::Sound& sound, const char* name);
    static constexpr int kMaxSounds = 24;
    core::Blob soundData_[kMaxSounds];  // the samples live here for the whole session
    int soundCount_ = 0;
    char soundError_[64] = {};
};

// Player options edited in the pause menu.
struct Settings {
    int livery = 0;
    bool invertPitch = false;  // false: stick back = nose up (flight-sim convention)
};

// A screen of the game. Virtual dispatch is fine without RTTI.
class Scene {
public:
    virtual ~Scene() = default;
    virtual const char* name() const = 0;
    virtual void enter() {}
    // Called before switching to another scene: silence what this one started.
    virtual void leave() {}
    virtual void update(const platform::Pad& pad, float dt) = 0;
    // 3D scene and its HUD, into the frame's display list.
    virtual void render() = 0;
    // Runs on the finished frame after the GE is done: debug text, reading
    // pixels back (sun occlusion for the lens flare).
    virtual void overlay(void* drawBuffer) { (void)drawBuffer; }
};

}  // namespace scenes
