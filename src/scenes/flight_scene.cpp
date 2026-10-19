#include "scenes/flight_scene.h"

#include <pspctrl.h>

#include <algorithm>
#include <cmath>

#include "audio/mixer.h"
#include "game/world.h"
#include "render/fx_draw.h"
#include "render/gu_context.h"
#include "render/hud_draw.h"
#include "render/screenshot.h"
#include "platform/psp_system.h"
#include "render/view.h"
#include "scenes/flight_hud.h"
#include "ui/ui.h"

namespace scenes {


namespace {

constexpr float kSpeedOfSound = 340.0f;  // m/s, for sound delays

gfx::MeshPose poseOf(const game::ControlSurfaces& surfaces) {
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
        for (int side = 0; side < 2; ++side) p.angle[m.channel][side] = surfaces.angle(m.surface, side);
    return p;
}

}  // namespace

FlightScene::FlightScene(Assets& assets)
    : assets_(assets), model_(game::kSu35S), informer_(game::kSu35S) {
    ocean_.init();
    deck_.init();
    world::buildMap(world::MapId::NorthSea, terrain_, arena_);
    int placed[world::kRockShapeCount] = {};
    for (int i = 0; i < terrain_.count(); ++i) rockInstance_[i] = static_cast<uint8_t>(placed[terrain_.rock(i).shape]++);
    applyEnvironment(world::Weather::Clear, world::TimeOfDay::Day);
    bot_.setTerrain(&terrain_);
    bot_.setArena(&arena_);
    informer_.setTerrain(&terrain_);
}

void FlightScene::applyEnvironment(world::Weather weather, world::TimeOfDay time) {
    if (envReady_ && env_.weather == weather && env_.time == time) return;
    env_ = world::makeEnvironment(weather, time);
    envReady_ = true;
    sky_.init(env_);
    clouds_.init(0x35C0FFEE, env_);
    ocean_.setup(env_);
    deck_.setup(env_);
    clearColor_ = world::packAbgr(env_.haze);
    bakeRocks();
}

gfx::LightRig FlightScene::lightRig() const {
    // docs/decisions/0019 rig, coloured by the sortie's light (0041): the sun
    // (with specular), the sky from above, the sea from below, ambient. Under
    // a deck the sun is a diffuse glow: the key drops, the ambient picks up.
    gfx::LightRig r;
    r.keyDir = env_.sunDir;
    r.fillDir = math::kUp;
    r.bounceDir = -math::kUp;
    r.key = world::packAbgr(world::scale(env_.key, sunLight_));
    r.keySpecular = world::packAbgr(world::scale(env_.keySpecular, sunLight_ * sunLight_));
    r.fill = world::packAbgr(env_.fill);
    r.bounce = world::packAbgr(env_.bounce);
    r.ambient = world::packAbgr(world::scale(env_.ambient, 1.0f + 0.5f * (1.0f - sunLight_)));
    return r;
}

void FlightScene::bakeRocks() {
    // Rock and turf (docs/decisions/0042) take sky light from the whole
    // hemisphere they face: a cliff in shade still sees half the sky and the
    // sea below it - folded into the ambient; the sun is the one directional
    // light. Baked into each placed rock's vertex colours once a sortie
    // (docs/decisions/0050): the GE then fetches 16 bytes a vertex and lights
    // nothing. The light is the rocks' own - at sea level, under any deck -
    // not the camera's (lightRig() dims the key for the aircraft by where the
    // camera is).
    const float sun = env_.directLight(0.0f);
    gfx::TerrainLight light;
    light.dirToSun = env_.sunDir;
    light.key = world::packAbgr(world::scale(env_.key, sun));
    light.ambient = world::packAbgr(world::add(world::scale(env_.ambient, 1.0f + 0.5f * (1.0f - sun)),
                                               world::add(world::scale(env_.fill, 0.8f), world::scale(env_.bounce, 0.6f))));
    for (int i = 0; i < terrain_.count(); ++i) {
        const world::Rock& r = terrain_.rock(i);
        assets_.rocks[r.shape].bake(rockInstance_[i], r.cosYaw, r.sinYaw, light);
    }
}

void FlightScene::updateWeather(const math::Vec3& eye) {
    // Rain and snow fall under and inside the deck, not above it.
    const float wet = env_.deck && eye.y > env_.deckTop ? 0.0f : 1.0f;
    const math::Vec3 wind{4.0f, 0.0f, -6.0f};  // m/s, with the swell (render/ocean.cpp)
    precip_.update(eye, env_.rain * wet, env_.snow * wet, wind, kStep);
    // Lightning in the storm cells: random strokes, ~`lightning` a minute.
    flash_ = std::max(0.0f, flash_ - kStep * 6.0f);
    if (env_.lightning > 0.0f && (nextStroke_ -= kStep) <= 0.0f) {
        auto rnd = [this] {
            stormRng_ = stormRng_ * 1664525u + 1013904223u;
            return static_cast<float>(stormRng_ >> 8) * (1.0f / 16777216.0f);
        };
        flash_ = 1.0f;
        nextStroke_ = -std::log(std::max(rnd(), 0.02f)) * 60.0f / env_.lightning;
        // Thunder rolls in at the speed of sound from 2..9 km.
        const float dist = 2000.0f + 7000.0f * rnd();
        playDelayed(assets_.blastFar, dist / kSpeedOfSound, 0.55f * 2000.0f / dist + 0.15f, rnd() * 1.2f - 0.6f);
    }
}

void FlightScene::configure(const meta::SortieConfig& sortie, const meta::Settings& settings) {
    sortie_ = sortie;
    settings_ = settings;
    // A campaign mission flies in its own weather; the training range is a clear day.
    world::Weather weather = world::Weather::Clear;
    world::TimeOfDay time = world::TimeOfDay::Day;
    if (sortie.mode == meta::SortieMode::Mission && sortie.location >= 0 && sortie.location < meta::locationCount()) {
        const meta::Location& loc = meta::location(sortie.location);
        if (sortie.mission >= 0 && sortie.mission < loc.missionCount) {
            weather = loc.missions[sortie.mission].weather;
            time = loc.missions[sortie.mission].time;
        }
    }
    applyEnvironment(weather, time);
    camera_.setShake(settings_.cameraShake ? 1.0f : 0.0f);
    applyVolumes();
}

void FlightScene::applyVolumes() {
    audio::Mixer& m = audio::mixer();
    m.setVolume(audio::kVolumeSfx, audio::volumeGain(settings_.sfxVolume));
    m.setVolume(audio::kVolumeVoice, audio::volumeGain(settings_.voiceVolume));
    m.setVolume(audio::kVolumeUi, audio::volumeGain(settings_.uiVolume));
}

game::BotMode FlightScene::enemyMode() const {
    return sortie_.mode == meta::SortieMode::Mission ? meta::missionEnemy(sortie_.difficulty) : sortie_.enemy;
}

int FlightScene::liveryIndex() const { return std::clamp(static_cast<int>(sortie_.livery), 0, kLiveryCount - 1); }

meta::SortieResult FlightScene::result() const {
    meta::SortieResult r;
    r.outcome = outcome_;
    r.airKills = kills_;
    r.groundKills = groundKills_;
    r.timesShotDown = losses_;
    r.seconds = sortieTime_;
    return r;
}

void FlightScene::abort() {
    if (finished_) return;
    finished_ = true;
    outcome_ = meta::SortieOutcome::Aborted;
}

void FlightScene::enter() {
    kills_ = deaths_ = losses_ = groundKills_ = 0;
    sortieTime_ = 0.0f;
    finished_ = false;
    outcome_ = meta::SortieOutcome::Aborted;
    for (int i = 0; i < audio::kMessageCount; ++i)
        informer_.setDuration(static_cast<audio::Message>(i), assets_.voice[i].seconds());
    applyVolumes();  // after a pause or a visit to another scene the mixer may hold stale ones
    restart();
    startLoops();
}

void FlightScene::leave() {
    audio::Mixer& m = audio::mixer();
    for (int ch = 0; ch < audio::kChannelCount; ++ch)
        if (ch != audio::kUi) m.stop(ch);
}

// The continuous sounds run for the whole visit at their current levels;
// updateAudio() re-targets them every frame.
void FlightScene::startLoops() {
    audio::Mixer& m = audio::mixer();
    const audio::FlightSoundMix& x = audio_.mix();
    m.play(audio::kEngineRoar, &assets_.roar, x.roar.gain, x.roar.pitch, 0.0f, true);
    m.play(audio::kEngineWhine, &assets_.whine, x.whine.gain, x.whine.pitch, 0.0f, true);
    m.play(audio::kAfterburner, &assets_.afterburner, x.afterburner.gain, x.afterburner.pitch, 0.0f, true);
    m.play(audio::kWind, &assets_.wind, x.wind.gain, x.wind.pitch, 0.0f, true);
    m.play(audio::kBuffet, &assets_.buffet, x.buffet.gain, x.buffet.pitch, 0.0f, true);
}

void FlightScene::updateAudio() {
    audio::Mixer& m = audio::mixer();
    const audio::FlightSoundMix& x = audio_.mix();
    m.set(audio::kEngineRoar, x.roar.gain, x.roar.pitch);
    m.set(audio::kEngineWhine, x.whine.gain, x.whine.pitch);
    m.set(audio::kAfterburner, x.afterburner.gain, x.afterburner.pitch);
    m.set(audio::kWind, x.wind.gain, x.wind.pitch);
    m.set(audio::kBuffet, x.buffet.gain, x.buffet.pitch);
}

void FlightScene::playOneShot(const audio::Sound& sound, float gain, float pan) {
    audio::Mixer& m = audio::mixer();
    m.play(m.nextOneShot(), &sound, gain, 1.0f, pan);
}

void FlightScene::playDelayed(const audio::Sound& sound, float delay, float gain, float pan) {
    if (delayedCount_ < kMaxDelayed) delayed_[delayedCount_++] = {&sound, delay, gain, pan};
}

void FlightScene::showMessage(const char* text, uint32_t color) {
    // A stream of 30 mm hits keeps one HIT up; DESTROYED stays up over them.
    if (message_ == text && messageAge_ < 0.3f) return;
    if (message_ && message_[0] == 'D' && text[0] != 'D' && messageAge_ < 1.5f) return;
    message_ = text;
    messageColor_ = color;
    messageAge_ = 0.0f;
}

void FlightScene::restart() {
    // The loadout first: the start is trimmed for what hangs on the pylons.
    // The Ace Combat reloads stay on in every mode, but only for loaded pylons.
    arms_.reset(true, meta::stationMask(sortie_.loadout));
    const game::StoresRatios load = arms_.storesRatios(model_.params());
    model_.setStores(load.mass, load.drag);
    // Where the mission starts on the map (the training range: its centre).
    const world::MissionLayout& lay = layout();
    restartAt(lay.playerStart, lay.playerSpeed, lay.playerHeading);
    flares_.reset();
    surfaces_.reset();
    radar_.reset();
    // The test sortie starts against an unarmed target; its HARD bot comes later.
    bot_.setLowLevel(isMission() && lay.lowLevel);
    bot_.reset(model_.state(), autopilot_ ? game::BotMode::Straight : enemyMode());
    // Ground targets: the ships of the mission variant for this aircraft type.
    int ground = 0;
    if (isMission() && sortie_.location >= 0 && sortie_.location < meta::locationCount()) {
        const meta::Location& loc = meta::location(sortie_.location);
        if (sortie_.mission >= 0 && sortie_.mission < loc.missionCount)
            if (const meta::MissionVariant* v =
                    meta::missionVariant(loc.missions[sortie_.mission], meta::vehicle(sortie_.vehicle).type))
                ground = std::min<int>(v->groundTargets, std::min(lay.ships, kMaxShips));
    }
    spawnShips(lay, ground);
    objective_.reset(isMission() ? std::max<int>(sortie_.airEnemies, 1) + ground : 0, ground);
    enemyHeld_ = false;
    droneFx_.reset();
    weaponFx_.reset();
    botWeaponFx_.reset();
    bot_.takeRespawned();
    rwr_.reset();
    health_ = 100.0f;
    hitFlash_ = 0.0f;
    fireSide_ = 0;
    message_ = nullptr;
    messageAge_ = 99.0f;
    delayedCount_ = 0;
    simStep_ = 0;
    time_ = 0.0f;
    eventShotAt_ = -1;
    hitShotAt_ = -1;
    rwrShotAt_ = -1;
    missileShotAt_ = -1;
}

const world::MissionLayout& FlightScene::layout() const {
    return isMission() ? world::missionLayout(sortie_.location, sortie_.mission) : world::trainingLayout();
}

void FlightScene::spawnShips(const world::MissionLayout& lay, int count) {
    shipCount_ = std::min(count, kMaxShips);
    for (int i = 0; i < shipCount_; ++i) {
        const world::ShipRoute& r = lay.routes[i];
        const math::Vec3 a = r.points[0], b = r.points[r.count > 1 ? 1 : 0];
        ships_[i].reset(a.x, a.z, std::atan2(b.x - a.x, -(b.z - a.z)), r.points, r.count, 0x5417u + 977u * i);
        shipFx_[i].reset();
        shipWeaponFx_[i].reset();
    }
}

void FlightScene::restartAt(const math::Vec3& pos, float speed, float heading) {
    // Quat about +Y turns -Z towards -X for a positive angle: the heading is the opposite.
    model_.reset(pos, math::Quat::axisAngle(math::kUp, -heading), speed);
    boundary_.reset();
    camera_.reset(model_.state());
    effects_.reset();
    precip_.reset();
    audio_.reset();
    informer_.reset();
    accumulator_ = 0.0f;
    crashTimer_ = 0.0f;
    prevAfterburner_ = false;
    lastInput_ = game::ControlInput{};
    if (gunSounding_) audio::mixer().stop(audio::kGun);
    if (tone_) audio::mixer().stop(audio::kTone);
    if (rwrSound_) audio::mixer().stop(audio::kRwr);
    gunSounding_ = false;
    tone_ = 0;
    rwrSound_ = 0;
}

void FlightScene::respawnBot() {
    bot_.reset(model_.state(), enemyMode());
    bot_.takeRespawned();
    droneFx_.reset();
    weaponFx_.targetMoved();
    botWeaponFx_.reset();
    radar_.reset();
    rwr_.reset();
}

void FlightScene::damage(const game::HitEvent& e) {
    // The enemy's 30 mm and warheads on us (docs/decisions/0033). The test
    // sortie flies on regardless - its screenshots must not depend on a bot.
    if (autopilot_ || model_.state().crashed) return;
    const game::FlightState& s = model_.state();
    health_ -= e.damage;
    hitFlash_ = 1.0f;
    camera_.impulse(e.kind == game::HitKind::Missile ? 0.5f : 0.15f);
    playOneShot(assets_.hit, 1.0f, math::clamp(math::dot(e.pos - s.position, s.orientation.right()) * 0.2f, -0.6f, 0.6f));
    // Heavy damage sets an engine on fire - the side the hit came from.
    if (health_ < 40.0f && fireSide_ == 0)
        fireSide_ = math::dot(e.pos - s.position, s.orientation.right()) < 0.0f ? -1 : 1;
    if (health_ <= 0.0f) {
        health_ = 0.0f;
        model_.destroy();
        ++deaths_;
        showMessage("SHOT DOWN", hud::kRed);
    }
}

game::ControlInput FlightScene::mapInput(const platform::Pad& pad) const {
    // Ace Combat X (PSP) layout (docs/decisions/0030): R/L throttle and
    // brake, X gun, O missile, triangle target, D-pad yaw; flares stay on
    // square, the weapon switch is SELECT (the PS2 Ace Combats' toggle).
    game::ControlInput in;
    // Pad y is +1 with the stick pulled back: that is "nose up" unless inverted.
    in.pitch = settings_.invertPitch ? -pad.ly : pad.ly;
    in.roll = pad.lx;
    in.yaw = (pad.down(PSP_CTRL_RIGHT) ? 1.0f : 0.0f) - (pad.down(PSP_CTRL_LEFT) ? 1.0f : 0.0f);
    in.throttleUp = pad.down(PSP_CTRL_RTRIGGER);
    in.throttleDown = pad.down(PSP_CTRL_LTRIGGER);
    // Ace Combat 7 post-stall: brake + stick hard back. Slow, the FCS limiter
    // goes (cobra, kulbit); fast, the G-limiter is overridden. The engines
    // stay at MIL for the nozzles, so the brake is let off meanwhile.
    in.psm = in.throttleDown && in.pitch > 0.85f;
    if (in.psm) in.throttleDown = false;
    in.flares = pad.down(PSP_CTRL_SQUARE);
    in.gun = pad.down(PSP_CTRL_CROSS);
    in.missile = pad.down(PSP_CTRL_CIRCLE);
    in.cycleTarget = pad.down(PSP_CTRL_TRIANGLE);
    in.cycleWeapon = pad.down(PSP_CTRL_SELECT);
    return in;
}

game::ControlInput FlightScene::scriptedInput(int step) {
    // A sortie that shows every effect and the weapons for the smoke-test screenshots.
    game::ControlInput in;
    const math::Vec3 here = model_.state().position;
    if (step == 50 || step == 160 || step == 238 || step == 345 || step == 445 || step == 980 || step == 1012 ||
        step == 1080 || step == 1356 || step == 1485 || step == 2160 || step == 2350 || step == 2410 ||
        step == 2470 || step == 2530 || step == 2590 || step == 2650 || step == 2700 || step == 2770 ||
        step == 2830 || step == 2890 || step == 2950 || step == 3010 || step == 3065 || step == 3130 || step == 3180 || step == 3250 || step == 3310 ||
        step == eventShotAt_ || step == hitShotAt_ ||
        step == rwrShotAt_ || step == missileShotAt_)
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
    // 5. Weapons against the drone flying straight 3 km ahead: the R-73's head
    //    slews and locks (980), launch off the rail (1012), the smoke trail
    //    (1080), the hit (event); an R-77 kicked off its ejector (1356); a gun
    //    burst from 600 m (1485, rounds striking).
    if (step == 880) {
        restartAt({here.x, 1500.0f, here.z}, 250.0f);
        bot_.reset(model_.state(), game::BotMode::Straight);
        bot_.takeRespawned();
        droneFx_.reset();
        weaponFx_.targetMoved();
        radar_.reset();
        arms_.reset(true, meta::stationMask(sortie_.loadout));
    }
    if (step >= 1000 && step < 1004) in.missile = true;
    if (step >= 1320 && step < 1323) in.cycleWeapon = true;
    if (step >= 1340 && step < 1344) in.missile = true;
    if (step == 1420) {
        // 600 m behind it, low by what the trim AoA raises the gun line (less
        // the rounds' drop): the burst goes where a pilot on the pipper puts it.
        const game::FlightState& d = bot_.state();
        const math::Vec3 at = d.position - d.pathDir * 600.0f;
        model_.reset(at, math::lookRotation(d.pathDir, math::kUp), d.speed + 10.0f);
        const float drop = 0.5f * math::kGravity * 0.75f * 0.75f;
        model_.reset(at - math::kUp * (600.0f * std::tan(model_.state().alpha) - drop),
                     math::lookRotation(d.pathDir, math::kUp), d.speed + 10.0f);
        camera_.reset(model_.state());
    }
    if (step >= 1430 && step < 1470) in.gun = true;
    // 6. A HARD bot comes in from 9 km: its radar lock on the RWR (event shot),
    //    its R-77 on the way - MISSILE, the red bar (event shot). The script's
    //    aircraft takes no damage, so the sortie doesn't depend on the bot.
    if (step == 1500) {
        restartAt({here.x, 1500.0f, here.z}, 250.0f);
        bot_.reset(model_.state(), game::BotMode::Hard);
        bot_.takeRespawned();
        droneFx_.reset();
        weaponFx_.targetMoved();
        radar_.reset();
        rwr_.reset();
    }
    // 7. Low pass over the waves: spray and shadow. 8. Into the sea.
    if (step == 2100) restartAt({here.x, 14.0f, here.z}, 240.0f);
    if (step >= 2190 && step < 2300) in.pitch = -0.6f;
    // 9. The weathers (docs/decisions/0041), one shot each, in open water
    //    south of the islands looking north at them: dawn under scattered
    //    cumulus; under the overcast deck; above it; a storm at dusk with
    //    rain and big seas; snow; a low pass over the storm's waves.
    // 10. The archipelago (docs/decisions/0042, 0044): the approach; low in
    //     the Sound; a pass at 8 m (rooster tail, white water); 80 m off
    //     Conachair's cliffs (software clipping next to the camera); the
    //     stack garden; Mykines under an overcast.
    struct TourShot {
        int step;
        world::Weather weather;
        world::TimeOfDay time;
        float x, altitude, z, heading;
    };
    static const TourShot kTour[] = {
        {2300, world::Weather::Cloudy, world::TimeOfDay::Dawn, 1800.0f, 450.0f, 4200.0f, 0.0f},
        {2360, world::Weather::Overcast, world::TimeOfDay::Day, 1800.0f, 600.0f, 4200.0f, 0.0f},
        {2420, world::Weather::Overcast, world::TimeOfDay::Day, 1800.0f, 2300.0f, 4200.0f, 0.0f},
        {2480, world::Weather::Rain, world::TimeOfDay::Dusk, 1800.0f, 220.0f, 4200.0f, 0.0f},
        {2540, world::Weather::Snow, world::TimeOfDay::Day, 1800.0f, 400.0f, 4200.0f, 0.0f},
        {2600, world::Weather::Rain, world::TimeOfDay::Dusk, 1800.0f, 25.0f, 4200.0f, 0.0f},
        {2660, world::Weather::Clear, world::TimeOfDay::Day, -1500.0f, 650.0f, 1500.0f, -0.25f},
        {2720, world::Weather::Clear, world::TimeOfDay::Day, -3600.0f, 110.0f, -4500.0f, 1.6f},
        {2790, world::Weather::Clear, world::TimeOfDay::Day, 1800.0f, 8.0f, 4800.0f, 0.0f},
        {2850, world::Weather::Clear, world::TimeOfDay::Day, -3700.0f, 150.0f, -3820.0f, 1.62f},
        {2910, world::Weather::Clear, world::TimeOfDay::Day, -4400.0f, 120.0f, -4200.0f, -0.9f},
        {2970, world::Weather::Overcast, world::TimeOfDay::Day, 1200.0f, 800.0f, -3300.0f, -0.1f},
    };
    for (const TourShot& w : kTour)
        if (step == w.step) {
            applyEnvironment(w.weather, w.time);
            restartAt({w.x, w.altitude, w.z}, w.altitude < 20.0f ? 240.0f : 220.0f, w.heading);
        }
    // 11. Two patrol ships working up the Sound, a pass at 150 m from its
    //     west end: their wakes, then their AK-630 and missiles at us
    //     (docs/decisions/0045).
    if (step == 3030) {
        applyEnvironment(world::Weather::Clear, world::TimeOfDay::Day);
        restartAt({-4600.0f, 150.0f, -4450.0f}, 200.0f, 1.6f);
        world::MissionLayout lay;
        lay.ships = 2;
        lay.routes[0].count = 2;
        lay.routes[0].points[0] = {-2800.0f, 0.0f, -4380.0f};
        lay.routes[0].points[1] = {100.0f, 0.0f, -4150.0f};
        lay.routes[1].count = 2;
        lay.routes[1].points[0] = {-1700.0f, 0.0f, -4320.0f};
        lay.routes[1].points[1] = {1600.0f, 0.0f, -4350.0f};
        spawnShips(lay, 2);
    }
    // 12. 40-45 m off Conachair's cliff at 45 m, the camera as close: its
    //     chunks go through the software clip (counter c > 0) - no holes.
    if (step == 3150) restartAt({-3400.0f, 45.0f, -3780.0f}, 220.0f, 1.62f);
    // 13. The fog bank (docs/decisions/0041): inside it at 60 m (the sea and
    //     the stacks a kilometre out), then above it at 400 m - the peaks
    //     standing out of a white sea of fog.
    if (step == 3210) {
        applyEnvironment(world::Weather::Fog, world::TimeOfDay::Dawn);
        restartAt({1800.0f, 60.0f, 4200.0f}, 220.0f, 0.0f);
    }
    if (step == 3270) restartAt({-1500.0f, 400.0f, 1500.0f}, 220.0f, -0.25f);
    // Banked under the cumulus at dawn: the clouds must stay level with the
    // horizon, not roll with the camera.
    if (step >= 2305 && step < 2350) in.roll = 0.55f;
    return in;
}

void FlightScene::weaponAudio(const game::FlightState& s) {
    audio::Mixer& mixer = audio::mixer();
    const game::WeaponEvents& ev = arms_.events();
    // Cannon: a loop while it fires, the last shot's tail on release.
    const bool firing = arms_.gun().firing();
    if (firing != gunSounding_) {
        if (firing) {
            mixer.play(audio::kGun, &assets_.gunBurst, 0.75f, 1.0f, 0.15f, true);
        } else {
            mixer.stop(audio::kGun);
            playOneShot(assets_.gunTail, 0.75f, 0.15f);
        }
        gunSounding_ = firing;
    }
    // Launches: a rail missile lights at once; an ejected one is kicked off
    // with a clunk and lights 0.4 s later (game/missile.cpp).
    for (int i = 0; i < ev.launches; ++i) {
        const game::Station& st = game::kSu35SStations[ev.launchStation[i]];
        const float pan = st.attach.x > 0.0f ? 0.35f : -0.35f;
        if (st.launcher == game::Launcher::Rail) {
            playOneShot(assets_.missileLaunch, 0.85f, pan);
        } else {
            playOneShot(assets_.missileEject, 0.8f, pan);
            playDelayed(assets_.missileLaunch, 0.4f, 0.85f, pan);
        }
    }
    // Warheads and 30 mm strikes are far away: heard at the speed of sound.
    auto heard = [&](const audio::Sound& snd, const math::Vec3& at, float nearGain, float nearDist) {
        const math::Vec3 rel = at - s.position;
        const float d = std::max(math::length(rel), 1.0f);
        const float pan = math::clamp(math::dot(rel, s.orientation.right()) / d, -1.0f, 1.0f) * 0.6f;
        playDelayed(snd, d / kSpeedOfSound, math::clamp(nearDist / d, 0.12f, 1.0f) * nearGain, pan);
    };
    for (int i = 0; i < ev.ends; ++i) heard(assets_.blastFar, ev.end[i].pos, 1.0f, 900.0f);
    for (int i = 0; i < hits_.count; ++i)
        if (hits_.items[i].kind == game::HitKind::Shell) heard(assets_.hit, hits_.items[i].pos, 0.7f, 400.0f);

    // Headset tone: the R-73's seeker searching / locked; for the R-77 the
    // radar acquiring (docs/decisions/0050), then the lock in the zone.
    int tone = 0;
    if (!s.crashed) {
        if (arms_.selected() == game::MissileSlot::R73) {
            // The head's audio: searching while it is being cued at a target, steady once locked.
            if (arms_.seekerLock() >= 0)
                tone = 2;
            else if (radar_.designated() >= 0)
                tone = 1;
        } else if (arms_.locked() && arms_.inZone()) {
            tone = 2;
        } else if (arms_.lockProgress() > 0.0f && !arms_.locked()) {
            tone = 1;
        }
    }
    if (tone != tone_) {
        if (tone == 0)
            mixer.stop(audio::kTone);
        else
            mixer.play(audio::kTone, tone == 1 ? &assets_.toneSeek : &assets_.toneLock, tone == 1 ? 0.35f : 0.4f,
                       1.0f, 0.0f, true);
        tone_ = tone;
    }

    // RWR (docs/decisions/0034), SPO-15 tones: a low double beep for a new
    // radar, a steady high tone while one holds us, the high tone beeping
    // 4 times a second while a missile homes on us.
    if (rwr_.newContact()) playOneShot(assets_.rwrNew, 0.6f);
    const int rwr = s.crashed ? 0 : rwr_.missile() ? 2 : rwr_.worst() == game::RwrLevel::Lock ? 1 : 0;
    if (rwr != rwrSound_) {
        if (rwr == 0)
            mixer.stop(audio::kRwr);
        else
            mixer.play(audio::kRwr, rwr == 2 ? &assets_.rwrMissile : &assets_.rwrLock, rwr == 2 ? 0.55f : 0.4f, 1.0f,
                       0.0f, true);
        rwrSound_ = rwr;
    }
    // Ships' guns and missiles, heard from where they are.
    for (int k = 0; k < shipCount_; ++k) {
        const game::WeaponEvents& sev = *ships_[k].weapons().events;
        for (int i = 0; i < sev.launches; ++i) heard(assets_.missileLaunch, ships_[k].state().position, 0.8f, 400.0f);
        for (int i = 0; i < sev.ends; ++i) heard(assets_.blastFar, sev.end[i].pos, 1.0f, 900.0f);
        if (ships_[k].gun().firing() && ships_[k].gun().fired() > 0 && static_cast<int>(time_ * 4.0f) % 4 == 0)
            heard(assets_.gunTail, ships_[k].state().position, 0.6f, 600.0f);
    }
    // The enemy's launches, heard if it's close enough.
    const game::WeaponEvents& bev = bot_.arms().events();
    for (int i = 0; i < bev.launches; ++i) heard(assets_.missileLaunch, bot_.state().position, 0.8f, 300.0f);
    for (int i = 0; i < bev.ends; ++i) heard(assets_.blastFar, bev.end[i].pos, 1.0f, 900.0f);

    // Delayed sounds whose time has come.
    for (int i = 0; i < delayedCount_;) {
        Delayed& d = delayed_[i];
        if ((d.wait -= kStep) > 0.0f) {
            ++i;
            continue;
        }
        playOneShot(*d.sound, d.gain, d.pan);
        d = delayed_[--delayedCount_];
    }
}

void FlightScene::stepSimulation(const game::ControlInput& in) {
    lastInput_ = in;
    const bool wasCrashed = model_.state().crashed;
    // What hangs on the pylons now - missiles leave, reloads bring them back.
    const game::StoresRatios load = arms_.storesRatios(model_.params());
    model_.setStores(load.mass, load.drag);
    model_.step(in, kStep);
    // Into a rock: the same end as the sea (the explosion, the loss).
    if (!autopilot_ && terrain_.solid(model_.state().position)) model_.destroy();
    // Past the mission area's edge the game swings the nose back.
    model_.turnHeading(boundary_.update(model_.state(), arena_, kStep));
    const game::FlightState& s = model_.state();
    surfaces_.update(s, in, kStep);
    flares_.update(s, in.flares, kStep);

    // --- The enemy, the radar, the weapons, the RWR. Free flight has no enemy
    // at all; in a mission the last one does not come back.
    const bool enemy = enemyEnabled();
    if (enemy && !autopilot_ && !enemyHeld_ && bot_.mode() != enemyMode()) respawnBot();
    game::Opponent me;
    me.state = &s;
    me.flares = &flares_;
    me.missiles = arms_.missiles();
    me.missileCount = game::Armament::kMaxMissiles;
    hitsOnMe_.clear();
    if (enemy && !enemyHeld_) bot_.update(me, hitsOnMe_, kStep);
    if (enemy && bot_.takeRespawned()) {
        droneFx_.reset();
        weaponFx_.targetMoved();
    }
    // The ships (docs/decisions/0045): they see only us; a missile of ours
    // homing on one makes it weave.
    game::TargetList shipView;
    shipView.terrain = &terrain_;
    shipView.add(&s, &flares_);
    for (int k = 0; k < shipCount_; ++k) {
        bool threatened = false;
        for (int m = 0; m < game::Armament::kMaxMissiles; ++m) {
            const game::Missile& mi = arms_.missile(m);
            threatened |= mi.live() && (mi.target == k + 1 || mi.trackIndex == k + 1);
        }
        ships_[k].update(shipView, 0, threatened, hitsOnMe_, kStep);
        if (ships_[k].takeDestroyed()) {
            showMessage("DESTROYED", hud::kRed);
            ++groundKills_;
            objective_.groundTargetDestroyed();
        }
    }
    // Fixed slots: the enemy aircraft is target 0 (a shot-down one is still
    // there, "crashed": the radar drops it, the missiles lose it), ships 1..n.
    game::TargetList targets;
    targets.terrain = &terrain_;
    if (enemy) targets.add(&bot_.state(), &bot_.flares());
    for (int k = 0; k < shipCount_; ++k) targets.addSurface(&ships_[k].state(), &ships_[k].params().hull);
    radar_.update(s, targets, kStep);
    hits_.clear();
    arms_.update(s, in, radar_, targets, hits_, kStep);
    for (int i = 0; i < hits_.count; ++i) {
        const int t = hits_.items[i].target;
        if (t == 0 && enemy)
            bot_.hit(hits_.items[i]);
        else if (t >= 1 && t <= shipCount_)
            ships_[t - 1].hit(hits_.items[i]);
        showMessage("HIT", hud::kGreen);
        if (autopilot_ && hitShotAt_ < 0 && hits_.items[i].kind == game::HitKind::Missile) hitShotAt_ = simStep_ + 3;
    }
    if (enemy && bot_.takeDestroyed()) {
        showMessage("DESTROYED", hud::kRed);
        ++kills_;
        objective_.enemyDestroyed();
        if (objective_.airDone()) enemyHeld_ = true;  // that was the last one: it stays down
    }
    for (int i = 0; i < hitsOnMe_.count; ++i) damage(hitsOnMe_.items[i]);
    hitFlash_ = std::max(0.0f, hitFlash_ - kStep * 3.0f);
    const game::WeaponEvents& ev = arms_.events();
    for (int i = 0; i < ev.ends; ++i)
        if (ev.end[i].end != game::MissileEnd::Proximity && ev.end[i].aimedAt >= 0) showMessage("MISS", hud::kAmber);
    messageAge_ += kStep;
    // RWR: the enemy fighter in slot 0, the ships after it.
    game::Emitter emitters[1 + kMaxShips];
    if (enemy && isCombat(bot_.mode()) && bot_.alive()) {
        emitters[0].platform = &bot_.state();
        emitters[0].radar = &bot_.radar();
        emitters[0].arms = &bot_.arms();
    }
    emitters[0].terrain = &terrain_;
    for (int k = 0; k < shipCount_; ++k) {
        game::Emitter& e = emitters[1 + k];
        e.platform = &ships_[k].state();
        e.radar = &ships_[k].radar();
        e.missiles = ships_[k].missiles();
        e.missileCount = game::Ship::kMaxMissiles;
        e.terrain = &terrain_;
    }
    rwr_.update(s, emitters, 1 + shipCount_, kStep);
    if (autopilot_ && rwrShotAt_ < 0 && rwr_.worst() == game::RwrLevel::Lock) rwrShotAt_ = simStep_ + 20;
    if (autopilot_ && missileShotAt_ < 0 && rwr_.missile()) missileShotAt_ = simStep_ + 40;

    // The GSh-30-1's recoil shakes the whole airframe while it fires.
    if (arms_.gun().firing()) camera_.vibrate(0.04f);
    camera_.update(s, kStep);
    const game::CameraPose& cam = camera_.pose();
    const math::Vec3 camFwd = math::normalize(cam.target - cam.eye);
    effects_.update(s, flares_, cam.eye, camFwd, kStep);
    updateWeather(cam.eye);
    if (enemy) droneFx_.update(bot_.state(), bot_.flares(), cam.eye, camFwd, kStep);
    weaponFx_.update(arms_.view(), hits_, s, enemy ? &bot_.state() : nullptr, bot_.health(), kStep);
    // The enemy's weapons; its "target" smoke is ours once we're hurt.
    if (enemy) botWeaponFx_.update(bot_.arms().view(), hitsOnMe_, bot_.state(), &s, health_, kStep);
    for (int k = 0; k < shipCount_; ++k) {
        shipFx_[k].update(ships_[k], kStep);
        shipWeaponFx_[k].update(ships_[k].weapons(), hitsOnMe_, ships_[k].state(), nullptr, 100.0f, kStep);
    }
    // Mach 1 itself is uneventful for the pilot (docs/decisions/0020): no
    // camera kick. The script photographs the clean supersonic flight after it.
    if (effects_.takeMachOne() && autopilot_ && eventShotAt_ < 0) eventShotAt_ = simStep_ + 150;
    if (s.afterburner && !prevAfterburner_) {
        camera_.impulse(0.06f);
        playOneShot(assets_.abLight, 0.9f);
    }
    prevAfterburner_ = s.afterburner;

    // --- Sound: loop levels, cartridge pops, the crash, weapons, the voice informer.
    audio_.update(s, effects_.engine(), effects_.transonic(), kStep);
    const int released = flares_.justReleased();
    if (released >= 0) {
        // Pan by which block fired: left/right of the fuselage as seen from behind.
        const float side = math::dot(flares_.flare(released).pos - s.position, s.orientation.right());
        playOneShot(assets_.flare, 0.7f, side > 0.0f ? 0.45f : -0.45f);
    }
    if (s.crashed && !wasCrashed) {
        playOneShot(assets_.explosion, 1.0f);
        audio::mixer().stop(audio::kVoice);
        if (autopilot_) eventShotAt_ = simStep_ + 25;
        ++losses_;
        objective_.playerLost();  // shot down or into the sea: the mission is over
    }
    weaponAudio(s);
    informer_.setLaunchAuthorized(arms_.locked() && arms_.inZone());
    informer_.setMissileWarning(rwr_.missile(), rwr_.missileDir());
    informer_.setEngineFire(fireSide_);
    const audio::Message say = informer_.update(s, kStep);
    if (say != audio::Message::None)
        audio::mixer().play(audio::kVoice, &assets_.voice[static_cast<int>(say)], 1.0f);
    time_ += kStep;

    // --- The end of a mission, a few seconds after the event (kills first:
    // whichever came first has already latched in the objective).
    objective_.update(kStep);
    if (!finished_) {
        sortieTime_ += kStep;
        if (objective_.done()) {
            finished_ = true;
            outcome_ = objective_.phase() == game::Objective::Phase::Won ? meta::SortieOutcome::Success
                                                                          : meta::SortieOutcome::Failed;
        }
    }
}

void FlightScene::update(const platform::Pad& pad, float dt) {
    // Fixed-step simulation: identical results at any frame rate (and on any peer).
    accumulator_ += std::min(dt, 0.1f);
    for (int i = 0; accumulator_ >= kStep && i < kMaxStepsPerFrame; ++i) {
        game::ControlInput in = autopilot_ ? scriptedInput(simStep_) : mapInput(pad);
        stepSimulation(in);
        accumulator_ -= kStep;
        ++simStep_;
        // A mission is not given a second try: its failure screen follows.
        if (model_.state().crashed && (crashTimer_ += kStep) > 2.5f && !autopilot_ && !isMission()) {
            restart();
            break;
        }
    }
    if (accumulator_ > kStep) accumulator_ = 0.0f;  // fell behind: drop time, don't spiral
    updateAudio();
}

void FlightScene::drawAircraft(const game::FlightState& s, const game::ControlSurfaces& surfaces,
                               const fx::AircraftEffects& effects, const gfx::Texture& livery,
                               gfx::NozzleFrame* nozzles, bool distant) {
    // The aircraft with its moving parts; nozzle frames carry the flames.
    const gfx::MeshPose p = poseOf(surfaces);
    for (int side = 0; side < 2; ++side) {
        math::Vec3 pp = s.position;
        math::Quat pq = s.orientation;
        assets_.su35s.partTransform(gfx::kChannelNozzle, side == 0 ? gfx::kSideRight : gfx::kSideLeft, s.position,
                                    s.orientation, &p, pp, pq);
        math::Vec3 exit = effects.points().nozzleExit;
        if (side) exit.x = -exit.x;
        nozzles[side] = {pp + pq.rotate(exit), pq.rotate(math::Vec3{0.0f, 0.0f, 1.0f})};
    }
    if (s.crashed) return;
    if (distant) {
        // A few dozen pixels: no moving parts, one light (docs/decisions/0032).
        gfx::enableDistantLight(env_.sunDir, world::packAbgr(world::scale(env_.key, sunLight_)),
                                world::packAbgr(world::add(env_.ambient, world::scale(env_.fill, 0.7f))));
        livery.bind();
        assets_.su35sLod.draw(s.position, s.orientation);
        return;
    }
    gfx::enableLightRig(lightRig());
    // Afterburner light on the tail booms and stabilators.
    const fx::EngineFx& engine = effects.engine();
    float glow = std::min(engine.afterburner, 1.0f) * (0.85f + 0.15f * engine.flicker);
    gfx::setPointLight((nozzles[0].exit + nozzles[1].exit) * 0.5f + nozzles[0].axis * 1.5f, gfx::abgr(255, 150, 70),
                       glow);
    livery.bind();
    assets_.su35s.draw(s.position, s.orientation, &p);
    gfx::setPointLight({}, 0, 0.0f);
}

void FlightScene::drawStores(const game::FlightState& s, const game::Armament& arms) {
    // Missiles on the pylons, in the aircraft's frame. Up to ten meshes of
    // ~300 triangles - near half the airframe's vertices - for thin tubes a
    // few pixels across from the chase camera: one light (the sun) with the
    // sky folded into the ambient (docs/decisions/0049).
    if (s.crashed) return;
    gfx::enableDistantLight(env_.sunDir, world::packAbgr(world::scale(env_.key, sunLight_)),
                            world::packAbgr(world::add(env_.ambient, world::scale(env_.fill, 0.8f))));
    for (int pass = 0; pass < 2; ++pass) {
        const game::MissileParams* type = pass == 0 ? &game::kR73 : &game::kR77;
        const gfx::Mesh& mesh = pass == 0 ? assets_.r73 : assets_.r77;
        bool bound = false;
        for (int i = 0; i < game::kStationCount; ++i) {
            const game::Station& st = game::kSu35SStations[i];
            if (st.type != type || !arms.loaded(i)) continue;
            if (!bound) (pass == 0 ? assets_.r73Skin : assets_.r77Skin).bind();
            bound = true;
            mesh.draw(s.position + s.orientation.rotate(st.attach), s.orientation);
        }
    }
}

void FlightScene::drawMissiles(const game::Armament& arms, float range) {
    gfx::enableLightRig(lightRig());
    for (int pass = 0; pass < 2; ++pass) {
        const game::MissileParams* type = pass == 0 ? &game::kR73 : &game::kR77;
        const gfx::Mesh& mesh = pass == 0 ? assets_.r73 : assets_.r77;
        bool bound = false;
        for (int i = 0; i < game::Armament::kMaxMissiles; ++i) {
            const game::Missile& m = arms.missile(i);
            if (!m.live() || m.type != type || !gfx::sphereVisible(m.pos, 2.0f, range)) continue;
            if (!bound) (pass == 0 ? assets_.r73Skin : assets_.r77Skin).bind();
            bound = true;
            mesh.draw(m.pos, m.orientation);
        }
    }
    gfx::disableLighting();
}

void FlightScene::drawShips(const math::Vec3& eye, float range) {
    bool rig = false;
    for (int k = 0; k < shipCount_; ++k) {
        const game::Ship& sh = ships_[k];
        if (!sh.afloat()) continue;
        const game::FlightState& st = sh.state();
        const math::Vec3 at{st.position.x, -sh.settle(), st.position.z};  // the mesh's origin is its waterline
        if (!gfx::sphereVisible(at, 30.0f, range)) continue;
        if (!rig) {
            gfx::enableLightRig(lightRig());
            assets_.shipSkin.bind();
            rig = true;
        }
        const bool far = math::length(at - eye) > 1500.0f;
        (far ? assets_.shipLod : assets_.ship).draw(at, st.orientation);
    }
    gfx::disableLighting();
}

void FlightScene::drawRocks(const math::Vec3& eye, float range) {
    // Chunked levels of detail (docs/decisions/0042): each mesh picks its
    // chunks by projected error. Rock is most of the vertices in view: 16
    // bytes each, the light already in their colour (bakeRocks, 0050).
    // Nearest first, so the GE's depth test throws away what they hide.
    if (serialFrame_) gfx::profileSync();  // the sea before us is the sea's
    const double t0 = platform::now();
    constexpr float kTolerancePx = 10.0f;
    const gfx::TerrainView view{gfx::kScreenH * 0.5f / gfx::camera().tanHalfY, kTolerancePx, range};
    int order[world::Terrain::kMaxRocks];
    float dist[world::Terrain::kMaxRocks];
    int n = 0;
    for (int i = 0; i < terrain_.count(); ++i) {
        const world::Rock& r = terrain_.rock(i);
        const world::RockShape& sh = terrain_.shape(i);
        const math::Vec3 centre{r.x, sh.apex * 0.45f, r.z};
        const float radius = std::max(sh.maxRadius, sh.apex * 0.6f);
        if (!gfx::sphereVisible(centre, radius, range + radius)) continue;
        const math::Vec3 rel = centre - eye;
        const float d = math::dot(rel, rel);
        int k = n++;
        while (k > 0 && dist[k - 1] > d) {
            dist[k] = dist[k - 1];
            order[k] = order[k - 1];
            --k;
        }
        dist[k] = d;
        order[k] = i;
    }
    rockStats_ = {};
    if (n == 0) {
        rocksMs_ += static_cast<float>((platform::now() - t0) * 1000.0);
        return;
    }
    gfx::TerrainMesh::beginBatch();
    const gfx::Texture* bound = nullptr;
    for (int k = 0; k < n; ++k) {
        const world::Rock& r = terrain_.rock(order[k]);
        assets_.rocks[r.shape].draw(rockInstance_[order[k]], {r.x, 0.0f, r.z}, r.rot, r.cosYaw, r.sinYaw,
                                    assets_.rockMaterials[r.shape], view, &bound, &rockStats_);
    }
    gfx::TerrainMesh::endBatch();
    if (serialFrame_) gfx::profileSync();
    rocksMs_ += static_cast<float>((platform::now() - t0) * 1000.0);
}

void FlightScene::drawSurf(const math::Vec3& eye) {
    // Surf at the foot of every rock in the near sea: a ring of foam along the
    // real waterline, white at the rock and fading outwards, crawling out as
    // the swell breaks on it. Heavier seas throw a wider, brighter band.
    constexpr float kRange = 2500.0f;
    const float width = 5.0f + 3.0f * env_.waveHeight;
    const float strength = math::clamp(0.45f + 0.9f * env_.whitecaps + 0.08f * env_.waveHeight, 0.0f, 1.0f);
    for (int i = 0; i < terrain_.count(); ++i) {
        const world::Rock& r = terrain_.rock(i);
        const world::RockShape& sh = terrain_.shape(i);
        const math::Vec3 c{r.x, 0.0f, r.z};
        if (math::length(c - eye) > kRange + sh.maxRadius || !gfx::sphereVisible(c, sh.maxRadius + width, kRange))
            continue;
        constexpr int kMaxSegments = 384;  // a big island's whole coastline
        float radii[kMaxSegments];
        const int n = std::min(sh.sectors, kMaxSegments);
        for (int k = 0; k < n; ++k) radii[k] = terrain_.waterlineRadius(i, 6.2831853f * k / n);
        gfx::drawSurfRing(c, n, radii, width, strength, time_, assets_.seaFoam);
    }
}

void FlightScene::render() {
    const game::CameraPose& cam = camera_.pose();
    const game::FlightState& s = model_.state();
    const game::FlightState& d = bot_.state();
    const math::Vec3 sun = env_.sunDir;
    const gfx::FxTextures fx = assets_.fx();

    // The light and the haze where the camera is: under, inside or above the
    // overcast deck (docs/decisions/0041).
    const float alt = cam.eye.y;
    sunLight_ = env_.directLight(alt);
    world::Rgb hazeRgb;
    float fogNear, fogFar;
    env_.hazeAt(alt, hazeRgb, fogNear, fogFar);
    clearColor_ = world::packAbgr(hazeRgb);
    const bool aboveDeck = deck_.above(alt);
    const bool inDeck = env_.deck && !aboveDeck && !deck_.below(alt);

    // Near plane at 3 m, not 1: a vertex clipped to z = near lands at
    // lateral/near on screen, and past the GE's 4096-px virtual screen the
    // whole triangle is dropped (docs/psp/gotchas.md #19). The sea's LOD rings
    // keep every such triangle off screen (docs/decisions/0040); the aircraft
    // is never closer than ~15 m.
    gfx::setProjection(cam.fovDeg, 3.0f, 12000.0f);
    gfx::setCamera(cam.eye, cam.target, cam.up);
    const gfx::CameraBasis& basis = gfx::camera();
    if (!deck_.below(alt)) sky_.draw(cam.eye, basis.right, basis.up, assets_.sun, sunLight_);

    gfx::enableFog(fogNear, fogFar, world::packAbgr(hazeRgb));
    // Above the deck (or inside a cloud deck) the sea is hidden: don't draw
    // it. Inside a fog bank the sea and the rocks are what you see; above it
    // the peaks stand out of it (drawn before the layer, which covers the
    // rest of them by depth).
    const bool hideSea = aboveDeck || (inDeck && !env_.deckFog);
    // CPU time per part of the list (debug corner): which part to optimise.
    // On a serial (profiling) frame each part also waits for the GE.
    serialFrame_ = profiling_ && (++profileCounter_ & 63) == 0;
    if (serialFrame_) gfx::profileSync();
    rocksMs_ = 0.0f;
    const double tSea = platform::now();
    if (aboveDeck && terrain_.maxHeight() > env_.deckTop) drawRocks(cam.eye, fogFar);
    if (!hideSea) {
        // Under a deck the sea fades into a darker haze than the sky: the
        // horizon stays a line (docs/decisions/0048).
        const bool underDeck = deck_.below(alt) && !env_.deckFog;
        if (underDeck) gfx::enableFog(fogNear, fogFar, world::packAbgr(env_.seaHaze));
        ocean_.draw(cam.eye, assets_.seaDetail, assets_.seaFoam, time_, sunLight_);
        ocean_.drawGlint(cam.eye, assets_.glint, time_, sunLight_);
        if (underDeck) gfx::enableFog(fogNear, fogFar, world::packAbgr(hazeRgb));
        // Shadows on the water, before anything stands over them (no depth test).
        if (!s.crashed) gfx::drawShadow(s.position, s.orientation, sun, fx, sunLight_);
        if (enemyEnabled() && !d.crashed) gfx::drawShadow(d.position, d.orientation, sun, fx, sunLight_);
        // White water and rings on the sea, under everything that stands on it.
        gfx::drawWaterMarks(effects_.waterMarks(), assets_.seaFoam, fx);
        gfx::drawWaterMarks(droneFx_.waterMarks(), assets_.seaFoam, fx);
        gfx::drawWaterMarks(weaponFx_.waterMarks(), assets_.seaFoam, fx);
        gfx::drawWaterMarks(botWeaponFx_.waterMarks(), assets_.seaFoam, fx);
        for (int k = 0; k < shipCount_; ++k) {
            gfx::drawWaterMarks(shipFx_[k].waterMarks(), assets_.seaFoam, fx);
            gfx::drawWaterMarks(shipWeaponFx_[k].waterMarks(), assets_.seaFoam, fx);
        }
        drawRocks(cam.eye, fogFar);
        drawSurf(cam.eye);
        drawShips(cam.eye, fogFar);
    }
    if (serialFrame_) gfx::profileSync();
    const double tAircraft = platform::now();

    // --- Aircraft: the player with the stores, the drone in the other livery.
    gfx::NozzleFrame nozzles[2], droneNozzles[2];
    drawAircraft(s, surfaces_, effects_, assets_.liveries[liveryIndex()], nozzles, false);
    drawStores(s, arms_);
    gfx::disableLighting();
    // The drone only when it can be on screen (fog hides it past fogFar), and
    // as the light copy unless it's right next to us: a second full Su-35S
    // cost a third of the frame on the PSP (docs/decisions/0032).
    const float droneDist = math::length(d.position - cam.eye);
    const bool droneVisible = enemyEnabled() && gfx::sphereVisible(d.position, 12.0f, fogFar);
    if (droneVisible) {
        drawAircraft(d, bot_.surfaces(), droneFx_, assets_.liveries[(liveryIndex() + 1) % kLiveryCount],
                     droneNozzles, droneDist > 120.0f);
        // Its missiles on the pylons only where they're more than a few pixels.
        if (isCombat(bot_.mode()) && droneDist < 400.0f) {
            drawStores(d, bot_.arms());
        }
    }
    gfx::disableLighting();
    drawMissiles(arms_, fogFar);
    drawMissiles(bot_.arms(), fogFar);

    if (serialFrame_) gfx::profileSync();
    const double tClouds = platform::now();
    deck_.draw(cam.eye, {&assets_.deck, &assets_.deckBase, &assets_.deckStorm, &assets_.deckScud, &assets_.glow}, time_);
    clouds_.draw(cam, assets_.cloud);
    if (serialFrame_) gfx::profileSync();
    const double tFx = platform::now();

    // --- Effects: alpha-blended first, additive last.
    constexpr int kFlares = game::Countermeasures::kMaxFlares;
    const fx::Trail* trails[2 * (4 + kFlares) + 2 * fx::WeaponEffects::kSmokeTrails + 2];
    int trailCount = 0;
    for (int side = 0; side < 2; ++side) {
        trails[trailCount++] = &effects_.wingtipTrail(side);
        trails[trailCount++] = &effects_.contrail(side);
        trails[trailCount++] = &droneFx_.wingtipTrail(side);
        trails[trailCount++] = &droneFx_.contrail(side);
    }
    for (int i = 0; i < kFlares; ++i) {
        trails[trailCount++] = &effects_.flareTrail(i);
        trails[trailCount++] = &droneFx_.flareTrail(i);
    }
    for (int i = 0; i < fx::WeaponEffects::kSmokeTrails; ++i) {
        trails[trailCount++] = &weaponFx_.smoke(i);
        trails[trailCount++] = &botWeaponFx_.smoke(i);
    }
    trails[trailCount++] = &weaponFx_.damageSmoke();
    trails[trailCount++] = &botWeaponFx_.damageSmoke();
    gfx::drawTrails(trails, trailCount, fx);
    for (int k = 0; k < shipCount_; ++k) {
        const fx::Trail* st[fx::WeaponEffects::kSmokeTrails + 1];
        int n = 0;
        st[n++] = &shipFx_[k].smoke();
        for (int i = 0; i < fx::WeaponEffects::kSmokeTrails; ++i) st[n++] = &shipWeaponFx_[k].smoke(i);
        gfx::drawTrails(st, n, fx);
        gfx::drawParticles(shipFx_[k].particles(), fx);
        gfx::drawParticles(shipWeaponFx_[k].particles(), fx);
    }
    gfx::drawParticles(effects_.particles(), fx);
    gfx::drawParticles(droneFx_.particles(), fx);
    gfx::drawParticles(weaponFx_.particles(), fx);
    gfx::drawParticles(botWeaponFx_.particles(), fx);
    if (!s.crashed) {
        gfx::drawLerxVapor(effects_, s.position, s.orientation, fx);
        gfx::drawTransonicVapor(effects_, s.position, s.orientation, fx);
        gfx::drawEngineFlames(effects_.engine(), nozzles, fx);
    }
    if (!d.crashed && droneVisible) {
        if (droneDist < 800.0f) gfx::drawLerxVapor(droneFx_, d.position, d.orientation, fx);
        if (droneDist < 1500.0f) gfx::drawEngineFlames(droneFx_.engine(), droneNozzles, fx);
    }
    gfx::drawFlares(flares_, time_, fx);
    gfx::drawFlares(bot_.flares(), time_, fx);
    gfx::drawMissileMotors(arms_.missiles(), game::Armament::kMaxMissiles, fx);
    gfx::drawMissileMotors(bot_.arms().missiles(), game::Armament::kMaxMissiles, fx);
    for (int k = 0; k < shipCount_; ++k) {
        gfx::drawMissileMotors(ships_[k].missiles(), game::Ship::kMaxMissiles, fx);
        gfx::drawTracers(ships_[k].gun(), fx);
    }
    gfx::drawTracers(arms_.gun(), fx);
    gfx::drawTracers(bot_.arms().gun(), fx);
    if (!s.crashed)
        gfx::drawMuzzleFlash(s.position + s.orientation.rotate(arms_.gun().params().muzzle), s.orientation.forward(),
                             weaponFx_.muzzleFlash(), fx);
    gfx::drawSpeedStreaks(effects_, s.pathDir, s.speed, fx);
    if (precip_.count() > 0) {
        // Rain is grey streaks in the grey light; snow white flakes.
        const bool snow = precip_.snow() > precip_.rain();
        const world::Rgb lit = world::scale(hazeRgb, 1.0f + 0.6f * flash_);
        const uint32_t tint = snow ? world::packAbgr(world::mix(lit, {255, 255, 255}, 0.5f), 200.0f)
                                   : world::packAbgr(world::scale(lit, 1.5f), 170.0f);
        gfx::drawPrecipitation(precip_, s.velocity(), tint, fx);
    }
    gfx::disableFog();

    // --- Screen space: lens flare, whiteout inside a cloud, HUD.
    // The flare needs a sun to look at: not under the deck, not at night.
    const float flare = env_.sunDisc * sunLight_ * (env_.time == world::TimeOfDay::Night ? 0.0f : 1.0f);
    sunOnScreen_ = flare > 0.3f && gfx::projectDirection(sun, sunX_, sunY_) && sunX_ >= 0.0f &&
                   sunX_ < gfx::kScreenW && sunY_ >= 0.0f && sunY_ < gfx::kScreenH;
    if (sunOnScreen_) gfx::drawLensFlare(sunX_, sunY_, sunVisibility_ * flare, fx);
    // Inside a cloud: white (tinted by the deck's own grey inside it).
    const float deckIn = env_.deckImmersion(alt);
    const float puffIn = clouds_.immersion() * 0.85f;
    if (deckIn > puffIn)
        gfx::drawScreenTint(world::packAbgr(env_.deckInside, deckIn * (env_.deckFog ? 0.2f : 0.75f) * 255.0f));
    else
        gfx::drawScreenTint(gfx::abgr(240, 244, 248, static_cast<unsigned>(puffIn * 255.0f)));
    // Lightning lights the clouds from inside: a cold flash over everything below the tops.
    if (flash_ > 0.0f && !aboveDeck) {
        const float f = flash_ * (0.75f + 0.25f * std::sin(time_ * 90.0f));
        gfx::drawScreenTint(gfx::abgr(220, 228, 255, static_cast<unsigned>(f * 0.32f * 255.0f)));
    }
    gfx::drawScreenTint(gfx::abgr(220, 30, 20, static_cast<unsigned>(hitFlash_ * 0.35f * 255.0f)));  // we're hit
    CombatView view;
    view.radar = &radar_;
    view.arms = &arms_;
    view.hasEnemy = enemyEnabled();
    view.targetName = isCombat(bot_.mode()) ? "BANDIT" : "DRONE";
    for (int k = 0; k < shipCount_ && k + 1 < game::kMaxTargets; ++k) view.targetNames[k + 1] = ships_[k].params().name;
    view.targetMode = game::botModeName(bot_.mode());
    view.targetHealth = bot_.alive() ? bot_.health() : 0.0f;
    view.objectiveTotal = objective_.targets();
    view.objectiveDone = std::min(objective_.destroyed(), objective_.targets());
    switch (objective_.phase()) {
        case game::Objective::Phase::Won: view.banner = "MISSION COMPLETE"; view.bannerColor = hud::kGreen; break;
        case game::Objective::Phase::Lost: view.banner = "MISSION FAILED"; view.bannerColor = hud::kRed; break;
        default: break;
    }
    view.hits = kills_;
    view.kills = deaths_;
    view.rwr = &rwr_;
    view.terrain = &terrain_;
    view.area = static_cast<int>(boundary_.zone());
    view.areaCountdown = boundary_.countdown();
    view.homeBearing = boundary_.homeBearing();
    view.health = health_;
    view.message = message_;
    view.messageColor = messageColor_;
    view.messageAge = messageAge_;
    if (serialFrame_) gfx::profileSync();
    const double tHud = platform::now();
    gfx::HudCanvas& hud = gfx::hudCanvas();
    drawFlightHud(hud, s, flares_, lastInput_.psm && !s.psm, time_, view);
    hud.flush(assets_.line, &ui::res().hudFont, &ui::res().hudPlates);
    if (serialFrame_) gfx::profileSync();
    const double tEnd = platform::now();
    if (serialFrame_) {
        serial_.rocks = rocksMs_;
        serial_.sea = static_cast<float>((tAircraft - tSea) * 1000.0) - rocksMs_;
        serial_.aircraft = static_cast<float>((tClouds - tAircraft) * 1000.0);
        serial_.clouds = static_cast<float>((tFx - tClouds) * 1000.0);
        serial_.fx = static_cast<float>((tHud - tFx) * 1000.0);
        serial_.hud = static_cast<float>((tEnd - tHud) * 1000.0);
        serial_.cloudQuads = clouds_.drawn();
        return;  // keep the CPU-only averages clean
    }
    auto avg = [](float& v, double ms) { v += (static_cast<float>(ms) - v) * 0.1f; };
    avg(times_.rocks, rocksMs_);
    avg(times_.sea, (tAircraft - tSea) * 1000.0 - rocksMs_);
    avg(times_.aircraft, (tClouds - tAircraft) * 1000.0);
    avg(times_.clouds, (tFx - tClouds) * 1000.0);
    avg(times_.fx, (tHud - tFx) * 1000.0);
    avg(times_.hud, (tEnd - tHud) * 1000.0);
    times_.cloudQuads = clouds_.drawn();
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
