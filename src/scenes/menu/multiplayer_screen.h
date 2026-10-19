#pragma once

#include "scenes/menu/menu.h"

namespace menu {

// Multiplayer (docs/decisions/0035): pick the network. Two big cards, LAN
// (ad hoc, PSP systems nearby) and INTERNET (through an access point); the
// choice goes to Context::network and the lobby list opens.
class MultiplayerScreen : public Screen {
public:
    void enter(Context& ctx) override;
    void update(Context& ctx, const platform::Pad& pad, float dt) override;
    void draw(Context& ctx) override;
    Shot shot() const override { return Shot::Wide; }

private:
    int cursor_ = 0;
    float focus_[2] = {1.0f, 0.0f};
};

// Lobby list of the chosen network. Multiplayer is not implemented yet, so
// the search runs its animation and then honestly reports nothing found;
// CREATE LOBBY only says the mode is in development.
class LobbyScreen : public Screen {
public:
    void enter(Context& ctx) override;
    void update(Context& ctx, const platform::Pad& pad, float dt) override;
    void draw(Context& ctx) override;
    Shot shot() const override { return Shot::Wide; }

private:
    float search_ = 0.0f;     // seconds into the current search
    float toast_ = 0.0f;      // seconds left of the "in development" banner
    float toastAge_ = 0.0f;   // seconds since it appeared (slide-in)
    float focus_ = 0.0f;
};

}  // namespace menu
