#pragma once
// Moving pairs (puzzle type 30, class lt2qCoupleObject).
// ARM9 overlay 51: ROM 0xbb800 (+0xb2c bytes), loaded at 020f78a0, vtable 020f86e4.
//
// How the puzzle plays
// --------------------
// A row of slots holds two kinds of items (the sprites call them "bin" and
// "can"), alternating bin, can, bin, can, ... with empty slots at the end.
// The player picks up two neighbouring items together and drops them on two
// neighbouring empty slots; the pair keeps its order. The puzzle is solved
// when all bins form one unbroken group and all cans form another. There is
// no "Submit": the puzzle judges itself 30 frames after the row is sorted
// (virtual function +220 returns 0). The game counts the moves.
//
// The parameter script gives the number of items, the number of slots and
// the row's left edge (command 0x66).
//
// Layout: slots are 16 pixels apart at y = 0x50; a marker ("touchpoint")
// between two neighbouring items shows where a pair can be grabbed.
#include "system/effect_queue.hpp"
#include "system/interaction.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace picarat {

class CouplePuzzle {
public:
    // Sound effects: 0x115 a pair is picked up, 0x116 it is put down.
    EffectQueue sounds;

    explicit CouplePuzzle(std::span<const std::uint8_t> script);

    // Back to the starting row (virtual function +204, 020f7a00).
    void reset();

    // One frame of input and movement (020f7b60).
    void update(const TouchFilter& touch);

    // Sorted and 30 more frames have passed (virtual function +1fc, 020f7ad4).
    bool complete() const { return state_ == kSorted && timer_ > 0x1e; }

    // The common puzzle buttons only work while no pair is held (020f7af8).
    bool commonInput() const { return state_ == kIdle; }

    // One sprite to draw (020f80e8, drawn after the move counter).
    struct Draw {
        const char* animation;
        int x, y;
    };

    std::vector<Draw> draws() const;

    int moves() const { return moves_; }

private:
    // Item kinds.
    static constexpr int kEmpty = 0;
    static constexpr int kBin = 1;
    static constexpr int kCan = 2;

    // Puzzle states (+0x70).
    static constexpr int kIdle = 0;
    static constexpr int kHolding = 1;
    static constexpr int kSorted = 2;

    // A slot or held item (16-byte record): what it is, where it is heading
    // and where it is drawn.
    struct Item {
        int kind = kEmpty;
        std::int16_t targetX = 0, targetY = 0;
        float x = 0, y = 0;
    };

    std::array<Item, 16> slots_{};           // +0xa4
    std::array<Item, 2> held_{};             // +0x84: the picked-up pair
    std::int16_t count_ = 0;                 // +0x1a4: number of items
    std::int16_t slotCount_ = 0;             // +0x1a6
    std::int16_t originX_ = 0;               // +0x1a8: x of the first slot
    int state_ = kIdle;                      // +0x70
    std::int16_t timer_ = 0;                 // +0x74: frames since sorted
    std::int16_t moves_ = 0;                 // +0x1aa
    std::int16_t touchX_ = -1, touchY_ = -1; // +0x76/+0x78
    std::int16_t picked_ = -1;               // +0x7a: slot the pair came from
    float pointX_ = 0, pointY_ = 0;          // +0x7c/+0x80: the grab marker of the held pair

    // Moves a value halfway towards its target (one frame of easing).
    static void ease(float& v, int target);
};

} // namespace picarat
