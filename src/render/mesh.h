#pragma once

#include <cstdint>

#include "core/file.h"
#include "math/quat.h"

namespace gfx {

// Matches the FSM4 vertex written by tools/models/fmodel.py.
struct MeshVertex {
    float u, v;
    float nx, ny, nz;
    float x, y, z;
};
static_assert(sizeof(MeshVertex) == 32, "GE vertex layout");

// Articulation channels of the FSM4 part table (docs/formats/fsm4.md); must
// match CH_* in tools/models/fmodel.py. Positive angles are mirror-symmetric:
// both sides move alike (TE down, LE down, rudder TE outboard, nozzle exit down).
enum MeshChannel : uint16_t {
    kChannelStatic,
    kChannelStabilator,
    kChannelFlaperon,
    kChannelSlat,
    kChannelRudder,
    kChannelNozzle,
    kChannelCount
};
enum MeshSide : uint16_t { kSideCentre, kSideRight, kSideLeft };

struct MeshPart {
    uint32_t first, count;  // index range
    uint16_t channel, side;
    math::Vec3 pivot;       // model space, m
    math::Vec3 axis;        // unit hinge axis, model space
};

// Deflection per channel and side, radians: angle[channel][0 = right, 1 = left].
struct MeshPose {
    float angle[kChannelCount][2] = {};
};

// Indexed, smooth-shaded triangle list (FSM4) split into parts: the static
// airframe plus hinged parts, each drawn with its own model matrix.
class Mesh {
public:
    static constexpr int kMaxParts = 16;

    bool load(const char* path);
    // Sets the model matrix (camera-relative, docs/decisions/0013) and draws
    // every part, hinged ones rotated by `pose` (null = all neutral).
    void draw(const math::Vec3& worldPos, const math::Quat& rot, const MeshPose* pose = nullptr) const;
    // World transform of a hinged part, to attach effects to it (afterburner
    // flame to a nozzle): a model-space point p of the part lands at outPos + outRot * p.
    bool partTransform(uint16_t channel, uint16_t side, const math::Vec3& worldPos, const math::Quat& rot,
                       const MeshPose* pose, math::Vec3& outPos, math::Quat& outRot) const;

    uint32_t triangleCount() const { return indexCount_ / 3; }
    int partCount() const { return partCount_; }
    float radius() const { return radius_; }

private:
    core::Blob blob_;
    const MeshVertex* vertices_ = nullptr;
    const uint16_t* indices_ = nullptr;
    uint32_t vertexCount_ = 0;
    uint32_t indexCount_ = 0;
    float radius_ = 0.0f;
    MeshPart parts_[kMaxParts];
    int partCount_ = 0;
};

}  // namespace gfx
