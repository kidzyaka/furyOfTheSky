#pragma once

#include "meta/profile.h"
#include "scenes/menu/menu.h"

namespace menu {

// Combat profile (docs/decisions/0037): the pilot's card over a close view of
// the aircraft's nose. Two rows to work with - CALLSIGN (X opens the
// keyboard) and EMBLEM (X opens the emblem gallery, which equips and saves) -
// plus rank progress, the favourite aircraft and the service record.
class ProfileScreen : public Screen {
public:
    void enter(Context& ctx) override;
    void update(Context& ctx, const platform::Pad& pad, float dt) override;
    void draw(Context& ctx) override;
    Shot shot() const override { return Shot::Nose; }
    float backdropDim() const override { return 0.25f; }

private:
    int cursor_ = 0;           // 0 CALLSIGN, 1 EMBLEM
    float focus_[2] = {1.0f, 0.0f};
};

// On-screen keyboard for the callsign: a grid of metal keys, the text in a
// field with a blinking caret. Only ASCII is typed here; an older name (the
// PSP's system nickname may be anything) can still be cleared with DEL.
class NameEntryScreen : public Screen {
public:
    void enter(Context& ctx) override;
    void update(Context& ctx, const platform::Pad& pad, float dt) override;
    void draw(Context& ctx) override;
    Shot shot() const override { return Shot::Wide; }

    static constexpr int kKeys = 42;
    static constexpr int kMaxChars = 16;

private:
    void moveHorizontal(Context& ctx, int dir);
    void moveVertical(Context& ctx, int dir);
    void press(Context& ctx);
    void commit(Context& ctx);
    int length() const;     // characters (UTF-8 code points) in text_
    void backspace();

    char text_[meta::kNicknameBytes] = {};
    int key_ = 0;           // focused key
    int col_ = 0;           // column the cursor keeps while moving up/down
    float focus_[kKeys] = {};
};

}  // namespace menu
