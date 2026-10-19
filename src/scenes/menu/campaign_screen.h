#pragma once

#include "scenes/menu/menu.h"

namespace menu {

// Campaign (docs/decisions/0037): a horizontal carousel of location cards over
// the dimmed hangar. The centre card is big and crisp (the location art 1:1 in
// a metal frame) with a nameplate under it; its neighbours are smaller and
// darker and slide in as you press left/right. Locked locations are dark with a
// lock and say which location to clear first.
class CampaignScreen : public Screen {
public:
    void enter(Context& ctx) override;
    void update(Context& ctx, const platform::Pad& pad, float dt) override;
    void draw(Context& ctx) override;
    Shot shot() const override { return Shot::Wide; }
    float backdropDim() const override { return 0.62f; }

private:
    void drawCard(Context& ctx, int i, float d);
    void drawPlate(Context& ctx);
    void drawPlateText(Context& ctx, int i, float alpha, int x, int y);
    void drawArrows();
    void drawPager(Context& ctx);

    int index_ = 0;         // the card the carousel is heading for
    float pos_ = 0.0f;      // where it is now (fractional while sliding)
    float kick_[2] = {};    // arrow nudge after a press (left, right)
    float shake_ = 0.0f;    // the lock shakes when a locked card is selected
};

}  // namespace menu
