#include "scenes/menu/emblem_screen.h"

#include <pspctrl.h>

#include <cstdio>

#include "render/draw2d.h"
#include "render/gu_context.h"
#include "scenes/menu/mission_ui.h"
#include "ui/anim.h"
#include "ui/ui.h"

namespace menu {

namespace {

// Slots at 64 px pitch hold the 56 px emblem at its native size (no filtering).
constexpr int kPitch = 64, kSlot = 62, kEmblem = ui::emblems::kSize;
constexpr int kGridX = ui::kMargin, kGridTop = 54;
constexpr int kViewTop = 50, kViewBottom = ui::kBarY - 34;  // slots keep clear of the bar's shadow
constexpr int kGridW = EmblemScreen::kCols * kPitch - (kPitch - kSlot);
constexpr int kPageRows = 2;  // L / R jump this many rows

// Side panel with the focused emblem.
constexpr int kPanelX = kGridX + kGridW + 8, kPanelY = 8;
constexpr int kPanelW = gfx::kScreenW - ui::kMargin - kPanelX, kPanelH = ui::kBarY - 6 - kPanelY;
constexpr int kBigFrame = 92, kBig = 84;  // the emblem at x1.5 inside its frame

const ui::Hint kHintEquip{&ui::icons::kBtnCross, "EQUIP"};
const ui::Hint kHintPageL{&ui::icons::kBtnL, ""};
const ui::Hint kHintPageR{&ui::icons::kBtnR, "PAGE"};

// A locked emblem is the picture pressed into the dark: a flat grey multiplier.
uint32_t lockedTint(float a) { return mui::withAlpha(mui::grey(60.0f / 255.0f), a); }

void drawSlot(int index, int x, int y, float focus, bool unlocked, bool equipped, float flash) {
    ui::panel(x, y, kSlot, kSlot, ui::Plate::Inset);
    if (focus > 0.01f) ui::panel(x, y, kSlot, kSlot, ui::Plate::Light, mui::withAlpha(ui::kWhite, focus));
    const int ex = x + (kSlot - kEmblem) / 2, ey = y + (kSlot - kEmblem) / 2;
    if (unlocked) {
        ui::emblem(index, ex, ey);
    } else {
        ui::emblem(index, ex, ey, kEmblem, lockedTint(1.0f));
        ui::icon(ui::icons::kLock, x + (kSlot - ui::icons::kLock.w) / 2, y + (kSlot - ui::icons::kLock.h) / 2);
    }
    if (equipped) {
        // The chosen one: an amber frame and a check in the corner.
        mui::frame(x, y, kSlot, kSlot, 2, ui::kAccent);
        gfx::fillRect(x + kSlot - 21, y + 3, 18, 18, gfx::abgr(8, 10, 8, 200));
        ui::icon(ui::icons::kCheck, x + kSlot - 20, y + 4);
    }
    if (focus > 0.01f) mui::brackets(x + 1, y + 1, kSlot - 2, kSlot - 2, 9, 2, mui::withAlpha(ui::kAccent, focus));
    if (flash > 0.01f) gfx::fillRect(x, y, kSlot, kSlot, gfx::abgr(255, 236, 190, static_cast<unsigned>(flash * 150.0f)));
}

}  // namespace

void EmblemScreen::enter(Context& ctx) {
    const meta::Profile& p = ctx.profile;
    count_ = meta::emblemCount() < ui::emblems::kCount ? meta::emblemCount() : ui::emblems::kCount;
    unlockedCount_ = 0;
    for (int i = 0; i < count_; ++i) {
        unlocked_[i] = meta::emblemUnlocked(p, i);
        progress_[i] = meta::unlockProgress(p, i);
        unlockedCount_ += unlocked_[i];
    }
    cursor_ = mui::clampi(p.avatar, 0, count_ - 1);
    for (int i = 0; i < count_; ++i) focus_[i] = i == cursor_ ? 1.0f : 0.0f;
    scroll_ = target_ = 0.0f;
    snap_ = true;
    detail_ = 1.0f;
    flash_ = 0.0f;
}

void EmblemScreen::update(Context& ctx, const platform::Pad& pad, float) {
    auto go = [&](int to, Sfx s) {
        to = mui::clampi(to, 0, count_ - 1);
        if (to == cursor_) return;
        cursor_ = to;
        detail_ = 0.0f;
        ctx.sfx(s);
    };
    const int lastRow = (count_ - 1) / kCols;
    if (pad.hit(PSP_CTRL_LEFT)) go(cursor_ - 1, Sfx::Move);
    if (pad.hit(PSP_CTRL_RIGHT)) go(cursor_ + 1, Sfx::Move);
    if (pad.hit(PSP_CTRL_UP) && cursor_ >= kCols) go(cursor_ - kCols, Sfx::Move);
    if (pad.hit(PSP_CTRL_DOWN)) {
        // From a column that the last, shorter row lacks: land on its last slot.
        if (cursor_ + kCols < count_) go(cursor_ + kCols, Sfx::Move);
        else if (cursor_ / kCols < lastRow) go(count_ - 1, Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_LTRIGGER)) go(cursor_ - kCols * kPageRows, Sfx::Tab);
    if (pad.hit(PSP_CTRL_RTRIGGER)) go(cursor_ + kCols * kPageRows, Sfx::Tab);

    if (pad.hit(PSP_CTRL_CROSS)) {
        if (!unlocked_[cursor_]) {
            ctx.sfx(Sfx::Deny);
        } else if (ctx.profile.avatar == cursor_) {
            ctx.sfx(Sfx::Tab);  // already the one
        } else {
            ctx.profile.avatar = static_cast<uint8_t>(cursor_);
            ctx.save();
            ctx.sfx(Sfx::Select);
            flash_ = 1.0f;
            flashIndex_ = cursor_;
        }
    }
    if (pad.hit(PSP_CTRL_CIRCLE)) {
        ctx.sfx(Sfx::Back);
        ctx.pop();
    }
}

void EmblemScreen::draw(Context& ctx) {
    const float dt = ctx.dt;
    const meta::Profile& p = ctx.profile;
    for (int i = 0; i < count_; ++i) focus_[i] = ui::approach(focus_[i], i == cursor_ ? 1.0f : 0.0f, 16.0f, dt);
    detail_ = ui::approach(detail_, 1.0f, 16.0f, dt);
    flash_ = ui::approach(flash_, 0.0f, 7.0f, dt);

    // The cursor's row stays in view; the grid glides there.
    const int rows = (count_ + kCols - 1) / kCols;
    const float content = static_cast<float>(kGridTop + rows * kPitch - (kPitch - kSlot));
    const float maxScroll = content > kViewBottom ? content - kViewBottom : 0.0f;
    const float rowTop = static_cast<float>(kGridTop + (cursor_ / kCols) * kPitch);
    target_ = mui::scrollToShow(target_, rowTop, rowTop + kSlot, kGridTop, kViewBottom, maxScroll);
    if (snap_) scroll_ = target_, snap_ = false;
    scroll_ = ui::approach(scroll_, target_, 12.0f, dt);
    const int sc = static_cast<int>(scroll_ + 0.5f);

    gfx::gradientRect(0, 0, kPanelX - 2, kViewTop + 6, gfx::abgr(4, 6, 5, 150), gfx::abgr(4, 6, 5, 0));
    char sub[32];
    std::snprintf(sub, sizeof sub, "%d / %d UNLOCKED", unlockedCount_, count_);
    ui::screenTitle("EMBLEMS", sub);

    // --- The grid. Slots pass under the title row and into the bar's shadow.
    gfx::clip(0, kViewTop, kGridX + kGridW + 2, ui::kBarY);
    for (int i = 0; i < count_; ++i) {
        const int x = kGridX + (i % kCols) * kPitch, y = kGridTop + (i / kCols) * kPitch - sc;
        if (y + kSlot < kViewTop || y > ui::kBarY) continue;
        drawSlot(i, x, y, focus_[i], unlocked_[i], p.avatar == i, i == flashIndex_ ? flash_ : 0.0f);
    }
    gfx::gradientRect(0, kViewTop, kGridX + kGridW + 2, 10, gfx::abgr(4, 6, 5, 140), gfx::abgr(4, 6, 5, 0));
    gfx::unclip();

    // The bar first: the side panel is static and sits above its shadow.
    ui::Hint hints[4];
    int hc = 0;
    if (unlocked_[cursor_] && p.avatar != cursor_) hints[hc++] = kHintEquip;
    hints[hc++] = kHintPageL;
    hints[hc++] = kHintPageR;
    hints[hc++] = ui::kHintBack;
    ui::bottomBar(hints, hc);
    mui::scrollbar(kGridX + kGridW + 1, kGridTop, kViewBottom - kGridTop, static_cast<float>(kViewBottom - kGridTop),
                   content - kGridTop, scroll_);

    // --- The focused emblem: large, its name, state, and how it is earned.
    const int i = cursor_;
    const meta::EmblemInfo& e = meta::emblem(i);
    const bool unlocked = unlocked_[i], equipped = p.avatar == i;
    const float a = detail_;
    ui::panel(kPanelX, kPanelY, kPanelW, kPanelH);
    const int x0 = kPanelX + 12, cx = kPanelX + kPanelW / 2, inner = kPanelW - 24;

    const int fx = cx - kBigFrame / 2, fy = kPanelY + 10;
    ui::panel(fx, fy, kBigFrame, kBigFrame, ui::Plate::Inset);
    ui::emblem(i, fx + (kBigFrame - kBig) / 2, fy + (kBigFrame - kBig) / 2, kBig,
               unlocked ? mui::withAlpha(ui::kWhite, a) : lockedTint(a));
    if (!unlocked) {
        constexpr int lw = ui::icons::kLock.w * 2, lh = ui::icons::kLock.h * 2;
        ui::iconScaled(ui::icons::kLock, cx - lw / 2, fy + (kBigFrame - lh) / 2, lw, lh, mui::withAlpha(ui::kWhite, a));
    }

    int y = fy + kBigFrame + 8;
    ui::text(x0, y, e.name, mui::fitFace(e.name, inner, ui::Face::Label), mui::withAlpha(ui::kTextBright, a));
    y += 22;

    // State: EQUIPPED / AVAILABLE / LOCKED (with how far along it is).
    if (equipped) {
        ui::icon(ui::icons::kCheck, x0 - 2, y - 3, mui::withAlpha(ui::kWhite, a));
        ui::text(x0 + 17, y, "EQUIPPED", ui::Face::Body, mui::withAlpha(ui::kAccent, a));
    } else if (unlocked) {
        ui::text(x0, y, "AVAILABLE", ui::Face::Body, mui::withAlpha(ui::kGood, a));
    } else {
        ui::icon(ui::icons::kLock, x0 - 1, y - 3, mui::withAlpha(ui::kWhite, a));
        ui::text(x0 + 17, y, "LOCKED", ui::Face::Body, mui::withAlpha(ui::kTextDim, a));
        char pct[8];
        std::snprintf(pct, sizeof pct, "%d%%", static_cast<int>(progress_[i] * 100.0f));
        ui::text(x0 + inner, y, pct, ui::Face::Body, mui::withAlpha(ui::kAccent, a), gfx::kAlignRight);
    }
    y += 22;

    char cond[64];
    meta::describeUnlock(e, cond, sizeof cond);
    const int h = ui::paragraph(x0, y, inner, cond, ui::Face::Body,
                                mui::withAlpha(unlocked ? ui::kTextDim : ui::kText, a), 0);
    if (!unlocked) ui::progressBar(x0, y + h + 6, inner, progress_[i] * a);
}

}  // namespace menu
