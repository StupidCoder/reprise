#pragma once
// Puzzle progress, picarats and hint coins.
//
// Each puzzle has one progress byte in the save data (the game's own
// operations on it live in overlay 2):
//   bits 0-1  status: 0 not seen, 1 seen, 2 solved
//   bits 2-3  wrong answers given (0..3)
//   bits 4-5  hints unlocked (0..3)
// The setters never lower a value unless forced, and they keep the byte's
// two high bits, as the originals do.
//
// Picarats are the game's score. A puzzle is worth fewer picarats after each
// wrong answer: its data lists three values, for 0, 1 and 2-or-more wrong
// answers. Hint coins (found while exploring) unlock hints, one coin each.
#include <array>
#include <cstdint>

namespace picarat {

struct PuzzleProgress {
    std::uint8_t packed = 0;

    unsigned status() const { return packed & 3; }

    unsigned failures() const { return (packed >> 2) & 3; }

    unsigned hints() const { return (packed >> 4) & 3; }

    void setStatus(std::uint32_t value, bool force = false);
    void setFailures(std::uint32_t value, bool force = false);
    void setHints(std::uint32_t value, bool force = false);

    // Opening a puzzle marks it as seen (overlay 2, 020697e0).
    void enter();

    // A wrong answer counts as a failure unless the puzzle is already solved
    // (overlay 27, 020f251c).
    void finishFailure();
};

// The progress of the puzzle being played, with the player's coins and
// score. This is the engine's own working state, not a copy of the DS save
// file layout.
struct PuzzleSession {
    PuzzleProgress first;
    std::uint16_t coins = 10, picarats = 0;
    int completionValue = 0;
    // Random generator state (the game's LCG). The original shares one
    // generator with the whole game; the native session starts its own.
    std::uint32_t randomState = 1;
    // Save data +200: one bit for each of the tutorials 1..3 already shown
    // (overlay 2, 02082f58 / 02082f7c).
    std::uint8_t tutorialsSeen = 0;
    // State +0x7ebe: cleared when a puzzle opens (overlay 27, 020f26c8), set
    // by the last judgment (020f37a0 correct / 020f37bc wrong).
    bool lastJudgedCorrect = false;

    // Unlocks hints up to `count` for one coin (fails without coins, or for
    // a solved puzzle).
    bool purchaseHint(unsigned count);

    // Works out the picarats for a correct answer and adds them to the score
    // unless the puzzle was solved before or does not award picarats.
    bool award(unsigned internalId, unsigned followingScene, const std::array<unsigned, 3>& values,
               std::uint32_t& reward);
};

// What the picarat count-up animation needs from the result screen.
struct PicaratCountHost {
    virtual ~PicaratCountHost() = default;
    virtual void waitOneFrame() = 0;
    virtual bool confirmed() = 0; // the player tapped to skip ahead
    virtual void stopCountSound() = 0;
    virtual void update() = 0;
    virtual void present(std::uint32_t total, std::uint32_t remaining, std::uint32_t elapsed,
                         bool confirmed) = 0;
};

// The count-up of the score after a solved puzzle (overlay 27,
// 020f14b4..020f15a4, separated from the surrounding result screen).
void countPicarats(std::uint32_t total, std::uint32_t reward, PicaratCountHost& host);

} // namespace picarat
