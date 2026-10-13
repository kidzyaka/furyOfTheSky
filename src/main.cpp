// Fury of the Sky - entry point: boot, scene switching, pause menu, HUD overlay.
// Architecture: docs/architecture.md.

#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>

#include <cstdio>

#include "core/build_info.h"
#include "game/world.h"
#include "platform/psp_input.h"
#include "platform/psp_system.h"
#include "render/gu_context.h"
#include "render/screenshot.h"
#include "render/view.h"
#include "scenes/flight_scene.h"
#include "scenes/hangar_scene.h"

PSP_MODULE_INFO("FuryOfTheSky", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);  // everything except 1 MB left for the kernel

namespace {

using scenes::Scene;

enum MenuItem { kResume, kScene, kLivery, kInvert, kRestart, kMenuCount };

struct App {
    scenes::Assets assets;
    scenes::Settings settings;
    Scene* scenes[2] = {};
    int scene = 0;
    bool paused = false;
    int menuCursor = 0;
    float fps = 0.0f;
    bool clock333 = false;
    bool wantShot = false;
};

App g_app;

Scene& current() { return *g_app.scenes[g_app.scene]; }

void switchScene(int index) {
    g_app.scene = index;
    current().enter();
}

void updateMenu(const platform::Pad& pad) {
    if (pad.hit(PSP_CTRL_UP)) g_app.menuCursor = (g_app.menuCursor + kMenuCount - 1) % kMenuCount;
    if (pad.hit(PSP_CTRL_DOWN)) g_app.menuCursor = (g_app.menuCursor + 1) % kMenuCount;
    bool activate = pad.hit(PSP_CTRL_CROSS) || pad.hit(PSP_CTRL_LEFT) || pad.hit(PSP_CTRL_RIGHT);
    if (!activate) return;
    switch (g_app.menuCursor) {
        case kResume: g_app.paused = false; break;
        case kScene: switchScene(1 - g_app.scene); g_app.paused = false; break;
        case kLivery: g_app.settings.livery = (g_app.settings.livery + 1) % scenes::kLiveryCount; break;
        case kInvert: g_app.settings.invertPitch = !g_app.settings.invertPitch; break;
        case kRestart: switchScene(g_app.scene); g_app.paused = false; break;
    }
}

void drawMenu() {
    const char* labels[kMenuCount];
    char scene[40], livery[40], invert[40];
    std::snprintf(scene, sizeof scene, "Scene:  %s", g_app.scenes[1 - g_app.scene]->name());
    std::snprintf(livery, sizeof livery, "Livery: %s", scenes::kLiveryNames[g_app.settings.livery]);
    std::snprintf(invert, sizeof invert, "Pitch:  %s", g_app.settings.invertPitch ? "inverted" : "normal");
    labels[kResume] = "Resume";
    labels[kScene] = scene;
    labels[kLivery] = livery;
    labels[kInvert] = invert;
    labels[kRestart] = "Restart";
    pspDebugScreenSetXY(22, 12);
    pspDebugScreenSetTextColor(0xFF40C0FF);
    pspDebugScreenPrintf("=== PAUSED ===");
    for (int i = 0; i < kMenuCount; ++i) {
        pspDebugScreenSetXY(18, 14 + i);
        pspDebugScreenSetTextColor(i == g_app.menuCursor ? 0xFF40FFFF : 0xFFFFFFFF);
        pspDebugScreenPrintf("%s %s", i == g_app.menuCursor ? ">" : " ", labels[i]);
    }
}

void overlay(void* drawBuffer) {
    pspDebugScreenSetOffset(reinterpret_cast<int>(drawBuffer));
    if (g_app.assets.error) {
        pspDebugScreenSetXY(0, 0);
        pspDebugScreenSetTextColor(0xFF4040FF);
        pspDebugScreenPrintf("ERROR: cannot load %s", g_app.assets.error);
        return;
    }
    current().overlay();
    pspDebugScreenSetTextColor(g_app.clock333 ? 0xFFA0A0A0 : 0xFF4040FF);
    pspDebugScreenSetXY(51, 1);
    pspDebugScreenPrintf("%4.1f fps %s", g_app.fps, g_app.clock333 ? "333" : "222!");
    pspDebugScreenSetTextColor(0xFFA0A0A0);
    pspDebugScreenSetXY(51, 2);
    pspDebugScreenPrintf("%.16s", core::kBuildId);
    if (g_app.paused) drawMenu();
    if (g_app.wantShot) {  // after the text, so the HUD is in the picture
        g_app.wantShot = false;
        gfx::saveScreenshot(drawBuffer);
    }
}

bool fileExists(const char* path) {
    if (FILE* f = std::fopen(path, "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

}  // namespace

int main() {
    platform::init();
    g_app.clock333 = platform::setMaxClock();
    platform::initInput();
    gfx::init();
    pspDebugScreenInitEx(nullptr, PSP_DISPLAY_PIXEL_FORMAT_8888, 0);
    pspDebugScreenEnableBackColor(0);

    g_app.assets.load();
    // Scenes hold sizeable static geometry (ocean grid, sky dome): keep them off the stack.
    static scenes::FlightScene flight(g_app.assets, g_app.settings);
    static scenes::HangarScene hangar(g_app.assets, g_app.settings);
    g_app.scenes[0] = &flight;
    g_app.scenes[1] = &hangar;
    switchScene(0);

    // Smoke-test hook (docs/psp/emulator.md): with an "autoshot" file beside the
    // EBOOT, fly a scripted sortie, capture a few frames, visit the hangar, quit.
    const bool autoshot = fileExists("autoshot");
    flight.setAutopilot(autoshot);
    int frame = 0;

    const uint32_t clearColor = gfx::abgr(game::world::kHazeRGB[0], game::world::kHazeRGB[1],
                                          game::world::kHazeRGB[2]);
    double last = platform::now(), fpsTimer = last;
    int fpsFrames = 0;

    while (platform::running()) {
        double now = platform::now();
        float dt = static_cast<float>(now - last);
        last = now;
        if (++fpsFrames, now - fpsTimer >= 0.5) {
            g_app.fps = static_cast<float>(fpsFrames / (now - fpsTimer));
            fpsFrames = 0;
            fpsTimer = now;
        }

        platform::Pad pad = platform::readPad();
        if (autoshot) {
            pad = platform::Pad{};  // the script flies; the pad is ignored
            ++frame;
            if (frame == 100 || frame == 220 || frame == 340 || frame == 420) g_app.wantShot = true;
            if (frame == 350) switchScene(1);
            if (frame == 430) break;
        }
        if (pad.hit(PSP_CTRL_SELECT)) g_app.wantShot = true;
        if (pad.hit(PSP_CTRL_START)) {
            g_app.paused = !g_app.paused;
            g_app.menuCursor = 0;
        }

        if (!g_app.assets.error) {
            if (g_app.paused) updateMenu(pad);
            else current().update(pad, dt);
        }

        gfx::beginFrame(clearColor);
        if (!g_app.assets.error) current().render();
        gfx::endFrame(overlay);
    }

    sceGuTerm();
    platform::shutdown();
}
