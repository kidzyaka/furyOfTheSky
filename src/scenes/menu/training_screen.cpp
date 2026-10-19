#include "scenes/menu/training_screen.h"

#include <pspctrl.h>

#include "game/bot.h"
#include "render/draw2d.h"
#include "render/gu_context.h"
#include "ui/anim.h"
#include "ui/ui.h"

namespace menu {

namespace {

constexpr int kX = ui::kMargin;
constexpr int kW = 270;
constexpr int kTop = 48;
constexpr int kBtnH = 32, kGap = 4;

const char* const kLabels[TrainingScreen::kCount] = {"FREE FLIGHT", "TARGET PRACTICE", "DOGFIGHT"};

const game::BotMode kTargets[] = {game::BotMode::Straight, game::BotMode::Orbit, game::BotMode::Weave,
                                  game::BotMode::Evade};
const game::BotMode kLevels[] = {game::BotMode::Easy, game::BotMode::Medium, game::BotMode::Hard};
constexpr int kTargetCount = sizeof kTargets / sizeof kTargets[0];
constexpr int kLevelCount = sizeof kLevels / sizeof kLevels[0];

// What each option does, from the bot's behaviour (docs/decisions/0033).
const char* const kFreeText = "Open sky and no enemy. Learn the aircraft, the afterburner and the weapons.";
const char* const kTargetText[kTargetCount] = {
    "An unarmed target in level flight at a constant heading. The easiest one to hit.",
    "An unarmed target in a steady 3.5 g circle. Lead your shots.",
    "An unarmed target flying S-turns and changing altitude.",
    "An unarmed target that beams your missiles, dives and pops flares.",
};
const char* const kLevelText[kLevelCount] = {
    "An armed pilot. Reacts late, pulls little and shoots at what drifts in front of him.",
    "An armed pilot who hunts you: R-77 from range, lead pursuit, notching radar missiles.",
    "An armed pilot at the limit: 13 g breaks, split-S reversals, two missiles in the air.",
};

uint32_t withAlpha(uint32_t c, float a) {
    a = ui::clamp01(a);
    return (c & 0x00FFFFFFu) | (static_cast<uint32_t>(((c >> 24) & 0xFF) * a + 0.5f) << 24);
}

// Option value at the right end of a button, between two arrows that appear
// on the focused row. Ink follows the plate: amber on dark metal, engraved
// dark on the lit one (the same cross-fade ui::button uses for its label).
void drawChoice(int x, int y, int w, int h, const char* text, float focus, int slot) {
    const int bx = x + static_cast<int>(6.0f * focus + 0.5f);
    const int right = bx + w - 10;
    const int left = right - slot;
    const int cx = left + slot / 2;
    const int ty = y + (h - ui::capHeight(ui::Face::Label) + 1) / 2;
    if (focus < 0.99f) ui::text(cx, ty, text, ui::Face::Label, withAlpha(ui::kAccent, 1.0f - focus), gfx::kAlignCenter);
    if (focus > 0.01f)
        ui::text(cx, ty, text, ui::Face::Label, withAlpha(ui::kEngraved, focus), gfx::kAlignCenter,
                 gfx::TextStyle::Engraved);
    if (focus > 0.01f) {
        const uint32_t ink = withAlpha(gfx::abgr(34, 40, 34), focus);
        const int ay = y + (h - ui::icons::kArrowLeft.h) / 2;
        ui::icon(ui::icons::kArrowLeft, left - 2, ay, ink);
        ui::icon(ui::icons::kArrowRight, right - ui::icons::kArrowRight.w + 2, ay, ink);
    }
}

}  // namespace

void TrainingScreen::enter(Context&) {}

void TrainingScreen::launch(Context& ctx) {
    meta::SortieConfig cfg;
    meta::VehicleId v = ctx.profile.lastVehicle;
    if (static_cast<int>(v) >= meta::kVehicleCount || !meta::vehicle(v).available) v = meta::VehicleId::Su35S;
    const meta::VehicleProgress& vp = ctx.profile.vehicles[static_cast<int>(v)];
    cfg.vehicle = v;
    cfg.livery = vp.livery < meta::vehicle(v).liveryCount ? vp.livery : 0;
    cfg.loadout = vp.loadout;
    switch (cursor_) {
        case 0: cfg.mode = meta::SortieMode::FreeFlight; break;
        case 1:
            cfg.mode = meta::SortieMode::TargetPractice;
            cfg.enemy = kTargets[target_];
            break;
        default:
            cfg.mode = meta::SortieMode::Dogfight;
            cfg.enemy = kLevels[dogfight_];
            break;
    }
    ctx.sortie = cfg;
    ctx.sfx(Sfx::Confirm);
    ctx.launch(cfg);
}

void TrainingScreen::update(Context& ctx, const platform::Pad& pad, float) {
    if (pad.hit(PSP_CTRL_UP) && cursor_ > 0) {
        --cursor_;
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_DOWN) && cursor_ < kCount - 1) {
        ++cursor_;
        ctx.sfx(Sfx::Move);
    }
    const int dir = pad.hit(PSP_CTRL_RIGHT) ? 1 : (pad.hit(PSP_CTRL_LEFT) ? -1 : 0);
    if (dir != 0 && cursor_ == 1) {
        target_ = (target_ + dir + kTargetCount) % kTargetCount;
        ctx.sfx(Sfx::Tab);
    } else if (dir != 0 && cursor_ == 2) {
        dogfight_ = (dogfight_ + dir + kLevelCount) % kLevelCount;
        ctx.sfx(Sfx::Tab);
    }
    if (pad.hit(PSP_CTRL_CROSS)) launch(ctx);
    if (pad.hit(PSP_CTRL_CIRCLE)) {
        ctx.sfx(Sfx::Back);
        ctx.pop();
    }
}

void TrainingScreen::draw(Context& ctx) {
    for (int i = 0; i < kCount; ++i) focus_[i] = ui::approach(focus_[i], i == cursor_ ? 1.0f : 0.0f, 16.0f, ctx.dt);

    // The bar first: this screen is static, so its plates sit above the
    // bar's shadow instead of under it.
    const ui::Hint startHint{&ui::icons::kBtnCross, "START"};
    if (cursor_ > 0) {
        const ui::Hint hints[] = {startHint, ui::kHintChange, ui::kHintBack};
        ui::bottomBar(hints, 3);
    } else {
        const ui::Hint hints[] = {startHint, ui::kHintBack};
        ui::bottomBar(hints, 2);
    }
    // Darken the left side under the column: legible over any hangar light.
    gfx::hGradientRect(0, 0, 330, ui::kBarY - 40, gfx::abgr(4, 6, 5, 150), gfx::abgr(4, 6, 5, 0));
    ui::screenTitle("TRAINING");

    for (int i = 0; i < kCount; ++i) {
        const int y = kTop + i * (kBtnH + kGap);
        ui::ButtonLook look;
        look.focus = focus_[i];
        ui::button(kX, y, kW, ui::ButtonKind::Main, kLabels[i], look);
        if (i == 1) drawChoice(kX, y, kW, kBtnH, game::botModeName(kTargets[target_]), focus_[i], 96);
        if (i == 2) drawChoice(kX, y, kW, kBtnH, game::botModeName(kLevels[dogfight_]), focus_[i], 96);
    }

    // What the focused exercise is.
    const char* about = kFreeText;
    if (cursor_ == 1) about = kTargetText[target_];
    else if (cursor_ == 2) about = kLevelText[dogfight_];
    constexpr int ay = kTop + kCount * (kBtnH + kGap) + 4, ah = ui::kBarY - 8 - ay;
    ui::panel(kX, ay, kW, ah);
    ui::paragraph(kX + 14, ay + 10, kW - 28, about, ui::Face::Body, ui::kText, 1);

    // The aircraft that will fly it.
    const meta::Profile& p = ctx.profile;
    meta::VehicleId v = p.lastVehicle;
    if (static_cast<int>(v) >= meta::kVehicleCount || !meta::vehicle(v).available) v = meta::VehicleId::Su35S;
    const meta::VehicleInfo& info = meta::vehicle(v);
    const int livery = p.vehicles[static_cast<int>(v)].livery < info.liveryCount ? p.vehicles[static_cast<int>(v)].livery : 0;
    constexpr int cw = 176, ch = 66;
    const int cx = gfx::kScreenW - ui::kMargin - cw, cy = 10;
    ui::panel(cx, cy, cw, ch);
    ui::text(cx + 14, cy + 11, "YOUR AIRCRAFT", ui::Face::Small, ui::kTextDim);
    ui::icon(info.type == meta::VehicleType::Plane ? ui::icons::kPlane : ui::icons::kHeli, cx + cw - 14 - 20, cy + 9);
    ui::text(cx + 14, cy + 26, info.name, ui::Face::Label, ui::kTextBright);
    ui::text(cx + 14, cy + 46, info.liveryNames[livery], ui::Face::Body, ui::kAccent);
}

}  // namespace menu
