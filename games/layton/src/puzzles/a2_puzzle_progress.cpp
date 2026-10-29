// Puzzle progress, picarats and hint coins. See a2_puzzle_progress.hpp.
#include "puzzles/a2_puzzle_progress.hpp"
#include <algorithm>
#include <bit>

namespace picarat {

namespace {
std::int32_t signedValue(std::uint32_t value) {
    return std::bit_cast<std::int32_t>(value);
}

// Stores a 2-bit count (overlay 2, 0208249c / 020824e8). Quirks kept: the
// value is truncated to a byte before being capped at 3, and the "never
// lower" test compares the original signed argument.
void setCount(std::uint8_t& packed, unsigned shift, std::uint32_t value, bool force) {
    const auto count = std::min(unsigned(std::uint8_t(value)), 3u);
    if (signedValue(value) < int((packed >> shift) & 3) && !force) {
        return;
    }
    packed = std::uint8_t((packed & ~(3u << shift)) | (count << shift));
}
} // namespace

void PuzzleProgress::setStatus(std::uint32_t value, bool force) {
    // Overlay 2, 02082454. Unlike the counts, the status caps the full
    // unsigned argument at 2.
    value = std::min(value, 2u);
    if (value < status() && !force) {
        return;
    }
    packed = std::uint8_t((packed & 0xfc) | value);
}

void PuzzleProgress::setFailures(std::uint32_t value, bool force) {
    setCount(packed, 2, value, force);
}

void PuzzleProgress::setHints(std::uint32_t value, bool force) {
    setCount(packed, 4, value, force);
}

void PuzzleProgress::enter() {
    if (status() == 0) {
        setStatus(1);
    }
}

void PuzzleProgress::finishFailure() {
    if (status() != 2) {
        setFailures(failures() + 1);
    }
}

bool PuzzleSession::purchaseHint(unsigned count) {
    // The normal branch of overlay 27, 020f64f4, for an unsolved puzzle.
    // (Replaying a solved puzzle uses a separate, temporary hint field,
    // which the native route does not need.)
    if (first.status() == 2 || coins == 0) {
        return false;
    }
    first.setHints(count);
    --coins; // overlay 2, 0208288c (only ever decrements a non-zero count)
    return true;
}

bool PuzzleSession::award(unsigned internalId, unsigned followingScene,
                          const std::array<unsigned, 3>& values, std::uint32_t& reward) {
    // Overlay 27, 020f134c..020f13ec. The reward is worked out before the
    // result screen; the puzzle only becomes "solved" when that screen ends.
    // No picarats are added for a puzzle solved before, when the following
    // scene is 22, or for internal puzzle 206.
    reward = values[std::min(first.failures(), 2u)];
    if (first.status() == 2 || followingScene == 22 || internalId == 206) {
        return false;
    }
    picarats = std::uint16_t(std::uint32_t(picarats) + reward); // 020828f0 stores 16 bits
    return true;
}

void countPicarats(std::uint32_t total, std::uint32_t reward, PicaratCountHost& host) {
    // The score counts up one picarat per frame (two per frame for rewards
    // of 25 or more; half speed for rewards under 15). After the first three
    // frames a tap jumps to the end. The loop runs until the player confirms.
    std::uint32_t remaining = reward;
    std::uint32_t elapsed = 0;
    bool confirmed = false;
    do {
        if (signedValue(remaining) > 0) {
            --remaining;
            ++total;
        }
        ++elapsed;
        if (signedValue(remaining) > 0) {
            if (reward < 15) {
                host.waitOneFrame();
                ++elapsed;
            }
            if (reward >= 25) {
                --remaining;
                ++total;
            }
        }
        if (signedValue(elapsed) > 3) {
            if (host.confirmed()) {
                total += remaining;
                remaining = 0;
                confirmed = true;
            }
            if (remaining == 0) {
                host.stopCountSound();
            }
        }
        host.update();
        host.present(total, remaining, elapsed, confirmed);
    } while (!confirmed);
}

} // namespace picarat
