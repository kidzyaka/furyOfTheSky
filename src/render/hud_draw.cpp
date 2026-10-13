#include "render/hud_draw.h"

#include <pspgu.h>

#include <cmath>
#include <cstring>

#include "math/vec3.h"
#include "render/gu_context.h"

namespace gfx {
namespace {

// Stroke font on a 9 x 13 grid (x '0'..'8', y 'a'..'m', top = 'a'); a glyph
// is polylines separated by spaces. Angular "HUD" shapes: no curves to tessellate.
const char* glyphStrokes(char c) {
    switch (c) {
        case '0': case 'O': return "2a6a8c8k6m2m0k0c2a";
        case '1': return "2c4a4m 2m6m";
        case '2': return "0c2a6a8c8e0m8m";
        case '3': return "0c2a6a8c8e6g8i8k6m2m0k 2g6g";
        case '4': return "6m6a0i8i";
        case '5': return "8a0a0g6g8i8k6m0m";
        case '6': return "8c6a2a0c0k2m6m8k8i6g0g";
        case '7': return "0a8a2m";
        case '8': return "2g0e0c2a6a8c8e6g2g0i0k2m6m8k8i6g";
        case '9': return "8g2g0e0c2a6a8c8k6m2m0k";
        case 'A': return "0m0e4a8e8m 0i8i";
        case 'B': return "0m0a6a8c8e6g0g 6g8i8k6m0m";
        case 'C': return "8c6a2a0c0k2m6m8k";
        case 'D': return "0a6a8c8k6m0m0a";
        case 'E': return "8a0a0m8m 0g6g";
        case 'F': return "8a0a0m 0g6g";
        case 'G': return "8c6a2a0c0k2m6m8k8g4g";
        case 'H': return "0a0m 8a8m 0g8g";
        case 'I': return "2a6a 4a4m 2m6m";
        case 'J': return "8a8k6m2m0k";
        case 'K': return "0a0m 8a0i 2g8m";
        case 'L': return "0a0m8m";
        case 'M': return "0m0a4g8a8m";
        case 'N': return "0m0a8m8a";
        case 'P': return "0m0a6a8c8e6g0g";
        case 'Q': return "2a6a8c8k6m2m0k0c2a 4i8m";
        case 'R': return "0m0a6a8c8e6g0g 4g8m";
        case 'S': return "8c6a2a0c0e2g6g8i8k6m2m0k";
        case 'T': return "0a8a 4a4m";
        case 'U': return "0a0k2m6m8k8a";
        case 'V': return "0a4m8a";
        case 'W': return "0a2m4g6m8a";
        case 'X': return "0a8m 8a0m";
        case 'Y': return "0a4g8a 4g4m";
        case 'Z': return "0a8a0m8m";
        case '.': return "3m5m";
        case ',': return "4l3m";
        case '-': return "2g6g";
        case '+': return "2g6g 4e4i";
        case '/': return "0m8a";
        case ':': return "4d4e 4i4j";
        case '\'': return "6a8a8c6c6a";  // degree sign
        case '<': return "8a0g8m";
        case '>': return "0a8g0m";
        case '!': return "4a4i 4l4m";
        case '%': return "0m8a 0a2a2c0c0a 6k8k8m6m6k";
        case '[': return "6a2a2m6m";
        case ']': return "2a6a6m2m";
        case '=': return "2e6e 2i6i";
        case '^': return "0e4a8e";
        default: return "";
    }
}

constexpr float kGridH = 12.0f;  // glyph height in grid units
constexpr float kAdvance = 11.5f;

}  // namespace

HudCanvas& hudCanvas() {
    static HudCanvas canvas;
    return canvas;
}

void HudCanvas::line(float x0, float y0, float x1, float y1) {
    if (n_ >= kMaxSegments) return;
    seg_[n_++] = {x0, y0, x1, y1, color_, width_};
}

void HudCanvas::dashed(float x0, float y0, float x1, float y1, float dash, float gap) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;
    dx /= len;
    dy /= len;
    for (float t = 0.0f; t < len; t += dash + gap) {
        float e = t + dash < len ? t + dash : len;
        line(x0 + dx * t, y0 + dy * t, x0 + dx * e, y0 + dy * e);
    }
}

void HudCanvas::box(float x0, float y0, float x1, float y1) {
    line(x0, y0, x1, y0);
    line(x1, y0, x1, y1);
    line(x1, y1, x0, y1);
    line(x0, y1, x0, y0);
}

void HudCanvas::circle(float cx, float cy, float r, int segments) {
    float px = cx + r, py = cy;
    for (int i = 1; i <= segments; ++i) {
        float a = i * (2.0f * math::kPi / segments);
        float x = cx + r * std::cos(a), y = cy + r * std::sin(a);
        line(px, py, x, y);
        px = x;
        py = y;
    }
}

float HudCanvas::textWidth(const char* s, float height) {
    size_t n = std::strlen(s);
    if (n == 0) return 0.0f;
    float unit = height / kGridH;
    return ((n - 1) * kAdvance + 8.0f) * unit;
}

void HudCanvas::glyph(char c, float x, float y, float unit, float ca, float sa) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    const char* p = glyphStrokes(c);
    float px = 0.0f, py = 0.0f;
    bool pen = false;
    for (; *p; ++p) {
        if (*p == ' ') {
            pen = false;
            continue;
        }
        float gx = (p[0] - '0') * unit, gy = (p[1] - 'a') * unit;
        ++p;
        float qx = x + gx * ca - gy * sa, qy = y + gx * sa + gy * ca;
        if (pen) line(px, py, qx, qy);
        px = qx;
        py = qy;
        pen = true;
    }
}

float HudCanvas::text(float x, float y, const char* s, float height, Align align) {
    float w = textWidth(s, height);
    if (align == kCenter) x -= w * 0.5f;
    if (align == kRight) x -= w;
    float unit = height / kGridH;
    for (; *s; ++s, x += kAdvance * unit) glyph(*s, x, y, unit, 1.0f, 0.0f);
    return w;
}

void HudCanvas::textRotated(float x, float y, const char* s, float height, float ca, float sa, Align align) {
    float w = textWidth(s, height);
    float shift = align == kCenter ? -0.5f * w : (align == kRight ? -w : 0.0f);
    float unit = height / kGridH;
    // (x, y) is the left/centre/right point of the vertical middle of the text.
    float ox = x + shift * ca + (0.5f * height) * sa, oy = y + shift * sa - (0.5f * height) * ca;
    for (; *s; ++s) {
        glyph(*s, ox, oy, unit, ca, sa);
        ox += kAdvance * unit * ca;
        oy += kAdvance * unit * sa;
    }
}

void HudCanvas::flush(const Texture& strokeTexture) {
    if (n_ == 0) return;
    struct V {
        float u, v;
        uint32_t color;
        float x, y, z;
    };
    auto* v = static_cast<V*>(sceGuGetMemory(n_ * 6 * sizeof(V)));
    V* w = v;
    for (int i = 0; i < n_; ++i) {
        const Segment& s = seg_[i];
        float dx = s.x1 - s.x0, dy = s.y1 - s.y0;
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-3f) {
            dx = 1.0f;
            dy = 0.0f;
        } else {
            dx /= len;
            dy /= len;
        }
        float h = 0.5f * s.width;
        // Normal across the stroke and a short extension so joints close up.
        float nx = -dy * h, ny = dx * h;
        float ex = dx * h * 0.6f, ey = dy * h * 0.6f;
        float ax = s.x0 - ex, ay = s.y0 - ey, bx = s.x1 + ex, by = s.y1 + ey;
        // Through mode (GU_TRANSFORM_2D) takes UVs in texels, not 0..1: the
        // stroke texture is 16 x 16 (docs/psp/gotchas.md #22).
        V p0 = {0.0f, 0.0f, s.color, ax + nx, ay + ny, 0.0f};
        V p1 = {0.0f, 16.0f, s.color, bx + nx, by + ny, 0.0f};
        V p2 = {16.0f, 16.0f, s.color, bx - nx, by - ny, 0.0f};
        V p3 = {16.0f, 0.0f, s.color, ax - nx, ay - ny, 0.0f};
        w[0] = p0;
        w[1] = p1;
        w[2] = p2;
        w[3] = p0;
        w[4] = p2;
        w[5] = p3;
        w += 6;
    }
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_LIGHTING);
    strokeTexture.bind(false, true);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, n_ * 6,
                   nullptr, v);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_CULL_FACE);
    sceGuEnable(GU_DEPTH_TEST);
    n_ = 0;
}

}  // namespace gfx
