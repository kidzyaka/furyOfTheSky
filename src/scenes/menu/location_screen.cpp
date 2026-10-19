#include "scenes/menu/location_screen.h"

#include <pspctrl.h>

#include <cstdio>

#include "render/draw2d.h"
#include "render/gu_context.h"
#include "scenes/menu/mission_ui.h"
#include "ui/anim.h"
#include "ui/ui.h"

namespace menu {

namespace {

constexpr int kListX = ui::kMargin, kListW = 282, kListTop = 62;
constexpr int kRowH = 28, kPitch = 32;
constexpr int kViewTop = 56, kViewBottom = ui::kBarY - 34;  // rows keep clear of the bar's shadow
constexpr int kPanelX = 308, kPanelY = 62, kPanelW = 158, kPanelH = 158;
constexpr int kThumbW = 96, kThumbH = 48;

}  // namespace

void LocationScreen::enter(Context& ctx) {
    const int loc = mui::clampi(ctx.location, 0, meta::locationCount() - 1);
    const int cleared = meta::clearedCount(ctx.profile, loc);
    // The cursor stays where it was when coming back from a briefing; a new
    // location, or a win that opened the next mission, puts it on the newest open one.
    if (loc != loc_ || cleared != cleared_) {
        loc_ = loc;
        cleared_ = cleared;
        cursor_ = 0;
        for (int m = 0; m < meta::location(loc).missionCount; ++m)
            if (meta::missionUnlocked(ctx.profile, loc, m)) cursor_ = m;
        for (int m = 0; m < meta::kMaxMissions; ++m) focus_[m] = m == cursor_ ? 1.0f : 0.0f;
        scroll_ = 0.0f;
        preview_ = 1.0f;
    }
    ctx.location = loc_;
}

void LocationScreen::update(Context& ctx, const platform::Pad& pad, float) {
    const int count = meta::location(loc_).missionCount;
    if (pad.hit(PSP_CTRL_UP) && cursor_ > 0) {
        --cursor_;
        preview_ = 0.0f;
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_DOWN) && cursor_ < count - 1) {
        ++cursor_;
        preview_ = 0.0f;
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_CROSS) || pad.hit(PSP_CTRL_START)) {
        if (meta::missionUnlocked(ctx.profile, loc_, cursor_)) {
            ctx.mission = cursor_;
            ctx.sfx(Sfx::Select);
            ctx.push(ScreenId::Briefing);
        } else {
            ctx.sfx(Sfx::Deny);
        }
    } else if (pad.hit(PSP_CTRL_CIRCLE)) {
        ctx.sfx(Sfx::Back);
        ctx.pop();
    }
}

void LocationScreen::draw(Context& ctx) {
    const float dt = ctx.dt;
    const meta::Location& loc = meta::location(loc_);
    const int count = loc.missionCount;
    for (int m = 0; m < meta::kMaxMissions; ++m)
        focus_[m] = ui::approach(focus_[m], m == cursor_ ? 1.0f : 0.0f, 16.0f, dt);
    preview_ = ui::approach(preview_, 1.0f, 16.0f, dt);
    const float want = static_cast<float>(
        mui::clampi(kListTop + cursor_ * kPitch + kRowH - kViewBottom, 0, 100000));
    scroll_ = ui::approach(scroll_, want, 12.0f, dt);
    const int sc = static_cast<int>(scroll_ + 0.5f);

    gfx::gradientRect(0, 0, gfx::kScreenW, 56, gfx::abgr(4, 6, 5, 150), gfx::abgr(4, 6, 5, 0));
    ui::screenTitle(loc.name, loc.subtitle);

    // Location thumbnail in the corner, in a small frame.
    const int tx = gfx::kScreenW - ui::kMargin - kThumbW, ty = 13;
    ui::panel(tx - 4, ty - 4, kThumbW + 8, kThumbH + 8, ui::Plate::Panel);
    ui::locationImage(loc_, tx, ty, kThumbW, kThumbH);

    gfx::clip(0, kViewTop, gfx::kScreenW, ui::kBarY);
    for (int m = 0; m < count; ++m) drawRow(ctx, m, kListTop + m * kPitch - sc);
    gfx::unclip();

    // Progress under the list, when the list is short enough for it to show.
    const int below = kListTop + count * kPitch + 2 - sc;
    if (below + 20 < kViewBottom) {
        char s[24];
        std::snprintf(s, sizeof s, "CLEARED %d/%d", meta::clearedCount(ctx.profile, loc_), count);
        const int w = ui::textWidth(s, ui::Face::Body);
        ui::text(kListX + 2, below + 4, s, ui::Face::Body, ui::kTextDim);
        ui::progressBar(kListX + w + 14, below + 6, kListW - w - 14,
                        static_cast<float>(meta::clearedCount(ctx.profile, loc_)) / static_cast<float>(count));
    }

    drawPreview(ctx);

    const ui::Hint hints[] = {ui::kHintSelect, ui::kHintBack};
    ui::bottomBar(hints, 2);
}

void LocationScreen::drawRow(Context& ctx, int m, int y) {
    if (y > ui::kBarY || y + kRowH < kViewTop) return;
    const meta::Mission& ms = meta::location(loc_).missions[m];
    const bool open = meta::missionUnlocked(ctx.profile, loc_, m);
    char label[48];
    std::snprintf(label, sizeof label, "%s  %s", ms.code, ms.name);
    ui::ButtonLook look;
    look.focus = focus_[m];
    look.disabled = !open;
    ui::button(kListX, y, kListW, ui::ButtonKind::Row, label, look, nullptr, open ? nullptr : &ui::icons::kLock);
    if (!open) {
        // A locked row can still hold the cursor (to read what it needs): a light
        // wash and the amber marker, without the lit plate's engraved text.
        const float f = focus_[m];
        if (f > 0.01f) {
            ui::strip(ui::skin::kBtn28Focus, kListX, y, kListW, mui::withAlpha(ui::kWhite, 0.22f * f));
            gfx::fillRect(kListX + 3, y + 4, 3, kRowH - 8, mui::withAlpha(ui::kAccent, f));
        }
        return;
    }

    // The plate slides out with the cursor: the icons ride with it. On the
    // lit metal the white line icons turn dark.
    const float t = focus_[m];
    const int bx = kListX + static_cast<int>(6.0f * t + 0.5f);
    const uint32_t ink = mui::mix(ui::kText, ui::kEngraved, t);
    int right = bx + kListW - 10 - mui::medalsWidth();
    mui::drawMedals(right, y + 6, ctx.profile.medals[loc_][m], ui::kWhite);
    right -= 8;
    for (int v = ms.variantCount - 1; v >= 0; --v) {
        const ui::AtlasRect& r = ms.variants[v].type == meta::VehicleType::Plane ? ui::icons::kPlane : ui::icons::kHeli;
        right -= r.w;
        ui::icon(r, right, y + (kRowH - r.h) / 2, ink);
        right -= 4;
    }
}

void LocationScreen::drawPreview(Context& ctx) {
    const meta::Mission& ms = meta::location(loc_).missions[cursor_];
    const bool open = meta::missionUnlocked(ctx.profile, loc_, cursor_);
    const float a = preview_ * (open ? 1.0f : 0.5f);
    const int px = kPanelX, py = kPanelY;
    ui::panel(px, py, kPanelW, kPanelH);

    const int x0 = px + 12;
    int y = py + 8;
    const uint32_t white = mui::withAlpha(ui::kWhite, a);
    const uint32_t text = mui::withAlpha(ui::kTextBright, a);

    // Weather and time of day: icon + name.
    ui::icon(mui::weatherIcon(ms.weather), x0, y, white);
    ui::textInBox(x0 + 30, y, 24, meta::weatherName(ms.weather), ui::Face::Label, text);
    y += 25;
    ui::icon(mui::timeIcon(ms.time), x0, y, white);
    ui::textInBox(x0 + 30, y, 24, meta::timeName(ms.time), ui::Face::Label, text);
    y += 25;

    // Enemies per variant: the vehicle that flies it, then what is in the way.
    for (int v = 0; v < ms.variantCount; ++v) {
        const meta::MissionVariant& var = ms.variants[v];
        int x = x0;
        const ui::AtlasRect& vr = var.type == meta::VehicleType::Plane ? ui::icons::kPlane : ui::icons::kHeli;
        ui::icon(vr, x, y + 3, white);
        x += 26;
        auto count = [&](const ui::AtlasRect& icon, int n) {
            if (n <= 0) return;
            char s[8];
            std::snprintf(s, sizeof s, "\xC3\x97%d", n);  // "×N" (UTF-8)
            ui::icon(icon, x, y + 1, white);
            x += 27;
            x += ui::textInBox(x, y, 24, s, ui::Face::Label, text) + 9;
        };
        count(ui::icons::kEnemyAir, var.airEnemies);
        count(ui::icons::kEnemyGround, var.groundTargets);
        y += 25;
    }

    // Reward, or what to clear first, pinned to the bottom of the panel.
    const int by = py + kPanelH - 38;
    gfx::fillRect(px + 10, by - 4, kPanelW - 20, 1, mui::withAlpha(gfx::abgr(0, 0, 0), 0.6f));
    if (open) {
        char credits[24];
        ui::formatCredits(ms.reward, credits, sizeof credits);
        ui::text(x0, by + 1, "REWARD (NORMAL)", ui::Face::Small, mui::withAlpha(ui::kTextDim, a));
        ui::text(x0, by + 14, credits, ui::Face::Label, mui::withAlpha(ui::kCredits, a));
    } else {
        char s[48];
        if (cursor_ > 0) std::snprintf(s, sizeof s, "CLEAR %s TO UNLOCK", meta::location(loc_).missions[cursor_ - 1].code);
        else std::snprintf(s, sizeof s, "LOCATION LOCKED");
        ui::text(x0, by + 5, s, mui::fitFace(s, kPanelW - 24, ui::Face::Body), mui::withAlpha(ui::kAccent, preview_));
    }
}

}  // namespace menu
