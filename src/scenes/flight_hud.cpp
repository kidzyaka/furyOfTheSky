#include "scenes/flight_hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "render/gu_context.h"
#include "render/view.h"

namespace scenes {

using gfx::HudCanvas;
using math::Vec3;

namespace {

constexpr float kCx = gfx::kScreenW * 0.5f;
constexpr float kCy = gfx::kScreenH * 0.5f;
constexpr float kText = 8.0f;     // px, normal text height
constexpr float kSmall = 6.5f;
constexpr float kBig = 10.0f;

// The "glass": conformal symbols are only drawn inside it.
constexpr float kGlassL = 112.0f, kGlassR = 368.0f, kGlassT = 40.0f, kGlassB = 232.0f;
bool inGlass(float x, float y) { return x > kGlassL && x < kGlassR && y > kGlassT && y < kGlassB; }

void flightPathMarker(HudCanvas& c, float x, float y) {
    c.circle(x, y, 4.5f, 10);
    c.line(x - 13.0f, y, x - 4.5f, y);
    c.line(x + 4.5f, y, x + 13.0f, y);
    c.line(x, y - 4.5f, x, y - 9.5f);
}

void boresight(HudCanvas& c, float x, float y) {
    // "Waterline": where the nose points.
    c.line(x - 14.0f, y, x - 7.0f, y);
    c.line(x - 7.0f, y, x - 3.5f, y + 4.0f);
    c.line(x - 3.5f, y + 4.0f, x, y);
    c.line(x, y, x + 3.5f, y + 4.0f);
    c.line(x + 3.5f, y + 4.0f, x + 7.0f, y);
    c.line(x + 7.0f, y, x + 14.0f, y);
}

void pitchLadder(HudCanvas& c, const game::FlightState& s) {
    Vec3 h{s.pathDir.x, 0.0f, s.pathDir.z};
    if (math::length(h) < 0.05f) h = Vec3{s.orientation.forward().x, 0.0f, s.orientation.forward().z};
    if (math::length(h) < 0.05f) h = math::kForward;
    h = math::normalize(h);
    const Vec3 hr = math::cross(h, math::kUp);
    const float gamma = math::degrees(std::asin(math::clamp(s.pathDir.y, -1.0f, 1.0f)));

    const int lo = static_cast<int>(std::floor((gamma - 30.0f) / 10.0f)) * 10;
    for (int deg = lo; deg <= gamma + 30.0f; deg += 10) {
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
        const float ux = dy, uy = -dx;  // towards higher pitch on screen
        const bool horizon = deg == 0;
        const float gap = horizon ? 26.0f : 15.0f, half = horizon ? 96.0f : 40.0f;
        for (int side = -1; side <= 1; side += 2) {
            float x0 = cx + dx * gap * side, y0 = cy + dy * gap * side;
            float x1 = cx + dx * half * side, y1 = cy + dy * half * side;
            if (deg < 0)
                c.dashed(x0, y0, x1, y1, 5.0f, 3.5f);
            else
                c.line(x0, y0, x1, y1);
            if (!horizon) {
                // End ticks point at the horizon.
                float k = deg > 0 ? -6.0f : 6.0f;
                c.line(x1, y1, x1 + ux * k, y1 + uy * k);
                char label[8];
                std::snprintf(label, sizeof label, "%d", deg < 0 ? -deg : deg);
                float lx = cx + dx * (half + 5.0f) * side, ly = cy + dy * (half + 5.0f) * side;
                c.textRotated(lx, ly, label, kSmall, dx, dy, side < 0 ? HudCanvas::kRight : HudCanvas::kLeft);
            }
        }
    }
}

void headingTape(HudCanvas& c, const game::FlightState& s) {
    Vec3 h{s.pathDir.x, 0.0f, s.pathDir.z};
    if (math::length(h) < 0.05f) h = Vec3{s.orientation.forward().x, 0.0f, s.orientation.forward().z};
    float heading = math::degrees(std::atan2(h.x, -h.z));  // 0 = start heading ("north"), clockwise
    if (heading < 0.0f) heading += 360.0f;
    constexpr float kPxPerDeg = 2.0f, kHalf = 92.0f, kY = 20.0f;
    int first = static_cast<int>(std::ceil((heading - kHalf / kPxPerDeg) / 5.0f)) * 5;
    for (int d = first; d <= heading + kHalf / kPxPerDeg; d += 5) {
        float x = kCx + (d - heading) * kPxPerDeg;
        bool major = d % 10 == 0;
        c.line(x, kY, x, kY - (major ? 6.0f : 3.0f));
        int norm = ((d % 360) + 360) % 360;
        if (norm % 30 == 0 && std::fabs(x - kCx) > 14.0f) {
            static const char* const kNames[] = {"N", "3", "6", "E", "12", "15", "S", "21", "24", "W", "30", "33"};
            c.text(x, kY - 15.0f, kNames[norm / 30], kSmall, HudCanvas::kCenter);
        }
    }
    c.line(kCx - kHalf, kY, kCx + kHalf, kY);
    c.line(kCx, kY + 1.5f, kCx - 4.0f, kY + 7.0f);  // caret
    c.line(kCx, kY + 1.5f, kCx + 4.0f, kY + 7.0f);
    char buf[8];
    std::snprintf(buf, sizeof buf, "%03d", static_cast<int>(heading + 0.5f) % 360);
    c.text(kCx, kY - 15.0f, buf, kSmall, HudCanvas::kCenter);
}

// Vertical tape: spine at x, ticks towards `dir` (-1 left, +1 right) with the
// labels beyond them, readout box on the outside.
void tape(HudCanvas& c, float x, int dir, float value, float step, float pxPerUnit, const char* fmt) {
    constexpr float kHalf = 66.0f;
    c.line(x, kCy - kHalf, x, kCy + kHalf);
    int first = static_cast<int>(std::ceil((value - kHalf / pxPerUnit) / step));
    for (int i = first; i * step <= value + kHalf / pxPerUnit; ++i) {
        float v = i * step;
        float y = kCy - (v - value) * pxPerUnit;
        bool major = i % 2 == 0;
        c.line(x, y, x - dir * (major ? 7.0f : 3.5f), y);
        if (major && std::fabs(y - kCy) > 13.0f && v >= 0.0f) {
            char label[12];
            std::snprintf(label, sizeof label, fmt, static_cast<int>(v));
            c.text(x - dir * 10.0f, y - kSmall * 0.5f, label, kSmall, dir < 0 ? HudCanvas::kLeft : HudCanvas::kRight);
        }
    }
    // Readout box with a pointer onto the spine.
    char buf[12];
    std::snprintf(buf, sizeof buf, fmt, static_cast<int>(value + 0.5f));
    float bw = 46.0f, b0 = x + dir * 9.0f, b1 = b0 + dir * bw;
    float xl = dir > 0 ? b0 : b1, xr = dir > 0 ? b1 : b0;
    c.line(x + dir * 2.0f, kCy, b0, kCy - 6.0f);
    c.line(x + dir * 2.0f, kCy, b0, kCy + 6.0f);
    c.line(b0, kCy - 6.0f, b0, kCy - 8.0f);
    c.line(b0, kCy + 6.0f, b0, kCy + 8.0f);
    c.line(b0, kCy - 8.0f, b1, kCy - 8.0f);
    c.line(b0, kCy + 8.0f, b1, kCy + 8.0f);
    c.line(b1, kCy - 8.0f, b1, kCy + 8.0f);
    c.text((xl + xr) * 0.5f, kCy - kText * 0.5f, buf, kText, HudCanvas::kCenter);
}

}  // namespace

void drawFlightHud(HudCanvas& c, const game::FlightState& s, const game::Countermeasures& cm, bool gOverride,
                   float time) {
    using namespace hud;
    c.reset();
    char buf[32];
    const bool blink = std::fmod(time, 0.8f) < 0.5f;  // 1.25 Hz: slow enough for the PSP-3000 LCD

    if (s.crashed) {
        c.color(kRed);
        c.width(kLineWidth);
        c.text(kCx, kCy - 10.0f, "CRASHED", 14.0f, HudCanvas::kCenter);
        return;
    }

    // --- Conformal symbols.
    c.color(kGreen);
    c.width(kLineWidth);
    float x, y;
    if (gfx::projectDirection(s.pathDir, x, y) && inGlass(x, y)) flightPathMarker(c, x, y);
    if (gfx::projectDirection(s.orientation.forward(), x, y) && inGlass(x, y)) boresight(c, x, y);
    c.color(kDim);
    c.width(2.6f);
    pitchLadder(c, s);

    // --- Tapes and readouts.
    c.color(kGreen);
    c.width(hud::kTextWidth);
    headingTape(c, s);
    tape(c, 104.0f, -1, s.speed * 3.6f, 50.0f, 0.4f, "%d");
    tape(c, 376.0f, +1, s.position.y, 50.0f, 0.4f, "%d");
    c.text(80.0f, kCy - 84.0f, "KM/H", kSmall, HudCanvas::kCenter);
    c.text(400.0f, kCy - 84.0f, "ALT M", kSmall, HudCanvas::kCenter);

    std::snprintf(buf, sizeof buf, "M %.2f", s.mach);
    c.text(58.0f, kCy + 76.0f, buf, kText);
    if (s.loadFactor > 9.0f) c.color(kAmber);
    std::snprintf(buf, sizeof buf, "G %.1f", s.loadFactor);
    c.text(58.0f, kCy + 89.0f, buf, kText);
    c.color(std::fabs(s.alpha) > 0.5f ? kAmber : kGreen);
    std::snprintf(buf, sizeof buf, "AOA %d", static_cast<int>(math::degrees(s.alpha)));
    c.text(58.0f, kCy + 102.0f, buf, kText);
    c.color(kGreen);

    float vs = s.pathDir.y * s.speed;
    std::snprintf(buf, sizeof buf, "VS %+d", static_cast<int>(vs));
    c.text(368.0f, kCy + 76.0f, buf, kText);
    if (s.position.y < 300.0f) {
        c.color(kAmber);
        std::snprintf(buf, sizeof buf, "R %d", static_cast<int>(s.position.y));
        c.text(368.0f, kCy + 89.0f, buf, kText);
        c.color(kGreen);
    }

    // Engine and stores.
    std::snprintf(buf, sizeof buf, "THR %d", static_cast<int>(s.throttle * 100.0f + 0.5f));
    c.text(16.0f, 252.0f, buf, kText);
    if (s.afterburner) {
        c.color(kAmber);
        c.text(78.0f, 252.0f, "AB", kText);
        c.color(kGreen);
    }
    if (s.airbrake) c.text(100.0f, 252.0f, "BRK", kText);
    c.color(cm.stock() < 16 ? kAmber : kGreen);
    std::snprintf(buf, sizeof buf, "FLR %d", cm.stock());
    c.text(464.0f, 252.0f, buf, kText, HudCanvas::kRight);

    // --- Mode and warnings, centre bottom.
    float t = s.position.y / std::max(-vs, 1.0f);
    if (vs < -20.0f && t < 4.5f && s.position.y < 900.0f) {
        if (blink) {
            c.color(kRed);
            c.width(kLineWidth);
            c.text(kCx, 196.0f, "PULL UP", kBig + 2.0f, HudCanvas::kCenter);
        }
    } else if (s.psm) {
        c.color(kAmber);
        c.width(kLineWidth);
        c.text(kCx, 196.0f, "PSM", kBig + 2.0f, HudCanvas::kCenter);
        c.width(hud::kTextWidth);
        c.text(kCx, 212.0f, "LIMITER OFF", kSmall, HudCanvas::kCenter);
    } else if (s.stalled && blink) {
        c.color(kRed);
        c.width(kLineWidth);
        c.text(kCx, 196.0f, "STALL", kBig + 2.0f, HudCanvas::kCenter);
    } else if (gOverride) {
        c.color(kAmber);
        c.text(kCx, 200.0f, "G-LIM 13", kText, HudCanvas::kCenter);
    }
}

}  // namespace scenes
