#pragma once
// Pancake towers (puzzle type 13, ARM9 overlay 41, code 020f78a0..020f8e60).
//
// How the puzzle plays
// --------------------
// This is the Tower of Hanoi with pancakes. A stack of up to seven pancakes of
// different sizes lies on the left of three plates. The player lifts the top
// pancake of a plate with the stylus and drops it on another plate. A pancake
// may never be put on a smaller one; such a drop is rejected and the pancake
// jumps back. The puzzle is solved when the whole stack sits on the right
// plate, and the game counts the moves.
//
// The parameter script only gives the number of pancakes (opcode 0x40).
//
// Pancakes are identified by index: index 0 is the smallest, higher indices
// are larger. Each pancake has its own little physics: dropped pancakes fall
// with gravity and bounce slightly, held ones follow the stylus with easing,
// and rejected ones hop back to the plate they came from.
//
// Lower-screen layout: the plates are 64 pixels apart; a pancake's sprite
// sits at x = 4, 68 or 132, and each pancake on a stack is 8 pixels above the
// one below it, the bottom one at y = 147.
#include "system/effect_queue.hpp"
#include "system/interaction.hpp"
#include "system/script.hpp"
#include <array>

namespace picarat {

class TowerPuzzle {
public:
    // Sound effects: 0x105 a pancake is lifted, 0x106 it lands (or bounces),
    // 0x104 a rejected drop.
    EffectQueue sounds;

    // What a pancake is doing (Piece::state).
    static constexpr int kResting = 0;       // on a plate, easing to its place
    static constexpr int kFalling = 1;       // dropped, falling under gravity
    static constexpr int kHeld = 2;          // following the stylus
    static constexpr int kRejectedSlide = 3; // illegal drop: easing over the target, then hop back
    static constexpr int kRejectedFall = 4;  // illegal drop from high up: fall, then hop back

    // Puzzle states (state()).
    static constexpr int kIdle = 0;
    static constexpr int kHolding = 1;
    static constexpr int kComplete = 2;

    struct Piece {
        int state = kResting;
        std::int16_t plate = 0, level = 0;             // where it lies (level 0 = bottom)
        std::int16_t priorPlate = -1, priorLevel = -1; // where it was lifted from
        std::int16_t x = 4, y = 147;                   // where it is heading (pixels)
        std::int16_t bounces = 0;
        float displayX = 0, displayY = 0; // where it is drawn
        float vx = 0, vy = 0;             // velocity in pixels per frame
    };

    // The pancakes on one plate, bottom first (-1 = empty slot).
    struct Stack {
        std::int16_t count = 0;
        std::array<std::int16_t, 7> pieces{-1, -1, -1, -1, -1, -1, -1};
    };

    // `randomState` is the game's shared random generator (the falling
    // entrance uses it); a private one is used when none is given.
    explicit TowerPuzzle(std::span<const std::uint8_t> script,
                         std::uint32_t* randomState = nullptr);

    // Rebuilds the stack on the left plate and lets it fall in from above.
    void reset();

    // One frame of input.
    void update(const Touch& touch);

    // One frame of pancake movement.
    void animate();

    // Judged automatically once complete for 46 frames (020f7cb4).
    bool solved() const { return state_ == kComplete && counter_ > 45; }

    int state() const { return state_; }

    int selected() const { return selected_; }

    int counter() const { return counter_; }

    int moves() const { return moves_; }

    int count() const { return count_; }

    const auto& stacks() const { return stacks_; }

    const auto& pieces() const { return pieces_; }

    // Pancake indices in drawing order (back to front).
    std::vector<int> drawOrder() const;

private:
    std::array<Stack, 3> stacks_;
    std::array<Piece, 7> pieces_;
    std::int16_t count_ = 0;     // number of pancakes in this puzzle
    std::int16_t selected_ = -1; // held pancake, or -1
    std::int16_t counter_ = 0;   // frames spent complete
    std::int16_t moves_ = 0;     // moves made (capped at 9999)
    std::int16_t touchX_ = -1, touchY_ = -1;
    int state_ = kIdle;
    std::uint32_t ownedRandom_ = 1;
    std::uint32_t* random_;

    // Plate under a horizontal stylus position (020f867c): left of x = 64 is
    // the left plate, right of x = 128 the right plate; x = 128 itself still
    // counts as the middle plate.
    static int plateAt(int x) { return x < 64 ? 0 : x > 128 ? 2 : 1; }

    // Sets a pancake's target position from its plate and level.
    static void target(Piece& p);

    // Aims a falling pancake so that it lands on its target.
    static void fall(Piece& p);

    // Starts the hop of a rejected pancake back to its target.
    static void bounceBack(Piece& p);

    // Lifts the top pancake of a plate.
    void pick(int plate);

    // Puts the held pancake on a plate if allowed; `countMove` counts it as a move.
    bool drop(int plate, bool countMove);
};

} // namespace picarat
