#include "game/rwr.h"

#include <algorithm>
#include <cmath>

namespace game {

using math::Vec3;

void Rwr::reset() {
    for (RwrContact& c : contacts_) c = RwrContact{};
    for (float& p : prevScan_) p = 0.0f;
    worst_ = RwrLevel::None;
    missile_ = prevMissile_ = newContact_ = newLaunch_ = false;
    missileRange_ = 0.0f;
}

void Rwr::update(const FlightState& own, const Emitter* emitters, int count, float dt) {
    newContact_ = newLaunch_ = false;
    worst_ = RwrLevel::None;
    missile_ = false;
    missileRange_ = 1e30f;
    for (int i = 0; i < kMaxEmitters; ++i) {
        RwrContact& c = contacts_[i];
        c.sinceSeen += dt;
        if (i >= count || !emitters[i].platform || emitters[i].platform->crashed || own.crashed) {
            c = RwrContact{};
            continue;
        }
        const Emitter& e = emitters[i];
        const RwrLevel before = c.level;
        const Vec3 dir = math::normalize(e.platform->position - own.position);

        // Lock: its radar holds us continuously (our slot is its designated track).
        const bool masked = e.terrain && !e.terrain->lineOfSight(e.platform->position, own.position);
        const bool holding = !masked && e.radar->designated() == e.index && e.radar->track(e.index).valid;
        // Search: a pass of its scan just swept the volume we're in.
        const float phase = e.radar->scanPhase();
        const bool sweep = phase < prevScan_[i];
        prevScan_[i] = phase;
        if (holding && e.radar->beamReaches(*e.platform, own, true)) {
            c.level = RwrLevel::Lock;
            c.dir = dir;
            c.sinceSeen = 0.0f;
        } else if (sweep && !masked && e.radar->beamReaches(*e.platform, own, false)) {
            c.level = RwrLevel::Search;
            c.dir = dir;
            c.sinceSeen = 0.0f;
        } else if (c.level == RwrLevel::Lock && c.sinceSeen > 0.2f) {
            c.level = RwrLevel::Search;  // the lock broke; it may still be searching
        } else if (c.sinceSeen > kPersistence) {
            c.level = RwrLevel::None;
        }
        if (before == RwrLevel::None && c.level != RwrLevel::None) newContact_ = true;
        if (c.level > worst_) worst_ = c.level;

        // Missiles homing on us: an active radar seeker transmitting at us is
        // heard; an IR one only by a plume detector, and only while it burns.
        const Missile* ms = e.missiles ? e.missiles : (e.arms ? e.arms->missiles() : nullptr);
        const int msCount = e.missiles ? e.missileCount : Armament::kMaxMissiles;
        if (!ms) continue;
        for (int k = 0; k < msCount; ++k) {
            const Missile& m = ms[k];
            if (!m.live()) continue;
            const bool radarOnUs = m.type->guidance == Guidance::ActiveRadar && m.active &&
                                   m.tracking == SeekerTrack::Aircraft && m.trackIndex == e.index;
            const float range = math::length(m.pos - own.position);
            const bool plume = maws_ && m.burning() && m.target == e.index && range < 10000.0f;
            if (!radarOnUs && !plume) continue;
            if (range < missileRange_) {
                missileRange_ = range;
                missileDir_ = (m.pos - own.position) / std::max(range, 1.0f);
                missile_ = true;
            }
        }
    }
    if (missile_) worst_ = RwrLevel::Missile;
    newLaunch_ = missile_ && !prevMissile_;
    prevMissile_ = missile_;
}

}  // namespace game
