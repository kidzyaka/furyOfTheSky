#include "scenes/flight_scene.h"

#include <pspctrl.h>
#include <pspdebug.h>

#include <algorithm>
#include <cmath>

#include "game/world.h"
#include "render/view.h"

namespace scenes {

namespace world = game::world;

FlightScene::FlightScene(Assets& assets, Settings& settings)
    : assets_(assets), settings_(settings), model_(game::kSu35S) {
    sky_.init(world::sunDirection());
    ocean_.init();
    clouds_.init(0x35C0FFEE);
}

void FlightScene::restart() {
    model_.reset({0.0f, world::kStartAltitude, 0.0f}, math::Quat{}, world::kStartSpeed);
    camera_.reset(model_.state());
    accumulator_ = 0.0f;
    crashTimer_ = 0.0f;
    simStep_ = 0;
}

game::ControlInput FlightScene::mapInput(const platform::Pad& pad) const {
    game::ControlInput in;
    // Pad y is +1 with the stick pulled back: that is "nose up" unless inverted.
    in.pitch = settings_.invertPitch ? -pad.ly : pad.ly;
    in.roll = pad.lx;
    in.yaw = (pad.down(PSP_CTRL_RTRIGGER) ? 1.0f : 0.0f) - (pad.down(PSP_CTRL_LTRIGGER) ? 1.0f : 0.0f);
    in.throttleUp = pad.down(PSP_CTRL_TRIANGLE);
    in.throttleDown = pad.down(PSP_CTRL_CROSS);
    return in;
}

game::ControlInput FlightScene::scriptedInput(int step) const {
    // Level cruise, bank right into a hard boosted turn, roll out, boosted climb.
    game::ControlInput in;
    if (step >= 100 && step < 118) in.roll = 1.0f;
    if (step >= 118 && step < 230) { in.pitch = 1.0f; in.throttleUp = true; }
    if (step >= 230 && step < 250) in.roll = -1.0f;
    if (step >= 250 && step < 340) { in.pitch = 0.8f; in.throttleUp = true; }
    return in;
}

void FlightScene::update(const platform::Pad& pad, float dt) {
    if (model_.state().crashed) {
        crashTimer_ += dt;
        if (crashTimer_ > 2.5f) restart();
        return;
    }
    // Fixed-step simulation: identical results at any frame rate (and on any peer).
    accumulator_ += std::min(dt, 0.1f);
    for (int i = 0; accumulator_ >= kStep && i < kMaxStepsPerFrame; ++i) {
        game::ControlInput in = autopilot_ ? scriptedInput(simStep_) : mapInput(pad);
        model_.step(in, kStep);
        camera_.update(model_.state(), kStep);
        accumulator_ -= kStep;
        ++simStep_;
    }
    if (accumulator_ > kStep) accumulator_ = 0.0f;  // fell behind: drop time, don't spiral
}

void FlightScene::render() {
    const game::CameraPose& cam = camera_.pose();
    const game::FlightState& s = model_.state();

    gfx::setProjection(cam.fovDeg, 1.0f, 12000.0f);
    gfx::setCamera(cam.eye, cam.target, cam.up);
    math::Vec3 fwd = math::normalize(cam.target - cam.eye);
    math::Vec3 right = math::normalize(math::cross(fwd, cam.up));
    math::Vec3 up = math::cross(right, fwd);

    sky_.draw(cam.eye, right, up, assets_.sun);

    const uint32_t haze = gfx::abgr(world::kHazeRGB[0], world::kHazeRGB[1], world::kHazeRGB[2]);
    gfx::enableFog(world::kFogNear, world::kFogFar, haze);
    ocean_.draw(cam.eye, assets_.ocean);

    gfx::enableSunLight(world::sunDirection());
    assets_.liveries[settings_.livery].bind();
    gfx::setModel(s.position, s.orientation);
    assets_.su35s.draw();
    gfx::disableLighting();

    clouds_.draw(cam, assets_.cloud);
    gfx::disableFog();
}

void FlightScene::overlay() {
    const game::FlightState& s = model_.state();
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenSetXY(0, 0);
    pspDebugScreenPrintf("SPD %5.0f km/h  ALT %5.0f m  THR %3.0f%%  G %4.1f", s.speed * 3.6f, s.position.y,
                         s.throttle * 100.0f, s.loadFactor);
    pspDebugScreenSetXY(0, 1);
    if (s.crashed) {
        pspDebugScreenSetTextColor(0xFF3030FF);
        pspDebugScreenPrintf("CRASHED - restarting");
    } else {
        pspDebugScreenSetTextColor(0xFF40C0FF);
        pspDebugScreenPrintf("%s%s%s", s.afterburner ? "AFTERBURNER  " : "", s.airbrake ? "AIRBRAKE  " : "",
                             s.stalled ? "STALL" : "");
    }
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenSetXY(0, 33);
    pspDebugScreenPrintf("stick: pitch/roll  L/R: yaw  /\\: boost  X: brake  START: menu");
}

}  // namespace scenes
