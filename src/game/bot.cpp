#include "game/bot.h"

#include <algorithm>
#include <cmath>

namespace game {

using math::clamp;
using math::Quat;
using math::Vec3;

namespace {

constexpr float kCruise = 230.0f;        // m/s (830 km/h)
constexpr float kOrbitRadius = 1500.0f;  // m: ~3.6 g at cruise
constexpr float kTrainingMaws = 8000.0f; // m, the Evade target's warning
constexpr float kFlareRange = 5000.0f;   // m: pops flares inside it
constexpr float kSpawnTraining = 3000.0f;
constexpr float kSpawnCombat = 9000.0f;  // m: comes in from beyond visual range
// Last-ditch pull: ~1-1.5 s before impact at the end-game closure (~800 m/s).
// Earlier, the missile picks us up again out of the notch and corrects.
float kLastDitchHard = 1200.0f;
float kLastDitchMedium = 900.0f;

// docs/decisions/0033. Easy reacts late and pulls little, shoots only what
// drifts in front of it, and its defence is a plain break plus flares.
// Medium hunts the player: R-77 from range, lead pursuit, breaks into attacks,
// notches radar missiles. Hard does all of it harder and adds the Su-35's
// tricks: limiter-off nose snaps and 13 g breaks, cranking after a launch,
// split-S reversals out of a bad position, two missiles in the air.
const BotSkill kSkills[3] = {
    // reaction maxG limOff maws    aimErr gunR  gunCone r73Cone r77    interval inFlight notch  crank  revers lead   corner
    {0.9f, 6.0f, false, 3000.0f, 0.035f, 600.0f, 0.030f, 0.21f, false, 8.0f, 1, false, false, false, false, 210.0f},
    {0.45f, 8.5f, false, 6000.0f, 0.015f, 800.0f, 0.035f, 0.52f, true, 5.0f, 1, true, false, false, true, 220.0f},
    {0.15f, 11.0f, true, 10000.0f, 0.006f, 1000.0f, 0.040f, 1.05f, true, 2.5f, 2, true, true, true, true, 235.0f},
};

Vec3 horizontal(const Vec3& v) {
    Vec3 h{v.x, 0.0f, v.z};
    return math::length(h) > 1e-3f ? math::normalize(h) : math::kForward;
}

float angleBetween(const Vec3& a, const Vec3& b) { return std::acos(clamp(math::dot(a, b), -1.0f, 1.0f)); }

// Direction with a climb/dive slope that brings it to `altitude`.
Vec3 withClimb(const Vec3& flat, float altitude, const FlightState& s) {
    const float slope = clamp((altitude - s.position.y) * 0.0025f, -0.35f, 0.35f);
    return math::normalize(flat + math::kUp * slope);
}

// The opponent's closest missile coming at us (index 0 in its target list).
struct Threat {
    const Missile* missile = nullptr;
    float range = 1e30f;
    Vec3 los;  // from us to the missile
};

Threat incoming(const FlightState& s, const Opponent& enemy, float range) {
    Threat t;
    for (int i = 0; i < enemy.missileCount; ++i) {
        const Missile& m = enemy.missiles[i];
        if (!m.live() || (m.target != 0 && m.trackIndex != 0)) continue;  // aimed at us / homing on us
        const Vec3 rel = m.pos - s.position;
        const float r = math::length(rel);
        if (r > range || math::dot(rel, m.vel - s.velocity()) > 0.0f) continue;  // out of reach / not closing
        if (r < t.range) {
            t.missile = &m;
            t.range = r;
            t.los = rel / std::max(r, 1.0f);
        }
    }
    return t;
}

// A beam direction to `los` (horizontal, at right angles), on the side nearer
// the current heading so the turn is the short one; `side` keeps it sticky.
Vec3 beam(const FlightState& s, const Vec3& los, int& side) {
    Vec3 b = horizontal(math::cross(math::kUp, los));
    if (math::dot(b * static_cast<float>(side), s.pathDir) < -0.2f) side = -side;
    return b * static_cast<float>(side);
}

}  // namespace

const char* botModeName(BotMode mode) {
    switch (mode) {
        case BotMode::Straight: return "STRAIGHT";
        case BotMode::Orbit: return "ORBIT";
        case BotMode::Weave: return "WEAVE";
        case BotMode::Evade: return "EVADE";
        case BotMode::Easy: return "EASY";
        case BotMode::Medium: return "MEDIUM";
        case BotMode::Hard: return "HARD";
        default: return "?";
    }
}

const BotSkill& botSkill(BotMode mode) {
    int i = static_cast<int>(mode) - static_cast<int>(BotMode::Easy);
    if (i < 0 || i > 2) i = 1;
    return kSkills[i];
}

float Bot::random() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return static_cast<float>(rng_ >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

float Bot::lookupTerrainFloor(const FlightState& s) const {
    if (!terrain_ || s.position.y > terrain_->maxHeight() + 900.0f) return 0.0f;
    float floor = 0.0f;
    for (int k = 0; k <= 8; ++k) {
        const Vec3 p = s.position + s.velocity() * (0.6f * static_cast<float>(k));
        floor = std::max(floor, terrain_->clearanceHeight(p.x, p.z, 200.0f));
    }
    return floor;
}

void Bot::reset(const FlightState& player, BotMode mode) {
    mode_ = mode;
    hits_ = kills_ = 0;
    destroyed_ = false;
    rng_ = 0xB07B07u;
    spawn(player);
}

void Bot::spawn(const FlightState& player) {
    floorTimer_ = 0.0f;  // look the terrain up on the first step
    heading_ = horizontal(player.pathDir);
    altitude_ = clamp(player.position.y, 800.0f, 4000.0f);
    Vec3 pos;
    if (isCombat(mode_)) {
        // Beyond visual range ahead, offset to one side, coming at the player.
        const Vec3 right = math::cross(heading_, math::kUp);
        altitude_ = lowLevel_ ? 215.0f + 65.0f * random() : clamp(player.position.y + 300.0f * random(), 1000.0f, 5000.0f);
        pos = player.position + heading_ * kSpawnCombat + right * (2000.0f * random());
        pos.y = altitude_;
        // Never inside a rock: over one, it starts high enough to clear it.
        if (terrain_) pos.y = std::max(pos.y, terrain_->clearanceHeight(pos.x, pos.z, 300.0f) + 150.0f);
        heading_ = horizontal(player.position - pos);
    } else {
        // Training: ahead of the player, flying the same way - a tail chase.
        pos = player.position + heading_ * kSpawnTraining;
        pos.y = altitude_;
    }
    centre_ = pos + math::cross(heading_, math::kUp) * kOrbitRadius;
    model_.reset(pos, math::lookRotation(heading_, math::kUp), kCruise);
    boundary_.reset();
    surfaces_.reset();
    flares_.reset();
    radar_.reset();
    arms_.reset(false);
    arms_.setZoneEffort(12);
    in_ = ControlInput{};
    health_ = 100.0f;
    dead_ = false;
    deadTimer_ = 0.0f;
    wantFlares_ = false;
    threatTime_ = threatHold_ = tailTime_ = burst_ = manoeuvre_ = crank_ = 0.0f;
    launchTimer_ = 3.0f;
    time_ = 0.0f;
    respawned_ = true;
    intent_ = "";
}

PilotDemand Bot::training(const Opponent& enemy) {
    const FlightState& s = model_.state();
    PilotDemand d;
    d.speed = kCruise;
    d.maxG = 6.0f;
    const float tau = 2.0f * math::kPi;
    const Threat threat = mode_ == BotMode::Evade ? incoming(s, enemy, kTrainingMaws) : Threat{};
    wantFlares_ = threat.missile && threat.range < kFlareRange;

    Vec3 dir;
    if (threat.missile) {
        // Beam the missile and dive into the sea clutter (the notch).
        intent_ = "beam";
        dir = withClimb(beam(s, threat.los, breakSide_), std::max(altitude_ - 600.0f, 500.0f), s);
        d.maxG = 9.0f;
        d.speed = 260.0f;
    } else if (mode_ == BotMode::Orbit) {
        intent_ = "orbit";
        // Level circle: centripetal demand plus a correction back onto the radius.
        Vec3 rel = s.position - centre_;
        rel.y = 0.0f;
        const float r = std::max(math::length(rel), 1.0f);
        const Vec3 inward = rel * (-1.0f / r);
        const Vec3 tangent = math::cross(math::kUp, inward);  // clockwise seen from above
        const float k = clamp((r - kOrbitRadius) / 400.0f, -0.6f, 0.6f);
        dir = withClimb(math::normalize(tangent + inward * k), altitude_, s);
        d.turn = turnTowards(s, dir, 3.0f) + inward * (s.speed * s.speed / r);
        d.turn -= s.pathDir * math::dot(d.turn, s.pathDir);
        return d;
    } else if (mode_ == BotMode::Straight) {
        intent_ = "straight";
        dir = withClimb(heading_, altitude_, s);
    } else {
        intent_ = "weave";
        // Weave: heading +-50 deg over 18 s, height +-300 m over 23 s.
        const float yaw = 0.87f * std::sin(time_ * tau / 18.0f);
        const Vec3 flat = Quat::axisAngle(math::kUp, yaw).rotate(heading_);
        dir = withClimb(flat, altitude_ + 300.0f * std::sin(time_ * tau / 23.0f), s);
        d.maxG = 5.0f;
    }
    d.turn = turnTowards(s, dir, d.maxG * 0.8f);
    // Rocks ahead: climb over them.
    const float floor = terrainFloor(s);
    if (floor > 0.0f && s.position.y < floor + 250.0f) {
        intent_ = "terrain";
        d.turn = turnTowards(s, math::normalize(horizontal(s.pathDir) + math::kUp * 0.8f), 6.0f);
    }
    return d;
}

PilotDemand Bot::combat(const Opponent& enemy, ControlInput& w, float dt) {
    const FlightState& s = model_.state();
    const FlightState& e = *enemy.state;
    const BotSkill& k = botSkill(mode_);
    PilotDemand d;
    d.speed = k.cornerSpeed;
    d.maxG = k.maxG;
    wantFlares_ = false;
    launchTimer_ += dt;
    crank_ -= dt;
    manoeuvre_ -= dt;

    const Vec3 rel = e.position - s.position;
    const float range = std::max(math::length(rel), 1.0f);
    const Vec3 los = rel / range;
    const float ata = angleBetween(s.pathDir, los);          // enemy off our nose
    const float enemyAta = angleBetween(e.pathDir, -los);    // we off the enemy's nose
    const bool behindUs = angleBetween(-s.pathDir, los) < 1.05f;  // in our rear 60 deg

    // --- Threats, each only once it has persisted for the pilot's reaction time.
    // A missile that stops closing for a moment (it lost us and flies on its
    // memory) is not gone: keep defending a few seconds after the last sight,
    // or turning back into it lets it find us again.
    const Threat threat = incoming(s, enemy, k.mawsRange);
    if (threat.missile) {
        threatHold_ = 3.0f;
        threatLos_ = threat.los;
        threatRange_ = threat.range;
        threatRadar_ = threat.missile->type->guidance == Guidance::ActiveRadar;
    } else {
        threatHold_ -= dt;
    }
    const bool threatened = threatHold_ > 0.0f;
    threatTime_ = threatened ? threatTime_ + dt : 0.0f;
    const bool onTail = !e.crashed && behindUs && enemyAta < 0.45f && range < 3000.0f;
    tailTime_ = onTail ? tailTime_ + dt : 0.0f;

    Vec3 dir;
    if (threatened && threatTime_ > k.reaction) {
        const bool radarMissile = threatRadar_;
        Threat t;  // the last sight of it
        t.los = threatLos_;
        t.range = threatRange_;
        const Threat& threat = t;
        if (!k.notch) {
            // Easy: break away from it and pop flares at anything.
            intent_ = "break";
            dir = beam(s, threat.los, breakSide_);
            wantFlares_ = threat.range < kFlareRange;
        } else if (radarMissile) {
            // Beam it - no closure in its Doppler - and dive so it looks down at the sea.
            intent_ = "notch";
            dir = withClimb(beam(s, threat.los, breakSide_), std::max(s.position.y - 900.0f, lowLevel_ ? 140.0f : 700.0f), s);
            d.speed = 260.0f;
            // Last ditch: a missile that lost us in the notch flies to where its
            // memory says we'll be - on a straight beam line that's exact. A hard
            // vertical pull in the last seconds moves us ~100 m off that point.
            if (threat.range < (k.limiterOff ? kLastDitchHard : kLastDitchMedium)) {
                intent_ = "last ditch";
                dir = math::normalize(s.pathDir * 0.2f + (s.position.y < 1800.0f ? math::kUp : -math::kUp));
                d.limiterOff = k.limiterOff;
            }
        } else {
            // IR missile: burner off (cooler), flares, beam it; break hard as it closes.
            intent_ = "flares";
            dir = beam(s, threat.los, breakSide_);
            d.speed = 170.0f;  // below it the autopilot holds cruise power, never the burner
            wantFlares_ = threat.range < kFlareRange;
            if (threat.range < 2000.0f && k.limiterOff) d.limiterOff = true;
        }
        d.turn = turnTowards(s, dir, d.maxG);
    } else if (onTail && tailTime_ > k.reaction) {
        // Someone on our six: break into him (turning towards his side makes
        // the angles hardest for his gun and seeker), flares against his IR shots.
        wantFlares_ = range < 2500.0f && enemyAta < 0.25f;
        if (k.reversals && manoeuvre_ <= 0.0f && s.position.y > 2200.0f && range < 2000.0f && random() > 0.2f) {
            // Split-S: roll inverted and pull through to the opposite heading
            // (the "антипод"), trading height for a reversal he can't follow flat.
            manoeuvre_ = 4.0f;
            manoeuvreDir_ = -horizontal(s.pathDir);
        }
        if (manoeuvre_ > 0.0f) {
            intent_ = "split-s";
            dir = manoeuvre_ > 2.5f ? math::normalize(manoeuvreDir_ * 0.3f - math::kUp) : manoeuvreDir_;
            d.speed = 200.0f;
        } else {
            intent_ = "break";
            dir = horizontal(los);
            // Medium jinks: reverses every few seconds when he's close.
            if (!k.reversals && k.notch && range < 1200.0f && static_cast<int>(time_ / 2.5f) % 2) dir = -dir;
            // Hard goes out of plane: up while it has the energy for it.
            if (k.reversals && s.speed > 250.0f) dir = math::normalize(dir + math::kUp * 0.5f);
            d.limiterOff = k.limiterOff && range < 1200.0f;
            d.speed = k.cornerSpeed + 20.0f;
        }
        d.turn = turnTowards(s, dir, d.maxG);
    } else {
        // --- Offence. R-77 from beyond visual range (if the pilot uses it), the
        // R-73 and the gun up close.
        const bool r77 = k.r77 && range > 4000.0f && range < 30000.0f;
        arms_.select(r77 ? MissileSlot::R77 : MissileSlot::R73);
        const Gun& gun = arms_.gun();
        Vec3 leadDir;
        float tof = 0.0f;
        const bool lead = range < 1800.0f && gun.lead(s, e.position, e.velocity(), leadDir, tof);
        const float closure = -math::dot(e.velocity() - s.velocity(), los);

        if (crank_ > 0.0f) {
            // After an R-77 shot: turn ~45 deg off, keep him in the radar
            // (datalink), slow the closure.
            intent_ = "crank";
            dir = Quat::axisAngle(math::kUp, 0.8f * breakSide_).rotate(horizontal(los));
            d.maxG = 4.0f;
        } else if (k.leadPursuit && lead) {
            // Gun range: lead pursuit (aim where the rounds meet him); lag
            // instead if overshooting.
            intent_ = "lead";
            dir = leadDir;
            if (k.reversals && closure > 80.0f && range < 900.0f) {
                intent_ = "lag";
                dir = math::normalize(e.position - e.velocity() * 1.0f - s.position);
            }
        } else {
            intent_ = mode_ == BotMode::Easy ? "approach" : "pursuit";
            dir = los;
        }
        // Easy doesn't hunt hard: it closes at modest G and takes what comes.
        if (mode_ == BotMode::Easy) d.maxG = std::min(d.maxG, 4.0f);
        // Hard snaps the nose round with the limiter off when slow and close.
        if (k.limiterOff && range < 1500.0f && ata > 0.35f && ata < 1.6f && s.speed < 190.0f) {
            intent_ = "snap";
            d.limiterOff = true;
        }
        if (range > 4000.0f) d.speed = mode_ == BotMode::Hard ? 270.0f : 240.0f;  // close the distance
        // A raider keeps low on the way in, under the radar horizon.
        if (lowLevel_ && range > 6000.0f) {
            intent_ = "low";
            dir = withClimb(horizontal(los), 200.0f + terrainFloor(s), s);
        }
        d.turn = turnTowards(s, dir, d.maxG);

        // --- Fire control.
        // Gun: a burst when the rounds' line sits on the lead point.
        if (burst_ <= 0.0f && lead && range < k.gunRange &&
            angleBetween(gun.shellDirection(s), math::normalize(leadDir + aimOffset_)) < k.gunCone) {
            burst_ = mode_ == BotMode::Easy ? 0.4f : 0.7f;
        }
        if (burst_ > 0.0f) {
            w.gun = true;
            burst_ -= dt;
            if (burst_ <= 0.0f) aimOffset_ = Vec3{random(), random(), random()} * k.aimError;  // next burst's error
        }
    }

    // Missiles: locked, in the zone, inside its shot cone, not too many up.
    // Offence fires whenever it can; a Hard pilot also gets its own shot off
    // before going defensive ("shoot, then notch").
    if (intent_[0] != 'p' || mode_ != BotMode::Easy) {
        int up = 0;
        for (int i = 0; i < Armament::kMaxMissiles; ++i) up += arms_.missile(i).live();
        const float cone = arms_.selected() == MissileSlot::R73 ? k.r73Cone : 1.0f;
        const bool defending = threatened && threatTime_ > k.reaction;
        if ((!defending || k.crank) && launchTimer_ > k.launchInterval && up < k.maxInFlight && arms_.locked() &&
            arms_.inZone() && ata < cone && !e.crashed) {
            w.missile = true;
            launchTimer_ = 0.0f;
            if (!defending && arms_.selected() == MissileSlot::R77 && k.crank) crank_ = 8.0f;
        }
    }

    // Never into the sea or a rock: everything gives way to the pull-up. The
    // rocks under the next few seconds of flight raise the floor.
    const float vs = s.pathDir.y * s.speed;
    const float alt = s.position.y - terrainFloor(s);
    const float floorAlt = lowLevel_ ? 70.0f : 250.0f, sinkAlt = lowLevel_ ? 220.0f : 600.0f;
    if (alt < floorAlt || (alt < sinkAlt && vs < -40.0f)) {
        intent_ = "pull up";
        d.turn = turnTowards(s, math::normalize(horizontal(s.pathDir) + math::kUp * 0.9f), k.maxG);
        d.limiterOff = false;
        manoeuvre_ = 0.0f;
    }
    return d;
}

void Bot::update(const Opponent& enemy, HitQueue& hits, float dt) {
    time_ += dt;
    const FlightState& s = model_.state();
    if (dead_) {
        if ((deadTimer_ -= dt) <= 0.0f) spawn(*enemy.state);
        floorTimer_ = 0.0f;
        return;
    }
    if ((floorTimer_ -= dt) <= 0.0f) {
        floor_ = lookupTerrainFloor(s);
        floorTimer_ = kFloorPeriod;
    }
    if (s.crashed) {  // flew into the sea or a rock
        dead_ = destroyed_ = true;
        ++kills_;
        deadTimer_ = kRespawnDelay;
        return;
    }
    if (math::length(s.position - enemy.state->position) > kLeash) spawn(*enemy.state);

    TargetList targets;
    targets.add(enemy.state, enemy.flares);
    targets.terrain = terrain_;
    ControlInput weapons;
    PilotDemand d;
    if (isCombat(mode_)) {
        radar_.update(s, targets, dt);
        d = combat(enemy, weapons, dt);
    } else {
        d = training(enemy);
    }
    in_ = autopilot(s, model_.params(), d);
    in_.flares = wantFlares_;
    in_.gun = weapons.gun;
    in_.missile = weapons.missile;
    model_.step(in_, dt);
    if (terrain_ && terrain_->solid(model_.state().position)) model_.destroy();  // into a rock
    if (arena_) model_.turnHeading(boundary_.update(model_.state(), *arena_, dt));
    surfaces_.update(model_.state(), in_, dt);
    flares_.update(model_.state(), in_.flares, dt);
    if (isCombat(mode_)) arms_.update(model_.state(), in_, radar_, targets, hits, dt);
}

void Bot::hit(const HitEvent& e) {
    if (dead_) return;
    ++hits_;
    health_ -= e.damage;
    if (health_ <= 0.0f) {
        ++kills_;
        destroyed_ = dead_ = true;
        deadTimer_ = kRespawnDelay;
        model_.destroy();
    }
}

}  // namespace game
