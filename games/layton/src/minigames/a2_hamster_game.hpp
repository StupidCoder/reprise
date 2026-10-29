#pragma once
#include "graphics/nds3d/g3d.hpp"
#include "story/a2_game_state.hpp"
#include "system/scene_kit.hpp"
#include <memory>

namespace picarat {
// The hamster.
//
// How it plays: the player places items (collected in the story) on a
// small 8x6 field; the hamster then walks the field on its own in 3D and
// reacts to the items it meets. Enough steps in one walk raise its level;
// reaching level 6 plays a story event.
//
// Hamster, ARM9 overlay 15 (load 020efda0), run by ov2 scene 16. A 0x10-byte
// object (020f86c8) alternates two screens (020f8864 / 020f890c):
//   item screen (0xe8 bytes, 020f32d0 / logic 020f38d8 / draw 020f3dc8 /
//     destructor 020f379c): place the owned items (save +0x284.., ten kinds)
//     on an 8x6 field (save +0x28e, one nibble per cell) and start the walk;
//   walk (0x4fc0 bytes, 020f4708 / logic 020f5e90 / draw 020f7f04 /
//     destructor 020f4df0): the hamster walks the field in 3D (map1, obj1..10,
//     eff1..4, ham%d) and reacts to the items; reaching the level's goal steps
//     raises the level (+0x283), the record is kept at save +0x2be.
// Leaving returns to the bag (scene 0x11); reaching level 6 plays event 18510.
class HamsterGame {
public:
    HamsterGame(GraphicsAssets& assets, PresentFrame present, AudioService* audio, GameState& state,
                std::uint32_t& random);
    ~HamsterGame();
    void run();

    std::function<HostInput(unsigned, const HostInput&)>& researchPoll() {
        return kit_.researchPoll;
    } // research only

    std::function<void(unsigned poll, const std::string& state)> researchTrace; // research only
    struct Walk;

private:
    using SpriteObject = SceneKit::SpriteObject;
    using SpriteButton = SceneKit::SpriteButton;
    SceneKit kit_;
    GameState& state_;
    std::uint32_t& random_;
    // Scene object (020f86c8): mode 0 item screen, 1 walk, 2 quit, 3 level 6.
    int mode_ = 0, level_ = -1;

    // --- Item screen (020f32d0).
    struct Items {
        int state = 0;                   // +0x00: 0 idle, 1 dragging, 2 settle, 3 start, 4 back
        int dragX = -1, dragY = -1;      // +0x02 / +0x04
        int fromCol = -1, fromRow = -1;  // +0x06 / +0x08
        int item = -1, selected = -1;    // +0x0a / +0x0c
        SpriteButton back, start, clear; // +0x10 / +0x14 / +0x18
        SpriteObject icon, upperIcon, count, digits, levelNumber, levelText, face; // +0x1c..+0x34
        std::array<int, 10> owned{}, placed{};                                     // +0x38 pairs
        std::array<std::array<int, 8>, 6> grid{}; // +0x88 (-1 empty)
    };

    std::unique_ptr<Items> items_;
    std::unique_ptr<Walk> walk_;
    void itemsCreate();                                    // 020f32d0
    void itemsDestroy();                                   // 020f379c (saves the grid, 020f4360)
    void itemsReset(Items& m);                             // 020f41f0
    void itemsLoad(Items& m);                              // 020f427c
    bool itemsPlace(Items& m, int col, int row, int item); // 020f44d0
    bool itemsRemove(Items& m, int col, int row);          // 020f4548
    void itemsShowText(Items& m, int item);
    void itemsLogic(); // 020f38d8
    void itemsDraw();  // 020f3dc8
    bool frame();      // 020f890c: false when the scene ends

    // Save accessors (ov2 02082aec..02082b68).
    int level() const { return state_.progress.bytes[0x283]; }

    int owned(int i) const { return i < 10 ? state_.progress.bytes[0x284 + std::size_t(i)] : 0; }

    std::string hamsterName() const;
    std::string member(const std::string& name);
    static int goalSteps(int level); // 020f8824 (020f8ecc table)
    // Walk (implemented in a2_hamster_walk.cpp).
    void walkCreate();
    void walkDestroy();
    void walkLogic();
    void walkDraw();
    int walkState() const;
};
} // namespace picarat
