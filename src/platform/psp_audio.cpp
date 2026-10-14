#include "platform/psp_audio.h"

#include <pspaudio.h>
#include <pspkernel.h>

#include "audio/mixer.h"

namespace platform {
namespace {

constexpr int kBlock = 1024;  // frames: 23 ms at 44.1 kHz (sceAudio wants a multiple of 64)

// Two buffers: one is being played by the hardware while the other is mixed.
// 64-byte aligned for the audio DMA.
int16_t g_buffers[2][kBlock * 2] __attribute__((aligned(64)));
int g_channel = -1;
int g_thread = -1;
volatile bool g_run = false;

int audioThread(SceSize, void*) {
    int which = 0;
    while (g_run) {
        int16_t* buf = g_buffers[which];
        audio::mixer().mix(buf, kBlock);
        // Cheap insurance: the block must be in RAM, not dirty in the D-cache,
        // whichever way the driver fetches it.
        sceKernelDcacheWritebackRange(buf, sizeof g_buffers[0]);
        // Blocks until the hardware has room for the block: this is the
        // thread's clock. The mixer's own gain handles volume, so full scale here.
        sceAudioOutputPannedBlocking(g_channel, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, buf);
        which ^= 1;
    }
    return 0;
}

}  // namespace

bool initAudio() {
    g_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, kBlock, PSP_AUDIO_FORMAT_STEREO);
    if (g_channel < 0) return false;
    // Above the game loop (0x20) so a slow frame never starves the output,
    // below the HOME-button callback thread (0x11). Mixing ~10 voices costs a
    // few percent of the CPU, so the game loop barely notices.
    g_thread = sceKernelCreateThread("audio", audioThread, 0x12, 0x4000, PSP_THREAD_ATTR_USER, nullptr);
    if (g_thread < 0) {
        sceAudioChRelease(g_channel);
        g_channel = -1;
        return false;
    }
    g_run = true;
    sceKernelStartThread(g_thread, 0, nullptr);
    return true;
}

void shutdownAudio() {
    if (g_thread < 0) return;
    g_run = false;
    sceKernelWaitThreadEnd(g_thread, nullptr);
    sceKernelDeleteThread(g_thread);
    sceAudioChRelease(g_channel);
    g_thread = g_channel = -1;
}

}  // namespace platform
