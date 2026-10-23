// The playfield part of the frame ($AE19, raster $76): the VIC settings for
// the playfield, the enemy, the characters the game redefines in place, and
// the sound effects.

#include "fort_apocalypse_c64/game/bcd.hpp"
#include "fort_apocalypse_c64/game/game.hpp"

namespace fort_apocalypse::game {

void Game::playfield_interrupt() {
    // Raster $76: shared colours black, the playfield's scroll and window,
    // the playfield character set.
    vic_.shared1 = vic_.shared2 = 0;
    vic_.xscroll = xscroll_;
    vic_.columns40 = false;
    vic_.yscroll = yscroll_;
    vic_.rows25 = false;
    vic_.playfield_charset = true;
    split_ = vic_;
    static_noise();
    // Raster $7F: the level's colours, sprites on.
    vic_.shared1 = level_colour_;
    vic_.shared2 = 1;
    vic_.border = vic_.background = playfield_colour_;
    vic_.sprites = 0x1F;
    lower_ = vic_;
    if (mode_ == Mode::locked) {
        sound_interrupt();
    } else if (mode_ == Mode::playing) {
        enemy_visibility();
        shimmer();
        enemy_watchdog();
        enemy_interrupt();
        barriers();
        sweeping_walls();
        laser_grids();
        sound_interrupt();
    }
}

// --- characters redefined in place -----------------------------------------------

void Game::barriers() {
    // $A7ED, $A830: every 8 frames each group's timer advances; a group is
    // lit only in the 8 frames after its timer wraps to 0. After the fort's
    // destruction they stay dark.
    if ((frame_ & 7) != 0) {
        return;
    }
    auto group = [&](u8& timer, int first, bool second) {
        bool lit = false;
        if (barriers_off_ != 1) {
            timer = u8(timer + barrier_step_);
            lit = timer == 0;
        }
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 8; ++r) {
                glyphs_[std::size_t(first + c)][std::size_t(r)] =
                    lit ? tables_.barrier_pattern[std::size_t(c * 8 + r)] : 0;
            }
        }
        if (lit) {
            glyphs_[9] = second ? tables_.barrier_cap_b : tables_.barrier_cap_a;
        } else if (!second) {
            glyphs_[9] = {};
        }
    };
    group(barrier_a_, 1, false);
    group(barrier_b_, 5, true);
}

void Game::laser_grids() {
    // $A86B: every 128 frames each laser character is lit at random.
    if ((frame_ & 0x7F) != 0) {
        return;
    }
    for (int c = 0x0A; c <= 0x0D; ++c) {
        glyphs_[std::size_t(c)] = {};
    }
    constexpr std::uint16_t sites[] = {0xA87B, 0xA88A, 0xA899, 0xA8A8};
    for (int c = 0x0A; c <= 0x0D; ++c) {
        if (!(noise(sites[c - 0x0A]) & 0x80)) {
            glyphs_[std::size_t(c)].fill(0x55);
        }
    }
}

void Game::sweeping_walls() {
    // $A8B8: one of four wall characters is solid; the solid one moves on
    // every few frames, in the direction the last shot set.
    if (--sweep_timer_ != 0) {
        return;
    }
    sweep_timer_ = sweep_period_;
    for (int c = 0x0E; c <= 0x11; ++c) {
        glyphs_[std::size_t(c)] = {};
    }
    sweep_phase_ = u8((sweep_phase_ + sweep_direction_) & 3);
    glyphs_[std::size_t(0x0E + sweep_phase_)].fill(0x55);
}

void Game::static_noise() {
    // $A8F3: the explosion and the fort's core flicker, and the missiles'
    // exhaust, every frame.
    for (int r = 7; r >= 0; --r) {
        const u8 v = tables_.static_pattern[std::size_t(r)] & noise(0xA8F8);
        glyphs_[0x20][std::size_t(r)] = v;
        glyphs_[0x3F][std::size_t(r)] = v;
    }
    for (int r = 3; r < 5; ++r) {
        glyphs_[0x71][std::size_t(r)] = u8((noise(0xA906) & 0x0F) | 0xA0);
    }
    for (int r = 3; r < 5; ++r) {
        glyphs_[0x72][std::size_t(r)] = u8((noise(0xA917) & 0xE0) | 0x0A);
    }
}

void Game::shimmer() {
    // $AF54: the energy-field walls alternate between two patterns.
    const int phase = frame_ & 0x0F;
    if (phase != 0 && phase != 8) {
        return;
    }
    for (int i = 0; i < 32; ++i) {
        glyphs_[std::size_t(0x4C + i / 8)][std::size_t(i % 8)] =
            phase == 0 ? tables_.shimmer[std::size_t(i)]
                       : assets_.playfield_charset[std::size_t(0x4C + i / 8)][std::size_t(i % 8)];
    }
    glyphs_[0x47][3] = glyphs_[0x47][4] = phase == 0 ? 0x59 : 0x51;
}

// --- sound ------------------------------------------------------------------------------

void Game::sound_off() {
    // $B11A.
    for (int r = 0; r < 0x17; ++r) {
        sid_.write(r, 0);
    }
    sid_.write(0x18, 0x0A);
    sid_.write(0x12, 0x80);
    sid_.write(0x0F, 0xFF);
}

void Game::sound_engine_on() {
    // $B1D8: voice 1, a filtered sawtooth for the engine.
    sid_.write(0x01, 0);
    sid_.write(0x04, 0);
    sid_.write(0x00, 0x80);
    sid_.write(0x06, 0xF0);
    sid_.write(0x04, 0x21);
    sid_.write(0x17, 0xF1);
    sid_.write(0x18, 0x1F);
}

void Game::sound_explosion() {
    // $B189: voice 3 noise.
    sid_.write(0x13, 0x0B);
    sid_.write(0x14, 0x0B);
    explosion_sound_ = 0x0B;
    sid_.write(0x0E, u8(noise(0xB195) | 0x80));
    sid_.write(0x0F, 0x02);
    sid_.write(0x12, 0x02);
    sid_.write(0x12, 0x81);
}

void Game::sound_shot(u8 control) {
    // $B1AB: not over an explosion, and not outside play.
    if (explosion_sound_ != 0 || !split_layout_) {
        return;
    }
    sid_.write(0x13, 0x88);
    sid_.write(0x0F, 0x88);
    sid_.write(0x0E, noise(0xB1BD));
    sid_.write(0x14, 0x08);
    sid_.write(0x12, 0x08);
    if (shot_sound_) {
        sid_.write(0x14, 0x88);
    }
    sid_.write(0x12, control);
}

void Game::sound_rescue(u8 frequency) {
    // $B1FA: voice 2.
    sid_.write(0x08, frequency);
    sid_.write(0x0C, 0x58);
    sid_.write(0x0D, 0x0A);
    sid_.write(0x0B, 0x0A);
    sid_.write(0x0B, 0x11);
}

void Game::sound_missile() {
    // $B210: voice 2 noise.
    sid_.write(0x0C, 0x59);
    sid_.write(0x0D, 0x09);
    sid_.write(0x07, u8(noise(0xB21A) | 0x80));
    sid_.write(0x08, 0x04);
    sid_.write(0x0B, 0x04);
    sid_.write(0x0B, 0x81);
}

void Game::sound_interrupt() {
    // $AE90, every playfield frame.
    sid_.write(0x16, engine_pitch_);
    if (engine_sound_timer_) {
        --engine_sound_timer_;
        sid_.write(0x00, 0x5A);
    }
    if (explosion_sound_) {
        --explosion_sound_;
    }
    if (refuel_sound_) {
        u8 control = (frame_ & 7) == 0 ? 0x00 : 0x11;
        u8 pitch = 0;
        sid_.write(0x0D, 0x00);
        sid_.write(0x0C, 0x08);
        if (fuel_[1] < 0x20) {
            pitch = 0xA6;
            bool carry = false;
            fuel_[0] = bcd_add(fuel_[0], 4, carry);
            fuel_[1] = bcd_add(fuel_[1], 0, carry);
        }
        sid_.write(0x0B, control);
        sid_.write(0x07, u8((pitch & 3) << 6));
        sid_.write(0x08, u8(pitch >> 2));
    }
    if (shot_sound_) {
        --shot_sound_;
        sid_.write(0x0F, shot_sound_);
        if (shot_sound_ == 5) {
            sid_.write(0x14, 0);
            shot_sound_ = 0;
        }
    }
    if (teleport_grace_) {
        sid_.write(0x0C, 9);
        sid_.write(0x0D, 9);
        const u8 before = teleport_grace_++;
        if (before == 0x20) {
            tone_timer_ = 0x1E;
            teleport_grace_ = 0;
        }
        sid_.write(0x08, u8(~before));
        sid_.write(0x0B, 0x16);
        sid_.write(0x0B, 0x17);
    }
    if (tone_timer_ && --tone_timer_ == 0) {
        sid_.write(0x08, 0);
    }
    if (fuel_[1] == 0) {
        if ((frame_ & 0x10) == 0) {
            sid_.write(0x0C, 0);
            sid_.write(0x07, 0x50);
            sid_.write(0x08, 0x50);
            sid_.write(0x0D, 0xA0);
            sid_.write(0x0B, 0x21);
        } else {
            sid_.write(0x0B, 0);
        }
    }
}

} // namespace fort_apocalypse::game
