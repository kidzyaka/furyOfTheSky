#include "game/ship.h"

#include <algorithm>
#include <cmath>

namespace game {

using math::Vec3;

// docs/decisions/0045 has the sources. A missile/patrol corvette of the
// Tarantul / Molniya class: 56 m, ~38 kn flat out, an AK-630M aft and an
// Igla launcher - the 76 mm gun forward is left out (it is not a threat to a
// manoeuvring fighter in this game).
const ShipParams kPatrolShip = {
    "SHIP",
    /*hull*/ {{5.5f, 5.5f, 28.0f}, {0.01f, 0.01f, 0.01f}, {0.0f, 0.0f, 0.0f}},
    /*cruise*/ 9.0f,      // ~17 kn
    /*dash*/ 16.0f,       // ~31 kn
    /*turnRate*/ 0.07f,   // ~4 deg/s
    /*health*/ 300.0f,    // three missile hits, or a long gun run
    /*shellFactor*/ 0.15f,
    /*ceiling*/ 1500.0f,  // gameplay rule: only aircraft that come down to it
    /*gunRange*/ 3500.0f, // AK-630M: effective ~4 km against aircraft
    /*samRange*/ 5000.0f, // Igla: 5.2 km
    /*samMinRange*/ 600.0f,
    /*sams*/ 6,
    /*samInterval*/ 7.0f,
    /*gunMount*/ {0.0f, 6.5f, 17.0f},
    /*samMount*/ {0.0f, 8.0f, 9.0f},
    /*radarMount*/ {0.0f, 17.0f, -2.0f},
};

// AK-630M: 30 x 165 mm (the GSh-30-1's round), ~900 m/s, 4000-5000 rds/min.
// Each tracer here stands for ~3 rounds (the shell pool is shared logic with
// the aircraft gun); damage per hit is per tracer.
const GunParams kAK630 = {
    /*muzzle*/ {0.0f, 0.0f, 0.0f},
    /*muzzleVelocity*/ 900.0f,
    /*rate*/ 30.0f,
    /*rounds*/ 3000,
    /*dispersion*/ 0.004f,
    /*dragK*/ 3.3e-4f,
    /*life*/ 5.0f,        // the rounds self-destruct past ~5 km
    /*damage*/ 14.0f,
};

// Igla (9K38) family, as in the naval launchers: length 1.57 m, 72 mm,
// 10.8 kg, ~570 m/s, 5.2 km, ceiling 3.5 km, cooled two-colour IR seeker.
// Turn rates and seeker details not published: estimates (ADR 0045).
const MissileParams kIglaN = {
    "IGLA",
    Guidance::Infrared,
    /*length*/ 1.57f, /*diameter*/ 0.072f,
    /*burnTime*/ 2.2f,
    /*boostAccel*/ 300.0f,
    /*refK*/ 2.07e-4f,       // 1.1 * 0.00407 m^2 / (2 * 10.8 kg)
    /*cd0*/ 0.45f,
    /*cnAlpha*/ 22.0f,
    /*cnMax*/ 10.0f,
    /*maxG*/ 16.0f,
    /*tvc*/ 0.0f,
    /*response*/ 7.0f,
    /*navGain*/ 3.5f,
    /*guidanceDelay*/ 0.25f,
    /*gimbal*/ math::radians(38.0f),
    /*fov*/ math::radians(1.5f),
    /*seekerRange*/ 6000.0f,  // locks a dry-power fighter's tail at ~6 km, an afterburner much further
    /*trackRate*/ 0.5f,
    /*flareRejection*/ 0.35f, // two-colour seeker: some rejection, still decoyed
    /*memory*/ 0.5f,
    /*armTime*/ 0.5f,
    /*fuzeRadius*/ 2.5f,
    /*lethalRadius*/ 0.5f,    // 1.17 kg warhead: a hit hurts, rarely kills outright
    /*maxTime*/ 14.0f,
    /*minSpeed*/ 150.0f,
    /*mass*/ 10.8f,
    /*carryDragArea*/ 0.0f,
    /*lockTime*/ 0.0f,         // ship SAMs don't use the aircraft's lock-on
};

// Search + fire control: all round, sees a fighter at ~40 km, looks up
// (no sea behind the target, no clutter notch).
const RadarParams kShipRadar = {
    /*searchAz*/ math::kPi,
    /*searchEl*/ math::radians(80.0f),
    /*trackLimit*/ math::kPi,
    /*rangeRef*/ 40000.0f,
    /*scanPeriod*/ 2.5f,
    /*notchSpeed*/ 25.0f,
    /*clutterAngle*/ math::radians(-3.0f),
    /*memory*/ 2.0f,
};

Ship::Ship(const ShipParams& params) : p_(params), radar_(kShipRadar), gun_(kAK630) {}

float Ship::random() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return static_cast<float>(rng_ >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

void Ship::reset(float x, float z, float heading, const Vec3* route, int count, uint32_t seed) {
    rng_ = seed | 1u;
    routeCount_ = std::min(count, kMaxWaypoints);
    for (int i = 0; i < routeCount_; ++i) route_[i] = route[i];
    next_ = 0;
    heading_ = heading;
    speed_ = p_.cruise;
    weave_ = time_ = 0.0f;
    health_ = p_.health;
    sink_ = roll_ = trim_ = settle_ = 0.0f;
    listSide_ = random() < 0.0f ? -1.0f : 1.0f;
    destroyed_ = engaging_ = false;
    radar_.reset();
    radar_.setAutoDesignate(false);
    gun_.reset();
    burst_ = pause_ = 0.0f;
    for (Missile& m : missiles_) m = Missile{};
    samsLeft_ = p_.sams;
    samTimer_ = p_.samInterval * 0.5f;
    events_ = WeaponEvents{};
    state_ = FlightState{};
    state_.position = {x, 4.0f, z};  // sensors aim at the superstructure, not the waterline
    sail(false, 0.0f);
}

FlightState Ship::mount(const Vec3& at, const Vec3& dir) const {
    FlightState m = state_;
    m.position = state_.position + state_.orientation.rotate(at - Vec3{0.0f, 4.0f, 0.0f});
    m.orientation = math::lookRotation(dir, math::kUp);
    return m;
}

void Ship::sail(bool threatened, float dt) {
    if (!alive()) {
        // Dead in the water: lists, settles by the stern, goes down.
        sink_ += dt;
        speed_ = std::max(0.0f, speed_ - 1.0f * dt);
        const float t = sink_ / kSinkTime;
        roll_ = listSide_ * math::radians(22.0f) * math::smoothstep(0.0f, 0.6f, t);
        trim_ = math::radians(7.0f) * math::smoothstep(0.2f, 0.9f, t);
        settle_ = 16.0f * math::smoothstep(0.3f, 1.0f, t);
    } else {
        time_ += dt;
        float want = heading_;
        if (routeCount_ > 0) {
            const Vec3 wp = route_[next_];
            const float dx = wp.x - state_.position.x, dz = wp.z - state_.position.z;
            if (dx * dx + dz * dz < 300.0f * 300.0f) next_ = (next_ + 1) % routeCount_;
            want = std::atan2(dx, -dz);
        }
        const bool alarm = threatened || engaging_;
        if (alarm) {
            // Evasive: weave ~+-35 deg off the course, flat out.
            weave_ += dt;
            want += 0.6f * std::sin(weave_ * (2.0f * math::kPi / 14.0f));
        }
        float d = std::remainder(want - heading_, 2.0f * math::kPi);
        const float step = p_.turnRate * (alarm ? 1.3f : 1.0f) * dt;
        heading_ += std::clamp(d, -step, step);
        const float target = alarm ? p_.dash : p_.cruise;
        speed_ += std::clamp(target - speed_, -0.6f * dt, 0.6f * dt);
        // Heel into the turn a little.
        roll_ += (std::clamp(-d * 2.0f, -0.08f, 0.08f) - roll_) * math::approach(1.5f, dt);
    }
    const Vec3 dir{std::sin(heading_), 0.0f, -std::cos(heading_)};
    state_.position += dir * (speed_ * dt);
    state_.position.y = 4.0f - settle_;
    state_.pathDir = dir;
    state_.speed = speed_;
    state_.orientation = math::lookRotation(dir, math::kUp) * math::Quat::axisAngle(math::kForward, -roll_) *
                         math::Quat::axisAngle(math::kRight, trim_);
    state_.crashed = !alive();
}

void Ship::defend(const TargetList& targets, int target, HitQueue& hits, float dt) {
    engaging_ = false;
    const bool valid = alive() && target >= 0 && target < targets.count && targets.items[target].state &&
                       !targets.items[target].state->crashed;
    radar_.update(state_, targets, dt);
    const Vec3 radarPos = mount(p_.radarMount, state_.pathDir).position;
    if (valid) {
        const FlightState& t = *targets.items[target].state;
        const float range = math::length(t.position - radarPos);
        // The rule: only what comes down to it, inside its weapons' reach, in sight.
        const bool envelope = t.position.y < p_.ceiling && range < std::max(p_.gunRange, p_.samRange) &&
                              targets.visible(radarPos, t.position);
        if (envelope && radar_.track(target).valid)
            radar_.designate(target);
        else
            radar_.release();
        engaging_ = radar_.designated() == target;
    } else {
        radar_.release();
    }

    // --- Gun: bursts on the radar's lead solution, a new aim error each burst.
    const Track& tr = radar_.track(target >= 0 && target < kMaxTargets ? target : 0);
    const Vec3 gunPos = mount(p_.gunMount, state_.pathDir).position;
    FlightState turret = mount(p_.gunMount, engaging_ ? math::normalize(tr.pos - gunPos) : state_.pathDir);
    // The fire-control computer allows for the ship's own motion: the rounds
    // are solved as if fired from a still mount (exactly what it cancels).
    turret.speed = 0.0f;
    bool trigger = false;
    if (engaging_ && math::length(tr.pos - gunPos) < p_.gunRange) {
        Vec3 leadDir;
        float tof;
        if (gun_.lead(turret, tr.pos, tr.vel, leadDir, tof)) {
            turret.orientation = math::lookRotation(math::normalize(leadDir + aimError_), math::kUp);
            if (burst_ > 0.0f) {
                trigger = true;
                if ((burst_ -= dt) <= 0.0f) pause_ = 0.8f;
            } else if ((pause_ -= dt) <= 0.0f) {
                burst_ = 1.2f;
                const float range = math::length(tr.pos - gunPos);
                // Radar fire control: a few mrad of error per burst, growing with range.
                aimError_ = Vec3{random(), random(), random()} * (0.0012f + 0.0035f * range / p_.gunRange);
            }
        }
    } else {
        burst_ = 0.0f;
    }
    if (gun_.rounds() < 100) gun_.reload();  // the magazines below decks
    gun_.update(turret, trigger, targets, hits, dt);

    // --- Missiles: a launch needs the seeker's own lock (a MANPADS rule).
    samTimer_ += dt;
    int up = 0;
    for (const Missile& m : missiles_) up += m.live();
    if (engaging_ && samsLeft_ > 0 && samTimer_ >= p_.samInterval && up < 2) {
        const FlightState& t = *targets.items[target].state;
        const Vec3 at = mount(p_.samMount, state_.pathDir).position;
        const float range = math::length(t.position - at);
        if (range > p_.samMinRange && range < p_.samRange) {
            const Vec3 cue = math::normalize(t.position - at);
            FlightState launcher = mount(p_.samMount, cue);
            Vec3 head;
            if (infraredLock(kIglaN, launcher, cue, targets, head) == target) {
                for (Missile& m : missiles_) {
                    if (m.live()) continue;
                    // Lofted a little over the line of sight, off the rail.
                    launcher.orientation = math::lookRotation(math::normalize(head + Vec3{0.0f, 0.08f, 0.0f}), math::kUp);
                    m = launchMissile(kIglaN, Launcher::Rail, launcher, {0.0f, 0.0f, 0.0f}, target, &tr);
                    // Off the launch tube along it (the ship's own way is slow and sideways).
                    m.vel = launcher.orientation.forward() * 30.0f + state_.velocity();
                    m.aimPos = t.position;
                    m.aimVel = t.velocity();
                    --samsLeft_;
                    samTimer_ = 0.0f;
                    if (events_.launches < WeaponEvents::kMax) events_.launchStation[events_.launches++] = -1;
                    break;
                }
            }
        }
    }
    for (Missile& m : missiles_) {
        if (!m.live()) continue;
        const MissileResult r = stepMissile(m, targets, nullptr, dt);
        if (r.end == MissileEnd::None) continue;
        if (events_.ends < WeaponEvents::kMax) events_.end[events_.ends++] = r;
        if (r.end == MissileEnd::Proximity) hits.push({HitKind::Missile, r.target, r.pos, r.damage});
    }
}

void Ship::update(const TargetList& targets, int targetIndex, bool threatened, HitQueue& hits, float dt) {
    events_.launches = events_.ends = 0;
    events_.reloaded = false;
    if (!afloat()) return;
    sail(threatened, dt);
    defend(targets, targetIndex, hits, dt);
}

void Ship::hit(const HitEvent& e) {
    if (!alive()) return;
    health_ -= e.kind == HitKind::Shell ? e.damage * p_.shellFactor : e.damage;
    if (health_ <= 0.0f) {
        health_ = 0.0f;
        destroyed_ = true;
        engaging_ = false;
        radar_.release();
        state_.crashed = true;
    }
}

}  // namespace game
