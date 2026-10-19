#pragma once

#include "scenes/menu/menu.h"
#include "ui/ui_atlas.h"

namespace menu {

// Emblem gallery (docs/decisions/0039): every pilot emblem at its native 56 px
// in a framed slot, five to a row, scrolling under the bar. A side panel shows
// the focused one large with its name and state; for a locked one, how it is
// earned and how far along the pilot is. X equips an unlocked emblem
// (profile.avatar, saved at once), a locked one answers with a deny sound.
class EmblemScreen : public Screen {
public:
    void enter(Context& ctx) override;
    void update(Context& ctx, const platform::Pad& pad, float dt) override;
    void draw(Context& ctx) override;
    Shot shot() const override { return Shot::Wide; }

    static constexpr int kCols = 5;

private:
    int count_ = 0;
    int unlockedCount_ = 0;
    int cursor_ = 0;
    bool unlocked_[ui::emblems::kCount] = {};   // cached on enter: the profile cannot change here
    float progress_[ui::emblems::kCount] = {};
    float focus_[ui::emblems::kCount] = {};
    float scroll_ = 0.0f;                        // px, smoothed
    float target_ = 0.0f;                        // px the grid wants to be scrolled to
    bool snap_ = false;                          // jump to the target on the next draw (a fresh visit)
    float detail_ = 1.0f;                        // side panel flick when the cursor moves
    float flash_ = 0.0f;                         // the slot that was just equipped lights up
    int flashIndex_ = -1;
};

}  // namespace menu
