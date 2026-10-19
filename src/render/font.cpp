#include "render/font.h"

#include <pspkernel.h>

#include <cstddef>
#include <cstring>

#include "render/draw2d.h"

namespace gfx {

namespace {

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

constexpr float kShadowOpacity = 0.8f;  // must match SHADOW in tools/textures/ui.py

uint32_t gray(float c, float a) {
    unsigned g = static_cast<unsigned>(c * 255.0f + 0.5f), al = static_cast<unsigned>(a * 255.0f + 0.5f);
    return (al << 24) | (g << 16) | (g << 8) | g;
}

}  // namespace

uint32_t nextCodePoint(const char*& p) {
    const auto* s = reinterpret_cast<const unsigned char*>(p);
    uint32_t c = s[0];
    int extra = 0;
    if (c < 0x80) {
        p += 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0) c &= 0x1F, extra = 1;
    else if ((c & 0xF0) == 0xE0) c &= 0x0F, extra = 2;
    else if ((c & 0xF8) == 0xF0) c &= 0x07, extra = 3;
    else {
        p += 1;
        return '?';
    }
    for (int i = 1; i <= extra; ++i) {
        if ((s[i] & 0xC0) != 0x80) {
            p += i;
            return '?';
        }
        c = (c << 6) | (s[i] & 0x3F);
    }
    p += 1 + extra;
    return c;
}

bool Font::load(const char* fntPath, const char* atlasPath) {
    // The glyph records are read in place: 16 bytes each, laid out exactly
    // like Glyph (docs/formats/ffnt.md), and the blob is 16-byte aligned.
    static_assert(sizeof(Glyph) == 16 && offsetof(Glyph, advance) == 12, "FFNT glyph record");
    if (!atlas_.load(atlasPath)) return false;
    blob_ = core::loadFile(fntPath);
    if (!blob_ || blob_.size < 16 || std::memcmp(blob_.data, "FFNT", 4) != 0 || rd16(blob_.data + 4) != 1)
        return false;
    faceCount_ = rd16(blob_.data + 6);
    const uint32_t total = rd32(blob_.data + 12);
    if (faceCount_ < 1 || faceCount_ > kMaxFaces || 16 + faceCount_ * 16 + total * 16 > blob_.size) return false;
    const auto* glyphs = reinterpret_cast<const Glyph*>(blob_.data + 16 + faceCount_ * 16);
    for (int i = 0; i < faceCount_; ++i) {
        const uint8_t* f = blob_.data + 16 + i * 16;
        Face& face = faces_[i];
        face.size = rd16(f);
        face.lineHeight = rd16(f + 2);
        face.ascent = rd16(f + 4);
        face.capTop = static_cast<int16_t>(rd16(f + 6));
        face.capHeight = rd16(f + 8);
        face.count = rd16(f + 10);
        const uint32_t first = rd32(f + 12);
        if (first + face.count > total) return false;
        face.glyphs = glyphs + first;
        for (int c = 0; c < 96; ++c) face.ascii[c] = -1;
        for (int g = 0; g < face.count; ++g)
            if (face.glyphs[g].cp >= 32 && face.glyphs[g].cp < 128) face.ascii[face.glyphs[g].cp - 32] = g;
    }
    // Index = fill nibble << 4 | shadow nibble (tools/textures/ui.py encode_glyph).
    for (int i = 0; i < 256; ++i) {
        const float fill = (i >> 4) / 15.0f, shadow = (i & 15) / 15.0f * kShadowOpacity;
        const float a = fill + shadow * (1.0f - fill);
        clutComposite_[i] = gray(a > 0.0f ? fill / a : 0.0f, a);
        clutFill_[i] = gray(1.0f, fill);
        clutShadow_[i] = gray(1.0f, (i & 15) / 15.0f);
    }
    sceKernelDcacheWritebackRange(clutComposite_, sizeof clutComposite_);
    sceKernelDcacheWritebackRange(clutFill_, sizeof clutFill_);
    sceKernelDcacheWritebackRange(clutShadow_, sizeof clutShadow_);
    return true;
}

const Font::Glyph* Font::find(const Face& f, uint32_t cp) const {
    if (cp >= 32 && cp < 128) {
        int i = f.ascii[cp - 32];
        return i >= 0 ? &f.glyphs[i] : nullptr;
    }
    int lo = 0, hi = f.count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f.glyphs[mid].cp == cp) return &f.glyphs[mid];
        if (f.glyphs[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

int Font::widthRun(int face, const char* p, const char* end) const {
    if (face < 0 || face >= faceCount_) return 0;
    const Face& f = faces_[face];
    int q = 0;  // quarter pixels
    while (p < end && *p) {
        const Glyph* g = find(f, nextCodePoint(p));
        if (!g) g = find(f, '?');
        if (g) q += g->advance;
    }
    return (q + 2) / 4;
}

int Font::width(int face, const char* utf8) const { return widthRun(face, utf8, utf8 + std::strlen(utf8)); }

int Font::drawRun(int face, int x, int y, const char* begin, const char* end, uint32_t color, TextStyle style,
                  uint32_t lip) const {
    if (face < 0 || face >= faceCount_) return 0;
    const Face& f = faces_[face];
    const int top = y - f.capTop;  // the line's top, where glyph offsets start
    constexpr int kBatch = 96;
    Quad quads[kBatch];
    int pen = x * 4;
    const char* p = begin;
    while (p < end && *p) {
        int n = 0;
        while (p < end && *p && n < kBatch) {
            const Glyph* g = find(f, nextCodePoint(p));
            if (!g) g = find(f, '?');
            if (!g) continue;
            if (g->w > 0) {
                const int gx = (pen + 2) / 4 + g->ox;
                quads[n++] = {static_cast<int16_t>(g->x), static_cast<int16_t>(g->y), static_cast<int16_t>(g->w),
                              static_cast<int16_t>(g->h), static_cast<int16_t>(gx), static_cast<int16_t>(top + g->oy)};
            }
            pen += g->advance;
        }
        switch (style) {
            case TextStyle::Shadow: sprites(atlas_, quads, n, color, clutComposite_); break;
            case TextStyle::Plain: sprites(atlas_, quads, n, color, clutFill_); break;
            case TextStyle::Engraved:
                sprites(atlas_, quads, n, lip, clutShadow_);
                sprites(atlas_, quads, n, color, clutFill_);
                break;
        }
    }
    return (pen + 2) / 4 - x;
}

int Font::draw(int face, int x, int y, const char* utf8, uint32_t color, TextAlign align, TextStyle style,
               uint32_t lip) const {
    const char* end = utf8 + std::strlen(utf8);
    if (align != kAlignLeft) {
        const int w = widthRun(face, utf8, end);
        x -= align == kAlignCenter ? w / 2 : w;
    }
    return drawRun(face, x, y, utf8, end, color, style, lip);
}

int Font::drawWrapped(int face, int x, int y, int maxWidth, const char* utf8, uint32_t color, int lineGap,
                      TextStyle style) const {
    if (face < 0 || face >= faceCount_) return 0;
    const int step = faces_[face].lineHeight + lineGap;
    int lines = 0;
    const char* p = utf8;
    while (*p) {
        // Greedy: extend the line word by word while it fits.
        const char* lineEnd = p;
        const char* q = p;
        while (*q && *q != '\n') {
            const char* wordEnd = q;
            while (*wordEnd == ' ') ++wordEnd;
            while (*wordEnd && *wordEnd != ' ' && *wordEnd != '\n') ++wordEnd;
            if (lineEnd != p && widthRun(face, p, wordEnd) > maxWidth) break;
            lineEnd = q = wordEnd;
        }
        drawRun(face, x, y + lines * step, p, lineEnd, color, style, 0);
        ++lines;
        p = lineEnd;
        while (*p == ' ') ++p;
        if (*p == '\n') ++p;
    }
    return lines * step;
}

}  // namespace gfx
