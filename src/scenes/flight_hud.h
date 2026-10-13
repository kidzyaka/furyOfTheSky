#pragma once

#include "game/countermeasures.h"
#include "game/flight_model.h"
#include "render/hud_draw.h"

namespace scenes {

// Head-up display of the flight scene (docs/decisions/0018, layout 0021):
// between a fighter's holographic HUD and Ace Combat - few, large elements.
// Conformal horizon/ladder and flight-path marker (projected through the chase
// camera), boxed speed and altitude, heading, Mach and G, stores, warnings.
// Call after the 3D scene with setCamera() of this frame still current.
void drawFlightHud(gfx::HudCanvas& canvas, const game::FlightState& s, const game::Countermeasures& cm,
                   bool gOverride, float time);

// Shared HUD palette (also used by menus).
namespace hud {
constexpr uint32_t kGreen = 0xEB8CFF5Au;  // abgr(90, 255, 140, 235)
constexpr uint32_t kDim = 0x968CFF5Au;    // same, alpha 150
constexpr uint32_t kAmber = 0xF046C8FFu;  // abgr(255, 200, 70, 240)
constexpr uint32_t kRed = 0xF53C5AFFu;    // abgr(255, 90, 60, 245)
constexpr float kLineWidth = 3.0f;
constexpr float kTextWidth = 2.6f;
}  // namespace hud

}  // namespace scenes
