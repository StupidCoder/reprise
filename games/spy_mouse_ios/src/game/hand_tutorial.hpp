// The demonstrating finger of the first level (SceneHandTutorial, pushed by
// SceneGame::postInitGame @0x105f98 for map 10 when the level is entered, not on a retry):
// a finger with its shadow glides along a route stored in the map, presses down at the mouse,
// draws to the cheese and on into the home hole, lifts and leaves; 7 s later it starts again.
// It goes for good once the player has drawn a path. The route is the map's type-142 lines
// (showScene @0x1f6788): each line's first vertex is a sample, the vertex's `d` field the time
// to wait there (ms; a negative value also toggles the finger between lifted and pressed).
// Reconstructed from ::update @0x1f6e38 and ::render @0x1f739c / renderHand @0x1f76dc.
#pragma once

#include "data/map_file.hpp"
#include "game/geometry.hpp"
#include "game/sprite_anim.hpp"

#include <vector>

namespace doc::game {

class HandTutorial {
public:
    struct Sample {
        Vec2 position;  // map space
        bool pressed = false;
        int waitMs = 0;  // how long the finger stays once it got there
    };
    // The route of a map (empty: the map has none).
    static std::vector<Sample> route(const data::MapFile& map);

    HandTutorial() = default;
    HandTutorial(std::vector<Sample> route, SheetSize rippleSheet, float rippleFrameWidth);

    // Still on the scene stack (below the level intro until that ends).
    bool active() const { return active_; }
    // One tick while it is the top scene, after the level's own update. `pathPoints`: the
    // player's path length (any path ends the demonstration).
    void update(int dtMs, int pathPoints);
    void end() { active_ = false; }

    // Drawing (nothing while waiting to start).
    bool visible() const { return active_ && delayMs_ <= 0; }
    Vec2 position() const { return position_; }  // the fingertip, map space
    float lift() const { return lift_; }           // finger above its shadow
    bool rippleVisible() const { return rippleState_ == 1; }
    const SpriteAnim& ripple() const { return ripple_; }

    // State for the trace comparison (doc-verify).
    int delayMs() const { return delayMs_; }
    bool playing() const { return playing_; }
    int index() const { return index_; }
    int waitMs() const { return waitMs_; }
    Vec2 velocity() const { return velocity_; }
    int rippleState() const { return rippleState_; }

private:
    void startOver();

    std::vector<Sample> route_;
    bool active_ = false;
    int delayMs_ = 500;          // +0xb8: before (re)starting
    bool playing_ = false;       // +0xf8 == 2
    int index_ = 0;              // +0xc8: heading from route_[index_] to route_[index_ + 1]
    int waitMs_ = 0;             // +0xbc: time spent at the next sample
    Vec2 position_{-9999.99f, -9999.99f};  // +0xe8
    Vec2 velocity_;              // +0xf0
    // Statics in the original (not reset between loops; the lift starts lifted at 25).
    float lift_ = 25.0f;         // @0x5f1bfc: eases to 5 pressed, 25 lifted
    int rippleState_ = 0;        // @0x5fc4dc: 0 armed, 1 showing, 2 done for this loop
    SpriteAnim ripple_;          // sprites/node_ripple.png, 59 wide
};

}  // namespace doc::game
