#pragma once
// The third adventure's parrot: the delivery minigame the story introduces
// (and the bag offers afterwards). Three loops, each the native part around
// a script program:
//
//  * MINI_PARROT_MENU (overlay 25, lt3CLoopMiniParrotM;
//    evt/prg/lt3_mini_parrot_menu): pick whom to deliver to. Twelve
//    character buttons - only the stages unlocked so far ("＜オウム＞ステージ１"
//    [n]) - and, once one is picked, the start button slides in with the
//    stage's title, text and rules on the upper screen (commands 1280 and
//    1281 below). START asks for the game.
//
// Reconstructed from overlay 25: 0211db78 (start), 0211dc20 (activate),
// 0211dd18 / 0211dcdc (frame), 0211dd94 (1280), 0211de88 (1281).
#include "story/a3_story.hpp"
#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace picarat::adventure3 {

class ParrotMenuLoop : public Loop {
public:
    ParrotMenuLoop(Story& story, LoopArgs args) : Loop(story, "MINI_PARROT_MENU", args) {}

    void start() override;
    void activate() override;
    void deactivate() override;
    void frame(const lscr::SceneInput& input) override;
    bool busy() const override;
    // 0: 1280, hide what is locked; 1: 1281, pick character {number}.
    lscr::Result command(int index, lscr::Call& call) override;

private:
    enum class State { Idle, Loading, Running, Finished };
    State state_ = State::Idle;
    int picked_ = 0; // +0x7bc: the character picked, 0 none
};

} // namespace picarat::adventure3

namespace picarat::adventure3 {

//  * MINI_PARROT_GAME (overlay 26, lt3CLoopMiniParrotG;
//    evt/prg/lt3_mini_parrot_game, stage data rc/mode/lt3_mini_parrot):
//    the delivery itself. On a grid of points, some of them pegs, the
//    player stretches up to a few ropes from peg to peg (dragging from one
//    to another; tapping a rope takes it away; ropes may not cross each
//    other, the walls, a peg or the goal). START sends the parrot: a hop
//    up from its perch, then a free flight under gravity. Ropes and walls
//    bounce it back (both its body and its head, eight pixels round, bump);
//    landing on top of a level rope it perches for a moment and hops on
//    from there. Coming down onto the goal ledge delivers; leaving the
//    screens, getting stuck or running out of time fails. The script does
//    the buttons, messages and results (sections PARO_AFT_JDG_SUCCESS /
//    _FAIL / PARO_RST_OBJ, called from here), through commands 1280..1287.
class ParrotGameLoop : public Loop {
public:
    ParrotGameLoop(Story& story, LoopArgs args);
    ~ParrotGameLoop() override;

    void start() override;
    void activate() override;
    void deactivate() override;
    void end() override;
    void frame(const lscr::SceneInput& input) override;
    bool busy() const override;
    lscr::Result command(int index, lscr::Call& call) override;

    // Research (the automatic player): the stage's pegs (grid points) and
    // their places on the lower screen, the rope limit, whether a rope may
    // go between two pegs (no other rope there), and whether the parrot -
    // flown in a silent copy of the game - delivers with these ropes, each
    // drawn in turn from its first peg (false if they cannot be drawn so).
    std::vector<int> pegs() const;
    std::array<int, 2> pegPlace(int point) const;
    int ropeLimit() const;
    bool ropeAllowed(int a, int b) const;
    bool delivers(const std::vector<std::pair<int, int>>& ropes) const;

private:
    class Engine;
    enum class State { Idle, Loading, Running, Finished };
    State state_ = State::Idle;
    int stage_ = 0; // +0x810
    std::unique_ptr<Engine> engine_;
};

// The app's minigame free play: the twelve stages' flags (the menu's
// ＜オウム＞ステージ１ [n-1] "open" and ＜オウムクリアー＞ [n] "delivered").
constexpr int kParrotStages = 12;
void openParrotStages(Story& story);
std::vector<std::uint8_t> parrotDelivered(const Story& story); // stages 1..12
void setParrotDelivered(Story& story, const std::vector<std::uint8_t>& delivered);

} // namespace picarat::adventure3
