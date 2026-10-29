#pragma once
// Cycling pictures (puzzle type 10, ARM9 overlay 38).
//
// How the puzzle plays
// --------------------
// The lower screen shows several pictures that change each time they are
// tapped: every tap advances a picture to its next variant (for example the
// next colour), wrapping around after the last one. The player arranges all
// pictures into the required combination and presses "Submit".
//
// The parameter script places each picture (opcode 0x2e) with its sprite, the
// variant shown at the start and the variant the answer requires. A required
// variant of -1 means "any variant is fine" for that picture.
//
// Each variant is one animation of the picture's sprite. Animation 0 is not a
// variant, so the cycle runs 1, 2, ..., last, 1, ...
//
// A tap counts when the stylus is pressed on a picture and released while
// still over that same picture.
#include "graphics/graphics.hpp"
#include "system/effect_queue.hpp"
#include "system/script.hpp"

namespace picarat {

class ColorPuzzle {
public:
    // The tap sound depends on the puzzle (020f7a30); see update().
    EffectQueue sounds;

    struct Item {
        int x, y;     // position on the lower screen
        int initial;  // variant (animation) shown at the start
        int current;  // variant shown now
        int expected; // variant the answer requires, or -1 for any
        const Sprite* sprite;
        AnimationPlayer animation;
    };

    ColorPuzzle(std::span<const std::uint8_t> script, GraphicsAssets& assets,
                unsigned internalId = 0);

    unsigned internalId_ = 0; // selects the tap sound

    // One frame of input. `tick` is the frame counter used to restart the
    // newly selected variant's animation.
    void update(const Touch& touch, std::uint32_t tick);

    // Every picture back to its starting variant (the "Reset" button).
    void reset(std::uint32_t tick);

    // True when every picture shows its required variant.
    bool solved() const;

    // Advances the pictures' own animations by one frame.
    void animate(std::uint32_t tick);

    void render(Surface& target, int yOffset) const;

    const std::vector<Item>& items() const { return items_; }

    // Whether a picture is pressed, and which one.
    bool armed() const { return armed_; }

    unsigned selected() const { return selected_; }

private:
    // Hit rectangle: the current image at the picture's position.
    Rect bounds(const Item& item) const;

    std::vector<Item> items_;
    bool armed_ = false;
    unsigned selected_ = 0;
    // Last stylus position while held (a release reports no position).
    int lastX_ = -1, lastY_ = -1;
};

} // namespace picarat
