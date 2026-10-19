#pragma once

#include <cstdint>

#include "game/armament.h"
#include "game/radar.h"

namespace game {

// Another aircraft's emitters as a radar warning receiver hears them: its
// radar (search / lock) and its missiles. `index`: our slot in its TargetList.
struct Emitter {
    const FlightState* platform = nullptr;
    const Radar* radar = nullptr;
    const Armament* arms = nullptr;           // its missiles: an aircraft's...
    const Missile* missiles = nullptr;        // ...or any array of them (a ship's); wins over `arms`
    int missileCount = 0;
    int index = 0;
    const ::world::Terrain* terrain = nullptr;  // rocks block its beam (docs/decisions/0042)
};

enum class RwrLevel : uint8_t {
    None,
    Search,   // its radar's scan sweeps over us
    Lock,     // its radar holds us continuously (designated / STT)
    Missile,  // a missile is homing on us (radar seeker on us / plume seen)
};

struct RwrContact {
    RwrLevel level = RwrLevel::None;
    math::Vec3 dir;       // world unit vector towards the emitter (last known)
    float sinceSeen = 0.0f;  // s since the last illumination
};

// Radar warning receiver (docs/decisions/0034). Passive: it only knows what
// reaches its antennas - the bearing of a radar that illuminates us, whether
// that radar holds us continuously, and an active radar seeker locked on us.
// A passive IR missile emits nothing it can hear; only a missile approach
// warner (UV/IR plume detector) would see one (`maws`).
class Rwr {
public:
    static constexpr int kMaxEmitters = kMaxTargets;
    static constexpr float kPersistence = 2.5f;  // s a search contact stays shown after a sweep

    explicit Rwr(bool maws = false) : maws_(maws) {}

    void reset();
    void update(const FlightState& own, const Emitter* emitters, int count, float dt);

    const RwrContact& contact(int i) const { return contacts_[i]; }
    RwrLevel worst() const { return worst_; }
    // The most urgent missile: direction (world, from us) and range.
    bool missile() const { return missile_; }
    const math::Vec3& missileDir() const { return missileDir_; }
    float missileRange() const { return missileRange_; }
    // Events of this step (sounds, voice).
    bool newContact() const { return newContact_; }
    bool newLaunch() const { return newLaunch_; }

private:
    bool maws_;
    RwrContact contacts_[kMaxEmitters];
    RwrLevel worst_ = RwrLevel::None;
    bool missile_ = false, prevMissile_ = false;
    math::Vec3 missileDir_;
    float missileRange_ = 0.0f;
    bool newContact_ = false, newLaunch_ = false;
    float prevScan_[kMaxEmitters] = {};
};

}  // namespace game
