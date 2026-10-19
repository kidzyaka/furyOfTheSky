#pragma once

#include <cstdint>

#include "game/armament.h"
#include "game/autopilot.h"
#include "game/boundary.h"
#include "game/combat.h"
#include "game/control_surfaces.h"
#include "game/countermeasures.h"
#include "game/flight_model.h"
#include "game/radar.h"

namespace game {

// What the enemy aircraft does (pause menu "ENEMY").
enum class BotMode : uint8_t {
    // Training targets - unarmed, they never shoot back.
    Straight,  // level, constant heading
    Orbit,     // steady ~3.5 g circle
    Weave,     // S-turns with height changes
    Evade,     // weaves; beams missiles, dives, pops flares
    // Opponents - fly and fight with the same aircraft, sensors and weapons.
    Easy,
    Medium,
    Hard,
    Count
};
const char* botModeName(BotMode mode);
inline bool isCombat(BotMode m) { return m >= BotMode::Easy; }

// How well a bot pilot flies and fights (docs/decisions/0033).
struct BotSkill {
    float reaction;        // s a new threat has to persist before it reacts
    float maxG;            // G it is willing to pull
    bool limiterOff;       // uses the limiter-off snap / 13 g override (Su-35 trick)
    float mawsRange;       // m: notices a missile coming at it
    float aimError;        // rad: gun aim scatter (per burst)
    float gunRange;        // m: opens fire inside it
    float gunCone;         // rad: fires when the rounds' line is this close to the lead point
    float r73Cone;         // rad off its nose it takes R-73 shots
    bool r77;              // uses R-77 beyond visual range
    float launchInterval;  // s between its launches
    int maxInFlight;       // own missiles in the air at once
    bool notch;            // beams radar missiles and dives into the clutter (else plain break)
    bool crank;            // turns off after an R-77 shot, keeping the target in the radar
    bool reversals;        // split-S / reversals / scissors when an enemy is on its tail
    bool leadPursuit;      // lead/lag pursuit (else pure pursuit)
    float cornerSpeed;     // m/s it tries to fight at
};
const BotSkill& botSkill(BotMode mode);

// Who the bot fights: the other aircraft and what it has in the air.
struct Opponent {
    const FlightState* state = nullptr;
    const Countermeasures* flares = nullptr;
    const Missile* missiles = nullptr;  // its missiles in the air (the bot's warning of them)
    int missileCount = 0;
};

// The enemy aircraft (docs/decisions/0033, replacing the immortal drone of
// 0029): a Su-35S on the player's flight model, flown through the autopilot
// by a pilot of a given skill, with its own radar and weapons. It can be shot
// down: it explodes, and a few seconds later a new one comes.
class Bot {
public:
    static constexpr float kRespawnDelay = 4.0f;    // s after it is shot down
    static constexpr float kLeash = 16000.0f;       // m: further than this, it comes back

    Bot() : model_(kSu35S) {}

    void reset(const FlightState& player, BotMode mode);
    // The map's rocks (docs/decisions/0042): it flies round them, its radar
    // and missiles can't see through them, and it dies if it hits one.
    void setTerrain(const ::world::Terrain* terrain) { terrain_ = terrain; }
    // The mission area: past its edge the bot is turned back like the player (docs/decisions/0043).
    void setArena(const ::world::Arena* arena) { arena_ = arena; }
    // A low-level raider (docs/decisions/0046): comes in at 150-280 m under the
    // radar horizon and only pulls up 70 m over the sea or a rock.
    void setLowLevel(bool on) { lowLevel_ = on; }
    // Hits it scores on the opponent go to `hits` (target index 0).
    void update(const Opponent& enemy, HitQueue& hits, float dt);
    void hit(const HitEvent& e);

    BotMode mode() const { return mode_; }
    bool alive() const { return !dead_; }
    const FlightState& state() const { return model_.state(); }
    const Countermeasures& flares() const { return flares_; }
    const ControlSurfaces& surfaces() const { return surfaces_; }
    const Radar& radar() const { return radar_; }
    const Armament& arms() const { return arms_; }
    float health() const { return health_; }
    int hits() const { return hits_; }
    int kills() const { return kills_; }  // times it has been shot down
    const ControlInput& input() const { return in_; }
    // What its pilot is doing (debug / tests): see plan().
    const char* intent() const { return intent_; }

    // Events (true once): shot down / reappeared this step.
    bool takeDestroyed() {
        bool d = destroyed_;
        destroyed_ = false;
        return d;
    }
    bool takeRespawned() {
        bool r = respawned_;
        respawned_ = false;
        return r;
    }

private:
    void spawn(const FlightState& player);
    PilotDemand training(const Opponent& enemy);
    PilotDemand combat(const Opponent& enemy, ControlInput& weapons, float dt);
    float random();  // -1..1, deterministic
    // Highest rock top (plus clearance) under the next few seconds of flight;
    // 0 over open sea. Looked up every kFloorPeriod (it looks 5 s ahead - a
    // tenth of a second stale is nothing; 27 terrain queries a step were).
    float terrainFloor(const FlightState&) const { return floor_; }
    float lookupTerrainFloor(const FlightState& s) const;
    static constexpr float kFloorPeriod = 0.1f;  // s
    float floor_ = 0.0f, floorTimer_ = 0.0f;

    FlightModel model_;
    ControlSurfaces surfaces_;
    Countermeasures flares_;
    Radar radar_;
    Armament arms_;
    ControlInput in_;
    BotMode mode_ = BotMode::Weave;
    math::Vec3 heading_{0.0f, 0.0f, -1.0f};
    math::Vec3 centre_;
    float altitude_ = 1500.0f;
    float time_ = 0.0f;
    float health_ = 100.0f;
    int hits_ = 0, kills_ = 0;
    bool dead_ = false, destroyed_ = false, respawned_ = false;
    float deadTimer_ = 0.0f;
    bool wantFlares_ = false;
    int breakSide_ = 1;
    // Combat pilot state.
    float threatTime_ = 0.0f;   // s the current missile threat has been seen
    float threatHold_ = 0.0f;   // s it keeps defending after losing sight of the missile
    math::Vec3 threatLos_;      // last known direction to it
    float threatRange_ = 0.0f;
    bool threatRadar_ = false;  // a radar missile (notch it) or IR (flares)
    float tailTime_ = 0.0f;     // s the enemy has been on its tail
    float launchTimer_ = 0.0f;  // s since its last launch
    float burst_ = 0.0f;        // s left of the current gun burst
    math::Vec3 aimOffset_;      // this burst's aim error
    float manoeuvre_ = 0.0f;    // s left of a committed manoeuvre (split-S)
    math::Vec3 manoeuvreDir_;
    float crank_ = 0.0f;        // s left of cranking after an R-77 shot
    uint32_t rng_ = 0xB07B07u;
    const ::world::Terrain* terrain_ = nullptr;
    const ::world::Arena* arena_ = nullptr;
    bool lowLevel_ = false;
    Boundary boundary_;
    const char* intent_ = "";
};

}  // namespace game
