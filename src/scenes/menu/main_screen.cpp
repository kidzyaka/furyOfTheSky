#include "scenes/menu/main_screen.h"

#include <pspctrl.h>

#include <cstdio>

#include "render/draw2d.h"
#include "render/gu_context.h"
#include "ui/anim.h"
#include "ui/ui.h"

namespace menu {

namespace {

struct Item {
    const char* label;
    int parent;       // -1: a main button
    ScreenId target;  // ignored for a group parent and disabled rows
    bool disabled;
    const char* tag;
};

constexpr Item kItems[MainScreen::kItemCount] = {
    {"SINGLEPLAYER", -1, ScreenId::Main, false, nullptr},
    {"CAMPAIGN", 0, ScreenId::Campaign, false, nullptr},
    {"CUSTOM MISSIONS", 0, ScreenId::Main, true, "SOON"},
    {"TRAINING", 0, ScreenId::Training, false, nullptr},
    {"MULTIPLAYER", -1, ScreenId::Multiplayer, false, nullptr},
    {"HANGAR", -1, ScreenId::Hangar, false, nullptr},
    {"SETTINGS", -1, ScreenId::Settings, false, nullptr},
    {"COMBAT PROFILE", -1, ScreenId::Profile, false, nullptr},
};

constexpr int kX = ui::kMargin;
constexpr int kW = 204;
constexpr int kMainH = 32, kSubH = 28, kGap = 4;
constexpr float kListTop = 48.0f;  // content y of the first button (the title sits above)

bool isGroup(int i) {
    for (const Item& it : kItems)
        if (it.parent == i) return true;
    return false;
}

int childCount(int g) {
    int n = 0;
    for (const Item& it : kItems) n += it.parent == g;
    return n;
}

}  // namespace

void MainScreen::enter(Context&) {}

bool MainScreen::visible(int i) const {
    const int p = kItems[i].parent;
    return p < 0 || p == open_;
}

void MainScreen::layout(float y[kItemCount], float& contentBottom) const {
    // The drawer: rows of the sliding group sit under their parent and come
    // out from beneath it; everything below moves down by the open height.
    const int g = open_ >= 0 ? open_ : closing_;
    float at = kListTop;
    for (int i = 0; i < kItemCount; ++i) {
        if (kItems[i].parent >= 0) continue;
        y[i] = at;
        at += kMainH + kGap;
        if (i == g) {
            const float height = childCount(g) * static_cast<float>(kSubH + kGap);
            const float hidden = (1.0f - expand_) * height;
            float sub = at - hidden;
            for (int j = 0; j < kItemCount; ++j)
                if (kItems[j].parent == g) {
                    y[j] = sub;
                    sub += kSubH + kGap;
                }
            at += height - hidden;
        }
    }
    for (int j = 0; j < kItemCount; ++j)
        if (kItems[j].parent >= 0 && kItems[j].parent != g) y[j] = -1000.0f;
    contentBottom = at;
}

void MainScreen::move(Context& ctx, int dir) {
    // While a drawer is open the cursor stays inside it (the rest is dimmed).
    int i = cursor_;
    for (;;) {
        i += dir;
        if (i < 0 || i >= kItemCount) return;
        if (!visible(i)) continue;
        if (open_ >= 0 && i != open_ && kItems[i].parent != open_) return;
        break;
    }
    cursor_ = i;
    ctx.sfx(Sfx::Move);
}

void MainScreen::activate(Context& ctx) {
    const Item& it = kItems[cursor_];
    if (isGroup(cursor_)) {
        if (open_ == cursor_) {
            closing_ = open_;
            open_ = -1;
            ctx.sfx(Sfx::Back);
        } else {
            open_ = cursor_;
            closing_ = -1;
            for (int j = 0; j < kItemCount; ++j)
                if (kItems[j].parent == open_) {
                    cursor_ = j;
                    break;
                }
            ctx.sfx(Sfx::Select);
        }
        return;
    }
    if (it.disabled) {
        ctx.sfx(Sfx::Deny);
        return;
    }
    ctx.sfx(Sfx::Select);
    ctx.push(it.target);
}

void MainScreen::update(Context& ctx, const platform::Pad& pad, float) {
    if (pad.hit(PSP_CTRL_UP)) move(ctx, -1);
    if (pad.hit(PSP_CTRL_DOWN)) move(ctx, +1);
    if (pad.hit(PSP_CTRL_CROSS)) activate(ctx);
    else if (pad.hit(PSP_CTRL_RIGHT) && isGroup(cursor_) && open_ != cursor_) activate(ctx);
    else if ((pad.hit(PSP_CTRL_CIRCLE) || pad.hit(PSP_CTRL_LEFT)) && open_ >= 0) {
        cursor_ = open_;
        closing_ = open_;
        open_ = -1;
        ctx.sfx(Sfx::Back);
    }
}

void MainScreen::draw(Context& ctx) {
    const float dt = ctx.dt;
    expand_ = ui::approach(expand_, open_ >= 0 ? 1.0f : 0.0f, 14.0f, dt);
    if (open_ < 0 && expand_ < 0.01f) closing_ = -1;
    dim_ = ui::approach(dim_, open_ >= 0 ? 1.0f : 0.0f, 12.0f, dt);
    for (int i = 0; i < kItemCount; ++i) focus_[i] = ui::approach(focus_[i], i == cursor_ ? 1.0f : 0.0f, 16.0f, dt);

    float y[kItemCount], bottom;
    layout(y, bottom);
    // Keep the cursor's row clear of the bar's shadow; scroll smoothly.
    const float viewBottom = ui::kBarY - 34.0f;
    float want = 0.0f;
    const float cb = y[cursor_] + (kItems[cursor_].parent < 0 ? kMainH : kSubH);
    if (cb - want > viewBottom) want = cb - viewBottom;
    scroll_ = ui::approach(scroll_, want, 12.0f, dt);
    const int sc = static_cast<int>(scroll_ + 0.5f);

    // Darken the left side under the column: legible over any hangar light.
    gfx::hGradientRect(0, 0, 250, ui::kBarY, gfx::abgr(4, 6, 5, 150), gfx::abgr(4, 6, 5, 0));
    ui::text(ui::kMargin + 2, 14 - sc, "FURY OF THE SKY", ui::Face::Title, ui::kTextBright);

    // The sliding group first (its parent and the buttons below cover it).
    const int g = open_ >= 0 ? open_ : closing_;
    if (g >= 0 && expand_ > 0.01f) {
        const int clipTop = static_cast<int>(y[g]) + kMainH - sc;
        gfx::clip(0, clipTop, gfx::kScreenW, ui::kBarY);
        int first = -1, last = -1;
        for (int j = 0; j < kItemCount; ++j) {
            if (kItems[j].parent != g) continue;
            if (first < 0) first = j;
            last = j;
            ui::ButtonLook look;
            look.focus = focus_[j];
            look.disabled = kItems[j].disabled;
            ui::button(kX, static_cast<int>(y[j]) - sc, kW, ui::ButtonKind::Sub, kItems[j].label, look, kItems[j].tag,
                       kItems[j].disabled ? &ui::icons::kLock : nullptr);
        }
        // The drawer sits under the main buttons: a soft shadow on its first
        // row from the button above, a deeper one on its last row from the
        // button below.
        if (first >= 0) {
            ui::innerShadow(kX + 1, static_cast<int>(y[first]) - sc, kW + 6, 6, false, 0.38f * expand_);
            ui::innerShadow(kX + 1, static_cast<int>(y[last]) + kSubH - 9 - sc, kW + 6, 9, true, 0.66f * expand_);
        }
        gfx::unclip();
    }
    for (int i = 0; i < kItemCount; ++i) {
        if (kItems[i].parent >= 0) continue;
        ui::ButtonLook look;
        look.focus = focus_[i];
        look.open = i == open_;
        look.dim = (open_ >= 0 && i != open_) || (closing_ >= 0 && i != closing_) ? dim_ : 0.0f;
        const int by = static_cast<int>(y[i]) - sc;
        ui::button(kX, by, kW, ui::ButtonKind::Main, kItems[i].label, look);
        if (isGroup(i)) {
            // Open/closed mark at the right end, as big as the label.
            const int bx = kX + static_cast<int>(6.0f * look.focus + 0.5f);
            const bool lit = look.focus > 0.5f;
            ui::textInBox(bx + kW - 14, by, kMainH, i == open_ ? "\xe2\x80\x93" : "+", ui::Face::Button,
                          lit ? ui::kEngraved : ui::kAccentDim, gfx::kAlignRight,
                          lit ? gfx::TextStyle::Engraved : gfx::TextStyle::Shadow);
        }
    }

    drawPilotCard(ctx);
    if (open_ >= 0) {
        const ui::Hint hints[] = {ui::kHintSelect, ui::kHintBack};
        ui::bottomBar(hints, 2);
    } else {
        ui::bottomBar(&ui::kHintSelect, 1);
    }
}

void MainScreen::drawPilotCard(Context& ctx) {
    const meta::Profile& p = ctx.profile;
    constexpr int w = 214, h = 66;
    const int x = gfx::kScreenW - ui::kMargin - w, y = 10;
    ui::panel(x, y, w, h);
    ui::emblem(p.avatar, x + 8, y + 9, 48);
    const int x0 = x + 64;
    const int rank = meta::rankIndex(p);
    ui::icon(*ui::icons::kRanks[rank], x0, y + 8);
    char credits[32];
    ui::formatCredits(p.balance, credits, sizeof credits);
    ui::text(x + w - 10, y + 14, credits, ui::Face::Body, ui::kCredits, gfx::kAlignRight);
    ui::text(x0, y + 37, p.nickname, ui::Face::Label, ui::kTextBright);
    ui::text(x0, y + 54, meta::rankName(rank), ui::Face::Small, ui::kAccent);
}

}  // namespace menu
