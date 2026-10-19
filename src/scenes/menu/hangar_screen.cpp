#include "scenes/menu/hangar_screen.h"

#include <pspctrl.h>

#include <cstdio>

#include "render/draw2d.h"
#include "render/gu_context.h"
#include "scenes/menu/mission_ui.h"
#include "ui/anim.h"
#include "ui/ui.h"

namespace menu {

namespace {

using mui::inkText;
using mui::kIconDark;
using mui::kInkAmber;
using mui::kInkGreen;
using mui::kInkRed;
using mui::withAlpha;

constexpr uint32_t kInkDark = gfx::abgr(26, 30, 26);

const ui::AtlasRect& typeIcon(meta::VehicleType t) {
    return t == meta::VehicleType::Plane ? ui::icons::kPlane : ui::icons::kHeli;
}

void formatUnit(char* out, int size, int value, const char* unit) {
    char num[24];
    ui::formatThousands(value, num, sizeof num);
    std::snprintf(out, size, "%s %s", num, unit);
}

int liveryOf(const meta::Profile& p, meta::VehicleId v) {
    const int l = p.vehicles[static_cast<int>(v)].livery;
    return l < meta::vehicle(v).liveryCount ? l : 0;
}

// Label left, value right: the stat lines of the info panels.
void statRow(int x, int y, int w, const char* label, const char* value) {
    ui::text(x, y, label, ui::Face::Body, ui::kTextDim);
    ui::text(x + w, y, value, ui::Face::Body, ui::kTextBright, gfx::kAlignRight);
}

void divider(int x, int y, int w) {
    gfx::fillRect(x, y, w, 1, gfx::abgr(0, 0, 0, 120));
    gfx::fillRect(x, y + 1, w, 1, gfx::abgr(255, 255, 255, 22));
}

// --- Hangar layout.
constexpr int kCardW = 108, kCardH = 50, kCardGap = 6;
constexpr int kCardY = ui::kBarY - 6 - kCardH;
constexpr int kStripX = ui::kMargin, kStripW = gfx::kScreenW - 2 * ui::kMargin;

// A card of the strip: a plate that lights up under the cursor, an icon, a
// name and a status line.
void drawCard(int x, const char* name, const char* sub, uint32_t subColor, uint32_t subInk,
              const ui::AtlasRect* icon, float f) {
    ui::panel(x, kCardY, kCardW, kCardH);
    if (f > 0.01f) ui::panel(x, kCardY, kCardW, kCardH, ui::Plate::Light, withAlpha(ui::kWhite, f));
    if (f > 0.01f) gfx::fillRect(x + 5, kCardY + 10, 3, kCardH - 20, withAlpha(ui::kAccent, f));
    int tx = x + 14;
    if (icon) {
        ui::icon(*icon, x + 14, kCardY + 9, mui::mix(ui::kWhite, kIconDark, f));
        tx += 26;
    }
    inkText(tx, kCardY + 12, name, ui::Face::Label, ui::kTextBright, kInkDark, f);
    inkText(x + 14, kCardY + 31, sub, ui::Face::Small, subColor, subInk, f);
}

}  // namespace

// ---------------------------------------------------------------- Hangar

void HangarScreen::rebuild(const Context& ctx) {
    count_ = 0;
    for (int i = 0; i < meta::kVehicleCount; ++i)
        if (ctx.profile.vehicles[i].owned) owned_[count_++] = static_cast<meta::VehicleId>(i);
}

void HangarScreen::showSelected(Context& ctx) {
    if (cursor_ >= count_) return;  // SHOP: the last aircraft keeps standing there
    ctx.shownVehicle = owned_[cursor_];
    ctx.shownLivery = liveryOf(ctx.profile, owned_[cursor_]);
}

void HangarScreen::enter(Context& ctx) {
    rebuild(ctx);
    if (toShop_) {
        // Back from the shop: stand on a vehicle just bought, else where we were.
        toShop_ = false;
        for (int i = 0; i < count_; ++i)
            if (!ownedBefore_[static_cast<int>(owned_[i])]) cursor_ = i;
        if (cursor_ > count_) cursor_ = count_;
    } else {
        cursor_ = 0;
        for (int i = 0; i < count_; ++i)
            if (owned_[i] == ctx.profile.lastVehicle) cursor_ = i;
        scroll_ = 0.0f;
        // A fresh visit: the nose towards the right, away from the info panel.
        ctx.orbitYaw = 2.85f;
        ctx.orbitPitch = 0.2f;
        ctx.orbitDistance = 24.0f;
    }
    showSelected(ctx);
}

void HangarScreen::update(Context& ctx, const platform::Pad& pad, float dt) {
    if (dt > 0.1f) dt = 0.1f;
    // Camera: the stick moves it around the aircraft, L/R dolly.
    ctx.orbitYaw += pad.lx * 1.7f * dt;
    ctx.orbitPitch -= pad.ly * 0.8f * dt;
    if (ctx.orbitPitch < 0.05f) ctx.orbitPitch = 0.05f;
    if (ctx.orbitPitch > 0.6f) ctx.orbitPitch = 0.6f;
    if (pad.down(PSP_CTRL_RTRIGGER)) ctx.orbitDistance -= 12.0f * dt;
    if (pad.down(PSP_CTRL_LTRIGGER)) ctx.orbitDistance += 12.0f * dt;
    if (ctx.orbitDistance < 14.0f) ctx.orbitDistance = 14.0f;
    if (ctx.orbitDistance > 30.0f) ctx.orbitDistance = 30.0f;

    if (pad.hit(PSP_CTRL_LEFT) && cursor_ > 0) {
        --cursor_;
        ctx.sfx(Sfx::Move);
        showSelected(ctx);
    }
    if (pad.hit(PSP_CTRL_RIGHT) && cursor_ < count_) {
        ++cursor_;
        ctx.sfx(Sfx::Move);
        showSelected(ctx);
    }

    if (pad.hit(PSP_CTRL_TRIANGLE) && cursor_ < count_) {
        const meta::VehicleId v = owned_[cursor_];
        const meta::VehicleInfo& info = meta::vehicle(v);
        if (info.liveryCount > 1) {
            meta::VehicleProgress& vp = ctx.profile.vehicles[static_cast<int>(v)];
            vp.livery = static_cast<uint8_t>((liveryOf(ctx.profile, v) + 1) % info.liveryCount);
            ctx.shownLivery = vp.livery;
            dirty_ = true;
            ctx.sfx(Sfx::Tab);
        } else {
            ctx.sfx(Sfx::Deny);
        }
    }

    if (pad.hit(PSP_CTRL_CROSS)) {
        if (cursor_ >= count_) {
            for (int i = 0; i < meta::kVehicleCount; ++i) ownedBefore_[i] = ctx.profile.vehicles[i].owned;
            toShop_ = true;
            ctx.sfx(Sfx::Select);
            ctx.push(ScreenId::Shop);
        } else if (owned_[cursor_] != ctx.profile.lastVehicle) {
            ctx.profile.lastVehicle = owned_[cursor_];
            dirty_ = true;
            ctx.sfx(Sfx::Select);
        } else {
            ctx.sfx(Sfx::Tab);  // already the active one
        }
    }

    if (pad.hit(PSP_CTRL_CIRCLE)) {
        if (dirty_) {
            ctx.save();
            dirty_ = false;
        }
        // The main menu stands the active aircraft in the hangar.
        ctx.shownVehicle = ctx.profile.lastVehicle;
        ctx.shownLivery = liveryOf(ctx.profile, ctx.profile.lastVehicle);
        ctx.sfx(Sfx::Back);
        ctx.pop();
    }
}

void HangarScreen::draw(Context& ctx) {
    const float dt = ctx.dt;
    const meta::Profile& p = ctx.profile;
    const bool onVehicle = cursor_ < count_;
    for (int i = 0; i <= count_; ++i) focus_[i] = ui::approach(focus_[i], i == cursor_ ? 1.0f : 0.0f, 16.0f, dt);
    plate_ = ui::approach(plate_, onVehicle ? 1.0f : 0.0f, 14.0f, dt);

    const int n = count_ + 1;
    const int total = n * (kCardW + kCardGap) - kCardGap;
    float want = 0.0f;
    if (total > kStripW) {
        const float centre = static_cast<float>(cursor_ * (kCardW + kCardGap) + kCardW / 2);
        want = centre - kStripW / 2;
        if (want < 0.0f) want = 0.0f;
        if (want > static_cast<float>(total - kStripW)) want = static_cast<float>(total - kStripW);
    }
    scroll_ = ui::approach(scroll_, want, 12.0f, dt);

    // Bottom: a soft shade for the cards, then the bar (the strip is static,
    // so it is drawn above the bar's shadow).
    gfx::gradientRect(0, kCardY - 34, gfx::kScreenW, ui::kBarY - kCardY + 34, gfx::abgr(0, 0, 0, 0),
                      gfx::abgr(0, 0, 0, 120));
    ui::Hint hints[3];
    int hc = 0;
    if (onVehicle) {
        const meta::VehicleId v = owned_[cursor_];
        if (meta::vehicle(v).liveryCount > 1) hints[hc++] = ui::Hint{&ui::icons::kBtnTriangle, "LIVERY"};
        if (v != p.lastVehicle) hints[hc++] = ui::Hint{&ui::icons::kBtnCross, "SET ACTIVE"};
    } else {
        hints[hc++] = ui::Hint{&ui::icons::kBtnCross, "SHOP"};
    }
    hints[hc++] = ui::kHintBack;
    ui::bottomBar(hints, hc, "STICK: ORBIT   L R: ZOOM");

    ui::screenTitle("HANGAR");

    // Info panel, left.
    constexpr int px = ui::kMargin, py = 44, pw = 176, ph = 134;
    ui::panel(px, py, pw, ph);
    const int ix = px + 14, iw = pw - 28;
    char buf[32];
    if (onVehicle) {
        const meta::VehicleInfo& info = meta::vehicle(owned_[cursor_]);
        ui::text(ix, py + 12, info.name, ui::Face::Title, ui::kTextBright);
        ui::text(ix, py + 36, info.role, ui::Face::Small, ui::kTextDim);
        divider(ix, py + 50, iw);
        formatUnit(buf, sizeof buf, info.maxSpeedKmh, "KM/H");
        statRow(ix, py + 58, iw, "MAX SPEED", buf);
        formatUnit(buf, sizeof buf, info.ceilingM, "M");
        statRow(ix, py + 76, iw, "CEILING", buf);
        std::snprintf(buf, sizeof buf, "%d.%d G", static_cast<int>(info.maxG), static_cast<int>(info.maxG * 10.0f + 0.5f) % 10);
        statRow(ix, py + 94, iw, "MAX G", buf);
        formatUnit(buf, sizeof buf, info.rangeKm, "KM");
        statRow(ix, py + 112, iw, "RANGE", buf);
    } else {
        int forSale = 0, soon = 0, mine = 0;
        for (int i = 0; i < meta::kVehicleCount; ++i) {
            const meta::VehicleInfo& info = meta::vehicle(static_cast<meta::VehicleId>(i));
            if (p.vehicles[i].owned) ++mine;
            else if (info.available) ++forSale;
            else ++soon;
        }
        ui::text(ix, py + 12, "SHOP", ui::Face::Title, ui::kTextBright);
        ui::text(ix, py + 36, "NEW AIRCRAFT FOR CREDITS", ui::Face::Small, ui::kTextDim);
        divider(ix, py + 50, iw);
        ui::formatCredits(p.balance, buf, sizeof buf);
        ui::text(ix, py + 58, "BALANCE", ui::Face::Body, ui::kTextDim);
        ui::text(ix + iw, py + 58, buf, ui::Face::Body, ui::kCredits, gfx::kAlignRight);
        std::snprintf(buf, sizeof buf, "%d", forSale);
        statRow(ix, py + 76, iw, "FOR SALE", buf);
        std::snprintf(buf, sizeof buf, "%d", soon);
        statRow(ix, py + 94, iw, "COMING SOON", buf);
        std::snprintf(buf, sizeof buf, "%d", mine);
        statRow(ix, py + 112, iw, "YOURS", buf);
    }

    // Livery plate, top right (gone on the SHOP card).
    if (plate_ > 0.02f && count_ > 0) {
        const meta::VehicleId v = owned_[cursor_ < count_ ? cursor_ : count_ - 1];
        const meta::VehicleInfo& info = meta::vehicle(v);
        constexpr int w = 172, h = 70;
        const int x = gfx::kScreenW - ui::kMargin - w, y = 10;
        const float a = plate_;
        ui::panel(x, y, w, h, ui::Plate::Panel, withAlpha(ui::kWhite, a));
        ui::text(x + 14, y + 11, "LIVERY", ui::Face::Small, withAlpha(ui::kTextDim, a));
        if (info.liveryCount > 1) ui::icon(ui::icons::kBtnTriangle, x + w - 14 - 18, y + 8, withAlpha(ui::kWhite, a));
        ui::text(x + 14, y + 26, info.liveryNames[liveryOf(p, v)], ui::Face::Label, withAlpha(ui::kTextBright, a));
        std::snprintf(buf, sizeof buf, "%d / %d", liveryOf(p, v) + 1, info.liveryCount);
        ui::text(x + w - 14, y + 29, buf, ui::Face::Small, withAlpha(ui::kTextDim, a), gfx::kAlignRight);
        ui::text(x + 14, y + 52, "SORTIES", ui::Face::Small, withAlpha(ui::kTextDim, a));
        ui::formatThousands(static_cast<int>(p.vehicles[static_cast<int>(v)].sorties), buf, sizeof buf);
        ui::text(x + w - 14, y + 49, buf, ui::Face::Label, withAlpha(ui::kTextBright, a), gfx::kAlignRight);
    }

    // The strip of cards: owned vehicles, then SHOP.
    gfx::clip(0, kCardY - 4, gfx::kScreenW, ui::kBarY - 2);
    const int sc = static_cast<int>(scroll_ + 0.5f);
    for (int i = 0; i < n; ++i) {
        const int x = kStripX + i * (kCardW + kCardGap) - sc;
        if (x + kCardW < 0 || x > gfx::kScreenW) continue;
        const float f = focus_[i];
        if (i < count_) {
            const meta::VehicleId v = owned_[i];
            const meta::VehicleInfo& info = meta::vehicle(v);
            const bool active = v == p.lastVehicle;
            drawCard(x, info.name, active ? "ACTIVE" : info.liveryNames[liveryOf(p, v)],
                     active ? ui::kAccent : ui::kTextDim, active ? kInkAmber : kInkDark, &typeIcon(info.type), f);
        } else {
            ui::formatCredits(p.balance, buf, sizeof buf);
            drawCard(x, "SHOP", buf, ui::kCredits, kInkDark, nullptr, f);
        }
    }
    gfx::unclip();
}

// ---------------------------------------------------------------- Shop

namespace {

// A gallery of cards, two to a row, scrolling under the bar's shadow.
constexpr int kShopCols = 2, kShopGapX = 8, kShopGapY = 8;
constexpr int kShopX = ui::kMargin, kShopTop = 46;
constexpr int kShopW = (gfx::kScreenW - 2 * ui::kMargin - kShopGapX) / kShopCols;  // 222
constexpr int kShopH = 140;
constexpr int kShopViewTop = 42, kShopViewBottom = ui::kBarY - 34;  // cards keep clear of the bar's shadow
// The 256x64 schematic cell at ~0.77: the line art stays legible at 1:1 on the LCD.
constexpr int kShopArtW = 196, kShopArtH = 49;

meta::VehicleId g_pending = meta::VehicleId::Su35S;  // the vehicle the open dialog is about

// The dialog's YES: the confirmation sound has already played.
void buyPending(Context& ctx) {
    if (meta::buyVehicle(ctx.profile, g_pending)) ctx.save();
    else ctx.sfx(Sfx::Deny);
}

// A red box over the dark schematic, like a stamp on a file.
void drawStamp(int cx, int cy) {
    const char* s = "CLASSIFIED";
    const int w = ui::textWidth(s, ui::Face::Label) + 24, h = 26;
    const int x = cx - w / 2, y = cy - h / 2;
    const uint32_t red = gfx::abgr(222, 70, 54);
    gfx::fillRect(x, y, w, h, gfx::abgr(12, 6, 6, 130));
    mui::frame(x, y, w, h, 2, red);
    ui::text(cx, y + (h - ui::capHeight(ui::Face::Label) + 1) / 2, s, ui::Face::Label, red, gfx::kAlignCenter);
}

float contentBottom() {
    const int rows = (meta::kVehicleCount + kShopCols - 1) / kShopCols;
    return static_cast<float>(kShopTop + rows * (kShopH + kShopGapY) - kShopGapY);
}

// One card: the side view on a dark screen (a silhouette under a CLASSIFIED
// stamp when the machine is in development), name and role, and a plate with
// the state and the price. The focused card turns light metal.
void drawCard(const meta::Profile& p, int i, int x, int y, float f, bool haveArt) {
    const meta::VehicleInfo& info = meta::vehicle(static_cast<meta::VehicleId>(i));
    const bool owned = p.vehicles[i].owned;
    const bool poor = !owned && info.available && p.balance < info.price;

    ui::panel(x, y, kShopW, kShopH);
    if (f > 0.01f) {
        ui::panel(x, y, kShopW, kShopH, ui::Plate::Light, withAlpha(ui::kWhite, f));
        gfx::fillRect(x + 5, y + 14, 3, kShopH - 28, withAlpha(ui::kAccent, f));
    }

    // Side view. White line art stays white on its dark screen, lit card or not.
    const int ix = x + 12, iy = y + 10, iw = kShopW - 24, ih = 54;
    if (info.available) ui::panel(ix, iy, iw, ih, ui::Plate::Inset);
    if (haveArt) {
        const uint32_t tint = info.available ? ui::kWhite : gfx::abgr(30, 34, 32);
        gfx::sprite(ui::res().schematics, 0, info.schematicRow * 64, 256, 64, ix + (iw - kShopArtW) / 2,
                    iy + (ih - kShopArtH) / 2, kShopArtW, kShopArtH, tint, true);
    } else {
        const ui::AtlasRect& ic = typeIcon(info.type);
        ui::iconScaled(ic, ix + iw / 2 - 18, iy + (ih - 36) / 2, 36, 36,
                       info.available ? ui::kWhite : gfx::abgr(30, 34, 32));
    }
    if (!info.available) drawStamp(x + kShopW / 2, iy + ih / 2);

    // Name and role.
    const int nx = x + 14;
    ui::icon(typeIcon(info.type), nx, y + 68, mui::mix(ui::kWhite, kIconDark, f));
    inkText(nx + 26, y + 68 + (20 - ui::capHeight(ui::Face::Label) + 1) / 2, info.name, ui::Face::Label,
            ui::kTextBright, kInkDark, f);
    inkText(nx, y + 92, info.role, ui::Face::Small, ui::kTextDim, kInkDark, f);

    // State on the left, the price on the right.
    const int px = x + 12, py = y + 108, pw = kShopW - 24, ph = 24;
    ui::panel(px, py, pw, ph, ui::Plate::Inset);
    if (owned) {
        ui::icon(ui::icons::kCheck, px + 9, py + (ph - 16) / 2);
        ui::textInBox(px + 31, py, ph, "OWNED", ui::Face::Body, ui::kGood);
        return;
    }
    const char* state = !info.available ? "IN DEVELOPMENT" : (poor ? "NOT ENOUGH" : "AVAILABLE");
    ui::textInBox(px + 11, py, ph, state, ui::Face::Body, info.available && !poor ? ui::kAccent : ui::kBad);
    char price[24];
    ui::formatCredits(info.price, price, sizeof price);
    ui::textInBox(px + pw - 11, py, ph, price, ui::Face::Body,
                  !info.available ? ui::kTextDim : (poor ? ui::kBad : ui::kCredits), gfx::kAlignRight);
}

}  // namespace

void ShopScreen::enter(Context& ctx) {
    if (ctx.time - lastDraw_ < 0.8f) return;  // a dialog just closed: stay where we were
    // A fresh visit starts on the first aircraft that can be bought.
    cursor_ = 0;
    for (int i = 0; i < meta::kVehicleCount; ++i) {
        const meta::VehicleId v = static_cast<meta::VehicleId>(i);
        if (meta::vehicle(v).available && !ctx.profile.vehicles[i].owned) {
            cursor_ = i;
            break;
        }
    }
    scroll_ = target_ = 0.0f;
    snap_ = true;
}

void ShopScreen::update(Context& ctx, const platform::Pad& pad, float) {
    constexpr int n = meta::kVehicleCount;
    auto go = [&](int to) {
        cursor_ = to;
        ctx.sfx(Sfx::Move);
    };
    if (pad.hit(PSP_CTRL_LEFT) && cursor_ % kShopCols > 0) go(cursor_ - 1);
    if (pad.hit(PSP_CTRL_RIGHT) && cursor_ % kShopCols < kShopCols - 1 && cursor_ + 1 < n) go(cursor_ + 1);
    if (pad.hit(PSP_CTRL_UP) && cursor_ >= kShopCols) go(cursor_ - kShopCols);
    if (pad.hit(PSP_CTRL_DOWN)) {
        // From a column that the last, shorter row lacks: land on its last card.
        if (cursor_ + kShopCols < n) go(cursor_ + kShopCols);
        else if (cursor_ / kShopCols < (n - 1) / kShopCols) go(n - 1);
    }
    if (pad.hit(PSP_CTRL_CROSS)) {
        const meta::VehicleId v = static_cast<meta::VehicleId>(cursor_);
        const meta::VehicleInfo& info = meta::vehicle(v);
        if (ctx.profile.vehicles[cursor_].owned) {
            ctx.sfx(Sfx::Tab);  // already in the hangar
        } else if (!meta::canBuy(ctx.profile, v)) {
            ctx.sfx(Sfx::Deny);  // classified, or not enough credits
        } else {
            char price[24];
            ui::formatCredits(info.price, price, sizeof price);
            std::snprintf(question_, sizeof question_, "BUY %s FOR %s?", info.name, price);
            g_pending = v;
            ctx.confirm(question_, buyPending, "BUY");
        }
    }
    if (pad.hit(PSP_CTRL_CIRCLE)) {
        ctx.sfx(Sfx::Back);
        ctx.pop();
    }
}

void ShopScreen::draw(Context& ctx) {
    const float dt = ctx.dt;
    lastDraw_ = ctx.time;
    const meta::Profile& p = ctx.profile;
    for (int i = 0; i < meta::kVehicleCount; ++i)
        focus_[i] = ui::approach(focus_[i], i == cursor_ ? 1.0f : 0.0f, 16.0f, dt);

    // The cursor's row stays in view; the gallery glides there.
    const float content = contentBottom();
    const float maxScroll = content > kShopViewBottom ? content - kShopViewBottom : 0.0f;
    const float rowTop = static_cast<float>(kShopTop + (cursor_ / kShopCols) * (kShopH + kShopGapY));
    target_ = mui::scrollToShow(target_, rowTop, rowTop + kShopH, kShopTop, kShopViewBottom, maxScroll);
    if (snap_) scroll_ = target_, snap_ = false;
    scroll_ = ui::approach(scroll_, target_, 12.0f, dt);
    const int sc = static_cast<int>(scroll_ + 0.5f);

    ui::screenTitle("SHOP");
    // Balance, top right.
    {
        constexpr int w = 190, h = 32;
        const int x = gfx::kScreenW - ui::kMargin - w, y = 10;
        char buf[32];
        ui::formatCredits(p.balance, buf, sizeof buf);
        ui::panel(x, y, w, h);
        ui::text(x + 14, y + 12, "BALANCE", ui::Face::Small, ui::kTextDim);
        ui::text(x + w - 14, y + 9, buf, ui::Face::Label, ui::kCredits, gfx::kAlignRight);
    }

    gfx::clip(0, kShopViewTop, gfx::kScreenW, ui::kBarY);
    const bool haveArt = ui::res().schematics.loaded();
    for (int i = 0; i < meta::kVehicleCount; ++i) {
        const int x = kShopX + (i % kShopCols) * (kShopW + kShopGapX);
        const int y = kShopTop + (i / kShopCols) * (kShopH + kShopGapY) - sc;
        if (y + kShopH < kShopViewTop || y > ui::kBarY) continue;
        drawCard(p, i, x, y, focus_[i], haveArt);
    }
    // Cards slide under the title row: a soft edge instead of a hard cut.
    gfx::gradientRect(0, kShopViewTop, gfx::kScreenW, 10, gfx::abgr(4, 6, 5, 140), gfx::abgr(4, 6, 5, 0));
    gfx::unclip();

    const ui::Hint buy{&ui::icons::kBtnCross, "BUY"};
    const ui::Hint hints[] = {buy, ui::kHintBack};
    const meta::VehicleId cv = static_cast<meta::VehicleId>(cursor_);
    ui::bottomBar(meta::canBuy(p, cv) ? hints : hints + 1, meta::canBuy(p, cv) ? 2 : 1);
    mui::scrollbar(gfx::kScreenW - 9, kShopTop, kShopViewBottom - kShopTop, static_cast<float>(kShopViewBottom - kShopTop),
                   content - kShopTop, scroll_);
}

}  // namespace menu
