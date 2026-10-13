// Why rendering is camera-relative (docs/decisions/0013).
// pspgu sends every matrix element to the GE as a 24-bit float: it shifts the
// IEEE bits right by 8 (verified in libpspgu.a: sceGuSetMatrix, `srl v0,v0,0x8`),
// leaving 15 explicit mantissa bits. This test reproduces that truncation.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

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
    std::printf(failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures);
    return failures ? 1 : 0;
}
