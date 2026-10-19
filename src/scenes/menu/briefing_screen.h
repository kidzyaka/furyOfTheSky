#pragma once

#include <cstdint>

#include "meta/catalog.h"
#include "meta/sortie.h"
#include "scenes/menu/menu.h"

namespace menu {

// Briefing of ctx.location / ctx.mission (docs/decisions/0037, 0038): four
// pages switched with L/R. MISSION (text, objective, weather, difficulty and
// the START button), AIRCRAFT (who can fly it), ARMAMENT (pylon pairs of the
// Su-35S) and LIVERY (the hangar behind shows it). START launches from any page.
class BriefingScreen : public Screen {
public:
    void enter(Context& ctx) override;
    void update(Context& ctx, const platform::Pad& pad, float dt) override;
    void draw(Context& ctx) override;
    // The livery page wants the aircraft in view: the camera moves aside and
    // the hangar is hardly dimmed; the panel stays on the left.
    Shot shot() const override { return page_ == kPageLivery ? Shot::Main : Shot::Wide; }
    float backdropDim() const override { return page_ == kPageLivery ? 0.1f : 0.6f; }

    enum Page : int { kPageMission, kPageAircraft, kPageArmament, kPageLivery, kPageCount };

private:
    const meta::Mission& mission() const;
    const meta::MissionVariant* variant() const;  // of the chosen vehicle's type, or null
    bool canFly(const Context& ctx, meta::VehicleId v) const;
    void chooseVehicle(Context& ctx, meta::VehicleId v);
    void setPage(Context& ctx, int page, Sfx sound);
    void setLoadout(Context& ctx, uint8_t mask);
    void setLivery(Context& ctx, int livery);
    void launch(Context& ctx);
    void leave(Context& ctx);

    void updateMission(Context& ctx, const platform::Pad& pad);
    void updateAircraft(Context& ctx, const platform::Pad& pad);
    void updateArmament(Context& ctx, const platform::Pad& pad);
    void updateLivery(Context& ctx, const platform::Pad& pad);

    void drawTabs();
    void drawPage(Context& ctx, int page);
    void drawMission(Context& ctx);
    void drawAircraft(Context& ctx);
    void drawArmament(Context& ctx);
    void drawLivery(Context& ctx);

    int loc_ = 0, mis_ = 0;
    int page_ = kPageMission;
    float pageF_ = 0.0f;                      // page slide position (fractional while sliding)
    float tabFocus_[kPageCount] = {};

    int difficulty_ = 1;                      // remembered between briefings: Normal at first
    float diffFocus_[meta::kDifficultyCount] = {};

    meta::VehicleId vehicle_ = meta::VehicleId::Su35S;      // chosen
    meta::VehicleId eligible_[meta::kVehicleCount] = {};    // vehicles with a variant in this mission
    int eligibleCount_ = 0;
    int vehCursor_ = 0;
    float vehFocus_[meta::kVehicleCount] = {};
    float vehDetail_ = 1.0f;                  // detail panel flick when the cursor moves

    uint8_t loadout_ = meta::kLoadoutFull;
    int armCursor_ = 0;
    float armFocus_[meta::kPairCount] = {};
    float presetKick_[2] = {};                // preset arrows nudge

    int liveryCursor_ = 0;
    float liveryFocus_[4] = {};

    bool dirty_ = false;                      // loadout / livery changed since the last save
};

}  // namespace menu
