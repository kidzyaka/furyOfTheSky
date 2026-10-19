#include "game/missile.h"

#include <algorithm>
#include <cmath>

#include "game/signature.h"

namespace game {

using math::approach;
using math::clamp;
using math::kGravity;
using math::Quat;
using math::Vec3;

namespace {

constexpr Vec3 kGravityVec{0.0f, -kGravity, 0.0f};
constexpr float kEjectSpeed = 7.0f;          // m/s downwards off an AKU-170
constexpr float kEjectIgnition = 0.4f;       // s
constexpr float kSeekerNotch = 25.0f;        // m/s, the missile's own Doppler filter
constexpr float kClutterAngle = 0.044f;      // rad (2.5 deg), as the aircraft radar

struct IrScene {
    Vec3 pos, vel;   // signal-weighted centroid
    float signal = 0.0f;
    SeekerTrack kind = SeekerTrack::None;  // of the strongest source
    int owner = -1;
    Vec3 strongestPos, strongestVel;
    float strongest = 0.0f;
};

void addSource(IrScene& sc, const Vec3& pos, const Vec3& vel, float w, SeekerTrack kind, int owner) {
    sc.pos += pos * w;
    sc.vel += vel * w;
    sc.signal += w;
    if (w > sc.strongest) {
        sc.strongest = w;
        sc.strongestPos = pos;
        sc.strongestVel = vel;
        sc.kind = kind;
        sc.owner = owner;
    }
}

// What an IR seeker at `from` looking along `dir` sees inside `fov`: every
// hot thing in the field adds to the signal and pulls the centroid it steers
// for. A flare next to its aircraft drags the aim point off it; once they
// separate, the head stays with whichever is still in the field. The signal
// falls with range squared (point sources).
IrScene irLook(const MissileParams& p, const Vec3& from, const Vec3& dir, float fov, const TargetList& targets) {
    IrScene sc;
    const float cosFov = std::cos(fov);
    for (int i = 0; i < targets.count; ++i) {
        const FlightState& t = *targets.items[i].state;
        Vec3 rel = t.position - from;
        float r = math::length(rel);
        if (r > 1.0f && math::dot(rel, dir) > cosFov * r && targets.visible(from, t.position))
            addSource(sc, t.position, t.velocity(), targetIr(targets.items[i], -rel / r) / (r * r), SeekerTrack::Aircraft, i);
        const Countermeasures* cm = targets.items[i].flares;
        if (!cm) continue;
        for (int f = 0; f < Countermeasures::kMaxFlares; ++f) {
            const Flare& fl = cm->flare(f);
            if (!fl.lit()) continue;
            rel = fl.pos - from;
            r = math::length(rel);
            if (r < 1.0f || math::dot(rel, dir) < cosFov * r || !targets.visible(from, fl.pos)) continue;
            const float burn = 1.0f - math::smoothstep(3.3f, Countermeasures::kBurnTime, fl.age);
            const float w = kFlareIntensity * burn * (1.0f - p.flareRejection) / (r * r);
            addSource(sc, fl.pos, fl.vel, w, SeekerTrack::Flare, i);
        }
    }
    if (sc.signal > 0.0f) {
        sc.pos = sc.pos / sc.signal;
        sc.vel = sc.vel / sc.signal;
    }
    return sc;
}

float lockThreshold(const MissileParams& p) { return 1.0f / (p.seekerRange * p.seekerRange); }

float angleBetween(const Vec3& a, const Vec3& b) { return std::acos(clamp(math::dot(a, b), -1.0f, 1.0f)); }

// Infrared homing: keep the head on the centroid of what it sees.
void irSeeker(Missile& m, const TargetList& targets, float dt) {
    const MissileParams& p = *m.type;
    const Vec3 axis = math::normalize(m.vel);
    const Vec3 dir = math::normalize(m.aimPos - m.pos);
    IrScene sc = irLook(p, m.pos, dir, p.fov, targets);
    bool ok = sc.signal >= lockThreshold(p);
    if (!ok) {
        // Lost the centroid (the target and a flare drifted apart either side
        // of it): a wider look, and the head jumps to the strongest source.
        sc = irLook(p, m.pos, dir, p.fov * 2.5f, targets);
        ok = sc.strongest >= lockThreshold(p);
        sc.pos = sc.strongestPos;
        sc.vel = sc.strongestVel;
    }
    if (ok) {
        const Vec3 rel = sc.pos - m.pos;
        const float r = math::length(rel);
        const float losRate = math::length(math::cross(rel, sc.vel - m.vel)) / std::max(r * r, 1.0f);
        ok = angleBetween(rel / std::max(r, 1e-3f), axis) < p.gimbal && losRate < p.trackRate;
    }
    if (ok) {
        m.aimPos = sc.pos;
        m.aimVel = sc.vel;
        m.tracking = sc.kind;
        m.trackIndex = sc.owner;
        m.lost = 0.0f;
    } else {
        m.lost += dt;
        m.tracking = SeekerTrack::None;
    }
}

// Active radar homing after inertial/datalink midcourse.
void radarSeeker(Missile& m, const TargetList& targets, const Track* datalink, float dt) {
    const MissileParams& p = *m.type;
    // Midcourse: the launcher's radar track comes over the datalink until
    // the missile's own seeker has the target.
    const bool fed = datalink && datalink->valid && m.tracking != SeekerTrack::Aircraft;
    if (fed) {
        m.aimPos = datalink->pos;
        m.aimVel = datalink->vel;
    }
    // Pitbull: close enough to the predicted target for its own radar.
    if (!m.active && math::length(m.aimPos - m.pos) < p.seekerRange) m.active = true;
    if (!m.active) return;  // inertial midcourse on the (coasting) estimate
    const Vec3 axis = math::normalize(m.vel);
    const Vec3 est = math::normalize(m.aimPos - m.pos);
    const float cosFov = std::cos(p.fov);
    int best = -1;
    float bestErr = 1e30f;
    for (int i = 0; i < targets.count; ++i) {
        const FlightState& t = *targets.items[i].state;
        if (t.crashed) continue;
        const Vec3 rel = t.position - m.pos;
        const float r = math::length(rel);
        if (r < 1.0f) continue;
        const Vec3 los = rel / r;
        if (math::dot(los, est) < cosFov || angleBetween(los, axis) > p.gimbal) continue;
        const float rcs = targetRcs(targets.items[i], -los);
        if (r > p.seekerRange * std::sqrt(std::sqrt(rcs / 5.0f))) continue;
        // Its own pulse-Doppler filter: notched against the sea like the aircraft's
        // radar - except a ship, held in the sea-surface mode (docs/decisions/0045).
        if (!targets.items[i].surface && los.y < std::sin(kClutterAngle) &&
            std::fabs(math::dot(t.velocity(), los)) < kSeekerNotch)
            continue;
        if (!targets.visible(m.pos, t.position)) continue;  // behind a rock
        const float err = math::length(t.position - m.aimPos);
        if (err < bestErr) {
            bestErr = err;
            best = i;
        }
    }
    if (best >= 0) {
        const FlightState& t = *targets.items[best].state;
        m.aimPos = t.position;
        m.aimVel = t.velocity();
        m.tracking = SeekerTrack::Aircraft;
        m.trackIndex = best;
        m.lost = 0.0f;
    } else {
        m.tracking = SeekerTrack::None;
        m.lost = fed ? 0.0f : m.lost + dt;
    }
}

}  // namespace

// Sources and the reasoning behind each number: docs/decisions/0026. Air
// density is taken at ~1 km (rho 1.1): the world has no atmosphere model and
// the fights happen low, where real missiles have a fraction of their
// brochure range.
const MissileParams kR73 = {
    "R-73",
    Guidance::Infrared,
    /*length*/ 2.90f, /*diameter*/ 0.170f,
    /*burnTime*/ 6.0f,        // <= 6.8 s (MiG-29 manual)
    /*boostAccel*/ 120.0f,    // ~11 kN on ~95 kg mean mass: Mach 2.5 class
    /*refK*/ 1.19e-4f,        // 1.1 * 0.0227 m^2 / (2 * 105 kg)
    /*cd0*/ 0.60f,            // canards, destabilizers, rollerons, base drag
    /*cnAlpha*/ 28.0f,
    /*cnMax*/ 14.0f,
    /*maxG*/ 40.0f,
    /*tvc*/ 0.35f,            // gas-dynamic spoilers in the jet: agile off the rail
    /*response*/ 6.0f,
    /*navGain*/ 4.0f,
    /*guidanceDelay*/ 0.25f,
    /*gimbal*/ math::radians(60.0f),   // R-73M (RMD-2): +-60 deg off boresight
    /*fov*/ math::radians(2.5f),
    /*seekerRange*/ 12000.0f, // dry-power tail-on lock range; ~4 km nose-on, ~35 km on a burner
    /*trackRate*/ 1.2f,
    /*flareRejection*/ 0.4f,  // has IRCCM, still beaten by well-timed salvos
    /*memory*/ 0.6f,
    /*armTime*/ 0.6f,         // ~300 m minimum range
    /*fuzeRadius*/ 6.0f,      // radio proximity fuze (not published: 3-5 m class)
    /*lethalRadius*/ 3.5f,    // 7.4 kg continuous rod
    /*maxTime*/ 20.0f,        // not published; gas generator / battery life class
    /*minSpeed*/ 200.0f,
    /*mass*/ 105.0f,          // R-73 (RMD-1), ru.wikipedia; the RMD-2 is 110 kg
    /*carryDragArea*/ 0.0102f, // 0.3 * 0.0227 m^2 * 1.5: estimate, not verified (0038)
    /*lockTime*/ 0.8f,         // game value (Ace Combat pace), not a published figure (0050)
};

const MissileParams kR77 = {
    "R-77",
    Guidance::ActiveRadar,
    /*length*/ 3.60f, /*diameter*/ 0.200f,
    /*burnTime*/ 5.5f,        // 4.5-6 s, ~59 kg of propellant
    /*boostAccel*/ 170.0f,    // ~25 kN on ~145 kg: Mach 3+ at burnout
    /*refK*/ 9.87e-5f,        // 1.1 * 0.0314 m^2 / (2 * 175 kg)
    /*cd0*/ 0.60f,            // lattice fins are draggy, the burnt-out motor base too
    /*cnAlpha*/ 30.0f,
    /*cnMax*/ 15.0f,
    /*maxG*/ 40.0f,
    /*tvc*/ 0.0f,
    /*response*/ 5.0f,
    /*navGain*/ 4.0f,
    /*guidanceDelay*/ 0.2f,
    /*gimbal*/ math::radians(55.0f),   // not published
    /*fov*/ math::radians(8.0f),       // acquisition basket around the predicted target
    /*seekerRange*/ 16000.0f, // 9B-1348E: locks 5 m^2 at 16 km; goes active there
    /*trackRate*/ 1.5f,
    /*flareRejection*/ 1.0f,  // radar: flares mean nothing to it
    /*memory*/ 3.0f,          // re-searches on its target model after a loss
    /*armTime*/ 0.8f,
    /*fuzeRadius*/ 12.0f,     // active laser fuze, adapts to the target size
    /*lethalRadius*/ 7.0f,    // 22.5 kg rod warhead
    /*maxTime*/ 80.0f,
    /*minSpeed*/ 250.0f,
    /*mass*/ 175.0f,          // RVV-AE, ru.wikipedia
    /*carryDragArea*/ 0.0141f, // 0.3 * 0.0314 m^2 * 1.5: estimate, not verified (0038)
    /*lockTime*/ 1.2f,         // TWS track -> single-target track; game value, not verified (0050)
};

float missileDamage(const MissileParams& p, float d) {
    if (d <= p.lethalRadius) return 100.0f;
    const float t = clamp((d - p.lethalRadius) / std::max(p.fuzeRadius - p.lethalRadius, 0.1f), 0.0f, 1.0f);
    return 100.0f - 75.0f * t;
}

Missile launchMissile(const MissileParams& type, Launcher launcher, const FlightState& a, const Vec3& attach,
                      int target, const Track* datalink) {
    Missile m;
    m.type = &type;
    m.pos = a.position + a.orientation.rotate(attach);
    m.vel = a.velocity();
    m.orientation = a.orientation;
    m.target = target;
    if (launcher == Launcher::Ejector) {
        m.vel += a.orientation.rotate(Vec3{0.0f, -kEjectSpeed, 0.0f});
        m.ignition = kEjectIgnition;
    }
    // Initial estimate: the designated target's track, else straight ahead
    // (a radar missile then goes active and takes what it finds there).
    if (datalink && datalink->valid) {
        m.aimPos = datalink->pos;
        m.aimVel = datalink->vel;
    } else {
        m.aimPos = m.pos + a.orientation.forward() * 8000.0f;
        m.aimVel = a.velocity();
    }
    m.active = type.guidance == Guidance::Infrared;
    return m;
}

int infraredLock(const MissileParams& p, const FlightState& a, const Vec3& cue, const TargetList& targets,
                 Vec3& outDir) {
    outDir = cue;
    if (angleBetween(cue, a.orientation.forward()) > p.gimbal) return -1;
    IrScene sc = irLook(p, a.position, cue, p.fov, targets);
    if (sc.signal < lockThreshold(p)) return -1;
    outDir = math::normalize(sc.pos - a.position);
    return sc.kind == SeekerTrack::Aircraft ? sc.owner : -1;
}

MissileResult stepMissile(Missile& m, const TargetList& targets, const Track* datalink, float dt) {
    MissileResult res;
    if (!m.live()) return res;
    const MissileParams& p = *m.type;
    const Vec3 pos0 = m.pos;
    m.age += dt;
    m.aimPos += m.aimVel * dt;  // the estimate coasts; the seeker refreshes it

    // --- Seeker.
    if (p.guidance == Guidance::Infrared)
        irSeeker(m, targets, dt);
    else
        radarSeeker(m, targets, datalink, dt);

    // --- Guidance: proportional navigation on the estimate, plus 1 g to
    // cancel the sag, once clear of the launcher.
    float v = math::length(m.vel);
    const Vec3 fwd = v > 1.0f ? m.vel / v : m.orientation.forward();
    Vec3 cmd = -kGravityVec;
    // An IR head that lost everything flies straight; a radar missile keeps
    // flying to where its target model says the target went, searching.
    const bool guiding = m.age > m.ignition + p.guidanceDelay &&
                         (p.guidance == Guidance::ActiveRadar || m.lost < p.memory);
    if (guiding) {
        const Vec3 r = m.aimPos - m.pos;
        const Vec3 vr = m.aimVel - m.vel;
        const float R2 = std::max(math::dot(r, r), 1.0f);
        const float R = std::sqrt(R2);
        const Vec3 omega = math::cross(r, vr) / R2;               // line-of-sight rate
        const float closing = std::max(-math::dot(r, vr) / R, 150.0f);
        cmd += math::cross(omega, r / R) * (p.navGain * closing);
    }
    if (m.age < m.ignition) cmd = {};  // still falling off the ejector
    cmd -= fwd * math::dot(cmd, fwd);  // fins push sideways only

    // --- Aerodynamics: the available normal force grows with dynamic
    // pressure up to the G limit; every bit of it costs induced drag.
    const float q = p.refK * v * v;
    const float thrust = m.burning() ? p.boostAccel : 0.0f;
    const float aMax = std::min(p.maxG * kGravity, p.cnMax * q + p.tvc * thrust);
    const float cmdLen = math::length(cmd);
    if (cmdLen > aMax) cmd *= aMax / cmdLen;
    m.lateral += (cmd - m.lateral) * approach(p.response, dt);
    m.lateral -= fwd * math::dot(m.lateral, fwd);
    const float cn = q > 1e-3f ? math::length(m.lateral) / q : 0.0f;
    const float drag = q * (p.cd0 + cn * cn / p.cnAlpha);
    m.vel += (fwd * (thrust - drag) + m.lateral + kGravityVec) * dt;
    m.pos += m.vel * dt;
    v = math::length(m.vel);

    // --- Attitude for drawing: the nose leads the path by the AoA the turn
    // takes; just off the launcher it still points where the aircraft did.
    if (v > 1.0f) {
        const Vec3 path = m.vel / v;
        const float alpha = std::min(cn / p.cnAlpha, 0.5f);
        const float latLen = math::length(m.lateral);
        const Vec3 nose = latLen > 1e-3f ? math::normalize(path + m.lateral / latLen * std::tan(alpha)) : path;
        const Quat want = math::lookRotation(nose, m.orientation.up());
        const float rate = m.age < m.ignition + 0.15f ? 2.0f : 12.0f;
        m.orientation = math::nlerp(m.orientation, want, approach(rate, dt));
    }

    // --- Fuze: closest approach to each airframe during this step, assuming
    // straight-line relative motion over 1/60 s.
    if (m.age >= p.armTime) {
        for (int i = 0; i < targets.count; ++i) {
            const FlightState& t = *targets.items[i].state;
            if (t.crashed) continue;
            if (targets.items[i].surface) {
                // A ship: the fuze fires on its hull (grown by the fuze radius).
                HitShape grown = shapeOf(targets.items[i]);
                grown.bodyHalf += Vec3{p.fuzeRadius, p.fuzeRadius, p.fuzeRadius};
                const float f = segmentHit(t, grown, pos0, m.pos);
                if (f >= 0.0f) {
                    res.end = MissileEnd::Proximity;
                    res.pos = pos0 + (m.pos - pos0) * f;
                    res.target = i;
                    res.miss = 0.0f;
                    res.damage = missileDamage(p, 0.0f);
                    break;
                }
                continue;
            }
            const Vec3 tv = t.velocity();
            const Vec3 rel0 = (t.position - tv * dt) - pos0;
            const Vec3 dv = tv - (m.pos - pos0) / dt;
            const float dv2 = math::dot(dv, dv);
            const float tau = dv2 > 1e-6f ? clamp(-math::dot(rel0, dv) / dv2, 0.0f, dt) : 0.0f;
            if (tau >= dt) continue;  // still closing: the closest point is in a later step
            const float d = math::length(rel0 + dv * tau);
            if (d < p.fuzeRadius) {
                res.end = MissileEnd::Proximity;
                res.pos = pos0 + (m.pos - pos0) * (tau / dt);
                res.target = i;
                res.miss = d;
                res.damage = missileDamage(p, d);
                break;
            }
        }
    }
    if (res.end == MissileEnd::None) {
        // Into a rock (docs/decisions/0042): the warhead goes off on impact.
        const float f = targets.terrain ? targets.terrain->segment(pos0, m.pos) : -1.0f;
        if (f >= 0.0f) {
            res.end = MissileEnd::Terrain;
            m.pos = pos0 + (m.pos - pos0) * f;
        } else if (m.pos.y <= 0.0f) {
            res.end = MissileEnd::Water;
            m.pos.y = 0.0f;
        } else if (m.age > p.maxTime || (m.age > m.ignition + p.burnTime && v < p.minSpeed)) {
            res.end = MissileEnd::SelfDestruct;
        }
        res.pos = m.pos;
    }
    if (res.end != MissileEnd::None) {
        res.type = m.type;
        res.aimedAt = m.target;
        m.type = nullptr;
    }
    return res;
}

}  // namespace game
