#pragma once

#include <cstdint>

#include "render/texture.h"

namespace gfx {

class Font;

// Immediate-mode 2D vector canvas for the HUD and menus (docs/decisions/0018).
// Strokes are collected, then drawn in one GE call as thin quads textured with
// a bright-core/dark-halo profile: anti-aliased, readable over sky and sea,
// with no per-pixel work on the CPU. Text uses a built-in stroke font
// (uppercase Latin, digits, a few symbols), so it scales and rotates freely.
class HudCanvas {
public:
    static constexpr int kMaxSegments = 1400;  // + coastlines on the radar scope
    enum Align { kLeft, kCenter, kRight };

    static constexpr int kMaxPanels = 40;

    void reset() { n_ = panels_ = labels_ = sprites_ = 0; }
    void color(uint32_t abgr) { color_ = abgr; }
    void width(float px) { width_ = px; }

    void line(float x0, float y0, float x1, float y1);
    void dashed(float x0, float y0, float x1, float y1, float dash, float gap);
    void box(float x0, float y0, float x1, float y1);
    void circle(float cx, float cy, float r, int segments = 12);
    // Dark translucent backdrops, drawn under all strokes: contrast against a
    // bright sky or sea glare without adding lines (docs/decisions/0031).
    void panel(float x0, float y0, float x1, float y1, uint32_t abgr);
    void disc(float cx, float cy, float r, uint32_t abgr);
    // Returns the advance width. Height = cap height in pixels.
    float text(float x, float y, const char* s, float height, Align align = kLeft);
    // Text along a rotated baseline (pitch ladder labels follow the horizon).
    void textRotated(float x, float y, const char* s, float height, float cosA, float sinA, Align align);
    static float textWidth(const char* s, float height);

    // HUD v3 (docs/decisions/0049): bitmap text of a Font face and sprites of
    // a plates texture, queued and drawn after the strokes (text on top).
    // y = top of the capitals; outline: the face's outline nibble (Shadow
    // style), off for dark text on a light fill.
    static constexpr int kMaxLabels = 64;
    static constexpr int kMaxSprites = 8;
    void label(float x, float y, const char* s, int face, uint32_t color, Align align = kLeft, bool outline = true);
    void sprite(int u, int v, int w, int h, float x, float y, uint32_t tint);

    // Draws everything collected since reset(): backdrops, strokes, then the
    // plates and the bitmap text (when a font / plates texture is given).
    // 2D, no depth, alpha blending.
    void flush(const Texture& strokeTexture, const Font* font = nullptr, const Texture* plates = nullptr);

private:
    void flushVector(const Texture& strokeTexture);
    void flushOverlay(const Font* font, const Texture* plates);
    struct Segment {
        float x0, y0, x1, y1;
        uint32_t color;
        float width;
    };
    void glyph(char c, float x, float y, float unit, float cosA, float sinA);

    struct Panel {
        float x0, y0, x1, y1;  // rect; for a disc x0,y0 = centre, x1 = radius, y1 < 0
        uint32_t color;
    };
    Segment seg_[kMaxSegments];
    int n_ = 0;
    Panel panel_[kMaxPanels];
    int panels_ = 0;
    struct Label {
        int16_t x, y;
        uint8_t face, align;
        bool outline;
        uint32_t color;
        char text[28];
    };
    Label label_[kMaxLabels];
    int labels_ = 0;
    struct Sprite {
        int16_t u, v, w, h, x, y;
        uint32_t tint;
    };
    Sprite sprite_[kMaxSprites];
    int sprites_ = 0;
    uint32_t color_ = 0xFFFFFFFF;
    float width_ = 3.0f;
};

// The one canvas everybody draws into (scenes, menus); ~36 KB, not on the stack.
HudCanvas& hudCanvas();

}  // namespace gfx
