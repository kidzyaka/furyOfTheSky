#include "render/screenshot.h"

#include <pspge.h>
#include <pspiofilemgr.h>

#include <cstdint>
#include <cstdio>

#include "render/gu_context.h"

namespace gfx {
namespace {

void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (v >> (8 * i)) & 0xFF; }

int g_counter = 0;

}  // namespace

bool saveScreenshot(void* drawBuffer) {
    constexpr int kRow = kScreenW * 3;  // 1440, already a multiple of 4
    constexpr int kImage = kRow * kScreenH;

    sceIoMkdir("screenshots", 0777);
    char path[64];
    std::snprintf(path, sizeof path, "screenshots/shot_%03d.bmp", g_counter++);
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    uint8_t header[54] = {'B', 'M'};
    put32(header + 2, sizeof header + kImage);
    put32(header + 10, sizeof header);
    put32(header + 14, 40);
    put32(header + 18, kScreenW);
    put32(header + 22, kScreenH);  // positive height = bottom-up rows
    put16(header + 26, 1);
    put16(header + 28, 24);
    put32(header + 34, kImage);
    std::fwrite(header, 1, sizeof header, f);

    // Uncached mirror of VRAM: the GE wrote it, the CPU cache never saw it.
    auto* vram = reinterpret_cast<const uint32_t*>(
        0x40000000u | (reinterpret_cast<uintptr_t>(sceGeEdramGetAddr()) +
                       reinterpret_cast<uintptr_t>(drawBuffer)));
    static uint8_t row[kRow];
    for (int y = kScreenH - 1; y >= 0; --y) {
        const uint32_t* src = vram + y * kBufferW;
        for (int x = 0; x < kScreenW; ++x) {
            uint32_t c = src[x];  // ABGR
            row[x * 3 + 0] = (c >> 16) & 0xFF;
            row[x * 3 + 1] = (c >> 8) & 0xFF;
            row[x * 3 + 2] = c & 0xFF;
        }
        std::fwrite(row, 1, kRow, f);
    }
    return std::fclose(f) == 0;
}

}  // namespace gfx
