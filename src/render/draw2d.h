#pragma once

#include <cstdint>

#include "render/texture.h"

namespace gfx {

// Immediate-mode 2D for the menus (docs/decisions/0035): GE through mode
// (GU_TRANSFORM_2D), integer pixel coordinates, 16-bit vertices, sprites
// split into 64-pixel strips (the GE texture cache likes narrow spans).
// Everything goes straight into the frame's display list.

// Sets 2D state (no depth, fog, culling or lighting; alpha blending) and
// forgets which texture is bound. end2D restores the 3D defaults.
void begin2D();
void end2D();

// Layer transform for a whole screen: offset in pixels and an opacity
// multiplier (screen transitions slide and fade everything drawn after it).
void setLayer(int dx, int dy, float alpha);
void resetLayer();
float layerAlpha();

void fillRect(int x, int y, int w, int h, uint32_t abgr);
void gradientRect(int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void hGradientRect(int x, int y, int w, int h, uint32_t left, uint32_t right);

// Texture region (u, v, uw, vh texels) to a screen rect; tint modulates.
// linear: bilinear filtering for scaled images; 1:1 art uses nearest.
void sprite(const Texture& tex, int u, int v, int uw, int vh, int x, int y, int w, int h, uint32_t tint,
            bool linear = false, const uint32_t* clut = nullptr);

// Many small 1:1 sprites from one texture in one GE call (glyphs, icons).
struct Quad {
    int16_t u, v, w, h;  // texels = pixels
    int16_t x, y;        // screen position (layer offset added)
};
void sprites(const Texture& tex, const Quad* quads, int count, uint32_t tint, const uint32_t* clut = nullptr);

// Scissor rectangle (exclusive right/bottom). Nesting is not supported:
// clip() replaces, unclip() returns to the full screen.
void clip(int x0, int y0, int x1, int y1);
void unclip();

// Multiplies the alpha of an ABGR colour by the layer opacity and `a`.
uint32_t fade(uint32_t abgr, float a);

}  // namespace gfx
