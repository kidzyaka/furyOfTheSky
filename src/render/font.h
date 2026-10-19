#pragma once

#include <cstdint>

#include "core/file.h"
#include "render/texture.h"

namespace gfx {

// How a string is inked (docs/decisions/0035). The atlas stores fill and a
// 1 px drop shadow in the two nibbles of each texel; a style is just which
// CLUT the GE sees.
enum class TextStyle : uint8_t {
    Shadow,    // fill in the colour over a black drop shadow (default, any background)
    Plain,     // fill only
    Engraved,  // dark fill with a light lip below: cut into a light metal plate
};

enum TextAlign : uint8_t { kAlignLeft, kAlignCenter, kAlignRight };

// Bitmap font from tools/textures/ui.py: FFNT metrics (docs/formats/ffnt.md)
// + a T8 atlas. Several pixel sizes ("faces") share one atlas. UTF-8 text;
// missing characters draw as '?'.
class Font {
public:
    bool load(const char* fntPath, const char* atlasPath);

    // Draws at (x, y) where y is the top of the capital letters - so a label
    // is centred in a box of height h at y = top + (h - capHeight) / 2.
    // Returns the advance width in pixels.
    int draw(int face, int x, int y, const char* utf8, uint32_t color, TextAlign align = kAlignLeft,
             TextStyle style = TextStyle::Shadow, uint32_t lip = 0xC0D6E0DCu) const;
    int width(int face, const char* utf8) const;
    int capHeight(int face) const { return faces_[face].capHeight; }
    int lineHeight(int face) const { return faces_[face].lineHeight; }

    // Word-wrapped paragraph ('\n' breaks too) inside maxWidth; returns the
    // height used. lineGap is added to the face's line height.
    int drawWrapped(int face, int x, int y, int maxWidth, const char* utf8, uint32_t color, int lineGap = 0,
                    TextStyle style = TextStyle::Shadow) const;

private:
    struct Glyph {
        uint32_t cp;
        uint16_t x, y;
        uint8_t w, h;
        int8_t ox, oy;
        uint8_t advance;  // quarter pixels
    };
    struct Face {
        int size, lineHeight, ascent, capTop, capHeight;
        const Glyph* glyphs;
        int count;
        int16_t ascii[96];  // glyph index for ' '..DEL, -1 if absent
    };
    static constexpr int kMaxFaces = 8;

    const Glyph* find(const Face& f, uint32_t cp) const;
    int drawRun(int face, int x, int y, const char* begin, const char* end, uint32_t color, TextStyle style,
                uint32_t lip) const;
    int widthRun(int face, const char* begin, const char* end) const;

    core::Blob blob_;
    Texture atlas_;
    Face faces_[kMaxFaces];
    int faceCount_ = 0;
    // Runtime CLUTs built from the nibbles; the GE reads them from RAM, so
    // 16-byte aligned and written back from the data cache.
    alignas(16) uint32_t clutComposite_[256];
    alignas(16) uint32_t clutFill_[256];
    alignas(16) uint32_t clutShadow_[256];
};

// Next code point of a UTF-8 string (advances p); malformed bytes -> '?'.
uint32_t nextCodePoint(const char*& p);

}  // namespace gfx
