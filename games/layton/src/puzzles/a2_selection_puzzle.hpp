#pragma once
// Selection (puzzle type 3, ARM9 overlay 31).
//
// How the puzzle plays
// --------------------
// The picture contains several items that can be switched on and off by
// tapping them. Each tap toggles one item, showing or hiding its marker. When
// the player presses "Submit", the puzzle is solved only if exactly the right
// items are selected: every expected item on and every other item off.
//
// Items come from the parameter script (opcode 0x14, one per item). An
// unselected item is invisible but can still be tapped.
//
// Puzzle type 14 is an exclusive variant (only one item at a time) that
// exists in the code but is not used by any puzzle in this game.
#include "graphics/graphics.hpp"
#include "system/effect_queue.hpp"
#include "system/script.hpp"

namespace picarat {

class SelectionPuzzle {
public:
    // Click state of an item, as in the original toggle button (+20).
    static constexpr int kIdle = 0;
    static constexpr int kArmed = 1;   // pressed on the item, stylus still down
    static constexpr int kClicked = 2; // released on the item; waiting to toggle

    struct Choice {
        std::string sprite;    // marker sprite ("nazo/onoff/...")
        Rect bounds;           // position and size of the marker image
        bool expected = false; // must be selected for the answer to be right
        bool selected = false; // currently switched on
        bool inside = false;   // stylus currently over the armed item
        int state = kIdle;
        std::uint8_t age = 0;           // frames since the click completed
        int sound = 0;                  // click sound from the script (-1: none)
        const Surface* image = nullptr; // marker shown while selected
    };

    // `internalId` selects the alternative toggle sounds of one puzzle.
    SelectionPuzzle(std::span<const std::uint8_t> script, GraphicsAssets& assets,
                    unsigned internalId = 0);

    // Sounds: the script's click sound when a click completes (02080300),
    // then 0xf7 when an item is switched on and 0xf8 when switched off
    // (0x127/0x128 instead for the puzzle with internal ID 0x86).
    EffectQueue sounds;

    // One frame of input for every item.
    void update(const Touch& touch);

    // Applies a completed click (called after the common puzzle buttons).
    void consume();

    // True when the selection matches the expected answer exactly.
    bool solved() const;

    const auto& choices() const { return choices_; }

private:
    bool alternate_ = false; // puzzle 0x86's own toggle sounds
    std::vector<Choice> choices_;
};

} // namespace picarat
