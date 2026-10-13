#include "fx/aircraft_fx.h"

#include <algorithm>
#include <cmath>

namespace fx {

using math::approach;
using math::smoothstep;
using math::Vec3;

// Su-35S, measured off tools/models/su35s.py (Blender y -> game -z).
const AttachPoints kSu35SPoints = {
    /*nozzleExit*/ {1.15f, -0.24f, 9.70f},
    /*wingtip*/ {7.15f, -0.12f, 5.90f},
    /*vapor*/
    {{0.95f, 0.62f, -3.9f}, {1.30f, 0.60f, -2.6f}, {1.70f, 0.55f, -1.3f}, {2.30f, 0.48f, -0.1f},
     {3.20f, 0.40f, 1.1f}, {4.30f, 0.30f, 2.2f}, {5.40f, 0.20f, 3.2f}},
    /*vaporSize*/ {1.1f, 1.5f, 1.8f, 2.0f, 2.0f, 1.8f, 1.5f},
    /*vaporOnset*/ {0.0f, 0.0f, 0.05f, 0.15f, 0.3f, 0.5f, 0.65f},
};

void AircraftEffects::reset() {
    engine_ = EngineFx{};
    abLevel_ = vapor_ = cone_ = flash_ = 0.0f;
    prevAb_ = boom_ = wasCrashed_ = false;
    prevMach_ = boomCooldown_ = sprayAcc_ = time_ = 0.0f;
    particles_.clear();
    for (int i = 0; i < 2; ++i) {
        tips_[i].clear();
        tips_[i].life = 1.4f;
        tips_[i].spacing = 0.04f;
        tips_[i].growth = 2.5f;
        tips_[i].color = rgba(240, 244, 248);
        contrails_[i].clear();
        contrails_[i].life = 2.3f;
        contrails_[i].spacing = 0.06f;
        contrails_[i].growth = 5.0f;
        contrails_[i].color = rgba(246, 248, 250);
    }
    for (Trail& t : flareTrails_) {
        t.clear();
        t.life = 2.6f;
        t.spacing = 0.07f;
        t.growth = 6.0f;
        t.color = rgba(214, 214, 208);
    }
    for (ShockRing& r : rings_) r.age = -1.0f;
    streaksLive_ = false;
    streakAlpha_ = 0.0f;
}

Vec3 AircraftEffects::attach(const game::FlightState& s, Vec3 body, int side) const {
    if (side) body.x = -body.x;
    return s.position + s.orientation.rotate(body);
}

void AircraftEffects::update(const game::FlightState& s, const game::Countermeasures& cm, const Vec3& eye,
                             const Vec3& camForward, float dt) {
    time_ += dt;
    const float v = s.speed;
    const bool alive = !s.crashed;

    // --- Engines: afterburner light-up overshoots, then settles; dry power glows.
    if (s.afterburner && !prevAb_) engine_.ignition = 1.0f;
    prevAb_ = s.afterburner;
    float ab = alive && s.afterburner ? 1.0f : 0.0f;
    abLevel_ += (ab - abLevel_) * approach(ab > abLevel_ ? 7.0f : 4.0f, dt);
    engine_.ignition *= std::exp(-6.0f * dt);
    engine_.afterburner = abLevel_ * (1.0f + 0.35f * engine_.ignition);
    engine_.heat += ((alive ? s.throttle : 0.0f) - engine_.heat) * approach(2.0f, dt);
    engine_.flicker = rng_.next();

    // --- Condensation: needs airflow; appears with G and with AoA.
    const float flow = alive ? smoothstep(40.0f, 110.0f, v) : 0.0f;
    const float aoa = std::fabs(s.alpha);
    float vaporTarget = flow * std::max(smoothstep(4.5f, 9.5f, s.loadFactor), smoothstep(0.22f, 0.6f, aoa));
    vapor_ += (vaporTarget - vapor_) * approach(8.0f, dt);

    float tip = flow * std::max(smoothstep(3.0f, 7.5f, s.loadFactor), smoothstep(0.2f, 0.5f, aoa));
    // Contrails only high up, where the air is cold enough (arcade threshold).
    float contrail = flow * smoothstep(4000.0f, 5500.0f, s.position.y);
    for (int side = 0; side < 2; ++side) {
        tips_[side].update(attach(s, points_.wingtip, side), tip * 0.7f, 0.25f, dt);
        Vec3 exit = attach(s, points_.nozzleExit, side) + s.pathDir * -6.0f;  // condenses behind the jet
        contrails_[side].update(exit, contrail * 0.55f, 0.7f, dt);
    }

    // --- Transonic vapor cone and the boom.
    float m = s.mach;
    float coneTarget = alive ? smoothstep(0.93f, 0.99f, m) * (1.0f - smoothstep(1.01f, 1.08f, m)) : 0.0f;
    cone_ += (coneTarget - cone_) * approach(10.0f, dt);
    boomCooldown_ -= dt;
    if (alive && prevMach_ < 1.0f && m >= 1.0f && boomCooldown_ <= 0.0f) boom(s);
    prevMach_ = m;
    flash_ *= std::exp(-7.0f * dt);
    for (ShockRing& r : rings_)
        if (r.age >= 0.0f && (r.age += dt) > kRingLife) r.age = -1.0f;

    // --- Flares: a smoke ribbon per slot, a pop at ejection.
    for (int i = 0; i < game::Countermeasures::kMaxFlares; ++i) {
        const game::Flare& f = cm.flare(i);
        float burn = f.burning ? 1.0f - smoothstep(3.2f, 4.0f, f.age) : 0.0f;
        flareTrails_[i].update(f.pos, burn * 0.55f, 0.45f, dt);
    }
    if (cm.justReleased() >= 0) {
        if (Particle* p = particles_.spawn()) {
            p->pos = cm.flare(cm.justReleased()).pos;
            p->vel = s.velocity();
            p->life = 0.25f;
            p->size0 = 1.2f;
            p->size1 = 3.0f;
            p->color0 = rgba(255, 240, 200, 255);
            p->color1 = rgba(255, 180, 90, 0);
            p->drag = 3.0f;
        }
    }

    if (alive) spray(s, dt);
    if (s.crashed && !wasCrashed_) explode(s);
    wasCrashed_ = s.crashed;
    updateStreaks(s, eye, camForward);
    particles_.update(dt);
}

void AircraftEffects::boom(const game::FlightState& s) {
    boom_ = true;
    boomCooldown_ = 2.5f;
    flash_ = 1.0f;
    for (ShockRing& r : rings_) {
        if (r.age < 0.0f) {
            r.age = 0.0f;
            break;
        }
    }
    // A puff of condensation left hanging in the air where the shock passed.
    Vec3 right = s.orientation.right(), up = s.orientation.up();
    for (int i = 0; i < 10; ++i) {
        Particle* p = particles_.spawn();
        if (!p) break;
        float a = i * (2.0f * math::kPi / 10.0f);
        Vec3 radial = right * std::cos(a) + up * std::sin(a);
        p->pos = s.position + radial * 3.0f;
        // Mostly carried along, so the burst stays around the aircraft for a moment.
        p->vel = s.velocity() * 0.85f + radial * 18.0f;
        p->life = rng_.range(0.9f, 1.4f);
        p->size0 = 2.5f;
        p->size1 = 7.0f;
        p->color0 = rgba(242, 246, 250, 150);
        p->color1 = rgba(242, 246, 250, 0);
        p->drag = 2.5f;
        p->sprite = Sprite::Smoke;
        p->blend = Blend::Alpha;
    }
}

void AircraftEffects::explode(const game::FlightState& s) {
    // Fireball over a column of spray where the aircraft hit the water.
    for (int i = 0; i < 14; ++i) {
        Particle* p = particles_.spawn();
        if (!p) break;
        p->pos = s.position + Vec3{rng_.range(-4.0f, 4.0f), rng_.range(0.0f, 6.0f), rng_.range(-4.0f, 4.0f)};
        p->vel = Vec3{rng_.range(-14.0f, 14.0f), rng_.range(4.0f, 26.0f), rng_.range(-14.0f, 14.0f)};
        p->life = rng_.range(0.5f, 1.1f);
        p->size0 = rng_.range(2.0f, 3.0f);
        p->size1 = rng_.range(6.0f, 9.0f);
        p->color0 = rgba(255, 190, 110, 170);
        p->color1 = rgba(160, 40, 10, 0);
        p->drag = 2.0f;
    }
    for (int i = 0; i < 26; ++i) {
        Particle* p = particles_.spawn();
        if (!p) break;
        float a = rng_.range(0.0f, 2.0f * math::kPi);
        p->pos = s.position + Vec3{std::cos(a) * 3.0f, 1.0f, std::sin(a) * 3.0f};
        p->vel = Vec3{std::cos(a) * rng_.range(6.0f, 18.0f), rng_.range(20.0f, 45.0f), std::sin(a) * rng_.range(6.0f, 18.0f)};
        p->life = rng_.range(1.6f, 2.6f);
        p->size0 = 3.0f;
        p->size1 = rng_.range(10.0f, 16.0f);
        p->color0 = rgba(240, 244, 248, 210);
        p->color1 = rgba(220, 228, 236, 0);
        p->drag = 1.2f;
        p->lift = -9.0f;
        p->sprite = Sprite::Smoke;
        p->blend = Blend::Alpha;
    }
}

void AircraftEffects::spray(const game::FlightState& s, float dt) {
    // Rooster tail on the sea surface when flying very low and fast.
    float k = smoothstep(35.0f, 6.0f, s.position.y) * smoothstep(90.0f, 250.0f, s.speed);
    sprayAcc_ += k * 45.0f * dt;
    Vec3 flat = math::normalize(Vec3{s.pathDir.x, 0.0f, s.pathDir.z});
    Vec3 side{-flat.z, 0.0f, flat.x};
    while (sprayAcc_ >= 1.0f) {
        sprayAcc_ -= 1.0f;
        Particle* p = particles_.spawn();
        if (!p) break;
        p->pos = Vec3{s.position.x, 0.4f, s.position.z} - flat * rng_.range(4.0f, 14.0f) +
                 side * rng_.range(-3.0f, 3.0f);
        p->vel = flat * (s.speed * rng_.range(0.05f, 0.15f)) + side * rng_.range(-5.0f, 5.0f) +
                 Vec3{0.0f, rng_.range(6.0f, 15.0f), 0.0f};
        p->life = rng_.range(1.1f, 1.7f);
        p->size0 = 1.6f;
        p->size1 = 7.5f;
        p->color0 = rgba(236, 242, 246, 170);
        p->color1 = rgba(236, 242, 246, 0);
        p->drag = 1.4f;
        p->lift = -7.0f;
        p->sprite = Sprite::Smoke;
        p->blend = Blend::Alpha;
    }
}

void AircraftEffects::updateStreaks(const game::FlightState& s, const Vec3& eye, const Vec3& fwd) {
    // Motes of air around the camera, drawn as motion-blurred lines: the only
    // speed reference over an empty sea.
    streakAlpha_ = s.crashed ? 0.0f : smoothstep(170.0f, 420.0f, s.speed);
    Vec3 right = math::normalize(math::cross(fwd, math::kUp));
    Vec3 up = math::cross(right, fwd);
    for (int i = 0; i < kStreaks; ++i) {
        Vec3 rel = streaks_[i] - eye;
        float depth = math::dot(rel, fwd);
        if (streaksLive_ && depth > 3.0f && math::dot(rel, rel) < 110.0f * 110.0f) continue;
        // Respawn ahead, never right in the middle where the aircraft is.
        float x = rng_.range(-1.0f, 1.0f), y = rng_.range(-1.0f, 1.0f);
        if (std::fabs(x) < 0.25f && std::fabs(y) < 0.25f) x = x < 0 ? -0.6f : 0.6f;
        float d = streaksLive_ ? rng_.range(70.0f, 100.0f) : rng_.range(10.0f, 100.0f);
        streaks_[i] = eye + fwd * d + right * (x * d * 0.55f) + up * (y * d * 0.32f);
    }
    streaksLive_ = true;
}

}  // namespace fx
