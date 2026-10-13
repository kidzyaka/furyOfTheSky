#pragma once

#include "platform/psp_input.h"
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
    const char* error = nullptr;  // path of the first file that failed

    bool load();
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
    virtual void update(const platform::Pad& pad, float dt) = 0;
    virtual void render() = 0;
    // Debug text over the finished frame (runs after the GE is done).
    virtual void overlay() {}
};

}  // namespace scenes
