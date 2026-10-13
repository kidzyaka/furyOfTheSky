#pragma once

#include "game/chase_camera.h"
#include "game/flight_model.h"
#include "render/clouds.h"
#include "render/ocean.h"
#include "render/sky.h"
#include "scenes/scene.h"

namespace scenes {

// M1 test range: free flight over an endless sea under a cumulus field.
class FlightScene : public Scene {
public:
    FlightScene(Assets& assets, Settings& settings);

    const char* name() const override { return "Flight test"; }
    void enter() override { restart(); }
    void update(const platform::Pad& pad, float dt) override;
    void render() override;
    void overlay() override;

    void restart();
    // Scripted manoeuvres instead of the pad (emulator smoke test).
    void setAutopilot(bool on) { autopilot_ = on; }

private:
    static constexpr float kStep = 1.0f / 60.0f;  // fixed simulation step
    static constexpr int kMaxStepsPerFrame = 5;    // spiral-of-death guard

    game::ControlInput mapInput(const platform::Pad& pad) const;
    game::ControlInput scriptedInput(int step) const;

    Assets& assets_;
    Settings& settings_;
    game::FlightModel model_;
    game::ChaseCamera camera_;
    gfx::Sky sky_;
    gfx::Ocean ocean_;
    gfx::Clouds clouds_;

    float accumulator_ = 0.0f;
    float crashTimer_ = 0.0f;
    bool autopilot_ = false;
    int simStep_ = 0;
};

}  // namespace scenes
