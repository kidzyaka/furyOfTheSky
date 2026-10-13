#include "render/ocean.h"

#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>

#include <cmath>

#include "render/view.h"

namespace gfx {

void Ocean::init() {
    // Whole tiles per texture repeat keep UVs identical after re-centring.
    static_assert(static_cast<int>(kTileSize) % static_cast<int>(kTexRepeat) == 0, "tile/repeat");
    const float half = kTiles * kTileSize * 0.5f;
    const float uvPerTile = kTileSize / kTexRepeat;
    for (int z = 0; z <= kTiles; ++z) {
        for (int x = 0; x <= kTiles; ++x) {
            Vertex& v = vertices_[z * (kTiles + 1) + x];
            v.u = x * uvPerTile;
            v.v = z * uvPerTile;
            v.x = x * kTileSize - half;
            v.y = 0.0f;
            v.z = z * kTileSize - half;
        }
    }
    int k = 0;
    for (int z = 0; z < kTiles; ++z) {
        for (int x = 0; x < kTiles; ++x) {
            uint16_t a = z * (kTiles + 1) + x, b = a + 1, c = a + (kTiles + 1), d = c + 1;
            indices_[k++] = a; indices_[k++] = c; indices_[k++] = b;
            indices_[k++] = b; indices_[k++] = c; indices_[k++] = d;
        }
    }
    sceKernelDcacheWritebackRange(vertices_, sizeof vertices_);
    sceKernelDcacheWritebackRange(indices_, sizeof indices_);
}

void Ocean::draw(const math::Vec3& eye, const Texture& texture) const {
    math::Vec3 origin{std::floor(eye.x / kTileSize) * kTileSize, 0.0f, std::floor(eye.z / kTileSize) * kTileSize};
    setModelTranslation(origin);
    disableLighting();
    texture.bind(true, false);
    sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D,
                    kIndexCount, indices_, vertices_);
}

}  // namespace gfx
