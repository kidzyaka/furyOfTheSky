#pragma once

#include "scenes/menu/menu.h"

namespace menu {

// Settings (docs/decisions/0035, 0037): a list of metal rows with the value
// on the right, left/right changes it (hold to repeat), a plate on the right
// explains the focused row. RESET PROGRESS asks first. Leaving with BACK
// saves the profile.
class SettingsScreen : public Screen {
public:
    void enter(Context& ctx) override;
    void update(Context& ctx, const platform::Pad& pad, float dt) override;
    void draw(Context& ctx) override;
    Shot shot() const override { return Shot::Wide; }

    static constexpr int kRows = 7;

private:
    void change(Context& ctx, int dir, bool first);

    int cursor_ = 0;
    float focus_[kRows] = {};
    float scroll_ = 0.0f;
    float repeat_ = 0.0f;  // seconds until the held direction steps again
};

}  // namespace menu
