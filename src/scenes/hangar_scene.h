#pragma once

#include "game/control_surfaces.h"
#include "scenes/scene.h"

namespace scenes {

// Model viewer: orbit camera around the aircraft, livery switching and a demo
// cycle of the moving parts (control surfaces, thrust-vectoring nozzles).
class HangarScene : public Scene {
public:
    HangarScene(Assets& assets, Settings& settings) : assets_(assets), settings_(settings) {}

    const char* name() const override { return "Hangar"; }
    void enter() override { surfaces_.reset(); }
    void update(const platform::Pad& pad, float dt) override;
    void render() override;

private:
    Assets& assets_;
    Settings& settings_;
    game::ControlSurfaces surfaces_;
    float yaw_ = 2.4f, pitch_ = 0.32f, distance_ = 26.0f;
    float demoTime_ = 0.0f;
    bool autoSpin_ = true;
    bool demo_ = true;
};

}  // namespace scenes
