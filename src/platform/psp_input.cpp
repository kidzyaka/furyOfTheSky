#include "platform/psp_input.h"

#include <pspctrl.h>

#include <cmath>

namespace platform {
namespace {

unsigned g_prev = 0;

// The PSP nub is 8-bit with a sloppy centre (often 110..145 at rest), so a
// generous dead zone, rescaled so the response still starts from zero.
float axis(unsigned char raw) {
    constexpr float kDead = 0.22f;
    float v = (static_cast<int>(raw) - 128) / 127.0f;
    if (std::fabs(v) < kDead) return 0.0f;
    v = (v > 0 ? v - kDead : v + kDead) / (1.0f - kDead);
    return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
}

}  // namespace

void initInput() {
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}

Pad readPad() {
    SceCtrlData data;
    sceCtrlPeekBufferPositive(&data, 1);
    Pad pad;
    pad.lx = axis(data.Lx);
    pad.ly = axis(data.Ly);
    pad.held = data.Buttons;
    pad.pressed = data.Buttons & ~g_prev;
    g_prev = data.Buttons;
    return pad;
}

}  // namespace platform
