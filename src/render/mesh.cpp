#include "render/mesh.h"

#include <pspgu.h>
#include <pspgum.h>

#include <cstring>

#include "render/view.h"

namespace gfx {
namespace {

struct FsmHeader {
    char magic[4];
    uint32_t vertexCount;
    uint32_t indexCount;
    float radius;
    uint32_t partCount;
    uint32_t reserved[3];
};
static_assert(sizeof(FsmHeader) == 32, "FSM4 header");

struct FsmPart {
    uint32_t first, count;
    uint16_t channel, side;
    uint32_t flags;  // bit 0: the indices are one triangle strip (tools/models/stripify.py)
    float pivot[3];
    float pad0;
    float axis[3];
    float pad1;
};
static_assert(sizeof(FsmPart) == 48, "FSM4 part record");

constexpr int kVertexFormat =
    GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_INDEX_16BIT | GU_TRANSFORM_3D;

}  // namespace

bool Mesh::load(const char* path) {
    blob_ = core::loadFile(path);
    if (!blob_ || blob_.size < sizeof(FsmHeader)) return false;

    FsmHeader h;
    std::memcpy(&h, blob_.data, sizeof h);
    if (std::memcmp(h.magic, "FSM4", 4) != 0 || h.partCount == 0 || h.partCount > kMaxParts) return false;
    size_t need = sizeof h + h.partCount * sizeof(FsmPart) + h.vertexCount * sizeof(MeshVertex) +
                  h.indexCount * sizeof(uint16_t);
    if (blob_.size < need) return false;

    const uint8_t* p = blob_.data + sizeof h;
    for (uint32_t i = 0; i < h.partCount; ++i, p += sizeof(FsmPart)) {
        FsmPart r;
        std::memcpy(&r, p, sizeof r);
        if (r.first + r.count > h.indexCount || r.channel >= kChannelCount) return false;
        parts_[i] = {r.first, r.count, r.channel, r.side, {r.pivot[0], r.pivot[1], r.pivot[2]},
                     {r.axis[0], r.axis[1], r.axis[2]}, (r.flags & 1u) != 0};
    }
    // Header 32 + parts 48 each + vertices 32 each: both arrays stay 16-byte aligned.
    vertices_ = reinterpret_cast<const MeshVertex*>(p);
    indices_ = reinterpret_cast<const uint16_t*>(vertices_ + h.vertexCount);
    vertexCount_ = h.vertexCount;
    indexCount_ = h.indexCount;
    partCount_ = static_cast<int>(h.partCount);
    radius_ = h.radius;
    return true;
}

namespace {

float partAngle(const MeshPart& part, const MeshPose* pose) {
    if (!pose || part.channel == kChannelStatic || part.side == kSideCentre) return 0.0f;
    return pose->angle[part.channel][part.side == kSideRight ? 0 : 1];
}

// Rotation about a hinge through `pivot`: x' = pos + rot * (pivot + Rp (x - pivot)).
void hinged(const MeshPart& part, float angle, const math::Vec3& pos, const math::Quat& rot, math::Vec3& outPos,
            math::Quat& outRot) {
    math::Quat rp = math::Quat::axisAngle(part.axis, angle);
    outRot = rot * rp;
    outPos = pos + rot.rotate(part.pivot - rp.rotate(part.pivot));
}

}  // namespace

void Mesh::draw(const math::Vec3& worldPos, const math::Quat& rot, const MeshPose* pose) const {
    if (!vertices_) return;
    bool baseLoaded = false;
    for (int i = 0; i < partCount_; ++i) {
        const MeshPart& part = parts_[i];
        float angle = partAngle(part, pose);
        if (angle == 0.0f) {
            if (!baseLoaded) setModel(worldPos, rot);
            baseLoaded = true;
        } else {
            math::Vec3 p;
            math::Quat q;
            hinged(part, angle, worldPos, rot, p, q);
            setModel(p, q);
            baseLoaded = false;
        }
        // Strips where the exporter made them: the GE transforms every index
        // (no vertex cache), ~2 per triangle instead of 3.
        sceGumDrawArray(part.strip ? GU_TRIANGLE_STRIP : GU_TRIANGLES, kVertexFormat, part.count,
                        indices_ + part.first, vertices_);
    }
}

bool Mesh::partTransform(uint16_t channel, uint16_t side, const math::Vec3& worldPos, const math::Quat& rot,
                         const MeshPose* pose, math::Vec3& outPos, math::Quat& outRot) const {
    for (int i = 0; i < partCount_; ++i) {
        if (parts_[i].channel == channel && parts_[i].side == side) {
            hinged(parts_[i], partAngle(parts_[i], pose), worldPos, rot, outPos, outRot);
            return true;
        }
    }
    return false;
}

}  // namespace gfx
