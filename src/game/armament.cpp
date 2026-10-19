#include "game/armament.h"

#include <algorithm>
#include <cmath>

namespace game {

using math::Vec3;

// Missile centre points measured on the model (tools/models/su35s.py,
// docs/assets/su35s.md): game body axes, +Z aft.
const Station kSu35SStations[kStationCount] = {
    {{7.15f, -0.37f, 4.10f}, &kR73, Launcher::Rail},       // wingtip, P-72-1 rail
    {{-7.15f, -0.37f, 4.10f}, &kR73, Launcher::Rail},
    {{5.60f, -0.57f, 3.40f}, &kR73, Launcher::Rail},       // outer wing
    {{-5.60f, -0.57f, 3.40f}, &kR73, Launcher::Rail},
    {{4.40f, -0.62f, 2.90f}, &kR77, Launcher::Ejector},    // middle wing, AKU-170
    {{-4.40f, -0.62f, 2.90f}, &kR77, Launcher::Ejector},
    {{3.00f, -0.59f, 2.10f}, &kR77, Launcher::Ejector},    // inner wing
    {{-3.00f, -0.59f, 2.10f}, &kR77, Launcher::Ejector},
    {{1.15f, -1.36f, -0.80f}, &kR77, Launcher::Ejector},   // under the intakes
    {{-1.15f, -1.36f, -0.80f}, &kR77, Launcher::Ejector},
};

namespace {

// Ace Combat-sized stocks (an Su-37 carries 82 there): rails + reserve.
constexpr int kReserve[kMissileSlots] = {/*R-77*/ 42, /*R-73*/ 20};
const MissileParams& slotType(MissileSlot s) { return s == MissileSlot::R73 ? kR73 : kR77; }

Vec3 rotateTowards(const Vec3& from, const Vec3& to, float maxAngle) {
    const float c = math::clamp(math::dot(from, to), -1.0f, 1.0f);
    const float angle = std::acos(c);
    if (angle <= maxAngle || angle < 1e-5f) return to;
    Vec3 axis = math::cross(from, to);
    if (math::length(axis) < 1e-6f) axis = math::kUp;
    return math::normalize(math::Quat::axisAngle(axis, maxAngle).rotate(from));
}

}  // namespace

void Armament::reset(bool range, uint16_t stationMask) {
    range_ = range;
    mask_ = stationMask & kAllStations;
    for (int i = 0; i < kStationCount; ++i) {
        loaded_[i] = carried(i);
        reload_[i] = 0.0f;
    }
    // The reserve refills loaded pylons only, so a type with no pylon has none.
    for (int i = 0; i < kMissileSlots; ++i)
        reserve_[i] = range && carries(static_cast<MissileSlot>(i)) ? kReserve[i] : 0;
    lastSide_ = 1;
    selected_ = carries(MissileSlot::R73) || !carries(MissileSlot::R77) ? MissileSlot::R73 : MissileSlot::R77;
    for (Missile& m : missiles_) m = Missile{};
    gun_.reset();
    gunReload_ = 0.0f;
    prevMissile_ = prevWeapon_ = prevTarget_ = false;
    events_ = WeaponEvents{};
    seekerDir_ = {0.0f, 0.0f, -1.0f};
    lockDir_ = seekerDir_;
    clearLock();
    lockTarget_ = -1;
    job_ = ZoneJob{};
    inZone_ = false;
    zoneTime_ = 0.0f;
}

const MissileParams& Armament::selectedType() const { return slotType(selected_); }

bool Armament::carries(MissileSlot slot) const {
    for (int i = 0; i < kStationCount; ++i)
        if (carried(i) && kSu35SStations[i].type == &slotType(slot)) return true;
    return false;
}

StoresRatios Armament::storesRatios(const AircraftParams& a) const {
    // The aircraft is tuned with every pylon loaded, so that load is exactly
    // 1.0: the same sums in the same order, no rounding to spoil it.
    float mass = 0.0f, drag = 0.0f, fullMass = 0.0f, fullDrag = 0.0f;
    for (int i = 0; i < kStationCount; ++i) {
        const MissileParams& m = *kSu35SStations[i].type;
        fullMass += m.mass;
        fullDrag += m.carryDragArea;
        if (loaded_[i]) {
            mass += m.mass;
            drag += m.carryDragArea;
        }
    }
    StoresRatios r;
    r.mass = (a.cleanMass + mass) / (a.cleanMass + fullMass);
    r.drag = (a.cd0 + (drag - fullDrag) / a.wingArea) / a.cd0;
    return r;
}

void Armament::select(MissileSlot slot) {
    if (slot == selected_) return;
    selected_ = slot;
    clearLock();
    inZone_ = false;
}

int Armament::ready(MissileSlot slot) const {
    int n = 0;
    for (int i = 0; i < kStationCount; ++i) n += loaded_[i] && kSu35SStations[i].type == &slotType(slot);
    return n;
}

int Armament::pickStation() const {
    // Outermost loaded station of the selected type, alternating sides so the
    // aircraft stays balanced (and both wings get to show a launch).
    const MissileParams* type = &selectedType();
    int fallback = -1;
    for (int i = 0; i < kStationCount; ++i) {
        const Station& st = kSu35SStations[i];
        if (!loaded_[i] || st.type != type) continue;
        const int side = st.attach.x > 0.0f ? 1 : -1;
        if (side != lastSide_) return i;
        if (fallback < 0) fallback = i;
    }
    return fallback;
}

void Armament::launch(const FlightState& a, int station, Radar& radar) {
    const Station& st = kSu35SStations[station];
    int slot = -1;
    for (int i = 0; i < kMaxMissiles; ++i)
        if (!missiles_[i].live()) slot = i;
    if (slot < 0) return;

    int target = -1;
    const Track* link = nullptr;
    if (st.type->guidance == Guidance::Infrared) {
        target = seekerLock_;
    } else if (radar.designated() >= 0) {
        target = radar.designated();
        link = &radar.track(target);
    }
    Missile m = launchMissile(*st.type, st.launcher, a, st.attach, target, link);
    if (st.type->guidance == Guidance::Infrared) {
        // The head leaves looking where it looked on the rail.
        m.aimPos = m.pos + seekerDir_ * 3000.0f;
        m.aimVel = a.velocity();
        if (target >= 0 && radar.track(target).valid) {
            m.aimPos = radar.track(target).pos;
            m.aimVel = radar.track(target).vel;
        }
    }
    missiles_[slot] = m;
    loaded_[station] = false;
    reload_[station] = kReloadTime;
    lastSide_ = st.attach.x > 0.0f ? 1 : -1;
    if (events_.launches < WeaponEvents::kMax) events_.launchStation[events_.launches++] = station;
}

void Armament::clearLock() {
    seekerLock_ = -1;
    locked_ = false;
    lockProgress_ = 0.0f;
    lockAge_ = 0.0f;
}

void Armament::acquire(const FlightState& a, bool can, const Vec3& aim, float dt) {
    const float t = selectedType().lockTime;
    lockProgress_ = math::clamp(lockProgress_ + (can ? dt : -2.0f * dt) / t, 0.0f, 1.0f);
    // Smoothstep: the ring leaves the nose gently and settles on the target.
    const float e = lockProgress_ * lockProgress_ * (3.0f - 2.0f * lockProgress_);
    const Vec3 nose = a.orientation.forward();
    lockDir_ = math::normalize(nose * (1.0f - e) + aim * e);
}

void Armament::updateSeeker(const FlightState& a, const Radar& radar, const TargetList& targets, float dt) {
    const Vec3 nose = a.orientation.forward();
    if (!carries(selected_)) {  // nothing of this type in the loadout: no head, no lock
        clearLock();
        seekerDir_ = lockDir_ = nose;
        return;
    }
    // A new designation (or losing it) starts the acquisition over.
    const int d = radar.designated();
    if (d != lockTarget_) {
        clearLock();
        lockTarget_ = d;
    }
    const MissileParams& type = selectedType();
    const bool track = d >= 0 && radar.track(d).valid;
    // The line of sight the head / radar is cued along, held inside its gimbal.
    Vec3 cue = nose;
    if (track) cue = rotateTowards(nose, math::normalize(radar.track(d).pos - a.position), type.gimbal);

    if (selected_ == MissileSlot::R77) {
        // Radar missile: the lock is the radar going from a TWS track to a
        // single-target track - it needs a fresh track inside the gimbal.
        seekerLock_ = -1;
        seekerDir_ = nose;
        const bool can = track && radar.track(d).sinceReturn < 0.5f &&
                         math::dot(cue, math::normalize(radar.track(d).pos - a.position)) > 0.9999f;
        acquire(a, can, cue, dt);
        if (!can) locked_ = false;
        else if (lockProgress_ >= 1.0f && !locked_) locked_ = true, lockAge_ = 0.0f;
        if (locked_) lockAge_ += dt;
        return;
    }
    // IR head: cued by the radar's line of sight to the designated target,
    // else it looks along the boresight.
    if (seekerLock_ >= 0 && seekerLock_ < targets.count) {
        // Locked: the head follows its target by itself until it's lost.
        Vec3 dir;
        const int still = infraredLock(kR73, a, seekerDir_, targets, dir);
        if (still == seekerLock_ && (d < 0 || d == seekerLock_)) {
            seekerDir_ = lockDir_ = dir;
            lockProgress_ = 1.0f;
            lockAge_ += dt;
            return;
        }
        seekerLock_ = -1;
        locked_ = false;
    }
    // Acquiring: the head turns from the nose to the cue as the signal
    // builds up; it locks once the progress is full (docs/decisions/0050).
    Vec3 dir;
    const int candidate = infraredLock(kR73, a, cue, targets, dir);
    const bool can = candidate >= 0 && (d < 0 || d == candidate);
    acquire(a, can, can ? dir : cue, dt);
    seekerDir_ = lockDir_;
    if (can && lockProgress_ >= 1.0f) {
        seekerLock_ = candidate;
        seekerDir_ = lockDir_ = dir;
        locked_ = true;
        lockAge_ = 0.0f;
    }
}

void Armament::updateZone(const FlightState& a, const Radar& radar, const TargetList& targets) {
    const int d = radar.designated();
    if (d < 0 || !radar.track(d).valid || a.crashed || !carries(selected_)) {
        job_.running = false;
        inZone_ = false;
        return;
    }
    if (job_.running && (job_.targetIndex != d || job_.slot != selected_)) {
        job_.running = false;
        inZone_ = false;
    }
    if (!job_.running) {
        // Fly a missile from the station that would fire next, at the
        // radar's picture of the target flying straight on.
        int st = pickStation();
        if (st < 0) {
            for (int i = 0; i < kStationCount; ++i)
                if (kSu35SStations[i].type == &selectedType()) st = i;
        }
        const Track& tr = radar.track(d);
        job_.target = *targets.items[d].state;
        job_.target.position = tr.pos;
        job_.target.speed = math::length(tr.vel);
        job_.target.pathDir = job_.target.speed > 1.0f ? tr.vel / job_.target.speed : job_.target.pathDir;
        Track link = tr;
        job_.m = launchMissile(selectedType(), kSu35SStations[st].launcher, a, kSu35SStations[st].attach, 0, &link);
        job_.m.aimPos = tr.pos;
        job_.m.aimVel = tr.vel;
        job_.t = 0.0f;
        job_.prevRange = 1e30f;
        job_.targetIndex = d;
        job_.slot = selected_;
        job_.running = true;
    }
    TargetList one;
    one.add(&job_.target, nullptr);
    for (int i = 0; i < zoneSteps_ && job_.running; ++i) {
        job_.target.position += job_.target.velocity() * kZoneStep;
        Track link;
        link.valid = true;
        link.pos = job_.target.position;
        link.vel = job_.target.velocity();
        const MissileResult r = stepMissile(job_.m, one, &link, kZoneStep);
        job_.t += kZoneStep;
        bool done = r.end != MissileEnd::None;
        bool hit = r.end == MissileEnd::Proximity;
        if (!done) {
            // Past the target and opening with the motor out: a miss.
            const float range = math::length(job_.m.pos - job_.target.position);
            const MissileParams& p = *job_.m.type;
            if (job_.t > job_.m.ignition + p.burnTime && range > job_.prevRange) done = true;
            job_.prevRange = range;
        }
        if (done) {
            inZone_ = hit;
            zoneTime_ = job_.t;
            job_.running = false;
        }
    }
}

void Armament::update(const FlightState& a, const ControlInput& in, Radar& radar, const TargetList& targets,
                      HitQueue& hits, float dt) {
    events_ = WeaponEvents{};

    // --- Buttons (edges: one action per press, whatever the frame rate).
    if (in.cycleWeapon && !prevWeapon_) {
        // Only to a type that is on the wings (a light loadout may carry one).
        const MissileSlot other = selected_ == MissileSlot::R73 ? MissileSlot::R77 : MissileSlot::R73;
        if (carries(other)) select(other);
    }
    if (in.cycleTarget && !prevTarget_) {
        radar.cycle(a);
        clearLock();
        inZone_ = false;
    }
    updateSeeker(a, radar, targets, dt);
    if (in.missile && !prevMissile_ && !a.crashed) {
        const int st = pickStation();
        if (st >= 0) launch(a, st, radar);
    }
    prevMissile_ = in.missile;
    prevWeapon_ = in.cycleWeapon;
    prevTarget_ = in.cycleTarget;

    // --- Reloads from the reserve.
    for (int i = 0; i < kStationCount; ++i) {
        if (loaded_[i] || !carried(i)) continue;
        const int slot = kSu35SStations[i].type == &kR73 ? 1 : 0;
        if (reserve_[slot] <= 0) continue;
        reload_[i] -= dt;
        if (reload_[i] <= 0.0f) {
            loaded_[i] = true;
            --reserve_[slot];
            events_.reloaded = true;
        }
    }

    // --- Missiles in flight. The datalink lives while the launcher does.
    for (Missile& m : missiles_) {
        if (!m.live()) continue;
        const Track* link = !a.crashed && m.target >= 0 ? &radar.track(m.target) : nullptr;
        const MissileResult r = stepMissile(m, targets, link, dt);
        if (r.end == MissileEnd::None) continue;
        if (events_.ends < WeaponEvents::kMax) events_.end[events_.ends++] = r;
        if (r.end == MissileEnd::Proximity) hits.push({HitKind::Missile, r.target, r.pos, r.damage});
    }

    // --- Gun: the magazine refills a few seconds after it runs dry.
    if (range_ && gun_.rounds() == 0 && gunReload_ <= 0.0f) gunReload_ = kGunReload;
    if (gunReload_ > 0.0f && (gunReload_ -= dt) <= 0.0f) gun_.reload();
    gun_.update(a, in.gun, targets, hits, dt);

    updateZone(a, radar, targets);
}

}  // namespace game
