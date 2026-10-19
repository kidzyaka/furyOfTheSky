#include "render/terrain_mesh.h"

#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>

#include <cmath>
#include <cstring>
#include <new>

#include "math/quat.h"
#include "render/gu_context.h"
#include "render/texture.h"
#include "render/view.h"

namespace gfx {
namespace {

struct FtrHeader {
    char magic[4];
    uint32_t chunkCount, vertexCount, indexCount;
    uint16_t rootCount, materialCount;
    float radius;
    uint32_t reserved[2];
    float scale[3];
    uint32_t reserved2;
};
static_assert(sizeof(FtrHeader) == 48, "FTR2 header");

struct FtrMaterial {
    char name[16];
    float uvScale[2], uvOffset[2];
};
static_assert(sizeof(FtrMaterial) == 32, "FTR2 material");

constexpr int kFormat = GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_3D;
constexpr int kClipFormat = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D;
constexpr float kFraction = 1.0f / 32768.0f;  // 16-bit fixed point as the GE reads it in 3D

// Guard frustum for the software clip: projected vertices stay within
// +-1900 px of the centre of the GE's 4096 px space (limit 2048, gotchas #5).
constexpr float kGuardPx = 1900.0f;
constexpr float kNearClip = 3.05f;  // just inside the projection's 3 m near plane

constexpr int kMaxChunkVertices = 8192;  // the biggest chunk is a whole stack (~4300)
uint8_t g_codes[kMaxChunkVertices];
constexpr int kClipBuffer = 1536;
ClipVertex g_clipped[kClipBuffer];

void flush(int count) {
    if (count == 0) return;
    void* mem = sceGuGetMemory(count * sizeof(ClipVertex));
    std::memcpy(mem, g_clipped, count * sizeof(ClipVertex));
    sceGumDrawArray(GU_TRIANGLES, kClipFormat, count, nullptr, mem);
}

// The same units the GE works in: fractions, the model matrix scales them.
ClipVertex decode(const TerrainVertex& t) {
    return {t.u * kFraction, t.v * kFraction, t.color, t.x * kFraction, t.y * kFraction, t.z * kFraction};
}

}  // namespace

TerrainMesh::~TerrainMesh() {
    for (TerrainVertex* p : instances_) delete[] p;
    delete[] coneSin_;
}

bool TerrainMesh::load(const char* path) {
    for (TerrainVertex*& p : instances_) {
        delete[] p;
        p = nullptr;
    }
    blob_ = core::loadFile(path);
    if (!blob_ || blob_.size < sizeof(FtrHeader)) return false;
    FtrHeader h;
    std::memcpy(&h, blob_.data, sizeof h);
    if (std::memcmp(h.magic, "FTR2", 4) != 0 || h.rootCount == 0 || h.rootCount > h.chunkCount ||
        h.materialCount == 0 || h.materialCount > kMaxMaterials || h.chunkCount > 65535)
        return false;
    const size_t need = sizeof h + sizeof(FtrMaterial) * h.materialCount + sizeof(TerrainChunk) * h.chunkCount +
                        (sizeof(TerrainVertex) + sizeof(TerrainBakeVertex)) * h.vertexCount + 2u * h.indexCount;
    if (blob_.size < need) return false;
    uint8_t* p = blob_.data + sizeof h;
    for (int i = 0; i < h.materialCount; ++i, p += sizeof(FtrMaterial)) {
        FtrMaterial m;
        std::memcpy(&m, p, sizeof m);
        std::memcpy(materials_[i], m.name, 16);
        materials_[i][15] = 0;
        uvScale_[i][0] = m.uvScale[0];
        uvScale_[i][1] = m.uvScale[1];
        uvOffset_[i][0] = m.uvOffset[0];
        uvOffset_[i][1] = m.uvOffset[1];
    }
    // 48 + 32 n + 64 n: the vertex array stays 16-byte aligned.
    chunks_ = reinterpret_cast<const TerrainChunk*>(p);
    vertices_ = reinterpret_cast<TerrainVertex*>(p + sizeof(TerrainChunk) * h.chunkCount);
    bakeData_ = reinterpret_cast<const TerrainBakeVertex*>(vertices_ + h.vertexCount);
    indices_ = reinterpret_cast<const uint16_t*>(bakeData_ + h.vertexCount);
    for (uint32_t i = 0; i < h.chunkCount; ++i) {
        const TerrainChunk& c = chunks_[i];
        if (c.firstVertex + c.vertexCount > h.vertexCount || c.firstIndex + c.indexCount > h.indexCount ||
            c.vertexCount > kMaxChunkVertices || c.material >= h.materialCount || c.stripIndices > c.indexCount ||
            c.firstChild + c.childCount > h.chunkCount)
            return false;
    }
    chunkCount_ = static_cast<int>(h.chunkCount);
    vertexCount_ = static_cast<int>(h.vertexCount);
    delete[] coneSin_;
    coneSin_ = new float[chunkCount_];
    for (int i = 0; i < chunkCount_; ++i) {
        const float c = chunks_[i].coneCos;
        coneSin_[i] = std::sqrt(std::fmax(0.0f, 1.0f - c * c));
    }
    for (int k = 0; k < 3; ++k) scale_[k] = h.scale[k];
    rootCount_ = h.rootCount;
    materialCount_ = h.materialCount;
    radius_ = h.radius;
    return true;
}

bool TerrainMesh::bake(int instance, float cosYaw, float sinYaw, const TerrainLight& light) {
    if (!chunks_ || instance < 0 || instance >= kMaxInstances) return false;
    TerrainVertex* dst = vertices_;
    if (instance > 0) {
        if (!instances_[instance]) {
            TerrainVertex* copy = new (std::nothrow) TerrainVertex[vertexCount_];
            if (!copy) return false;
            std::memcpy(copy, vertices_, sizeof(TerrainVertex) * vertexCount_);
            instances_[instance] = copy;
        }
        dst = instances_[instance];
    }
    // The sun in the mesh's frame (the inverse of the yaw, as draw()), over
    // 127 for the stored normals.
    const math::Vec3& l = light.dirToSun;
    const float c = cosYaw, s = sinYaw, k = 1.0f / 127.0f;
    const float lx = (l.x * c - l.z * s) * k, ly = l.y * k, lz = (l.x * s + l.z * c) * k;
    float ambient[3], key[3];
    for (int ch = 0; ch < 3; ++ch) {
        ambient[ch] = static_cast<float>((light.ambient >> (8 * ch)) & 0xFFu) * (1.0f / 255.0f);
        key[ch] = static_cast<float>((light.key >> (8 * ch)) & 0xFFu) * (1.0f / 255.0f);
    }
    for (int i = 0; i < vertexCount_; ++i) {
        const TerrainBakeVertex& b = bakeData_[i];
        float d = b.nx * lx + b.ny * ly + b.nz * lz;
        if (d < 0.0f) d = 0.0f;
        uint32_t out = b.albedo & 0xFF000000u;
        for (int ch = 0; ch < 3; ++ch) {
            const float a = static_cast<float>((b.albedo >> (8 * ch)) & 0xFFu);
            int v = static_cast<int>(a * (ambient[ch] + key[ch] * d) + 0.5f);
            if (v > 255) v = 255;
            out |= static_cast<uint32_t>(v) << (8 * ch);
        }
        dst[i].color = out;
    }
    sceKernelDcacheWritebackRange(dst, sizeof(TerrainVertex) * vertexCount_);
    return true;
}

void TerrainMesh::beginBatch() {
    // The vertex colour is the lit colour (bake()): no GE lighting.
    sceGuDisable(GU_LIGHTING);
}

void TerrainMesh::endBatch() {
    // draw() set the materials' texture scale and offset.
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuColor(0xFFFFFFFF);
}

void TerrainMesh::draw(int instance, const math::Vec3& worldPos, const math::Quat& rot, float cosYaw, float sinYaw,
                       const Texture* const* textures, const TerrainView& view, const Texture** lastBound,
                       TerrainStats* stats) const {
    if (!chunks_) return;
    const TerrainVertex* vertices =
        instance > 0 && instance < kMaxInstances && instances_[instance] ? instances_[instance] : vertices_;
    const CameraBasis& cam = camera();
    // The camera in the model's frame (the inverse of the yaw, as world::Terrain).
    const float c = cosYaw, s = sinYaw;
    auto toModel = [&](const math::Vec3& v) { return math::Vec3{v.x * c - v.z * s, v.y, v.x * s + v.z * c}; };
    const math::Vec3 eye = toModel(cam.eye - worldPos);
    const math::Vec3 fwd = toModel(cam.forward), right = toModel(cam.right), up = toModel(cam.up);
    const float e3[3] = {eye.x, eye.y, eye.z}, f3[3] = {fwd.x, fwd.y, fwd.z};
    const float r3[3] = {right.x, right.y, right.z}, u3[3] = {up.x, up.y, up.z};
    // Planes in metres, then in the stored fractions the clipper sees.
    ClipPlanes planes = makeClipPlanes(e3, f3, r3, u3, kNearClip, kGuardPx * cam.tanHalfX / (kScreenW * 0.5f),
                                       kGuardPx * cam.tanHalfY / (kScreenH * 0.5f));
    scaleClipPlanes(planes, scale_);
    setModel(worldPos, rot, {scale_[0], scale_[1], scale_[2]});
    int material = -1;  // this mesh's texture scale/offset not set yet
    const float lodK = view.pixelsPerRadian / view.tolerance;

    int stack[64];
    int sp = 0;
    for (int r = rootCount_ - 1; r >= 0; --r) stack[sp++] = r;
    while (sp > 0) {
        const int idxChunk = stack[--sp];
        const TerrainChunk& ch = chunks_[idxChunk];
        const math::Vec3 rel = math::Vec3{ch.cx, ch.cy, ch.cz} - eye;
        const float z = math::dot(rel, fwd);
        if (z < -ch.radius || z > view.maxDistance + ch.radius) continue;
        const float zc = z > 1e-3f ? z : 1e-3f;
        if (std::fabs(math::dot(rel, right)) > zc * cam.tanHalfX + ch.radius * 1.5f ||
            std::fabs(math::dot(rel, up)) > zc * cam.tanHalfY + ch.radius * 1.5f)
            continue;
        const float d = std::sqrt(math::dot(rel, rel));  // the one square root per chunk
        float dist = d - ch.radius;
        if (dist < 1.0f) dist = 1.0f;
        if (ch.childCount > 0 && ch.error * lodK > dist) {
            for (int k = ch.childCount - 1; k >= 0 && sp < 64; --k) stack[sp++] = ch.firstChild + k;
            continue;
        }
        // Every face of the chunk looks away from every point of its sphere
        // as seen from the eye: the far side of the island (normal cone).
        if (ch.coneCos > -0.99f && d > ch.radius) {
            const float sinA = coneSin_[idxChunk];
            const float sinB = ch.radius / d, cosB = std::sqrt(1.0f - sinB * sinB);
            // cos and sin of (cone half-angle + the sphere's angular radius)
            const float cosAB = ch.coneCos * cosB - sinA * sinB, sinAB = sinA * cosB + ch.coneCos * sinB;
            const float cosT = -(rel.x * ch.coneX + rel.y * ch.coneY + rel.z * ch.coneZ) / d;  // eye seen from the chunk
            if (cosAB > 0.0f && cosT < -sinAB) {
                if (stats) ++stats->backFacing;
                continue;
            }
        }
        if (ch.material != material) {
            material = ch.material;
            const Texture* tex = textures[material];
            if (tex != *lastBound) {
                *lastBound = tex;
                const bool tiling = std::strncmp(materials_[material], "terrain_", 8) == 0;
                tex->bind(tiling, false, !tiling);  // tiling details: bilinear in the nearest mip
            }
            // The 16-bit coordinates are fractions of this mesh's range for the material.
            sceGuTexScale(uvScale_[material][0], uvScale_[material][1]);
            sceGuTexOffset(uvOffset_[material][0], uvOffset_[material][1]);
        }
        const TerrainVertex* v = vertices + ch.firstVertex;
        const uint16_t* idx = indices_ + ch.firstIndex;
        const int strip = ch.stripIndices, list = ch.indexCount - ch.stripIndices;
        if (stats) {
            ++stats->chunks;
            stats->triangles += (strip > 2 ? strip - 2 : 0) + list / 3;
            stats->vertices += ch.indexCount;
        }
        // Triangles here are small next to their distance: the GE takes them as they are.
        if (dist > 3.0f * ch.edge + 6.0f) {
            if (strip > 2) sceGumDrawArray(GU_TRIANGLE_STRIP, kFormat | GU_INDEX_16BIT, strip, idx, v);
            if (list > 0) sceGumDrawArray(GU_TRIANGLES, kFormat | GU_INDEX_16BIT, list, idx + strip, v);
            continue;
        }
        // Near the eye: wholly inside the guard frustum as they are, culled
        // when outside one plane, clipped otherwise. The strip is unrolled
        // into triangles (every other one reversed, as the GE draws it).
        for (int i = 0; i < ch.vertexCount; ++i)
            g_codes[i] = static_cast<uint8_t>(outcode(planes, v[i].x * kFraction, v[i].y * kFraction, v[i].z * kFraction));
        const int triangles = (strip > 2 ? strip - 2 : 0) + list / 3;
        auto corners = [&](int t, int& a, int& b, int& cc) {
            if (t < strip - 2) {
                a = idx[t + (t & 1)];
                b = idx[t + 1 - (t & 1)];
                cc = idx[t + 2];
            } else {
                const int k = strip + (t - (strip > 2 ? strip - 2 : 0)) * 3;
                a = idx[k];
                b = idx[k + 1];
                cc = idx[k + 2];
            }
        };
        int inside = 0;
        for (int t = 0; t < triangles; ++t) {
            int a, b, cc;
            corners(t, a, b, cc);
            if (a != b && b != cc && a != cc && (g_codes[a] | g_codes[b] | g_codes[cc]) == 0) ++inside;
        }
        if (inside > 0) {
            auto* out = static_cast<uint16_t*>(sceGuGetMemory(((inside * 3 * 2) + 3) & ~3));
            int w = 0;
            for (int t = 0; t < triangles; ++t) {
                int a, b, cc;
                corners(t, a, b, cc);
                if (a != b && b != cc && a != cc && (g_codes[a] | g_codes[b] | g_codes[cc]) == 0) {
                    out[w++] = static_cast<uint16_t>(a);
                    out[w++] = static_cast<uint16_t>(b);
                    out[w++] = static_cast<uint16_t>(cc);
                }
            }
            sceGumDrawArray(GU_TRIANGLES, kFormat | GU_INDEX_16BIT, w, out, v);
        }
        int n = 0;
        for (int t = 0; t < triangles; ++t) {
            int a, b, cc;
            corners(t, a, b, cc);
            if (a == b || b == cc || a == cc) continue;
            const unsigned ca = g_codes[a], cb = g_codes[b], cc3 = g_codes[cc];
            if ((ca | cb | cc3) == 0 || (ca & cb & cc3) != 0) continue;
            if (n + kClipMaxOut > kClipBuffer) {
                flush(n);
                n = 0;
            }
            n += clipTriangle(decode(v[a]), decode(v[b]), decode(v[cc]), planes, ca | cb | cc3, g_clipped + n);
            if (stats) ++stats->clipped;
        }
        flush(n);
    }
}

}  // namespace gfx
