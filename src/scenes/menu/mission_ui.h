#pragma once

// Small helpers shared by the menu screens (campaign, location, briefing,
// debrief, shop, profile, emblems): icon lookup, colour mixing, clock text,
// ink on lit plates, frames and scroll bars. Header-only on purpose.

#include <cstdint>
#include <cstdio>

#include "meta/catalog.h"
#include "meta/profile.h"
#include "render/draw2d.h"
#include "ui/anim.h"
#include "ui/ui.h"

namespace menu {
namespace mui {

inline uint32_t withAlpha(uint32_t abgr, float a) {
    a = ui::clamp01(a);
    return (abgr & 0x00FFFFFFu) | (static_cast<uint32_t>(((abgr >> 24) & 0xFF) * a + 0.5f) << 24);
}

// Colour mix in ABGR, per channel (alpha too).
inline uint32_t mix(uint32_t a, uint32_t b, float t) {
    t = ui::clamp01(t);
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        const float ca = static_cast<float>((a >> s) & 0xFF), cb = static_cast<float>((b >> s) & 0xFF);
        out |= static_cast<uint32_t>(ca + (cb - ca) * t + 0.5f) << s;
    }
    return out;
}

// Grey tint (multiplies a sprite): k = 1 leaves it as is.
inline uint32_t grey(float k) {
    const unsigned v = static_cast<unsigned>(ui::clamp01(k) * 255.0f + 0.5f);
    return gfx::abgr(v, v, v);
}

inline const ui::AtlasRect& weatherIcon(meta::Weather w) {
    switch (w) {
        case meta::Weather::Clear: return ui::icons::kClear;
        case meta::Weather::Cloudy: return ui::icons::kCloudy;
        case meta::Weather::Overcast: return ui::icons::kOvercast;
        case meta::Weather::Fog: return ui::icons::kFog;
        case meta::Weather::Rain: return ui::icons::kRain;
        case meta::Weather::Snow: return ui::icons::kSnow;
    }
    return ui::icons::kClear;
}

inline const ui::AtlasRect& timeIcon(meta::TimeOfDay t) {
    switch (t) {
        case meta::TimeOfDay::Dawn: return ui::icons::kDawn;
        case meta::TimeOfDay::Day: return ui::icons::kDay;
        case meta::TimeOfDay::Dusk: return ui::icons::kDusk;
        case meta::TimeOfDay::Night: return ui::icons::kNight;
    }
    return ui::icons::kDay;
}

// Bronze / silver / gold for Easy / Normal / Hard; the dark ring when not earned.
inline const ui::AtlasRect& medalIcon(int difficulty, bool earned) {
    if (!earned) return ui::icons::kMedalNone;
    switch (difficulty) {
        case 0: return ui::icons::kMedalEasy;
        case 1: return ui::icons::kMedalNormal;
        default: return ui::icons::kMedalHard;
    }
}

// The three medals of a mission (bit per difficulty), 16 px each.
inline void drawMedals(int x, int y, uint8_t bits, uint32_t tint, int gap = 3) {
    for (int d = 0; d < meta::kDifficultyCount; ++d)
        ui::icon(medalIcon(d, (bits >> d) & 1), x + d * (16 + gap), y, tint);
}
inline int medalsWidth(int gap = 3) { return meta::kDifficultyCount * 16 + (meta::kDifficultyCount - 1) * gap; }

// How many missions of a location carry the medal of a difficulty.
inline int medalCount(const meta::Profile& p, int loc, int difficulty) {
    int n = 0;
    for (int m = 0; m < meta::location(loc).missionCount; ++m)
        if ((p.medals[loc][m] >> difficulty) & 1) ++n;
    return n;
}

inline void formatClock(float seconds, char* out, int size) {
    const int s = seconds < 0.0f ? 0 : static_cast<int>(seconds + 0.5f);
    std::snprintf(out, size, "%02d:%02d", s / 60, s % 60);
}

// The preferred face if the text fits, else the smallest legal one.
inline ui::Face fitFace(const char* s, int maxWidth, ui::Face preferred) {
    return ui::textWidth(s, preferred) <= maxWidth ? preferred : ui::Face::Small;
}

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// --- Lit plates. A focused card/row turns light metal: the light ink of the
// dark plate cross-fades to engraved dark ink (lit = the focus animation, 0..1).
constexpr uint32_t kInkAmber = gfx::abgr(104, 70, 8);
constexpr uint32_t kInkGreen = gfx::abgr(30, 96, 40);
constexpr uint32_t kInkRed = gfx::abgr(158, 36, 28);
constexpr uint32_t kIconDark = gfx::abgr(40, 46, 40);  // a white line icon on the lit plate

inline void inkText(int x, int y, const char* s, ui::Face f, uint32_t idle, uint32_t litInk, float lit,
                    gfx::TextAlign align = gfx::kAlignLeft) {
    if (lit < 0.99f) ui::text(x, y, s, f, withAlpha(idle, 1.0f - lit), align);
    if (lit > 0.01f) ui::text(x, y, s, f, withAlpha(litInk, lit), align, gfx::TextStyle::Engraved);
}

// A rectangular outline of thickness t drawn inside (x, y, w, h).
inline void frame(int x, int y, int w, int h, int t, uint32_t c) {
    gfx::fillRect(x, y, w, t, c);
    gfx::fillRect(x, y + h - t, w, t, c);
    gfx::fillRect(x, y + t, t, h - 2 * t, c);
    gfx::fillRect(x + w - t, y + t, t, h - 2 * t, c);
}

// Corner brackets (an L of `len` px per corner) inside (x, y, w, h).
inline void brackets(int x, int y, int w, int h, int len, int t, uint32_t c) {
    for (int sy = 0; sy < 2; ++sy)
        for (int sx = 0; sx < 2; ++sx) {
            const int cx = sx ? x + w - len : x, cy = sy ? y + h - t : y;
            gfx::fillRect(cx, cy, len, t, c);
            const int vx = sx ? x + w - t : x, vy = sy ? y + h - len : y;
            gfx::fillRect(vx, vy, t, len, c);
        }
}

// Thin scroll bar (3 px) for a list that does not fit: `view` and `content`
// are heights in px, `offset` how far the content has scrolled.
inline void scrollbar(int x, int top, int height, float view, float content, float offset) {
    if (content <= view + 0.5f) return;
    gfx::fillRect(x, top, 3, height, gfx::abgr(0, 0, 0, 110));
    float len = static_cast<float>(height) * view / content;
    if (len < 14.0f) len = 14.0f;
    const float room = static_cast<float>(height) - len;
    const float pos = room * ui::clamp01(offset / (content - view));
    gfx::fillRect(x, top + static_cast<int>(pos + 0.5f), 3, static_cast<int>(len + 0.5f), gfx::abgr(168, 176, 164, 190));
}

// Moves a wanted scroll offset the least that brings [top, bottom) of an item
// into [viewTop, viewBottom]; clamped to [0, maxOffset].
inline float scrollToShow(float wanted, float top, float bottom, float viewTop, float viewBottom, float maxOffset) {
    if (bottom - wanted > viewBottom) wanted = bottom - viewBottom;
    if (top - wanted < viewTop) wanted = top - viewTop;
    if (wanted > maxOffset) wanted = maxOffset;
    return wanted < 0.0f ? 0.0f : wanted;
}

}  // namespace mui
}  // namespace menu
