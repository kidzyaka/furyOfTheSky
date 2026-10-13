#include "scenes/flight_scene.h"

#include <pspctrl.h>

#include <algorithm>
#include <cmath>

#include "game/world.h"
#include "render/fx_draw.h"
#include "render/gu_context.h"
#include "render/hud_draw.h"
#include "render/screenshot.h"
#include "render/view.h"
#include "scenes/flight_hud.h"

namespace scenes {

namespace world = game::world;

FlightScene::FlightScene(Assets& assets, Settings& settings)
    : assets_(assets), settings_(settings), model_(game::kSu35S) {
    sky_.init(world::sunDirection());
    ocean_.init();
    clouds_.init(0x35C0FFEE, world::sunDirection());
}

void FlightScene::restart() {
    restartAt({0.0f, world::kStartAltitude, 0.0f}, world::kStartSpeed);
    flares_.reset();
    surfaces_.reset();
    simStep_ = 0;
    time_ = 0.0f;
    eventShotAt_ = -1;
}

void FlightScene::restartAt(const math::Vec3& pos, float speed) {
    model_.reset(pos, math::Quat{}, speed);
    camera_.reset(model_.state());
    effects_.reset();
    accumulator_ = 0.0f;
    crashTimer_ = 0.0f;
    prevAfterburner_ = false;
    lastInput_ = game::ControlInput{};
}

game::ControlInput FlightScene::mapInput(const platform::Pad& pad) const {
    game::ControlInput in;
    // Pad y is +1 with the stick pulled back: that is "nose up" unless inverted.
    in.pitch = settings_.invertPitch ? -pad.ly : pad.ly;
    in.roll = pad.lx;
    in.yaw = (pad.down(PSP_CTRL_RTRIGGER) ? 1.0f : 0.0f) - (pad.down(PSP_CTRL_LTRIGGER) ? 1.0f : 0.0f);
    in.throttleUp = pad.down(PSP_CTRL_TRIANGLE);
    in.throttleDown = pad.down(PSP_CTRL_CROSS);
    in.psm = pad.down(PSP_CTRL_CIRCLE);
    in.flares = pad.down(PSP_CTRL_SQUARE);
    return in;
}

game::ControlInput FlightScene::scriptedInput(int step) {
    // A sortie that shows every effect for the smoke-test screenshots.
    game::ControlInput in;
    const math::Vec3 here = model_.state().position;
    if (step == 50 || step == 160 || step == 238 || step == 345 || step == 445 || step == 940 ||
        step == eventShotAt_)
        shotRequest_ = true;
    // 1. Cruise, then a hard afterburner turn: vapor, wingtip trails, flame.
    if (step >= 60 && step < 78) in.roll = 1.0f;
    if (step >= 78 && step < 200) {
        in.pitch = 1.0f;
        in.throttleUp = true;
    }
    // 2. Flares, in level flight so the fan is easy to judge.
    if (step == 210) restartAt({here.x, 900.0f, here.z}, 230.0f);
    if (step >= 220 && step < 262) in.flares = true;
    // 3. Cobra from 430 km/h.
    if (step == 275) restartAt({here.x, 1500.0f, here.z}, 120.0f);
    if (step >= 290 && step < 370) {
        in.psm = true;
        in.pitch = 1.0f;
    }
    // 4. Through the sound barrier: transonic cloud at 445, clean supersonic
    //    flight 2.5 s after Mach 1.
    if (step == 430) restartAt({here.x, 1500.0f, here.z}, 333.0f);
    if (step >= 430 && step < 880) in.throttleUp = true;
    // 5. Low pass over the waves: spray and shadow. 6. Into the sea.
    if (step == 880) restartAt({here.x, 14.0f, here.z}, 240.0f);
    if (step >= 970) in.pitch = -0.6f;
    return in;
}

void FlightScene::stepSimulation(const game::ControlInput& in) {
    lastInput_ = in;
    const bool wasCrashed = model_.state().crashed;
    model_.step(in, kStep);
    const game::FlightState& s = model_.state();
    surfaces_.update(s, in, kStep);
    flares_.update(s, in.flares, kStep);
    camera_.update(s, kStep);
    const game::CameraPose& cam = camera_.pose();
    effects_.update(s, flares_, cam.eye, math::normalize(cam.target - cam.eye), kStep);
    // Mach 1 itself is uneventful for the pilot (docs/decisions/0020): no
    // camera kick. The script photographs the clean supersonic flight after it.
    if (effects_.takeMachOne() && autopilot_) eventShotAt_ = simStep_ + 150;
    if (s.afterburner && !prevAfterburner_) camera_.impulse(0.06f);
    prevAfterburner_ = s.afterburner;
    if (s.crashed && !wasCrashed && autopilot_) eventShotAt_ = simStep_ + 25;
    time_ += kStep;
}

void FlightScene::update(const platform::Pad& pad, float dt) {
    // Fixed-step simulation: identical results at any frame rate (and on any peer).
    accumulator_ += std::min(dt, 0.1f);
    for (int i = 0; accumulator_ >= kStep && i < kMaxStepsPerFrame; ++i) {
        game::ControlInput in = autopilot_ ? scriptedInput(simStep_) : mapInput(pad);
        stepSimulation(in);
        accumulator_ -= kStep;
        ++simStep_;
        if (model_.state().crashed && (crashTimer_ += kStep) > 2.5f && !autopilot_) {
            restart();
            break;
        }
    }
    if (accumulator_ > kStep) accumulator_ = 0.0f;  // fell behind: drop time, don't spiral
}

gfx::MeshPose FlightScene::pose() const {
    using game::Surface;
    gfx::MeshPose p;
    const struct {
        gfx::MeshChannel channel;
        Surface surface;
    } map[] = {{gfx::kChannelStabilator, Surface::Stabilator},
               {gfx::kChannelFlaperon, Surface::Flaperon},
               {gfx::kChannelSlat, Surface::Slat},
               {gfx::kChannelRudder, Surface::Rudder},
               {gfx::kChannelNozzle, Surface::Nozzle}};
    for (const auto& m : map)
        for (int side = 0; side < 2; ++side) p.angle[m.channel][side] = surfaces_.angle(m.surface, side);
    return p;
}

void FlightScene::render() {
    const game::CameraPose& cam = camera_.pose();
    const game::FlightState& s = model_.state();
    const math::Vec3 sun = world::sunDirection();
    const gfx::FxTextures fx = assets_.fx();

    // Near plane at 3 m, not 1: a vertex clipped to z = near lands at
    // lateral/near on screen, and past the GE's 4096-px virtual screen the
    // whole triangle is dropped - holes in the sea when flying low
    // (docs/psp/gotchas.md #19). The aircraft is never closer than ~15 m.
    gfx::setProjection(cam.fovDeg, 3.0f, 12000.0f);
    gfx::setCamera(cam.eye, cam.target, cam.up);
    const gfx::CameraBasis& basis = gfx::camera();
    sky_.draw(cam.eye, basis.right, basis.up, assets_.sun);

    const uint32_t haze = gfx::abgr(world::kHazeRGB[0], world::kHazeRGB[1], world::kHazeRGB[2]);
    gfx::enableFog(world::kFogNear, world::kFogFar, haze);
    ocean_.draw(cam.eye, assets_.ocean, assets_.waves, time_);
    ocean_.drawGlint(sun, assets_.glint, time_);

    // --- The aircraft with its moving parts; nozzle frames carry the flames.
    const gfx::MeshPose p = pose();
    gfx::NozzleFrame nozzles[2];
    for (int side = 0; side < 2; ++side) {
        math::Vec3 pp = s.position;
        math::Quat pq = s.orientation;
        assets_.su35s.partTransform(gfx::kChannelNozzle, side == 0 ? gfx::kSideRight : gfx::kSideLeft, s.position,
                                    s.orientation, &p, pp, pq);
        math::Vec3 exit = effects_.points().nozzleExit;
        if (side) exit.x = -exit.x;
        nozzles[side] = {pp + pq.rotate(exit), pq.rotate(math::Vec3{0.0f, 0.0f, 1.0f})};
    }
    if (!s.crashed) {
        gfx::drawShadow(s.position, s.orientation, sun, fx);
        gfx::enableSunLight(sun);
        // Afterburner light on the tail booms and stabilators.
        const fx::EngineFx& engine = effects_.engine();
        float glow = std::min(engine.afterburner, 1.0f) * (0.85f + 0.15f * engine.flicker);
        gfx::setPointLight((nozzles[0].exit + nozzles[1].exit) * 0.5f + nozzles[0].axis * 1.5f,
                           gfx::abgr(255, 150, 70), glow);
        assets_.liveries[settings_.livery].bind();
        assets_.su35s.draw(s.position, s.orientation, &p);
        gfx::setPointLight({}, 0, 0.0f);
        gfx::disableLighting();
    }

    clouds_.draw(cam, assets_.cloud);

    // --- Effects: alpha-blended first, additive last.
    const fx::Trail* trails[4 + game::Countermeasures::kMaxFlares];
    int trailCount = 0;
    for (int side = 0; side < 2; ++side) {
        trails[trailCount++] = &effects_.wingtipTrail(side);
        trails[trailCount++] = &effects_.contrail(side);
    }
    for (int i = 0; i < game::Countermeasures::kMaxFlares; ++i) trails[trailCount++] = &effects_.flareTrail(i);
    gfx::drawTrails(trails, trailCount, fx);
    gfx::drawParticles(effects_.particles(), fx);
    if (!s.crashed) {
        gfx::drawLerxVapor(effects_, s.position, s.orientation, fx);
        gfx::drawTransonicVapor(effects_, s.position, s.orientation, fx);
        gfx::drawEngineFlames(effects_.engine(), nozzles, fx);
    }
    gfx::drawFlares(flares_, time_, fx);
    gfx::drawSpeedStreaks(effects_, s.pathDir, s.speed, fx);
    gfx::disableFog();

    // --- Screen space: lens flare, whiteout inside a cloud, HUD.
    sunOnScreen_ = gfx::projectDirection(sun, sunX_, sunY_) && sunX_ >= 0.0f && sunX_ < gfx::kScreenW &&
                   sunY_ >= 0.0f && sunY_ < gfx::kScreenH;
    if (sunOnScreen_) gfx::drawLensFlare(sunX_, sunY_, sunVisibility_, fx);
    gfx::drawScreenTint(gfx::abgr(240, 244, 248, static_cast<unsigned>(clouds_.immersion() * 0.85f * 255.0f)));
    gfx::HudCanvas& hud = gfx::hudCanvas();
    drawFlightHud(hud, s, flares_, lastInput_.psm && !s.psm, time_);
    hud.flush(assets_.line);
}

void FlightScene::overlay(void* drawBuffer) {
    // Is the sun actually visible (not behind the aircraft or a cloud)? Read
    // a few pixels of the finished frame: the sun core is near-white. Used
    // by the next frame's lens flare.
    float visible = 0.0f;
    if (sunOnScreen_) {
        const int offsets[5][2] = {{0, 0}, {-3, 0}, {3, 0}, {0, -3}, {0, 3}};
        int hits = 0;
        for (const auto& o : offsets) {
            uint32_t c = gfx::readPixel(drawBuffer, static_cast<int>(sunX_) + o[0], static_cast<int>(sunY_) + o[1]);
            unsigned r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
            if (r > 235 && g > 228 && b > 205) ++hits;
        }
        visible = hits / 5.0f;
    }
    sunVisibility_ += (visible - sunVisibility_) * 0.35f;
}

}  // namespace scenes
