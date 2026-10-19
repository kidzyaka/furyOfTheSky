#pragma once

#include "scenes/menu/menu.h"

namespace menu {

// Main menu (docs/decisions/0035): a column of big metal buttons on the left
// over the live hangar. SINGLEPLAYER opens a drawer of darker sub-rows that
// slide out from under it while the other main buttons dim; what doesn't fit
// scrolls under the bottom plate. The pilot card sits top right.
class MainScreen : public Screen {
public:
    void enter(Context& ctx) override;
    void update(Context& ctx, const platform::Pad& pad, float dt) override;
    void draw(Context& ctx) override;
    Shot shot() const override { return Shot::Main; }
    float backdropDim() const override { return 0.0f; }

    static constexpr int kItemCount = 8;

private:
    bool visible(int i) const;
    void move(Context& ctx, int dir);
    void activate(Context& ctx);
    void layout(float y[kItemCount], float& contentBottom) const;
    void drawPilotCard(Context& ctx);

    int cursor_ = 0;
    int open_ = -1;         // expanded group, -1 none
    int closing_ = -1;      // group whose drawer is still sliding shut
    float expand_ = 0.0f;   // 0..1 drawer of open_/closing_
    float dim_ = 0.0f;      // other main buttons pushed back
    float focus_[kItemCount] = {};
    float scroll_ = 0.0f;
};

}  // namespace menu
