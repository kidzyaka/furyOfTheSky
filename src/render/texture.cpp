#include "render/texture.h"

#include <pspgu.h>

#include <cstring>

namespace gfx {
namespace {

struct FtxHeader {
    char magic[4];
    uint16_t width, height;
    uint8_t psm, levels, swizzled, pad0;
    uint16_t clutEntries, pad1;
};
static_assert(sizeof(FtxHeader) == 16, "FTX2 header");

}  // namespace

bool Texture::load(const char* path) {
    blob_ = core::loadFile(path);
    if (!blob_ || blob_.size < sizeof(FtxHeader)) return false;

    FtxHeader h;
    std::memcpy(&h, blob_.data, sizeof h);
    if (std::memcmp(h.magic, "FTX2", 4) != 0 || h.psm != GU_PSM_T8 || h.clutEntries != 256 ||
        h.levels < 1 || h.levels > 8)
        return false;

    const uint8_t* p = blob_.data + sizeof h;
    clut_ = reinterpret_cast<const uint32_t*>(p);
    p += 256 * sizeof(uint32_t);
    for (int i = 0; i < h.levels; ++i) {
        levels_[i] = p;
        p += (h.width >> i) * (h.height >> i);
    }
    if (p > blob_.data + blob_.size) return false;

    width_ = h.width;
    height_ = h.height;
    levelCount_ = h.levels;
    swizzled_ = h.swizzled != 0;
    return true;
}

void Texture::bind(bool repeat, bool alpha) const {
    if (!clut_) {
        sceGuDisable(GU_TEXTURE_2D);
        return;
    }
    sceGuEnable(GU_TEXTURE_2D);
    sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
    sceGuClutLoad(256 / 8, clut_);  // counted in 8-entry blocks
    sceGuTexMode(GU_PSM_T8, levelCount_ - 1, 0, swizzled_ ? 1 : 0);
    for (int i = 0; i < levelCount_; ++i)
        sceGuTexImage(i, width_ >> i, height_ >> i, width_ >> i, levels_[i]);
    sceGuTexFunc(GU_TFX_MODULATE, alpha ? GU_TCC_RGBA : GU_TCC_RGB);
    sceGuTexFilter(GU_LINEAR_MIPMAP_LINEAR, GU_LINEAR);
    sceGuTexLevelMode(GU_TEXTURE_AUTO, 0.0f);
    sceGuTexWrap(repeat ? GU_REPEAT : GU_CLAMP, repeat ? GU_REPEAT : GU_CLAMP);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
}

}  // namespace gfx
