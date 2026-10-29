// The title's objects and the name table animations (228E) that reveal
// parts of a screen.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
// 0E:B393: name table animations by number; 0E:B497: colours (four, from
// background colour 1) some of them set.
constexpr Table map_animation_records{0x0E, 0xB393};
constexpr Table map_animation_colors{0x0E, 0xB497};
} // namespace

// 23A3 (044D): starts name table animation `number` in a free record (if
// any); with a colour set its colours are set at once.
void Game::start_map_animation(u8 number) {
    MapAnimation* free = nullptr;
    for (auto& r : map_animations.records) { // 23F6
        if (!r.active) {
            free = &r;
            break;
        }
    }
    if (!free) {
        return;
    }
    const unsigned source = rom.word(map_animation_records, number * 2u);
    auto b = [&](unsigned i) {
        return rom.byte(map_animation_records.bank, source + i);
    };
    MapAnimation& r = *free;
    r.active = b(0);
    r.growth = b(1);
    r.period = b(2);
    r.counter = b(3);
    r.bank = b(4);
    r.steps = b(5);
    r.source_columns = b(6);
    r.columns = b(7);
    r.max_rows = b(8);
    r.rows = b(9);
    r.left_steps = b(10);
    r.colors = b(11);
    r.source = std::uint16_t(b(12) | (b(13) << 8));
    r.name_address = std::uint16_t(b(14) | (b(15) << 8));
    if (r.colors) {
        const unsigned colors = rom.word(map_animation_colors, (r.colors - 1u) * 2u);
        for (unsigned i = 0; i < 8; ++i) {
            palette.colors[2 + i] = rom.byte(map_animation_colors.bank, colors + i);
        }
        palette.dirty = 0xFF;
    }
}

// 228E: every `period` frames each animation draws its rectangle (rows of
// `columns` entries from `source`, whose rows are `source_columns` long)
// at its name table address, then grows: bit 0 to the left (first the
// source moves left `left_steps` times while the width grows, then the
// address moves right), bit 1 to the left by moving the address, bit 3
// upward (the address moves up a row while the height grows). After
// `steps` draws it ends (FF, cleared on the next frame).
Flow Game::run_map_animations() {
    unsigned clocks = 0;
    for (auto& r : map_animations.records) {
        if (!r.active) {
            continue;
        }
        if (r.active == 0xFF) {
            r = {};
            continue;
        }
        if (--r.counter) {
            continue;
        }
        r.counter = r.period;
        di();
        unsigned at = r.name_address, from = r.source;
        for (unsigned row = 0; row < r.rows; ++row) {
            clocks += draw_rect(at, r.bank, from, 1, r.columns);
            at = (at + 0x40) & 0xFFFF;
            from = (from + u8(r.source_columns * 2)) & 0xFFFF;
        }
        ei();
        if (--r.steps == 0) {
            r.active = 0xFF;
            continue;
        }
        const u8 g = r.growth;
        if (g & 0x01) {
            if (r.left_steps) {
                --r.left_steps;
                r.source = std::uint16_t(r.source - 2);
                if (r.source_columns > r.columns) {
                    ++r.columns;
                }
            } else {
                // 2344: (quirk) both bytes of the address get its high byte.
                const std::uint16_t a = std::uint16_t(r.name_address + 2);
                r.name_address = std::uint16_t((a & 0xFF00) | hi(a));
            }
        } else if (g & 0x02) {
            r.name_address = std::uint16_t(r.name_address - 2);
            if (r.source_columns > r.columns) {
                ++r.columns;
            }
        } else if (g & 0x08) {
            r.name_address = std::uint16_t(r.name_address - 0x40);
            if (r.max_rows > r.rows) {
                ++r.rows;
            }
        }
    }
    if (clocks) {
        co_await busy(clocks);
    }
}

// 1E:B5B7: on the title Knuckles runs in from the left at Y 80, facing
// right.
void Game::knuckles_title_start(Object& o) {
    o.x = 0;
    o.y = 0x0080;
    o.vx = 0x0200;
    o.display |= display_flag::mirrored;
}

// 1E:B5D4: past X 60 he jumps (the script goes on at B2F5).
void Game::knuckles_title_run(Object& o) {
    move_object(o);
    if (o.x < 0x0060) {
        return;
    }
    jump_script(o, 0xB2F5);
    o.frame_ticks = 1;
    o.vy = 0xFB00;
    o.vx = 0;
}

// 1E:B601: sound C8; the title's emblem rises (name table animation 11).
void Game::knuckles_title_jump(Object&) {
    sound_.request(0xC8, 1);
    start_map_animation(0x11);
}

// 1E:B616: falling; once below Y 68 on the way down the script goes on
// (B301: the title is up).
void Game::knuckles_title_fall(Object& o) {
    o.vy = std::uint16_t(o.vy + 0x20); // 03AB
    move_object(o);
    if (o.vy & 0x8000) {
        return;
    }
    if (o.y < 0x0068) {
        return;
    }
    jump_script(o, 0xB301);
    o.frame_ticks = 1;
}

// 1E:8B11: type 10 (the title's emblem) at (C0, 6C).
void Game::emblem_start(Object& o) {
    o.next_state = 0x01;
    o.x = std::uint16_t((o.x & 0xFF00) | 0xC0);
    o.y = std::uint16_t((o.y & 0xFF00) | 0x6C);
}

} // namespace triple_trouble_gg
