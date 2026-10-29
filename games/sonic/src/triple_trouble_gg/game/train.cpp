// Sunset Park act 3 (zone 1, act 2): the player rides the roof of a
// train moving left (bank 0C, BB86-BD45; player states 2A-2F). He can only
// step left and right (from X 40 up to the limit DC57) and jump; the world
// drifts him back half a pixel a frame. The roof is at Y CE; while DC56 bit
// 3 is set a gap (32 pixels from X DBEB) lets him fall through. A hit there
// throws him off (state 2D); falling below the train kills him.
//
// At the end the boss (type 51, bank 17, 8FED-91DB) rides the train at
// band 4's place: it holds the player back, and a rolling player bounces
// off it, costing it a hit (3; 4 against Tails); beaten it blows up and
// signals the director (DC56 bit 6).
//
// The scene is drawn in horizontal bands, each scrolled by its own byte
// (DBE7-DBEC, in the decoder's work space; Game::band_scroll and the line
// interrupt, 0930). The director (type 13, bank 1E, 8CC2-91AD) runs the
// ride: it speeds the bands up, sends enemies, redraws the map columns that
// scroll in, and ends with the slowdown at the station. The gap moves with
// band 4 (DBEB).

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {
constexpr u8 roof_y = 0xCE;
constexpr u8 below_train = 0xEE;
} // namespace

namespace {
// 8FAD / 8FD2: per step of the speed-up / slowdown the bands' speeds
// (background, band 4, band 5).
constexpr std::uint16_t speed_up = 0x8FAD, slow_down = 0x8FD2;
constexpr u8 ride_song = 0x93, station_song = 0x89;

// The chain of map redraws (8D3C-8DE8): the script position each step
// moves to when band 3 (or later band 4) reaches its mark.
struct Redraw {
    u8 mark;
    std::uint16_t next;
    unsigned column; // 1CF4-1D33: block 0 at 3820, 3828, ... 3818
};

constexpr std::array<Redraw, 8> column_redraws{{
    {0x98, 0x8D4C, 0x3820},
    {0x78, 0x8D5C, 0x3828},
    {0x58, 0x8D6C, 0x3830},
    {0x38, 0x8D7C, 0x3838},
    {0x18, 0x8D8C, 0x3800},
    {0xF8, 0x8D9C, 0x3808},
    {0xD8, 0x8DAC, 0x3810},
    {0xB8, 0x8DBC, 0x3818},
}};
} // namespace

u8& Game::band(unsigned k) {
    return act.work[1 + k]; // DBE7 + k
}

u8 Game::train_gap_x() const {
    return act.work[5]; // DBEB
}

// The director's bytes: counter (1E) the speed step, timer (1F) its
// frames, scratch 0-1 (34) the speed table's place, 2-3 and 4-5 (36, 38)
// the fractions of bands 3 and 2; display bit 4 band 4's direction at the
// end.
namespace {
std::uint16_t word_at(const Object& o, unsigned i) {
    return std::uint16_t(o.scratch[i] | o.scratch[i + 1] << 8);
}

void set_word(Object& o, unsigned i, std::uint16_t v) {
    o.scratch[i] = lo(v);
    o.scratch[i + 1] = hi(v);
}
} // namespace

// 1CDE + 1DE4: a 4x4 block of the act's map at a fixed name table place.
void Game::draw_block_at(unsigned name_address, unsigned block) {
    const unsigned tiles = rom.word(level.block_bank, level.block_table + block * 2u);
    draw_rect_at(name_address, level.block_bank, tiles, 0x04, 0x04);
}

bool Game::run_train_routine(Object& o, Routine r) {
    switch (r) {
    case Routine::train_enter_start: // BD18 (state 2F): still, facing right; the right limit 98
        o.display &= u8(~display_flag::mirrored);
        o.vx = 0;
        o.vy = 0;
        act.ride_limit = 0x98;
        return true;
    case Routine::train_enter: // BD2B: dropping onto the roof (gravity 1/8); the script then rides
                               // (2A)
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o);    // 5359 (0381)
        object_terrain(o); // 713D (0384)
        if (lo(o.x) < 0x38) {
            o.x = std::uint16_t((o.x & 0xFF00) | 0x38);
        }
        return true;
    case Routine::train_ride_start: // BB86 (state 2A): still, facing right
        o.display &= u8(~display_flag::mirrored);
        o.flags &= u8(~0x02);
        o.vx = 0;
        o.vy = 0;
        return true;
    case Routine::train_thrown_start: // BCB8 (state 2D): thrown back and up
        o.vx = 0xFF00;
        o.vy = 0xFD00;
        return true;
    case Routine::train_leave: // BCF9 (state 2E, the act's end): falling (1/8); past the screen's
                               // right the act ends
        o.vy = std::uint16_t(o.vy + 0x20);
        move_object(o);
        if (std::uint16_t(camera.x + 0x0100 + wide_right) < o.x) { // Widescreen: past the view
            flow.frame_work |= 0x10;
        }
        return true;
    case Routine::roof_ring_start: // 1E:9269 (type 15): a ring rolling along the roof from (1000,
                                   // 1CE) at 2
        o.tile_base = o.tile_base_mirrored = 0x10;
        o.next_state = 0x01;
        o.vx = 0xFE00;
        o.vy = 0;
        o.x = 0x1000;
        o.y = 0x01CE;
        return true;
    case Routine::roof_ring_roll: // 9296: taken: sound C9, a ring more (the low BCD byte), a
                                  // sparkle (state 2)
        move_object(o);           // 5359 (0381)
        if (touches_player(o)) {  // 53FD (0372)
            o.next_state = 0x02;
            sound_.request(0xC9, 1);
            score.rings_low = bcd_add(score.rings_low, 1);
        }
        return true;
    case Routine::train_boss_start: // 17:9111
        train_boss_start(o);
        return true;
    case Routine::train_boss_shot: // 9154: flying left until X (low byte) 1E, then gone
        if (lo(o.x) >= 0x1E) {
            move_object(o); // 5359 (0381)
        } else {
            o.type = 0xFF;
        }
        return true;
    case Routine::train_boss_arrive: // 9161: the player's limit BC
        act.ride_limit = 0xBC;
        train_boss_follow(o);
        return true;
    case Routine::train_boss_fight: // 9168
        train_boss_touch(o);        // 9174
        train_boss_follow(o);
        return true;
    case Routine::train_boss_ride: // 916B
        train_boss_follow(o);
        return true;
    case Routine::train_boss_beaten: // 91D6: the director slows the train
        act.effect_flags |= 0x40;
        return true;
    case Routine::director_start: // 1E:8E15
        o.next_state = 0x01;
        o.display |= 0x02;
        o.counter = 0;
        o.timer = 0x20;
        set_word(o, 0, speed_up);
        script_camera(0x0F01, 0x0152); // 4CDA (0321)
        sound_.request(ride_song, 2);
        return true;
    case Routine::director_wait: // 8E3C
        flow.act = 0x02;
        return true;
    case Routine::director_landing: // 8E42: the player down on the roof: the effects on (1C72),
                                    // speeding up (3)
        if (objects[0].terrain & 0x02) {
            act.effect_flags |= 0x01;
            o.next_state = 0x03;
            objects[0].next_state = 0x2F; // 3ED0 (03C9)
        }
        return true;
    case Routine::director_speed_up: // 8F35
        change_speed(o, true);
        return true;
    case Routine::director_slow_down_start: // 8FBC
        o.counter = 0;
        o.timer = 0x20;
        set_word(o, 0, slow_down);
        sound_.request(0xCB, 1);
        return true;
    case Routine::director_slow_down: // 8FE1
        change_speed(o, false);
        return true;
    case Routine::director_stop: // 904B: the player on the platform: the ride ends (player state
                                 // 2E), its song
        if ((objects[0].terrain & 0x02) && lo(objects[0].y) >= 0xCC && !(act.effect_flags & 0x80)) {
            auto& p = objects[0];
            player.hit = 0;
            p.touch_sides = 0;
            p.next_state = 0x2E;
            p.vx = 0;
            p.vy = 0;
            act.effect_flags |= 0x80;
            sound_.request(station_song, 2);
        }
        return true;
    case Routine::director_ride: // 8E5F
        ride_bands(o);
        return true;
    case Routine::director_ride_closer: // 8E54: the player's limit closes in to 60
        if (act.ride_limit >= 0x60) {
            --act.ride_limit;
        }
        ride_bands(o);
        return true;
    case Routine::director_gap_open: // 907D
        act.effect_flags |= 0x02;
        return true;
    case Routine::director_gap_close: // 9083
        act.effect_flags |= 0x04;
        return true;
    case Routine::director_redraw_0: // 9089-9131: band 3 at a mark: the next column redrawn
    case Routine::director_redraw_1:
    case Routine::director_redraw_2:
    case Routine::director_redraw_3:
    case Routine::director_redraw_4:
    case Routine::director_redraw_5:
    case Routine::director_redraw_6:
    case Routine::director_redraw_7: {
        const auto& step = column_redraws[unsigned(r) - unsigned(Routine::director_redraw_0)];
        ride_bands(o);
        if (band(3) != step.mark) {
            return true;
        }
        jump_script(o, step.next);
        o.frame_ticks = 0x01;
        if (r == Routine::director_redraw_7) {
            act.effect_flags |= 0x10;
        }
        draw_block_at(step.column, 0); // 04B0-04C5
        return true;
    }
    case Routine::director_station_0: // 914E-9196: band 4 at a mark: the station's columns
    case Routine::director_station_1:
    case Routine::director_station_2:
    case Routine::director_station_3:
        station_columns(o, unsigned(r) - unsigned(Routine::director_station_0));
        return true;
    case Routine::director_end: // 8EB3
        director_end(o);
        return true;
    default:
        return false;
    }
}

// 9111: the hit flash colour; 3 hits (4 against Tails), tiles 6E;
// parameter 0 the boss (state 2: its song, at (F60, 1CE), kept off
// screen), others its shots (state 1).
void Game::train_boss_start(Object& o) {
    set_boss_flash_color(); // 5888 (050D)
    o.status_ticks = objects[0].type == 0x01 ? 0x03 : 0x04;
    o.tile_base = o.tile_base_mirrored = 0x6E;
    if (o.param) {
        o.next_state = 0x01;
        return;
    }
    o.next_state = 0x02;
    sound_.request(0x8A, 2);
    o.x = 0x0F60;
    o.y = 0x01CE;
    o.display |= 0x02;
}

// 916B: its X (low byte) band 4's plus 3D.
void Game::train_boss_follow(Object& o) {
    o.x = std::uint16_t((o.x & 0xFF00) | u8(train_gap_x() + 0x3D));
}

// 9174: after a short wait (34, then 1E), a player on the roof (Y low
// byte 8E on) who reaches 64 left of it is held there, or, rolling,
// thrown back and up (state 2B), sound C1: a hit; the last one blows it
// up (state 3).
void Game::train_boss_touch(Object& o) {
    if (o.scratch[0]) {
        --o.scratch[0];
        return;
    }
    auto& p = objects[0];
    if (p.state == 0x1F) {
        return;
    }
    if (o.counter) {
        --o.counter;
        return;
    }
    if (lo(p.y) < 0x8E) {
        return;
    }
    const u8 edge = u8(lo(o.x) - 0x40);
    if (lo(p.x) < edge) {
        return;
    }
    if (!(p.flags & 0x02)) {
        p.x = std::uint16_t((p.x & 0xFF00) | edge);
        return;
    }
    o.counter = 0x10;
    p.next_state = 0x2B;
    p.vy = 0xFC00;
    p.vx = 0xFE00;
    p.flags |= 0x02;
    sound_.request(0xC1, 1);
    o.scratch[0] = 0x08;
    if (!--o.status_ticks) {
        ++o.next_state;
    }
}

// 8F35 / 8FE1: every 32 frames the next step of the speed table, four
// steps (then state 2 riding, or 5 stopped); meanwhile bands 3 and 2 slow
// by 1/8 and 3/16 (the slowdown: band 2 only), the background (and band 4
// while speeding up) and band 5 by the table's step.
void Game::change_speed(Object& o, bool up) {
    if (!--o.timer) {
        o.timer = 0x20;
        set_word(o, 0, std::uint16_t(word_at(o, 0) + 3));
        if (++o.counter >= 0x04) {
            o.next_state = up ? 0x02 : 0x05;
            return;
        }
    }
    set_word(o, 2, std::uint16_t(word_at(o, 2) - 0x20));
    set_word(o, 4, std::uint16_t(word_at(o, 4) - 0x30));
    const Table speeds{0x1E, word_at(o, 0)};
    if (up) {
        band(3) = o.scratch[3];
        band(2) = o.scratch[5];
    } else {
        band(2) = o.scratch[3];
    }
    camera.scroll_x = u8(camera.scroll_x - rom.byte(speeds, 0));
    band(0) = camera.scroll_x;
    if (up) {
        band(4) = u8(band(4) - rom.byte(speeds, 1));
    }
    band(5) = u8(band(5) - rom.byte(speeds, 2));
}

// 8E5F: full speed: the background 8 a frame, band 3 1/4, band 2 3/8
// (once DC56 bit 4 is set band 3 follows band 4), band 4 2, band 5 4.
void Game::ride_bands(Object& o) {
    camera.scroll_x = u8(camera.scroll_x - 8);
    band(0) = camera.scroll_x;
    set_word(o, 2, std::uint16_t(word_at(o, 2) - 0x40));
    set_word(o, 4, std::uint16_t(word_at(o, 4) - 0x60));
    band(3) = o.scratch[3];
    band(4) = u8(band(4) - 2);
    if (act.effect_flags & 0x10) {
        band(3) = band(4);
    } else {
        band(2) = o.scratch[5];
    }
    band(5) = u8(band(5) - 4);
}

// 914E-9196: with the player's limit closing in, as band 4 passes D8 (then
// below B8, 98, 78) the station's columns are drawn (1D3C-1DBA: blocks
// F0/9E, F1/9F, F2/19 and 0/0 at 3800-3818 and 3900-3918) and the next
// step follows; the third brings the boss (type 51, script 8DD8).
void Game::station_columns(Object& o, unsigned step) {
    static constexpr std::array<u8, 4> marks{0xD8, 0xB8, 0x98, 0x78};
    static constexpr std::array<std::uint16_t, 4> next{0x8DC8, 0x8DD0, 0x8DD8, 0x8DE8};
    static constexpr std::array<std::array<unsigned, 2>, 4> blocks{
        {{0xF0, 0x9E}, {0xF1, 0x9F}, {0xF2, 0x19}, {0x00, 0x00}}};
    if (act.ride_limit >= 0x60) { // 8E54
        --act.ride_limit;
    }
    ride_bands(o);
    const u8 b = band(4);
    if (step == 0 ? b < marks[0] : b >= marks[step]) {
        return;
    }
    jump_script(o, next[step]);
    o.frame_ticks = 0x01;
    draw_block_at(0x3800 + 8 * step, blocks[step][0]); // 043E-0447
    draw_block_at(0x3900 + 8 * step, blocks[step][1]);
}

// 8EB3: the boss's part: band 4 swings between 5B and B0 (display bit 4
// its way back), at B0 throwing a bomber (type 26, FE) from (band 4 + F00,
// 1C2) with sound CF; the background and bands 2 and 5 run on. DC56 bit 6
// (the boss beaten) moves on to the slowdown (script 8DF0).
void Game::director_end(Object& o) {
    if (act.effect_flags & 0x40) {
        jump_script(o, 0x8DF0); // 8F28
        o.frame_ticks = 0x01;
        return;
    }
    camera.scroll_x = u8(camera.scroll_x - 8);
    band(0) = camera.scroll_x;
    set_word(o, 2, std::uint16_t(word_at(o, 2) - 0x40));
    band(2) = o.scratch[3];
    band(5) = u8(band(5) - 4);
    if (!(o.display & 0x10)) {
        band(4) = u8(band(4) - 1);
        band(3) = band(4);
        if (band(4) < 0x5B) {
            o.display |= 0x10;
        }
        return;
    }
    band(4) = u8(band(4) + 1);
    band(3) = band(4);
    if (band(4) < 0xB0) {
        return;
    }
    o.display &= u8(~0x10);
    spawn_object_at(o, 0x26, 0xFE, std::uint16_t(band(4) + 0x0F00), 0x01C2); // 510A (0357)
    sound_.request(0xCF, 1);
}

// BB98 (state 2A): hit, a button jumps, steps; off the roof he falls (2C).
Flow Game::train_ride(Object& o) {
    co_await train_hit(o);      // BC7F
    if (input.pressed & 0x30) { // BC05: a jump (2B), sound B1
        o.next_state = 0x2B;
        o.vy = 0xFBC0;
        o.flags |= 0x02;
        sound_.request(0xB1, 1);
    }
    train_steps(o); // BC21
    if (!on_train_roof(o)) {
        o.next_state = 0x2C;
    }
}

// BBAB (state 2B): jumping (gravity 0.2); landing on the roof rides again.
Flow Game::train_jump(Object& o) {
    o.vy = std::uint16_t(o.vy + 0x32);
    move_object(o);
    train_steps(o);
    if (lo(o.y) >= below_train) {
        co_await kill_player(o); // 3942 (03DE)
        co_return;
    }
    co_await train_hit(o);
    if (on_train_roof(o)) {
        return_to_roof(o);
    }
}

// BBDC (state 2C): falling through the gap (gravity 0.22), kept within its
// right edge; below the train he dies.
Flow Game::train_fall(Object& o) {
    o.vy = std::uint16_t(o.vy + 0x38);
    move_object(o);
    const u8 edge = u8(train_gap_x() + 0x10);
    if (edge < lo(o.x)) {
        o.x = std::uint16_t((o.x & 0xFF00) | edge);
    }
    train_drift(o); // BC45
    if (lo(o.y) >= below_train) {
        co_await kill_player(o);
    }
}

// BCC5 (state 2D): thrown off (gravity 1/8); falling back onto the roof he
// rides again, below Y 1DA he dies.
Flow Game::train_thrown(Object& o) {
    o.vy = std::uint16_t(o.vy + 0x20);
    move_object(o);
    train_drift(o);
    if (o.vy & 0x8000) {
        co_return;
    }
    if (o.y >= 0x01DA) {
        co_await kill_player(o);
        co_return;
    }
    if (!on_train_roof(o)) {
        co_return;
    }
    o.next_state = 0x2A;
    player.hit = 0;
    o.touch_sides = 0;
}

// BBCB
void Game::return_to_roof(Object& o) {
    o.y = std::uint16_t((o.y & 0xFF00) | roof_y);
    o.vy = 0;
    o.next_state = 0x2A;
}

// BC7F: hit (D3C1) with no rings he dies; otherwise his rings (up to 7 by
// the tens) scatter (type 1E), sound B3, thrown off (2D), no rings left.
Flow Game::train_hit(Object& o) {
    if (!player.hit) {
        co_return;
    }
    if (!score.rings_low) {
        co_await kill_player(o); // 3942 (03DE)
        co_return;
    }
    unsigned count = (score.rings_low >> 4) + 1u;
    if (count >= 8) {
        count = 7;
    }
    for (unsigned i = 0; i < count; ++i) {
        spawn_object(o, 0x1E, u8(i)); // 50E1 (0354)
    }
    sound_.request(0xB3, 1);
    o.next_state = 0x2D;
    score.rings_low = 0;
}

// BC21: the drift, then a pixel right (below the limit) or left (from 41).
void Game::train_steps(Object& o) {
    train_drift(o); // BC45
    u8 x = lo(o.x);
    if (input.held & 0x08) {
        if (u8(x + 1) >= act.ride_limit) {
            return;
        }
        ++x;
    } else if (input.held & 0x04) {
        if (u8(x - 1) < 0x40) {
            return;
        }
        --x;
    } else {
        return;
    }
    o.x = std::uint16_t((o.x & 0xFF00) | x);
}

// BC45: held at X 38 or more; otherwise half a pixel left (the low byte
// and fraction only).
void Game::train_drift(Object& o) {
    if (lo(o.x) < 0x38) {
        o.x = std::uint16_t((o.x & 0xFF00) | 0x38);
        return;
    }
    const std::uint16_t v = std::uint16_t(((o.x & 0xFF) << 8 | o.x_sub) - 0x80);
    o.x = std::uint16_t((o.x & 0xFF00) | (v >> 8));
    o.x_sub = u8(v);
}

// BC5C: on the roof: at or below Y CE and not over the gap (when DC56 bit
// 3 shows it: X from DBEB + 1 to DBEB + 20).
bool Game::on_train_roof(const Object& o) const {
    if (lo(o.y) < roof_y) {
        return false;
    }
    if (!(act.effect_flags & 0x08)) {
        return true;
    }
    const u8 gap = train_gap_x();
    const u8 x = lo(o.x);
    if (gap >= x) {
        return true;
    }
    return u8(gap + 0x20) < x;
}

} // namespace triple_trouble_gg
