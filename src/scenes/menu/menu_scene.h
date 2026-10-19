#pragma once

#include "audio/sound.h"
#include "core/file.h"
#include "game/control_surfaces.h"
#include "math/vec3.h"
#include "render/mesh.h"
#include "render/texture.h"
#include "scenes/menu/menu.h"
#include "scenes/scene.h"

namespace menu {

// The live hangar behind the menus (docs/assets/hangar.md): the shelter with
// baked lighting, the last chosen aircraft on its gear, a soft floor shadow,
// a camera that glides between presets as screens change. Every so often the
// aircraft runs a control check (surfaces sweep) like on a pre-flight.
class Backdrop {
public:
    void load();  // hangar and gear meshes are optional: a gradient stands in
    void update(Shot shot, const Context& ctx, float dt);
    void render(const Context& ctx, const scenes::Assets& assets);
    bool hasHangar() const { return hasHangar_; }

private:
    void target(Shot shot, const Context& ctx, math::Vec3& eye, math::Vec3& at, float& fov) const;

    gfx::Mesh hangar_, floor_, gear_;
    gfx::Texture hangarTex_, floorTex_, gearTex_;
    bool hasHangar_ = false, hasFloor_ = false, hasGear_ = false;
    game::ControlSurfaces surfaces_;
    float checkTime_ = 0.0f;
    math::Vec3 eye_{}, at_{};
    float fov_ = 50.0f;
    bool snapped_ = false;
};

class MenuScene : public scenes::Scene {
public:
    MenuScene(scenes::Assets& assets, meta::Profile& profile);

    const char* name() const override { return "Menu"; }
    void enter() override;
    void leave() override;
    void update(const platform::Pad& pad, float dt) override;
    void render() override;

    // --- The app's side.
    bool takeLaunch(meta::SortieConfig& out);       // a screen asked for a sortie
    void sortieReturned(const meta::SortieResult& r);  // before switching back to the menu
    // Emulator smoke test (docs/psp/emulator.md): visit every screen, ask for
    // a screenshot on each, then report done.
    void startTour();
    bool tourDone() const { return tourStep_ < 0 && tourStarted_; }
    bool takeShotRequest() {
        bool r = shotRequest_;
        shotRequest_ = false;
        return r;
    }
    Context& context() { return ctx_; }

    // --- Context's side.
    void push(ScreenId id);
    void pop();
    void popTo(ScreenId id);
    void replace(ScreenId id);
    void sfx(Sfx s);
    void save();
    void launch(const meta::SortieConfig& sortie);
    void confirm(const char* question, void (*onYes)(Context&), const char* yes);

private:
    static constexpr int kMaxDepth = 10;
    Screen* screen(ScreenId id);
    Screen* top() { return depth_ > 0 ? screen(stack_[depth_ - 1]) : nullptr; }
    void tour(platform::Pad& pad);
    bool loadSound(audio::Sound& s, const char* name);

    scenes::Assets& assets_;
    Context ctx_;
    Backdrop backdrop_;
    ScreenId stack_[kMaxDepth];
    int depth_ = 0;
    // Transition between the outgoing and the incoming top screen.
    ScreenId from_ = ScreenId::Main;
    bool hasFrom_ = false;
    float trans_ = 1.0f;   // 0 -> 1
    int transDir_ = 1;     // +1 push (new comes from the right), -1 pop
    float dim_ = 0.0f;     // current backdrop darkening
    bool launchPending_ = false;
    meta::SortieConfig launch_;

    // Extra menu sounds (tools/audio/sfx.py); missing ones fall back to the two in Assets.
    audio::Sound back_, tab_, slide_, deny_, confirm_;
    bool hasBack_ = false, hasTab_ = false, hasSlide_ = false, hasDeny_ = false, hasConfirm_ = false;
    static constexpr int kSoundBlobs = 5;
    core::Blob soundBlobs_[kSoundBlobs];
    int soundBlobCount_ = 0;

    // Smoke-test tour.
    int tourStep_ = -1;
    int tourFrames_ = 0;
    bool tourStarted_ = false;
    bool shotRequest_ = false;
    bool tourDebriefed_ = false;  // the tour's first debriefing is a win, the second a loss
};

}  // namespace menu
