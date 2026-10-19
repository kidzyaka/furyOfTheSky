#pragma once

#include "scenes/menu/menu.h"

namespace menu {

// Hangar (docs/decisions/0035, 0037): the player's aircraft over the live 3D
// hangar. The stick orbits the camera, L/R zoom; a strip of cards at the
// bottom lists the owned vehicles and ends with SHOP. The selected card is
// what stands in the hangar; X makes it the active one, triangle cycles its
// livery. Changes are saved when the screen is left.
class HangarScreen : public Screen {
public:
    void enter(Context& ctx) override;
    void update(Context& ctx, const platform::Pad& pad, float dt) override;
    void draw(Context& ctx) override;
    Shot shot() const override { return Shot::Orbit; }
    float backdropDim() const override { return 0.08f; }

private:
    void rebuild(const Context& ctx);
    void showSelected(Context& ctx);

    static constexpr int kMaxCards = meta::kVehicleCount + 1;  // + SHOP
    meta::VehicleId owned_[meta::kVehicleCount] = {};
    int count_ = 0;          // owned vehicles; the SHOP card is at index count_
    int cursor_ = 0;
    float focus_[kMaxCards] = {};
    float scroll_ = 0.0f;
    float plate_ = 1.0f;     // livery plate: fades out on the SHOP card
    bool dirty_ = false;     // profile changed: save on leaving
    bool toShop_ = false;    // the shop is open above: enter() is a return from it
    bool ownedBefore_[meta::kVehicleCount] = {};
};

// Shop: a gallery of cards (two to a row, scrolling down), one per catalog
// vehicle: its side schematic, name, role, and a state/price plate.
// Available and affordable ones are bought through the confirmation dialog;
// the rest are CLASSIFIED (in development) or too expensive.
class ShopScreen : public Screen {
public:
    void enter(Context& ctx) override;
    void update(Context& ctx, const platform::Pad& pad, float dt) override;
    void draw(Context& ctx) override;
    Shot shot() const override { return Shot::Wide; }

private:
    int cursor_ = 0;
    float focus_[meta::kVehicleCount] = {};
    float scroll_ = 0.0f;           // px, smoothed
    float target_ = 0.0f;           // px the gallery wants to be scrolled to
    bool snap_ = false;             // jump to the target on the next draw (a fresh visit)
    float lastDraw_ = -100.0f;      // ctx.time of the last draw: tells a fresh visit from a dialog closing
    char question_[64] = {};        // the dialog keeps this pointer while it is open
};

}  // namespace menu
