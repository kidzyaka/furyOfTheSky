#pragma once

#include <cstdint>

#include "core/file.h"

namespace gfx {

// Matches the FSM3 vertex written by tools/models/fmodel.py.
struct MeshVertex {
    float u, v;
    float nx, ny, nz;
    float x, y, z;
};
static_assert(sizeof(MeshVertex) == 32, "GE vertex layout");

// Indexed, smooth-shaded triangle list (FSM3), drawn in one GE call.
class Mesh {
public:
    bool load(const char* path);
    void draw() const;

    uint32_t triangleCount() const { return indexCount_ / 3; }
    float radius() const { return radius_; }

private:
    core::Blob blob_;
    const MeshVertex* vertices_ = nullptr;
    const uint16_t* indices_ = nullptr;
    uint32_t vertexCount_ = 0;
    uint32_t indexCount_ = 0;
    float radius_ = 0.0f;
};

}  // namespace gfx
