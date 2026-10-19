#pragma once

// Menu framework (docs/decisions/0035): screens on a stack over a live 3D
// hangar, a shared context with the profile, navigation, sounds and the
// sortie launcher. Each screen lives in its own file (screens.h lists them).

#include <cstdint>

#include "meta/profile.h"
#include "meta/sortie.h"
#include "platform/psp_input.h"

namespace scenes {
struct Assets;
}

namespace menu {

enum class ScreenId : uint8_t {
    Main,
    Training,
    Campaign,     // carousel of locations
    Location,     // missions of ctx.location
    Briefing,     // ctx.location / ctx.mission: info, vehicle, loadout, livery, difficulty
    Debrief,      // after a mission sortie: ctx.lastResult / ctx.lastReward
    Multiplayer,  // LAN / Internet
    Lobby,        // lobbies found + create (ctx.network)
    Hangar,
    Shop,
    Settings,
    Profile,
    NameEntry,    // on-screen keyboard for the nickname
    Emblems,      // emblem gallery (unlocked by achievements)
    Confirm,      // yes/no dialog (ctx.confirm)
    Count
};

// Where the hangar camera stands behind a screen (Backdrop presets).
enum class Shot : uint8_t {
    Main,     // three-quarter front, aircraft on the right, menu column on the left
    Orbit,    // free orbit (hangar screen), Backdrop::orbit*
    Nose,     // close three-quarter of the nose (profile)
    Wide,     // wide and dimmed: panels fill the screen (campaign, briefing, settings)
};

enum class Sfx : uint8_t { Move, Select, Back, Tab, Slide, Deny, Confirm };

enum class Network : uint8_t { Lan, Internet };

class MenuScene;

// What every screen gets.
struct Context {
    meta::Profile& profile;
    scenes::Assets& assets;
    MenuScene& scene;
    float time = 0.0f;  // seconds since the menu opened
    float dt = 1.0f / 60.0f;  // this frame's step: draw() advances animations with it

    // Selection shared between screens.
    int location = 0;                  // Campaign -> Location
    int mission = 0;                   // Location -> Briefing
    int briefingPage = -1;             // page the Briefing opens on (-1: the mission page); it resets it
    meta::SortieConfig sortie;         // assembled by Briefing / Training
    meta::SortieResult lastResult;     // set when a sortie comes back
    meta::MissionReward lastReward;
    bool lastWasMission = false;
    Network network = Network::Lan;    // Multiplayer -> Lobby

    // Hangar camera control for the Hangar screen (Shot::Orbit).
    float orbitYaw = 2.4f, orbitPitch = 0.22f, orbitDistance = 22.0f;
    meta::VehicleId shownVehicle = meta::VehicleId::Su35S;  // what stands in the hangar
    int shownLivery = 0;

    Context(meta::Profile& p, scenes::Assets& a, MenuScene& s) : profile(p), assets(a), scene(s) {}

    void push(ScreenId id);
    void pop();
    void popTo(ScreenId id);   // pops until `id` is on top (no-op if absent)
    void replace(ScreenId id); // swaps the top screen for `id` (e.g. Debrief -> Briefing)
    void sfx(Sfx s);
    void save();               // writes the profile to the memory stick
    void launch(const meta::SortieConfig& sortie);  // leaves the menu for the flight
    // Yes/no dialog over the current screen; onYes runs on confirmation.
    void confirm(const char* question, void (*onYes)(Context&), const char* yes = "CONFIRM");
};

// A screen. update() gets the pad only when the screen is on top and no
// transition is running; draw() is called inside gfx::begin2D with the
// transition's layer offset/opacity already set.
class Screen {
public:
    virtual ~Screen() = default;
    virtual void enter(Context& ctx) { (void)ctx; }  // pushed, or uncovered by a pop
    virtual void update(Context& ctx, const platform::Pad& pad, float dt) = 0;
    virtual void draw(Context& ctx) = 0;
    virtual Shot shot() const { return Shot::Wide; }
    // How much the 3D hangar behind is darkened, 0..1.
    virtual float backdropDim() const { return 0.55f; }
    // Draw the screen below this one too (dialogs).
    virtual bool overlay() const { return false; }
};

}  // namespace menu
