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
    /*wingRoot*/ {0.90f, 0.24f, -1.58f, 5.98f},
    /*wingTip*/ {7.05f, -0.09f, 3.96f, 1.60f},
    /*canopyZ*/ -5.5f,
};

void AircraftEffects::reset() {
    engine_ = EngineFx{};
    abLevel_ = vapor_ = transonic_ = shock_ = 0.0f;
    prevAb_ = machOne_ = wasCrashed_ = false;
    prevMach_ = sprayAcc_ = time_ = 0.0f;
    washAcc_ = washMark_ = wakeMark_ = 0.0f;
    particles_.clear();
    marks_.clear();
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

    // --- Condensation (docs/decisions/0020): water condenses where the flow
    // over the airframe is accelerated and its pressure (and temperature)
    // drops below the dew point. The suction that does it is what makes the
    // lift, so it follows the aerodynamic load (G) - not the angle of attack:
    // at 200 km/h even 60 deg of AoA makes little lift and no cloud. Speed is
    // gated too (dynamic pressure), and humid low air condenses far more easily.
    const float humid = 1.0f - 0.8f * smoothstep(3000.0f, 9000.0f, s.position.y);
    const float q = alive ? smoothstep(85.0f, 140.0f, v) : 0.0f;  // ~300..500 km/h
    float vaporTarget = q * humid * smoothstep(4.5f, 9.0f, s.loadFactor);
    vapor_ += (vaporTarget - vapor_) * approach(8.0f, dt);

    // Wingtip vortex cores: the pressure drop scales with lift (circulation) too.
    float tip = smoothstep(80.0f, 130.0f, v) * humid * smoothstep(3.5f, 8.0f, s.loadFactor) * (alive ? 1.0f : 0.0f);
    // Contrails only high up, where the air is cold enough (arcade threshold).
    float contrail = (alive ? smoothstep(40.0f, 110.0f, v) : 0.0f) * smoothstep(4000.0f, 5500.0f, s.position.y);
    for (int side = 0; side < 2; ++side) {
        tips_[side].update(attach(s, points_.wingtip, side), tip * 0.7f, 0.25f, dt);
        Vec3 exit = attach(s, points_.nozzleExit, side) + s.pathDir * -6.0f;  // condenses behind the jet
        contrails_[side].update(exit, contrail * 0.55f, 0.7f, dt);
    }

    // --- Transonic condensation. Around M 0.9 the flow over the canopy and the
    // wings goes locally supersonic and ends in a shock; between the expansion
    // and the shock it is cold enough to condense, at the shock it re-heats and
    // the cloud ends abruptly. As M -> 1 the shocks slide aft to the trailing
    // edges; past ~M 1.03 the flow is supersonic everywhere and the cloud is gone.
    // There is no flash or visible ring at Mach 1.
    const float m = s.mach;
    float transonicTarget = alive ? humid * smoothstep(0.88f, 0.94f, m) * (1.0f - smoothstep(1.0f, 1.035f, m)) : 0.0f;
    float shockTarget = smoothstep(0.88f, 1.0f, m) + 1.5f * smoothstep(1.0f, 1.035f, m);
    transonic_ += (transonicTarget - transonic_) * approach(6.0f, dt);
    shock_ += (shockTarget - shock_) * approach(6.0f, dt);
    if (alive && prevMach_ < 1.0f && m >= 1.0f) machOne_ = true;
    prevMach_ = m;

    // --- Flares: a smoke ribbon per slot, from the moment the flare ignites
    // (a few metres clear of the airframe, as the real cartridge does).
    for (int i = 0; i < game::Countermeasures::kMaxFlares; ++i) {
        const game::Flare& f = cm.flare(i);
        float burn = f.lit() ? 1.0f - smoothstep(3.2f, game::Countermeasures::kBurnTime, f.age) : 0.0f;
        flareTrails_[i].update(f.pos, burn * 0.7f, 0.6f, dt);
    }

    if (alive) spray(s, dt);
    if (s.crashed && !wasCrashed_) explode(s);
    wasCrashed_ = s.crashed;
    updateStreaks(s, eye, camForward);
    particles_.update(dt);
    marks_.update(dt);
}

void AircraftEffects::explode(const game::FlightState& s) {
    if (s.position.y > 15.0f) {
        // Shot down in the air: the fuel goes up in a fireball that is carried
        // on along the old flight path, burning debris falls out of it, and a
        // dark cloud is left hanging.
        const Vec3 drift = s.velocity() * 0.35f;
        for (int i = 0; i < 18; ++i) {
            Particle* p = particles_.spawn();
            if (!p) break;
            p->pos = s.position + Vec3{rng_.range(-5.0f, 5.0f), rng_.range(-3.0f, 3.0f), rng_.range(-5.0f, 5.0f)};
            p->vel = drift + Vec3{rng_.range(-18.0f, 18.0f), rng_.range(-10.0f, 14.0f), rng_.range(-18.0f, 18.0f)};
            p->life = rng_.range(0.7f, 1.4f);
            p->size0 = rng_.range(3.0f, 4.5f);
            p->size1 = rng_.range(9.0f, 14.0f);
            p->color0 = rgba(255, 196, 120, 220);
            p->color1 = rgba(150, 40, 10, 0);
            p->drag = 1.5f;
        }
        for (int i = 0; i < 12; ++i) {
            Particle* p = particles_.spawn();
            if (!p) break;
            p->pos = s.position;
            p->vel = drift + math::normalize(Vec3{rng_.range(-1.f, 1.f), rng_.range(-0.6f, 1.f), rng_.range(-1.f, 1.f)}) *
                                 rng_.range(40.0f, 90.0f);
            p->life = rng_.range(1.5f, 2.6f);
            p->size0 = 0.8f;
            p->size1 = 0.3f;
            p->color0 = rgba(255, 210, 140, 255);
            p->color1 = rgba(255, 90, 30, 0);
            p->drag = 0.6f;
            p->lift = -9.81f;  // burning debris falls
        }
        for (int i = 0; i < 20; ++i) {
            Particle* p = particles_.spawn();
            if (!p) break;
            p->pos = s.position + Vec3{rng_.range(-6.0f, 6.0f), rng_.range(-4.0f, 4.0f), rng_.range(-6.0f, 6.0f)};
            p->vel = drift * 0.6f + Vec3{rng_.range(-6.0f, 6.0f), rng_.range(-2.0f, 6.0f), rng_.range(-6.0f, 6.0f)};
            p->life = rng_.range(3.5f, 5.0f);
            p->size0 = 4.0f;
            p->size1 = rng_.range(14.0f, 20.0f);
            p->color0 = rgba(60, 56, 52, 210);
            p->color1 = rgba(120, 118, 116, 0);
            p->drag = 0.8f;
            p->lift = 1.0f;
            p->sprite = Sprite::Smoke;
            p->blend = Blend::Alpha;
        }
        return;
    }
    // Fireball over a column of spray where the aircraft hit the water, a
    // wide patch of white water and rings running out from it.
    for (int i = 0; i < 3; ++i) {
        WaterMark m;
        m.kind = i == 0 ? MarkKind::Foam : MarkKind::Ring;
        m.x = s.position.x;
        m.z = s.position.z;
        m.r0 = 6.0f + 4.0f * i;
        m.r1 = i == 0 ? 22.0f : 45.0f + 25.0f * i;
        m.life = i == 0 ? 9.0f : 3.0f + 1.5f * i;
        m.alpha = i == 0 ? 0.9f : 0.6f;
        marks_.add(m);
    }
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

void AircraftEffects::downwash(const Vec3& c, float radius, float strength, float dt) {
    // The blast spreads as a radial jet over the surface and tears spray off
    // it (docs/decisions/0044): droplets and mist thrown outwards from the
    // rim, rings running out over the water, white water under it.
    if (strength < 0.02f) return;
    washAcc_ += strength * 70.0f * dt;
    while (washAcc_ >= 1.0f) {
        washAcc_ -= 1.0f;
        Particle* p = particles_.spawn();
        if (!p) break;
        const float a = rng_.range(0.0f, 2.0f * math::kPi);
        const Vec3 out{std::cos(a), 0.0f, std::sin(a)};
        const float r = radius * rng_.range(0.4f, 1.1f);
        p->pos = Vec3{c.x, 0.4f, c.z} + out * r;
        const bool mist = rng_.next() < 0.6f;
        p->vel = out * rng_.range(5.0f, 15.0f) * (0.6f + 0.6f * strength) + Vec3{0.0f, rng_.range(1.5f, 7.0f), 0.0f};
        p->life = mist ? rng_.range(1.2f, 2.0f) : rng_.range(0.6f, 1.1f);
        p->size0 = mist ? 1.2f + 0.08f * radius : 0.5f;
        p->size1 = mist ? 3.0f + 0.25f * radius : 0.9f;
        p->color0 = rgba(232, 238, 242, mist ? 120 : 200);
        p->color1 = rgba(232, 238, 242, 0);
        p->drag = mist ? 1.6f : 0.4f;
        p->lift = mist ? -1.5f : -9.81f;
        p->sprite = Sprite::Smoke;
        p->blend = Blend::Alpha;
    }
    if ((washMark_ -= dt) <= 0.0f) {
        washMark_ = 0.18f;
        WaterMark ring;
        ring.kind = MarkKind::Ring;
        ring.x = c.x;
        ring.z = c.z;
        ring.r0 = radius * 0.7f;
        ring.r1 = radius * 2.8f;
        ring.life = 1.4f;
        ring.alpha = 0.55f * strength;
        marks_.add(ring);
        WaterMark foam;
        foam.x = c.x + rng_.range(-0.2f, 0.2f) * radius;
        foam.z = c.z + rng_.range(-0.2f, 0.2f) * radius;
        foam.r0 = radius * 0.8f;
        foam.r1 = radius * 1.6f;
        foam.life = 2.5f;
        foam.alpha = 0.7f * strength;
        marks_.add(foam);
    }
}

void AircraftEffects::spray(const game::FlightState& s, float dt) {
    if (s.position.y > 80.0f) {
        sprayAcc_ = 0.0f;
        return;
    }
    // 1. The jets hitting the water. Each nozzle's exhaust runs aft along the
    // engine axis; where that line meets the sea within the jet's reach - a
    // few tens of metres, more on the burner - it blasts the surface. Nose up
    // near the water (a low cobra, a hover in the post-stall) aims it down.
    const float thrust = math::clamp(engine_.heat + 0.8f * engine_.afterburner, 0.0f, 1.8f);
    const Vec3 aft = s.orientation.rotate(Vec3{0.0f, 0.0f, 1.0f});
    if (aft.y < -0.05f && thrust > 0.05f) {
        const float reach = 25.0f + 30.0f * thrust;
        for (int side = 0; side < 2; ++side) {
            const Vec3 exit = attach(s, points_.nozzleExit, side);
            const float t = exit.y / -aft.y;
            if (t <= 0.0f || t > reach) continue;
            const Vec3 at = exit + aft * t;
            const float k = (1.0f - t / reach) * (1.0f - t / reach) * std::min(thrust, 1.0f);
            downwash({at.x, 0.0f, at.z}, 2.5f + 0.18f * t, 0.6f * k, dt);
        }
    }

    // 2. A fast pass a few metres up: the jets and the wing's downwash sweep
    // the surface behind - a rooster tail of spray, a streak of white water.
    float k = smoothstep(30.0f, 5.0f, s.position.y) * smoothstep(90.0f, 250.0f, s.speed);
    if (k < 0.01f) return;
    sprayAcc_ += k * 55.0f * dt;
    Vec3 flat = math::normalize(Vec3{s.pathDir.x, 0.0f, s.pathDir.z});
    Vec3 side{-flat.z, 0.0f, flat.x};
    while (sprayAcc_ >= 1.0f) {
        sprayAcc_ -= 1.0f;
        Particle* p = particles_.spawn();
        if (!p) break;
        // Thrown up behind and to the sides; the mist climbs ~10-15 m (the
        // reported plumes of very low passes) and hangs, the drops fall back.
        const bool mist = rng_.next() < 0.6f;
        p->pos = Vec3{s.position.x, 0.4f, s.position.z} - flat * rng_.range(-2.0f, 16.0f) +
                 side * rng_.range(-7.0f, 7.0f);
        p->vel = flat * (s.speed * rng_.range(0.12f, 0.3f)) + side * rng_.range(-9.0f, 9.0f) +
                 Vec3{0.0f, rng_.range(9.0f, 20.0f) * (0.5f + 0.5f * k), 0.0f};
        p->life = mist ? rng_.range(1.4f, 2.4f) : rng_.range(0.8f, 1.3f);
        p->size0 = mist ? 2.2f : 0.7f;
        p->size1 = mist ? 10.0f : 1.4f;
        p->color0 = rgba(236, 242, 246, mist ? 150 : 210);
        p->color1 = rgba(236, 242, 246, 0);
        p->drag = mist ? 1.4f : 0.5f;
        p->lift = mist ? -4.0f : -9.81f;
        p->sprite = Sprite::Smoke;
        p->blend = Blend::Alpha;
    }
    if ((wakeMark_ -= dt) <= 0.0f) {
        wakeMark_ = 0.08f;
        WaterMark m;
        m.x = s.position.x;
        m.z = s.position.z;
        m.dirX = flat.x;
        m.dirZ = flat.z;
        m.r0 = 3.0f;
        m.r1 = 7.0f;
        m.stretch = 3.0f;
        m.life = 3.0f;
        m.alpha = 0.6f * k;
        marks_.add(m);
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
