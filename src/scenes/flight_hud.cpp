#include "scenes/flight_hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/warnings.h"
#include "render/gu_context.h"
#include "render/view.h"

namespace scenes {

using gfx::HudCanvas;
using math::Vec3;

namespace {

// 480 x 272 is not many pixels: few, large elements (Ace Combat-like layout),
// fighter-HUD symbology only where it helps flying (docs/decisions/0021).
constexpr float kCx = gfx::kScreenW * 0.5f;
constexpr float kCy = gfx::kScreenH * 0.5f;
constexpr float kLabel = 6.5f;   // px, dim captions
constexpr float kText = 8.0f;    // px, secondary values
constexpr float kValue = 11.0f;  // px, speed / altitude
constexpr float kWarn = 12.0f;   // px, warnings

// The "glass": conformal symbols are only drawn inside it.
constexpr float kGlassL = 136.0f, kGlassR = 344.0f, kGlassT = 52.0f, kGlassB = 214.0f;
bool inGlass(float x, float y) { return x > kGlassL && x < kGlassR && y > kGlassT && y < kGlassB; }

void flightPathMarker(HudCanvas& c, float x, float y) {
    c.circle(x, y, 5.0f, 10);
    c.line(x - 15.0f, y, x - 5.0f, y);
    c.line(x + 5.0f, y, x + 15.0f, y);
    c.line(x, y - 5.0f, x, y - 10.0f);
}

void boresight(HudCanvas& c, float x, float y) {
    // Where the nose points; in level flight it sits on the flight-path marker.
    c.line(x - 8.0f, y, x - 3.5f, y + 4.0f);
    c.line(x - 3.5f, y + 4.0f, x, y);
    c.line(x, y, x + 3.5f, y + 4.0f);
    c.line(x + 3.5f, y + 4.0f, x + 8.0f, y);
}

Vec3 horizontalHeading(const game::FlightState& s) {
    Vec3 h{s.pathDir.x, 0.0f, s.pathDir.z};
    if (math::length(h) < 0.05f) h = Vec3{s.orientation.forward().x, 0.0f, s.orientation.forward().z};
    if (math::length(h) < 0.05f) h = math::kForward;
    return math::normalize(h);
}

// Horizon line plus short rungs every 10 deg near the flight path; labels on
// the right only. Conformal: projected through the chase camera.
void pitchLadder(HudCanvas& c, const game::FlightState& s) {
    const Vec3 h = horizontalHeading(s);
    const Vec3 hr = math::cross(h, math::kUp);
    const float gamma = math::degrees(std::asin(math::clamp(s.pathDir.y, -1.0f, 1.0f)));
    const int lo = static_cast<int>(std::floor((gamma - 25.0f) / 10.0f)) * 10;
    for (int deg = lo; deg <= gamma + 25.0f; deg += 10) {
        if (deg < -90 || deg > 90) continue;
        float th = math::radians(static_cast<float>(deg));
        Vec3 d = h * std::cos(th) + math::kUp * std::sin(th);
        float cx, cy, px, py;
        if (!gfx::projectDirection(d, cx, cy) || !gfx::projectDirection(d + hr * 0.02f, px, py)) continue;
        if (!inGlass(cx, cy)) continue;
        float dx = px - cx, dy = py - cy, len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) continue;
        dx /= len;
        dy /= len;
        const bool horizon = deg == 0;
        const float gap = horizon ? 30.0f : 14.0f, half = horizon ? 110.0f : 30.0f;
        for (int side = -1; side <= 1; side += 2) {
            float x0 = cx + dx * gap * side, y0 = cy + dy * gap * side;
            float x1 = cx + dx * half * side, y1 = cy + dy * half * side;
            if (deg < 0)
                c.dashed(x0, y0, x1, y1, 5.0f, 4.0f);
            else
                c.line(x0, y0, x1, y1);
        }
        if (!horizon) {
            char label[8];
            std::snprintf(label, sizeof label, "%d", deg);
            c.textRotated(cx + dx * (half + 5.0f), cy + dy * (half + 5.0f), label, kLabel, dx, dy, HudCanvas::kLeft);
        }
    }
}

void headingTape(HudCanvas& c, const game::FlightState& s) {
    const Vec3 h = horizontalHeading(s);
    float heading = math::degrees(std::atan2(h.x, -h.z));  // 0 = start heading ("north"), clockwise
    if (heading < 0.0f) heading += 360.0f;
    constexpr float kPxPerDeg = 1.6f, kHalf = 72.0f, kY = 14.0f;
    int first = static_cast<int>(std::ceil((heading - kHalf / kPxPerDeg) / 10.0f)) * 10;
    for (int d = first; d <= heading + kHalf / kPxPerDeg; d += 10) {
        float x = kCx + (d - heading) * kPxPerDeg;
        int norm = ((d % 360) + 360) % 360;
        bool cardinal = norm % 90 == 0;
        c.line(x, kY, x, kY + (cardinal ? 6.0f : 3.5f));
        if (cardinal && std::fabs(x - kCx) > 16.0f) {
            static const char* const kNames[] = {"N", "E", "S", "W"};
            c.text(x, kY - 9.0f, kNames[norm / 90], kLabel, HudCanvas::kCenter);
        }
    }
    char buf[8];
    std::snprintf(buf, sizeof buf, "%03d", static_cast<int>(heading + 0.5f) % 360);
    c.box(kCx - 14.0f, kY + 8.0f, kCx + 14.0f, kY + 20.0f);
    c.text(kCx, kY + 10.0f, buf, kText, HudCanvas::kCenter);
}

// Boxed value with a caption and a short moving tick scale on the inner side:
// the ticks give the sense of rate, the number gives the value.
void readout(HudCanvas& c, float boxX, int inner, const char* caption, float value, float step, float pxPerUnit,
             uint32_t color) {
    using namespace hud;
    constexpr float kW = 62.0f, kH = 17.0f, kHalfScale = 44.0f;
    const float x0 = boxX, x1 = boxX + kW, y0 = kCy - kH * 0.5f, y1 = kCy + kH * 0.5f;
    c.color(kDim);
    c.text((x0 + x1) * 0.5f, y0 - 11.0f, caption, kLabel, HudCanvas::kCenter);
    c.color(color);
    c.width(kLineWidth);
    c.box(x0, y0, x1, y1);
    char buf[12];
    std::snprintf(buf, sizeof buf, "%d", static_cast<int>(value + 0.5f));
    c.text((x0 + x1) * 0.5f, kCy - kValue * 0.5f, buf, kValue, HudCanvas::kCenter);

    const float sx = inner > 0 ? x1 + 6.0f : x0 - 6.0f;  // scale spine
    c.color(kDim);
    c.width(2.6f);
    int first = static_cast<int>(std::ceil((value - kHalfScale / pxPerUnit) / step));
    for (int i = first; i * step <= value + kHalfScale / pxPerUnit; ++i) {
        float y = kCy - (i * step - value) * pxPerUnit;
        c.line(sx, y, sx + inner * (i % 2 == 0 ? 6.0f : 3.0f), y);
    }
    c.color(color);
    c.line(sx - inner * 3.0f, kCy, sx + inner * 2.0f, kCy);  // index
}

void warning(HudCanvas& c, const char* text, uint32_t color) {
    using namespace hud;
    float w = HudCanvas::textWidth(text, kWarn) * 0.5f + 8.0f;
    c.color(color);
    c.width(kLineWidth);
    c.box(kCx - w, 182.0f, kCx + w, 182.0f + kWarn + 8.0f);
    c.text(kCx, 186.0f, text, kWarn, HudCanvas::kCenter);
}

}  // namespace

void drawFlightHud(HudCanvas& c, const game::FlightState& s, const game::Countermeasures& cm, bool gOverride,
                   float time) {
    using namespace hud;
    c.reset();
    char buf[32];
    const bool blink = std::fmod(time, 0.8f) < 0.5f;  // 1.25 Hz: slow enough for the PSP-3000 LCD

    if (s.crashed) {
        warning(c, "CRASHED", kRed);
        return;
    }

    // --- Centre: conformal symbols.
    float x, y;
    c.color(kDim);
    c.width(2.6f);
    pitchLadder(c, s);
    c.color(kGreen);
    c.width(kLineWidth);
    if (gfx::projectDirection(s.pathDir, x, y) && inGlass(x, y)) flightPathMarker(c, x, y);
    if (gfx::projectDirection(s.orientation.forward(), x, y) && inGlass(x, y)) boresight(c, x, y);

    // --- Top: heading. Sides: speed and altitude.
    c.width(kTextWidth);
    headingTape(c, s);
    readout(c, 30.0f, +1, "SPEED", s.speed * 3.6f, 50.0f, 0.3f, kGreen);
    readout(c, 388.0f, -1, "ALT", s.position.y, 50.0f, 0.3f, s.position.y < 150.0f ? kAmber : kGreen);

    c.width(kTextWidth);
    c.color(kGreen);
    std::snprintf(buf, sizeof buf, "M %.2f", s.mach);
    c.text(61.0f, kCy + 16.0f, buf, kText, HudCanvas::kCenter);
    c.color(s.loadFactor > 9.0f ? kAmber : kGreen);
    std::snprintf(buf, sizeof buf, "G %.1f", s.loadFactor);
    c.text(61.0f, kCy + 29.0f, buf, kText, HudCanvas::kCenter);

    // --- Bottom: engine state left, stores right.
    if (s.afterburner) {
        c.color(kAmber);
        c.text(16.0f, 250.0f, "AB", kText);
    } else if (s.airbrake) {
        c.color(kGreen);
        c.text(16.0f, 250.0f, "BRAKE", kText);
    }
    c.color(cm.stock() < 16 ? kAmber : kGreen);
    std::snprintf(buf, sizeof buf, "FLR %d", cm.stock());
    c.text(464.0f, 250.0f, buf, kText, HudCanvas::kRight);

    // --- Warnings and modes, one at a time (priority order).
    // Same rule as the voice informer's "переведи в набор" (game/warnings.h).
    if (game::groundWarning(s) == game::GroundWarning::PullUp) {
        if (blink) warning(c, "PULL UP", kRed);
    } else if (s.psm) {
        warning(c, "PSM", kAmber);
        c.width(kTextWidth);
        std::snprintf(buf, sizeof buf, "AOA %d", static_cast<int>(math::degrees(s.alpha)));
        c.text(kCx, 207.0f, buf, kText, HudCanvas::kCenter);
    } else if (s.stalled) {
        if (blink) warning(c, "STALL", kRed);
    } else if (gOverride) {
        c.color(kAmber);
        c.width(kTextWidth);
        c.text(kCx, 190.0f, "G-LIMIT OFF", kText, HudCanvas::kCenter);
    }
}

}  // namespace scenes
