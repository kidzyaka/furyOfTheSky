#include "game/countermeasures.h"

#include <cmath>

namespace game {

namespace {
// Dispenser on top of the tail "stinger", aircraft body axes (+Z aft).
constexpr math::Vec3 kDispenser{0.0f, 0.35f, 9.4f};
constexpr float kDrag = 0.9f;  // 1/s: terminal fall speed g / kDrag ~ 11 m/s
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
    // Ejected up and to the side, a little aft: the classic fan of flares.
    math::Vec3 kick{side_ * (14.0f + 6.0f * jitter), 11.0f + 5.0f * jitter, 6.0f};
    Flare& f = flares_[next_];
    f.pos = a.position + a.orientation.rotate(kDispenser);
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
