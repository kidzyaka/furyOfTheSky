#pragma once

#include <pspctrl.h>

#include "render/draw2d.h"
#include "scenes/menu/menu.h"
#include "ui/ui.h"

namespace menu {

// Placeholder for a screen that isn't built yet: a title and BACK.
class StubScreen : public Screen {
public:
    explicit StubScreen(const char* title) : title_(title) {}
    void update(Context& ctx, const platform::Pad& pad, float) override {
        if (pad.hit(PSP_CTRL_CIRCLE)) {
            ctx.sfx(Sfx::Back);
            ctx.pop();
        }
    }
    void draw(Context&) override {
        ui::screenTitle(title_, "UNDER CONSTRUCTION");
        ui::bottomBar(&ui::kHintBack, 1);
    }

private:
    const char* title_;
};

}  // namespace menu
