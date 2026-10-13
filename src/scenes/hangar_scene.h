#pragma once

#include "scenes/scene.h"

namespace scenes {

// M0 model viewer: orbit camera around the aircraft, livery switching.
class HangarScene : public Scene {
public:
    HangarScene(Assets& assets, Settings& settings) : assets_(assets), settings_(settings) {}

    const char* name() const override { return "Hangar"; }
    void update(const platform::Pad& pad, float dt) override;
    void render() override;
    void overlay() override;

private:
    Assets& assets_;
    Settings& settings_;
    float yaw_ = 2.4f, pitch_ = 0.32f, distance_ = 26.0f;
    bool autoSpin_ = true;
};

}  // namespace scenes
