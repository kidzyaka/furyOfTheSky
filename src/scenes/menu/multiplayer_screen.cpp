#include "scenes/menu/multiplayer_screen.h"

#include <pspctrl.h>

#include <cmath>

#include "render/draw2d.h"
#include "render/gu_context.h"
#include "ui/anim.h"
#include "ui/ui.h"

namespace menu {

namespace {

uint32_t withAlpha(uint32_t c, float a) {
    a = ui::clamp01(a);
    return (c & 0x00FFFFFFu) | (static_cast<uint32_t>(((c >> 24) & 0xFF) * a + 0.5f) << 24);
}

// RGB blend (alpha of `a` kept).
uint32_t mixRgb(uint32_t a, uint32_t b, float t) {
    uint32_t out = a & 0xFF000000u;
    for (int s = 0; s <= 16; s += 8) {
        const float ca = static_cast<float>((a >> s) & 0xFF), cb = static_cast<float>((b >> s) & 0xFF);
        out |= static_cast<uint32_t>(ca + (cb - ca) * t + 0.5f) << s;
    }
    return out;
}

// One line of text that is light on dark metal and engraved on the lit plate:
// the two inks cross-fade with `lit` (0..1), as ui::button does for labels.
void inkText(int x, int y, const char* s, ui::Face f, uint32_t idle, uint32_t litInk, float lit,
             gfx::TextAlign align = gfx::kAlignLeft) {
    if (lit < 0.99f) ui::text(x, y, s, f, withAlpha(idle, 1.0f - lit), align);
    if (lit > 0.01f) ui::text(x, y, s, f, withAlpha(litInk, lit), align, gfx::TextStyle::Engraved);
}

void inkParagraph(int x, int y, int w, const char* s, ui::Face f, uint32_t idle, uint32_t litInk, float lit) {
    if (lit < 0.99f) ui::paragraph(x, y, w, s, f, withAlpha(idle, 1.0f - lit), 1);
    if (lit > 0.01f)
        ui::res().font.drawWrapped(static_cast<int>(f), x, y, w, s, withAlpha(litInk, lit), 1,
                                   gfx::TextStyle::Engraved);
}

constexpr uint32_t kInkDark = gfx::abgr(26, 30, 26);
constexpr uint32_t kInkAmber = gfx::abgr(104, 70, 8);

// --- Network cards.
struct Net {
    const char* name;
    const char* mode;
    const char* text;
    const ui::AtlasRect* icon;
};
const Net kNets[2] = {
    {"LAN", "LOCAL WIRELESS \xE2\x80\xA2 AD HOC", "Play with PSP systems nearby.", &ui::icons::kLan},
    {"INTERNET", "INFRASTRUCTURE", "Connect through a Wi-Fi access point.", &ui::icons::kInternet},
};

constexpr int kCardW = 212, kCardH = 158, kCardY = 60;

void drawCard(int x, int y, const Net& n, float f) {
    ui::panel(x, y, kCardW, kCardH);
    if (f > 0.01f) ui::panel(x, y, kCardW, kCardH, ui::Plate::Light, withAlpha(ui::kWhite, f));
    if (f > 0.01f) gfx::fillRect(x + 6, y + 14, 3, kCardH - 28, withAlpha(ui::kAccent, f));
    // The icon is light line art with dark details: it sits in a dark well so it reads on the lit plate too.
    ui::panel(x + 14, y + 12, 60, 60, ui::Plate::Inset);
    ui::iconScaled(*n.icon, x + 20, y + 18, 48, 48);
    inkText(x + 88, y + 33, n.name, ui::Face::Title, ui::kTextBright, kInkDark, f);
    gfx::fillRect(x + 18, y + 76, kCardW - 36, 1, mixRgb(gfx::abgr(0, 0, 0, 120), gfx::abgr(60, 66, 58, 120), f));
    inkText(x + 18, y + 87, n.mode, ui::Face::Body, ui::kAccent, kInkAmber, f);
    inkParagraph(x + 18, y + 112, kCardW - 36, n.text, ui::Face::Body, ui::kText, kInkDark, f);
}

}  // namespace

// ---------------------------------------------------------------- Multiplayer

void MultiplayerScreen::enter(Context& ctx) { cursor_ = ctx.network == Network::Internet ? 1 : 0; }

void MultiplayerScreen::update(Context& ctx, const platform::Pad& pad, float) {
    if ((pad.hit(PSP_CTRL_LEFT) || pad.hit(PSP_CTRL_UP)) && cursor_ > 0) {
        cursor_ = 0;
        ctx.sfx(Sfx::Move);
    }
    if ((pad.hit(PSP_CTRL_RIGHT) || pad.hit(PSP_CTRL_DOWN)) && cursor_ < 1) {
        cursor_ = 1;
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_CROSS)) {
        ctx.network = cursor_ == 0 ? Network::Lan : Network::Internet;
        ctx.sfx(Sfx::Select);
        ctx.push(ScreenId::Lobby);
    }
    if (pad.hit(PSP_CTRL_CIRCLE)) {
        ctx.sfx(Sfx::Back);
        ctx.pop();
    }
}

void MultiplayerScreen::draw(Context& ctx) {
    for (int i = 0; i < 2; ++i) focus_[i] = ui::approach(focus_[i], i == cursor_ ? 1.0f : 0.0f, 16.0f, ctx.dt);
    const ui::Hint hints[] = {ui::kHintSelect, ui::kHintBack};
    ui::bottomBar(hints, 2);
    ui::screenTitle("MULTIPLAYER");
    for (int i = 0; i < 2; ++i) {
        const int x = ui::kMargin + i * (kCardW + 28);
        drawCard(x, kCardY - static_cast<int>(4.0f * focus_[i] + 0.5f), kNets[i], focus_[i]);
    }
}

// ---------------------------------------------------------------- Lobby

namespace {

constexpr float kSearchTime = 3.0f;
constexpr float kScanPeriod = 1.1f;  // one sweep of the scan line

constexpr int kListX = ui::kMargin, kListY = 48, kListW = 452, kListH = 130;
constexpr int kInX = kListX + 8, kInY = kListY + 30, kInW = kListW - 16, kInH = kListH - 38;

}  // namespace

void LobbyScreen::enter(Context&) {
    search_ = 0.0f;
    toast_ = toastAge_ = 0.0f;
    focus_ = 0.0f;
}

void LobbyScreen::update(Context& ctx, const platform::Pad& pad, float) {
    if (pad.hit(PSP_CTRL_CROSS)) {
        ctx.sfx(Sfx::Deny);  // nothing to host with yet
        toast_ = 2.2f;
        toastAge_ = 0.0f;
    }
    if (pad.hit(PSP_CTRL_TRIANGLE) && search_ >= kSearchTime) {
        search_ = 0.0f;
        ctx.sfx(Sfx::Select);
    }
    if (pad.hit(PSP_CTRL_CIRCLE)) {
        ctx.sfx(Sfx::Back);
        ctx.pop();
    }
}

void LobbyScreen::draw(Context& ctx) {
    const float dt = ctx.dt;
    search_ += dt;
    if (toast_ > 0.0f) {
        toast_ -= dt;
        toastAge_ += dt;
    }
    focus_ = ui::approach(focus_, 1.0f, 12.0f, dt);

    const bool lan = ctx.network == Network::Lan;
    const bool searching = search_ < kSearchTime;
    const ui::Hint hints[] = {ui::kHintSelect, {&ui::icons::kBtnTriangle, "SEARCH AGAIN"}, ui::kHintBack};
    ui::bottomBar(hints, 3);
    ui::screenTitle(lan ? "LAN LOBBIES" : "INTERNET LOBBIES");

    // The list: network header, then the dark well the lobbies would fill.
    ui::panel(kListX, kListY, kListW, kListH);
    ui::icon(lan ? ui::icons::kLan : ui::icons::kInternet, kListX + 14, kListY + 8);
    ui::text(kListX + 46, kListY + 12, lan ? "LOCAL WIRELESS \xE2\x80\xA2 AD HOC" : "INFRASTRUCTURE", ui::Face::Body,
             ui::kTextDim);
    ui::text(kListX + kListW - 16, kListY + 12, searching ? "SEARCHING" : "0 FOUND", ui::Face::Body,
             searching ? ui::kAccent : ui::kTextDim, gfx::kAlignRight);
    ui::panel(kInX, kInY, kInW, kInH, ui::Plate::Inset);

    const int cx = kInX + kInW / 2;
    if (searching) {
        // A scan line sweeps the well, an amber trail behind it.
        const float t = std::fmod(search_ / kScanPeriod, 1.0f);
        const int sx = kInX + 6 + static_cast<int>(ui::easeInOut(t) * (kInW - 12));
        const float fadeOut = ui::clamp01((kSearchTime - search_) * 3.0f);
        gfx::clip(kInX + 6, kInY + 6, kInX + kInW - 6, kInY + kInH - 6);
        gfx::hGradientRect(sx - 80, kInY + 6, 80, kInH - 12, withAlpha(ui::kAccent, 0.0f),
                           withAlpha(ui::kAccent, 0.22f * fadeOut));
        gfx::fillRect(sx, kInY + 6, 2, kInH - 12, withAlpha(ui::kAccent, 0.85f * fadeOut));
        gfx::unclip();
        ui::text(cx, kInY + 16, "SEARCHING\xE2\x80\xA6", ui::Face::Label, ui::kTextBright, gfx::kAlignCenter);
        ui::progressBar(cx - 100, kInY + 42, 200, search_ / kSearchTime);
        ui::text(cx, kInY + 62, lan ? "Looking for PSP systems nearby" : "Looking for lobbies through the access point",
                 ui::Face::Body, ui::kTextDim, gfx::kAlignCenter);
    } else {
        const float a = ui::clamp01((search_ - kSearchTime) * 4.0f);
        ui::text(cx, kInY + 22, "NO LOBBIES FOUND", ui::Face::Label, withAlpha(ui::kTextBright, a), gfx::kAlignCenter);
        // "PRESS (triangle) TO SEARCH AGAIN", the icon inline.
        const int w1 = ui::textWidth("PRESS", ui::Face::Body), w2 = ui::textWidth("TO SEARCH AGAIN", ui::Face::Body);
        const int icw = ui::icons::kBtnTriangle.w;
        int x = cx - (w1 + 6 + icw + 6 + w2) / 2;
        const int y = kInY + 52;
        ui::text(x, y, "PRESS", ui::Face::Body, withAlpha(ui::kTextDim, a));
        x += w1 + 6;
        ui::icon(ui::icons::kBtnTriangle, x, y - 3, withAlpha(ui::kWhite, a));
        x += icw + 6;
        ui::text(x, y, "TO SEARCH AGAIN", ui::Face::Body, withAlpha(ui::kTextDim, a));
    }

    ui::ButtonLook look;
    look.focus = focus_;
    ui::button(ui::kMargin, kListY + kListH + 12, 230, ui::ButtonKind::Primary, "CREATE LOBBY", look);

    // "In development" banner next to the button: slides in, fades out.
    if (toast_ > 0.0f) {
        const float a = ui::clamp01(std::fmin(toast_ * 5.0f, toastAge_ * 9.0f));
        constexpr int w = 214, h = 48;
        const int x = ui::kMargin + 230 + 8 - static_cast<int>((1.0f - ui::easeOutCubic(toastAge_ * 6.0f)) * 14.0f);
        const int y = kListY + kListH + 12 + 16 - h / 2;
        ui::panel(x, y, w, h, ui::Plate::Panel, withAlpha(ui::kWhite, a));
        gfx::fillRect(x + 8, y + 8, 3, h - 16, withAlpha(ui::kAccent, a));
        ui::paragraph(x + 22, y + 7, w - 34, "MULTIPLAYER IS IN DEVELOPMENT", ui::Face::Label, withAlpha(ui::kTextBright, a), 0);
    }
}

}  // namespace menu
