#include "game/radar.h"

#include <algorithm>
#include <cmath>

#include "game/signature.h"

namespace game {

using math::Vec3;

// N035 Irbis-E (docs/decisions/0027 has the sources): phased array scanned
// electronically +-60 deg, the whole array also turned by a hydraulic drive,
// so a single target can be held out to ~120 deg off the nose.
const RadarParams kIrbisE = {
    /*searchAz*/ math::radians(60.0f),
    /*searchEl*/ math::radians(60.0f),
    /*trackLimit*/ math::radians(120.0f),
    /*rangeRef*/ 200000.0f,
    /*scanPeriod*/ 1.0f,
    /*notchSpeed*/ 30.0f,
    /*clutterAngle*/ math::radians(2.5f),
    /*memory*/ 3.0f,
};

void Radar::reset() {
    for (Track& t : tracks_) t = Track{};
    designated_ = -1;
    scanTimer_ = 0.0f;
}

bool Radar::sees(const FlightState& own, const FlightState& t, bool designated, bool* notched, bool surface) const {
    if (notched) *notched = false;
    if (t.crashed || own.crashed) return false;
    const Vec3 rel = t.position - own.position;
    const float range = math::length(rel);
    if (range < 1.0f) return true;
    const Vec3 los = rel / range;

    // Antenna reach: the search volume around the nose (body axes), or the
    // mechanical limit for the designated target.
    const Vec3 b = own.orientation.conjugate().rotate(los);  // -Z forward
    const float off = std::acos(math::clamp(-b.z, -1.0f, 1.0f));
    if (designated) {
        if (off > p_.trackLimit) return false;
    } else {
        const float az = std::atan2(b.x, -b.z), el = std::atan2(b.y, std::sqrt(b.x * b.x + b.z * b.z));
        if (std::fabs(az) > p_.searchAz || std::fabs(el) > p_.searchEl) return false;
    }

    // Radar equation: detection range grows with the fourth root of the RCS.
    const float rcs = surface ? 1000.0f : radarCrossSection(t, -los);
    if (range > p_.rangeRef * std::sqrt(std::sqrt(rcs / 3.0f))) return false;

    // Doppler notch. Looking down (or level), the beam has the sea behind the
    // target, and the sea's echo comes back at the Doppler of the ground. A
    // pulse-Doppler radar throws away everything near that Doppler - including
    // a target whose own velocity along the line of sight is ~0.
    const bool clutter = !surface && los.y < std::sin(p_.clutterAngle);
    if (clutter && std::fabs(math::dot(t.velocity(), los)) < p_.notchSpeed) {
        if (notched) *notched = true;
        return false;
    }
    return true;
}

bool Radar::beamReaches(const FlightState& own, const FlightState& t, bool designated) const {
    if (t.crashed || own.crashed) return false;
    const Vec3 rel = t.position - own.position;
    const float range = math::length(rel);
    if (range < 1.0f) return true;
    // One-way propagation: an RWR hears a radar well beyond the radar's own
    // detection range (sqrt of the two-way loss) - in this world, always.
    if (range > 2.0f * p_.rangeRef) return false;
    const Vec3 b = own.orientation.conjugate().rotate(rel / range);
    if (designated) return std::acos(math::clamp(-b.z, -1.0f, 1.0f)) <= p_.trackLimit;
    const float az = std::atan2(b.x, -b.z), el = std::atan2(b.y, std::sqrt(b.x * b.x + b.z * b.z));
    return std::fabs(az) <= p_.searchAz && std::fabs(el) <= p_.searchEl;
}

void Radar::update(const FlightState& own, const TargetList& targets, float dt) {
    // Every track coasts on its last estimate.
    for (int i = 0; i < kMaxTargets; ++i) {
        Track& tr = tracks_[i];
        if (!tr.valid) continue;
        tr.pos += tr.vel * dt;
        tr.sinceReturn += dt;
    }

    // Track-while-scan: one look at every target per pass of the volume.
    scanTimer_ += dt;
    const bool pass = scanTimer_ >= p_.scanPeriod;
    if (pass) scanTimer_ -= p_.scanPeriod;

    for (int i = 0; i < targets.count; ++i) {
        const FlightState& t = *targets.items[i].state;
        const bool focus = i == designated_;
        // The designated target is illuminated continuously; the rest once a pass.
        if (!focus && !pass) continue;
        Track& tr = tracks_[i];
        bool notched = false;
        // A rock between us hides it like the horizon would (docs/decisions/0042).
        if (sees(own, t, focus, &notched, targets.items[i].surface)) {
            if (targets.visible(own.position, t.position)) {
                tr.valid = true;
                tr.pos = t.position;
                tr.vel = t.velocity();
                tr.sinceReturn = 0.0f;
                tr.notched = false;
                tr.masked = false;
            } else {
                tr.masked = true;  // in the beam, but behind terrain
            }
        } else {
            tr.notched = notched;
        }
    }
    for (int i = 0; i < kMaxTargets; ++i) {
        Track& tr = tracks_[i];
        const float memory = tr.masked ? std::min(p_.memory, kTerrainMemory) : p_.memory;
        if (i >= targets.count || (tr.valid && tr.sinceReturn > memory)) tr = Track{};
    }
    if (designated_ >= 0 && !tracks_[designated_].valid) designated_ = -1;
    if (designated_ < 0 && auto_) autoDesignate(own);
}

namespace {
float offNose(const FlightState& own, const Vec3& p) {
    return math::dot(own.orientation.forward(), math::normalize(p - own.position));
}
}  // namespace

void Radar::autoDesignate(const FlightState& own) {
    // Ace Combat convention: with nothing selected, take the target nearest
    // the nose. Only fresh tracks - a coasting one is not worth handing over.
    float best = -2.0f;
    for (int i = 0; i < kMaxTargets; ++i) {
        const Track& tr = tracks_[i];
        if (!tr.valid || tr.sinceReturn > 0.0f) continue;
        const float c = offNose(own, tr.pos);
        if (c > best) {
            best = c;
            designated_ = i;
        }
    }
}

void Radar::cycle(const FlightState& own) {
    // Order valid tracks by angle off the nose; step to the next one after
    // the current designation (wrapping around).
    int order[kMaxTargets];
    float key[kMaxTargets];
    int n = 0;
    for (int i = 0; i < kMaxTargets; ++i) {
        if (!tracks_[i].valid) continue;
        const float c = offNose(own, tracks_[i].pos);
        int j = n++;
        for (; j > 0 && key[j - 1] < c; --j) {
            order[j] = order[j - 1];
            key[j] = key[j - 1];
        }
        order[j] = i;
        key[j] = c;
    }
    if (n == 0) {
        designated_ = -1;
        return;
    }
    int at = -1;
    for (int k = 0; k < n; ++k)
        if (order[k] == designated_) at = k;
    designated_ = order[(at + 1) % n];
}

}  // namespace game
