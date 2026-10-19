// Fury of the Sky - entry point: boot, menu <-> sortie, pause menu, debug corner.
// Architecture: docs/architecture.md.

#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspkernel.h>

#include <cstdio>
#include <cstring>

#include "audio/mixer.h"
#include "core/build_info.h"
#include "game/world.h"
#include "meta/profile.h"
#include "platform/psp_audio.h"
#include "platform/psp_input.h"
#include "platform/psp_save.h"
#include "platform/psp_system.h"
#include "render/draw2d.h"
#include "render/gu_context.h"
#include "render/screenshot.h"
#include "scenes/flight_scene.h"
#include "scenes/menu/menu_scene.h"
#include "ui/anim.h"
#include "ui/ui.h"

PSP_MODULE_INFO("FuryOfTheSky", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);  // everything except 1 MB left for the kernel

namespace {

enum PauseItem { kResume, kRestart, kQuit, kPauseCount };

struct App {
    scenes::Assets assets;
    meta::Profile profile;
    menu::MenuScene* menu = nullptr;
    scenes::FlightScene* flight = nullptr;
    bool inFlight = false;
    meta::SortieConfig sortie;
    bool paused = false;
    int pauseCursor = 0;
    float pauseFocus[kPauseCount] = {};
    float fps = 0.0f;
    bool clock333 = false;
    bool wantShot = false;
    bool sound = false;  // audio thread running
    const char* error = nullptr;
    // Frame-time breakdown, ms, averaged over the FPS window: simulation,
    // building the display list, CPU idle waiting for the GE to finish.
    float simMs = 0.0f, listMs = 0.0f, geWaitMs = 0.0f;
    uint8_t volumes[audio::kVolumeGroupCount] = {255, 255, 255};
};

App g_app;

scenes::Scene& current() {
    return g_app.inFlight ? static_cast<scenes::Scene&>(*g_app.flight) : static_cast<scenes::Scene&>(*g_app.menu);
}

// Master volumes from the settings, sent only when they change.
void applyVolumes() {
    const meta::Settings& s = g_app.profile.settings;
    const uint8_t want[audio::kVolumeGroupCount] = {s.sfxVolume, s.voiceVolume, s.uiVolume};
    for (int g = 0; g < audio::kVolumeGroupCount; ++g)
        if (want[g] != g_app.volumes[g]) {
            g_app.volumes[g] = want[g];
            audio::mixer().setVolume(static_cast<audio::VolumeGroup>(g), audio::volumeGain(want[g]));
        }
}

void uiSound(const audio::Sound& s) { audio::mixer().play(audio::kUi, &s, 0.6f); }

void setPaused(bool paused) {
    g_app.paused = paused;
    g_app.pauseCursor = 0;
    // The world holds its breath under the menu; menu clicks stay audible.
    audio::mixer().setWorldGain(paused ? 0.0f : 1.0f);
}

void startSortie(const meta::SortieConfig& s) {
    g_app.sortie = s;
    g_app.menu->leave();
    g_app.flight->configure(s, g_app.profile.settings);
    g_app.inFlight = true;
    g_app.flight->enter();
}

void endSortie() {
    const meta::SortieResult r = g_app.flight->result();
    g_app.flight->leave();
    setPaused(false);
    g_app.inFlight = false;
    g_app.menu->enter();
    g_app.menu->sortieReturned(r);
}

void updatePause(const platform::Pad& pad) {
    const int before = g_app.pauseCursor;
    if (pad.hit(PSP_CTRL_UP)) g_app.pauseCursor = (g_app.pauseCursor + kPauseCount - 1) % kPauseCount;
    if (pad.hit(PSP_CTRL_DOWN)) g_app.pauseCursor = (g_app.pauseCursor + 1) % kPauseCount;
    if (g_app.pauseCursor != before) uiSound(g_app.assets.uiMove);
    if (pad.hit(PSP_CTRL_CIRCLE)) {
        setPaused(false);
        uiSound(g_app.assets.uiMove);
        return;
    }
    if (!pad.hit(PSP_CTRL_CROSS)) return;
    uiSound(g_app.assets.uiSelect);
    switch (g_app.pauseCursor) {
        case kResume: setPaused(false); break;
        case kRestart:
            g_app.flight->restart();
            setPaused(false);
            break;
        case kQuit: g_app.flight->abort(); break;  // the loop sees finished() and returns to the menu
    }
}

const char* sortieTitle(const meta::SortieConfig& s) {
    switch (s.mode) {
        case meta::SortieMode::FreeFlight: return "FREE FLIGHT";
        case meta::SortieMode::TargetPractice: return "TARGET PRACTICE";
        case meta::SortieMode::Dogfight: return "DOGFIGHT";
        case meta::SortieMode::Mission: return meta::location(s.location).missions[s.mission].name;
    }
    return "";
}

// Pause menu in the menus' metal style (docs/decisions/0035).
void drawPause(float dt) {
    for (int i = 0; i < kPauseCount; ++i)
        g_app.pauseFocus[i] = ui::approach(g_app.pauseFocus[i], i == g_app.pauseCursor ? 1.0f : 0.0f, 16.0f, dt);
    const bool mission = g_app.sortie.mode == meta::SortieMode::Mission;
    const char* labels[kPauseCount] = {"RESUME", mission ? "RESTART MISSION" : "RESTART",
                                       mission ? "ABORT MISSION" : "QUIT TO MENU"};
    gfx::begin2D();
    ui::dimScreen(0.5f);
    ui::screenTitle("PAUSED", sortieTitle(g_app.sortie));
    constexpr int x = ui::kMargin, w = 220;
    for (int i = 0; i < kPauseCount; ++i) {
        ui::ButtonLook look;
        look.focus = g_app.pauseFocus[i];
        ui::button(x, 66 + i * 36, w, ui::ButtonKind::Main, labels[i], look);
    }
    // Controls (docs/decisions/0030), big enough to read on the PSP.
    constexpr int px = 252, pw = gfx::kScreenW - ui::kMargin - px;
    ui::panel(px, 60, pw, 168);
    ui::text(px + 12, 70, "CONTROLS", ui::Face::Label, ui::kAccent);
    const char* lines[] = {"R  THRUST / AFTERBURNER", "L  BRAKE,  L + PULL  PSM", "X  GUN      O  MISSILE",
                           "^  NEXT TARGET", "SELECT  MISSILE TYPE", "[]  FLARES    < >  YAW"};
    for (int i = 0; i < 6; ++i) ui::text(px + 12, 94 + i * 21, lines[i], ui::Face::Body, ui::kText);
    const ui::Hint hints[] = {ui::kHintSelect, {&ui::icons::kBtnCircle, "RESUME"}};
    ui::bottomBar(hints, 2);
    gfx::end2D();
}

void overlay(void* drawBuffer) {
    pspDebugScreenSetOffset(reinterpret_cast<int>(drawBuffer));
    if (g_app.error) {
        pspDebugScreenSetXY(0, 0);
        pspDebugScreenSetTextColor(0xFF4040FF);
        pspDebugScreenPrintf("ERROR: cannot load %s", g_app.error);
        return;
    }
    current().overlay(drawBuffer);
    // Debug corner: build id always (photos from the PSP must say which build
    // ran, docs/workflow.md); frame rate and timings if enabled in SETTINGS.
    // In the menus it is one line in the top 8 px, above every screen's title
    // and the pilot card.
    const bool debug = g_app.profile.settings.debugInfo;
    if (g_app.flight) g_app.flight->setProfiling(debug);
    if (g_app.inFlight) {
        // The corner costs time itself (float printf, ~300 characters drawn by
        // the CPU into VRAM): format every 15th frame, print the strings each
        // frame (the buffers alternate).
        static char lines[6][64];
        static int refresh = 0;
        if (debug && (--refresh <= 0 || !lines[0][0])) {
            refresh = 15;
            std::snprintf(lines[0], sizeof lines[0], "%4.1f fps %s%s", g_app.fps, g_app.clock333 ? "333" : "222!",
                          g_app.sound ? "" : " MUTE");
            // Where the 16.7 ms go: SIM + GFX over budget = CPU-bound; a large GE
            // wait = the GE is the bottleneck (fill rate / vertices). The next
            // frame's SIM runs while the GE draws: G is what is left after it.
            std::snprintf(lines[1], sizeof lines[1], "S%4.1f L%4.1f G%4.1f ms", g_app.simMs, g_app.listMs,
                          g_app.geWaitMs);
            lines[2][0] = lines[3][0] = lines[4][0] = 0;
            if (g_app.flight) {
                // Rocks and islands this frame: triangles, indices sent (each is
                // a vertex the GE transforms and lights), triangles clipped in
                // software next to the camera (docs/decisions/0042).
                const gfx::TerrainStats& t = g_app.flight->rockStats();
                std::snprintf(lines[2], sizeof lines[2], "T%5d V%5d c%3d", t.triangles, t.vertices, t.clipped);
                // CPU ms per part of the list: sea, rocks, aircraft, clouds
                // (+ quads drawn), effects, HUD.
                const scenes::FlightScene::FrameTimes& f = g_app.flight->frameTimes();
                std::snprintf(lines[3], sizeof lines[3], "O%3.1f R%3.1f A%3.1f C%3.1f/%3d F%3.1f H%3.1f", f.sea,
                              f.rocks, f.aircraft, f.clouds, f.cloudQuads, f.fx, f.hud);
                // Every 64th frame drawn part by part, waiting for the GE: CPU
                // and GE together. Much bigger than the line above = that part
                // is GE-bound (vertices, fill).
                const scenes::FlightScene::FrameTimes& z = g_app.flight->serialTimes();
                std::snprintf(lines[4], sizeof lines[4], "Z O%3.1f R%3.1f A%3.1f C%3.1f F%3.1f H%3.1f", z.sea,
                              z.rocks, z.aircraft, z.clouds, z.fx, z.hud);
            }
        }
        if (debug) {
            pspDebugScreenSetTextColor(g_app.clock333 ? 0xFFA0A0A0 : 0xFF4040FF);
            pspDebugScreenSetXY(51, 1);
            pspDebugScreenPuts(lines[0]);
        }
        pspDebugScreenSetTextColor(0xFFA0A0A0);
        pspDebugScreenSetXY(51, 2);
        pspDebugScreenPrintf("%.16s", core::kBuildId);
        if (debug) {
            const int col[5] = {0, 45, 41, 23, 23};
            for (int k = 1; k < 5; ++k) {
                if (!lines[k][0]) continue;
                pspDebugScreenSetXY(col[k], k + 2);
                pspDebugScreenPuts(lines[k]);
            }
        }
    } else {
        char line[64];
        if (debug)
            std::snprintf(line, sizeof line, "%4.1f fps %s%s  %.16s", g_app.fps, g_app.clock333 ? "333" : "222!",
                          g_app.sound ? "" : " MUTE", core::kBuildId);
        else
            std::snprintf(line, sizeof line, "%.16s", core::kBuildId);
        const int len = static_cast<int>(std::strlen(line));
        pspDebugScreenSetTextColor(g_app.clock333 ? 0xFF909890 : 0xFF4040FF);
        pspDebugScreenSetXY(60 - len, 0);
        pspDebugScreenPrintf("%s", line);
    }
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

void loadProfile() {
    if (platform::loadProfile(g_app.profile)) return;
    // First run: the pilot takes the console's nickname (XMB settings).
    char nick[128];
    platform::systemNickname(nick, sizeof nick);
    meta::resetProfile(g_app.profile, nick);
    platform::saveProfile(g_app.profile);
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
    g_app.error = g_app.assets.error;
    if (!g_app.error && !ui::res().load()) g_app.error = ui::res().error;
    loadProfile();
    // After the assets: the mixer must never see a half-loaded clip. Without
    // a channel the game simply runs silent.
    g_app.sound = !g_app.error && platform::initAudio();
    applyVolumes();
    // Scenes hold sizeable static geometry (ocean grid, sky dome): keep them off the stack.
    static scenes::FlightScene flight(g_app.assets);
    static menu::MenuScene menuScene(g_app.assets, g_app.profile);
    g_app.flight = &flight;
    g_app.menu = &menuScene;
    menuScene.enter();

    // Smoke-test hook (docs/psp/emulator.md): with an "autoshot" file beside the
    // EBOOT, tour every menu screen (screenshots), then fly the scripted
    // sortie (it asks for its own screenshots) and quit.
    const bool autoshot = fileExists("autoshot");
    if (autoshot) menuScene.startTour();

    double last = platform::now(), fpsTimer = last;
    int fpsFrames = 0;
    double simAcc = 0.0, listAcc = 0.0, waitAcc = 0.0;
    // The frame pipeline: the next frame is simulated while the GE draws the
    // last one; only then do we wait for it, show it and build the next list.
    // A screenshot asked for at the start of an iteration is of the frame
    // built in it - shown (and saved) one iteration later.
    bool frameInFlight = false, shotTaken = false, shotPending = false;

    while (platform::running()) {
        double now = platform::now();
        float dt = static_cast<float>(now - last);
        last = now;
        if (++fpsFrames, now - fpsTimer >= 0.5) {
            g_app.fps = static_cast<float>(fpsFrames / (now - fpsTimer));
            g_app.simMs = static_cast<float>(simAcc * 1000.0 / fpsFrames);
            g_app.listMs = static_cast<float>(listAcc * 1000.0 / fpsFrames);
            g_app.geWaitMs = static_cast<float>(waitAcc * 1000.0 / fpsFrames);
            simAcc = listAcc = waitAcc = 0.0;
            fpsFrames = 0;
            fpsTimer = now;
        }

        platform::Pad pad = platform::readPad();
        if (autoshot) {
            pad = platform::Pad{};  // the scripts drive; the pad is ignored
            if (!g_app.inFlight) {
                if (menuScene.takeShotRequest()) shotTaken = true;
                if (menuScene.tourDone()) {
                    meta::SortieConfig s;
                    s.mode = meta::SortieMode::TargetPractice;
                    s.enemy = game::BotMode::Straight;
                    flight.setAutopilot(true);
                    startSortie(s);
                }
            } else {
                if (flight.takeShotRequest()) shotTaken = true;
                if (flight.scriptDone()) break;
            }
        }
        // D-pad up shoots in flight (docs/decisions/0030); the menus need it.
        if (g_app.inFlight && pad.hit(PSP_CTRL_UP) && !g_app.paused) shotTaken = true;
        if (g_app.inFlight && pad.hit(PSP_CTRL_START) && !flight.finished()) {
            setPaused(!g_app.paused);
            uiSound(g_app.paused ? g_app.assets.uiSelect : g_app.assets.uiMove);
        }

        const double t0 = platform::now();
        if (!g_app.error) {
            if (!g_app.inFlight) {
                menuScene.update(pad, dt);
                meta::SortieConfig s;
                if (menuScene.takeLaunch(s)) startSortie(s);
            } else if (g_app.paused) {
                updatePause(pad);
            } else {
                flight.update(pad, dt);
            }
            if (g_app.inFlight && flight.finished() && !autoshot) endSortie();
            applyVolumes();
        }
        const double t1 = platform::now();
        simAcc += t1 - t0;

        // The last frame: the GE has been drawing it while we simulated.
        if (frameInFlight) {
            gfx::waitFrame();
            waitAcc += platform::now() - t1;
            g_app.wantShot = shotPending;
            gfx::presentFrame(overlay);
        }

        // In flight the clear colour is the haze: the sky below the horizon melts into it.
        const double t2 = platform::now();
        gfx::beginFrame(g_app.inFlight ? flight.clearColor() : gfx::abgr(20, 24, 22));
        if (!g_app.error) {
            current().render();
            if (g_app.inFlight && g_app.paused) drawPause(dt);
        }
        listAcc += platform::now() - t2;
        gfx::kickFrame();
        frameInFlight = true;
        shotPending = shotTaken;
        shotTaken = false;
    }
    if (frameInFlight) gfx::waitFrame();

    platform::shutdownAudio();
    sceGuTerm();
    platform::shutdown();
}
