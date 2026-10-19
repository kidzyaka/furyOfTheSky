#pragma once

#include <cstdint>

#include "game/aircraft_params.h"
#include "game/combat.h"
#include "game/controls.h"
#include "game/gun.h"
#include "game/missile.h"
#include "game/radar.h"

namespace game {

// One hardpoint with a missile hanging on it.
struct Station {
    math::Vec3 attach;           // body axes: the missile's mid-length point when loaded
    const MissileParams* type;
    Launcher launcher;
};

// The Su-35S air-to-air load (docs/decisions/0026): R-73 on the wingtip and
// outer wing rails, R-77 on AKU-170 ejectors under the wings and the intakes.
// Right side first, the left mirrors x. Must match tools/models/su35s.py.
constexpr int kStationCount = 10;
extern const Station kSu35SStations[kStationCount];
// Bit i = station i carries a missile (the loadout, docs/decisions/0038).
constexpr uint16_t kAllStations = (1u << kStationCount) - 1;

enum class MissileSlot : uint8_t { R77, R73, Count };
constexpr int kMissileSlots = static_cast<int>(MissileSlot::Count);

// What happened this step, for effects, sound and the HUD.
struct WeaponEvents {
    static constexpr int kMax = 8;
    int launches = 0;
    int launchStation[kMax];   // station index of each launch this step
    int ends = 0;
    MissileResult end[kMax];   // missiles that detonated / self-destructed
    bool reloaded = false;     // a station got a fresh missile (pylon "clunk")
};

// Any shooter's missiles in the air, this step's events and its gun (an
// aircraft's Armament, a ship's air defence, docs/decisions/0045): what the
// effects, the sound and the RWR read.
struct WeaponView {
    const Missile* missiles = nullptr;
    int missileCount = 0;
    const WeaponEvents* events = nullptr;
    const Gun* gun = nullptr;
};

// What hangs on the pylons as the flight model feels it (docs/decisions/0038):
// mass and zero-lift drag relative to the full load the aircraft is tuned for.
struct StoresRatios {
    float mass = 1.0f;
    float drag = 1.0f;
};

// Everything the player shoots with: stations, missiles in flight, the gun,
// the IR seeker head of the selected R-73, and the launch zone.
// Ace Combat convention on the test range: a fired station is reloaded from
// the reserve a few seconds later, the magazine of the gun too - the numbers
// on the panel are the real ones, but the range never runs dry.
class Armament {
public:
    static constexpr int kMaxMissiles = 12;
    static constexpr float kReloadTime = 4.0f;     // s per station
    static constexpr float kGunReload = 4.0f;      // s after the magazine runs dry
    static constexpr float kZoneStep = 0.1f;       // s, launch-zone prediction step
    static constexpr int kZoneStepsPerTick = 25;   // prediction work per sim step (default)

    // `range`: the Ace Combat reloads (the player); without them a sortie has
    // the missiles on the rails and one 150-round magazine (bots).
    // `stationMask`: the loadout - bit i = station i starts loaded. The other
    // pylons stay empty for the whole sortie: the reloads fill only loaded
    // ones, and a type with no pylon has no reserve (docs/decisions/0038).
    void reset(bool range = true, uint16_t stationMask = kAllStations);
    // Select a missile directly (the AI; the player toggles with cycleWeapon).
    void select(MissileSlot slot);
    // Launch-zone prediction effort per sim step (bots use less).
    void setZoneEffort(int steps) { zoneSteps_ = steps; }
    void update(const FlightState& aircraft, const ControlInput& in, Radar& radar, const TargetList& targets,
                HitQueue& hits, float dt);

    MissileSlot selected() const { return selected_; }
    const MissileParams& selectedType() const;
    bool loaded(int station) const { return loaded_[station]; }
    // The pylon is part of this sortie's loadout (it may be empty just now, awaiting a reload).
    bool carried(int station) const { return (mask_ >> station) & 1; }
    // Any pylon of this missile type is in the loadout.
    bool carries(MissileSlot slot) const;
    // Mass and drag of the missiles hanging now, relative to the full load.
    StoresRatios storesRatios(const AircraftParams& aircraft) const;
    // Missiles ready to fire of a type (on the rails) / in reserve.
    int ready(MissileSlot slot) const;
    int reserve(MissileSlot slot) const { return reserve_[static_cast<int>(slot)]; }

    const Missile& missile(int i) const { return missiles_[i]; }
    const Missile* missiles() const { return missiles_; }
    const Gun& gun() const { return gun_; }
    const WeaponEvents& events() const { return events_; }
    WeaponView view() const { return {missiles_, kMaxMissiles, &events_, &gun_}; }

    // IR seeker head of the selected R-73 (world direction it looks along)
    // and whether it has a lock (target index, -1 = searching).
    const math::Vec3& seekerDir() const { return seekerDir_; }
    int seekerLock() const { return seekerLock_; }
    // The selected weapon can engage the designated target now: locked and
    // inside the launch zone (a missile fired now would reach it, assuming
    // it flies on straight).
    bool inZone() const { return inZone_; }
    float zoneTime() const { return zoneTime_; }  // s of flight to impact, if in zone
    // Locked onto the designated target (IR head locked / radar track held).
    bool locked() const { return locked_; }
    // Lock-on is gradual (docs/decisions/0050): progress 0..1 while the head
    // (R-73) or the radar (R-77) acquires; lockDir is where it looks now - it
    // travels from the nose to the target as the progress grows (the HUD ring).
    // lockAge: s since the lock was made (the HUD's snap flash).
    float lockProgress() const { return lockProgress_; }
    const math::Vec3& lockDir() const { return lockDir_; }
    float lockAge() const { return lockAge_; }

private:
    int pickStation() const;
    void launch(const FlightState& aircraft, int station, Radar& radar);
    void updateSeeker(const FlightState& aircraft, const Radar& radar, const TargetList& targets, float dt);
    void updateZone(const FlightState& aircraft, const Radar& radar, const TargetList& targets);
    void clearLock();
    // Progress towards a lock: grows while `can`, decays twice as fast otherwise;
    // lockDir_ = the nose turned towards `aim` by the eased progress.
    void acquire(const FlightState& aircraft, bool can, const math::Vec3& aim, float dt);

    uint16_t mask_ = kAllStations;
    bool loaded_[kStationCount] = {};
    float reload_[kStationCount] = {};
    int reserve_[kMissileSlots] = {};
    int lastSide_ = 1;
    MissileSlot selected_ = MissileSlot::R73;
    Missile missiles_[kMaxMissiles];
    Gun gun_;
    float gunReload_ = 0.0f;
    bool prevMissile_ = false, prevWeapon_ = false, prevTarget_ = false;
    WeaponEvents events_;

    math::Vec3 seekerDir_{0.0f, 0.0f, -1.0f};
    int seekerLock_ = -1;
    bool locked_ = false;
    float lockProgress_ = 0.0f;
    float lockAge_ = 0.0f;
    int lockTarget_ = -1;  // the designation the progress was made against
    math::Vec3 lockDir_{0.0f, 0.0f, -1.0f};

    // Launch-zone prediction runs as a background job a few dozen steps at a
    // time: a missile flown forward against the target on a straight line.
    struct ZoneJob {
        bool running = false;
        Missile m;
        FlightState target;
        float t = 0.0f, prevRange = 1e30f;
        int targetIndex = -1;
        MissileSlot slot = MissileSlot::R73;
    } job_;
    bool inZone_ = false;
    float zoneTime_ = 0.0f;
    bool range_ = true;
    int zoneSteps_ = kZoneStepsPerTick;
};

}  // namespace game
