#include "render/ocean.h"

#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>

#include <cmath>

#include "render/view.h"

namespace gfx {

namespace {

int quad(uint16_t* out, int n, int x, int z) {
    uint16_t a = z * (n + 1) + x, b = a + 1, c = a + (n + 1), d = c + 1;
    out[0] = a; out[1] = c; out[2] = b;
    out[3] = b; out[4] = c; out[5] = d;
    return 6;
}

}  // namespace

void Ocean::fillGrid(Vertex* v, int n, float tile) {
    const float uv = tile / kTexRepeat;
    for (int z = 0; z <= n; ++z)
        for (int x = 0; x <= n; ++x) v[z * (n + 1) + x] = {x * uv, z * uv, x * tile, 0.0f, z * tile};
}

void Ocean::init() {
    // UVs follow world position / kTexRepeat; both grids move in whole
    // texture periods, so the pattern stays fixed in the world. The coarse
    // grid starts 15 tiles (4800 m = 75 periods) before the fine one: same phase.
    static_assert(static_cast<int>(kFineSize * 4) == static_cast<int>(kTexRepeat), "fine tile = 1/4 period");
    static_assert(static_cast<int>(kCoarseSize) % static_cast<int>(kTexRepeat) == 0, "coarse tile/period");
    fillGrid(fine_, kFine, kFineSize);
    fillGrid(coarse_, kCoarse, kCoarseSize);

    int k = 0;
    for (int z = 0; z < kFine; ++z)
        for (int x = 0; x < kFine; ++x) k += quad(fineIndices_ + k, kFine, x, z);

    // Coarse tiles ring by ring around the (missing) centre tile, so the
    // first indices cover the area nearest to the camera.
    const int c = kCoarse / 2;
    k = 0;
    for (int ring = 1; ring <= c; ++ring) {
        for (int z = 0; z < kCoarse; ++z)
            for (int x = 0; x < kCoarse; ++x) {
                int dx = x > c ? x - c : c - x, dz = z > c ? z - c : c - z;
                if ((dx > dz ? dx : dz) == ring) k += quad(coarseIndices_ + k, kCoarse, x, z);
            }
        if (ring == kWaveRings) innerCoarseIndices_ = k;
    }
    sceKernelDcacheWritebackRange(fine_, sizeof fine_);
    sceKernelDcacheWritebackRange(fineIndices_, sizeof fineIndices_);
    sceKernelDcacheWritebackRange(coarse_, sizeof coarse_);
    sceKernelDcacheWritebackRange(coarseIndices_, sizeof coarseIndices_);
}

void Ocean::drawGrids(const math::Vec3& eye, bool wavesOnly) const {
    constexpr int kFormat = GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D;
    // Fine patch centre: the camera snapped to whole texture periods (camera
    // is always within +-32 m of it, the patch reaches +-160 m).
    const float cx = std::round(eye.x / kTexRepeat) * kTexRepeat;
    const float cz = std::round(eye.z / kTexRepeat) * kTexRepeat;
    const float half = kCoarseSize * 0.5f;
    setModelTranslation({cx - half, 0.0f, cz - half});
    sceGumDrawArray(GU_TRIANGLES, kFormat, kFineIndices, fineIndices_, fine_);
    const float ring = (kCoarse / 2) * kCoarseSize;
    setModelTranslation({cx - half - ring, 0.0f, cz - half - ring});
    sceGumDrawArray(GU_TRIANGLES, kFormat, wavesOnly ? innerCoarseIndices_ : kCoarseIndices, coarseIndices_,
                    coarse_);
}

void Ocean::draw(const math::Vec3& eye, const Texture& texture, const Texture& waves, float time) const {
    disableLighting();
    texture.bind(true, false);
    drawGrids(eye, false);

    // Wave highlights: same triangles (identical depth, GEQUAL passes), the
    // texture at half scale (128 m) drifting with time. The offset stays in
    // 0..1 so the GE's 24-bit float keeps it exact (docs/psp/gotchas.md #20).
    waves.bind(true, true);
    sceGuTexScale(0.5f, 0.5f);
    float u = time * 0.011f, v = time * 0.006f;
    sceGuTexOffset(u - std::floor(u), v - std::floor(v));
    sceGuColor(abgr(255, 255, 255, 80));
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFF);
    sceGuDepthMask(GU_TRUE);
    setAdditiveFog(true);
    drawGrids(eye, true);
    setAdditiveFog(false);
    sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_BLEND);
    sceGuColor(0xFFFFFFFF);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
}

void Ocean::drawGlint(const math::Vec3& dirToSun, const Texture& glint, float time) const {
    if (dirToSun.y < 0.05f) return;
    const math::Vec3 g = math::normalize({dirToSun.x, -dirToSun.y, dirToSun.z});  // mirror image of the sun
    const math::Vec3 right = math::normalize(math::cross(g, math::kUp));
    const math::Vec3 up = math::cross(right, g);
    const math::Vec3 c = g * 900.0f;
    const math::Vec3 a = right * 110.0f, b = up * 300.0f;
    struct V {
        float u, v;
        uint32_t color;
        float x, y, z;
    };
    // Sparkles rearrange a few times a second: glitter, not crawling noise.
    float jump = std::floor(time * 6.0f) * 0.37f;
    float u0 = jump - std::floor(jump);
    uint32_t col = abgr(255, 240, 214, 220);
    auto* v = static_cast<V*>(sceGuGetMemory(6 * sizeof(V)));
    math::Vec3 p00 = c - a - b, p10 = c + a - b, p11 = c + a + b, p01 = c - a + b;
    v[0] = {u0, 1.0f, col, p00.x, p00.y, p00.z};
    v[1] = {u0 + 1.0f, 1.0f, col, p10.x, p10.y, p10.z};
    v[2] = {u0 + 1.0f, 0.0f, col, p11.x, p11.y, p11.z};
    v[3] = v[0];
    v[4] = v[2];
    v[5] = {u0, 0.0f, col, p01.x, p01.y, p01.z};

    setModelRenderSpace();
    disableLighting();
    glint.bind(true, true);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_FOG);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFF);
    sceGumDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 6, nullptr, v);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_FOG);
    sceGuEnable(GU_CULL_FACE);
    sceGuEnable(GU_DEPTH_TEST);
}

}  // namespace gfx
