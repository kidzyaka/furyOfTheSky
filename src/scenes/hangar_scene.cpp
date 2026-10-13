#include "scenes/hangar_scene.h"

#include <pspctrl.h>
#include <pspdebug.h>

#include <algorithm>
#include <cmath>

#include "game/world.h"
#include "render/gu_context.h"
#include "render/view.h"

namespace scenes {

void HangarScene::update(const platform::Pad& pad, float dt) {
    if (pad.hit(PSP_CTRL_TRIANGLE)) settings_.livery = (settings_.livery + 1) % kLiveryCount;
    if (pad.hit(PSP_CTRL_CROSS)) autoSpin_ = !autoSpin_;
    if (pad.lx != 0.0f || pad.ly != 0.0f) autoSpin_ = false;
    yaw_ += (pad.lx * 2.2f + (autoSpin_ ? 0.35f : 0.0f)) * dt;
    pitch_ = math::clamp(pitch_ + pad.ly * 1.6f * dt, -1.35f, 1.35f);
    if (pad.down(PSP_CTRL_LTRIGGER)) distance_ = std::max(14.0f, distance_ - 18.0f * dt);
    if (pad.down(PSP_CTRL_RTRIGGER)) distance_ = std::min(90.0f, distance_ + 18.0f * dt);
}

void HangarScene::render() {
    gfx::drawBackgroundGradient(gfx::abgr(46, 90, 138), gfx::abgr(168, 200, 224));
    gfx::setProjection(55.0f, 1.0f, 400.0f);
    math::Vec3 eye{distance_ * std::cos(pitch_) * std::sin(yaw_), distance_ * std::sin(pitch_),
                   distance_ * std::cos(pitch_) * std::cos(yaw_)};
    gfx::setView(eye, {0, 0, 0}, {0, 1, 0});
    gfx::enableSunLight(game::world::sunDirection());
    assets_.liveries[settings_.livery].bind();
    gfx::setModelIdentity();
    assets_.su35s.draw();
    gfx::disableLighting();
}

void HangarScene::overlay() {
    pspDebugScreenSetTextColor(0xFFFFFFFF);
    pspDebugScreenSetXY(0, 0);
    pspDebugScreenPrintf("HANGAR  Su-35S  %u tris  livery: %s", static_cast<unsigned>(assets_.su35s.triangleCount()),
                         kLiveryNames[settings_.livery]);
    pspDebugScreenSetXY(0, 33);
    pspDebugScreenPrintf("stick: orbit  L/R: zoom  /\\: livery  X: spin  START: menu");
}

}  // namespace scenes
