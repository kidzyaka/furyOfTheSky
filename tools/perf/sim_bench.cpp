// Host benchmark of one simulation step (docs/decisions/0049): the same
// platform-neutral code FlightScene::stepSimulation runs on the PSP, timed
// part by part over a low-level fight in the archipelago - the case where
// the frame rate drops. Absolute host times are tens of times shorter than
// the Allegrex's; the shares are what to read.
//
//   cmake --build build/host && build/host/sim_bench

#include <chrono>
#include <cmath>
#include <cstdio>

#include "fx/aircraft_fx.h"
#include "fx/ship_fx.h"
#include "fx/weapon_fx.h"
#include "game/aircraft_params.h"
#include "game/armament.h"
#include "game/bot.h"
#include "game/control_surfaces.h"
#include "game/countermeasures.h"
#include "game/flight_model.h"
#include "game/radar.h"
#include "game/rwr.h"
#include "game/ship.h"
#include "game/warnings.h"
#include "world/missions.h"
#include "world/terrain.h"

using Clock = std::chrono::steady_clock;

namespace {

enum Part { kPlayer, kBot, kShips, kRadar, kArms, kRwr, kWarn, kFxAir, kFxWeapons, kFxShips, kParts };
const char* kNames[kParts] = {"player model+surfaces+flares", "bot (AI, radar, weapons)", "ships x3", "radar",
                              "armament (head, missiles, gun)", "rwr", "ground warning", "aircraft fx x2",
                              "weapon fx x2", "ship fx x3 (+weapons)"};
double g_us[kParts] = {};

struct Timer {
    Part p;
    Clock::time_point t0 = Clock::now();
    explicit Timer(Part part) : p(part) {}
    ~Timer() { g_us[p] += std::chrono::duration<double, std::micro>(Clock::now() - t0).count(); }
};

}  // namespace

int main() {
    constexpr float kDt = 1.0f / 60.0f;
    constexpr int kSteps = 60 * 60;  // a minute of flight
    world::Terrain terrain;
    world::Arena arena;
    world::buildMap(world::MapId::NorthSea, terrain, arena);

    game::FlightModel model(game::kSu35S);
    // Low over the Sound, heading east (between Conachair and Mykines).
    model.reset({-3600.0f, 250.0f, -4500.0f}, math::Quat::axisAngle(math::kUp, -1.6f), 230.0f);
    game::ControlSurfaces surfaces;
    game::Countermeasures flares;
    game::Armament arms;
    arms.reset();
    game::Radar radar;
    radar.setAutoDesignate(true);
    game::Rwr rwr;
    game::Bot bot;
    bot.setTerrain(&terrain);
    bot.reset(model.state(), game::BotMode::Hard);
    game::Ship ships[3];
    fx::ShipEffects shipFx[3];
    fx::WeaponEffects shipWeaponFx[3];
    const world::MissionLayout& lay = world::missionLayout(0, 2);
    for (int k = 0; k < 3; ++k) {
        const world::ShipRoute& r = lay.routes[k];
        ships[k].reset(r.points[0].x, r.points[0].z, 0.0f, r.points, r.count, 17u + k);
        shipFx[k].reset();
        shipWeaponFx[k].reset();
    }
    fx::AircraftEffects fxPlayer, fxBot;
    fx::WeaponEffects wfxPlayer, wfxBot;
    fxPlayer.reset();
    fxBot.reset();
    wfxPlayer.reset();
    wfxBot.reset();
    game::HitQueue hits, hitsOnMe;

    for (int step = 0; step < kSteps; ++step) {
        game::ControlInput in;
        in.throttleUp = true;
        in.roll = 0.25f * std::sin(step * 0.013f);
        in.pitch = 0.05f + 0.1f * std::sin(step * 0.021f);
        in.gun = (step % 240) < 40;                 // bursts
        in.missile = step % 600 == 300;             // a missile every 10 s
        in.flares = (step % 300) < 10;
        {
            Timer t(kPlayer);
            model.step(in, kDt);
            if (model.state().position.y < 150.0f || terrain.solid(model.state().position))
                model.reset({model.state().position.x, 400.0f, model.state().position.z}, model.state().orientation,
                            230.0f);
            surfaces.update(model.state(), in, kDt);
            flares.update(model.state(), in.flares, kDt);
        }
        const game::FlightState& s = model.state();
        game::Opponent me;
        me.state = &s;
        me.flares = &flares;
        me.missiles = arms.missiles();
        me.missileCount = game::Armament::kMaxMissiles;
        hitsOnMe.clear();
        {
            Timer t(kBot);
            bot.update(me, hitsOnMe, kDt);
            bot.takeRespawned();
            bot.takeDestroyed();
        }
        game::TargetList shipView;
        shipView.terrain = &terrain;
        shipView.add(&s, &flares);
        {
            Timer t(kShips);
            for (auto& sh : ships) sh.update(shipView, 0, false, hitsOnMe, kDt);
        }
        game::TargetList targets;
        targets.terrain = &terrain;
        targets.add(&bot.state(), &bot.flares());
        for (auto& sh : ships) targets.addSurface(&sh.state(), &sh.params().hull);
        {
            Timer t(kRadar);
            radar.update(s, targets, kDt);
        }
        hits.clear();
        {
            Timer t(kArms);
            arms.update(s, in, radar, targets, hits, kDt);
        }
        for (int i = 0; i < hits.count; ++i)
            if (hits.items[i].target == 0) bot.hit(hits.items[i]);
        game::Emitter emitters[4];
        emitters[0].platform = &bot.state();
        emitters[0].radar = &bot.radar();
        emitters[0].arms = &bot.arms();
        emitters[0].terrain = &terrain;
        for (int k = 0; k < 3; ++k) {
            emitters[1 + k].platform = &ships[k].state();
            emitters[1 + k].radar = &ships[k].radar();
            emitters[1 + k].missiles = ships[k].missiles();
            emitters[1 + k].missileCount = game::Ship::kMaxMissiles;
            emitters[1 + k].terrain = &terrain;
        }
        {
            Timer t(kRwr);
            rwr.update(s, emitters, 4, kDt);
        }
        {
            Timer t(kWarn);
            volatile game::GroundWarning w = game::groundWarning(s, &terrain);
            (void)w;
        }
        const math::Vec3 eye = s.position - s.pathDir * 25.0f + math::Vec3{0.0f, 6.0f, 0.0f};
        const math::Vec3 fwd = s.pathDir;
        {
            Timer t(kFxAir);
            fxPlayer.update(s, flares, eye, fwd, kDt);
            fxBot.update(bot.state(), bot.flares(), eye, fwd, kDt);
        }
        {
            Timer t(kFxWeapons);
            wfxPlayer.update(arms.view(), hits, s, &bot.state(), bot.health(), kDt);
            wfxBot.update(bot.arms().view(), hitsOnMe, bot.state(), &s, 100.0f, kDt);
        }
        {
            Timer t(kFxShips);
            for (int k = 0; k < 3; ++k) {
                shipFx[k].update(ships[k], kDt);
                shipWeaponFx[k].update(ships[k].weapons(), hitsOnMe, ships[k].state(), nullptr, 100.0f, kDt);
            }
        }
    }
    double total = 0.0;
    for (double v : g_us) total += v;
    std::printf("sim step over the archipelago, %d steps: %.1f us/step on this host\n", kSteps, total / kSteps);
    for (int p = 0; p < kParts; ++p)
        std::printf("  %-32s %7.2f us/step  %5.1f %%\n", kNames[p], g_us[p] / kSteps, 100.0 * g_us[p] / total);
    return 0;
}
