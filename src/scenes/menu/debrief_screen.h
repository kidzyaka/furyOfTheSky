#pragma once

#include <cstdint>

#include "scenes/menu/menu.h"

namespace menu {

// Debriefing after a mission sortie (docs/decisions/0037): a stamped verdict
// (MISSION COMPLETE / FAILED / ABORTED), the sortie's figures, the reward
// counting up, the medal, and the rank with its progress bar. Under them a row
// of quick actions (left/right, X; O = MISSIONS):
//   failed / aborted: RETRY (the same sortie again), HANGAR (the briefing's
//     AIRCRAFT page, to re-equip), MISSIONS;
//   won: NEXT MISSION (when the location has one and it is open), REPLAY, MISSIONS.
// The first action holds the cursor, so one X press does the likeliest thing.
class DebriefScreen : public Screen {
public:
    void enter(Context& ctx) override;
    void update(Context& ctx, const platform::Pad& pad, float dt) override;
    void draw(Context& ctx) override;
    Shot shot() const override { return Shot::Wide; }
    float backdropDim() const override { return 0.68f; }

private:
    enum Action : uint8_t { kRetry, kHangar, kMissions, kNext, kReplay };
    static constexpr int kMaxActions = 3;

    void resolve(const Context& ctx, int& loc, int& mis) const;  // the mission that was flown
    int nextMission(const Context& ctx) const;                   // open mission after it, or -1
    void activate(Context& ctx, Action action);
    float rankProgress(const Context& ctx) const;  // 0..1 towards the next rank, 1 at the top

    float time_ = 0.0f;       // s since the screen opened: drives the reveal, the counters and the arming
    Action actions_[kMaxActions] = {};
    int count_ = 0;
    int cursor_ = 0;
    float focus_[kMaxActions] = {1.0f, 0.0f, 0.0f};
};

}  // namespace menu
