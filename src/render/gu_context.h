#pragma once

#include <cstdint>

namespace gfx {

constexpr int kScreenW = 480;
constexpr int kScreenH = 272;
constexpr int kBufferW = 512;  // VRAM stride must be a power of two

void init();

// Opens the frame's display list and clears colour + depth.
void beginFrame(uint32_t clearColor);

// Submits the display list, waits for the GE, then flips on vblank.
// `overlay` runs after the GE finished and before the flip, so it may
// write straight into the finished frame (debug text).
void endFrame(void (*overlay)(void* drawBuffer) = nullptr);
// The same in two halves, so the caller can time how long the CPU waits for
// the GE (the frame-time breakdown in the debug corner, docs/workflow.md).
void submitFrame();                                  // finish the list, wait for the GE
// The two halves of submitFrame, for overlapping the next frame's simulation
// with the GE drawing this one (src/main.cpp).
void kickFrame();                                    // finish the list; the GE goes on drawing it
void waitFrame();                                    // wait until the GE has finished it
// Profiling (debug corner): finish the list so far, wait for the GE to draw it
// and go on in a fresh list - the time to here then includes the GE's work.
void profileSync();
void presentFrame(void (*overlay)(void* drawBuffer));  // overlay, vblank, flip

// Vertical sky gradient over the whole screen (no depth write).
void drawBackgroundGradient(uint32_t top, uint32_t bottom);

}  // namespace gfx
