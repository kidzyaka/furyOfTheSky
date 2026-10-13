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
    void bind(bool repeat = false, bool alpha = false) const;

private:
    core::Blob blob_;
    const uint32_t* clut_ = nullptr;
    const uint8_t* levels_[8] = {};
    int width_ = 0, height_ = 0, levelCount_ = 0;
    bool swizzled_ = false;
};

}  // namespace gfx
