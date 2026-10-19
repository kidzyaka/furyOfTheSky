#include "fx/weapon_fx.h"

#include <cmath>

namespace fx {

using game::Armament;
using game::Missile;
using math::Vec3;

namespace {
constexpr float kSmokeLife = 3.2f;     // s a puff of motor smoke stays visible
constexpr float kSmokeSpacing = 0.08f; // 40 points cover the life
}  // namespace

void WeaponEffects::reset() {
    marks_.clear();
    particles_.clear();
    for (Trail& t : smoke_) {
        t.clear();
        t.life = kSmokeLife;
        t.spacing = kSmokeSpacing;
        t.growth = 8.0f;
        t.color = rgba(236, 236, 232, 235);
    }
    for (int i = 0; i < Armament::kMaxMissiles; ++i) {
        trailOf_[i] = -1;
        prevAge_[i] = 0.0f;
    }
    nextTrail_ = 0;
    damageSmoke_.clear();
    damageSmoke_.life = 2.6f;
    damageSmoke_.spacing = 0.08f;
    damageSmoke_.growth = 6.0f;
    damageSmoke_.color = rgba(70, 68, 66, 200);
    muzzle_ = 0.0f;
}

void WeaponEffects::puff(const Vec3& pos, const Vec3& vel, float size, uint32_t color, float life) {
    Particle* p = particles_.spawn();
    if (!p) return;
    p->pos = pos;
    p->vel = vel;
    p->life = life;
    p->size0 = size;
    p->size1 = size * 3.0f;
    p->color0 = color;
    p->color1 = color & 0x00FFFFFFu;
    p->drag = 2.0f;
    p->sprite = Sprite::Smoke;
    p->blend = Blend::Alpha;
}

void WeaponEffects::burst(const Vec3& pos, const game::MissileParams* type) {
    // Warhead: a short white-hot flash, a small fireball, rod fragments flying
    // out at a few hundred m/s, then a dirty grey cloud that hangs there.
    const float k = type && type->lethalRadius > 5.0f ? 1.0f : 0.7f;  // R-77's 22 kg vs R-73's 7 kg
    if (Particle* p = particles_.spawn()) {
        p->pos = pos;
        p->life = 0.16f;
        p->size0 = 7.0f * k;
        p->size1 = 10.0f * k;
        p->color0 = rgba(255, 244, 214, 255);
        p->color1 = rgba(255, 170, 90, 0);
    }
    for (int i = 0; i < 8; ++i) {
        Particle* p = particles_.spawn();
        if (!p) break;
        p->pos = pos + Vec3{rng_.range(-1.5f, 1.5f), rng_.range(-1.5f, 1.5f), rng_.range(-1.5f, 1.5f)} * k;
        p->vel = Vec3{rng_.range(-20.0f, 20.0f), rng_.range(-20.0f, 20.0f), rng_.range(-20.0f, 20.0f)};
        p->life = rng_.range(0.35f, 0.7f);
        p->size0 = 2.0f * k;
        p->size1 = rng_.range(5.0f, 7.0f) * k;
        p->color0 = rgba(255, 196, 120, 230);
        p->color1 = rgba(150, 40, 10, 0);
        p->drag = 3.0f;
    }
    for (int i = 0; i < 12; ++i) {
        Particle* p = particles_.spawn();
        if (!p) break;
        Vec3 d = math::normalize(Vec3{rng_.range(-1.0f, 1.0f), rng_.range(-1.0f, 1.0f), rng_.range(-1.0f, 1.0f)});
        p->pos = pos;
        p->vel = d * rng_.range(150.0f, 300.0f);
        p->life = rng_.range(0.15f, 0.3f);
        p->size0 = 0.5f;
        p->size1 = 0.3f;
        p->color0 = rgba(255, 236, 190, 255);
        p->color1 = rgba(255, 140, 60, 0);
        p->drag = 2.0f;
    }
    for (int i = 0; i < 10; ++i) {
        Vec3 off{rng_.range(-3.0f, 3.0f), rng_.range(-3.0f, 3.0f), rng_.range(-3.0f, 3.0f)};
        Particle* p = particles_.spawn();
        if (!p) break;
        p->pos = pos + off * k;
        p->vel = off * 3.0f;
        p->life = rng_.range(2.5f, 3.6f);
        p->size0 = 2.5f * k;
        p->size1 = rng_.range(8.0f, 11.0f) * k;
        p->color0 = rgba(96, 92, 88, 200);
        p->color1 = rgba(150, 148, 146, 0);
        p->drag = 1.5f;
        p->lift = 1.0f;
        p->sprite = Sprite::Smoke;
        p->blend = Blend::Alpha;
    }
}

void WeaponEffects::splash(const Vec3& pos, float scale) {
    // A column of spray thrown up from the sea and falling back, mist
    // drifting off it, a ring running out and white water left behind
    // (docs/decisions/0044). A missile's warhead throws ~20-30 m (cube-root
    // scaling of a 250 kg torpedo's ~100 m column to a 7-22 kg warhead at the
    // surface - an estimate), a 30 mm round a few metres.
    const int n = scale > 0.5f ? 16 : 5;
    for (int i = 0; i < n; ++i) {
        Particle* p = particles_.spawn();
        if (!p) break;
        const bool mist = i % 3 == 2;
        p->pos = Vec3{pos.x + rng_.range(-2.0f, 2.0f) * scale, 0.5f, pos.z + rng_.range(-2.0f, 2.0f) * scale};
        p->vel = Vec3{rng_.range(-3.0f, 3.0f) * scale, rng_.range(14.0f, 26.0f) * std::sqrt(scale),
                      rng_.range(-3.0f, 3.0f) * scale};
        p->life = rng_.range(1.6f, 2.4f) * (0.5f + 0.5f * scale) * (mist ? 1.6f : 1.0f);
        p->size0 = 1.5f * scale + 0.4f;
        p->size1 = (mist ? 12.0f : 6.0f) * scale + 1.2f;
        p->color0 = rgba(236, 242, 246, mist ? 140 : 210);
        p->color1 = rgba(236, 242, 246, 0);
        p->drag = mist ? 1.8f : 0.7f;
        p->lift = mist ? -2.0f : -9.81f;
        p->sprite = Sprite::Smoke;
        p->blend = Blend::Alpha;
    }
    WaterMark ring;
    ring.kind = MarkKind::Ring;
    ring.x = pos.x;
    ring.z = pos.z;
    ring.r0 = 2.0f * scale + 0.5f;
    ring.r1 = 30.0f * scale + 3.0f;
    ring.life = 1.5f + 1.5f * scale;
    ring.alpha = 0.7f;
    marks_.add(ring);
    WaterMark foam;
    foam.x = pos.x;
    foam.z = pos.z;
    foam.r0 = 3.0f * scale + 0.6f;
    foam.r1 = 9.0f * scale + 1.5f;
    foam.life = 3.0f + 4.0f * scale;
    foam.alpha = 0.8f;
    marks_.add(foam);
}

void WeaponEffects::rockHit(const Vec3& pos, float scale) {
    // HE on rock (docs/decisions/0042): a flash, then a cloud of red-brown
    // dust and chips thrown off the face, falling back.
    if (scale > 0.5f) {
        if (Particle* p = particles_.spawn()) {
            p->pos = pos;
            p->life = 0.18f;
            p->size0 = 4.0f;
            p->size1 = 7.0f;
            p->color0 = rgba(255, 228, 180, 255);
            p->color1 = rgba(255, 130, 60, 0);
        }
    }
    const int dust = scale > 0.5f ? 10 : 3;
    for (int i = 0; i < dust; ++i)
        puff(pos + Vec3{rng_.range(-1.f, 1.f), rng_.range(-1.f, 1.f), rng_.range(-1.f, 1.f)} * (2.0f * scale),
             Vec3{rng_.range(-6.f, 6.f), rng_.range(-1.f, 8.f), rng_.range(-6.f, 6.f)} * scale, 2.5f * scale + 0.6f,
             rgba(124, 92, 74, 190), 1.6f + 1.4f * scale);
    const int chips = scale > 0.5f ? 10 : 3;
    for (int i = 0; i < chips; ++i) {
        Particle* p = particles_.spawn();
        if (!p) break;
        p->pos = pos;
        p->vel = Vec3{rng_.range(-25.f, 25.f), rng_.range(0.f, 30.f), rng_.range(-25.f, 25.f)} * scale;
        p->life = rng_.range(0.8f, 1.6f);
        p->size0 = 0.4f * scale + 0.15f;
        p->size1 = 0.3f * scale + 0.1f;
        p->color0 = rgba(70, 56, 48, 230);
        p->color1 = rgba(70, 56, 48, 0);
        p->drag = 0.5f;
        p->lift = -9.81f;
        p->sprite = Sprite::Smoke;
        p->blend = Blend::Alpha;
    }
}

void WeaponEffects::strike(const Vec3& pos, const Vec3& vel) {
    // 30 mm HE on an airframe: a flash, sparks carried on with the target, a puff.
    if (Particle* p = particles_.spawn()) {
        p->pos = pos;
        p->vel = vel;
        p->life = 0.1f;
        p->size0 = 2.6f;
        p->size1 = 3.4f;
        p->color0 = rgba(255, 236, 196, 255);
        p->color1 = rgba(255, 150, 80, 0);
    }
    for (int i = 0; i < 6; ++i) {
        Particle* p = particles_.spawn();
        if (!p) break;
        p->pos = pos;
        p->vel = vel + Vec3{rng_.range(-60.0f, 60.0f), rng_.range(-60.0f, 60.0f), rng_.range(-60.0f, 60.0f)};
        p->life = rng_.range(0.15f, 0.3f);
        p->size0 = 0.35f;
        p->size1 = 0.2f;
        p->color0 = rgba(255, 220, 160, 255);
        p->color1 = rgba(255, 120, 40, 0);
        p->drag = 3.0f;
    }
    puff(pos, vel * 0.8f, 1.2f, rgba(60, 58, 56, 170), 1.0f);
}

void WeaponEffects::update(const game::WeaponView& arm, const game::HitQueue& hits, const game::FlightState& aircraft,
                           const game::FlightState* target, float targetHealth, float dt) {
    // --- Missiles: smoke while the motor burns, puffs at ejection and light-up.
    int owner[kSmokeTrails];
    for (int t = 0; t < kSmokeTrails; ++t) owner[t] = -1;
    const int count = arm.missileCount < Armament::kMaxMissiles ? arm.missileCount : Armament::kMaxMissiles;
    for (int i = 0; i < count; ++i) {
        const Missile& m = arm.missiles[i];
        if (!m.live()) {
            trailOf_[i] = -1;
            continue;
        }
        if (trailOf_[i] < 0 || m.age < prevAge_[i]) {  // a new missile in this slot
            trailOf_[i] = nextTrail_;
            nextTrail_ = (nextTrail_ + 1) % kSmokeTrails;
            smoke_[trailOf_[i]].clear();
            prevAge_[i] = -1.0f;
            if (m.ignition > 0.0f)  // AKU-170 cartridge
                puff(m.pos, aircraft.velocity() * 0.9f, 0.7f, rgba(170, 168, 160, 150), 0.8f);
        }
        const bool lightUp = prevAge_[i] < m.ignition && m.age >= m.ignition;
        const Vec3 tail = m.pos - m.orientation.forward() * (0.5f * m.type->length);
        if (lightUp) {
            // The first instants of the burn: a dense cloud left at the launcher.
            for (int k = 0; k < 3; ++k)
                puff(tail, m.vel * 0.7f + Vec3{rng_.range(-3.f, 3.f), rng_.range(-3.f, 3.f), rng_.range(-3.f, 3.f)},
                     1.0f, rgba(230, 230, 226, 200), 1.6f);
        }
        prevAge_[i] = m.age;
        owner[trailOf_[i]] = i;
    }
    for (int t = 0; t < kSmokeTrails; ++t) {
        const int i = owner[t];
        if (i >= 0 && arm.missiles[i].burning()) {
            const Missile& m = arm.missiles[i];
            smoke_[t].update(m.pos - m.orientation.forward() * (0.5f * m.type->length), 0.85f, 0.55f, dt);
        } else {
            smoke_[t].update(smoke_[t].emitter(), 0.0f, 0.0f, dt);
        }
    }

    // --- Detonations.
    const game::WeaponEvents& ev = *arm.events;
    for (int i = 0; i < ev.ends; ++i) {
        const game::MissileResult& r = ev.end[i];
        if (r.end == game::MissileEnd::Water) {
            splash(r.pos, 1.0f);
        } else if (r.end == game::MissileEnd::Terrain) {
            rockHit(r.pos, 1.0f);
            burst(r.pos, r.type);
        } else {
            burst(r.pos, r.type);
        }
    }
    for (int i = 0; i < hits.count; ++i) {
        const game::HitEvent& h = hits.items[i];
        if (h.kind == game::HitKind::Shell) strike(h.pos, target ? target->velocity() : Vec3{});
    }

    // --- Gun: muzzle flash flicker and rounds into the sea.
    const game::Gun& gun = *arm.gun;
    muzzle_ = gun.fired() > 0 ? 0.6f + 0.4f * rng_.next() : muzzle_ * 0.3f;
    for (int i = 0; i < gun.splashes(); ++i) {
        // Above the waterline it was a rock, not the sea.
        if (gun.splash(i).y > 1.0f)
            rockHit(gun.splash(i), 0.25f);
        else
            splash(gun.splash(i), 0.25f);
    }

    // --- The target smokes once it's been hurt (it heals when "destroyed").
    if (target) {
        const float k = math::smoothstep(75.0f, 20.0f, targetHealth);
        damageSmoke_.update(target->position + target->orientation.rotate(Vec3{0.0f, 0.4f, 6.0f}), k * 0.8f,
                            1.2f, dt);
    }
    particles_.update(dt);
    marks_.update(dt);
}

}  // namespace fx
