#include "game/countermeasures.h"

#include <cmath>

namespace game {

namespace {
// Dispenser blocks on top of the rear fuselage between the nozzles (early
// Su-35S layout; serial aircraft carry them underneath - docs/decisions/0022),
// right one; the left mirrors X. Body axes, +Z aft.
constexpr math::Vec3 kDispenser{0.16f, 0.36f, 8.7f};
constexpr float kDrag = 0.25f;  // 1/s, softened from a real pellet's ~2: docs/decisions/0022
}  // namespace

void Countermeasures::reset() {
    for (Flare& f : flares_) f = Flare{};
    stock_ = kStock;
    salvoLeft_ = 0;
    timer_ = 0.0f;
    side_ = 1;
    next_ = 0;
    justReleased_ = -1;
}

void Countermeasures::release(const FlightState& a) {
    rng_ = rng_ * 1664525u + 1013904223u;
    float jitter = static_cast<float>(rng_ >> 8) * (1.0f / 16777216.0f);
    // Kicked out of the canted blocks sideways and up (cartridge ejection
    // ~25 m/s): the salvo fans out either side of the tail, in view of the
    // chase camera, before the drag drops it behind.
    math::Vec3 kick{side_ * (20.0f + 8.0f * jitter), 11.0f + 4.0f * jitter, 0.0f};
    Flare& f = flares_[next_];
    f.pos = a.position + a.orientation.rotate(math::Vec3{side_ * kDispenser.x, kDispenser.y, kDispenser.z});
    f.vel = a.velocity() + a.orientation.rotate(kick);
    f.age = 0.0f;
    f.burning = true;
    justReleased_ = next_;
    next_ = (next_ + 1) % kMaxFlares;
    side_ = -side_;
    --stock_;
}

void Countermeasures::update(const FlightState& aircraft, bool trigger, float dt) {
    justReleased_ = -1;
    for (Flare& f : flares_) {
        if (!f.burning) continue;
        f.age += dt;
        f.vel *= 1.0f / (1.0f + kDrag * dt);
        f.vel.y -= math::kGravity * dt;
        f.pos += f.vel * dt;
        if (f.age >= kBurnTime || f.pos.y <= 0.0f) f.burning = false;
    }

    timer_ -= dt;
    if (salvoLeft_ == 0 && trigger && timer_ <= 0.0f && stock_ > 0 && !aircraft.crashed) salvoLeft_ = kSalvo;
    if (salvoLeft_ > 0 && timer_ <= 0.0f) {
        if (stock_ > 0) release(aircraft);
        --salvoLeft_;
        timer_ = salvoLeft_ > 0 && stock_ > 0 ? kInterval : kCooldown;
        if (stock_ == 0) salvoLeft_ = 0;
    }
}

}  // namespace game
