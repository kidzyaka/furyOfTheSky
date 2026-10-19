#include "scenes/menu/profile_screen.h"

#include <pspctrl.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "render/draw2d.h"
#include "render/gu_context.h"
#include "scenes/menu/mission_ui.h"
#include "ui/anim.h"
#include "ui/ui.h"

namespace menu {

namespace {

using mui::inkText;
using mui::withAlpha;

constexpr uint32_t kInkDark = gfx::abgr(26, 30, 26);
// The profile's plates are a little translucent: the aircraft's nose shows through.
constexpr uint32_t kGlass = gfx::abgr(255, 255, 255, 226);

void plate(int x, int y, int w, int h) { ui::panel(x, y, w, h, ui::Plate::Panel, kGlass); }

// --- Profile layout (480x272). Three bands: identity (left) and the favourite
// aircraft (right), the EMBLEM row across the width, the service record along
// the bottom.
constexpr int kColX = ui::kMargin, kColW = 300;
constexpr int kIdY = 44, kIdH = 88;
constexpr int kEmY = kIdY + kIdH + 6, kEmH = 40;
constexpr int kFavX = kColX + kColW + 8, kFavW = gfx::kScreenW - ui::kMargin - kFavX;
constexpr int kRecH = 56, kRecY = ui::kBarY - 6 - kRecH;

// "123 456" for the stat cells; saturated so a uint32_t cannot wrap negative.
void formatCount(uint32_t v, char* out, int size) {
    ui::formatThousands(v > 999999999u ? 999999999 : static_cast<int>(v), out, size);
}

// A stat: the value big, its caption in up to two small lines, centred in a cell.
void statCell(int cx, int y, const char* value, const char* cap1, const char* cap2) {
    ui::text(cx, y, value, ui::Face::Label, ui::kTextBright, gfx::kAlignCenter);
    ui::text(cx, y + 20, cap1, ui::Face::Small, ui::kTextDim, gfx::kAlignCenter);
    if (cap2[0]) ui::text(cx, y + 33, cap2, ui::Face::Small, ui::kTextDim, gfx::kAlignCenter);
}

}  // namespace

// ---------------------------------------------------------------- Profile

void ProfileScreen::enter(Context&) {}

void ProfileScreen::update(Context& ctx, const platform::Pad& pad, float) {
    if (pad.hit(PSP_CTRL_UP) && cursor_ > 0) {
        --cursor_;
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_DOWN) && cursor_ < 1) {
        ++cursor_;
        ctx.sfx(Sfx::Move);
    }
    if (pad.hit(PSP_CTRL_CROSS)) {
        ctx.sfx(Sfx::Select);
        ctx.push(cursor_ == 0 ? ScreenId::NameEntry : ScreenId::Emblems);
    }
    if (pad.hit(PSP_CTRL_CIRCLE)) {
        ctx.sfx(Sfx::Back);
        ctx.pop();
    }
}

void ProfileScreen::draw(Context& ctx) {
    const float dt = ctx.dt;
    const meta::Profile& p = ctx.profile;
    for (int i = 0; i < 2; ++i) focus_[i] = ui::approach(focus_[i], i == cursor_ ? 1.0f : 0.0f, 16.0f, dt);

    // The bar first: the record along the bottom is static and sits above its shadow.
    if (cursor_ == 0) {
        const ui::Hint hints[] = {{&ui::icons::kBtnCross, "CHANGE CALLSIGN"}, ui::kHintBack};
        ui::bottomBar(hints, 2);
    } else {
        const ui::Hint hints[] = {{&ui::icons::kBtnCross, "EMBLEMS"}, ui::kHintBack};
        ui::bottomBar(hints, 2, "EMBLEMS ARE UNLOCKED BY ACHIEVEMENTS");
    }
    ui::screenTitle("COMBAT PROFILE");

    // --- Identity: emblem, callsign field, rank and its progress.
    plate(kColX, kIdY, kColW, kIdH);
    const int av = p.avatar;
    ui::panel(kColX + 8, kIdY + 14, 60, 60, ui::Plate::Inset);
    ui::emblem(av, kColX + 14, kIdY + 20, 48);

    const float fc = focus_[0];
    const int fx = kColX + 78, fy = kIdY + 8, fw = kColW - 78 - 10, fh = 30;
    ui::panel(fx, fy, fw, fh, ui::Plate::Inset);
    if (fc > 0.01f) ui::panel(fx, fy, fw, fh, ui::Plate::Light, withAlpha(ui::kWhite, fc));
    if (fc > 0.01f) gfx::fillRect(fx + 3, fy + 5, 3, fh - 10, withAlpha(ui::kAccent, fc));
    ui::Face nf = ui::Face::Title;
    if (ui::textWidth(p.nickname, nf) > fw - 24) nf = ui::Face::Label;
    if (ui::textWidth(p.nickname, nf) > fw - 24) nf = ui::Face::Body;
    inkText(fx + 14, fy + (fh - ui::capHeight(nf) + 1) / 2, p.nickname, nf, ui::kTextBright, kInkDark, fc);

    // Rank: the 70x24 plate, its name beside it, the progress bar under both.
    const int score = meta::rankScore(p), rank = meta::rankIndex(p);
    const bool topRank = rank >= meta::kRankCount - 1;
    const int lo = meta::rankThreshold(rank), hi = topRank ? lo : meta::rankThreshold(rank + 1);
    const float t = topRank || hi <= lo ? 1.0f : static_cast<float>(score - lo) / static_cast<float>(hi - lo);
    const int ry = kIdY + 44;
    ui::icon(*ui::icons::kRanks[rank], fx, ry);
    ui::textInBox(fx + ui::icons::kRanks[rank]->w + 8, ry, ui::icons::kRanks[rank]->h, meta::rankName(rank),
                  ui::Face::Body, ui::kAccent);
    char buf[32];
    if (topRank) std::snprintf(buf, sizeof buf, "MAX RANK");
    else std::snprintf(buf, sizeof buf, "%d / %d", score, hi);
    const int by = kIdY + 74, scoreW = ui::textWidth(buf, ui::Face::Small) + 10;
    ui::progressBar(fx, by, fw - scoreW, t);
    ui::textInBox(fx + fw, by - 3, 14, buf, ui::Face::Small, ui::kTextDim, gfx::kAlignRight);

    // --- Favourite aircraft.
    plate(kFavX, kIdY, kFavW, kIdH);
    const meta::VehicleId fv = meta::favoriteVehicle(p);
    const meta::VehicleInfo& info = meta::vehicle(fv);
    ui::text(kFavX + 12, kIdY + 11, "FAVOURITE", ui::Face::Small, ui::kTextDim);
    ui::text(kFavX + kFavW - 12, kIdY + 8, info.name, ui::Face::Label, ui::kTextBright, gfx::kAlignRight);
    const gfx::Texture& art = ui::res().schematics;
    if (art.loaded()) {
        gfx::sprite(art, 0, info.schematicRow * 64, 256, 64, kFavX + (kFavW - 124) / 2, kIdY + 36, 124, 31,
                    ui::kWhite, true);
    } else {
        const ui::AtlasRect& ic = info.type == meta::VehicleType::Plane ? ui::icons::kPlane : ui::icons::kHeli;
        ui::iconScaled(ic, kFavX + kFavW / 2 - 15, kIdY + 38, 30, 30, withAlpha(ui::kWhite, 0.8f));
    }

    // --- The emblem row: the current emblem and how many are unlocked; X opens the gallery.
    const float fe = focus_[1];
    const int ew = gfx::kScreenW - 2 * ui::kMargin;
    plate(kColX, kEmY, ew, kEmH);
    if (fe > 0.01f) ui::panel(kColX, kEmY, ew, kEmH, ui::Plate::Light, withAlpha(ui::kWhite, fe));
    if (fe > 0.01f) gfx::fillRect(kColX + 5, kEmY + 8, 3, kEmH - 16, withAlpha(ui::kAccent, fe));
    ui::emblem(av, kColX + 16, kEmY + (kEmH - 32) / 2, 32);
    inkText(kColX + 58, kEmY + 7, "EMBLEM", ui::Face::Label, ui::kTextBright, kInkDark, fe);
    inkText(kColX + 58, kEmY + 25, meta::emblem(av).name, ui::Face::Small, ui::kAccent, mui::kInkAmber, fe);
    const ui::AtlasRect& arrow = ui::icons::kArrowRight;
    const int ax = kColX + ew - 14 - arrow.w;
    ui::icon(arrow, ax, kEmY + (kEmH - arrow.h) / 2, mui::mix(withAlpha(ui::kWhite, 0.85f), mui::kIconDark, fe));
    char count[24];
    std::snprintf(count, sizeof count, "%d / %d", meta::unlockedEmblemCount(p), meta::emblemCount());
    inkText(ax - 12, kEmY + (kEmH - ui::capHeight(ui::Face::Label) + 1) / 2, count, ui::Face::Label,
            ui::kTextBright, kInkDark, fe, gfx::kAlignRight);
    inkText(ax - 12 - ui::textWidth(count, ui::Face::Label) - 8, kEmY + (kEmH - ui::capHeight(ui::Face::Small) + 1) / 2,
            "UNLOCKED", ui::Face::Small, ui::kTextDim, kInkDark, fe, gfx::kAlignRight);

    // --- Service record.
    plate(ui::kMargin, kRecY, gfx::kScreenW - 2 * ui::kMargin, kRecH);
    struct Cell {
        char value[16];
        const char* cap1;
        const char* cap2;
    } cells[6];
    formatCount(p.airKills, cells[0].value, sizeof cells[0].value);
    cells[0].cap1 = "AIR", cells[0].cap2 = "KILLS";
    formatCount(p.groundKills, cells[1].value, sizeof cells[1].value);
    cells[1].cap1 = "GROUND", cells[1].cap2 = "TARGETS";
    formatCount(p.sorties, cells[2].value, sizeof cells[2].value);
    cells[2].cap1 = "SORTIES", cells[2].cap2 = "";
    formatCount(p.missionsWon, cells[3].value, sizeof cells[3].value);
    cells[3].cap1 = "MISSIONS", cells[3].cap2 = "WON";
    // Multiplayer is not in the game yet: "no data" until a match is on record.
    const bool played = p.mpMatches > 0 || p.mpKills > 0 || p.mpDeaths > 0;
    if (played) {
        const uint32_t kd = p.mpKills * 100u / (p.mpDeaths > 0 ? p.mpDeaths : 1u);
        std::snprintf(cells[4].value, sizeof cells[4].value, "%u.%02u", static_cast<unsigned>(kd / 100u),
                      static_cast<unsigned>(kd % 100u));
    } else {
        std::snprintf(cells[4].value, sizeof cells[4].value, "\xE2\x80\x94");
    }
    if (p.mpMatches > 0) std::snprintf(cells[5].value, sizeof cells[5].value, "%u%%", static_cast<unsigned>(p.mpWins * 100u / p.mpMatches));
    else std::snprintf(cells[5].value, sizeof cells[5].value, "\xE2\x80\x94");
    cells[4].cap1 = "MULTIPLAYER", cells[4].cap2 = "K/D";
    cells[5].cap1 = "MULTIPLAYER", cells[5].cap2 = "WIN RATE";
    const int cw = (gfx::kScreenW - 2 * ui::kMargin - 20) / 6;
    for (int i = 0; i < 6; ++i) {
        const int cx = ui::kMargin + 10 + cw * i + cw / 2;
        if (i > 0) gfx::fillRect(ui::kMargin + 10 + cw * i, kRecY + 10, 1, kRecH - 20, gfx::abgr(0, 0, 0, 110));
        statCell(cx, kRecY + 8, cells[i].value, cells[i].cap1, cells[i].cap2);
    }
}

// ---------------------------------------------------------------- Name entry

namespace {

constexpr char kDelKey = '\b';
constexpr char kOkKey = '\n';

struct Key {
    char label[6];
    char ch;
    uint8_t row, col, span;
};

Key g_keys[NameEntryScreen::kKeys];
bool g_built = false;

// A-Z, 0-9, - _ . in rows of 11, then SPACE (two columns), DEL, OK (two).
void buildKeys() {
    if (g_built) return;
    g_built = true;
    const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.";
    int n = 0;
    for (int i = 0; i < 39; ++i) {
        Key& k = g_keys[n++];
        k.label[0] = chars[i];
        k.label[1] = 0;
        k.ch = chars[i];
        k.row = static_cast<uint8_t>(i / 11);
        k.col = static_cast<uint8_t>(i % 11);
        k.span = 1;
    }
    auto special = [&](const char* label, char ch, int col, int span) {
        Key& k = g_keys[n++];
        std::strcpy(k.label, label);
        k.ch = ch;
        k.row = 3;
        k.col = static_cast<uint8_t>(col);
        k.span = static_cast<uint8_t>(span);
    };
    special("SPACE", ' ', 6, 2);
    special("DEL", kDelKey, 8, 1);
    special("OK", kOkKey, 9, 2);
}

constexpr int kRows = 4;
constexpr int kGridX = 16, kGridY = 92, kPitchX = 41, kPitchY = 32, kKeyW = 38, kKeyH = 28;
constexpr int kFieldY = 46, kFieldH = 36;

}  // namespace

void NameEntryScreen::enter(Context& ctx) {
    buildKeys();
    std::memset(text_, 0, sizeof text_);
    std::memcpy(text_, ctx.profile.nickname, sizeof text_ - 1);
    key_ = 0;
    col_ = 0;
}

int NameEntryScreen::length() const {
    int n = 0;
    for (const char* c = text_; *c; ++c)
        if ((static_cast<unsigned char>(*c) & 0xC0) != 0x80) ++n;
    return n;
}

void NameEntryScreen::backspace() {
    int len = static_cast<int>(std::strlen(text_));
    // Drop a whole UTF-8 character: step back over its continuation bytes.
    while (len > 0) {
        --len;
        if ((static_cast<unsigned char>(text_[len]) & 0xC0) != 0x80) break;
    }
    text_[len] = 0;
}

void NameEntryScreen::moveHorizontal(Context& ctx, int dir) {
    const int row = g_keys[key_].row;
    int i = key_ + dir;
    if (i < 0 || i >= kKeys || g_keys[i].row != row) {
        // Wrap to the other end of the row.
        i = key_;
        while (i - dir >= 0 && i - dir < kKeys && g_keys[i - dir].row == row) i -= dir;
    }
    key_ = i;
    col_ = g_keys[i].col;
    ctx.sfx(Sfx::Move);
}

void NameEntryScreen::moveVertical(Context& ctx, int dir) {
    const int row = (g_keys[key_].row + dir + kRows) % kRows;
    for (int i = 0; i < kKeys; ++i) {
        if (g_keys[i].row == row && col_ >= g_keys[i].col && col_ < g_keys[i].col + g_keys[i].span) {
            key_ = i;
            break;
        }
    }
    ctx.sfx(Sfx::Move);
}

void NameEntryScreen::commit(Context& ctx) {
    int b = 0, e = static_cast<int>(std::strlen(text_));
    while (b < e && text_[b] == ' ') ++b;
    while (e > b && text_[e - 1] == ' ') --e;
    if (e <= b) {
        ctx.sfx(Sfx::Deny);  // a pilot needs a name
        return;
    }
    meta::Profile& p = ctx.profile;
    std::memset(p.nickname, 0, sizeof p.nickname);
    std::memcpy(p.nickname, text_ + b, static_cast<size_t>(e - b));
    ctx.save();
    ctx.sfx(Sfx::Confirm);
    ctx.pop();
}

void NameEntryScreen::press(Context& ctx) {
    const Key& k = g_keys[key_];
    if (k.ch == kOkKey) {
        commit(ctx);
    } else if (k.ch == kDelKey) {
        if (text_[0]) {
            backspace();
            ctx.sfx(Sfx::Tab);
        } else {
            ctx.sfx(Sfx::Deny);
        }
    } else if (length() < kMaxChars && std::strlen(text_) < sizeof text_ - 1) {
        const size_t n = std::strlen(text_);
        text_[n] = k.ch;
        text_[n + 1] = 0;
        ctx.sfx(Sfx::Tab);
    } else {
        ctx.sfx(Sfx::Deny);  // the name is full
    }
}

void NameEntryScreen::update(Context& ctx, const platform::Pad& pad, float) {
    if (pad.hit(PSP_CTRL_LEFT)) moveHorizontal(ctx, -1);
    if (pad.hit(PSP_CTRL_RIGHT)) moveHorizontal(ctx, 1);
    if (pad.hit(PSP_CTRL_UP)) moveVertical(ctx, -1);
    if (pad.hit(PSP_CTRL_DOWN)) moveVertical(ctx, 1);
    if (pad.hit(PSP_CTRL_CROSS)) press(ctx);
    if (pad.hit(PSP_CTRL_SQUARE)) {
        if (text_[0]) {
            backspace();
            ctx.sfx(Sfx::Tab);
        } else {
            ctx.sfx(Sfx::Deny);
        }
    }
    if (pad.hit(PSP_CTRL_START)) commit(ctx);
    if (pad.hit(PSP_CTRL_CIRCLE)) {
        ctx.sfx(Sfx::Back);
        ctx.pop();
    }
}

void NameEntryScreen::draw(Context& ctx) {
    const float dt = ctx.dt;
    for (int i = 0; i < kKeys; ++i) focus_[i] = ui::approach(focus_[i], i == key_ ? 1.0f : 0.0f, 18.0f, dt);

    const ui::Hint hints[] = {{&ui::icons::kBtnCross, "TYPE"},
                              {&ui::icons::kBtnSquare, "DELETE"},
                              {nullptr, "START  SAVE"},
                              ui::kHintBack};
    ui::bottomBar(hints, 4);
    ui::screenTitle("CALLSIGN");

    // The text so far, in a field with a blinking caret.
    const int fx = ui::kMargin, fw = gfx::kScreenW - 2 * ui::kMargin;
    ui::panel(fx, kFieldY, fw, kFieldH, ui::Plate::Inset);
    const int ty = kFieldY + (kFieldH - ui::capHeight(ui::Face::Title) + 1) / 2;
    const int adv = ui::text(fx + 16, ty, text_, ui::Face::Title, ui::kTextBright);
    if (std::fmod(ctx.time, 1.0f) < 0.6f) gfx::fillRect(fx + 16 + adv + 2, ty - 2, 2, ui::capHeight(ui::Face::Title) + 4, ui::kAccent);
    char count[16];
    std::snprintf(count, sizeof count, "%d / %d", length(), kMaxChars);
    ui::text(fx + fw - 14, kFieldY + (kFieldH - ui::capHeight(ui::Face::Body) + 1) / 2, count, ui::Face::Body,
             length() > kMaxChars ? ui::kBad : ui::kTextDim, gfx::kAlignRight);

    for (int i = 0; i < kKeys; ++i) {
        const Key& k = g_keys[i];
        const int x = kGridX + k.col * kPitchX, y = kGridY + k.row * kPitchY;
        const int w = k.span * kPitchX - (kPitchX - kKeyW);
        const float f = focus_[i];
        ui::strip(ui::skin::kBtn28, x, y, w);
        if (f > 0.01f) ui::strip(ui::skin::kBtn28Focus, x, y, w, withAlpha(ui::kWhite, f));
        if (f > 0.01f) gfx::fillRect(x + 3, y + 4, 3, kKeyH - 8, withAlpha(ui::kAccent, f));
        const int cx = x + w / 2 + 1, cy = y + (kKeyH - ui::capHeight(ui::Face::Label) + 1) / 2;
        const uint32_t idle = k.ch == kOkKey ? ui::kAccent : ui::kText;
        inkText(cx, cy, k.label, ui::Face::Label, idle, kInkDark, f, gfx::kAlignCenter);
    }
}

}  // namespace menu
