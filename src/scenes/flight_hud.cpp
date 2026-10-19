#include "scenes/flight_hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <utility>

#include "game/warnings.h"
#include "render/font.h"
#include "render/gu_context.h"
#include "render/view.h"
#include "ui/ui.h"
#include "world/sea.h"

namespace scenes {

using gfx::HudCanvas;
using math::Vec3;
using ui::HudFace;

namespace {

// HUD v3 (docs/decisions/0049), the A+B hybrid: everything the earlier HUD
// showed, but the text is the menus' bitmap PT Sans with a 1 px outline
// instead of a backdrop per label; backdrops only under the dense clusters
// (the speed / altitude numbers, heading, scope, stores). Alerts are Ace
// Combat plates at the top, never over the aircraft; the ladder yields to
// alerts, messages and the target block and never crosses the aircraft.
constexpr float kCx = gfx::kScreenW * 0.5f;
constexpr float kCy = gfx::kScreenH * 0.5f;
constexpr float kThin = 3.0f;  // px, ticks, ladder, scope rings: the LCD still holds them

// The chase view puts the Su-35 here: nothing conformal is drawn over it.
constexpr float kPlaneL = 128.0f, kPlaneR = 352.0f, kPlaneT = 138.0f;
// Conformal symbols live inside the glass.
constexpr float kGlassL = 96.0f, kGlassR = 384.0f, kGlassT = 26.0f, kGlassB = 240.0f;
bool inGlass(float x, float y) { return x > kGlassL && x < kGlassR && y > kGlassT && y < kGlassB; }
bool overPlane(float x, float y) { return x > kPlaneL && x < kPlaneR && y > kPlaneT; }

constexpr uint32_t kCaption = 0xAA8CFF5Au;    // green, alpha 170: captions, ladder labels
constexpr uint32_t kRingDim = 0x6E8CFF5Au;    // green, alpha 110: the lock ring's unfilled part
constexpr uint32_t kOnGreen = 0xFF0C1A06u;    // abgr(6, 26, 12): text on the selected-weapon fill
constexpr uint32_t kSelectFill = 0xD28CFF5Au; // green, alpha 210

const gfx::Font& font() { return ui::res().hudFont; }
int face(HudFace f) { return static_cast<int>(f); }
float textW(const char* s, HudFace f) { return static_cast<float>(font().width(face(f), s)); }
float capH(HudFace f) { return static_cast<float>(font().capHeight(face(f))); }

// Bitmap text, outlined; y = top of the capitals.
void text(HudCanvas& c, float x, float y, const char* s, HudFace f, uint32_t color,
          HudCanvas::Align a = HudCanvas::kLeft) {
    c.label(x, y, s, face(f), color, a);
}

uint32_t withAlpha(uint32_t abgr, float a) {
    a = math::clamp(a, 0.0f, 1.0f);
    return (abgr & 0x00FFFFFFu) | (static_cast<uint32_t>(((abgr >> 24) & 0xFF) * a + 0.5f) << 24);
}

// RGB times k, alpha kept.
uint32_t shade(uint32_t abgr, float k) {
    auto ch = [&](int s) { return static_cast<uint32_t>(((abgr >> s) & 0xFF) * k + 0.5f) << s; };
    return (abgr & 0xFF000000u) | ch(16) | ch(8) | ch(0);
}

float ease(float t) { return t * t * (3.0f - 2.0f * t); }

// Screen areas the ladder keeps clear of: the alert stack, the message line,
// the designated target's block.
struct Reserved {
    struct Rect {
        float x0, y0, x1, y1;
    };
    Rect r[8];
    int n = 0;
    void add(float x0, float y0, float x1, float y1) {
        if (n < 8) r[n++] = {x0, y0, x1, y1};
    }
    bool hit(float x0, float y0, float x1, float y1) const {
        for (int i = 0; i < n; ++i)
            if (x0 < r[i].x1 && x1 > r[i].x0 && y0 < r[i].y1 && y1 > r[i].y0) return true;
        return false;
    }
};

void flightPathMarker(HudCanvas& c, float x, float y) {
    c.circle(x, y, 6.0f, 10);
    c.line(x - 16.0f, y, x - 6.0f, y);
    c.line(x + 6.0f, y, x + 16.0f, y);
    c.line(x, y - 6.0f, x, y - 11.0f);
}

void boresight(HudCanvas& c, float x, float y) {
    // Where the nose points; in level flight it sits on the flight-path marker.
    c.line(x - 9.0f, y, x - 4.0f, y + 5.0f);
    c.line(x - 4.0f, y + 5.0f, x, y);
    c.line(x, y, x + 4.0f, y + 5.0f);
    c.line(x + 4.0f, y + 5.0f, x + 9.0f, y);
}

void gunCross(HudCanvas& c, float x, float y) {
    c.line(x - 8.0f, y, x - 3.0f, y);
    c.line(x + 3.0f, y, x + 8.0f, y);
    c.line(x, y - 8.0f, x, y - 3.0f);
    c.line(x, y + 3.0f, x, y + 8.0f);
}

void arc(HudCanvas& c, float x, float y, float r, float a0, float a1) {
    const int n = std::max(2, static_cast<int>((a1 - a0) / 0.3f) + 1);
    float px = x + std::cos(a0) * r, py = y + std::sin(a0) * r;
    for (int i = 1; i <= n; ++i) {
        const float a = a0 + (a1 - a0) * i / n;
        const float qx = x + std::cos(a) * r, qy = y + std::sin(a) * r;
        c.line(px, py, qx, qy);
        px = qx;
        py = qy;
    }
}

void arrowhead(HudCanvas& c, float x, float y, float dx, float dy, float size) {
    const float px = -dy, py = dx;
    const float tx = x + dx * size, ty = y + dy * size;
    const float bx = x - dx * size * 0.6f, by = y - dy * size * 0.6f;
    const float lx = bx + px * size * 0.7f, ly = by + py * size * 0.7f;
    const float rx = bx - px * size * 0.7f, ry = by - py * size * 0.7f;
    c.line(tx, ty, lx, ly);
    c.line(lx, ly, rx, ry);
    c.line(rx, ry, tx, ty);
}

Vec3 horizontalHeading(const game::FlightState& s) {
    Vec3 h{s.pathDir.x, 0.0f, s.pathDir.z};
    if (math::length(h) < 0.05f) h = Vec3{s.orientation.forward().x, 0.0f, s.orientation.forward().z};
    if (math::length(h) < 0.05f) h = math::kForward;
    return math::normalize(h);
}

// ---------------------------------------------------------------- alerts
// Ace Combat plates (filled, the text cut out and darkened) for dangers and
// cautions; an amber frame for modes the pilot chose (PSM, G-LIMIT OFF).
struct Alert {
    const ui::AtlasRect* plate = nullptr;  // null: a framed text
    const char* text = nullptr;
    uint32_t color = 0;
    bool blink = false;
    char sub[24] = {};    // the line under it (AOA, AUTO TURN)
    bool homeArrow = false;
};

Alert pickAlert(const game::FlightState& s, bool gOverride, const CombatView& v, const game::Rwr* rwr) {
    using namespace hud;
    Alert a;
    const bool missile = rwr && rwr->missile();
    // Same rule the voice informer uses for "высота опасная" (game/warnings.h).
    if (game::groundWarning(s, v.terrain) == game::GroundWarning::PullUp) {
        a = {&ui::plates::kPullUp, nullptr, kRed, true};
    } else if (missile) {
        a = {&ui::plates::kMissile, nullptr, kRed, true};
    } else if (v.area == 2) {
        a = {&ui::plates::kTurningBack, nullptr, kAmber, false};
        a.homeArrow = true;
    } else if (v.area == 1) {
        a = {&ui::plates::kReturnToArea, nullptr, kAmber, true};
        a.homeArrow = true;
        std::snprintf(a.sub, sizeof a.sub, "AUTO TURN %d", static_cast<int>(std::ceil(std::max(v.areaCountdown, 0.0f))));
    } else if (rwr && rwr->worst() == game::RwrLevel::Lock) {
        a = {&ui::plates::kRadarLock, nullptr, kAmber, false};
    } else if (s.psm) {
        a = {nullptr, "PSM", kAmber, false};
        std::snprintf(a.sub, sizeof a.sub, "AOA %d", static_cast<int>(math::degrees(s.alpha)));
    } else if (s.stalled) {
        a = {&ui::plates::kStall, nullptr, kRed, true};
    } else if (gOverride) {
        a = {nullptr, "G-LIMIT OFF", kAmber, false};
    }
    return a;
}

constexpr float kAlertY = 27.0f, kAlertH = 26.0f;

void plate(HudCanvas& c, const ui::AtlasRect& r, float cx, float y, uint32_t tint) {
    c.sprite(r.x, r.y, r.w, r.h, cx - r.w * 0.5f, y, tint);
}

// Draws the alert stack under the heading; returns the next free line.
float drawAlert(HudCanvas& c, const Alert& a, bool blink, float homeBearing, Reserved& res) {
    using namespace hud;
    float y = kAlertY;
    if (!a.plate && !a.text) return y;
    float w;
    if (a.plate) {
        // Blinking = the plate pulses darker, it never fades out (slow
        // PSP-3000 LCD; a translucent plate lost its letters on the sky).
        plate(c, *a.plate, kCx, y, shade(a.color | 0xFF000000u, a.blink && !blink ? 0.72f : 1.0f));
        w = a.plate->w;
    } else {
        w = textW(a.text, HudFace::Large) + 20.0f;
        c.panel(kCx - w * 0.5f, y, kCx + w * 0.5f, y + kAlertH, kBackdrop);
        c.color(a.color);
        c.width(kLineWidth);
        c.box(kCx - w * 0.5f, y, kCx + w * 0.5f, y + kAlertH);
        text(c, kCx, y + (kAlertH - capH(HudFace::Large)) * 0.5f, a.text, HudFace::Large, a.color, HudCanvas::kCenter);
    }
    res.add(kCx - w * 0.5f - 4.0f, y - 2.0f, kCx + w * 0.5f + 4.0f, y + kAlertH + 2.0f);
    y += kAlertH + 4.0f;
    if (a.sub[0]) {
        text(c, kCx, y, a.sub, HudFace::Small, a.color, HudCanvas::kCenter);
        res.add(kCx - 50.0f, y - 2.0f, kCx + 50.0f, y + capH(HudFace::Small) + 3.0f);
        y += capH(HudFace::Small) + 6.0f;
    }
    if (a.homeArrow) {
        // Where home is, 0 = straight ahead (up on the screen).
        const float cy = y + 12.0f, r = 10.0f;
        const float sn = std::sin(homeBearing), cs = std::cos(homeBearing);
        auto pt = [&](float x, float yy) { return std::make_pair(kCx + x * cs - yy * sn, cy + x * sn + yy * cs); };
        c.disc(kCx, cy, r + 4.0f, kBackdrop);
        c.color(a.color);
        c.width(kLineWidth);
        const auto tip = pt(0.0f, -r), l = pt(-r * 0.7f, r * 0.2f), rr = pt(r * 0.7f, r * 0.2f), tail = pt(0.0f, r);
        c.line(tip.first, tip.second, l.first, l.second);
        c.line(tip.first, tip.second, rr.first, rr.second);
        c.line(tip.first, tip.second, tail.first, tail.second);
        res.add(kCx - r - 6.0f, cy - r - 6.0f, kCx + r + 6.0f, cy + r + 6.0f);
        y = cy + r + 8.0f;
    }
    return y;
}

// ---------------------------------------------------------------- flight data

// Horizon plus rungs every 10 deg, projected through the chase camera; one
// upright label right of each rung. A rung half yields to the aircraft and
// to the reserved areas; the horizon always shows.
void pitchLadder(HudCanvas& c, const game::FlightState& s, const Reserved& res) {
    using namespace hud;
    const Vec3 h = horizontalHeading(s);
    const Vec3 hr = math::cross(h, math::kUp);
    const float gamma = math::degrees(std::asin(math::clamp(s.pathDir.y, -1.0f, 1.0f)));
    const int lo = static_cast<int>(std::floor((gamma - 35.0f) / 10.0f)) * 10;
    char buf[8];
    for (int deg = lo; deg <= gamma + 35.0f; deg += 10) {
        if (deg < -90 || deg > 90) continue;
        const float th = math::radians(static_cast<float>(deg));
        const Vec3 d = h * std::cos(th) + math::kUp * std::sin(th);
        float cx, cy, px, py;
        if (!gfx::projectDirection(d, cx, cy) || !gfx::projectDirection(d + hr * 0.02f, px, py)) continue;
        if (!inGlass(cx, cy)) continue;
        float dx = px - cx, dy = py - cy;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) continue;
        dx /= len;
        dy /= len;
        const bool horizon = deg == 0;
        const float gap = horizon ? 28.0f : 18.0f, half = horizon ? 130.0f : 50.0f;
        c.color(horizon ? withAlpha(kGreen, 0.72f) : withAlpha(kGreen, 0.62f));
        c.width(kThin);
        for (int side = -1; side <= 1; side += 2) {
            const float x0 = cx + dx * gap * side, y0 = cy + dy * gap * side;
            const float x1 = cx + dx * half * side, y1 = cy + dy * half * side;
            if (!horizon) {
                const float mx = 0.5f * (x0 + x1), my = 0.5f * (y0 + y1);
                if (overPlane(mx, my) || !inGlass(mx, my)) continue;
                if (res.hit(std::min(x0, x1), std::min(y0, y1) - 2.0f, std::max(x0, x1), std::max(y0, y1) + 2.0f))
                    continue;
            }
            if (deg < 0)
                c.dashed(x0, y0, x1, y1, 7.0f, 5.0f);
            else
                c.line(x0, y0, x1, y1);
        }
        if (horizon) continue;
        std::snprintf(buf, sizeof buf, "%d", deg);
        const float lx = cx + dx * (half + 6.0f), ly = cy + dy * (half + 6.0f) - 0.5f * capH(HudFace::Small);
        const float w = textW(buf, HudFace::Small);
        if (overPlane(lx, ly) || !inGlass(lx, ly) || res.hit(lx - 2.0f, ly - 2.0f, lx + w + 2.0f, ly + 12.0f)) continue;
        text(c, lx, ly, buf, HudFace::Small, kCaption);
    }
}

void headingTape(HudCanvas& c, const game::FlightState& s) {
    using namespace hud;
    const Vec3 h = horizontalHeading(s);
    float heading = math::degrees(std::atan2(h.x, -h.z));  // 0 = start heading ("north"), clockwise
    if (heading < 0.0f) heading += 360.0f;
    constexpr float kPxPerDeg = 1.5f, kHalf = 68.0f, kTop = 4.0f;
    const float cap = capH(HudFace::Small);
    const int first = static_cast<int>(std::ceil((heading - kHalf / kPxPerDeg) / 10.0f)) * 10;
    char buf[8];
    for (int d = first; d <= heading + kHalf / kPxPerDeg; d += 10) {
        const float x = kCx + (d - heading) * kPxPerDeg;
        if (std::fabs(x - kCx) < 22.0f) continue;
        const int norm = ((d % 360) + 360) % 360;
        if (norm % 90 == 0) {
            static const char* const kNames[] = {"N", "E", "S", "W"};
            text(c, x, kTop + 5.0f, kNames[norm / 90], HudFace::Small, kGreen, HudCanvas::kCenter);
        } else if (norm % 30 == 0) {
            std::snprintf(buf, sizeof buf, "%d", norm / 10);
            text(c, x, kTop + 5.0f, buf, HudFace::Small, kCaption, HudCanvas::kCenter);
        } else {
            c.color(kDim);
            c.width(kThin);
            c.line(x, kTop + 6.0f, x, kTop + 12.0f);
        }
    }
    std::snprintf(buf, sizeof buf, "%03d", static_cast<int>(heading + 0.5f) % 360);
    const float x0 = kCx - 18.0f, x1 = kCx + 18.0f, y0 = kTop, y1 = kTop + cap + 9.0f;
    c.panel(x0, y0, x1, y1, kBackdrop);
    c.color(kGreen);
    c.width(kLineWidth);
    c.box(x0, y0, x1, y1);
    text(c, kCx, y0 + 5.0f, buf, HudFace::Small, kGreen, HudCanvas::kCenter);
}

// Caption, a boxed big number and a moving tick tape on the inner side (the
// ticks give the rate, the number the value).
void readout(HudCanvas& c, float x0, int inner, const char* caption, float value, uint32_t color) {
    using namespace hud;
    constexpr float kW = 70.0f, kBoxT = 115.0f, kBoxB = 141.0f, kStep = 50.0f, kPx = 8.0f;
    const float x1 = x0 + kW, mid = 0.5f * (x0 + x1);
    text(c, mid, kBoxT - 5.0f - capH(HudFace::Small), caption, HudFace::Small, kCaption, HudCanvas::kCenter);
    c.panel(x0, kBoxT, x1, kBoxB, kBackdrop);
    c.color(color);
    c.width(kLineWidth);
    c.box(x0, kBoxT, x1, kBoxB);
    char buf[12];
    std::snprintf(buf, sizeof buf, "%d", static_cast<int>(value + 0.5f));
    text(c, mid, 0.5f * (kBoxT + kBoxB) - 0.5f * capH(HudFace::Number), buf, HudFace::Number, color, HudCanvas::kCenter);

    const float sx = inner > 0 ? x1 + 6.0f : x0 - 6.0f;  // tape spine
    const float phase = std::fmod(value, kStep) / kStep;
    const int base = static_cast<int>(std::floor(value / kStep));
    c.color(kDim);
    c.width(kThin);
    for (int i = -5; i <= 5; ++i) {
        const float y = kCy + (i + phase) * kPx;
        if (y < 90.0f || y > 168.0f) continue;
        const bool major = ((base - i) & 1) == 0;
        c.line(sx, y, sx + inner * (major ? 7.0f : 4.0f), y);
    }
    c.color(color);
    c.width(kLineWidth);
    arrowhead(c, sx - inner * 2.0f, kCy, -static_cast<float>(inner), 0.0f, 3.5f);
}

// ---------------------------------------------------------------- targeting
// Ace Combat-like targeting on the aircraft's own sensors (docs/decisions/
// 0028): boxes sit where the RADAR thinks the targets are (a coasting track
// drifts, dashed). Lock-on is gradual (0050): a ring travels from the nose to
// the target and fills; at the lock it snaps to a red diamond.

constexpr float kRadarX = 48.0f, kRadarY = 224.0f, kRadarR = 37.0f;
constexpr float kRadarRange = 10000.0f;  // m at the rim

void dashedBox(HudCanvas& c, float x0, float y0, float x1, float y1) {
    c.dashed(x0, y0, x1, y0, 6.0f, 5.0f);
    c.dashed(x1, y0, x1, y1, 6.0f, 5.0f);
    c.dashed(x1, y1, x0, y1, 6.0f, 5.0f);
    c.dashed(x0, y1, x0, y0, 6.0f, 5.0f);
}

void brackets(HudCanvas& c, float x, float y, float h) {
    const float l = h * 0.4f;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
            const float cx = x + sx * h, cy = y + sy * h;
            c.line(cx, cy, cx - sx * l, cy);
            c.line(cx, cy, cx, cy - sy * l);
        }
}

// The lock: a diamond with four arrowheads pointing in.
void lockDiamond(HudCanvas& c, float x, float y, float r) {
    c.line(x, y - r, x + r, y);
    c.line(x + r, y, x, y + r);
    c.line(x, y + r, x - r, y);
    c.line(x - r, y, x, y - r);
    const float k = r + 7.0f;
    arrowhead(c, x, y - k, 0.0f, 1.0f, 4.0f);
    arrowhead(c, x + k, y, -1.0f, 0.0f, 4.0f);
    arrowhead(c, x, y + k, 0.0f, -1.0f, 4.0f);
    arrowhead(c, x - k, y, 1.0f, 0.0f, 4.0f);
}

bool offscreenArrow(HudCanvas& c, const Vec3& world, float& ax, float& ay) {
    const gfx::CameraBasis& cam = gfx::camera();
    const Vec3 rel = world - cam.eye;
    float dx = math::dot(rel, cam.right), dy = -math::dot(rel, cam.up);
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-3f) {
        dx = 0.0f;
        dy = 1.0f;
    } else {
        dx /= len;
        dy /= len;
    }
    ax = kCx + dx * 72.0f;
    ay = kCy + dy * 72.0f;
    arrowhead(c, ax, ay, dx, dy, 11.0f);
    return true;
}

// Target boxes, the lock ring / diamond, the gun pipper. Reserves the
// designated target's block for the ladder. Returns true if the gun cross
// replaced the nose symbol.
bool targetSymbols(HudCanvas& c, const game::FlightState& s, const CombatView& v, float time, Reserved& res) {
    using namespace hud;
    const game::Radar& radar = *v.radar;
    const game::Armament& arms = *v.arms;
    const int d = radar.designated();
    const bool locked = arms.locked();
    const bool shoot = locked && arms.inZone();
    const bool blink = std::fmod(time, 0.5f) < 0.32f;
    const float capS = capH(HudFace::Small);
    char buf[16];

    for (int i = 0; i < game::kMaxTargets; ++i) {
        const game::Track& tr = radar.track(i);
        if (!tr.valid) continue;
        const bool des = i == d;
        // A TWS track is refreshed once a scan; "coasting" = it missed a look.
        const bool coasting = tr.sinceReturn > (des ? 0.3f : radar.params().scanPeriod * 1.2f);
        const uint32_t color = des && locked ? kRed : kGreen;
        const float km = math::length(tr.pos - s.position) * 0.001f;
        std::snprintf(buf, sizeof buf, km < 10.0f ? "%.1f KM" : "%.0f KM", km);
        float x, y;
        const bool front = gfx::projectPoint(tr.pos, x, y);
        if (!front || x < 8.0f || x > gfx::kScreenW - 8.0f || y < 8.0f || y > gfx::kScreenH - 8.0f) {
            if (des) {
                float ax, ay;
                c.color(color);
                c.width(kLineWidth);
                offscreenArrow(c, tr.pos, ax, ay);
                text(c, ax, ay + 12.0f, buf, HudFace::Small, color, HudCanvas::kCenter);
            }
            continue;
        }
        const float h = des ? 10.0f : 8.0f;
        c.color(coasting ? kDim : color);
        c.width(kLineWidth);
        if (des && locked && !coasting) {
            const float age = arms.lockAge();
            lockDiamond(c, x, y, 14.0f);
            if (age < 0.25f) {  // the snap: a ring that closes in and fades
                c.color(withAlpha(kRed, 1.0f - age / 0.25f));
                c.width(kThin);
                c.circle(x, y, 27.0f + 10.0f * (1.0f - age / 0.25f), 24);
            }
        } else if (coasting) {
            dashedBox(c, x - h, y - h, x + h, y + h);
        } else {
            c.box(x - h, y - h, x + h, y + h);
        }
        if (!des) continue;
        if (!locked || coasting) brackets(c, x, y, h + 5.0f);
        res.add(x - 44.0f, y - 38.0f, x + 72.0f, y + 40.0f);
        const char* name = v.targetNames[i] ? v.targetNames[i] : v.targetName;
        text(c, x, y - 26.0f - capS, name, HudFace::Small, color, HudCanvas::kCenter);
        text(c, x, y + 23.0f, buf, HudFace::Small, color, HudCanvas::kCenter);
        if (coasting)
            text(c, x, y + 23.0f + capS + 4.0f, "MEM", HudFace::Small, kAmber, HudCanvas::kCenter);
        else if (shoot && blink)
            text(c, x + 28.0f, y - 0.5f * capS, "SHOOT", HudFace::Small, kRed);
    }

    // The lock ring: where the head (R-73) / the radar (R-77) looks now. It
    // leaves the nose as the acquisition starts, shrinks and fills on the way
    // to the target; caged on the nose while the R-73 has nothing.
    float x, y;
    const bool cueing = d >= 0 && radar.track(d).valid;
    const bool r73 = arms.selected() == game::MissileSlot::R73;
    if (!locked && (cueing || r73) && gfx::projectDirection(arms.lockDir(), x, y)) {
        const float p = arms.lockProgress();
        const float r = 22.0f - 10.0f * ease(p);
        c.color(kRingDim);
        c.width(kThin);
        c.circle(x, y, r, 20);
        if (p > 0.0f) {
            c.color(kGreen);
            c.width(kLineWidth);
            arc(c, x, y, r, -0.5f * math::kPi, -0.5f * math::kPi + 2.0f * math::kPi * p);
        }
    } else if (locked && (d < 0 || !radar.track(d).valid) && gfx::projectDirection(arms.lockDir(), x, y)) {
        // An R-73 that locked on its own (nothing designated): the diamond on the head's line.
        c.color(kRed);
        c.width(kLineWidth);
        lockDiamond(c, x, y, 14.0f);
    }

    // Gun: lead pipper inside gun range; the gun cross (where rounds go)
    // replaces the nose symbol meanwhile - put the cross in the ring.
    const game::Gun& gun = arms.gun();
    bool pipper = false;
    c.width(kLineWidth);
    if (cueing) {
        const game::Track& tr = radar.track(d);
        Vec3 dir;
        float tof;
        if (math::length(tr.pos - s.position) < 1800.0f && gun.lead(s, tr.pos, tr.vel, dir, tof) &&
            gfx::projectDirection(dir, x, y)) {
            c.color(kGreen);
            c.circle(x, y, 11.0f, 14);
            pipper = true;
        }
    }
    if ((pipper || gun.firing()) && gfx::projectDirection(gun.shellDirection(s), x, y)) {
        c.color(kGreen);
        gunCross(c, x, y);
        return true;
    }
    return false;
}

// Missile warning around the screen centre, Ace Combat 7's red bar: an arc on
// a ring, centred on the direction of the missile as seen through the camera.
void missileRing(HudCanvas& c, const Vec3& dirWorld) {
    using namespace hud;
    const gfx::CameraBasis& cam = gfx::camera();
    float dx = math::dot(dirWorld, cam.right), dy = -math::dot(dirWorld, cam.up);
    const float len = std::sqrt(dx * dx + dy * dy);
    const float a = len < 1e-3f ? math::kPi * 0.5f : std::atan2(dy, dx);
    c.color(kRed);
    c.width(6.0f);
    arc(c, kCx, kCy, 88.0f, a - 0.35f, a + 0.35f);
}

// Heading-up radar scope on a dark disc: own aircraft in the middle, contacts
// as arrowheads along their track, the designated one ringed, the radar's
// +-60 deg scan limits dashed, a 5 km ring, islands and stacks quietly.
void radarScope(HudCanvas& c, const game::FlightState& s, const CombatView& v, float time) {
    using namespace hud;
    const game::Radar& radar = *v.radar;
    const Vec3 h = horizontalHeading(s);
    const Vec3 r = math::cross(h, math::kUp);
    const float k = kRadarR / kRadarRange;
    c.disc(kRadarX, kRadarY, kRadarR + 3.0f, kBackdrop);
    c.color(kDim);
    c.width(kThin);
    c.circle(kRadarX, kRadarY, kRadarR, 24);
    c.color(kRingDim);
    c.circle(kRadarX, kRadarY, kRadarR * 0.5f, 16);
    const float az = radar.params().searchAz;
    for (int side = -1; side <= 1; side += 2)
        c.dashed(kRadarX, kRadarY, kRadarX + side * std::sin(az) * kRadarR, kRadarY - std::cos(az) * kRadarR, 5.0f,
                 4.0f);
    // Islands and stacks in range (docs/decisions/0042): what blocks sight and
    // missiles. Coastlines of the islands, a dot for each stack.
    if (v.terrain) {
        const ::world::Terrain& t = *v.terrain;
        c.color(kTerrainInk);
        c.width(kThin);
        const float rim2 = kRadarR * kRadarR;
        for (int i = 0; i < t.count(); ++i) {
            const ::world::Rock& rock = t.rock(i);
            const ::world::RockShape& sh = t.shape(i);
            if (sh.apex < 30.0f) continue;  // skerries: awash, no cover
            const Vec3 rel{rock.x - s.position.x, 0.0f, rock.z - s.position.z};
            const float cx = math::dot(rel, r) * k, cy = math::dot(rel, h) * k;
            const float reach = kRadarR + sh.maxRadius * k;
            if (cx * cx + cy * cy > reach * reach) continue;
            if (sh.maxRadius < 250.0f) {  // a stack: a dot (crosses turned the scope into noise)
                if (cx * cx + cy * cy > rim2) continue;
                c.width(2.6f);
                c.line(kRadarX + cx - 0.5f, kRadarY - cy, kRadarX + cx + 0.5f, kRadarY - cy);
                c.width(kThin);
                continue;
            }
            constexpr int kSides = 24;
            float px0 = 0.0f, py0 = 0.0f;
            bool in0 = false;
            for (int n = 0; n <= kSides; ++n) {
                const float th = 6.2831853f * n / kSides;  // world azimuth
                const float wr = t.waterlineRadius(i, th);
                const Vec3 p{rel.x + world::fastCos(th) * wr, 0.0f, rel.z + world::fastSin(th) * wr};
                const float px = math::dot(p, r) * k, py = math::dot(p, h) * k;
                const bool in = px * px + py * py <= rim2;
                if (n > 0 && in && in0) c.line(kRadarX + px0, kRadarY - py0, kRadarX + px, kRadarY - py);
                px0 = px;
                py0 = py;
                in0 = in;
            }
        }
    }
    c.color(kGreen);
    c.width(kLineWidth);
    arrowhead(c, kRadarX, kRadarY, 0.0f, -1.0f, 5.0f);

    const int d = radar.designated();
    const bool blink = std::fmod(time, 0.5f) < 0.32f;
    for (int i = 0; i < game::kMaxTargets; ++i) {
        const game::Track& tr = radar.track(i);
        if (!tr.valid) continue;
        const Vec3 rel = tr.pos - s.position;
        float px = math::dot(rel, r) * k, py = math::dot(rel, h) * k;
        const float dist = std::sqrt(px * px + py * py);
        if (dist > kRadarR * 0.92f) {
            px *= kRadarR * 0.92f / dist;
            py *= kRadarR * 0.92f / dist;
        }
        float hx = math::dot(tr.vel, r), hy = -math::dot(tr.vel, h);
        const float hl = std::sqrt(hx * hx + hy * hy);
        if (hl > 1e-3f) {
            hx /= hl;
            hy /= hl;
        } else {
            hx = 0.0f;
            hy = -1.0f;
        }
        const bool coasting = tr.sinceReturn > (i == d ? 0.3f : radar.params().scanPeriod * 1.2f);
        if (coasting && !blink) continue;
        c.color(i == d && v.arms->locked() ? kRed : kGreen);
        c.width(kLineWidth);
        arrowhead(c, kRadarX + px, kRadarY - py, hx, hy, 5.5f);
        if (i == d) c.circle(kRadarX + px, kRadarY - py, 8.0f, 12);
    }
    // RWR (docs/decisions/0034): bearings on the rim - a tick for a radar
    // sweeping us, an arrowhead pointing in for a lock, a red one for a missile.
    if (v.rwr) {
        auto rim = [&](const Vec3& dir, float& x, float& y, float& dx, float& dy) {
            float px = math::dot(dir, r), py = math::dot(dir, h);
            const float l = std::sqrt(px * px + py * py);
            if (l < 1e-3f) {
                px = 0.0f;
                py = 1.0f;
            } else {
                px /= l;
                py /= l;
            }
            dx = px;
            dy = -py;
            x = kRadarX + dx * (kRadarR + 7.0f);
            y = kRadarY + dy * (kRadarR + 7.0f);
        };
        float x, y, dx, dy;
        for (int i = 0; i < game::Rwr::kMaxEmitters; ++i) {
            const game::RwrContact& rc = v.rwr->contact(i);
            if (rc.level == game::RwrLevel::None) continue;
            rim(rc.dir, x, y, dx, dy);
            if (rc.level == game::RwrLevel::Lock) {
                c.color(kAmber);
                c.width(kLineWidth);
                arrowhead(c, x, y, -dx, -dy, 6.0f);
            } else {
                c.color(kGreen);
                c.width(4.0f);
                c.line(x - dx * 4.0f, y - dy * 4.0f, x + dx * 4.0f, y + dy * 4.0f);
            }
        }
        if (v.rwr->missile() && blink) {
            rim(v.rwr->missileDir(), x, y, dx, dy);
            c.color(kRed);
            c.width(kLineWidth + 0.6f);
            arrowhead(c, x, y, -dx, -dy, 8.0f);
        }
    }

    // Own missiles in flight: dots.
    c.color(kGreen);
    c.width(kLineWidth);
    for (int i = 0; i < game::Armament::kMaxMissiles; ++i) {
        const game::Missile& m = v.arms->missile(i);
        if (!m.live()) continue;
        const Vec3 rel = m.pos - s.position;
        const float px = math::dot(rel, r) * k, py = math::dot(rel, h) * k;
        if (px * px + py * py > kRadarR * kRadarR) continue;
        c.line(kRadarX + px - 1.5f, kRadarY - py, kRadarX + px + 1.5f, kRadarY - py);
    }
}

// Su-35S from above, metres (x right, z aft): the stores panel's silhouette.
constexpr float kPlan[][2] = {
    {0.0f, -11.0f}, {0.6f, -9.0f}, {0.9f, -6.0f}, {1.3f, -3.5f}, {2.6f, 0.2f}, {7.6f, 3.7f}, {7.6f, 4.7f},
    {3.0f, 4.6f},   {2.4f, 5.3f},  {4.8f, 8.7f},  {4.8f, 9.7f},  {2.0f, 9.7f}, {1.4f, 11.0f}, {0.0f, 10.4f},
};

// Ace Combat's weapon panel: the stores list (the selected weapon filled) and
// the Su-35S from above with every missile on its pylon - the selected type
// bright, the other dim; a pylon awaiting its reload shows a short dash.
void storesPanel(HudCanvas& c, const game::Armament& a, const game::Countermeasures& cm, float health) {
    using namespace hud;
    constexpr float kL = 352.0f, kR = 476.0f, kT = 200.0f, kB = 268.0f, kRow = 16.0f;
    c.panel(kL, kT, kR, kB, kBackdrop);
    char buf[16];
    // Ace Combat's DMG: shown once we've been hit.
    if (health < 99.5f) {
        std::snprintf(buf, sizeof buf, "DMG %d%%", static_cast<int>(100.0f - health + 0.5f));
        text(c, kR - 2.0f, kT - 6.0f - capH(HudFace::Small), buf, HudFace::Small, health < 40.0f ? kRed : kAmber,
             HudCanvas::kRight);
    }
    const game::Gun& gun = a.gun();
    struct Row {
        const char* name;
        int value;
        bool selected, warn;
    } rows[4] = {
        {"GUN", gun.rounds(), false, gun.rounds() == 0},
        {"R-73", a.ready(game::MissileSlot::R73) + a.reserve(game::MissileSlot::R73),
         a.selected() == game::MissileSlot::R73, false},
        {"R-77", a.ready(game::MissileSlot::R77) + a.reserve(game::MissileSlot::R77),
         a.selected() == game::MissileSlot::R77, false},
        {"FLR", cm.stock(), false, cm.stock() < 16},
    };
    const float cap = capH(HudFace::Small);
    for (int i = 0; i < 4; ++i) {
        const float y0 = kT + 3.0f + i * kRow, ty = y0 + 0.5f * (kRow - 1.0f - cap);
        if (i == 0 && gun.rounds() == 0)
            std::snprintf(buf, sizeof buf, "RLD");
        else
            std::snprintf(buf, sizeof buf, "%d", rows[i].value);
        if (rows[i].selected) {
            c.panel(kL + 3.0f, y0, kL + 70.0f, y0 + kRow - 1.0f, kSelectFill);
            c.label(kL + 7.0f, ty, rows[i].name, face(HudFace::Small), kOnGreen, HudCanvas::kLeft, false);
            c.label(kL + 66.0f, ty, buf, face(HudFace::Small), kOnGreen, HudCanvas::kRight, false);
        } else {
            const uint32_t col = rows[i].warn ? kAmber : kGreen;
            text(c, kL + 7.0f, ty, rows[i].name, HudFace::Small, col);
            text(c, kL + 66.0f, ty, buf, HudFace::Small, col, HudCanvas::kRight);
        }
    }
    // The silhouette and the pylons.
    constexpr float kSx = 450.0f, kSy = 234.0f, kK = 2.0f;  // px per metre
    constexpr int kN = sizeof kPlan / sizeof kPlan[0];
    c.color(withAlpha(kGreen, 0.55f));
    c.width(2.6f);
    for (int side = -1; side <= 1; side += 2)
        for (int i = 0; i + 1 < kN; ++i)
            c.line(kSx + side * kPlan[i][0] * kK, kSy + kPlan[i][1] * kK, kSx + side * kPlan[i + 1][0] * kK,
                   kSy + kPlan[i + 1][1] * kK);
    const game::MissileParams* sel = &a.selectedType();
    for (int i = 0; i < game::kStationCount; ++i) {
        if (!a.carried(i)) continue;
        const game::Station& st = game::kSu35SStations[i];
        const float x = kSx + st.attach.x * kK, y = kSy + st.attach.z * kK;
        const bool mine = st.type == sel;
        if (a.loaded(i)) {
            c.color(mine ? kGreen : kDim);
            c.width(mine ? 3.6f : 2.6f);
            c.line(x, y - 3.0f, x, y + 3.0f);
        } else {
            c.color(kDim);
            c.width(2.4f);
            c.line(x - 1.5f, y + 3.0f, x + 1.5f, y + 3.0f);
        }
    }
}

// Tally, top left: the objective, the enemy, the score.
void tally(HudCanvas& c, const CombatView& v) {
    using namespace hud;
    char buf[40];
    float y = 6.0f;
    const float step = capH(HudFace::Small) + 5.0f;
    if (v.objectiveTotal > 0) {
        std::snprintf(buf, sizeof buf, "TARGETS %d/%d", v.objectiveDone, v.objectiveTotal);
        text(c, 8.0f, y, buf, HudFace::Small, v.objectiveDone >= v.objectiveTotal ? kGreen : kAmber);
        y += step;
    }
    if (v.hasEnemy) {
        std::snprintf(buf, sizeof buf, "%s %s", v.targetName, v.targetMode);
        text(c, 8.0f, y, buf, HudFace::Small, kGreen);
        y += step;
        std::snprintf(buf, sizeof buf, "HP %d  KILL %d  LOST %d", static_cast<int>(v.targetHealth + 0.5f), v.hits,
                      v.kills);
        text(c, 8.0f, y, buf, HudFace::Small, v.targetHealth < 40.0f ? kAmber : kCaption);
    }
}

// The mission's closing banner: a big plate, until the sortie ends.
void missionBanner(HudCanvas& c, const CombatView& v) {
    if (!v.banner) return;
    const ui::AtlasRect* p = nullptr;
    if (std::strcmp(v.banner, "MISSION COMPLETE") == 0) p = &ui::plates::kMissionComplete;
    if (std::strcmp(v.banner, "MISSION FAILED") == 0) p = &ui::plates::kMissionFailed;
    if (p)
        plate(c, *p, kCx, 84.0f, v.bannerColor | 0xFF000000u);
    else
        text(c, kCx, 90.0f, v.banner, HudFace::Large, v.bannerColor, HudCanvas::kCenter);
}

}  // namespace

void drawFlightHud(HudCanvas& c, const game::FlightState& s, const game::Countermeasures& cm, bool gOverride,
                   float time, const CombatView& combat) {
    using namespace hud;
    c.reset();
    char buf[32];
    const bool blink = std::fmod(time, 0.8f) < 0.5f;  // 1.25 Hz: slow enough for the PSP-3000 LCD
    Reserved res;

    if (s.crashed) {
        plate(c, ui::plates::kCrashed, kCx, kAlertY, kRed | 0xFF000000u);
        missionBanner(c, combat);  // the failure shows on the wreck
        return;
    }
    const bool combatOn = combat.radar && combat.arms;

    // --- Top centre: heading, then the alert stack and the message line.
    headingTape(c, s);
    const Alert alert = pickAlert(s, gOverride, combat, combat.rwr);
    float y = drawAlert(c, alert, blink, combat.homeBearing, res);
    if (combat.message && combat.messageAge < 1.8f) {
        text(c, kCx, y, combat.message, HudFace::Large, combat.messageColor, HudCanvas::kCenter);
        res.add(kCx - 70.0f, y - 2.0f, kCx + 70.0f, y + capH(HudFace::Large) + 3.0f);
    }
    if (combat.rwr && combat.rwr->missile()) missileRing(c, combat.rwr->missileDir());

    // --- Centre: targets first (they reserve their block), then the ladder,
    // the flight-path marker and the nose (or the gun cross).
    const bool gunCrossShown = combatOn && targetSymbols(c, s, combat, time, res);
    pitchLadder(c, s, res);
    float x, yy;
    c.color(kGreen);
    c.width(kLineWidth);
    if (gfx::projectDirection(s.pathDir, x, yy) && inGlass(x, yy)) flightPathMarker(c, x, yy);
    if (!gunCrossShown && gfx::projectDirection(s.orientation.forward(), x, yy) && inGlass(x, yy)) boresight(c, x, yy);

    // --- Sides: speed and altitude; Mach, G and the engine state under the speed.
    readout(c, 10.0f, +1, "SPEED", s.speed * 3.6f, kGreen);
    readout(c, 400.0f, -1, "ALT", s.position.y, s.position.y < 150.0f ? kAmber : kGreen);
    const float yLine = 147.0f;
    std::snprintf(buf, sizeof buf, "M %.2f", s.mach);
    text(c, 12.0f, yLine, buf, HudFace::Small, kGreen);
    std::snprintf(buf, sizeof buf, "G %.1f", s.loadFactor);
    text(c, 78.0f, yLine, buf, HudFace::Small, s.loadFactor > 9.0f ? kAmber : kGreen, HudCanvas::kRight);
    const float yEngine = yLine + capH(HudFace::Small) + 5.0f;
    if (s.afterburner)
        text(c, 45.0f, yEngine, "AB", HudFace::Small, kAmber, HudCanvas::kCenter);
    else if (s.airbrake)
        text(c, 45.0f, yEngine, "BRAKE", HudFace::Small, kGreen, HudCanvas::kCenter);

    // --- Corners: tally, scope, stores.
    if (combatOn) {
        tally(c, combat);
        radarScope(c, s, combat, time);
        storesPanel(c, *combat.arms, cm, combat.health);
    }
    missionBanner(c, combat);
}

}  // namespace scenes
