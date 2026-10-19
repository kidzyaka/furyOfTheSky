#pragma once

#include "audio/flight_audio.h"
#include "audio/voice_informer.h"
#include "fx/aircraft_fx.h"
#include "fx/precipitation.h"
#include "fx/ship_fx.h"
#include "fx/weapon_fx.h"
#include "game/armament.h"
#include "game/chase_camera.h"
#include "game/control_surfaces.h"
#include "game/countermeasures.h"
#include "game/flight_model.h"
#include "game/radar.h"
#include "game/bot.h"
#include "game/boundary.h"
#include "game/ship.h"
#include "game/objective.h"
#include "game/rwr.h"
#include "meta/profile.h"
#include "meta/sortie.h"
#include "render/cloud_deck.h"
#include "render/clouds.h"
#include "render/ocean.h"
#include "render/sky.h"
#include "render/view.h"
#include "scenes/scene.h"
#include "world/environment.h"
#include "world/missions.h"
#include "world/terrain.h"

namespace gfx {
struct NozzleFrame;
}

namespace scenes {

// A sortie: free flight over an endless sea under a cumulus field, with the
// full effects set, the HUD, the weapons, the RWR and - by the sortie's mode -
// no enemy, a training target, a fighting bot of three skill levels
// (docs/decisions/0033) or a mission to destroy a number of them
// (docs/decisions/0038). The menus configure it before enter() and poll
// finished() / result() for the way out.
class FlightScene : public Scene {
public:
    explicit FlightScene(Assets& assets);

    const char* name() const override { return "Flight test"; }
    const gfx::TerrainStats& rockStats() const { return rockStats_; }
    // CPU ms building the frame's list, by part (smoothed): sea (with the
    // surf, marks, ships), rocks, aircraft, clouds (with the deck), effects,
    // HUD - and the cloud quads drawn.
    struct FrameTimes {
        float sea = 0.0f, rocks = 0.0f, aircraft = 0.0f, clouds = 0.0f, fx = 0.0f, hud = 0.0f;
        int cloudQuads = 0;
    };
    const FrameTimes& frameTimes() const { return times_; }
    // The same parts, CPU and GE together: every 64th frame while profiling
    // is on (the debug corner), the frame is drawn part by part, waiting for
    // the GE after each (gfx::profileSync). Last such frame's ms.
    void setProfiling(bool on) { profiling_ = on; }
    const FrameTimes& serialTimes() const { return serial_; }
    void enter() override;
    void leave() override;
    void update(const platform::Pad& pad, float dt) override;
    void render() override;
    void overlay(void* drawBuffer) override;

    // What to fly (mode, enemy, livery, loadout, mission) and the player's
    // settings (pitch inversion, volumes, camera shake). Call before enter();
    // it may be called again while flying - a changed enemy, livery or
    // setting takes over at once (the loadout and the mode at the next
    // restart()). Applies the volumes to the mixer.
    void configure(const meta::SortieConfig& sortie, const meta::Settings& settings);
    // Start the flight over (a fresh spawn, full ammunition, the loadout of
    // the sortie) keeping the config and the sortie's tally. A new sortie is
    // leave() + enter(), which also zeroes the tally and the clock.
    void restart();
    // The sortie is over: a mission won or failed (a few seconds after the
    // last enemy goes down / we do), or abort(). Never true on the test
    // range unless abort() is called. The scene keeps running meanwhile, so
    // the picture doesn't freeze while the caller switches over.
    bool finished() const { return finished_; }
    // Outcome and tally so far (a sortie still flying reads as Aborted).
    meta::SortieResult result() const;
    // Pause-menu QUIT: ends the sortie as Aborted, keeping the kills so far.
    void abort();
    // Scripted sortie instead of the pad (emulator smoke test, docs/psp/emulator.md).
    void setAutopilot(bool on) { autopilot_ = on; }
    bool takeShotRequest() {
        bool r = shotRequest_;
        shotRequest_ = false;
        return r;
    }
    bool scriptDone() const { return autopilot_ && simStep_ >= kScriptEnd; }
    // In flight the frame is cleared to the haze where the camera is (under,
    // inside or above a cloud deck): the sky below the horizon melts into it.
    uint32_t clearColor() const { return clearColor_; }
    // Weather and time of day of this sortie (docs/decisions/0041).
    const world::Environment& environment() const { return env_; }
    // The enemy and the tally, shown in the pause menu.
    const game::Bot& bot() const { return bot_; }
    int deaths() const { return deaths_; }
    int kills() const { return kills_; }

private:
    static constexpr float kStep = 1.0f / 60.0f;  // fixed simulation step
    static constexpr int kMaxStepsPerFrame = 5;    // spiral-of-death guard
    static constexpr int kScriptEnd = 3330;

    game::ControlInput mapInput(const platform::Pad& pad) const;
    game::ControlInput scriptedInput(int step);
    void stepSimulation(const game::ControlInput& in);
    // heading: rad, 0 = -Z, + = towards +X.
    void restartAt(const math::Vec3& pos, float speed, float heading = 0.0f);
    // The mission's ships on their routes (docs/decisions/0045): `count` of the layout's.
    void spawnShips(const world::MissionLayout& layout, int count);
    void drawShips(const math::Vec3& eye, float range);
    const world::MissionLayout& layout() const;
    void respawnBot();
    void applyVolumes();
    // Is there an enemy at all (free flight has none; the smoke-test script always does).
    bool enemyEnabled() const { return autopilot_ || sortie_.mode != meta::SortieMode::FreeFlight; }
    bool isMission() const { return sortie_.mode == meta::SortieMode::Mission && !autopilot_; }
    game::BotMode enemyMode() const;
    int liveryIndex() const;
    void damage(const game::HitEvent& e);
    void startLoops();
    void updateAudio();
    void weaponAudio(const game::FlightState& s);
    void playOneShot(const audio::Sound& sound, float gain, float pan = 0.0f);
    // Sound that reaches us later: a blast 2 km away is heard ~6 s after it's seen.
    void playDelayed(const audio::Sound& sound, float delay, float gain, float pan);
    void showMessage(const char* text, uint32_t color);
    // Sky, sea, clouds and light for a weather and a time of day.
    void applyEnvironment(world::Weather weather, world::TimeOfDay time);
    void updateWeather(const math::Vec3& eye);
    // The aircraft light rig in the current light (sun hidden under a deck).
    gfx::LightRig lightRig() const;
    // `distant`: the ~1500-triangle copy with the cheap light rig (docs/decisions/0032).
    void drawAircraft(const game::FlightState& s, const game::ControlSurfaces& surfaces,
                      const fx::AircraftEffects& effects, const gfx::Texture& livery, gfx::NozzleFrame* nozzles,
                      bool distant);
    void drawStores(const game::FlightState& s, const game::Armament& arms);
    void drawMissiles(const game::Armament& arms, float range);
    // Rocks in range: full meshes near, the distant copies further (docs/decisions/0042).
    gfx::TerrainStats rockStats_;
    FrameTimes times_, serial_;
    float rocksMs_ = 0.0f;
    bool profiling_ = false, serialFrame_ = false;
    int profileCounter_ = 0;  // last frame: chunks, triangles, clipped (for the debug corner)
    // The sortie's light into the rocks' vertex colours (docs/decisions/0050).
    void bakeRocks();
    uint8_t rockInstance_[world::Terrain::kMaxRocks] = {};  // which placement of its shape each rock is
    void drawRocks(const math::Vec3& eye, float range);
    void drawSurf(const math::Vec3& eye);

    Assets& assets_;
    meta::SortieConfig sortie_;
    meta::Settings settings_;
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
    gfx::CloudDeck deck_;
    world::Environment env_;
    world::Terrain terrain_;       // the map's rocks (docs/decisions/0042)
    world::Arena arena_;
    game::Boundary boundary_;      // the mission-area edge (docs/decisions/0043)
    // Ground targets (docs/decisions/0045): target slots 1..shipCount_ after the bot's 0.
    static constexpr int kMaxShips = 3;
    game::Ship ships_[kMaxShips];
    fx::ShipEffects shipFx_[kMaxShips];
    fx::WeaponEffects shipWeaponFx_[kMaxShips];
    int shipCount_ = 0;
    int groundKills_ = 0;
    bool envReady_ = false;
    float sunLight_ = 1.0f;        // direct sun where the camera is (0.22 under a deck)
    uint32_t clearColor_ = 0xFFDCCCBAu;
    fx::Precipitation precip_;     // rain / snow around the camera
    float flash_ = 0.0f;           // lightning: 1 at a stroke, decays in a fraction of a second
    float nextStroke_ = 8.0f;      // s to the next one
    uint32_t stormRng_ = 0x5707Au;

    // Weapons, sensors and the target (docs/decisions/0026-0029).
    game::Radar radar_;
    game::Armament arms_;
    game::Bot bot_;
    game::HitQueue hits_;      // ours on the enemy
    game::HitQueue hitsOnMe_;  // the enemy's on us
    game::Rwr rwr_{true};      // L150 RWR + the IR missile approach warners (docs/decisions/0034)
    fx::AircraftEffects droneFx_;
    fx::WeaponEffects weaponFx_, botWeaponFx_;
    float health_ = 100.0f;    // ours: 30 mm and warheads take it, at 0 we're shot down
    float hitFlash_ = 0.0f;    // red screen flash when hit
    int fireSide_ = 0;         // engine fire after heavy damage (-1 left, +1 right)
    int deaths_ = 0, kills_ = 0;  // times shot down by the enemy / enemies destroyed
    int losses_ = 0;              // every time our aircraft was lost, the sea included
    float sortieTime_ = 0.0f;     // s flown in this sortie (restart() doesn't reset it)
    game::Objective objective_;   // the mission: how many enemies, how it ends
    bool enemyHeld_ = false;      // the last mission enemy is down: no more coming
    bool finished_ = false;
    meta::SortieOutcome outcome_ = meta::SortieOutcome::Aborted;
    int rwrSound_ = 0;         // 0 none, 1 lock, 2 missile
    const char* message_ = nullptr;  // HIT / MISS / DESTROYED
    uint32_t messageColor_ = 0;
    float messageAge_ = 99.0f;
    struct Delayed {
        const audio::Sound* sound;
        float wait, gain, pan;
    };
    static constexpr int kMaxDelayed = 8;
    Delayed delayed_[kMaxDelayed];
    int delayedCount_ = 0;
    bool gunSounding_ = false;
    int tone_ = 0;  // 0 none, 1 seeker searching, 2 locked

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
    int hitShotAt_ = -1;    // script: a shot just after the first missile hit
    int rwrShotAt_ = -1;    // script: the enemy's radar lock on the RWR
    int missileShotAt_ = -1;  // script: its missile coming
    int simStep_ = 0;
};

}  // namespace scenes
