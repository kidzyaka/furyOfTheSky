#pragma once

// Menu UI kit (docs/decisions/0035): resources and the widgets every screen
// is built from - metal buttons, panels, the bottom hint bar, text. Draws
// with render/draw2d between gfx::begin2D() and gfx::end2D().

#include <cstdint>

#include "meta/catalog.h"
#include "render/font.h"
#include "render/texture.h"
#include "render/gu_context.h"
#include "render/view.h"
#include "ui/ui_atlas.h"

namespace ui {

// --- Palette (ABGR). Text on dark metal is light; on the lit (focused)
// plate it is engraved dark.
constexpr uint32_t kWhite = 0xFFFFFFFFu;
constexpr uint32_t kText = gfx::abgr(210, 216, 206);
constexpr uint32_t kTextBright = gfx::abgr(246, 244, 232);
constexpr uint32_t kTextDim = gfx::abgr(128, 134, 126);
constexpr uint32_t kTextDisabled = gfx::abgr(104, 110, 102);
constexpr uint32_t kEngraved = gfx::abgr(22, 26, 22);
constexpr uint32_t kAccent = gfx::abgr(232, 170, 60);   // amber: focus marker, ranks, highlights
constexpr uint32_t kAccentDim = gfx::abgr(150, 120, 64);
constexpr uint32_t kGood = gfx::abgr(150, 222, 120);
constexpr uint32_t kBad = gfx::abgr(232, 92, 72);
constexpr uint32_t kCredits = gfx::abgr(190, 206, 184);

// Screen layout constants.
constexpr int kBarHeight = 26;                       // bottom hint bar
constexpr int kBarY = gfx::kScreenH - kBarHeight;
constexpr int kMargin = 14;

// --- Resources (assets/ui, tools/textures/ui.py; location art and
// schematics from tools/textures/locations.py).
struct Resources {
    gfx::Font font;
    gfx::Texture skin, icons, schematics, emblems;
    // Flight HUD (docs/decisions/0049): outlined faces (HudFace) and the warning plates.
    gfx::Font hudFont;
    gfx::Texture hudPlates;
    gfx::Texture locations[meta::kMaxLocations];  // may be missing: a placeholder is drawn
    const char* error = nullptr;
    bool load();
};
Resources& res();

// --- Text. y = top of the capitals (see gfx::Font::draw).
int text(int x, int y, const char* s, Face face, uint32_t color, gfx::TextAlign align = gfx::kAlignLeft,
         gfx::TextStyle style = gfx::TextStyle::Shadow);
int textWidth(const char* s, Face face);
int capHeight(Face face);
int lineHeight(Face face);
// Text vertically centred in a box of height h starting at y.
int textInBox(int x, int y, int h, const char* s, Face face, uint32_t color, gfx::TextAlign align = gfx::kAlignLeft,
              gfx::TextStyle style = gfx::TextStyle::Shadow);
int paragraph(int x, int y, int maxWidth, const char* s, Face face, uint32_t color, int lineGap = 1);

// --- Images.
void icon(const AtlasRect& r, int x, int y, uint32_t tint = kWhite);
void iconScaled(const AtlasRect& r, int x, int y, int w, int h, uint32_t tint = kWhite);
// A strip of the skin (button heights) cut to width w: left part + right cap,
// tiled when wider than the strip.
void strip(const AtlasRect& r, int x, int y, int w, uint32_t tint = kWhite);
// 9-slice of a skin piece with `border` px corners.
void nineSlice(const AtlasRect& r, int x, int y, int w, int h, int border, uint32_t tint = kWhite);

enum class Plate : uint8_t { Panel, Light, Inset };
void panel(int x, int y, int w, int h, Plate plate = Plate::Panel, uint32_t tint = kWhite);

// Shadow cast onto a plate by an edge above (fromBelow = false) or below it.
void innerShadow(int x, int y, int w, int h, bool fromBelow, float strength);
// Full-screen darkening (behind dialogs, dimmed backdrop).
void dimScreen(float alpha);

// --- Buttons.
enum class ButtonKind : uint8_t {
    Main,     // 32 px, main menu and big actions
    Sub,      // 28 px, second-level rows (the same metal, darker)
    Row,      // 28 px, list rows
    Primary,  // 32 px, red: the action that starts a sortie / buys
    Tab,      // 24 px
};
int buttonHeight(ButtonKind kind);

struct ButtonLook {
    float focus = 0.0f;     // 0..1, animated: lit plate, engraved text, amber marker, slides right
    bool disabled = false;
    bool open = false;      // its sub-rows are expanded below it
    float dim = 0.0f;       // 0..1: pushed back while a sibling's group is open
};
void button(int x, int y, int w, ButtonKind kind, const char* label, const ButtonLook& look,
            const char* tag = nullptr, const AtlasRect* rightIcon = nullptr);

// --- Bars and titles.
struct Hint {
    const AtlasRect* icon;
    const char* label;
};
// The bottom plate with button hints (right-aligned) and the shadow it casts
// on whatever scrolls under it.
void bottomBar(const Hint* hints, int count, const char* leftText = nullptr);
void screenTitle(const char* title, const char* subtitle = nullptr);
void progressBar(int x, int y, int w, float t);

// Common hints.
extern const Hint kHintSelect, kHintBack, kHintTabs, kHintChange;

// "125 400 CR": thousands separated by spaces, the currency after.
void formatCredits(int value, char* out, int size);
void formatThousands(int value, char* out, int size);

// Pilot emblem (meta::emblem order), 56 px native; other sizes are filtered.
void emblem(int index, int x, int y, int size = emblems::kSize, uint32_t tint = kWhite);

// Location art / vehicle schematics (with placeholders when missing).
void locationImage(int index, int x, int y, int w, int h, uint32_t tint = kWhite);
void schematic(int row, int x, int y, uint32_t tint);  // a 256x64 cell 1:1

}  // namespace ui
