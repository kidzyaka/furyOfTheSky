#include "game/gun.h"

#include <algorithm>
#include <cmath>

namespace game {

using math::kGravity;
using math::Vec3;

const GunParams kGSh301 = {
    /*muzzle*/ {0.95f, 0.18f, -4.6f},
    /*muzzleVelocity*/ 860.0f,
    /*rate*/ 25.0f,
    /*rounds*/ 150,
    /*dispersion*/ 0.004f,
    /*dragK*/ 3.3e-4f,
    /*life*/ 4.0f,
    /*damage*/ 40.0f,
};

void Gun::reset() {
    for (Shell& s : shells_) s = Shell{};
    next_ = 0;
    rounds_ = p_.rounds;
    timer_ = 0.0f;
    firing_ = false;
    fired_ = 0;
    splashCount_ = 0;
}

Vec3 Gun::shellDirection(const FlightState& a) const {
    return math::normalize(a.velocity() + a.orientation.forward() * p_.muzzleVelocity);
}

void Gun::fire(const FlightState& a) {
    auto rnd = [this] {
        rng_ = rng_ * 1664525u + 1013904223u;
        return static_cast<float>(rng_ >> 8) * (1.0f / 8388608.0f) - 1.0f;  // -1..1
    };
    const Vec3 bore = a.orientation.forward() + a.orientation.right() * (rnd() * p_.dispersion) +
                      a.orientation.up() * (rnd() * p_.dispersion);
    Shell& s = shells_[next_];
    s.pos = a.position + a.orientation.rotate(p_.muzzle);
    s.vel = a.velocity() + math::normalize(bore) * p_.muzzleVelocity;
    s.age = 0.0f;
    s.live = true;
    next_ = (next_ + 1) % kMaxShells;
    --rounds_;
    ++fired_;
}

void Gun::update(const FlightState& a, bool trigger, const TargetList& targets, HitQueue& hits, float dt) {
    fired_ = 0;
    splashCount_ = 0;
    const float interval = 1.0f / p_.rate;
    const bool want = trigger && rounds_ > 0 && !a.crashed;
    if (want && !firing_) timer_ = interval;  // the first round leaves at once
    firing_ = want;
    if (firing_) {
        timer_ += dt;
        while (timer_ >= interval && rounds_ > 0) {
            timer_ -= interval;
            fire(a);
        }
    }

    for (Shell& s : shells_) {
        if (!s.live) continue;
        const Vec3 p0 = s.pos;
        const float v = math::length(s.vel);
        s.vel *= 1.0f / (1.0f + p_.dragK * v * dt);
        s.vel.y -= kGravity * dt;
        s.pos += s.vel * dt;
        s.age += dt;
        // Swept against each airframe, in its moving frame: shift the start by
        // the target's own motion over the step.
        for (int i = 0; i < targets.count && s.live; ++i) {
            const FlightState& t = *targets.items[i].state;
            const Vec3 start = p0 + t.velocity() * dt;
            const float f = segmentHit(t, shapeOf(targets.items[i]), start, s.pos);
            if (f >= 0.0f) {
                hits.push({HitKind::Shell, i, math::lerp(start, s.pos, f), p_.damage});
                s.live = false;
            }
        }
        if (!s.live) continue;
        // Into a rock: reported with the splashes (above the sea, the effects
        // throw rock dust instead of spray).
        if (targets.terrain) {
            const float f = targets.terrain->segment(p0, s.pos);
            if (f >= 0.0f) {
                if (splashCount_ < kMaxSplash) splash_[splashCount_++] = math::lerp(p0, s.pos, f);
                s.live = false;
                continue;
            }
        }
        if (s.pos.y <= 0.0f) {
            if (splashCount_ < kMaxSplash) {
                const float f = p0.y / std::max(p0.y - s.pos.y, 1e-3f);
                splash_[splashCount_++] = math::lerp(p0, s.pos, f);
            }
            s.live = false;
        } else if (s.age > p_.life) {
            s.live = false;
        }
    }
}

bool Gun::lead(const FlightState& a, const Vec3& targetPos, const Vec3& targetVel, Vec3& outDir,
               float& outTime) const {
    // Rounds slow down as v' = -k v^2: distance flown s(t) = ln(1 + k v0 t) / k,
    // so the time to reach distance d is (e^{k d} - 1) / (k v0). Iterate on
    // the target's future position (and the drop) - converges in a few passes.
    const Vec3 muzzle = a.position + a.orientation.rotate(p_.muzzle);
    const float v0 = math::length(a.velocity() + a.orientation.forward() * p_.muzzleVelocity);
    const float k = p_.dragK;
    float t = math::length(targetPos - muzzle) / v0;
    Vec3 aim;
    for (int i = 0; i < 4; ++i) {
        aim = targetPos + targetVel * t - muzzle + Vec3{0.0f, 0.5f * kGravity * t * t, 0.0f};
        const float d = math::length(aim);
        if (k * d > 8.0f) return false;
        t = (std::exp(k * d) - 1.0f) / (k * v0);
        if (t > p_.life) return false;
    }
    outDir = math::normalize(aim);
    outTime = t;
    return true;
}

}  // namespace game
