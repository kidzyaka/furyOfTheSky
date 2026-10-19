#include "scenes/menu/campaign_screen.h"

#include <pspctrl.h>

#include <cmath>
#include <cstdio>

#include "render/draw2d.h"
#include "render/gu_context.h"
#include "scenes/menu/mission_ui.h"
#include "ui/anim.h"
#include "ui/ui.h"

namespace menu {

namespace {

constexpr int kImgW = 256, kImgH = 128;  // location art, drawn 1:1 on the centre card
constexpr int kFrame = 5;                // metal frame around the art
constexpr int kCenterY = 102;            // vertical centre of the art
constexpr float kNeighbourScale = 0.62f;
// Centre-to-centre distance of a neighbour at rest. Less than the sum of the
// half widths: the neighbour tucks behind the centre card and still shows 100 px.
constexpr float kStep = 200.0f;
constexpr float kFarStep = 100.0f;       // cards beyond the first neighbour bunch up

constexpr int kPlateW = 340, kPlateH = 46;
constexpr int kPlateX = (gfx::kScreenW - kPlateW) / 2;
constexpr int kPlateY = kCenterY + kImgH / 2 + kFrame - 4;  // overlaps the frame: a tag hanging off it

const ui::Hint kHintLocation{&ui::icons::kDpadLR, "LOCATION"};

}  // namespace

void CampaignScreen::enter(Context& ctx) {
    int want = ctx.profile.lastLocation;
    if (want >= meta::locationCount() || !meta::locationUnlocked(ctx.profile, want)) want = 0;
    index_ = want;
    pos_ = static_cast<float>(index_);
    shake_ = 0.0f;
}

void CampaignScreen::update(Context& ctx, const platform::Pad& pad, float) {
    const int n = meta::locationCount();
    int dir = 0;
    if (pad.hit(PSP_CTRL_LEFT) || pad.hit(PSP_CTRL_LTRIGGER)) dir = -1;
    if (pad.hit(PSP_CTRL_RIGHT) || pad.hit(PSP_CTRL_RTRIGGER)) dir = +1;
    if (dir != 0) {
        const int next = index_ + dir;
        kick_[dir < 0 ? 0 : 1] = 1.0f;  // the arrow answers even at the end of the strip
        if (next >= 0 && next < n) {
            index_ = next;
            ctx.sfx(Sfx::Slide);
            shake_ = 0.0f;
        }
    }
    if (pad.hit(PSP_CTRL_CROSS)) {
        if (meta::locationUnlocked(ctx.profile, index_)) {
            ctx.location = index_;
            ctx.profile.lastLocation = static_cast<uint8_t>(index_);
            ctx.sfx(Sfx::Select);
            ctx.push(ScreenId::Location);
        } else {
            ctx.sfx(Sfx::Deny);
            shake_ = 1.0f;
        }
    } else if (pad.hit(PSP_CTRL_CIRCLE)) {
        ctx.sfx(Sfx::Back);
        ctx.pop();
    }
}

void CampaignScreen::draw(Context& ctx) {
    const float dt = ctx.dt;
    pos_ = ui::approach(pos_, static_cast<float>(index_), 12.0f, dt);
    if (std::fabs(pos_ - static_cast<float>(index_)) < 0.002f) pos_ = static_cast<float>(index_);
    for (float& k : kick_) k = ui::approach(k, 0.0f, 14.0f, dt);
    shake_ = ui::approach(shake_, 0.0f, 7.0f, dt);

    // A soft dark band behind the top edge: the title stays legible on any hangar light.
    gfx::gradientRect(0, 0, gfx::kScreenW, 48, gfx::abgr(4, 6, 5, 150), gfx::abgr(4, 6, 5, 0));
    ui::screenTitle("CAMPAIGN");
    drawPager(ctx);

    // Far cards first, the centre one last (it covers the neighbours' inner edges).
    const int n = meta::locationCount();
    int order[meta::kMaxLocations];
    for (int i = 0; i < n; ++i) order[i] = i;
    for (int a = 1; a < n; ++a) {  // insertion sort by |offset|, descending
        const int v = order[a];
        int b = a - 1;
        while (b >= 0 && std::fabs(order[b] - pos_) < std::fabs(v - pos_)) {
            order[b + 1] = order[b];
            --b;
        }
        order[b + 1] = v;
    }
    for (int k = 0; k < n; ++k) drawCard(ctx, order[k], static_cast<float>(order[k]) - pos_);

    drawArrows();
    drawPlate(ctx);

    int done = 0, total = 0;
    for (int l = 0; l < n; ++l) {
        done += meta::clearedCount(ctx.profile, l);
        total += meta::location(l).missionCount;
    }
    char left[40];
    std::snprintf(left, sizeof left, "%d/%d MISSIONS CLEARED", done, total);
    const ui::Hint hints[] = {kHintLocation, ui::kHintSelect, ui::kHintBack};
    ui::bottomBar(hints, 3, left);
}

void CampaignScreen::drawCard(Context& ctx, int i, float d) {
    const bool open = meta::locationUnlocked(ctx.profile, i);
    const float ad = std::fabs(d);
    const float nearK = ui::clamp01(ad);   // 0 = centre card, 1 = a neighbour
    const float farK = ad > 1.0f ? ad - 1.0f : 0.0f;
    const float s = 1.0f - (1.0f - kNeighbourScale) * nearK;
    const float cx = gfx::kScreenW * 0.5f + (d < 0.0f ? -1.0f : 1.0f) * (kStep * nearK + kFarStep * farK);
    const int w = static_cast<int>(kImgW * s + 0.5f), h = static_cast<int>(kImgH * s + 0.5f);
    const int x = static_cast<int>(cx - w * 0.5f + 0.5f), y = kCenterY - h / 2;
    if (x >= gfx::kScreenW || x + w <= 0) return;

    // Neighbours sink into the dark; a locked card is darker still.
    const float dark = 0.42f * nearK + (open ? 0.0f : 0.45f * (1.0f - 0.3f * nearK));
    const uint32_t frameTint = mui::grey(1.0f - 0.55f * nearK);
    ui::panel(x - kFrame, y - kFrame, w + 2 * kFrame, h + 2 * kFrame, ui::Plate::Panel, frameTint);
    // The card in focus has the lit metal frame, fading in as it arrives.
    if (nearK < 0.98f)
        ui::panel(x - kFrame, y - kFrame, w + 2 * kFrame, h + 2 * kFrame, ui::Plate::Light,
                  mui::withAlpha(ui::kWhite, 1.0f - nearK));
    ui::locationImage(i, x, y, w, h);
    if (dark > 0.01f) gfx::fillRect(x, y, w, h, gfx::abgr(3, 5, 4, static_cast<unsigned>(dark * 255.0f)));

    if (!open) {
        int lx = static_cast<int>(cx);
        if (nearK < 0.2f) lx += static_cast<int>(std::sin(shake_ * 30.0f) * shake_ * 5.0f);
        const int sz = static_cast<int>(48.0f * s + 0.5f);
        ui::iconScaled(ui::icons::kAvatarLocked, lx - sz / 2, y + (h - sz) / 2, sz, sz, mui::grey(1.0f - 0.35f * nearK));
    }
}

void CampaignScreen::drawPlate(Context& ctx) {
    ui::panel(kPlateX, kPlateY, kPlateW, kPlateH, ui::Plate::Light);
    // The two cards nearest the middle cross-fade their texts while the strip slides.
    const int lo = static_cast<int>(std::floor(pos_));
    for (int i = lo; i <= lo + 1; ++i) {
        if (i < 0 || i >= meta::locationCount()) continue;
        const float a = ui::clamp01(1.0f - 2.0f * std::fabs(static_cast<float>(i) - pos_));
        if (a > 0.01f) drawPlateText(ctx, i, a, kPlateX, kPlateY);
    }
}

void CampaignScreen::drawPlateText(Context& ctx, int i, float a, int px, int py) {
    const meta::Location& loc = meta::location(i);
    const bool open = meta::locationUnlocked(ctx.profile, i);
    const int left = px + 16, right = px + kPlateW - 16;
    const uint32_t ink = mui::withAlpha(ui::kEngraved, a * (open ? 1.0f : 0.85f));
    constexpr gfx::TextStyle kCut = gfx::TextStyle::Engraved;

    ui::text(left, py + 7, loc.name, ui::Face::Title, ink, gfx::kAlignLeft, kCut);

    if (!open) {
        const int lw = ui::icons::kLock.w;
        ui::icon(ui::icons::kLock, right - lw, py + 9, mui::withAlpha(ui::kEngraved, a));
        char hint[64];
        std::snprintf(hint, sizeof hint, "CLEAR %s TO UNLOCK", meta::location(i - 1).name);
        const ui::Face f = mui::fitFace(hint, kPlateW - 32, ui::Face::Body);
        ui::text(left, py + 29, hint, f, ink, gfx::kAlignLeft, kCut);
        return;
    }

    // Right of the name: how many missions are cleared.
    char prog[16];
    std::snprintf(prog, sizeof prog, "%d/%d", meta::clearedCount(ctx.profile, i), loc.missionCount);
    const int labelW = ui::textWidth("MISSIONS", ui::Face::Small);
    ui::text(right, py + 7 + ui::capHeight(ui::Face::Title) - ui::capHeight(ui::Face::Small), "MISSIONS",
             ui::Face::Small, ink, gfx::kAlignRight, kCut);
    ui::text(right - labelW - 5, py + 7, prog, ui::Face::Title, ink, gfx::kAlignRight, kCut);

    // Second row: medals earned per difficulty on the right, the subtitle on the left.
    int cur = right;
    for (int d = meta::kDifficultyCount - 1; d >= 0; --d) {
        const int count = mui::medalCount(ctx.profile, i, d);
        char num[16];
        std::snprintf(num, sizeof num, "%d", count);
        const uint32_t c = mui::withAlpha(ui::kEngraved, a * (count > 0 ? 1.0f : 0.55f));
        const int tw = ui::textWidth(num, ui::Face::Body);
        ui::textInBox(cur - tw, py + 26, 16, num, ui::Face::Body, c, gfx::kAlignLeft, kCut);
        cur -= tw + 3 + 16;
        ui::icon(mui::medalIcon(d, count > 0), cur, py + 26, mui::withAlpha(ui::kWhite, a));
        cur -= 10;
    }
    const ui::Face f = mui::fitFace(loc.subtitle, cur - left, ui::Face::Body);
    ui::text(left, py + 29, loc.subtitle, f, ink, gfx::kAlignLeft, kCut);
}

void CampaignScreen::drawArrows() {
    const int ay = kCenterY - ui::icons::kArrowLeft.h / 2;
    const bool canLeft = index_ > 0, canRight = index_ < meta::locationCount() - 1;
    const int lx = 5 - static_cast<int>(kick_[0] * 4.0f + 0.5f);
    const int rx = gfx::kScreenW - 5 - ui::icons::kArrowRight.w + static_cast<int>(kick_[1] * 4.0f + 0.5f);
    ui::icon(ui::icons::kArrowLeft, lx, ay, mui::withAlpha(ui::kWhite, canLeft ? 1.0f : 0.22f));
    ui::icon(ui::icons::kArrowRight, rx, ay, mui::withAlpha(ui::kWhite, canRight ? 1.0f : 0.22f));
}

void CampaignScreen::drawPager(Context& ctx) {
    const int n = meta::locationCount();
    char label[40];
    std::snprintf(label, sizeof label, "LOCATION %d / %d", index_ + 1, n);
    const int right = gfx::kScreenW - ui::kMargin;
    ui::text(right, 14, label, ui::Face::Body, ui::kTextDim, gfx::kAlignRight);
    // Dots: the one under the strip's position grows and turns amber.
    for (int i = 0; i < n; ++i) {
        const float glow = ui::clamp01(1.0f - std::fabs(static_cast<float>(i) - pos_));
        const int size = 6 + static_cast<int>(2.0f * glow + 0.5f);
        const int cx = right - 4 - (n - 1 - i) * 14;
        const uint32_t base = meta::locationUnlocked(ctx.profile, i) ? ui::kTextDim : ui::kTextDisabled;
        gfx::fillRect(cx - size / 2, 36 - size / 2, size, size, mui::mix(base, ui::kAccent, glow));
    }
}

}  // namespace menu
