#include "scenes/menu/menu_scene.h"

#include <pspctrl.h>

#include <cmath>
#include <cstdio>

#include "audio/mixer.h"
#include "game/armament.h"
#include "game/world.h"
#include "platform/psp_save.h"
#include "render/draw2d.h"
#include "render/fx_draw.h"
#include "render/gu_context.h"
#include "render/view.h"
#include "scenes/menu/briefing_screen.h"
#include "scenes/menu/campaign_screen.h"
#include "scenes/menu/debrief_screen.h"
#include "scenes/menu/emblem_screen.h"
#include "scenes/menu/hangar_screen.h"
#include "scenes/menu/location_screen.h"
#include "scenes/menu/main_screen.h"
#include "scenes/menu/multiplayer_screen.h"
#include "scenes/menu/profile_screen.h"
#include "scenes/menu/settings_screen.h"
#include "scenes/menu/training_screen.h"
#include "ui/anim.h"
#include "ui/ui.h"

namespace menu {

// ---------------------------------------------------------------- Context

void Context::push(ScreenId id) { scene.push(id); }
void Context::pop() { scene.pop(); }
void Context::popTo(ScreenId id) { scene.popTo(id); }
void Context::replace(ScreenId id) { scene.replace(id); }
void Context::sfx(Sfx s) { scene.sfx(s); }
void Context::save() { scene.save(); }
void Context::launch(const meta::SortieConfig& s) { scene.launch(s); }
void Context::confirm(const char* question, void (*onYes)(Context&), const char* yes) {
    scene.confirm(question, onYes, yes);
}

namespace {

// ---------------------------------------------------------------- Confirm dialog

class ConfirmScreen : public Screen {
public:
    void set(const char* question, void (*onYes)(Context&), const char* yes) {
        question_ = question;
        onYes_ = onYes;
        yes_ = yes;
        cursor_ = 1;  // NO first: a stray X must not spend credits
    }
    void update(Context& ctx, const platform::Pad& pad, float) override {
        if (pad.hit(PSP_CTRL_UP) || pad.hit(PSP_CTRL_DOWN) || pad.hit(PSP_CTRL_LEFT) || pad.hit(PSP_CTRL_RIGHT)) {
            cursor_ = 1 - cursor_;
            ctx.sfx(Sfx::Move);
        }
        if (pad.hit(PSP_CTRL_CIRCLE) || (pad.hit(PSP_CTRL_CROSS) && cursor_ == 1)) {
            ctx.sfx(Sfx::Back);
            ctx.pop();
        } else if (pad.hit(PSP_CTRL_CROSS)) {
            ctx.sfx(Sfx::Confirm);
            ctx.pop();
            if (onYes_) onYes_(ctx);
        }
    }
    void draw(Context& ctx) override {
        for (int i = 0; i < 2; ++i) focus_[i] = ui::approach(focus_[i], i == cursor_ ? 1.0f : 0.0f, 16.0f, ctx.dt);
        ui::dimScreen(0.55f);
        constexpr int w = 300, h = 132;
        const int x = (gfx::kScreenW - w) / 2, y = 56;
        ui::panel(x, y, w, h);
        ui::paragraph(x + 16, y + 16, w - 32, question_, ui::Face::Label, ui::kTextBright, 2);
        ui::ButtonLook yes, no;
        yes.focus = focus_[0];
        no.focus = focus_[1];
        ui::button(x + 16, y + h - 46, 124, ui::ButtonKind::Primary, yes_, yes);
        ui::button(x + w - 16 - 124 - 6, y + h - 46, 124, ui::ButtonKind::Main, "CANCEL", no);
        const ui::Hint hints[] = {ui::kHintSelect, ui::kHintBack};
        ui::bottomBar(hints, 2);
    }
    bool overlay() const override { return true; }
    float backdropDim() const override { return 0.55f; }

private:
    const char* question_ = "";
    const char* yes_ = "CONFIRM";
    void (*onYes_)(Context&) = nullptr;
    int cursor_ = 1;
    float focus_[2] = {0.0f, 1.0f};
};

MainScreen g_main;
TrainingScreen g_training;
CampaignScreen g_campaign;
LocationScreen g_location;
BriefingScreen g_briefing;
DebriefScreen g_debrief;
MultiplayerScreen g_multiplayer;
LobbyScreen g_lobby;
HangarScreen g_hangar;
ShopScreen g_shop;
SettingsScreen g_settings;
ProfileScreen g_profile;
NameEntryScreen g_nameEntry;
EmblemScreen g_emblems;
ConfirmScreen g_confirm;

// Aircraft on its gear: the menu world puts the hangar floor at y = 0
// (docs/assets/hangar.md), so the aircraft origin stands -GROUND_Y high
// (tools/models/su35s_gear.py: tyres touch at model y = -2.40, attitude level).
constexpr float kAircraftHeight = 2.40f;

// Is a camera eye inside the shelter with 3 m to spare from the vault and the
// walls (docs/assets/hangar.md)? 24 x 11 x 40 m: a real shelter has no room
// for the 14-30 m orbit everywhere, so the orbit pulls in instead.
bool insideShelter(const math::Vec3& e) {
    const float x = e.x / 9.0f, h = e.y / 8.0f;
    return e.y >= 0.5f && e.z >= -18.0f && e.z <= 16.0f && x * x + h * h <= 1.0f;
}
const math::Vec3 kAircraftPos{0.0f, kAircraftHeight, 0.0f};

// Pre-flight control check every few seconds: pitch, roll, yaw, the
// rudders' airbrake split, slats - the old hangar viewer's demo, slower.
game::ControlInput checkInput(float t, game::FlightState& s) {
    game::ControlInput in;
    s.speed = 60.0f;  // slow: the surfaces take their full travel
    s.alpha = 0.0f;
    s.airbrake = false;
    if (t < 0.0f || t > 7.5f) return in;
    if (t < 2.0f) in.pitch = std::sin(t * math::kPi);
    else if (t < 4.0f) in.roll = std::sin((t - 2.0f) * math::kPi);
    else if (t < 6.0f) in.yaw = std::sin((t - 4.0f) * math::kPi);
    else s.airbrake = true;
    return in;
}

// Indoor light rig fitted to the hangar bake at the aircraft (white probes,
// rms ~10/255, docs/assets/hangar.md): cool daylight from the open door ahead,
// warm lamps overhead, a warm ambient from the lit concrete; no separate bounce.
const gfx::LightRig kHangarLight = {
    math::Vec3{0.0f, 0.087f, -0.996f},  // key: the door, nearly level
    math::Vec3{0.0f, 0.866f, 0.500f},   // fill: the lamps along the vault
    math::Vec3{0.0f, -1.0f, 0.0f},
    gfx::abgr(53, 63, 79), gfx::abgr(70, 74, 80), gfx::abgr(73, 67, 57), 0u, gfx::abgr(101, 92, 78)};

}  // namespace

// ---------------------------------------------------------------- Backdrop

void Backdrop::load() {
    hasHangar_ = hangar_.load("assets/models/hangar.fsm") && hangarTex_.load("assets/models/hangar.ftx");
    hasFloor_ = floor_.load("assets/models/hangar_floor.fsm") && floorTex_.load("assets/models/hangar_floor.ftx");
    hasGear_ = gear_.load("assets/models/su35s_gear.fsm") && gearTex_.load("assets/models/su35s_gear_std.ftx");
    surfaces_.reset();
}

void Backdrop::target(Shot shot, const Context& ctx, math::Vec3& eye, math::Vec3& at, float& fov) const {
    // Framings from docs/assets/hangar.md (floor at y = 0), vertical FOV 55.
    fov = 55.0f;
    switch (shot) {
        case Shot::Main:  // aircraft on the right ~210-470 px, the menu column over the wall
            eye = {-8.40f, 2.20f, -17.90f};
            at = {6.30f, 1.60f, -3.00f};
            break;
        case Shot::Orbit: {
            at = {0.0f, kAircraftHeight, 0.0f};
            const float cp = std::cos(ctx.orbitPitch);
            const math::Vec3 dir{cp * std::sin(ctx.orbitYaw), std::sin(ctx.orbitPitch), cp * std::cos(ctx.orbitYaw)};
            float r = ctx.orbitDistance;
            while (r > 6.0f && !insideShelter(at + dir * r)) r -= 0.25f;
            eye = at + dir * r;
            break;
        }
        case Shot::Nose:
            eye = {-5.20f, 2.10f, -14.40f};
            at = {0.20f, 2.05f, -7.60f};
            break;
        case Shot::Wide:
            eye = {2.60f, 6.60f, -18.00f};
            at = {-0.60f, 1.20f, 1.50f};
            break;
    }
}

void Backdrop::update(Shot shot, const Context& ctx, float dt) {
    math::Vec3 eye, at;
    float fov;
    target(shot, ctx, eye, at, fov);
    if (!snapped_) {
        eye_ = eye, at_ = at, fov_ = fov;
        snapped_ = true;
    }
    // The orbit follows the stick directly; preset changes glide.
    const float rate = shot == Shot::Orbit ? 10.0f : 3.2f;
    const float k = 1.0f - std::exp(-rate * dt);
    eye_ = eye_ + (eye - eye_) * k;
    at_ = at_ + (at - at_) * k;
    fov_ += (fov - fov_) * k;

    checkTime_ += dt;
    if (checkTime_ > 14.0f) checkTime_ = -1.5f;  // a pause, then the next check
    game::FlightState s;
    game::ControlInput in = checkInput(checkTime_ - 1.0f, s);
    surfaces_.update(s, in, dt);
}

void Backdrop::render(const Context& ctx, const scenes::Assets& assets) {
    // Behind the shelter: the overexposed daylight seen through the door.
    gfx::drawBackgroundGradient(gfx::abgr(178, 204, 232), gfx::abgr(196, 210, 226));
    gfx::setProjection(fov_, 1.0f, 150.0f);  // near 1 m: the hangar is tessellated for it (gotchas #19)
    gfx::setCamera(eye_, at_, {0.0f, 1.0f, 0.0f});

    if (hasHangar_ || hasFloor_) {
        gfx::disableLighting();  // the shelter's light is baked into its texture
        if (hasFloor_) {
            floorTex_.bind();
            floor_.draw(kAircraftPos, math::Quat{});
        }
        if (hasHangar_) {
            hangarTex_.bind();
            hangar_.draw(kAircraftPos, math::Quat{});
        }
    }
    // Soft shadow on the floor from the lamps above.
    gfx::drawShadow(kAircraftPos, math::Quat{}, math::normalize(math::Vec3{0.08f, 1.0f, 0.12f}), assets.fx());

    gfx::enableLightRig(kHangarLight);
    const meta::VehicleId v = ctx.shownVehicle;
    (void)v;  // only the Su-35S has a model so far
    const int livery = ctx.shownLivery < scenes::kLiveryCount ? ctx.shownLivery : 0;
    assets.liveries[livery].bind();
    using game::Surface;
    gfx::MeshPose pose;
    const Surface surfaces[] = {Surface::Stabilator, Surface::Flaperon, Surface::Slat, Surface::Rudder,
                                Surface::Nozzle};
    const gfx::MeshChannel channels[] = {gfx::kChannelStabilator, gfx::kChannelFlaperon, gfx::kChannelSlat,
                                         gfx::kChannelRudder, gfx::kChannelNozzle};
    for (int i = 0; i < 5; ++i)
        for (int side = 0; side < 2; ++side) pose.angle[channels[i]][side] = surfaces_.angle(surfaces[i], side);
    assets.su35s.draw(kAircraftPos, math::Quat{}, &pose);
    if (hasGear_) {
        gearTex_.bind();
        gear_.draw(kAircraftPos, math::Quat{});
    }
    // The loadout chosen for it, on the pylons (docs/decisions/0038).
    const uint16_t mask =
        meta::stationMask(ctx.profile.vehicles[static_cast<int>(meta::VehicleId::Su35S)].loadout);
    for (int pass = 0; pass < 2; ++pass) {
        const game::MissileParams* type = pass == 0 ? &game::kR73 : &game::kR77;
        (pass == 0 ? assets.r73Skin : assets.r77Skin).bind();
        for (int i = 0; i < game::kStationCount; ++i) {
            const game::Station& st = game::kSu35SStations[i];
            if (st.type == type && (mask & (1u << i)))
                (pass == 0 ? assets.r73 : assets.r77).draw(kAircraftPos + st.attach, math::Quat{});
        }
    }
    gfx::disableLighting();
}

// ---------------------------------------------------------------- MenuScene

MenuScene::MenuScene(scenes::Assets& assets, meta::Profile& profile)
    : assets_(assets), ctx_(profile, assets, *this) {
    backdrop_.load();
    hasBack_ = loadSound(back_, "ui_back");
    hasTab_ = loadSound(tab_, "ui_tab");
    hasSlide_ = loadSound(slide_, "ui_slide");
    hasDeny_ = loadSound(deny_, "ui_deny");
    hasConfirm_ = loadSound(confirm_, "ui_confirm");
}

bool MenuScene::loadSound(audio::Sound& s, const char* name) {
    if (soundBlobCount_ >= kSoundBlobs) return false;
    char path[64];
    std::snprintf(path, sizeof path, "assets/sfx/%s.wav", name);
    core::Blob& b = soundBlobs_[soundBlobCount_];
    b = core::loadFile(path);
    if (!b || !audio::parseWav(b.data, b.size, s)) return false;
    ++soundBlobCount_;
    return true;
}

Screen* MenuScene::screen(ScreenId id) {
    switch (id) {
        case ScreenId::Main: return &g_main;
        case ScreenId::Training: return &g_training;
        case ScreenId::Campaign: return &g_campaign;
        case ScreenId::Location: return &g_location;
        case ScreenId::Briefing: return &g_briefing;
        case ScreenId::Debrief: return &g_debrief;
        case ScreenId::Multiplayer: return &g_multiplayer;
        case ScreenId::Lobby: return &g_lobby;
        case ScreenId::Hangar: return &g_hangar;
        case ScreenId::Shop: return &g_shop;
        case ScreenId::Settings: return &g_settings;
        case ScreenId::Profile: return &g_profile;
        case ScreenId::NameEntry: return &g_nameEntry;
        case ScreenId::Emblems: return &g_emblems;
        case ScreenId::Confirm: return &g_confirm;
        case ScreenId::Count: break;
    }
    return &g_main;
}

void MenuScene::enter() {
    if (depth_ == 0) {
        stack_[0] = ScreenId::Main;
        depth_ = 1;
        top()->enter(ctx_);
    }
    ctx_.shownVehicle = ctx_.profile.lastVehicle;
    ctx_.shownLivery = ctx_.profile.vehicles[static_cast<int>(ctx_.profile.lastVehicle)].livery;
    audio::mixer().setWorldGain(1.0f);
}

void MenuScene::leave() {}

void MenuScene::push(ScreenId id) {
    if (depth_ >= kMaxDepth) return;
    from_ = stack_[depth_ - 1];
    hasFrom_ = true;
    stack_[depth_++] = id;
    trans_ = 0.0f;
    transDir_ = 1;
    top()->enter(ctx_);
}

void MenuScene::pop() {
    if (depth_ <= 1) return;
    from_ = stack_[depth_ - 1];
    hasFrom_ = true;
    --depth_;
    trans_ = 0.0f;
    transDir_ = -1;
    top()->enter(ctx_);
}

void MenuScene::replace(ScreenId id) {
    if (depth_ < 1) return;
    from_ = stack_[depth_ - 1];
    hasFrom_ = true;
    stack_[depth_ - 1] = id;
    trans_ = 0.0f;
    transDir_ = 1;
    top()->enter(ctx_);
}

void MenuScene::popTo(ScreenId id) {
    int i = depth_ - 1;
    while (i >= 0 && stack_[i] != id) --i;
    if (i < 0 || i == depth_ - 1) return;
    from_ = stack_[depth_ - 1];
    hasFrom_ = true;
    depth_ = i + 1;
    trans_ = 0.0f;
    transDir_ = -1;
    top()->enter(ctx_);
}

void MenuScene::sfx(Sfx s) {
    const audio::Sound* snd = &assets_.uiMove;
    switch (s) {
        case Sfx::Move: snd = &assets_.uiMove; break;
        case Sfx::Select: snd = &assets_.uiSelect; break;
        case Sfx::Back: snd = hasBack_ ? &back_ : &assets_.uiMove; break;
        case Sfx::Tab: snd = hasTab_ ? &tab_ : &assets_.uiMove; break;
        case Sfx::Slide: snd = hasSlide_ ? &slide_ : &assets_.uiMove; break;
        case Sfx::Deny: snd = hasDeny_ ? &deny_ : &assets_.uiMove; break;
        case Sfx::Confirm: snd = hasConfirm_ ? &confirm_ : &assets_.uiSelect; break;
    }
    audio::mixer().play(audio::kUi, snd, 0.6f);
}

void MenuScene::save() { platform::saveProfile(ctx_.profile); }

void MenuScene::launch(const meta::SortieConfig& sortie) {
    launch_ = sortie;
    launchPending_ = true;
}

bool MenuScene::takeLaunch(meta::SortieConfig& out) {
    if (!launchPending_) return false;
    launchPending_ = false;
    out = launch_;
    return true;
}

void MenuScene::confirm(const char* question, void (*onYes)(Context&), const char* yes) {
    g_confirm.set(question, onYes, yes);
    push(ScreenId::Confirm);
}

void MenuScene::sortieReturned(const meta::SortieResult& r) {
    meta::Profile& p = ctx_.profile;
    const meta::SortieConfig& s = launch_;
    ctx_.lastResult = r;
    ctx_.lastWasMission = s.mode == meta::SortieMode::Mission;
    ctx_.lastReward = meta::MissionReward{};
    meta::recordSortie(p, s.vehicle, r.airKills, r.groundKills);
    if (ctx_.lastWasMission) {
        if (r.outcome == meta::SortieOutcome::Success)
            ctx_.lastReward = meta::completeMission(p, s.location, s.mission, s.difficulty, r.airKills, r.groundKills);
        else {
            const int rank = meta::rankIndex(p);
            ctx_.lastReward.rankBefore = ctx_.lastReward.rankAfter = rank;
        }
        // Back to the mission list with the debriefing on top.
        popTo(ScreenId::Location);
        push(ScreenId::Debrief);
        trans_ = 1.0f;
    }
    save();
}

void MenuScene::update(const platform::Pad& padIn, float dt) {
    ctx_.time += dt;
    ctx_.dt = dt < 0.1f ? dt : 0.1f;
    platform::Pad pad = padIn;
    if (tourStarted_) tour(pad);
    trans_ = ui::approach(trans_, 1.0f, 11.0f, dt);
    if (trans_ > 0.995f) {
        trans_ = 1.0f;
        hasFrom_ = false;
    }
    Screen* s = top();
    if (s && trans_ > 0.6f) s->update(ctx_, pad, dt);
    // The backdrop follows the topmost screen that isn't a dialog.
    int i = depth_ - 1;
    while (i > 0 && screen(stack_[i])->overlay()) --i;
    Screen* view = screen(stack_[i]);
    backdrop_.update(view->shot(), ctx_, dt);
    dim_ = ui::approach(dim_, view->backdropDim(), 6.0f, dt);
}

void MenuScene::render() {
    backdrop_.render(ctx_, assets_);
    gfx::begin2D();
    ui::dimScreen(dim_);
    const float t = ui::easeOutCubic(trans_);
    constexpr int kSlide = 28;
    // A dialog draws over the screen it belongs to.
    if (depth_ >= 2 && top()->overlay()) {
        gfx::resetLayer();
        screen(stack_[depth_ - 2])->draw(ctx_);
    }
    if (hasFrom_ && trans_ < 1.0f && !screen(from_)->overlay() && !top()->overlay()) {
        gfx::setLayer(static_cast<int>(-transDir_ * kSlide * t), 0, 1.0f - t);
        screen(from_)->draw(ctx_);
    }
    if (top()->overlay()) gfx::setLayer(0, 0, t);
    else gfx::setLayer(static_cast<int>(transDir_ * kSlide * (1.0f - t)), 0, t);
    top()->draw(ctx_);
    gfx::end2D();
}

// ---------------------------------------------------------------- smoke-test tour

namespace {

// One step of the tour: optional screen to push (from the main menu), pad
// buttons to press, frames to wait, then a screenshot.
struct TourStep {
    int push;          // ScreenId or -1
    unsigned press;    // PSP_CTRL_* or 0
    int wait;          // frames before the shot
    bool shot;
    bool popAll;       // back to the main menu first
};

constexpr TourStep kTour[] = {
    {-1, 0, 50, true, false},                                         // main menu
    {-1, PSP_CTRL_CROSS, 30, true, false},                            // SINGLEPLAYER drawer open
    {-1, PSP_CTRL_DOWN, 10, false, false},
    {-1, PSP_CTRL_DOWN, 24, true, false},                             // cursor on TRAINING
    {static_cast<int>(ScreenId::Training), 0, 30, true, true},
    {static_cast<int>(ScreenId::Campaign), 0, 40, true, true},
    {-1, PSP_CTRL_RIGHT, 40, true, false},                            // carousel slid
    {static_cast<int>(ScreenId::Location), 0, 30, true, false},
    {static_cast<int>(ScreenId::Briefing), 0, 30, true, false},
    {-1, PSP_CTRL_RTRIGGER, 24, true, false},                         // briefing pages
    {-1, PSP_CTRL_RTRIGGER, 24, true, false},
    {-1, PSP_CTRL_RTRIGGER, 24, true, false},
    {static_cast<int>(ScreenId::Hangar), 0, 50, true, true},
    {static_cast<int>(ScreenId::Shop), 0, 30, true, false},
    {static_cast<int>(ScreenId::Profile), 0, 50, true, true},
    {static_cast<int>(ScreenId::NameEntry), 0, 30, true, false},
    {static_cast<int>(ScreenId::Emblems), 0, 40, true, true},
    {static_cast<int>(ScreenId::Settings), 0, 30, true, true},
    {static_cast<int>(ScreenId::Multiplayer), 0, 30, true, true},
    {static_cast<int>(ScreenId::Lobby), 0, 60, true, false},
    {static_cast<int>(ScreenId::Debrief), 0, 40, true, true},  // a won mission
    {static_cast<int>(ScreenId::Debrief), 0, 40, true, true},  // a lost one: retry / rearm
};
constexpr int kTourSteps = sizeof kTour / sizeof kTour[0];

}  // namespace

void MenuScene::startTour() {
    tourStarted_ = true;
    tourStep_ = 0;
    tourFrames_ = 0;
}

void MenuScene::tour(platform::Pad& pad) {
    pad = platform::Pad{};
    if (tourStep_ < 0) return;
    const TourStep& st = kTour[tourStep_];
    if (tourFrames_ == 0) {
        if (st.popAll) {
            depth_ = 1;
            hasFrom_ = false;
            trans_ = 1.0f;
            stack_[0] = ScreenId::Main;
            top()->enter(ctx_);
        }
        if (st.push >= 0) {
            ctx_.location = 0;
            ctx_.mission = 0;
            if (static_cast<ScreenId>(st.push) == ScreenId::Debrief) {
                // A won, then a lost mission to debrief, without changing the profile.
                const bool won = !tourDebriefed_;
                tourDebriefed_ = true;
                ctx_.lastWasMission = true;
                ctx_.sortie = meta::SortieConfig{};
                ctx_.sortie.mode = meta::SortieMode::Mission;
                ctx_.sortie.location = 0;
                ctx_.sortie.mission = 0;
                ctx_.sortie.airEnemies = 1;
                ctx_.lastResult = meta::SortieResult{};
                ctx_.lastResult.outcome = won ? meta::SortieOutcome::Success : meta::SortieOutcome::Failed;
                ctx_.lastResult.airKills = won ? 3 : 1;
                ctx_.lastResult.timesShotDown = won ? 0 : 1;
                ctx_.lastResult.seconds = won ? 312.0f : 147.0f;
                ctx_.lastReward = meta::MissionReward{};
                ctx_.lastReward.credits = won ? 15000 : 0;
                ctx_.lastReward.firstClear = won;
                ctx_.lastReward.rankBefore = 0;
                ctx_.lastReward.rankAfter = won ? 1 : 0;
            }
            push(static_cast<ScreenId>(st.push));
        }
    }
    if (tourFrames_ == 2 && st.press) {
        pad.pressed = st.press;
        pad.held = st.press;
        trans_ = 1.0f;  // input goes through even mid-transition
    }
    // The app takes the request at the start of the next frame and saves that
    // frame: hold still for two frames after asking, then move on.
    ++tourFrames_;
    if (tourFrames_ == st.wait && st.shot) shotRequest_ = true;
    if (tourFrames_ >= st.wait + 2) {
        tourFrames_ = 0;
        if (++tourStep_ >= kTourSteps) tourStep_ = -1;
    }
}

}  // namespace menu
