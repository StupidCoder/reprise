// Time spent by the original's loading code (see game.hpp, "Time spent").

#include "sonic2_gg/game/game.hpp"

namespace sonic2_gg {

// The CPU runs `clocks` more in the current frame. Each frame boundary
// crossed makes a frame pass before the program continues (at its next
// wait); with interrupts enabled its frame interrupt runs, otherwise it
// stays pending until the next EI.
void Game::spend(unsigned clocks) {
    if (direct_start) {
        return; // shell option: a level chosen in the launcher loads at once
    }
    clock_ += clocks;
    while (clock_ >= frame_clocks) {
        clock_ -= frame_clocks;
        ++stall_frames_;
        if (di_) {
            irq_pending_ = true;
        } else {
            frame_interrupt();
        }
    }
}

// EI: a pending frame interrupt runs now.
void Game::ei() {
    di_ = false;
    take_pending_interrupt();
}

bool Game::take_pending_interrupt() {
    di_ = false;
    if (!irq_pending_) {
        return false;
    }
    irq_pending_ = false;
    frame_interrupt();
    return true;
}

} // namespace sonic2_gg
