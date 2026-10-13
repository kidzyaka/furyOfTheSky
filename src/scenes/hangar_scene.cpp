#include "scenes/hangar_scene.h"

#include <pspctrl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/world.h"
#include "render/gu_context.h"
#include "render/hud_draw.h"
#include "render/view.h"
#include "scenes/flight_hud.h"

namespace scenes {

namespace {

// Demo of the moving parts: a 9 s loop of pitch, roll, yaw, airbrake and
// thrust vectoring, fed through the same animator the flight uses.
game::ControlInput demoInput(float t, game::FlightState& s) {
    game::ControlInput in;
    float phase = std::fmod(t, 9.0f);
    s.speed = 60.0f;  // slow: nozzles and slats work hardest
    s.alpha = 0.0f;
    s.airbrake = false;
    if (phase < 2.0f) in.pitch = std::sin(phase * math::kPi);
    else if (phase < 4.0f) in.roll = std::sin((phase - 2.0f) * math::kPi);
    else if (phase < 6.0f) in.yaw = std::sin((phase - 4.0f) * math::kPi);
    else if (phase < 7.5f) s.airbrake = true;
    else s.alpha = 0.5f;  // high AoA: leading-edge flaps droop
    return in;
}

}  // namespace

void HangarScene::update(const platform::Pad& pad, float dt) {
    if (pad.hit(PSP_CTRL_TRIANGLE)) settings_.livery = (settings_.livery + 1) % kLiveryCount;
    if (pad.hit(PSP_CTRL_CROSS)) autoSpin_ = !autoSpin_;
    if (pad.hit(PSP_CTRL_SQUARE)) demo_ = !demo_;
    if (pad.lx != 0.0f || pad.ly != 0.0f) autoSpin_ = false;
    yaw_ += (pad.lx * 2.2f + (autoSpin_ ? 0.35f : 0.0f)) * dt;
    pitch_ = math::clamp(pitch_ + pad.ly * 1.6f * dt, -1.35f, 1.35f);
    if (pad.down(PSP_CTRL_LTRIGGER)) distance_ = std::max(14.0f, distance_ - 18.0f * dt);
    if (pad.down(PSP_CTRL_RTRIGGER)) distance_ = std::min(90.0f, distance_ + 18.0f * dt);

    game::FlightState s;
    game::ControlInput in;
    if (demo_) {
        demoTime_ += dt;
        in = demoInput(demoTime_, s);
    }
    surfaces_.update(s, in, dt);
}

void HangarScene::render() {
    gfx::drawBackgroundGradient(gfx::abgr(46, 90, 138), gfx::abgr(168, 200, 224));
    gfx::setProjection(55.0f, 1.0f, 400.0f);
    math::Vec3 eye{distance_ * std::cos(pitch_) * std::sin(yaw_), distance_ * std::sin(pitch_),
                   distance_ * std::cos(pitch_) * std::cos(yaw_)};
    gfx::setCamera(eye, {0, 0, 0}, {0, 1, 0});
    gfx::enableSunLight(game::world::sunDirection());
    assets_.liveries[settings_.livery].bind();

    using game::Surface;
    gfx::MeshPose pose;
    const Surface surfaces[] = {Surface::Stabilator, Surface::Flaperon, Surface::Slat, Surface::Rudder,
                                Surface::Nozzle};
    const gfx::MeshChannel channels[] = {gfx::kChannelStabilator, gfx::kChannelFlaperon, gfx::kChannelSlat,
                                         gfx::kChannelRudder, gfx::kChannelNozzle};
    for (int i = 0; i < 5; ++i)
        for (int side = 0; side < 2; ++side) pose.angle[channels[i]][side] = surfaces_.angle(surfaces[i], side);
    assets_.su35s.draw({0, 0, 0}, math::Quat{}, &pose);
    gfx::disableLighting();

    gfx::HudCanvas& c = gfx::hudCanvas();
    c.reset();
    c.color(hud::kGreen);
    c.width(hud::kTextWidth);
    char buf[64];
    std::snprintf(buf, sizeof buf, "HANGAR  SU-35S  %u TRIS  %d PARTS", static_cast<unsigned>(assets_.su35s.triangleCount()),
                  assets_.su35s.partCount());
    c.text(12.0f, 10.0f, buf, 8.0f);
    std::snprintf(buf, sizeof buf, "LIVERY %s", kLiveryNames[settings_.livery]);
    c.text(12.0f, 24.0f, buf, 8.0f);
    c.color(hud::kDim);
    c.text(12.0f, 252.0f, "STICK ORBIT  L/R ZOOM  ^ LIVERY  X SPIN  [] PARTS DEMO", 6.5f);
    c.flush(assets_.line);
}

}  // namespace scenes
