#pragma once

namespace platform {

// One frame of pad state. Button masks are PSP_CTRL_* from <pspctrl.h>.
struct Pad {
    float lx = 0.0f;  // -1..1, +1 = right (dead zone applied)
    float ly = 0.0f;  // -1..1, +1 = stick pulled back / down
    unsigned held = 0;
    unsigned pressed = 0;  // went down this frame

    bool down(unsigned b) const { return (held & b) != 0; }
    bool hit(unsigned b) const { return (pressed & b) != 0; }
};

void initInput();
Pad readPad();

}  // namespace platform
