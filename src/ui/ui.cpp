#include "ui/ui.h"

#include <cstdio>

#include "render/draw2d.h"

namespace ui {

namespace {

int face(Face f) { return static_cast<int>(f); }

// Multiplies RGB by k (dimming through the vertex colour).
uint32_t shade(uint32_t abgr, float k) {
    if (k >= 1.0f) return abgr;
    auto ch = [&](int s) { return static_cast<uint32_t>(((abgr >> s) & 0xFF) * k + 0.5f) << s; };
    return (abgr & 0xFF000000u) | ch(16) | ch(8) | ch(0);
}

uint32_t withAlpha(uint32_t abgr, float a) {
    a = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
    return (abgr & 0x00FFFFFFu) | (static_cast<uint32_t>(((abgr >> 24) & 0xFF) * a + 0.5f) << 24);
}

}  // namespace

Resources& res() {
    static Resources r;
    return r;
}

bool Resources::load() {
    if (!font.load("assets/ui/font.fnt", "assets/ui/font.ftx")) {
        error = "assets/ui/font.fnt";
        return false;
    }
    if (!skin.load("assets/ui/skin.ftx")) {
        error = "assets/ui/skin.ftx";
        return false;
    }
    if (!icons.load("assets/ui/icons.ftx")) {
        error = "assets/ui/icons.ftx";
        return false;
    }
    if (!emblems.load("assets/ui/emblems.ftx")) {
        error = "assets/ui/emblems.ftx";
        return false;
    }
    if (!hudFont.load("assets/ui/hud_font.fnt", "assets/ui/hud_font.ftx")) {
        error = "assets/ui/hud_font.fnt";
        return false;
    }
    if (!hudPlates.load("assets/ui/hud_plates.ftx")) {
        error = "assets/ui/hud_plates.ftx";
        return false;
    }
    // Optional art: placeholders are drawn without it.
    schematics.load("assets/ui/schematics.ftx");
    for (int i = 0; i < meta::locationCount() && i < meta::kMaxLocations; ++i) {
        char path[64];
        std::snprintf(path, sizeof path, "assets/ui/loc_%s.ftx", meta::location(i).id);
        locations[i].load(path);
    }
    return true;
}

int text(int x, int y, const char* s, Face f, uint32_t color, gfx::TextAlign align, gfx::TextStyle style) {
    return res().font.draw(face(f), x, y, s, color, align, style);
}

int textWidth(const char* s, Face f) { return res().font.width(face(f), s); }
int capHeight(Face f) { return res().font.capHeight(face(f)); }
int lineHeight(Face f) { return res().font.lineHeight(face(f)); }

int textInBox(int x, int y, int h, const char* s, Face f, uint32_t color, gfx::TextAlign align, gfx::TextStyle style) {
    return text(x, y + (h - capHeight(f) + 1) / 2, s, f, color, align, style);
}

int paragraph(int x, int y, int maxWidth, const char* s, Face f, uint32_t color, int lineGap) {
    return res().font.drawWrapped(face(f), x, y, maxWidth, s, color, lineGap);
}

void icon(const AtlasRect& r, int x, int y, uint32_t tint) {
    gfx::sprite(res().icons, r.x, r.y, r.w, r.h, x, y, r.w, r.h, tint);
}

void iconScaled(const AtlasRect& r, int x, int y, int w, int h, uint32_t tint) {
    gfx::sprite(res().icons, r.x, r.y, r.w, r.h, x, y, w, h, tint, true);
}

void strip(const AtlasRect& r, int x, int y, int w, uint32_t tint) {
    const gfx::Texture& t = res().skin;
    constexpr int kCap = 16;
    if (w <= r.w) {
        // Left part straight from the strip, then its right end: the noise is
        // periodic in x, so the join doesn't show.
        gfx::sprite(t, r.x, r.y, w - kCap, r.h, x, y, w - kCap, r.h, tint);
        gfx::sprite(t, r.x + r.w - kCap, r.y, kCap, r.h, x + w - kCap, y, kCap, r.h, tint);
        return;
    }
    gfx::sprite(t, r.x, r.y, r.w - kCap, r.h, x, y, r.w - kCap, r.h, tint);
    int at = r.w - kCap;
    const int span = r.w - 2 * kCap;
    while (at < w - kCap) {
        const int n = (w - kCap - at) < span ? (w - kCap - at) : span;
        gfx::sprite(t, r.x + kCap, r.y, n, r.h, x + at, y, n, r.h, tint);
        at += n;
    }
    gfx::sprite(t, r.x + r.w - kCap, r.y, kCap, r.h, x + w - kCap, y, kCap, r.h, tint);
}

void nineSlice(const AtlasRect& r, int x, int y, int w, int h, int b, uint32_t tint) {
    const gfx::Texture& t = res().skin;
    const int cw = r.w - 2 * b, ch = r.h - 2 * b;  // source centre
    const int iw = w - 2 * b, ih = h - 2 * b;      // destination centre
    // Corners 1:1, edges and centre stretched (linear: the metal grain blurs
    // gracefully instead of turning blocky).
    gfx::sprite(t, r.x, r.y, b, b, x, y, b, b, tint);
    gfx::sprite(t, r.x + r.w - b, r.y, b, b, x + w - b, y, b, b, tint);
    gfx::sprite(t, r.x, r.y + r.h - b, b, b, x, y + h - b, b, b, tint);
    gfx::sprite(t, r.x + r.w - b, r.y + r.h - b, b, b, x + w - b, y + h - b, b, b, tint);
    gfx::sprite(t, r.x + b, r.y, cw, b, x + b, y, iw, b, tint, true);
    gfx::sprite(t, r.x + b, r.y + r.h - b, cw, b, x + b, y + h - b, iw, b, tint, true);
    gfx::sprite(t, r.x, r.y + b, b, ch, x, y + b, b, ih, tint, true);
    gfx::sprite(t, r.x + r.w - b, r.y + b, b, ch, x + w - b, y + b, b, ih, tint, true);
    gfx::sprite(t, r.x + b, r.y + b, cw, ch, x + b, y + b, iw, ih, tint, true);
}

void panel(int x, int y, int w, int h, Plate plate, uint32_t tint) {
    switch (plate) {
        case Plate::Panel: nineSlice(skin::kPanel, x, y, w, h, 10, tint); break;
        case Plate::Light: nineSlice(skin::kPlate, x, y, w, h, 10, tint); break;
        case Plate::Inset: nineSlice(skin::kInset, x, y, w, h, 6, tint); break;
    }
}

void innerShadow(int x, int y, int w, int h, bool fromBelow, float strength) {
    const uint32_t dark = gfx::abgr(0, 0, 0, static_cast<unsigned>(strength * 255.0f));
    const uint32_t clear = gfx::abgr(0, 0, 0, 0);
    if (fromBelow) gfx::gradientRect(x, y, w, h, clear, dark);
    else gfx::gradientRect(x, y, w, h, dark, clear);
}

void dimScreen(float alpha) {
    if (alpha <= 0.0f) return;
    gfx::fillRect(0, 0, gfx::kScreenW, gfx::kScreenH, gfx::abgr(4, 6, 5, static_cast<unsigned>(alpha * 255.0f)));
}

int buttonHeight(ButtonKind kind) {
    switch (kind) {
        case ButtonKind::Sub:
        case ButtonKind::Row: return 28;
        case ButtonKind::Tab: return 24;
        default: return 32;
    }
}

void button(int x, int y, int w, ButtonKind kind, const char* label, const ButtonLook& look, const char* tag,
            const AtlasRect* rightIcon) {
    const int h = buttonHeight(kind);
    const AtlasRect* base = &skin::kBtn32;
    const AtlasRect* lit = &skin::kBtn32Focus;
    Face f = Face::Button;
    switch (kind) {
        case ButtonKind::Main: break;
        case ButtonKind::Sub: base = &skin::kBtn28Sub, lit = &skin::kBtn28Focus, f = Face::Label; break;
        case ButtonKind::Row: base = &skin::kBtn28, lit = &skin::kBtn28Focus, f = Face::Label; break;
        case ButtonKind::Primary: base = &skin::kBtn32Primary, lit = &skin::kBtn32PrimaryFocus; break;
        case ButtonKind::Tab: base = &skin::kTab24, lit = &skin::kTab24On, f = Face::Body; break;
    }
    const float t = look.disabled ? 0.0f : look.focus;
    const float k = (look.disabled ? 0.58f : 1.0f) * (1.0f - 0.5f * look.dim);
    // The focused plate slides out a few pixels: the cursor is felt, not just seen.
    const int bx = x + static_cast<int>(6.0f * t + 0.5f);
    strip(*base, bx, y, w, shade(kWhite, k));
    if (t > 0.01f) strip(*lit, bx, y, w, withAlpha(kWhite, t));
    // Amber marker on the left edge: bright on the cursor, dull on an open group's parent.
    if (t > 0.01f) gfx::fillRect(bx + 3, y + 4, 3, h - 8, withAlpha(kAccent, t));
    else if (look.open) gfx::fillRect(bx + 3, y + 4, 3, h - 8, shade(kAccentDim, k));

    const int tx = bx + (kind == ButtonKind::Tab ? 10 : 14);
    const bool primary = kind == ButtonKind::Primary;
    const uint32_t idle = look.disabled ? kTextDisabled : shade(kText, k);
    // Primary keeps light text when lit (a red plate); the others cross-fade
    // to engraved dark text on the lit metal.
    if (primary) {
        textInBox(tx, y, h, label, f, t > 0.5f ? kTextBright : idle);
    } else {
        if (t < 0.99f) textInBox(tx, y, h, label, f, withAlpha(idle, 1.0f - t));
        if (t > 0.01f)
            textInBox(tx, y, h, label, f, withAlpha(kEngraved, t), gfx::kAlignLeft, gfx::TextStyle::Engraved);
    }
    int right = bx + w - 10;
    if (rightIcon) {
        icon(*rightIcon, right - rightIcon->w, y + (h - rightIcon->h) / 2, shade(kWhite, k));
        right -= rightIcon->w + 6;
    }
    if (tag) {
        const uint32_t c = t > 0.5f && !primary ? kEngraved : shade(kAccentDim, k);
        textInBox(right, y, h, tag, Face::Small, c, gfx::kAlignRight,
                  t > 0.5f && !primary ? gfx::TextStyle::Plain : gfx::TextStyle::Shadow);
    }
}

const Hint kHintSelect{&icons::kBtnCross, "SELECT"};
const Hint kHintBack{&icons::kBtnCircle, "BACK"};
const Hint kHintTabs{&icons::kBtnR, "PAGE"};
const Hint kHintChange{&icons::kDpadLR, "CHANGE"};

void bottomBar(const Hint* hints, int count, const char* leftText) {
    // Whatever scrolls down slides under the plate and into its shadow.
    gfx::gradientRect(0, kBarY - 46, gfx::kScreenW, 46, gfx::abgr(0, 0, 0, 0), gfx::abgr(2, 3, 2, 236));
    strip(skin::kBar26, 0, kBarY, gfx::kScreenW);
    gfx::fillRect(0, kBarY - 1, gfx::kScreenW, 1, gfx::abgr(0, 0, 0, 160));
    if (leftText) textInBox(kMargin, kBarY, kBarHeight, leftText, Face::Body, kTextDim);
    int x = gfx::kScreenW - kMargin;
    for (int i = count - 1; i >= 0; --i) {
        const int w = textWidth(hints[i].label, Face::Body);
        x -= w;
        textInBox(x, kBarY, kBarHeight, hints[i].label, Face::Body, kText);
        if (hints[i].icon) {
            x -= hints[i].icon->w + 5;
            icon(*hints[i].icon, x, kBarY + (kBarHeight - hints[i].icon->h) / 2);
        }
        x -= 18;
    }
}

void screenTitle(const char* title, const char* subtitle) {
    text(kMargin + 2, 12, title, Face::Title, kTextBright);
    if (subtitle) text(kMargin + 3, 12 + capHeight(Face::Title) + 8, subtitle, Face::Body, kTextDim);
}

void progressBar(int x, int y, int w, float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    strip(skin::kTrack8, x, y, w);
    const int fw = static_cast<int>((w - 2) * t + 0.5f);
    if (fw >= 18) strip(skin::kFill8, x + 1, y, fw);
    else if (fw > 0) gfx::fillRect(x + 1, y + 2, fw, 4, kAccent);
}

void formatThousands(int value, char* out, int size) {
    char digits[16];
    const bool neg = value < 0;
    unsigned v = neg ? static_cast<unsigned>(-(value + 1)) + 1u : static_cast<unsigned>(value);
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v && n < 15);
    int o = 0;
    if (neg && o < size - 1) out[o++] = '-';
    for (int i = n - 1; i >= 0 && o < size - 1; --i) {
        out[o++] = digits[i];
        if (i > 0 && i % 3 == 0 && o < size - 1) out[o++] = ' ';
    }
    out[o] = 0;
}

void formatCredits(int value, char* out, int size) {
    char num[24];
    formatThousands(value, num, sizeof num);
    std::snprintf(out, size, "%s CR", num);
}

void locationImage(int index, int x, int y, int w, int h, uint32_t tint) {
    const gfx::Texture& t = res().locations[index];
    if (t.loaded()) {
        gfx::sprite(t, 0, 0, t.width(), t.height(), x, y, w, h, tint, w != t.width() || h != t.height());
        return;
    }
    gfx::gradientRect(x, y, w, h, shade(gfx::abgr(70, 92, 112), 1.0f), gfx::abgr(28, 36, 40));
}

void emblem(int index, int x, int y, int size, uint32_t tint) {
    if (index < 0 || index >= emblems::kCount) index = 0;
    const AtlasRect& r = emblems::kRects[index];
    gfx::sprite(res().emblems, r.x, r.y, r.w, r.h, x, y, size, size, tint, size != r.w);
}

void schematic(int row, int x, int y, uint32_t tint) {
    const gfx::Texture& t = res().schematics;
    if (!t.loaded()) return;
    gfx::sprite(t, 0, row * 64, 256, 64, x, y, 256, 64, tint);
}

}  // namespace ui
