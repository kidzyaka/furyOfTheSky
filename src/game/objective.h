#pragma once

#include <cstdint>

namespace game {

// How a mission ends (docs/decisions/0038): destroy N enemy aircraft one
// after another; a few seconds after the last goes down the sortie is won,
// a few seconds after the player's aircraft is lost it is failed. Whichever
// happens first stands - a missile that is still in the air when we are
// shot down must not turn a failure into a win. Pure bookkeeping, no
// platform code: the flight scene feeds it events and a clock.
class Objective {
public:
    static constexpr float kWinDelay = 4.0f;   // s from the last kill to the end of the sortie
    static constexpr float kLoseDelay = 3.0f;  // s from the loss of the aircraft to the end

    enum class Phase : uint8_t { Playing, Won, Lost };

    // `targets` = 0: no objective - the sortie never ends by itself
    // (free flight, target practice, dogfight). `ground`: how many of them
    // are ground targets (ships, docs/decisions/0045) - all present from the
    // start; the air ones come one after another.
    void reset(int targets, int ground = 0) {
        targets_ = targets;
        ground_ = ground;
        destroyed_ = groundDestroyed_ = 0;
        phase_ = Phase::Playing;
        wait_ = 0.0f;
    }

    void enemyDestroyed() {
        ++destroyed_;
        check();
    }
    void groundTargetDestroyed() {
        ++destroyed_;
        ++groundDestroyed_;
        check();
    }
    void playerLost() {
        if (phase_ != Phase::Playing || targets_ <= 0) return;
        phase_ = Phase::Lost;
        wait_ = kLoseDelay;
    }
    void update(float dt) {
        if (phase_ != Phase::Playing && wait_ > 0.0f) wait_ -= dt;
    }

    int targets() const { return targets_; }
    int destroyed() const { return destroyed_; }
    Phase phase() const { return phase_; }
    // The last enemy is down: nobody comes after it.
    bool allDestroyed() const { return targets_ > 0 && destroyed_ >= targets_; }
    // Every enemy aircraft of the mission is down (no more come; ships may remain).
    bool airDone() const { return targets_ > 0 && destroyed_ - groundDestroyed_ >= targets_ - ground_; }
    int groundTargets() const { return ground_; }
    // The closing delay has run out - time to leave the flight.
    bool done() const { return phase_ != Phase::Playing && wait_ <= 0.0f; }

private:
    void check() {
        if (phase_ == Phase::Playing && targets_ > 0 && destroyed_ >= targets_) {
            phase_ = Phase::Won;
            wait_ = kWinDelay;
        }
    }

    int targets_ = 0, ground_ = 0;
    int destroyed_ = 0, groundDestroyed_ = 0;
    Phase phase_ = Phase::Playing;
    float wait_ = 0.0f;
};

}  // namespace game
