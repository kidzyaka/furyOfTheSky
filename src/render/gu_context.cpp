#include "render/gu_context.h"

#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>

namespace gfx {
namespace {

// The GE reads commands straight from RAM, so the list must stay alive
// for the whole frame and be 16-byte aligned.
alignas(16) unsigned int g_list[256 * 1024];

void* g_drawBuffer = nullptr;

// VRAM is a flat 2 MB region; buffers are carved out of it front to back.
// The GU wants VRAM-relative offsets, not absolute addresses.
unsigned int g_vramTop = 0;

void* allocVram(unsigned int bytes) {
    void* offset = reinterpret_cast<void*>(g_vramTop);
    g_vramTop += bytes;
    return offset;
}

struct Vertex2D {
    uint32_t color;
    int16_t x, y, z;
};

}  // namespace

void init() {
    void* fb0 = allocVram(kBufferW * kScreenH * 4);  // 32-bit colour
    void* fb1 = allocVram(kBufferW * kScreenH * 4);
    void* zb = allocVram(kBufferW * kScreenH * 2);   // 16-bit depth
    g_drawBuffer = fb0;

    sceGuInit();
    sceGuStart(GU_DIRECT, g_list);
    sceGuDrawBuffer(GU_PSM_8888, fb0, kBufferW);
    sceGuDispBuffer(kScreenW, kScreenH, fb1, kBufferW);
    sceGuDepthBuffer(zb, kBufferW);

    // The GE rasterises in a 4096x4096 virtual space; centre the screen in it.
    sceGuOffset(2048 - kScreenW / 2, 2048 - kScreenH / 2);
    sceGuViewport(2048, 2048, kScreenW, kScreenH);
    sceGuScissor(0, 0, kScreenW, kScreenH);
    sceGuEnable(GU_SCISSOR_TEST);

    // Reversed depth (near = 65535) with GEQUAL is the idiomatic PSP setup.
    sceGuDepthRange(65535, 0);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuShadeModel(GU_SMOOTH);
    // Meshes are counter-clockwise seen from outside (right-handed, as Blender
    // writes them). Verified in PPSSPP's software renderer: GU_CW shows the
    // aircraft inside-out (docs/psp/gotchas.md #15).
    sceGuFrontFace(GU_CCW);
    sceGuEnable(GU_CULL_FACE);

    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

void beginFrame(uint32_t clearColor) {
    sceGuStart(GU_DIRECT, g_list);
    sceGuClearColor(clearColor);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

void endFrame(void (*overlay)(void* drawBuffer)) {
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    if (overlay) overlay(g_drawBuffer);
    sceDisplayWaitVblankStart();
    g_drawBuffer = sceGuSwapBuffers();
}

void drawBackgroundGradient(uint32_t top, uint32_t bottom) {
    // Per-frame geometry must live in the display list, not on the stack:
    // the GE reads it after this function has returned.
    auto* v = static_cast<Vertex2D*>(sceGuGetMemory(4 * sizeof(Vertex2D)));
    v[0] = {top, 0, 0, 0};
    v[1] = {top, kScreenW, 0, 0};
    v[2] = {bottom, 0, kScreenH, 0};
    v[3] = {bottom, kScreenW, kScreenH, 0};

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);  // GU_TRUE = writes *disabled*
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_CULL_FACE);  // the strip alternates winding
    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                   4, nullptr, v);
    sceGuEnable(GU_CULL_FACE);
    sceGuDepthMask(GU_FALSE);
    sceGuEnable(GU_DEPTH_TEST);
}

}  // namespace gfx
