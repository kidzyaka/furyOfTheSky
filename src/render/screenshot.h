#pragma once

#include <cstdint>

namespace gfx {

// Writes the finished frame (VRAM-relative draw buffer, 8888) to
// screenshots/shot_NNN.bmp next to the EBOOT. Returns false on I/O failure.
bool saveScreenshot(void* drawBuffer);

// One pixel (ABGR) of the finished frame, read through the uncached VRAM
// mirror. Only valid after the GE has finished (endFrame's overlay hook).
uint32_t readPixel(void* drawBuffer, int x, int y);

}  // namespace gfx
