#pragma once

#include "game/controls.h"
#include "game/flight_model.h"

namespace game {

enum class Surface { Stabilator, Flaperon, Slat, Rudder, Nozzle, Count };

// Animated deflections of the moving parts (docs/decisions/0015), radians.
// side 0 = right, 1 = left. Positive is mirror-symmetric, as in the FSM4 part
// table: TE down (stabilators, flaperons), LE down (slats), TE outboard
// (rudders), exit down (nozzles). Purely visual, but platform-neutral and
// deterministic so it can be tested and replayed.
class ControlSurfaces {
public:
    void reset();
    void update(const FlightState& s, const ControlInput& in, float dt);
    float angle(Surface surface, int side) const { return angle_[static_cast<int>(surface)][side]; }

private:
    static constexpr int kCount = static_cast<int>(Surface::Count);
    float angle_[kCount][2] = {};
};

}  // namespace game
