#pragma once

// Software clipping of geometry next to the camera (docs/psp/gotchas.md #19,
// docs/decisions/0042). The GE clips triangles against the near plane, but
// drops a triangle outright when a clipped vertex projects outside its
// 4096 x 4096 virtual screen - which any big triangle passing beside the
// camera does. Chunks near the eye are therefore clipped here against the
// near plane and a guard frustum well inside that limit before they reach
// the GE; nothing it receives needs clipping any more.
//
// Header-only and platform-neutral: tests/ge_precision_test.cpp checks it on
// the host.

#include <cmath>
#include <cstdint>

namespace gfx {

// A terrain vertex decoded for clipping (FTR2, docs/formats/ftr2.md, keeps
// 16-bit fractions; here they are floats in the same units, and the pieces go
// to the GE as GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF under the
// same model matrix and texture scale). Lighting is baked into the colour.
struct ClipVertex {
    float u, v;
    uint32_t color;
    float x, y, z;
};
static_assert(sizeof(ClipVertex) == 24, "GE vertex layout");

// Five planes, inside where n . p + d >= 0, in the frame the vertices are in.
struct ClipPlanes {
    float n[5][3];
    float d[5];
};

constexpr int kClipMaxOut = 18;  // a triangle cut by 5 planes: up to 8 corners, a fan of 6 triangles

// Near plane `nearZ` in front of the eye; the guard frustum |x| <= gx z,
// |y| <= gy z. All vectors in the vertices' frame; fwd/right/up orthonormal.
inline ClipPlanes makeClipPlanes(const float eye[3], const float fwd[3], const float right[3], const float up[3],
                                 float nearZ, float gx, float gy) {
    ClipPlanes p;
    auto set = [&](int i, float a, float b, float c, float extra) {
        p.n[i][0] = a;
        p.n[i][1] = b;
        p.n[i][2] = c;
        p.d[i] = -(a * eye[0] + b * eye[1] + c * eye[2]) - extra;
    };
    set(0, fwd[0], fwd[1], fwd[2], nearZ);
    set(1, gx * fwd[0] - right[0], gx * fwd[1] - right[1], gx * fwd[2] - right[2], 0.0f);
    set(2, gx * fwd[0] + right[0], gx * fwd[1] + right[1], gx * fwd[2] + right[2], 0.0f);
    set(3, gy * fwd[0] - up[0], gy * fwd[1] - up[1], gy * fwd[2] - up[2], 0.0f);
    set(4, gy * fwd[0] + up[0], gy * fwd[1] + up[1], gy * fwd[2] + up[2], 0.0f);
    return p;
}

// Planes for vertices stored as fractions of a per-axis scale (p = s * q):
// n . p + d = (n * s) . q + d - same signs, same crossing points.
inline void scaleClipPlanes(ClipPlanes& p, const float s[3]) {
    for (int i = 0; i < 5; ++i)
        for (int k = 0; k < 3; ++k) p.n[i][k] *= s[k];
}

inline float planeDistance(const ClipPlanes& p, int i, float x, float y, float z) {
    return p.n[i][0] * x + p.n[i][1] * y + p.n[i][2] * z + p.d[i];
}

inline float planeDistance(const ClipPlanes& p, int i, const ClipVertex& v) { return planeDistance(p, i, v.x, v.y, v.z); }

// Bit i set: the point is outside plane i.
inline unsigned outcode(const ClipPlanes& p, float x, float y, float z) {
    unsigned code = 0;
    for (int i = 0; i < 5; ++i)
        if (planeDistance(p, i, x, y, z) < 0.0f) code |= 1u << i;
    return code;
}

inline unsigned outcode(const ClipPlanes& p, const ClipVertex& v) { return outcode(p, v.x, v.y, v.z); }

inline ClipVertex lerpVertex(const ClipVertex& a, const ClipVertex& b, float t) {
    ClipVertex o;
    o.u = a.u + (b.u - a.u) * t;
    o.v = a.v + (b.v - a.v) * t;
    uint32_t c = 0;
    for (int s = 0; s < 32; s += 8) {
        const float ca = static_cast<float>((a.color >> s) & 0xFFu), cb = static_cast<float>((b.color >> s) & 0xFFu);
        c |= static_cast<uint32_t>(ca + (cb - ca) * t + 0.5f) << s;
    }
    o.color = c;
    o.x = a.x + (b.x - a.x) * t;
    o.y = a.y + (b.y - a.y) * t;
    o.z = a.z + (b.z - a.z) * t;
    return o;
}

// Clips triangle (a, b, c) (winding kept) against the planes in `mask`;
// writes the pieces as a triangle list to `out` (kClipMaxOut vertices at
// most) and returns the vertex count (a multiple of 3; 0 if nothing is left).
inline int clipTriangle(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c, const ClipPlanes& p,
                        unsigned mask, ClipVertex* out) {
    ClipVertex buf[2][9];
    int n = 3;
    buf[0][0] = a;
    buf[0][1] = b;
    buf[0][2] = c;
    int cur = 0;
    for (int i = 0; i < 5 && n > 0; ++i) {
        if (!(mask & (1u << i))) continue;
        const ClipVertex* in = buf[cur];
        ClipVertex* o = buf[cur ^ 1];
        int m = 0;
        for (int k = 0; k < n; ++k) {
            const ClipVertex& s = in[k];
            const ClipVertex& e = in[(k + 1) % n];
            const float ds = planeDistance(p, i, s), de = planeDistance(p, i, e);
            if (ds >= 0.0f) o[m++] = s;
            if ((ds >= 0.0f) != (de >= 0.0f) && m < 9) o[m++] = lerpVertex(s, e, ds / (ds - de));
        }
        n = m;
        cur ^= 1;
    }
    if (n < 3) return 0;
    int w = 0;
    for (int k = 1; k + 1 < n; ++k) {
        out[w++] = buf[cur][0];
        out[w++] = buf[cur][k];
        out[w++] = buf[cur][k + 1];
    }
    return w;
}

}  // namespace gfx
