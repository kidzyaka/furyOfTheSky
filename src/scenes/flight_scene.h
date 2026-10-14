#pragma once

#include "audio/flight_audio.h"
#include "audio/voice_informer.h"
#include "fx/aircraft_fx.h"
#include "game/chase_camera.h"
#include "game/control_surfaces.h"
#include "game/countermeasures.h"
#include "game/flight_model.h"
#include "render/clouds.h"
#include "render/ocean.h"
#include "render/sky.h"
#include "scenes/scene.h"

namespace scenes {

// Test range: free flight over an endless sea under a cumulus field, with the
// full effects set and the HUD.
class FlightScene : public Scene {
public:
    FlightScene(Assets& assets, Settings& settings);

    const char* name() const override { return "Flight test"; }
    void enter() override;
    void leave() override;
    void update(const platform::Pad& pad, float dt) override;
    void render() override;
    void overlay(void* drawBuffer) override;

    void restart();
    // Scripted sortie instead of the pad (emulator smoke test, docs/psp/emulator.md).
    void setAutopilot(bool on) { autopilot_ = on; }
    bool takeShotRequest() {
        bool r = shotRequest_;
        shotRequest_ = false;
        return r;
    }
    bool scriptDone() const { return autopilot_ && simStep_ >= kScriptEnd; }

private:
    static constexpr float kStep = 1.0f / 60.0f;  // fixed simulation step
    static constexpr int kMaxStepsPerFrame = 5;    // spiral-of-death guard
    static constexpr int kScriptEnd = 1080;

    game::ControlInput mapInput(const platform::Pad& pad) const;
    game::ControlInput scriptedInput(int step);
    void stepSimulation(const game::ControlInput& in);
    void restartAt(const math::Vec3& pos, float speed);
    gfx::MeshPose pose() const;
    void startLoops();
    void updateAudio();
    void playOneShot(const audio::Sound& sound, float gain, float pan = 0.0f);

    Assets& assets_;
    Settings& settings_;
    game::FlightModel model_;
    game::ChaseCamera camera_;
    game::ControlSurfaces surfaces_;
    game::Countermeasures flares_;
    fx::AircraftEffects effects_;
    audio::FlightAudio audio_;
    audio::VoiceInformer informer_;
    gfx::Sky sky_;
    gfx::Ocean ocean_;
    gfx::Clouds clouds_;

    game::ControlInput lastInput_;
    float accumulator_ = 0.0f;
    float crashTimer_ = 0.0f;
    float time_ = 0.0f;
    bool prevAfterburner_ = false;
    // Lens flare: where the sun was drawn, and how much of it the last frame showed.
    float sunX_ = 0.0f, sunY_ = 0.0f;
    bool sunOnScreen_ = false;
    float sunVisibility_ = 0.0f;

    bool autopilot_ = false;
    bool shotRequest_ = false;
    int eventShotAt_ = -1;  // script: a shot after Mach 1 / after the crash
    int simStep_ = 0;
};

}  // namespace scenes
