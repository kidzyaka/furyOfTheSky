#pragma once

#include <algorithm>

#include "game/flight_model.h"

namespace game {

// How an aircraft looks to sensors (docs/decisions/0027). Gameplay numbers
// anchored to published estimates for a Flanker-class fighter; the shape
// (aspect dependence, afterburner) is what matters.

// Radar cross-section, m^2, seen from the unit direction `toObserver` (world,
// from the aircraft to the radar). Nose-on is the smallest (Su-35S: radar
// blockers in the inlets, ~1-3 m^2 claimed), broadside the largest.
inline float radarCrossSection(const FlightState& s, const math::Vec3& toObserver) {
    const float c = math::dot(s.orientation.forward(), toObserver);  // +1 nose-on, -1 tail-on
    const float e = math::dot(s.orientation.up(), toObserver);       // plan view from above/below
    const float front = 2.0f, rear = 6.0f, side = 15.0f, plan = 40.0f;
    const float c2 = c * c, e2 = e * e;
    const float lobe = c > 0.0f ? front : rear;
    return (lobe * c2 + side * (1.0f - c2)) * (1.0f - e2) + plan * e2;
}

// Infrared intensity in the seeker band, relative units: 1 = both tailpipes
// at full dry power seen from dead astern. The hot nozzles only show from the
// rear hemisphere; an afterburner plume shows from every side and outshines
// everything; from the front only the skin, heated by the airflow, is left.
inline float infraredIntensity(const FlightState& s, const math::Vec3& toObserver) {
    if (s.crashed) return 0.0f;
    const float c = math::dot(s.orientation.forward(), toObserver);
    const float tail = math::smoothstep(0.1f, -0.85f, c);                // nozzle cavities in view
    const float engines = (0.25f + 0.75f * std::min(s.throttle, 1.0f)) * tail;
    const float plume = s.afterburner ? 2.5f + 5.0f * tail : 0.12f * s.throttle;
    const float skin = 0.04f * (1.0f + s.mach * s.mach);                // ram-air heating
    return engines + plume + skin;
}

// A burning decoy flare in the same units: hotter than the dry-power
// tailpipes, cooler than a lit afterburner - so "cut the burner and pop
// flares" works, as it does for real.
constexpr float kFlareIntensity = 4.0f;

}  // namespace game
