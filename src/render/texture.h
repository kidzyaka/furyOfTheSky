#pragma once

#include <cstdint>

#include "core/file.h"

namespace gfx {

// 8-bit paletted, swizzled, mip-mapped texture (FTX2) from tools/models.
class Texture {
public:
    bool load(const char* path);

    // Uploads the CLUT and binds all mip levels with trilinear filtering.
    // repeat: tile the texture (ocean); alpha: take alpha from the CLUT.
    // trilinear = false: the nearest mip level, bilinear in it - half the
    // texel reads, for big tiling surfaces (terrain) where the level steps
    // don't show.
    void bind(bool repeat = false, bool alpha = false, bool trilinear = true) const;
    // 2D menus (docs/decisions/0035): level 0 only, clamp, alpha from the
    // CLUT; nearest filtering for 1:1 pixel art and text, linear for scaled
    // images. `clut` replaces the file's palette (font styles).
    void bindUi(bool linear, const uint32_t* clut = nullptr) const;

    bool loaded() const { return clut_ != nullptr; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    core::Blob blob_;
    const uint32_t* clut_ = nullptr;
    const uint8_t* levels_[8] = {};
    int width_ = 0, height_ = 0, levelCount_ = 0;
    bool swizzled_ = false;
};

}  // namespace gfx
