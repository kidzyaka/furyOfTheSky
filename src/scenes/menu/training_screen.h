#pragma once

#include "scenes/menu/menu.h"

namespace menu {

// Training (docs/decisions/0035, 0037): three exercises as big metal buttons
// over the live hangar. Left/right picks the target pattern / the opponent's
// level, a plate below describes the focused exercise, and a card top right
// shows the aircraft that will fly it. X launches a sortie with the profile's
// last vehicle, livery and loadout.
class TrainingScreen : public Screen {
public:
    void enter(Context& ctx) override;
    void update(Context& ctx, const platform::Pad& pad, float dt) override;
    void draw(Context& ctx) override;
    Shot shot() const override { return Shot::Main; }
    float backdropDim() const override { return 0.2f; }

    static constexpr int kCount = 3;

private:
    void launch(Context& ctx);

    int cursor_ = 0;
    int target_ = 2;     // TARGET PRACTICE pattern: Straight, Orbit, Weave, Evade
    int dogfight_ = 1;   // DOGFIGHT level: Easy, Medium, Hard
    float focus_[kCount] = {1.0f, 0.0f, 0.0f};
};

}  // namespace menu
