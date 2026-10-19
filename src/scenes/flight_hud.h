#pragma once

#include "game/armament.h"
#include "game/countermeasures.h"
#include "game/flight_model.h"
#include "game/radar.h"
#include "game/rwr.h"
#include "render/hud_draw.h"
#include "world/terrain.h"

namespace scenes {

// What the HUD shows of the weapons and sensors (docs/decisions/0028).
struct CombatView {
    const game::Radar* radar = nullptr;
    const game::Armament* arms = nullptr;
    const game::Rwr* rwr = nullptr;
    float health = 100.0f;  // ours (DMG on the weapon panel)
    // The enemy and the tally (top left): its health, our kills / times shot down.
    // No enemy (free flight): no tally.
    bool hasEnemy = true;
    const char* targetName = "DRONE";
    // Per radar target slot (the enemy aircraft 0, ships 1..), null: targetName.
    const char* targetNames[game::kMaxTargets] = {};
    const char* targetMode = "";
    float targetHealth = 100.0f;
    int hits = 0, kills = 0;
    // Mission objective under the tally ("TARGETS 2/4"); total 0 = no mission.
    int objectiveDone = 0, objectiveTotal = 0;
    // The mission's closing banner (MISSION COMPLETE / FAILED), big and centred;
    // stays up until the sortie ends.
    const char* banner = nullptr;
    uint32_t bannerColor = 0;
    // The mission-area edge (docs/decisions/0043): 0 inside, 1 "return" with
    // the seconds left before the game turns us, 2 the game is turning us;
    // where home is relative to the heading (rad, + right).
    int area = 0;
    float areaCountdown = 0.0f;
    float homeBearing = 0.0f;
    // Rocks for the PULL UP rule (docs/decisions/0042).
    const ::world::Terrain* terrain = nullptr;
    // Ace Combat message line: HIT / MISS / DESTROYED, and how long it's been up.
    const char* message = nullptr;
    uint32_t messageColor = 0;
    float messageAge = 0.0f;
};

// Head-up display of the flight scene, v3 (docs/decisions/0049; targeting
// 0028, gradual lock-on 0050): between a fighter's HUD and Ace Combat. Text is
// the bitmap HUD font (ui::HudFace, outlined, >= 13 px), backdrops only under
// the dense clusters, alerts as Ace Combat plates above the aim, nothing
// over the aircraft. Call after the 3D scene with setCamera() of this frame
// still current; flush the canvas with ui::res().hudFont and hudPlates.
void drawFlightHud(gfx::HudCanvas& canvas, const game::FlightState& s, const game::Countermeasures& cm,
                   bool gOverride, float time, const CombatView& combat);

// Shared HUD palette (also used by menus).
namespace hud {
constexpr uint32_t kGreen = 0xEB8CFF5Au;  // abgr(90, 255, 140, 235)
constexpr uint32_t kDim = 0x968CFF5Au;    // same, alpha 150
constexpr uint32_t kTerrainInk = 0xC07AB4C8u;  // khaki: coastlines on the radar scope
constexpr uint32_t kAmber = 0xF046C8FFu;  // abgr(255, 200, 70, 240)
constexpr uint32_t kRed = 0xF53C5AFFu;    // abgr(255, 90, 60, 245)
constexpr float kLineWidth = 3.6f;   // PSP-3000 LCD: nothing thinner reads (docs/decisions/0031)
constexpr float kTextWidth = 3.2f;
constexpr uint32_t kBackdrop = 0x780C140Au;  // abgr(10, 20, 12, 120): dark glass under clusters
}  // namespace hud

}  // namespace scenes
