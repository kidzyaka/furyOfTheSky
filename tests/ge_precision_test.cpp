// Why rendering is camera-relative (docs/decisions/0013).
// pspgu sends every matrix element to the GE as a 24-bit float: it shifts the
// IEEE bits right by 8 (verified in libpspgu.a: sceGuSetMatrix, `srl v0,v0,0x8`),
// leaving 15 explicit mantissa bits. This test reproduces that truncation.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "render/guard_clip.h"

namespace {

float ge24(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    bits &= 0xFFFFFF00u;  // what survives `>> 8` into the 24-bit command argument
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

// Largest on-screen-relevant error of "aircraft ~20 m ahead of the camera" when
// the camera is `distance` metres from the world origin.
void measure(float distance, float& worldErr, float& relativeErr) {
    worldErr = relativeErr = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        float eye = distance + i * 0.137f;  // camera moving along the axis
        // A non-round, varying offset like the lagging chase camera produces
        // (a round 20 m would be an exact multiple of the coarse step and cancel).
        float plane = eye + 20.0f + 0.37f * std::sin(i * 0.1f);
        float exact = plane - eye;
        // World-space: model matrix carries `plane`, view matrix carries `-eye`.
        worldErr = std::fmax(worldErr, std::fabs((ge24(plane) - ge24(eye)) - exact));
        // Camera-relative: the CPU subtracts first, the matrix carries ~20 m.
        relativeErr = std::fmax(relativeErr, std::fabs(ge24(plane - eye) - exact));
    }
}

}  // namespace

// Software guard-band clipping (render/guard_clip.h, gotchas #19): every
// piece inside the near plane and the guard frustum, nothing visible lost,
// winding kept.
int testGuardClip() {
    using gfx::ClipVertex;
    std::printf("guard-band clip: pieces inside, coverage kept, winding kept\n");
    const float eye[3] = {0, 0, 0}, fwd[3] = {0, 0, 1}, right[3] = {1, 0, 0}, up[3] = {0, 1, 0};
    const float g = 6.0f, nearZ = 3.05f;
    const gfx::ClipPlanes pl = gfx::makeClipPlanes(eye, fwd, right, up, nearZ, g, g);
    uint32_t rng = 99u;
    auto rnd = [&](float lo, float hi) {
        rng = rng * 1664525u + 1013904223u;
        return lo + (hi - lo) * static_cast<float>(rng >> 8) / 16777216.0f;
    };
    auto inside = [&](float x, float y, float z, float eps) {
        return z >= nearZ - eps && std::fabs(x) <= g * z + eps && std::fabs(y) <= g * z + eps;
    };
    int failures = 0, clipped = 0;
    ClipVertex out[gfx::kClipMaxOut];
    for (int n = 0; n < 4000; ++n) {
        ClipVertex v[3];
        for (ClipVertex& c : v) {
            c = {rnd(0, 1), rnd(0, 1), 0xFF808080u, rnd(-80, 80), rnd(-80, 80), rnd(-40, 90)};
        }
        const unsigned m = gfx::outcode(pl, v[0]) | gfx::outcode(pl, v[1]) | gfx::outcode(pl, v[2]);
        if (!m) continue;
        const int k = gfx::clipTriangle(v[0], v[1], v[2], pl, m, out);
        ++clipped;
        auto cross = [](const ClipVertex& a, const ClipVertex& b, const ClipVertex& c, float* r) {
            const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, wx = c.x - a.x, wy = c.y - a.y, wz = c.z - a.z;
            r[0] = uy * wz - uz * wy;
            r[1] = uz * wx - ux * wz;
            r[2] = ux * wy - uy * wx;
        };
        float nrm[3];
        cross(v[0], v[1], v[2], nrm);
        for (int i = 0; i < k; ++i)
            if (!inside(out[i].x, out[i].y, out[i].z, 1e-3f * (1.0f + std::fabs(out[i].z)))) {
                if (failures++ < 5) std::printf("  FAIL piece vertex outside: %.3f %.3f %.3f\n", out[i].x, out[i].y, out[i].z);
            }
        for (int i = 0; i + 2 < k; i += 3) {
            float c[3];
            cross(out[i], out[i + 1], out[i + 2], c);
            if (c[0] * nrm[0] + c[1] * nrm[1] + c[2] * nrm[2] < -1e-3f) {
                if (failures++ < 5) std::printf("  FAIL piece winding flipped\n");
            }
        }
        // Coverage: points of the triangle inside the planes lie in a piece.
        const float area2 = nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2];
        if (area2 < 1e-2f) continue;
        for (int sIdx = 0; sIdx < 40; ++sIdx) {
            float a = rnd(0, 1), b = rnd(0, 1);
            if (a + b > 1) a = 1 - a, b = 1 - b;
            const float px = v[0].x + (v[1].x - v[0].x) * a + (v[2].x - v[0].x) * b;
            const float py = v[0].y + (v[1].y - v[0].y) * a + (v[2].y - v[0].y) * b;
            const float pz = v[0].z + (v[1].z - v[0].z) * a + (v[2].z - v[0].z) * b;
            if (!inside(px, py, pz, -1e-2f)) continue;
            bool found = false;
            for (int i = 0; i + 2 < k && !found; i += 3) {
                // Barycentric test in the triangle's plane (signed areas against nrm).
                const ClipVertex P{0, 0, 0, px, py, pz};
                float c0[3], c1[3], c2[3];
                cross(out[i], out[i + 1], P, c0);
                cross(out[i + 1], out[i + 2], P, c1);
                cross(out[i + 2], out[i], P, c2);
                const float d0 = c0[0] * nrm[0] + c0[1] * nrm[1] + c0[2] * nrm[2];
                const float d1 = c1[0] * nrm[0] + c1[1] * nrm[1] + c1[2] * nrm[2];
                const float d2 = c2[0] * nrm[0] + c2[1] * nrm[1] + c2[2] * nrm[2];
                const float tol = -1e-4f * area2;
                found = d0 >= tol && d1 >= tol && d2 >= tol;
            }
            if (!found && failures++ < 5) std::printf("  FAIL visible point lost: %.2f %.2f %.2f\n", px, py, pz);
        }
    }
    std::printf("  %d triangles crossed a plane\n", clipped);
    // FTR2 keeps positions as fractions of a per-axis scale: planes scaled
    // by it give the same outcodes for the stored fractions as the plain
    // planes for metres (render/terrain_mesh.cpp clips in those units).
    const float scale[3] = {1580.0f, 410.0f, 1210.0f};
    gfx::ClipPlanes scaled = pl;
    gfx::scaleClipPlanes(scaled, scale);
    int mismatch = 0;
    for (int n = 0; n < 4000; ++n) {
        const float x = rnd(-80, 80), y = rnd(-80, 80), z = rnd(-40, 90);
        if (gfx::outcode(pl, x, y, z) != gfx::outcode(scaled, x / scale[0], y / scale[1], z / scale[2])) ++mismatch;
    }
    if (mismatch > 2) {  // points within float rounding of a plane may land either side
        std::printf("  FAIL scaled planes disagree on %d points\n", mismatch);
        ++failures;
    }
    return failures;
}

int main() {
    int failures = 0;
    std::printf("GE 24-bit matrix precision, aircraft 20 m from the camera:\n");
    for (float d : {1000.0f, 8000.0f, 20000.0f, 60000.0f}) {
        float world, rel;
        measure(d, world, rel);
        // At 20 m with a 60 deg FOV one pixel is ~0.048 m.
        std::printf("  %6.0f m from origin: world-space error %.3f m (%.1f px), camera-relative %.5f m\n", d, world,
                    world / 0.048f, rel);
        if (rel > 0.002f) {
            std::printf("  FAIL camera-relative error too large\n");
            ++failures;
        }
        if (d >= 8000.0f && world < 0.1f) {
            std::printf("  FAIL expected the world-space path to show the problem\n");
            ++failures;
        }
    }
    failures += testGuardClip();
    std::printf(failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures);
    return failures ? 1 : 0;
}
