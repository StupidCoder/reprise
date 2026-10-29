#pragma once
// The fourth adventure's puppet show (loop 6100, overlay 38
// lt4CLoopMiniPuppet; the bag's second minigame, reached through its menu
// 6110).
//
// How it plays
// ------------
// Three plays, each a chain of scenes (res/sub/lt4_sub_puppet.cb: 61
// scenes, each a block of evt/sub/lt4_mini_puppet_N's script). At most
// scenes the play stops and the player picks what the puppet does next
// from the actions learned during the story (24 actions, items named
// "＜演技＞...", learned when the flag "＜演技＞" with the action's number
// is set), eight to a page. The scene's branches lead on by the action
// chosen; any other action takes the scene's last branch (a bad ending).
// A branch marked as the right one keeps the play on course: the play
// succeeds when every step taken was the right one. The player can step
// back to an earlier scene; actions already used are greyed out. The
// choices are kept in the variable "＜演目N演技＞" (one index per step) and
// copied to "＜演目N大団円＞" for the finale, which replays them.
//
// The script (evt/prg/lt4_mini_puppet_main) does the screens; the loop
// keeps the chain and the choices and answers 2400 with a sub-command
// (02106494): 0 start a play, 1 run the current scene's block, 2 follow
// the branch of the action in "汎用選択", 3 one step back, 4 the
// success/failure block, 5 the learned actions, 6..8 and 13 the choice
// pages, 9 the finale's lines, 10..12 objects whose places each step
// keeps, 14 the page buttons, 15 the step's line, 16 the scene's picture,
// 17 back at the start?, 18 the branch's value, 19 the finale's choices.
//
// Reconstructed from overlay 38 (02105220..02108820); see
// .analysis/adventure4/docs/minigames.md.
#include "story/a4_script_loop.hpp"
#include <array>
#include <vector>

namespace picarat::adventure4 {

class PuppetLoop final : public ScriptLoop {
public:
    PuppetLoop(Game& game, int id, std::span<const Value> values);

    bool command(gss::Call& c, int op) override;

private:
    struct Branch {
        int action = -1;  // -1: any other
        std::string next; // the scene it leads to
        bool right = false;
        int value = 0;
    };

    struct Scene {
        std::string name;
        std::vector<Branch> branches;
    };

    // An object whose look and place each step keeps (10..12).
    struct Kept {
        std::string name, sequence;
        int x = 0, y = 0;
    };

    // A step of the play (0x230 bytes each, at most 16).
    struct Step {
        std::string scene;  // +0x184
        bool right = false; // +0x188
        int action = -1;    // +0x18c
        int slot = -1;      // +0x190: the button on its page
        int page = -1;      // +0x194
        int picture = 0;    // +0x198
        std::string text;   // +0x19c
        std::array<Kept, 16> kept{};
    };

    void loadPlays();
    const Scene* scene(const std::string& name) const;
    const Branch* branchFor(const Scene& s, int action) const;
    std::string actionText(int action) const;
    std::string placeholder() const;
    std::string playVariable() const;   // ＜演目N演技＞
    std::string finaleVariable() const; // ＜演目N大団円＞
    void showPage(int page, int slot);  // 021080c0
    void showPicture(int picture);

    Step& step() { return steps_[std::size_t(current_)]; }

    std::vector<Scene> scenes_;
    std::vector<std::string> actions_; // their item names
    std::vector<int> learned_;         // +0x248c: action numbers
    std::array<Step, 16> steps_{};
    std::string at_;       // +0x180: the current scene
    int current_ = 0;      // +0x2484
    int keptCount_ = 0;    // +0x2488
    int chosen_ = -1;      // +0x249c: the highlighted action
    int chosenSlot_ = -1;  // +0x24a0
    int page_ = 0;         // +0x24a4
    int play_ = -1;        // +0x24a8
    int playValue_ = 0;    // +0x24ac
    int picture_ = 0;      // +0x24b0
    bool pending_ = false; // +0x24b4: a choice shown, not taken yet
    int finaleStep_ = 1;   // +0x24b8
};

} // namespace picarat::adventure4
