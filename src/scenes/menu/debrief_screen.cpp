#include "scenes/menu/debrief_screen.h"

#include <pspctrl.h>

#include <cmath>
#include <cstdio>

#include "render/draw2d.h"
#include "render/gu_context.h"
#include "scenes/menu/briefing_screen.h"
#include "scenes/menu/mission_ui.h"
#include "ui/anim.h"
#include "ui/ui.h"

namespace menu {

namespace {

constexpr int kStampY = 8, kStampH = 50;
constexpr int kPanelY = 84, kPanelH = 112;
constexpr int kStatsX = ui::kMargin, kStatsW = 222;
constexpr int kRankX = kStatsX + kStatsW + 10, kRankW = gfx::kScreenW - ui::kMargin - kRankX;
constexpr int kActionH = 32, kActionY = ui::kBarY - 6 - kActionH, kActionGap = 8;

// Reveal timeline, seconds since the screen opened.
constexpr float kRowStart = 0.05f, kRowStep = 0.07f;
constexpr float kCreditsStart = 0.1f, kCreditsTime = 0.5f;   // the counter has landed after ~0.6 s
constexpr float kBarStart = 0.3f, kBarTime = 0.45f, kPromotedStart = 0.5f;
// The buttons arm after this: a fire button still being mashed from the flight
// (X is the gun) must not restart the sortie. They are dimmed until then.
constexpr float kInputLock = 0.5f;

const ui::Hint kHintChoose{&ui::icons::kDpadLR, "CHOOSE"};
const ui::Hint kHintMissions{&ui::icons::kBtnCircle, "MISSIONS"};

float reveal(float t, float start) { return ui::clamp01((t - start) * 6.0f); }

const char* actionLabel(int kind) {
    switch (kind) {
        case 0: return "RETRY";
        case 1: return "HANGAR";
        case 2: return "MISSIONS";
        case 3: return "NEXT MISSION";
        default: return "REPLAY";
    }
}

// A button of the action row. The label is centred; the plate does not slide
// out sideways here (neighbours stand a few px apart), it only lights up.
void actionButton(int x, int y, int w, ui::ButtonKind kind, const char* label, float focus, float dim) {
    ui::ButtonLook look;
    look.focus = focus;
    look.dim = dim;
    ui::button(x - static_cast<int>(6.0f * focus + 0.5f), y, w, kind, "", look);
    const int cx = x + w / 2 + 1;
    if (kind == ui::ButtonKind::Primary) {
        ui::textInBox(cx, y, kActionH, label, ui::Face::Button,
                      mui::mix(ui::kText, ui::kTextBright, focus), gfx::kAlignCenter);
        return;
    }
    if (focus < 0.99f)
        ui::textInBox(cx, y, kActionH, label, ui::Face::Button, mui::withAlpha(ui::kText, 1.0f - focus), gfx::kAlignCenter);
    if (focus > 0.01f)
        ui::textInBox(cx, y, kActionH, label, ui::Face::Button, mui::withAlpha(ui::kEngraved, focus), gfx::kAlignCenter,
                      gfx::TextStyle::Engraved);
}

}  // namespace

// Which mission was flown (the tour has no sortie: fall back to the selection).
void DebriefScreen::resolve(const Context& ctx, int& loc, int& mis) const {
    const meta::SortieConfig& sortie = ctx.sortie;
    const bool known = sortie.mode == meta::SortieMode::Mission && sortie.location >= 0 && sortie.mission >= 0;
    loc = mui::clampi(known ? sortie.location : ctx.location, 0, meta::locationCount() - 1);
    mis = mui::clampi(known ? sortie.mission : ctx.mission, 0, meta::location(loc).missionCount - 1);
}

// The mission after the one flown, if the location has one and it is open (-1: none).
int DebriefScreen::nextMission(const Context& ctx) const {
    int loc, mis;
    resolve(ctx, loc, mis);
    const int next = mis + 1;
    if (next < meta::location(loc).missionCount && meta::missionUnlocked(ctx.profile, loc, next)) return next;
    return -1;
}

void DebriefScreen::enter(Context& ctx) {
    time_ = 0.0f;
    count_ = 0;
    // The first action is the one most likely wanted, and holds the cursor.
    if (ctx.lastResult.outcome == meta::SortieOutcome::Success) {
        if (nextMission(ctx) >= 0) actions_[count_++] = kNext;
        actions_[count_++] = kReplay;
    } else {
        actions_[count_++] = kRetry;
        actions_[count_++] = kHangar;
    }
    actions_[count_++] = kMissions;
    cursor_ = 0;
    for (int i = 0; i < kMaxActions; ++i) focus_[i] = i == cursor_ ? 1.0f : 0.0f;
}

float DebriefScreen::rankProgress(const Context& ctx) const {
    const int rank = ctx.lastReward.rankAfter;
    if (rank + 1 >= meta::kRankCount) return 1.0f;
    const int lo = meta::rankThreshold(rank), hi = meta::rankThreshold(rank + 1);
    if (hi <= lo) return 1.0f;
    return ui::clamp01(static_cast<float>(meta::rankScore(ctx.profile) - lo) / static_cast<float>(hi - lo));
}

void DebriefScreen::activate(Context& ctx, Action action) {
    int loc, mis;
    resolve(ctx, loc, mis);
    switch (action) {
        case kRetry:
        case kReplay: {
            const meta::SortieConfig& s = ctx.sortie;
            if (s.mode != meta::SortieMode::Mission) {
                ctx.sfx(Sfx::Deny);  // nothing to fly again
                return;
            }
            ctx.sfx(Sfx::Confirm);
            ctx.launch(s);
            break;
        }
        case kHangar:
            // Re-equip: the briefing's AIRCRAFT page; START there flies the same mission.
            ctx.location = loc;
            ctx.mission = mis;
            ctx.briefingPage = BriefingScreen::kPageAircraft;
            ctx.sfx(Sfx::Select);
            ctx.replace(ScreenId::Briefing);
            break;
        case kNext: {
            const int next = nextMission(ctx);
            if (next < 0) {
                ctx.sfx(Sfx::Deny);
                return;
            }
            ctx.location = loc;
            ctx.mission = next;
            ctx.briefingPage = BriefingScreen::kPageMission;
            ctx.sfx(Sfx::Select);
            ctx.replace(ScreenId::Briefing);
            break;
        }
        case kMissions:
            ctx.sfx(Sfx::Back);
            ctx.pop();
            break;
    }
}

void DebriefScreen::update(Context& ctx, const platform::Pad& pad, float) {
    if (time_ < kInputLock) return;
    if (pad.hit(PSP_CTRL_LEFT) && cursor_ > 0) {
        --cursor_;
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_RIGHT) && cursor_ < count_ - 1) {
        ++cursor_;
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_CROSS) || pad.hit(PSP_CTRL_START)) activate(ctx, actions_[cursor_]);
    else if (pad.hit(PSP_CTRL_CIRCLE)) activate(ctx, kMissions);
}

void DebriefScreen::draw(Context& ctx) {
    const float dt = ctx.dt;
    time_ += dt;
    const meta::SortieResult& r = ctx.lastResult;
    const meta::MissionReward& rw = ctx.lastReward;
    const bool won = r.outcome == meta::SortieOutcome::Success;
    // Counters follow the clock (eased, landing exactly on the value), not the frame rate.
    const float credits = static_cast<float>(rw.credits) *
                          ui::easeOutCubic((time_ - kCreditsStart) / kCreditsTime);
    const float bar = rankProgress(ctx) * ui::easeOutCubic((time_ - kBarStart) / kBarTime);

    // --- Which mission, how hard.
    int loc, mis;
    resolve(ctx, loc, mis);
    const int diff = static_cast<int>(ctx.sortie.difficulty);
    const meta::Mission& m = meta::location(loc).missions[mis];

    // --- The stamp.
    const char* verdict = "MISSION ABORTED";
    uint32_t color = ui::kAccent;
    if (r.outcome == meta::SortieOutcome::Success) {
        verdict = "MISSION COMPLETE";
        color = ui::kGood;
    } else if (r.outcome == meta::SortieOutcome::Failed) {
        verdict = "MISSION FAILED";
        color = ui::kBad;
    }
    const float st = ui::clamp01(time_ * 9.0f);
    const int drop = static_cast<int>((1.0f - ui::easeOutCubic(st)) * -10.0f);
    const int sx = ui::kMargin, sw = gfx::kScreenW - 2 * ui::kMargin, sy = kStampY + drop;
    ui::panel(sx, sy, sw, kStampH, ui::Plate::Panel, mui::withAlpha(ui::kWhite, st));
    gfx::fillRect(sx + 6, sy + 6, sw - 12, kStampH - 12, mui::withAlpha(color, 0.10f * st));
    // Stamp border: a thin double line in the verdict's colour.
    const uint32_t line = mui::withAlpha(color, 0.65f * st);
    for (int k = 0; k < 2; ++k) {
        const int o = 7 + k * 3, bx = sx + o, by = sy + o, bw = sw - 2 * o, bh = kStampH - 2 * o;
        gfx::fillRect(bx, by, bw, 1, line);
        gfx::fillRect(bx, by + bh - 1, bw, 1, line);
        gfx::fillRect(bx, by, 1, bh, line);
        gfx::fillRect(bx + bw - 1, by, 1, bh, line);
    }
    ui::text(gfx::kScreenW / 2, sy + (kStampH - ui::capHeight(ui::Face::Huge)) / 2, verdict, ui::Face::Huge,
             mui::withAlpha(color, st), gfx::kAlignCenter);

    char s[80];
    std::snprintf(s, sizeof s, "%s  %s  \xE2\x80\xA2  %s", m.code, m.name,
                  meta::difficultyName(static_cast<meta::Difficulty>(mui::clampi(diff, 0, 2))));
    ui::text(gfx::kScreenW / 2, kStampY + kStampH + 6, s, ui::Face::Body, mui::withAlpha(ui::kTextDim, st),
             gfx::kAlignCenter);

    // --- Figures.
    ui::panel(kStatsX, kPanelY, kStatsW, kPanelH);
    const int x0 = kStatsX + 12, x1 = kStatsX + kStatsW - 12;
    int y = kPanelY + 9;
    int row = 0;
    auto rowAlpha = [&]() { return reveal(time_, kRowStart + kRowStep * static_cast<float>(row++)); };

    float a = rowAlpha();
    char num[24];
    // Missions with ground targets (ships, ADR 0040+) count both on one row.
    bool ground = r.groundKills > 0;
    if (ctx.sortie.mode == meta::SortieMode::Mission && ctx.sortie.location >= 0 && ctx.sortie.mission >= 0) {
        const meta::Mission& m = meta::location(ctx.sortie.location).missions[ctx.sortie.mission];
        const meta::MissionVariant* v = meta::missionVariant(m, meta::vehicle(ctx.sortie.vehicle).type);
        ground = ground || (v && v->groundTargets > 0);
    }
    if (ground) {
        ui::textInBox(x0, y, 24, "KILLS", ui::Face::Label, mui::withAlpha(ui::kText, a));
        std::snprintf(num, sizeof num, "%d", r.groundKills);
        const int gw = ui::textInBox(x1, y, 24, num, ui::Face::Title, mui::withAlpha(ui::kTextBright, a),
                                     gfx::kAlignRight);
        int x = x1 - gw - 4 - ui::icons::kEnemyGround.w;
        ui::icon(ui::icons::kEnemyGround, x, y, mui::withAlpha(ui::kWhite, a));
        std::snprintf(num, sizeof num, "%d", r.airKills);
        x -= 12;
        const int aw = ui::textInBox(x, y, 24, num, ui::Face::Title, mui::withAlpha(ui::kTextBright, a),
                                     gfx::kAlignRight);
        ui::icon(ui::icons::kEnemyAir, x - aw - 4 - ui::icons::kEnemyAir.w, y, mui::withAlpha(ui::kWhite, a));
    } else {
        ui::icon(ui::icons::kEnemyAir, x0, y, mui::withAlpha(ui::kWhite, a));
        ui::textInBox(x0 + 32, y, 24, "AIR KILLS", ui::Face::Label, mui::withAlpha(ui::kText, a));
        std::snprintf(num, sizeof num, "%d", r.airKills);
        ui::textInBox(x1, y, 24, num, ui::Face::Title, mui::withAlpha(ui::kTextBright, a), gfx::kAlignRight);
    }
    y += 25;

    a = rowAlpha();
    char clock[16];
    mui::formatClock(r.seconds, clock, sizeof clock);
    ui::textInBox(x0 + 32, y, 24, "TIME", ui::Face::Label, mui::withAlpha(ui::kText, a));
    ui::textInBox(x1, y, 24, clock, ui::Face::Title, mui::withAlpha(ui::kTextBright, a), gfx::kAlignRight);
    y += 25;

    a = rowAlpha();
    ui::textInBox(x0 + 32, y, 24, "REWARD", ui::Face::Label, mui::withAlpha(ui::kText, a));
    ui::formatCredits(static_cast<int>(credits + 0.5f), num, sizeof num);
    ui::textInBox(x1, y, 24, num, ui::Face::Title, mui::withAlpha(rw.credits > 0 ? ui::kCredits : ui::kTextDim, a),
                  gfx::kAlignRight);
    y += 25;

    a = rowAlpha();
    const bool medal = won;
    ui::icon(mui::medalIcon(diff, medal), x0 + 4, y + 4, mui::withAlpha(ui::kWhite, a));
    const char* tag = !won ? "NO MEDAL" : (rw.firstClear ? "FIRST CLEAR" : "REPLAY");
    ui::textInBox(x0 + 32, y, 24, tag, ui::Face::Label,
                  mui::withAlpha(won && rw.firstClear ? ui::kAccent : ui::kTextDim, a));
    if (won) {
        ui::textInBox(x1, y, 24, meta::difficultyName(static_cast<meta::Difficulty>(mui::clampi(diff, 0, 2))),
                      ui::Face::Label, mui::withAlpha(ui::kText, a), gfx::kAlignRight);
    }

    // --- Rank.
    ui::panel(kRankX, kPanelY, kRankW, kPanelH);
    const int rx0 = kRankX + 12, rx1 = kRankX + kRankW - 12;
    const int after = mui::clampi(rw.rankAfter, 0, meta::kRankCount - 1);
    const int before = mui::clampi(rw.rankBefore, 0, meta::kRankCount - 1);
    const bool promoted = after > before;
    const float ra = reveal(time_, kBarStart - 0.15f);
    ui::text(rx0, kPanelY + 9, "RANK", ui::Face::Small, mui::withAlpha(ui::kTextDim, ra));
    constexpr int kPlateW = ui::icons::kRank0.w;  // the rank plates are 70x24
    int bxp = rx0;
    const int by = kPanelY + 25;
    if (promoted) {
        ui::icon(*ui::icons::kRanks[before], bxp, by, mui::withAlpha(ui::kWhite, 0.6f * ra));
        bxp += kPlateW + 4;
        ui::icon(ui::icons::kArrowRight, bxp, by + (ui::icons::kRank0.h - ui::icons::kArrowRight.h) / 2,
                 mui::withAlpha(ui::kAccent, ra));
        bxp += ui::icons::kArrowRight.w + 4;
    }
    const float pa = reveal(time_, promoted ? kPromotedStart : kBarStart - 0.2f);
    ui::icon(*ui::icons::kRanks[after], bxp, by, mui::withAlpha(ui::kWhite, promoted ? pa : ra));
    ui::text(rx0, kPanelY + 56, meta::rankName(after), ui::Face::Label, mui::withAlpha(ui::kTextBright, ra));
    if (promoted) {
        // A gentle pulse: the line breathes between amber and bright.
        const float pulse = 0.5f + 0.5f * std::sin(time_ * 5.0f);
        ui::text(rx1, kPanelY + 6, "PROMOTED", ui::Face::Label,
                 mui::withAlpha(mui::mix(ui::kAccent, ui::kTextBright, 0.35f * pulse), pa), gfx::kAlignRight);
    }
    ui::progressBar(rx0, kPanelY + 80, kRankW - 24, bar);
    char sc[32];
    if (after + 1 < meta::kRankCount)
        std::snprintf(sc, sizeof sc, "SCORE %d / %d", meta::rankScore(ctx.profile), meta::rankThreshold(after + 1));
    else
        std::snprintf(sc, sizeof sc, "HIGHEST RANK");
    ui::text(rx0, kPanelY + 93, sc, ui::Face::Body, mui::withAlpha(ui::kTextDim, ra));

    // --- What next. The bar first: the buttons are static and sit above its shadow.
    for (int i = 0; i < count_; ++i) focus_[i] = ui::approach(focus_[i], i == cursor_ ? 1.0f : 0.0f, 16.0f, dt);
    const ui::Hint hints[] = {kHintChoose, ui::kHintSelect, kHintMissions};
    ui::bottomBar(hints, 3);
    const float armed = ui::clamp01(time_ / kInputLock);
    const int bw = (gfx::kScreenW - 2 * ui::kMargin - (count_ - 1) * kActionGap) / count_;
    for (int i = 0; i < count_; ++i)
        actionButton(ui::kMargin + i * (bw + kActionGap), kActionY, bw,
                     i == 0 ? ui::ButtonKind::Primary : ui::ButtonKind::Main, actionLabel(actions_[i]), focus_[i],
                     1.0f - armed);
}

}  // namespace menu
