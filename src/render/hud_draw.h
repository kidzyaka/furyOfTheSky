#pragma once

#include <cstdint>

#include "render/texture.h"

namespace gfx {

// Immediate-mode 2D vector canvas for the HUD and menus (docs/decisions/0018).
// Strokes are collected, then drawn in one GE call as thin quads textured with
// a bright-core/dark-halo profile: anti-aliased, readable over sky and sea,
// with no per-pixel work on the CPU. Text uses a built-in stroke font
// (uppercase Latin, digits, a few symbols), so it scales and rotates freely.
class HudCanvas {
public:
    static constexpr int kMaxSegments = 1100;
    enum Align { kLeft, kCenter, kRight };

    void reset() { n_ = 0; }
    void color(uint32_t abgr) { color_ = abgr; }
    void width(float px) { width_ = px; }

    void line(float x0, float y0, float x1, float y1);
    void dashed(float x0, float y0, float x1, float y1, float dash, float gap);
    void box(float x0, float y0, float x1, float y1);
    void circle(float cx, float cy, float r, int segments = 12);
    // Returns the advance width. Height = cap height in pixels.
    float text(float x, float y, const char* s, float height, Align align = kLeft);
    // Text along a rotated baseline (pitch ladder labels follow the horizon).
    void textRotated(float x, float y, const char* s, float height, float cosA, float sinA, Align align);
    static float textWidth(const char* s, float height);

    // Draws everything collected since reset(). 2D, no depth, alpha blending.
    void flush(const Texture& strokeTexture);

private:
    struct Segment {
        float x0, y0, x1, y1;
        uint32_t color;
        float width;
    };
    void glyph(char c, float x, float y, float unit, float cosA, float sinA);

    Segment seg_[kMaxSegments];
    int n_ = 0;
    uint32_t color_ = 0xFFFFFFFF;
    float width_ = 3.0f;
};

// The one canvas everybody draws into (scenes, menus); ~26 KB, not on the stack.
HudCanvas& hudCanvas();

}  // namespace gfx
