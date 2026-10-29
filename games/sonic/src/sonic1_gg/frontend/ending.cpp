// The ending and credits; see ending.hpp.

#include "sonic1_gg/frontend/ending.hpp"
#include <algorithm>
#include <stdexcept>

namespace sonic1_gg {
Ending::Ending(const FlowAssets& assets, int points, int count, int gems, int monitors,
               int attempts, int successes, bool died, const Bytes& sprites)
    : score(points), lives(count), emeralds(gems), assets_(assets) {
    // 4663 computes the completion bonus before the emeralds and lives are spent.
    bonus = (gems >= 6 ? 5 : 0) + (monitors >= 18 ? 5 : 0) + (attempts >= 8 ? 5 : 0) +
            (successes >= 8 ? 5 : 0) + (!died ? 10 : 0);
    if (bonus == 30) {
        bonus = 50;
    }
    load_screen(6);
    if (sprites.size() != 4096) {
        throw std::runtime_error("Ending requires original sprite patterns");
    }
    std::copy(sprites.begin(), sprites.end(), screen.patterns.begin() + 0x2000);
    screen.palette = assets.palette(10, 11);
    phase = gems >= 6 ? Phase::EmeraldHold : Phase::MapHold;
}

void Ending::load_screen(unsigned id) {
    screen.patterns = assets_.screens.at(id).patterns;
    screen.tiles = assets_.screens.at(id).tiles;
    screen.sprites.clear();
    screen.enabled = true;
}

void Ending::next(Phase p) {
    phase = p;
    phase_ticks = 0;
}

void Ending::fade(Bytes target) {
    fade_target_ = std::move(target);
}

// Writes a row of tiles: column, row, tile numbers, 0xFF terminator.
void Ending::patch(unsigned address) {
    unsigned x = assets_.byte(address++), y = assets_.byte(address++);
    for (int i = 0; i < 256; ++i) {
        unsigned b = assets_.byte(address++);
        if (b == 255) {
            return;
        }
        auto at = (y * 32 + x++) * 2;
        screen.tiles.at(at) = b;
        screen.tiles.at(at + 1) = 0;
    }
    throw std::runtime_error("Unterminated ending row");
}

void Ending::number(int value, unsigned digits, int x, int y, bool suppress_zero) {
    std::vector<unsigned> d(digits);
    for (unsigned i = digits; i > 0; --i) {
        d[i - 1] = value % 10;
        value /= 10;
    }
    bool visible = !suppress_zero;
    for (unsigned i = 0; i < digits; ++i) {
        visible |= d[i] != 0 || i == digits - 1;
        if (visible) {
            screen.sprites.push_back({x + int(i) * 8, y, 128 + 2 * d[i]});
        }
    }
}

// Adds a sprite layout of up to 3 rows of six 8x16 sprites (see FrontEnd).
void Ending::mapping(unsigned a, int x, int y) {
    if (x < 0 || x >= 208 || y < -24 || y >= 168) {
        return;
    }
    for (int r = 0; r < 3; ++r) {
        if (assets_.byte(a + r * 6) == 255) {
            break;
        }
        for (int c = 0; c < 6; ++c) {
            unsigned tile = assets_.byte(a + r * 6 + c);
            if (tile < 254) {
                screen.sprites.push_back({(x + c * 8) & 255, y + r * 16, tile});
            }
        }
    }
}

void Ending::tally_numbers() {
    screen.sprites.clear();
    number(score, 8, 128, 112);
    if (tally_stage == 0) {
        number(emeralds, 2, 128, 80);
        number(20000, 6, 144, 80);
    } else if (tally_stage == 1) {
        number(lives, 2, 128, 80, false);
        number(5000, 6, 144, 80);
    } else {
        number(bonus * 10000, 6, 144, 80);
    }
}

// The final tally: emeralds every 60 updates, then lives and the bonus every
// 30, with pauses between the stages. Each update draws the old values before
// the counters change, as in the original.
void Ending::tally_tick() {
    tally_numbers();
    ++tally_timer;
    if (--tally_wait_ > 0) {
        return;
    }
    switch (tally_step_) {
    case 0:
        tally_step_ = 1;
        tally_wait_ = 60;
        break;
    case 1:
        if (emeralds) {
            --emeralds;
            score += 20000;
            sound_events.push_back({SoundEvent::Kind::Effect, unsigned(Effect::Ring)});
            tally_wait_ = 60;
        } else {
            tally_step_ = 2;
            tally_wait_ = 180;
        }
        break;
    case 2:
        tally_stage = 1;
        patch(0x4a6e);
        tally_step_ = 3;
        tally_wait_ = 180;
        break;
    case 3:
        tally_step_ = 4;
        tally_wait_ = 30;
        break;
    case 4:
        if (lives) {
            --lives;
            score += 5000;
            sound_events.push_back({SoundEvent::Kind::Effect, unsigned(Effect::Ring)});
            tally_wait_ = 30;
        } else {
            tally_step_ = 5;
            tally_wait_ = 180;
        }
        break;
    case 5:
        tally_stage = 2;
        patch(0x4a7e);
        patch(0x4a5a);
        tally_step_ = 6;
        tally_wait_ = 180;
        break;
    case 6:
        tally_step_ = 7;
        tally_wait_ = 30;
        break;
    case 7:
        if (bonus) {
            --bonus;
            score += 10000;
            sound_events.push_back({SoundEvent::Kind::Effect, unsigned(Effect::Ring)});
            tally_wait_ = 30;
        } else {
            tally_step_ = 8;
            tally_wait_ = 480;
        }
        break;
    case 8:
        next(Phase::ResultsHold);
        break;
    }
    score %= 100000000;
}

// Four characters walk by their own scripts: (duration, layout word) entries,
// with 0xFF followed by a jump address.
void Ending::animate_credits() {
    screen.sprites.clear();
    for (unsigned i = 0; i < 4; ++i) {
        unsigned& p = actor_pointer[i];
        unsigned& t = actor_ticks[i];
        for (int guard = 0;; ++guard) {
            if (guard > 8) {
                throw std::runtime_error("Credits actor loop");
            }
            ++t;
            if (assets_.byte(p) < t) {
                t = 0;
                p += 3;
                if (assets_.byte(p) == 255) {
                    p = assets_.word(p + 1);
                    continue;
                }
            }
            break;
        }
        unsigned map = assets_.word(p + 1);
        mapping(map + 2, assets_.byte(map), assets_.byte(map + 1));
    }
}

void Ending::credit_commands() {
    // 21C5: the credits text stream. 0xFE x y moves the cursor, 0xFD w waits
    // w updates, 0xFC n scrolls the text up n rows, 0xFF ends; any other byte
    // is a letter tile, written after a 9-update delay.
    for (int guard = 0; guard < 1024; ++guard) {
        unsigned b = assets_.byte(text_pointer++);
        if (b == 255) {
            next(Phase::CreditsHold);
            return;
        }
        if (b == 254) {
            text_x = assets_.byte(text_pointer++);
            text_y = assets_.byte(text_pointer++);
            continue;
        }
        if (b == 253) {
            text_wait = assets_.word(text_pointer);
            text_pointer += 2;
            return;
        }
        if (b == 252) {
            scroll_rows = assets_.byte(text_pointer++);
            text_wait = 12;
            return;
        }
        pending_tile_ = b;
        pending_letter_ = true;
        text_wait = 9;
        return;
    }
    throw std::runtime_error("Unbounded credits command stream");
}

void Ending::tick() {
    sound_events.clear();
    ++ticks;
    ++phase_ticks;
    // Fades step each palette nibble once every four updates.
    bool fading = phase == Phase::WhiteOut || phase == Phase::GoodMapIn ||
                  phase == Phase::BlackOut || phase == Phase::CreditsIn ||
                  phase == Phase::CreditsOut;
    if (fading && phase_ticks >= 4 && !((phase_ticks - 4) & 3)) {
        for (unsigned i = 0; i < 64; ++i) {
            int l = screen.palette[i] & 15, h = screen.palette[i] >> 4, a = fade_target_[i] & 15,
                b = fade_target_[i] >> 4;
            l += (l < a) - (l > a);
            h += (h < b) - (h > b);
            screen.palette[i] = l | (h << 4);
        }
    }
    switch (phase) {
    case Phase::EmeraldHold:
        screen.sprites = {{120, 96, 92}, {128, 96, 94}};
        if (phase_ticks == 60) {
            next(Phase::EmeraldFlight);
            sound_events.push_back({SoundEvent::Kind::Music, unsigned(Music::Ending)});
        }
        break;
    case Phase::EmeraldFlight: {
        if (phase_ticks & 1) {
            break; // 203B/2045: two waits precede each submission.
        }
        unsigned step = phase_ticks / 2 - 1;
        screen.sprites.clear();
        for (unsigned i = 0; i < 3; ++i) {
            unsigned a = 0x1b69 + step * 6 + i * 2;
            int x = assets_.byte(a), y = assets_.byte(a + 1);
            for (int sign : {1, -1}) {
                screen.sprites.push_back({(120 + sign * x) & 255, (96 + sign * y) & 255, 92});
                screen.sprites.push_back({(128 + sign * x) & 255, (96 + sign * y) & 255, 94});
            }
        }
        if (phase_ticks == 122) {
            next(Phase::WhiteOut);
            fade(assets_.palette(23, 23));
        }
        break;
    }
    case Phase::WhiteOut:
        if (phase_ticks == 68) {
            screen.tiles = assets_.screens[7].tiles;
            screen.sprites.clear();
            next(Phase::GoodMapIn);
            fade(assets_.palette(10, 11));
        }
        break;
    case Phase::GoodMapIn:
        if (phase_ticks == 68) {
            next(Phase::MapHold);
        }
        break;
    case Phase::MapHold:
        if (phase_ticks == 240) {
            sound_events.push_back({SoundEvent::Kind::Stop, 0});
            for (unsigned a :
                 {0x49f0u, 0x4a02u, 0x4a14u, 0x4a26u, 0x4a33u, 0x4a40u, 0x4a4du, 0x4a5eu}) {
                patch(a);
            }
            next(Phase::Tally);
            tally_wait_ = 180;
        }
        break;
    case Phase::Tally:
        tally_tick();
        break;
    case Phase::ResultsHold:
        if (phase_ticks == 240) {
            next(Phase::BlackOut);
            fade(assets_.palette(22, 22));
        }
        break;
    case Phase::BlackOut:
        if (phase_ticks == 68) {
            next(Phase::BlackHold);
        }
        break;
    case Phase::BlackHold:
        if (phase_ticks == 120) {
            auto retained = screen.sprites;
            load_screen(8);
            screen.sprites = std::move(retained);
            next(Phase::CreditsPrepare);
        }
        break;
    case Phase::CreditsPrepare:
        animate_credits();
        next(Phase::CreditsIn);
        fade(assets_.palette(21, 21));
        break;
    case Phase::CreditsIn:
        if (phase_ticks == 68) {
            next(Phase::Credits);
            sound_events.push_back({SoundEvent::Kind::Music, unsigned(Music::Credits)});
            text_x = assets_.byte(text_pointer++);
            text_y = assets_.byte(text_pointer++);
            credit_commands();
        }
        break;
    case Phase::Credits:
        animate_credits();
        if (text_wait && !--text_wait) {
            if (scroll_rows) {
                for (unsigned row = 0; row < 9; ++row) {
                    for (unsigned b = 0; b < 20; ++b) {
                        screen.tiles[0x29e + row * 64 + b] = screen.tiles[0x2de + row * 64 + b];
                    }
                }
                if (--scroll_rows) {
                    text_wait = 12;
                    break;
                }
            }
            if (pending_letter_) {
                unsigned at = (text_y * 32 + text_x++) * 2;
                screen.tiles.at(at) = pending_tile_;
                screen.tiles.at(at + 1) = 0;
                pending_letter_ = false;
            }
            credit_commands();
        }
        break;
    case Phase::CreditsHold:
        if (phase_ticks == 300) {
            next(Phase::CreditsOut);
            fade(assets_.palette(22, 22));
        }
        break;
    case Phase::CreditsOut:
        if (phase_ticks == 68) {
            next(Phase::Done);
        }
        break;
    case Phase::Done:
        break;
    }
}
} // namespace sonic1_gg
