#pragma once
#include "puzzles/judgment.hpp"
#include "story/a2_game_state.hpp"
#include "system/scene_kit.hpp"

namespace picarat {
// Photo hunt (spot the differences).
//
// How it plays: once the camera works, the player can photograph certain
// places. Later the photo is compared with the place as it is now: the
// player circles the differences between the photo and the room. Finding
// all of a photo's differences completes it; completing all nine photos
// unlocks an event.
//
// Photo hunt, ARM9 overlay 16 (load 020efda0), run by ov2 scenes 26/27
// (0206a8fc; the bag opens 27 once the camera works). A 4-byte object holds
// the screen number; 020efde8 runs the screens until one returns 7:
//   0 album (020f0000, run 020f04e8): the nine photos taken in rooms;
//   1 start (020f0680, run 020f095c): a finished photo and its puzzle icon;
//   2 intro (020f0a70, run 020f0d1c): photo number and place, then the wipe;
//   3 hunt (020f0ff4, run 020f15c8): circle the differences between the
//     photo (lower screen) and the room as it is now (upper screen);
//   4/5/6 result (020f1988, run 020f1a48): miss / found / photo complete.
// After the screens, all nine photos complete play event 18500 once.
class PhotoGame : private JudgmentHost {
public:
    PhotoGame(GraphicsAssets& assets, PresentFrame present, AudioService* audio, GameState& state,
              std::uint32_t& random);
    void run();

    std::function<HostInput(unsigned, const HostInput&)>& researchPoll() {
        return kit_.researchPoll;
    } // research only
private:
    using SpriteObject = SceneKit::SpriteObject;
    using SpriteButton = SceneKit::SpriteButton;
    SceneKit kit_;
    GameState& state_;
    std::uint32_t& random_;
    int album();          // 020f0000 / 020f04e8 / 020f02c4
    int start();          // 020f0680 / 020f095c / 020f07b4
    int intro();          // 020f0a70 / 020f0d1c / 020f0bf0
    int hunt();           // 020f0ff4 / 020f15c8 / 020f13ac
    int result(int mode); // 020f1988 / 020f1a48 / 020f19f0

    const std::uint8_t* row() { return state_.photoByIndex(state_.photo); }

    // JudgmentHost (ov2 0206b4fc): images on the lower (0206ffec) and, for
    // the final one, upper (0207064c) background; lower brightness only.
    bool aborted() override { return false; }

    void image(const std::string& path, bool final) override;

    void brightness(int value) override { kit_.setLowerBrightness(value); }

    void upperBrightness(int value) override { kit_.setUpperBrightness(value); }

    void present() override;

    void presentWithoutPoll() override { kit_.present(); }

    void wait(int frames) override { kit_.wait(frames); }

    void slide(int bottom, int top) override {
        kit_.lowerScrollY = bottom;
        kit_.upperScrollY = top;
    }

    void openWindows() override;
    void clipWindows(int bottom, int top) override;
    void clearWindows() override;
    void windowsOff() override;

    void sound(unsigned id) override { kit_.effect(int(id)); }

    void effectBank(int number) override {
        if (auto s = kit_.sound()) {
            s->loadBank(2, number);
        }
    }

    void voice(unsigned id) override {
        if (auto s = kit_.sound()) {
            s->playTrackedStream(0, int(id), -1, 1.0f, 0);
        }
    } // 0206b9cc/0206ba04
};
} // namespace picarat
