#include "scenes/menu/briefing_screen.h"

#include <pspctrl.h>

#include <cmath>
#include <cstdio>

#include "game/bot.h"
#include "render/draw2d.h"
#include "render/gu_context.h"
#include "scenes/menu/mission_ui.h"
#include "ui/anim.h"
#include "ui/ui.h"

namespace menu {

namespace {

constexpr int kLeft = ui::kMargin;
constexpr int kRight = gfx::kScreenW - ui::kMargin;
constexpr int kTop = 44;           // content starts here (the tab row is above)
constexpr int kTabY = 9, kTabGap = 3;
constexpr int kRowPitch = 32;

const char* const kTabNames[BriefingScreen::kPageCount] = {"MISSION", "AIRCRAFT", "ARMAMENT", "LIVERY"};

// Pylon pairs of the Su-35S, outboard first (meta::LoadoutPair). Mass of the
// stores is the reference full load: R-73 about 105 kg, R-77 about 175 kg.
constexpr int kR73Kg = 105, kR77Kg = 175;
struct PairInfo {
    const char* name;
    bool r73;       // R-73 (else R-77)
    float xMeters;  // station distance from the centreline (docs/assets/su35s.md "Подвеска")
};
constexpr PairInfo kPairs[meta::kPairCount] = {
    {"WINGTIP", true, 7.15f}, {"OUTER WING", true, 5.60f}, {"MIDDLE WING", false, 4.40f},
    {"INNER WING", false, 3.00f}, {"INTAKE", false, 1.15f},
};

const ui::Hint kHintPageL{&ui::icons::kBtnL, ""};
const ui::Hint kHintPageR{&ui::icons::kBtnR, "PAGE"};
const ui::Hint kHintDifficulty{&ui::icons::kDpadLR, "DIFFICULTY"};
const ui::Hint kHintLaunch{&ui::icons::kBtnCross, "LAUNCH"};
const ui::Hint kHintPreset{&ui::icons::kDpadLR, "PRESET"};
const ui::Hint kHintToggle{&ui::icons::kBtnCross, "TOGGLE"};
const ui::Hint kHintChoose{&ui::icons::kDpadUD, "CHOOSE"};

constexpr const char* kBullet = "\xE2\x80\xA2";  // U+2022
constexpr const char* kTimes = "\xC3\x97";       // U+00D7

int presetOf(uint8_t mask) {
    for (int i = 0; i < meta::loadoutPresetCount(); ++i)
        if (meta::loadoutPreset(i).mask == mask) return i;
    return -1;
}

int slideOf(float focus) { return static_cast<int>(6.0f * focus + 0.5f); }  // ui::button's focus slide

// Label text on a Row button that was drawn with an empty label: idle metal
// text cross-fades to engraved dark on the lit plate (the same as ui::button).
void rowText(int x, int y, int h, const char* s, float focus) {
    if (focus < 0.99f) ui::textInBox(x, y, h, s, ui::Face::Label, mui::withAlpha(ui::kText, 1.0f - focus));
    if (focus > 0.01f)
        ui::textInBox(x, y, h, s, ui::Face::Label, mui::withAlpha(ui::kEngraved, focus), gfx::kAlignLeft,
                      gfx::TextStyle::Engraved);
}

}  // namespace

// ---------------------------------------------------------------- state

const meta::Mission& BriefingScreen::mission() const { return meta::location(loc_).missions[mis_]; }

const meta::MissionVariant* BriefingScreen::variant() const {
    return meta::missionVariant(mission(), meta::vehicle(vehicle_).type);
}

bool BriefingScreen::canFly(const Context& ctx, meta::VehicleId v) const {
    const meta::VehicleInfo& info = meta::vehicle(v);
    return info.available && ctx.profile.vehicles[static_cast<int>(v)].owned &&
           meta::missionVariant(mission(), info.type) != nullptr;
}

void BriefingScreen::chooseVehicle(Context& ctx, meta::VehicleId v) {
    vehicle_ = v;
    for (int i = 0; i < eligibleCount_; ++i)
        if (eligible_[i] == v) vehCursor_ = i;
    const meta::VehicleProgress& vp = ctx.profile.vehicles[static_cast<int>(v)];
    loadout_ = static_cast<uint8_t>(vp.loadout & meta::kLoadoutFull);
    liveryCursor_ = vp.livery < meta::vehicle(v).liveryCount ? vp.livery : 0;
    ctx.shownVehicle = v;
    ctx.shownLivery = liveryCursor_;
}

void BriefingScreen::setLoadout(Context& ctx, uint8_t mask) {
    loadout_ = mask;
    ctx.profile.vehicles[static_cast<int>(vehicle_)].loadout = mask;  // the hangar behind shows it at once
    dirty_ = true;
}

void BriefingScreen::setLivery(Context& ctx, int livery) {
    liveryCursor_ = livery;
    ctx.profile.vehicles[static_cast<int>(vehicle_)].livery = static_cast<uint8_t>(livery);
    ctx.shownLivery = livery;
    dirty_ = true;
}

void BriefingScreen::setPage(Context& ctx, int page, Sfx sound) {
    page_ = (page + kPageCount) % kPageCount;
    ctx.sfx(sound);
}

void BriefingScreen::enter(Context& ctx) {
    loc_ = mui::clampi(ctx.location, 0, meta::locationCount() - 1);
    mis_ = mui::clampi(ctx.mission, 0, meta::location(loc_).missionCount - 1);
    ctx.location = loc_;
    ctx.mission = mis_;

    eligibleCount_ = 0;
    for (int v = 0; v < meta::kVehicleCount; ++v) {
        const auto id = static_cast<meta::VehicleId>(v);
        if (meta::missionVariant(mission(), meta::vehicle(id).type)) eligible_[eligibleCount_++] = id;
    }
    // The aircraft flown last if it fits this mission, else the first one that does.
    meta::VehicleId pick = eligibleCount_ > 0 ? eligible_[0] : meta::VehicleId::Su35S;
    if (canFly(ctx, ctx.profile.lastVehicle)) {
        pick = ctx.profile.lastVehicle;
    } else {
        for (int i = 0; i < eligibleCount_; ++i)
            if (canFly(ctx, eligible_[i])) {
                pick = eligible_[i];
                break;
            }
    }
    chooseVehicle(ctx, pick);

    // Entered from the debriefing's HANGAR / NEXT MISSION: open on the page asked for.
    page_ = ctx.briefingPage >= 0 && ctx.briefingPage < kPageCount ? ctx.briefingPage : kPageMission;
    pageF_ = static_cast<float>(page_);
    if (ctx.briefingPage >= 0) {
        // A retry from the debriefing keeps the difficulty it was flown on.
        const meta::SortieConfig& s = ctx.sortie;
        if (s.mode == meta::SortieMode::Mission && s.location == loc_ && s.mission == mis_)
            difficulty_ = static_cast<int>(s.difficulty);
        ctx.briefingPage = -1;
    }
    for (int i = 0; i < kPageCount; ++i) tabFocus_[i] = i == page_ ? 1.0f : 0.0f;
    difficulty_ = mui::clampi(difficulty_, 0, meta::kDifficultyCount - 1);
    for (int d = 0; d < meta::kDifficultyCount; ++d) diffFocus_[d] = d == difficulty_ ? 1.0f : 0.0f;
    for (int i = 0; i < meta::kVehicleCount; ++i) vehFocus_[i] = i == vehCursor_ ? 1.0f : 0.0f;
    armCursor_ = 0;
    for (int i = 0; i < meta::kPairCount; ++i) armFocus_[i] = i == armCursor_ ? 1.0f : 0.0f;
    for (int i = 0; i < 4; ++i) liveryFocus_[i] = i == liveryCursor_ ? 1.0f : 0.0f;
    vehDetail_ = 1.0f;
    dirty_ = false;
}

void BriefingScreen::leave(Context& ctx) {
    if (dirty_) {
        ctx.save();
        dirty_ = false;
    }
    // The hangar goes back to the aircraft of the main menu.
    const meta::VehicleId last = ctx.profile.lastVehicle;
    ctx.shownVehicle = last;
    ctx.shownLivery = ctx.profile.vehicles[static_cast<int>(last)].livery;
    ctx.sfx(Sfx::Back);
    ctx.pop();
}

void BriefingScreen::launch(Context& ctx) {
    const meta::MissionVariant* var = variant();
    if (!canFly(ctx, vehicle_) || !var) {
        // Nothing owned can fly it: show why.
        ctx.sfx(Sfx::Deny);
        page_ = kPageAircraft;
        return;
    }
    meta::SortieConfig s;
    s.mode = meta::SortieMode::Mission;
    s.vehicle = vehicle_;
    s.livery = static_cast<uint8_t>(liveryCursor_);
    s.loadout = loadout_;
    s.difficulty = static_cast<meta::Difficulty>(difficulty_);
    s.enemy = meta::missionEnemy(s.difficulty);
    s.location = loc_;
    s.mission = mis_;
    s.airEnemies = var->airEnemies;
    ctx.sortie = s;

    meta::VehicleProgress& vp = ctx.profile.vehicles[static_cast<int>(vehicle_)];
    vp.livery = s.livery;
    vp.loadout = s.loadout;
    ctx.profile.lastVehicle = vehicle_;
    ctx.profile.lastLocation = static_cast<uint8_t>(loc_);
    ctx.shownVehicle = vehicle_;
    ctx.shownLivery = s.livery;
    ctx.save();
    dirty_ = false;
    ctx.sfx(Sfx::Confirm);
    ctx.launch(ctx.sortie);
}

// ---------------------------------------------------------------- input

void BriefingScreen::update(Context& ctx, const platform::Pad& pad, float) {
    if (pad.hit(PSP_CTRL_CIRCLE)) {
        leave(ctx);
        return;
    }
    if (pad.hit(PSP_CTRL_START)) {
        launch(ctx);
        return;
    }
    if (pad.hit(PSP_CTRL_LTRIGGER)) setPage(ctx, page_ - 1, Sfx::Tab);
    if (pad.hit(PSP_CTRL_RTRIGGER)) setPage(ctx, page_ + 1, Sfx::Tab);
    switch (page_) {
        case kPageMission: updateMission(ctx, pad); break;
        case kPageAircraft: updateAircraft(ctx, pad); break;
        case kPageArmament: updateArmament(ctx, pad); break;
        case kPageLivery: updateLivery(ctx, pad); break;
        default: break;
    }
}

void BriefingScreen::updateMission(Context& ctx, const platform::Pad& pad) {
    if (pad.hit(PSP_CTRL_LEFT) && difficulty_ > 0) {
        --difficulty_;
        ctx.sfx(Sfx::Tab);
    }
    if (pad.hit(PSP_CTRL_RIGHT) && difficulty_ < meta::kDifficultyCount - 1) {
        ++difficulty_;
        ctx.sfx(Sfx::Tab);
    }
    if (pad.hit(PSP_CTRL_CROSS)) launch(ctx);
}

void BriefingScreen::updateAircraft(Context& ctx, const platform::Pad& pad) {
    if (pad.hit(PSP_CTRL_UP) && vehCursor_ > 0) {
        --vehCursor_;
        vehDetail_ = 0.0f;
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_DOWN) && vehCursor_ < eligibleCount_ - 1) {
        ++vehCursor_;
        vehDetail_ = 0.0f;
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_CROSS) && eligibleCount_ > 0) {
        const meta::VehicleId v = eligible_[vehCursor_];
        if (canFly(ctx, v)) {
            if (v != vehicle_) chooseVehicle(ctx, v);
            ctx.sfx(Sfx::Select);
        } else {
            ctx.sfx(Sfx::Deny);
        }
    }
}

void BriefingScreen::updateArmament(Context& ctx, const platform::Pad& pad) {
    if (vehicle_ != meta::VehicleId::Su35S) {  // only the Su-35S has pylon pairs so far
        if (pad.hit(PSP_CTRL_CROSS)) ctx.sfx(Sfx::Deny);
        return;
    }
    if (pad.hit(PSP_CTRL_UP) && armCursor_ > 0) {
        --armCursor_;
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_DOWN) && armCursor_ < meta::kPairCount - 1) {
        ++armCursor_;
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_CROSS)) {
        setLoadout(ctx, static_cast<uint8_t>(loadout_ ^ (1u << armCursor_)));
        ctx.sfx(Sfx::Tab);
    }
    const int count = meta::loadoutPresetCount();
    int dir = 0;
    if (pad.hit(PSP_CTRL_LEFT)) dir = -1;
    if (pad.hit(PSP_CTRL_RIGHT)) dir = +1;
    if (dir != 0) {
        const int cur = presetOf(loadout_);
        const int next = cur < 0 ? (dir > 0 ? 0 : count - 1) : (cur + dir + count) % count;
        setLoadout(ctx, meta::loadoutPreset(next).mask);
        presetKick_[dir < 0 ? 0 : 1] = 1.0f;
        ctx.sfx(Sfx::Tab);
    }
}

void BriefingScreen::updateLivery(Context& ctx, const platform::Pad& pad) {
    const int count = meta::vehicle(vehicle_).liveryCount;
    if (pad.hit(PSP_CTRL_UP) && liveryCursor_ > 0) {
        setLivery(ctx, liveryCursor_ - 1);
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_DOWN) && liveryCursor_ < count - 1) {
        setLivery(ctx, liveryCursor_ + 1);
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_CROSS)) ctx.sfx(Sfx::Select);  // the choice is already made: the cursor is the livery
}

// ---------------------------------------------------------------- drawing

void BriefingScreen::draw(Context& ctx) {
    const float dt = ctx.dt;
    pageF_ = ui::approach(pageF_, static_cast<float>(page_), 14.0f, dt);
    if (std::fabs(pageF_ - static_cast<float>(page_)) < 0.002f) pageF_ = static_cast<float>(page_);
    for (int i = 0; i < kPageCount; ++i) tabFocus_[i] = ui::approach(tabFocus_[i], i == page_ ? 1.0f : 0.0f, 16.0f, dt);
    for (int d = 0; d < meta::kDifficultyCount; ++d)
        diffFocus_[d] = ui::approach(diffFocus_[d], d == difficulty_ ? 1.0f : 0.0f, 16.0f, dt);
    for (int i = 0; i < meta::kVehicleCount; ++i)
        vehFocus_[i] = ui::approach(vehFocus_[i], i == vehCursor_ ? 1.0f : 0.0f, 16.0f, dt);
    for (int i = 0; i < meta::kPairCount; ++i)
        armFocus_[i] = ui::approach(armFocus_[i], i == armCursor_ ? 1.0f : 0.0f, 16.0f, dt);
    for (int i = 0; i < 4; ++i) liveryFocus_[i] = ui::approach(liveryFocus_[i], i == liveryCursor_ ? 1.0f : 0.0f, 16.0f, dt);
    vehDetail_ = ui::approach(vehDetail_, 1.0f, 16.0f, dt);
    for (float& k : presetKick_) k = ui::approach(k, 0.0f, 14.0f, dt);

    // A soft dark band behind the title row: legible over the bright hangar of the livery page too.
    gfx::gradientRect(0, 0, gfx::kScreenW, 44, gfx::abgr(4, 6, 5, 170), gfx::abgr(4, 6, 5, 0));
    ui::screenTitle(mission().name);
    drawTabs();

    // Pages slide sideways and cross-fade. The layer offset is only touched once
    // the screen transition is over (it uses the same layer).
    const bool settled = gfx::layerAlpha() > 0.999f;
    for (int p = 0; p < kPageCount; ++p) {
        const float off = static_cast<float>(p) - pageF_;
        if (std::fabs(off) >= 1.0f) continue;
        if (settled && std::fabs(off) > 0.001f) {
            gfx::setLayer(static_cast<int>(off * 36.0f), 0, 1.0f - std::fabs(off));
            drawPage(ctx, p);
            gfx::resetLayer();
        } else {
            drawPage(ctx, p);
        }
    }

    const char* left = nullptr;
    ui::Hint hints[6];
    int n = 0;
    switch (page_) {
        case kPageMission:
            hints[n++] = kHintDifficulty;
            hints[n++] = kHintLaunch;
            break;
        case kPageAircraft:
            hints[n++] = ui::kHintSelect;
            left = "START: LAUNCH";
            break;
        case kPageArmament:
            hints[n++] = kHintPreset;
            hints[n++] = kHintToggle;
            left = "START: LAUNCH";
            break;
        default:
            hints[n++] = kHintChoose;
            left = "START: LAUNCH";
            break;
    }
    hints[n++] = kHintPageL;
    hints[n++] = kHintPageR;
    hints[n++] = ui::kHintBack;
    ui::bottomBar(hints, n, left);
}

void BriefingScreen::drawTabs() {
    int w[kPageCount], total = 0;
    for (int i = 0; i < kPageCount; ++i) {
        w[i] = ui::textWidth(kTabNames[i], ui::Face::Body) + 22;
        total += w[i] + (i > 0 ? kTabGap : 0);
    }
    int x = kRight - total;
    for (int i = 0; i < kPageCount; ++i) {
        ui::ButtonLook look;
        look.focus = tabFocus_[i];
        // A tab does not slide out like a row: undo the button's own focus offset.
        ui::button(x - slideOf(look.focus), kTabY, w[i], ui::ButtonKind::Tab, kTabNames[i], look);
        x += w[i] + kTabGap;
    }
}

void BriefingScreen::drawPage(Context& ctx, int page) {
    switch (page) {
        case kPageMission: drawMission(ctx); break;
        case kPageAircraft: drawAircraft(ctx); break;
        case kPageArmament: drawArmament(ctx); break;
        case kPageLivery: drawLivery(ctx); break;
        default: break;
    }
}

// ---- MISSION

void BriefingScreen::drawMission(Context& ctx) {
    const meta::Mission& m = mission();
    const meta::MissionVariant* var = variant();
    const int diff = difficulty_;
    constexpr int kPanelX = 308, kPanelW = kRight - 308;

    char s[64];
    std::snprintf(s, sizeof s, "%s  %s  %s", m.code, kBullet, meta::location(loc_).name);
    ui::text(kLeft + 2, kTop, s, ui::Face::Body, ui::kTextDim);
    const int textH = ui::paragraph(kLeft + 2, kTop + 16, kPanelX - kLeft - 16, m.briefing, ui::Face::Body, ui::kText, 1);
    const int oy = mui::clampi(kTop + 16 + textH + 10, 0, 150);  // right under the text, never into the difficulty row
    ui::text(kLeft + 2, oy, "OBJECTIVE", ui::Face::Small, ui::kTextDim);
    ui::text(kLeft + 2, oy + 12, var ? var->objective : "NO VARIANT FOR THIS AIRCRAFT", ui::Face::Label,
             var ? ui::kAccent : ui::kBad);

    // Right: what is out there.
    constexpr int kPanelY = kTop, kPanelH = 122;
    ui::panel(kPanelX, kPanelY, kPanelW, kPanelH);
    const int x0 = kPanelX + 12;
    int y = kPanelY + 10;
    ui::icon(mui::weatherIcon(m.weather), x0, y);
    ui::textInBox(x0 + 30, y, 24, meta::weatherName(m.weather), ui::Face::Label, ui::kTextBright);
    y += 26;
    ui::icon(mui::timeIcon(m.time), x0, y);
    ui::textInBox(x0 + 30, y, 24, meta::timeName(m.time), ui::Face::Label, ui::kTextBright);
    y += 26;
    int x = x0;
    auto enemies = [&](const ui::AtlasRect& icon, int n) {
        if (n <= 0) return;
        char t[8];
        std::snprintf(t, sizeof t, "%s%d", kTimes, n);
        ui::icon(icon, x, y + 1);
        x += 27;
        x += ui::textInBox(x, y, 24, t, ui::Face::Label, ui::kTextBright) + 10;
    };
    if (var) {
        enemies(ui::icons::kEnemyAir, var->airEnemies);
        enemies(ui::icons::kEnemyGround, var->groundTargets);
    }
    y += 26;
    ui::text(x0, y + 6, "ENEMY", ui::Face::Small, ui::kTextDim);
    ui::text(kPanelX + kPanelW - 12, y + 3,
             game::botModeName(meta::missionEnemy(static_cast<meta::Difficulty>(diff))), ui::Face::Label,
             ui::kTextBright, gfx::kAlignRight);

    // Reward for the chosen difficulty: full on the first clear of it, a fifth after.
    const bool first = ((ctx.profile.medals[loc_][mis_] >> diff) & 1) == 0;
    char credits[24];
    ui::formatCredits(meta::missionPayout(ctx.profile, loc_, mis_, static_cast<meta::Difficulty>(diff)), credits,
                      sizeof credits);
    ui::text(kPanelX + 2, kPanelY + kPanelH + 7, first ? "FIRST CLEAR" : "REPLAY", ui::Face::Small,
             first ? ui::kAccent : ui::kTextDim);
    ui::text(kRight - 2, kPanelY + kPanelH + 3, credits, ui::Face::Label, ui::kCredits, gfx::kAlignRight);

    // START MISSION: always the lit red plate (✕ launches on this page).
    ui::ButtonLook go;
    go.focus = 1.0f;
    go.disabled = !canFly(ctx, vehicle_);
    ui::button(kPanelX - (go.disabled ? 0 : slideOf(1.0f)), 186, kPanelW, ui::ButtonKind::Primary, "START MISSION", go);

    // Left bottom: difficulty (left/right), with the medal of each.
    ui::text(kLeft + 2, 178, "DIFFICULTY", ui::Face::Small, ui::kTextDim);
    constexpr int kSegW = 92, kSegGap = 3, kSegY = 190;
    for (int d = 0; d < meta::kDifficultyCount; ++d) {
        const float f = diffFocus_[d];
        const int sx = kLeft + d * (kSegW + kSegGap);
        ui::ButtonLook look;
        look.focus = f;
        ui::button(sx - slideOf(f), kSegY, kSegW, ui::ButtonKind::Row, "", look);
        const bool earned = (ctx.profile.medals[loc_][mis_] >> d) & 1;
        ui::icon(mui::medalIcon(d, earned), sx + 8, kSegY + 6);
        rowText(sx + 30, kSegY, 28, meta::difficultyName(static_cast<meta::Difficulty>(d)), f);
    }
}

// ---- AIRCRAFT

void BriefingScreen::drawAircraft(Context& ctx) {
    constexpr int kListW = 160, kPanelX = 190, kPanelW = kRight - 190, kPanelH = 172;
    for (int i = 0; i < eligibleCount_; ++i) {
        const meta::VehicleId v = eligible_[i];
        const meta::VehicleInfo& info = meta::vehicle(v);
        const bool open = canFly(ctx, v);
        const int y = kTop + i * kRowPitch;
        ui::ButtonLook look;
        look.focus = vehFocus_[i];
        look.disabled = !open;
        const ui::AtlasRect* mark = !open ? &ui::icons::kLock : (v == vehicle_ ? &ui::icons::kCheck : nullptr);
        ui::button(kLeft, y, kListW, ui::ButtonKind::Row, info.name, look, nullptr, mark);
        // Plane / helicopter glyph left of the mark.
        const float t = open ? vehFocus_[i] : 0.0f;
        const ui::AtlasRect& tr = info.type == meta::VehicleType::Plane ? ui::icons::kPlane : ui::icons::kHeli;
        const int bx = kLeft + slideOf(t);
        const uint32_t ink = open ? mui::mix(ui::kText, ui::kEngraved, t) : ui::kTextDisabled;
        ui::icon(tr, bx + kListW - 10 - 16 - 6 - tr.w, y + (28 - tr.h) / 2, ink);
    }

    // What the cursor's aircraft would have to do here.
    if (eligibleCount_ > 0) {
        const meta::VehicleInfo& cv = meta::vehicle(eligible_[vehCursor_]);
        const meta::MissionVariant* var = meta::missionVariant(mission(), cv.type);
        const int y = kTop + eligibleCount_ * kRowPitch + 8;
        const float a = vehDetail_;
        ui::text(kLeft + 2, y, "OBJECTIVE", ui::Face::Small, mui::withAlpha(ui::kTextDim, a));
        if (var) ui::paragraph(kLeft + 2, y + 13, kListW - 4, var->objective, ui::Face::Body, mui::withAlpha(ui::kAccent, a), 1);
    }

    // Right: the cursor's aircraft in detail.
    if (eligibleCount_ == 0) return;
    const meta::VehicleId cv = eligible_[vehCursor_];
    const meta::VehicleInfo& info = meta::vehicle(cv);
    const bool open = canFly(ctx, cv);
    const float a = vehDetail_;
    // The side-view art comes from assets/ui/schematics.ftx; without it the
    // panel closes up instead of showing an empty frame.
    const bool art = ui::res().schematics.loaded();
    const int shift = art ? 0 : -68;
    const int px = kPanelX, py = kTop, x0 = px + 12;
    ui::panel(px, py, kPanelW, kPanelH + shift);
    ui::text(x0, py + 10, info.fullName, ui::Face::Title, mui::withAlpha(ui::kTextBright, a));
    ui::text(x0, py + 35, info.role, ui::Face::Body, mui::withAlpha(ui::kTextDim, a));
    const char* chip = !info.available ? "IN DEVELOPMENT"
                       : (!ctx.profile.vehicles[static_cast<int>(cv)].owned ? "NOT IN HANGAR"
                                                                               : (cv == vehicle_ ? "SELECTED" : "READY"));
    const uint32_t chipColor = !open ? ui::kAccent : (cv == vehicle_ ? ui::kGood : ui::kTextDim);
    // On the name's line, right-aligned: the role text under it keeps its full width.
    ui::text(px + kPanelW - 12, py + 16, chip, ui::Face::Body, mui::withAlpha(chipColor, a), gfx::kAlignRight);

    if (art) {
        // Side view on a dark inset (white line art, tinted).
        ui::panel(px + 10, py + 52, kPanelW - 20, 68, ui::Plate::Inset);
        ui::schematic(info.schematicRow, px + 10 + (kPanelW - 20 - 256) / 2, py + 54,
                      mui::withAlpha(open ? ui::kText : ui::kTextDisabled, a));
    }

    // Four figures with a bar each (scaled against round maxima, not each other).
    struct Stat {
        const char* label;
        char value[16];
        const char* unit;
        float t;
    };
    Stat stats[4] = {{"SPEED", {}, "KM/H", info.maxSpeedKmh / 2500.0f},
                     {"CEILING", {}, "M", info.ceilingM / 20000.0f},
                     {"G LIMIT", {}, "G", info.maxG / 10.0f},
                     {"RANGE", {}, "KM", info.rangeKm / 4000.0f}};
    std::snprintf(stats[0].value, sizeof stats[0].value, "%d", info.maxSpeedKmh);
    ui::formatThousands(info.ceilingM, stats[1].value, sizeof stats[1].value);
    std::snprintf(stats[2].value, sizeof stats[2].value, "%.1f", static_cast<double>(info.maxG));
    std::snprintf(stats[3].value, sizeof stats[3].value, "%d", info.rangeKm);
    constexpr int kColW = 64;
    const int sy = py + 128 + shift;
    for (int i = 0; i < 4; ++i) {
        const int cx = px + 10 + i * kColW;
        ui::text(cx, sy, stats[i].label, ui::Face::Small, mui::withAlpha(ui::kTextDim, a));
        const int vw = ui::text(cx, sy + 13, stats[i].value, ui::Face::Body, mui::withAlpha(ui::kTextBright, a));
        ui::text(cx + vw + 3, sy + 15, stats[i].unit, ui::Face::Small, mui::withAlpha(ui::kTextDim, a));
        ui::progressBar(cx, sy + 29, kColW - 8, stats[i].t * a);
    }
}

// ---- ARMAMENT

void BriefingScreen::drawArmament(Context&) {
    constexpr int kListW = 190, kPanelX = 216, kPanelW = kRight - 216;
    if (vehicle_ != meta::VehicleId::Su35S) {
        ui::panel(kLeft, kTop, 280, 60);
        ui::paragraph(kLeft + 14, kTop + 14, 250, "THIS AIRCRAFT CARRIES A FIXED LOADOUT.", ui::Face::Label,
                      ui::kTextDim, 2);
        return;
    }

    for (int p = 0; p < meta::kPairCount; ++p) {
        const bool on = (loadout_ >> p) & 1;
        char tag[16];
        if (on) std::snprintf(tag, sizeof tag, "%s %s2", kPairs[p].r73 ? "R-73" : "R-77", kTimes);
        ui::ButtonLook look;
        look.focus = armFocus_[p];
        look.dim = on ? 0.0f : 0.6f;
        ui::button(kLeft, kTop + p * kRowPitch, kListW, ui::ButtonKind::Row, kPairs[p].name, look,
                   on ? tag : "EMPTY", on ? &ui::icons::kCheck : nullptr);
    }

    // Right: preset selector, the pylons seen from the front, totals.
    constexpr int kPanelH = 150;
    const int px = kPanelX, py = kTop;
    ui::panel(px, py, kPanelW, kPanelH);
    const int x0 = px + 12, inner = kPanelW - 24;
    ui::text(x0, py + 10, "PRESET", ui::Face::Small, ui::kTextDim);
    const int sy = py + 22;
    ui::panel(x0, sy, inner, 32, ui::Plate::Inset);
    const int cur = presetOf(loadout_);
    const char* name = cur >= 0 ? meta::loadoutPreset(cur).name : "CUSTOM";
    ui::text(px + kPanelW / 2, sy + (32 - ui::capHeight(ui::Face::Button) + 1) / 2, name, ui::Face::Button,
             cur >= 0 ? ui::kTextBright : ui::kAccent, gfx::kAlignCenter);
    ui::icon(ui::icons::kArrowLeft, x0 + 8 - static_cast<int>(presetKick_[0] * 4.0f), sy + 4,
             mui::withAlpha(ui::kWhite, 0.85f));
    ui::icon(ui::icons::kArrowRight, x0 + inner - 8 - ui::icons::kArrowRight.w + static_cast<int>(presetKick_[1] * 4.0f),
             sy + 4, mui::withAlpha(ui::kWhite, 0.85f));

    // Front view: wing line, fuselage, 2 x 5 stations (amber = loaded, the pair under the cursor outlined).
    const int cx = px + kPanelW / 2, ymid = py + 76;
    constexpr float kPxPerMeter = 14.0f;
    gfx::fillRect(cx - 104, ymid, 208, 2, gfx::abgr(96, 104, 98));
    gfx::fillRect(cx - 5, ymid - 12, 10, 26, gfx::abgr(74, 82, 76));
    for (int p = 0; p < meta::kPairCount; ++p) {
        const bool on = (loadout_ >> p) & 1;
        const int h = kPairs[p].r73 ? 14 : 18;
        for (int side = -1; side <= 1; side += 2) {
            const int sx = cx + static_cast<int>(side * kPairs[p].xMeters * kPxPerMeter) - 4;
            const int top = ymid + 3;
            if (p == armCursor_) gfx::fillRect(sx - 1, top - 1, 10, h + 2, ui::kWhite);
            gfx::fillRect(sx, top, 8, h, on ? ui::kAccent : gfx::abgr(36, 42, 38));
        }
    }

    int n73 = 0, n77 = 0;
    for (int p = 0; p < meta::kPairCount; ++p)
        if ((loadout_ >> p) & 1) (kPairs[p].r73 ? n73 : n77) += 2;
    const int mass = n73 * kR73Kg + n77 * kR77Kg;
    char v73[16], v77[16], vm[32], th[16];
    std::snprintf(v73, sizeof v73, "%s%d", kTimes, n73);
    std::snprintf(v77, sizeof v77, "%s%d", kTimes, n77);
    ui::formatThousands(mass, th, sizeof th);
    std::snprintf(vm, sizeof vm, "%s KG", th);
    // Three columns: R-73, R-77 and the mass, right-aligned to the panel edge.
    const int ty = py + 106, xr = x0 + inner;
    ui::text(x0, ty, "R-73 (IR)", ui::Face::Small, ui::kTextDim);
    ui::text(x0 + 66, ty, "R-77 (RADAR)", ui::Face::Small, ui::kTextDim);
    ui::text(xr, ty, "STORES", ui::Face::Small, ui::kTextDim, gfx::kAlignRight);
    ui::text(x0, ty + 13, v73, ui::Face::Title, n73 ? ui::kTextBright : ui::kTextDisabled);
    ui::text(x0 + 66, ty + 13, v77, ui::Face::Title, n77 ? ui::kTextBright : ui::kTextDisabled);
    ui::text(xr, ty + 13, vm, ui::Face::Title, ui::kCredits, gfx::kAlignRight);
    if (n73 + n77 == 0)
        ui::text(px + kPanelW / 2, py + kPanelH + 8, "NO MISSILES: GUN ONLY", ui::Face::Body, ui::kBad, gfx::kAlignCenter);
}

// ---- LIVERY

void BriefingScreen::drawLivery(Context&) {
    const meta::VehicleInfo& info = meta::vehicle(vehicle_);
    const int n = info.liveryCount < 4 ? info.liveryCount : 4;
    constexpr int kPanelW = 200;
    const int h = 40 + n * kRowPitch + 4;
    ui::panel(kLeft, kTop, kPanelW, h);
    ui::text(kLeft + 14, kTop + 11, info.name, ui::Face::Label, ui::kTextBright);
    ui::text(kLeft + kPanelW - 14, kTop + 14, "LIVERY", ui::Face::Small, ui::kTextDim, gfx::kAlignRight);
    for (int i = 0; i < n; ++i) {
        ui::ButtonLook look;
        look.focus = liveryFocus_[i];
        ui::button(kLeft + 10, kTop + 34 + i * kRowPitch, kPanelW - 28, ui::ButtonKind::Row, info.liveryNames[i], look,
                   nullptr, i == liveryCursor_ ? &ui::icons::kCheck : nullptr);
    }
}

}  // namespace menu
