// Game construction, the per-frame entry point and the time model.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

Game::Game(const Assets& assets) : rom(assets) {
    program_.start(boot());
}

Game::Game(const Assets& assets, LevelStart start) : rom(assets) {
    program_.start(boot_to_act(start));
}

void Game::frame(std::uint8_t buttons) {
    buttons_ = buttons;
    voice_requested = false;
    if (!powered_on_) {
        // Power-on: the program starts; no frame interrupt has come yet.
        powered_on_ = true;
        program_.resume();
        return;
    }
    if (stall_frames_) {
        // A frame that starts while the program is still busy (busy()): its
        // interrupt runs, or waits for the next EI; the program goes on in
        // the frame where its work ends.
        if (di_) {
            irq_pending_ = true;
        } else {
            frame_interrupt();
            update_wide_view();     // Widescreen enhancement: margins of the presented view.
            water_line_interrupt(); // its line comes in this frame too
        }
        if (--stall_frames_ == 0) {
            program_.resume();
        }
        return;
    }
    clock_ = 0;
    frame_interrupt();
    update_wide_view(); // Widescreen enhancement: margins of the presented view.
    program_.resume();
    water_line_interrupt();
}

void Game::wait_frame_now() {
    take_pending_interrupt(); // 084C: EI
    video.busy = 0;
    if (wait_hook) {
        wait_hook();
    }
    if (!video.frame_done) {
        // The rest of the frame (Game::frame after the program waits), then
        // the next frame's start.
        water_line_interrupt();
        if (buttons_hook) {
            buttons_ = buttons_hook();
        }
        clock_ = 0;
        frame_interrupt();
        update_wide_view();
    }
    video.frame_done = 0;
    ++frame_waits;
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
    update_wide_view();
    clock_ += interrupt_clocks / cpu_speed;
    return true;
}

u8 Game::refresh_register() {
    if (random_source) {
        return random_source();
    }
    // xorshift32
    random_state_ ^= random_state_ << 13;
    random_state_ ^= random_state_ >> 17;
    random_state_ ^= random_state_ << 5;
    return u8(random_state_ & 0x7F);
}

} // namespace triple_trouble_gg
