#pragma once

#include <cstdint>

#include "core/file.h"
#include "math/quat.h"
#include "math/vec3.h"
#include "render/guard_clip.h"

namespace gfx {

class Texture;

// A rock or island mesh (FTR2, docs/formats/ftr2.md, ADR 0042, 0050): a tree
// of chunks at levels of detail. Each frame the tree is walked from the roots:
// a chunk is drawn when its geometric error projects under the tolerance (or
// it has no finer children), otherwise its children are. Chunks are skirted,
// so neighbours at different levels leave no cracks. Chunks close to the eye
// go through guard_clip.h first (big triangles beside the camera would
// otherwise be dropped by the GE, gotchas #19).
//
// Rock is most of the vertices in view, and the GE fetches, transforms and
// lights every index it is sent: so the vertex is 16 bytes (16-bit texture
// coordinates and position, scaled back by the texture scale and the model
// matrix) and carries its light - baked per placed rock by bake() - instead
// of a normal for the GE's lights.

// GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT.
struct TerrainVertex {
    uint16_t u, v;     // / 32768 * the material's texture scale + offset
    uint32_t color;    // ABGR, lit (bake()); the file holds the albedo
    int16_t x, y, z;   // / 32768 * the mesh's position scale, m
    int16_t pad;
};
static_assert(sizeof(TerrainVertex) == 16, "FTR2 vertex");

// What bake() needs and the GE never reads.
struct TerrainBakeVertex {
    int8_t nx, ny, nz, pad;  // normal * 127
    uint32_t albedo;         // ABGR: rock/turf colour x AO (FTR1's vertex colour)
};
static_assert(sizeof(TerrainBakeVertex) == 8, "FTR2 bake record");

struct TerrainChunk {
    float cx, cy, cz, radius;   // bounding sphere, model space, m
    float error;                // m against the finest level (0 for it)
    float edge;                 // m, longest triangle edge
    uint32_t firstVertex, firstIndex;
    uint16_t vertexCount, indexCount;
    uint16_t surfaceVertices;   // then the skirt's
    uint16_t material, level;
    uint16_t firstChild, childCount;
    uint16_t stripIndices;      // the first indices are one triangle strip, the rest a triangle list
    float coneX, coneY, coneZ;  // normal cone of the faces: axis ...
    float coneCos;              // ... and cos of its half-angle; -1: never back-facing
};
static_assert(sizeof(TerrainChunk) == 64, "FTR chunk record");

struct TerrainView {
    float pixelsPerRadian;  // screen height / 2 / tan(fovy / 2)
    float tolerance;        // px of geometric error allowed
    float maxDistance;      // m (fog end)
};

struct TerrainStats {
    int chunks = 0, triangles = 0, vertices = 0, clipped = 0;  // vertices: indices sent (the GE transforms each)
    int backFacing = 0;                                        // chunks skipped by their normal cone
};

// The light baked into a placed rock: what the GE's lighting did with one
// directional light, colour material ambient + diffuse (render/view.cpp
// enableDistantLight): colour = albedo * (ambient + key * max(0, n . sun)).
struct TerrainLight {
    math::Vec3 dirToSun;     // world
    uint32_t key, ambient;   // ABGR
};

class TerrainMesh {
public:
    static constexpr int kMaxMaterials = 4;
    static constexpr int kMaxInstances = 16;  // placements of one shape (the map has up to 11)

    TerrainMesh() = default;
    TerrainMesh(const TerrainMesh&) = delete;
    TerrainMesh& operator=(const TerrainMesh&) = delete;
    ~TerrainMesh();

    bool load(const char* path);
    int materialCount() const { return materialCount_; }
    const char* materialName(int i) const { return materials_[i]; }
    float radius() const { return radius_; }
    int chunkCount() const { return chunkCount_; }

    // Lights placement `instance` of this shape, turned by the yaw (cos/sin
    // as world::Rock): its own copy of the vertices with the light in their
    // colour (instance 0 lights the file's vertices in place). Once per
    // sortie; false if a copy could not be allocated (the instance is then
    // drawn with instance 0's colours).
    bool bake(int instance, float cosYaw, float sinYaw, const TerrainLight& light);

    // Around a batch of draws (no GE lighting: the light is in the colours).
    static void beginBatch();
    static void endBatch();
    // Draws placement `instance` at worldPos turned about +Y: rot =
    // Quat::axisAngle(kUp, yaw) with its cos/sin (world::Rock has them - no
    // trig per frame); textures[i] for material i. *lastBound: the texture
    // bound now, kept across meshes so a shared detail texture is not bound
    // again.
    void draw(int instance, const math::Vec3& worldPos, const math::Quat& rot, float cosYaw, float sinYaw,
              const Texture* const* textures, const TerrainView& view, const Texture** lastBound,
              TerrainStats* stats = nullptr) const;

private:
    core::Blob blob_;
    const TerrainChunk* chunks_ = nullptr;
    TerrainVertex* vertices_ = nullptr;           // in the blob: instance 0
    const TerrainBakeVertex* bakeData_ = nullptr;
    const uint16_t* indices_ = nullptr;
    TerrainVertex* instances_[kMaxInstances] = {};  // [0] unused (the blob's)
    float* coneSin_ = nullptr;   // per chunk: sin of the normal cone's half-angle (fixed, worked out once)
    int chunkCount_ = 0, rootCount_ = 0, materialCount_ = 0, vertexCount_ = 0;
    float radius_ = 0.0f;
    float scale_[3] = {1.0f, 1.0f, 1.0f};          // m: position = s16 / 32768 * scale
    float uvScale_[kMaxMaterials][2] = {}, uvOffset_[kMaxMaterials][2] = {};
    char materials_[kMaxMaterials][16] = {};
};

}  // namespace gfx
