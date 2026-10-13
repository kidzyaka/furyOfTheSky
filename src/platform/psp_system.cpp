#include "platform/psp_system.h"

#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>

namespace platform {
namespace {

volatile bool g_running = true;

int exitCallback(int, int, void*) {
    g_running = false;
    return 0;
}

// The kernel delivers callbacks only to threads sleeping in a *CB wait,
// so a dedicated thread parks itself there for the whole session.
int callbackThread(SceSize, void*) {
    int cbid = sceKernelCreateCallback("exit_cb", exitCallback, nullptr);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

}  // namespace

void init() {
    int thid = sceKernelCreateThread("callbacks", callbackThread, 0x11, 0xFA0, 0, nullptr);
    if (thid >= 0) sceKernelStartThread(thid, 0, nullptr);
}

bool running() { return g_running; }

void shutdown() {
    sceKernelExitGame();
    for (;;) {}
}

double now() {
    static const double resolution = static_cast<double>(sceRtcGetTickResolution());
    u64 tick;
    sceRtcGetCurrentTick(&tick);
    return static_cast<double>(tick) / resolution;
}

bool setMaxClock() { return scePowerSetClockFrequency(333, 333, 166) >= 0; }

}  // namespace platform
