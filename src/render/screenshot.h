#pragma once

namespace gfx {

// Writes the finished frame (VRAM-relative draw buffer, 8888) to
// screenshots/shot_NNN.bmp next to the EBOOT. Returns false on I/O failure.
bool saveScreenshot(void* drawBuffer);

}  // namespace gfx
