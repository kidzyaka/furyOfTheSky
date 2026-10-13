#pragma once

#include <cstdint>

#include "game/flight_model.h"

namespace game {

// IR flare decoy. Lives in the game layer: missiles (M2) will be seduced by it.
struct Flare {
    math::Vec3 pos, vel;   // world
    float age = 0.0f;      // s since ejection
    bool burning = false;  // in flight (ejected and not burnt out)

    // The pyrotechnic pellet ignites a moment after the cartridge kicks it out,
    // a few metres clear of the airframe: only then is it hot and visible.
    bool lit() const;
};

// Flare dispensers (docs/decisions/0017, 0022). Holding the button fires salvos of
// kSalvo flares, alternating between two blocks on top of the rear fuselage,
// kicked up and outwards; flares inherit the aircraft's velocity, then drag
// makes them fall behind. Deterministic (own LCG) like the rest of the sim.
class Countermeasures {
public:
    static constexpr int kMaxFlares = 24;  // burning at once
    static constexpr int kStock = 96;
    static constexpr int kSalvo = 6;
    static constexpr float kInterval = 0.09f;  // s between flares in a salvo
    static constexpr float kCooldown = 0.6f;   // s between salvos
    static constexpr float kBurnTime = 4.0f;   // s
    static constexpr float kIgnition = 0.05f;  // s from ejection to ignition (~1.5 m out)

    void reset();
    void update(const FlightState& aircraft, bool trigger, float dt);

    int stock() const { return stock_; }
    const Flare& flare(int i) const { return flares_[i]; }
    // Slot index of a flare ejected during the last update, or -1.
    int justReleased() const { return justReleased_; }

private:
    void release(const FlightState& aircraft);

    Flare flares_[kMaxFlares];
    int stock_ = kStock;
    int salvoLeft_ = 0;
    float timer_ = 0.0f;
    int side_ = 1;
    int next_ = 0;
    int justReleased_ = -1;
    uint32_t rng_ = 0xF1A2E5u;
};

inline bool Flare::lit() const { return burning && age >= Countermeasures::kIgnition; }

}  // namespace game
