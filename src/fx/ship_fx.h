#pragma once

#include "fx/particles.h"
#include "fx/water_marks.h"
#include "game/ship.h"

namespace fx {

// A ship's own effects (docs/decisions/0044, 0045): the white wake astern and
// the waves spreading from the bow, spray thrown off the stem at speed, smoke
// and fire once it's hurt, foam and steam as it goes down. Simulation only.
class ShipEffects {
public:
    void reset();
    void update(const game::Ship& ship, float dt);

    const ParticleSystem& particles() const { return particles_; }
    const WaterMarks& waterMarks() const { return marks_; }
    const Trail& smoke() const { return smoke_; }

private:
    ParticleSystem particles_;
    WaterMarks marks_;
    Trail smoke_;
    float wakeTimer_ = 0.0f, waveTimer_ = 0.0f, sprayAcc_ = 0.0f, fireAcc_ = 0.0f;
    Rng rng_{0x5B1Bu};
};

}  // namespace fx
