#include "scenes/menu/settings_screen.h"

#include <pspctrl.h>

#include <cstdio>
#include <cstring>

#include "render/draw2d.h"
#include "render/gu_context.h"
#include "ui/anim.h"
#include "ui/ui.h"

namespace menu {

namespace {

enum Row { kInvertPitch, kSfx, kVoice, kUi, kShake, kDebug, kReset };

struct RowInfo {
    const char* label;
    const char* help;
};
const RowInfo kRowInfo[SettingsScreen::kRows] = {
    {"INVERT PITCH", "OFF: stick back raises the nose.\nON: stick forward raises it."},
    {"SOUND EFFECTS", "Engines, wind, weapons and explosions."},
    {"VOICE INFORMER", "The RI-65 informer: warnings and callouts."},
    {"INTERFACE SOUNDS", "Menu clicks and confirmations."},
    {"CAMERA SHAKE", "Shaking of the view from buffet, gun fire and hits."},
    {"DEBUG INFO", "FPS, build id and the S / L / G timings in the corner of the flight screen."},
    {"RESET PROGRESS", "Erases the campaign, credits, aircraft and statistics. Callsign, emblem and settings stay."},
};

constexpr int kX = ui::kMargin, kW = 310, kTop = 48, kPitch = 32, kRowH = 28;
constexpr int kResetGap = 10;            // extra space above the dangerous row
constexpr int kClipTop = 42;
constexpr int kHelpX = kX + kW + 8, kHelpW = gfx::kScreenW - ui::kMargin - kHelpX;

constexpr int kValueW = 148;             // arrow, 112 px of value, arrow
constexpr int kSegW = 7, kSegGap = 2, kSegH = 12;

constexpr uint32_t kInkDark = gfx::abgr(26, 30, 26);
constexpr uint32_t kInkAmber = gfx::abgr(98, 64, 8);
constexpr uint32_t kInkGreen = gfx::abgr(30, 96, 40);
constexpr uint32_t kInkOff = gfx::abgr(62, 68, 62);

uint32_t withAlpha(uint32_t c, float a) {
    a = ui::clamp01(a);
    return (c & 0x00FFFFFFu) | (static_cast<uint32_t>(((c >> 24) & 0xFF) * a + 0.5f) << 24);
}

uint32_t mixRgb(uint32_t a, uint32_t b, float t) {
    uint32_t out = a & 0xFF000000u;
    for (int s = 0; s <= 16; s += 8) {
        const float ca = static_cast<float>((a >> s) & 0xFF), cb = static_cast<float>((b >> s) & 0xFF);
        out |= static_cast<uint32_t>(ca + (cb - ca) * t + 0.5f) << s;
    }
    return out;
}

int rowY(int i) { return kTop + i * kPitch + (i == kReset ? kResetGap : 0); }

uint8_t& volumeOf(meta::Settings& s, int row) {
    return row == kSfx ? s.sfxVolume : (row == kVoice ? s.voiceVolume : s.uiVolume);
}

// The dialog's YES: a new pilot, but the callsign, emblem and settings are
// not progress and stay.
void resetProgress(Context& ctx) {
    meta::Profile& p = ctx.profile;
    char nick[meta::kNicknameBytes];
    std::memcpy(nick, p.nickname, sizeof nick);
    nick[sizeof nick - 1] = 0;
    const uint8_t avatar = p.avatar;
    const meta::Settings settings = p.settings;
    meta::resetProfile(p, nick);
    p.avatar = avatar;
    p.settings = settings;
    ctx.shownVehicle = p.lastVehicle;
    ctx.shownLivery = p.vehicles[static_cast<int>(p.lastVehicle)].livery;
    ctx.save();
}

// The value of a row, at the right end of its button: arrows around it on
// the focused row, ink following the plate (light on dark metal, engraved on
// the lit one). `step` is 0..10 for the volume bars, -1 for ON/OFF text.
void drawValue(int x, int y, float f, int step, bool on) {
    const int bx = x + static_cast<int>(6.0f * f + 0.5f);
    const int right = bx + kW - 10, left = right - kValueW;
    const int ay = y + (kRowH - ui::icons::kArrowLeft.h) / 2;
    const int innerL = left + 18, innerW = kValueW - 36;
    const int ty = y + (kRowH - ui::capHeight(ui::Face::Label) + 1) / 2;
    if (step < 0) {
        const char* s = on ? "ON" : "OFF";
        const uint32_t idle = on ? ui::kGood : ui::kTextDim, litInk = on ? kInkGreen : kInkOff;
        const int cx = innerL + innerW / 2;
        if (f < 0.99f) ui::text(cx, ty, s, ui::Face::Label, withAlpha(idle, 1.0f - f), gfx::kAlignCenter);
        if (f > 0.01f)
            ui::text(cx, ty, s, ui::Face::Label, withAlpha(litInk, f), gfx::kAlignCenter, gfx::TextStyle::Engraved);
    } else {
        const int by = y + (kRowH - kSegH) / 2;
        const uint32_t full = mixRgb(ui::kAccent, kInkAmber, f);
        const uint32_t empty = mixRgb(gfx::abgr(14, 18, 16, 210), gfx::abgr(112, 120, 110), f);
        for (int i = 0; i < 10; ++i) gfx::fillRect(innerL + i * (kSegW + kSegGap), by, kSegW, kSegH, i < step ? full : empty);
        char num[8];
        std::snprintf(num, sizeof num, "%d", step);
        const int nx = innerL + innerW;
        if (f < 0.99f) ui::text(nx, ty, num, ui::Face::Label, withAlpha(ui::kTextBright, 1.0f - f), gfx::kAlignRight);
        if (f > 0.01f)
            ui::text(nx, ty, num, ui::Face::Label, withAlpha(kInkDark, f), gfx::kAlignRight, gfx::TextStyle::Engraved);
    }
    if (f > 0.01f) {
        // Arrows dim at the ends of a bar.
        const float la = step == 0 ? 0.3f : 1.0f, ra = step == 10 ? 0.3f : 1.0f;
        ui::icon(ui::icons::kArrowLeft, left, ay, withAlpha(gfx::abgr(34, 40, 34), f * la));
        ui::icon(ui::icons::kArrowRight, right - ui::icons::kArrowRight.w, ay, withAlpha(gfx::abgr(34, 40, 34), f * ra));
    }
}

}  // namespace

void SettingsScreen::enter(Context&) {}

void SettingsScreen::change(Context& ctx, int dir, bool first) {
    meta::Settings& s = ctx.profile.settings;
    const bool toggle = cursor_ == kInvertPitch || cursor_ == kShake || cursor_ == kDebug;
    if (toggle && !first) return;  // a toggle flips once per press, not while held
    switch (cursor_) {
        case kInvertPitch: s.invertPitch = !s.invertPitch; break;
        case kShake: s.cameraShake = !s.cameraShake; break;
        case kDebug: s.debugInfo = !s.debugInfo; break;
        case kSfx:
        case kVoice:
        case kUi: {
            uint8_t& v = volumeOf(s, cursor_);
            const int next = static_cast<int>(v) + dir;
            if (next < 0 || next > meta::kVolumeSteps) return;
            v = static_cast<uint8_t>(next);
            break;
        }
        default: return;
    }
    ctx.sfx(Sfx::Tab);
}

void SettingsScreen::update(Context& ctx, const platform::Pad& pad, float dt) {
    if (pad.hit(PSP_CTRL_UP) && cursor_ > 0) {
        --cursor_;
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_DOWN) && cursor_ < kRows - 1) {
        ++cursor_;
        ctx.sfx(Sfx::Move);
    }

    // Left/right: a step on press, then repeating while held (volumes).
    const int heldDir = pad.down(PSP_CTRL_RIGHT) ? 1 : (pad.down(PSP_CTRL_LEFT) ? -1 : 0);
    if (pad.hit(PSP_CTRL_LEFT) || pad.hit(PSP_CTRL_RIGHT)) {
        change(ctx, pad.hit(PSP_CTRL_RIGHT) ? 1 : -1, true);
        repeat_ = 0.4f;
    } else if (heldDir != 0) {
        repeat_ -= dt;
        if (repeat_ <= 0.0f) {
            change(ctx, heldDir, false);
            repeat_ += 0.08f;
        }
    }

    if (pad.hit(PSP_CTRL_CROSS)) {
        if (cursor_ == kReset) {
            ctx.confirm("ERASE ALL PROGRESS? THIS CANNOT BE UNDONE.", resetProgress, "ERASE");
        } else if (cursor_ == kInvertPitch || cursor_ == kShake || cursor_ == kDebug) {
            change(ctx, 1, true);
        }
    }
    if (pad.hit(PSP_CTRL_CIRCLE)) {
        ctx.save();
        ctx.sfx(Sfx::Back);
        ctx.pop();
    }
}

void SettingsScreen::draw(Context& ctx) {
    const float dt = ctx.dt;
    for (int i = 0; i < kRows; ++i) focus_[i] = ui::approach(focus_[i], i == cursor_ ? 1.0f : 0.0f, 16.0f, dt);

    // Scroll so that the cursor's row clears the bar's shadow.
    const float viewBottom = ui::kBarY - 34.0f;
    const float cb = static_cast<float>(rowY(cursor_) + kRowH);
    scroll_ = ui::approach(scroll_, cb > viewBottom ? cb - viewBottom : 0.0f, 12.0f, dt);
    const int sc = static_cast<int>(scroll_ + 0.5f);

    ui::screenTitle("SETTINGS");

    // The plate on the right explains the focused row.
    ui::panel(kHelpX, kTop, kHelpW, 164);
    ui::text(kHelpX + 12, kTop + 12, "ABOUT", ui::Face::Small, ui::kTextDim);
    gfx::fillRect(kHelpX + 12, kTop + 26, kHelpW - 24, 1, gfx::abgr(0, 0, 0, 120));
    ui::paragraph(kHelpX + 12, kTop + 34, kHelpW - 24, kRowInfo[cursor_].help, ui::Face::Body, ui::kText, 1);

    gfx::clip(0, kClipTop, gfx::kScreenW, ui::kBarY);
    const meta::Settings& s = ctx.profile.settings;
    for (int i = 0; i < kRows; ++i) {
        const int y = rowY(i) - sc;
        if (y + kRowH < kClipTop || y > ui::kBarY) continue;
        ui::ButtonLook look;
        look.focus = focus_[i];
        ui::button(kX, y, kW, ui::ButtonKind::Row, kRowInfo[i].label, look);
        switch (i) {
            case kInvertPitch: drawValue(kX, y, focus_[i], -1, s.invertPitch); break;
            case kShake: drawValue(kX, y, focus_[i], -1, s.cameraShake); break;
            case kDebug: drawValue(kX, y, focus_[i], -1, s.debugInfo); break;
            case kSfx: drawValue(kX, y, focus_[i], s.sfxVolume, true); break;
            case kVoice: drawValue(kX, y, focus_[i], s.voiceVolume, true); break;
            case kUi: drawValue(kX, y, focus_[i], s.uiVolume, true); break;
            default: break;
        }
    }
    gfx::unclip();

    if (cursor_ == kReset) {
        const ui::Hint hints[] = {ui::kHintSelect, ui::kHintBack};
        ui::bottomBar(hints, 2);
    } else {
        const ui::Hint hints[] = {ui::kHintChange, ui::kHintBack};
        ui::bottomBar(hints, 2);
    }
}

}  // namespace menu
