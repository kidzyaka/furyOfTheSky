#include "fx/ship_fx.h"

#include <cmath>

namespace fx {

using math::Vec3;

void ShipEffects::reset() {
    particles_.clear();
    marks_.clear();
    smoke_.clear();
    smoke_.life = 6.0f;
    smoke_.spacing = 0.15f;
    smoke_.growth = 9.0f;
    smoke_.color = rgba(52, 50, 48);
    wakeTimer_ = waveTimer_ = sprayAcc_ = fireAcc_ = 0.0f;
}

void ShipEffects::update(const game::Ship& ship, float dt) {
    const game::FlightState& s = ship.state();
    const Vec3 dir = s.pathDir;
    const Vec3 side{-dir.z, 0.0f, dir.x};
    const float half = ship.params().hull.bodyHalf.z;
    const float speed = s.speed;
    const float k = math::smoothstep(2.0f, 14.0f, speed);
    const Vec3 c{s.position.x, 0.0f, s.position.z};

    if (ship.afloat() && speed > 1.0f) {
        // The wake: white water churned by the screws, widening astern and
        // lingering for tens of seconds.
        if ((wakeTimer_ -= dt) <= 0.0f) {
            wakeTimer_ = 0.3f;
            WaterMark m;
            const Vec3 at = c - dir * (half + 2.0f);
            m.x = at.x;
            m.z = at.z;
            m.dirX = dir.x;
            m.dirZ = dir.z;
            m.r0 = 3.5f;
            m.r1 = 13.0f;
            m.stretch = 2.2f;
            m.life = 14.0f;
            m.alpha = 0.55f * k;
            marks_.add(m);
        }
        // The bow wave: rings leaving the stem, the diverging waves of the wake.
        if ((waveTimer_ -= dt) <= 0.0f) {
            waveTimer_ = 1.1f;
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                WaterMark m;
                m.kind = MarkKind::Ring;
                const Vec3 at = c + dir * (half * 0.7f) + side * (sgn * 4.0f);
                m.x = at.x;
                m.z = at.z;
                m.r0 = 4.0f;
                m.r1 = 40.0f;
                m.life = 5.0f;
                m.alpha = 0.3f * k;
                marks_.add(m);
            }
        }
        // Spray off the stem at speed.
        sprayAcc_ += k * k * 18.0f * dt;
        while (sprayAcc_ >= 1.0f) {
            sprayAcc_ -= 1.0f;
            Particle* p = particles_.spawn();
            if (!p) break;
            const float sgn = rng_.next() < 0.5f ? -1.0f : 1.0f;
            p->pos = c + dir * (half - 2.0f) + side * (sgn * 1.5f) + Vec3{0.0f, 1.0f, 0.0f};
            p->vel = dir * (speed * 0.6f) + side * (sgn * rng_.range(3.0f, 7.0f)) + Vec3{0.0f, rng_.range(2.0f, 5.0f), 0.0f};
            p->life = rng_.range(0.8f, 1.4f);
            p->size0 = 1.0f;
            p->size1 = 3.5f;
            p->color0 = rgba(236, 240, 242, 150);
            p->color1 = rgba(236, 240, 242, 0);
            p->drag = 1.4f;
            p->lift = -6.0f;
            p->sprite = Sprite::Smoke;
            p->blend = Blend::Alpha;
        }
    }

    // Hurt: black smoke from the superstructure, fire once badly hit or sinking.
    const float hurt = ship.afloat() ? math::smoothstep(70.0f, 20.0f, ship.health()) : 0.0f;
    const float sinking = ship.alive() ? 0.0f : 1.0f - math::smoothstep(0.7f, 1.0f, ship.sinkProgress());
    smoke_.update(s.position + Vec3{0.0f, 7.0f, 0.0f}, std::fmax(hurt, sinking) * 0.9f, 4.0f, dt);
    fireAcc_ += (math::smoothstep(40.0f, 0.0f, ship.health()) * (ship.afloat() ? 1.0f : 0.0f)) * 14.0f * dt;
    while (fireAcc_ >= 1.0f) {
        fireAcc_ -= 1.0f;
        Particle* p = particles_.spawn();
        if (!p) break;
        p->pos = s.position + side * rng_.range(-3.0f, 3.0f) + dir * rng_.range(-10.0f, 8.0f) + Vec3{0.0f, 2.0f, 0.0f};
        p->vel = Vec3{rng_.range(-1.5f, 1.5f), rng_.range(4.0f, 9.0f), rng_.range(-1.5f, 1.5f)};
        p->life = rng_.range(0.5f, 1.0f);
        p->size0 = 2.5f;
        p->size1 = 4.5f;
        p->color0 = rgba(255, 170, 80, 220);
        p->color1 = rgba(180, 50, 10, 0);
        p->drag = 0.6f;
        p->lift = 2.0f;
    }
    // Going down: white water round the hull.
    if (!ship.alive() && ship.afloat() && (wakeTimer_ -= dt) <= 0.0f) {
        wakeTimer_ = 0.5f;
        WaterMark m;
        m.x = c.x + side.x * rng_.range(-6.0f, 6.0f);
        m.z = c.z + side.z * rng_.range(-6.0f, 6.0f);
        m.dirX = dir.x;
        m.dirZ = dir.z;
        m.r0 = 10.0f;
        m.r1 = 22.0f;
        m.stretch = 2.0f;
        m.life = 8.0f;
        m.alpha = 0.6f;
        marks_.add(m);
    }
    particles_.update(dt);
    marks_.update(dt);
}

}  // namespace fx
