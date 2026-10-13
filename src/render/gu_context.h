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

// Vertical sky gradient over the whole screen (no depth write).
void drawBackgroundGradient(uint32_t top, uint32_t bottom);

}  // namespace gfx
