#include "render/draw2d.h"

#include <pspgu.h>

#include "render/gu_context.h"

namespace gfx {
namespace {

struct ColorVertex {
    uint32_t color;
    int16_t x, y, z;
};
static_assert(sizeof(ColorVertex) == 12, "GU_COLOR_8888 | GU_VERTEX_16BIT");

struct TexVertex {
    uint16_t u, v;
    uint32_t color;
    int16_t x, y, z;
};
static_assert(sizeof(TexVertex) == 16, "GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT");

int g_dx = 0, g_dy = 0;
float g_alpha = 1.0f;

// What the GE has bound, so runs of sprites from one atlas cost one bind.
const Texture* g_tex = nullptr;
const uint32_t* g_clut = nullptr;
bool g_linear = false;
bool g_textured = false;

void texturing(bool on) {
    if (on == g_textured) return;
    if (on) sceGuEnable(GU_TEXTURE_2D);
    else sceGuDisable(GU_TEXTURE_2D);
    g_textured = on;
}

}  // namespace

void begin2D() {
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);  // GU_TRUE = writes disabled
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_LIGHTING);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuShadeModel(GU_SMOOTH);
    sceGuDisable(GU_TEXTURE_2D);
    g_textured = false;
    g_tex = nullptr;
    g_clut = nullptr;
    resetLayer();
}

void end2D() {
    unclip();
    sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_CULL_FACE);
    sceGuEnable(GU_TEXTURE_2D);
    resetLayer();
}

void setLayer(int dx, int dy, float alpha) {
    g_dx = dx;
    g_dy = dy;
    g_alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
}

void resetLayer() { setLayer(0, 0, 1.0f); }
float layerAlpha() { return g_alpha; }

uint32_t fade(uint32_t abgr, float a) {
    float k = g_alpha * a;
    if (k >= 1.0f) return abgr;
    if (k <= 0.0f) return abgr & 0x00FFFFFFu;
    unsigned alpha = static_cast<unsigned>((abgr >> 24) * k + 0.5f);
    return (abgr & 0x00FFFFFFu) | (alpha << 24);
}

void fillRect(int x, int y, int w, int h, uint32_t abgr) { gradientRect(x, y, w, h, abgr, abgr); }

void gradientRect(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
    if (w <= 0 || h <= 0) return;
    top = fade(top, 1.0f);
    bottom = fade(bottom, 1.0f);
    if (((top | bottom) >> 24) == 0) return;
    texturing(false);
    x += g_dx;
    y += g_dy;
    auto* v = static_cast<ColorVertex*>(sceGuGetMemory(4 * sizeof(ColorVertex)));
    v[0] = {top, static_cast<int16_t>(x), static_cast<int16_t>(y), 0};
    v[1] = {top, static_cast<int16_t>(x + w), static_cast<int16_t>(y), 0};
    v[2] = {bottom, static_cast<int16_t>(x), static_cast<int16_t>(y + h), 0};
    v[3] = {bottom, static_cast<int16_t>(x + w), static_cast<int16_t>(y + h), 0};
    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 4, nullptr, v);
}

void hGradientRect(int x, int y, int w, int h, uint32_t left, uint32_t right) {
    if (w <= 0 || h <= 0) return;
    left = fade(left, 1.0f);
    right = fade(right, 1.0f);
    if (((left | right) >> 24) == 0) return;
    texturing(false);
    x += g_dx;
    y += g_dy;
    auto* v = static_cast<ColorVertex*>(sceGuGetMemory(4 * sizeof(ColorVertex)));
    v[0] = {left, static_cast<int16_t>(x), static_cast<int16_t>(y), 0};
    v[1] = {right, static_cast<int16_t>(x + w), static_cast<int16_t>(y), 0};
    v[2] = {left, static_cast<int16_t>(x), static_cast<int16_t>(y + h), 0};
    v[3] = {right, static_cast<int16_t>(x + w), static_cast<int16_t>(y + h), 0};
    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 4, nullptr, v);
}

void sprite(const Texture& tex, int u, int v, int uw, int vh, int x, int y, int w, int h, uint32_t tint, bool linear,
            const uint32_t* clut) {
    if (w <= 0 || h <= 0 || uw <= 0 || vh <= 0 || !tex.loaded()) return;
    tint = fade(tint, 1.0f);
    if ((tint >> 24) == 0) return;
    texturing(true);
    if (g_tex != &tex || g_clut != clut || g_linear != linear) {
        tex.bindUi(linear, clut);
        g_tex = &tex;
        g_clut = clut;
        g_linear = linear;
    }
    x += g_dx;
    y += g_dy;
    // Strips of <= 64 screen pixels: wide textured sprites thrash the GE's
    // texture cache (the classic PSP 2D advice; not measured here yet).
    constexpr int kStrip = 64;
    const int strips = (w + kStrip - 1) / kStrip;
    auto* p = static_cast<TexVertex*>(sceGuGetMemory(strips * 2 * sizeof(TexVertex)));
    TexVertex* q = p;
    for (int i = 0; i < strips; ++i) {
        const int sx0 = i * kStrip, sx1 = (i + 1) * kStrip < w ? (i + 1) * kStrip : w;
        // Texel span of this strip; exact when w == uw, proportional otherwise.
        const int u0 = u + sx0 * uw / w, u1 = u + sx1 * uw / w;
        *q++ = {static_cast<uint16_t>(u0), static_cast<uint16_t>(v), tint, static_cast<int16_t>(x + sx0),
                static_cast<int16_t>(y), 0};
        *q++ = {static_cast<uint16_t>(u1), static_cast<uint16_t>(v + vh), tint, static_cast<int16_t>(x + sx1),
                static_cast<int16_t>(y + h), 0};
    }
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, strips * 2,
                   nullptr, p);
}

void sprites(const Texture& tex, const Quad* quads, int count, uint32_t tint, const uint32_t* clut) {
    if (count <= 0 || !tex.loaded()) return;
    tint = fade(tint, 1.0f);
    if ((tint >> 24) == 0) return;
    texturing(true);
    if (g_tex != &tex || g_clut != clut || g_linear) {
        tex.bindUi(false, clut);
        g_tex = &tex;
        g_clut = clut;
        g_linear = false;
    }
    auto* p = static_cast<TexVertex*>(sceGuGetMemory(count * 2 * sizeof(TexVertex)));
    for (int i = 0; i < count; ++i) {
        const Quad& q = quads[i];
        const int x = q.x + g_dx, y = q.y + g_dy;
        p[2 * i] = {static_cast<uint16_t>(q.u), static_cast<uint16_t>(q.v), tint, static_cast<int16_t>(x),
                    static_cast<int16_t>(y), 0};
        p[2 * i + 1] = {static_cast<uint16_t>(q.u + q.w), static_cast<uint16_t>(q.v + q.h), tint,
                        static_cast<int16_t>(x + q.w), static_cast<int16_t>(y + q.h), 0};
    }
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, count * 2,
                   nullptr, p);
}

void clip(int x0, int y0, int x1, int y1) {
    x0 += g_dx;
    x1 += g_dx;
    y0 += g_dy;
    y1 += g_dy;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > kScreenW) x1 = kScreenW;
    if (y1 > kScreenH) y1 = kScreenH;
    if (x1 <= x0 || y1 <= y0) x1 = x0 + 1, y1 = y0 + 1;
    // Width and height: this pspsdk stores x + w - 1 as the last column
    // (checked in the disassembly; old SDKs took the edge, docs/psp/gotchas.md #29).
    sceGuScissor(x0, y0, x1 - x0, y1 - y0);
}

void unclip() { sceGuScissor(0, 0, kScreenW, kScreenH); }

}  // namespace gfx
