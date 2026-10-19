#pragma once

#include "meta/catalog.h"
#include "scenes/menu/menu.h"

namespace menu {

// Missions of ctx.location (docs/decisions/0037): a list of metal rows on the
// left (code, name, medals earned, plane/helicopter variants, a lock on the
// ones not open yet) and a preview of the focused mission on the right:
// weather, time of day, enemies, reward. Long lists scroll under the bar.
class LocationScreen : public Screen {
public:
    void enter(Context& ctx) override;
    void update(Context& ctx, const platform::Pad& pad, float dt) override;
    void draw(Context& ctx) override;
    Shot shot() const override { return Shot::Wide; }
    float backdropDim() const override { return 0.6f; }

private:
    void drawRow(Context& ctx, int m, int y);
    void drawPreview(Context& ctx);

    int loc_ = -1;          // location the cursor belongs to
    int cursor_ = 0;
    int cleared_ = -1;      // missions cleared when the cursor was placed: a win moves it on
    float focus_[meta::kMaxMissions] = {};
    float scroll_ = 0.0f;
    float preview_ = 1.0f;  // 0 -> 1: the preview flicks in when the cursor moves
};

}  // namespace menu
