#include "render/mesh.h"

#include <pspgu.h>
#include <pspgum.h>

#include <cstring>

namespace gfx {
namespace {

struct FsmHeader {
    char magic[4];
    uint32_t vertexCount;
    uint32_t indexCount;
    float radius;
};
static_assert(sizeof(FsmHeader) == 16, "FSM3 header");

constexpr int kVertexFormat =
    GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D;

}  // namespace

bool Mesh::load(const char* path) {
    blob_ = core::loadFile(path);
    if (!blob_ || blob_.size < sizeof(FsmHeader)) return false;

    FsmHeader h;
    std::memcpy(&h, blob_.data, sizeof h);
    size_t need = sizeof h + h.vertexCount * sizeof(MeshVertex) + h.indexCount * sizeof(uint16_t);
    if (std::memcmp(h.magic, "FSM3", 4) != 0 || blob_.size < need) return false;

    // Header is 16 bytes and vertices 32, so both arrays stay 16-byte aligned.
    vertices_ = reinterpret_cast<const MeshVertex*>(blob_.data + sizeof h);
    indices_ = reinterpret_cast<const uint16_t*>(vertices_ + h.vertexCount);
    vertexCount_ = h.vertexCount;
    indexCount_ = h.indexCount;
    radius_ = h.radius;
    return true;
}

void Mesh::draw() const {
    if (!vertices_) return;
    sceGumDrawArray(GU_TRIANGLES, kVertexFormat, indexCount_, indices_, vertices_);
}

}  // namespace gfx
