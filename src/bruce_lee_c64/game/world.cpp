#include "bruce_lee_c64/game/world.hpp"
#include "bruce_lee_c64/game/terrain.hpp"
#include <algorithm>

namespace bruce_lee::game {
namespace {

// Character kinds by the actor flags' top bits ($0B80, $0CD7, $0DEF).
constexpr std::uint8_t kind_yamo = 0x80, kind_ninja = 0x40;

// Sound effects ($24F6).
constexpr std::uint8_t sound_arrival = 0x82, sound_low_hit = 0x87, sound_hit = 0x86, sound_death = 0x8C,
                       sound_lantern = 0x8E;

// Points ($3172), added to the 24-bit score.
constexpr std::uint32_t points_lantern = 0x7D, points_chop = 0x64, points_kick = 0x4B, points_landing = 0x32,
                        points_yamo_down = 0x1C2, points_ninja_down = 0xC8;

// Energy an actor starts with ($0BCB) and the caps the enemies arrive with.
constexpr std::uint8_t full_energy = 0x24, yamo_energy_cap = 0x14, ninja_energy_cap = 0x07;

bool negative(std::uint8_t v) {
    return v & 0x80;
}

} // namespace

std::uint8_t World::draw(std::uint8_t offset) {
    ++random.counter;
    if (random.replay) {
        if (const auto v = random.replay()) {
            return *v;
        }
    }
    return memory_byte(random.counter + offset);
}

World::World(const data::Assets& a) : assets(a), charsets(a.charsets) {
    first_extra_life = assets.first_extra_life;
    sprite_owner.fill(-1);
    // $10BC: the lanterns of every room still hanging, the counts from $4CDB.
    for (std::size_t r = 0; r < progress.size() && r < assets.rooms.size(); ++r) {
        progress[r].lanterns.assign(assets.rooms[r].lanterns.size(), true);
        progress[r].remaining = assets.lanterns_per_room[r];
    }
}


ActorWorld World::actor_world() {
    ActorWorld w{assets, clock, flag40, two_players, lift.timer, lift.down, {}, false, false};
    return w;
}

// $15C0 (the parts the game state sees).
void World::frame() {
    ++random.counter; // $15D0
    // $1640: frame counters (and the idle count every 256 frames, in play).
    if (++frames_low == 0) {
        if (!(idle & 0x80) && playing) {
            ++idle;
        }
        ++frames_high;
    }
    probe_charset = shown_charset; // $15E8
    if (fall_hit_refresh == 0) {
        fall_hits = {2, 2, 2}; // $15DA
    }
    --fall_hit_refresh;
    if (countdown) {
        --countdown; // $1690
    }
    if (playing) {
        // $1636: the room clock, 1 per 256 frames up to $80.
        if (++clock_low == 0 && !negative(clock)) {
            ++clock;
        }
    }
}

// $40A9: three flames, each a random pattern in rows 2-3 of some glyphs,
// held for a random 1-15 passes.
void World::flames() {
    auto set_rows = [&](int set, int glyph, std::uint8_t value) {
        charsets[std::size_t(set)][std::size_t(glyph)][2] = value;
        charsets[std::size_t(set)][std::size_t(glyph)][3] = value;
    };
    // Y at $40A9: $40, or $2F when the blink ($1AB1) has just printed.
    const std::uint8_t y = blink_printed ? 0x2F : 0x40;
    auto timer = [&](std::uint8_t& t) {
        std::uint8_t v;
        do {
            v = draw(y) & 0x0F;
        } while (v == 0);
        t = v;
    };
    if (--flame_timers[0] == 0) {
        timer(flame_timers[0]);
        const std::uint8_t v = draw(y) & 0x30;
        set_rows(1, 0x93, v);
        set_rows(0, 0x93, v | 0xCC);
        set_rows(0, 0x86, (v | 0xCC) & 0xB8);
    }
    if (--flame_timers[1] == 0) {
        timer(flame_timers[1]);
        const std::uint8_t v = draw(y) & 0x30;
        set_rows(1, 0xB1, v);
        set_rows(0, 0xB1, v | 0xCC);
    }
    if (--flame_timers[2] == 0) {
        timer(flame_timers[2]);
        const std::uint8_t v = draw(y) & 0x0C;
        set_rows(1, 0x94, v);
        set_rows(0, 0x94, v | 0x33);
        set_rows(0, 0xE7, (v | 0x33) & 0x2E);
    }
}

// $0D98
void World::enemy_timers() {
    if (ninja_timer) {
        --ninja_timer;
    }
    if (yamo_timer) {
        --yamo_timer;
    }
}

// $0D36 / $0D3A / $0D6A: the wait until the next enemy, from the room's
// table; late in a room it is short (3), past $19 halved.
void World::enemy_timer_for(int slot) {
    const auto rules = [&](std::uint8_t& timer, std::uint8_t start) {
        timer = start;
        if (clock >= 0x20 || (clock < 0x19 && flag40)) {
            timer = 3;
        } else if (clock >= 0x19) {
            timer >>= 1;
        }
    };
    if (slot != 2) {
        rules(yamo_timer, two_players ? 0x30 : assets.yamo_waits[std::size_t(room)]);
    }
    if (slot != 1) {
        rules(ninja_timer, assets.ninja_waits[std::size_t(room)]);
    }
}

// $0B80: an actor enters.
void World::place_actor(int slot, std::uint8_t kind_flags) {
    // Only these fields are set; the rest (history, last move, ...) stays.
    Actor& a = actors.slots[std::size_t(slot)];
    a.flags = kind_flags | 1;
    const int kind = a.kind();
    // $0B8C: the actor's sprites, for the collision check.
    for (std::uint8_t sprite_code : {assets.actor_sprites[std::size_t(kind)].first,
                                     assets.actor_sprites[std::size_t(kind)].second}) {
        if (sprite_code != 0) {
            sprite_owner[std::size_t(assets.sprite_slot_of_code[std::size_t(sprite_code - 3)])] = std::int8_t(slot);
        }
    }
    a.sprite_codes = assets.actor_sprites[std::size_t(kind)];
    // $0C3F: the slot's two sprite pointers, from the half of the block
    // table shown next.
    {
        const std::size_t first = assets.first_sprite[std::size_t(slot)];
        const auto block = assets.sprite_blocks[first + (second_blocks ? 5 : 0)];
        sprite_pointers[first] = block;
        sprite_pointers[first + 1] = std::uint8_t(block + 1);
    }
    a.move = move::stand;
    a.step = 0;
    a.delay = 0;
    a.stick = stick::none;
    a.fire = 1;
    a.terrain.feet = 1;
    a.facing = 1;
    a.frame = 2;
    a.pose = 2;
    a.d1 = full_energy;
    const auto start = [&] {
        a.x = assets.start_x[std::size_t(room)];
        a.y = assets.start_y[std::size_t(room)];
    };
    const auto other = [&] {
        a.x = assets.other_x[std::size_t(room)];
        a.y = assets.other_y[std::size_t(room)];
    };
    if (!playing || slot == 0) {
        start();
        return;
    }
    if (a.carried_x != 0) {
        // $0C31: an enemy that followed Bruce from the last room.
        a.x = a.carried_x;
        a.y = a.carried_y;
        a.d1 = a.carried_energy;
        a.carried_x = 0;
        return;
    }
    // $0BF0-$0C2F: the room's start place or its other place, partly at
    // random (offset: the actor's kind, which the code holds in Y). The
    // Ninja's choice reads the Yamo's table (as the original does).
    const auto offset = std::uint8_t(kind);
    const std::uint8_t first = slot == 1 ? assets.yamo_places[std::size_t(room)]
                                         : assets.ninja_places[std::size_t(room)];
    if (first != 0 && draw(offset) < 0xC0) {
        if (negative(assets.yamo_places[std::size_t(room)])) {
            other();
        } else {
            start();
        }
        return;
    }
    if (draw(offset) & 1) {
        other();
    } else {
        start();
    }
}

// $0D03 / $0CC7: the Yamo arrives when its timer runs out.
void World::yamo_arrives() {
    if (yamo_timer != 0 || actors.slots[1].present()) {
        return;
    }
    pending_sounds.push_back(sound_arrival);
    yamo_arrived = true;
    place_actor(1, kind_yamo);
    Actor& a = actors.slots[1];
    a.move = move::turn_to_strike;
    a.frame = 0x2A;
    a.pose = 0x6C;
    a.step = 0x0A;
    a.d1 = std::min(a.d1, yamo_energy_cap);
    enemy_timer_for(1);
}

// $0DA5 / $0DEC: the Ninja arrives; while the Yamo is about, he usually waits.
void World::ninja_arrives() {
    if (ninja_timer != 0) {
        return;
    }
    if (actors.slots[1].present() && !flag40 && !two_players && clock < 0x19) {
        // Y: 3 after the Yamo's arrival in this pass, else $40.
        if (draw(yamo_arrived ? 3 : 0x40) >= 0x19) {
            ninja_timer = 0x10;
            return;
        }
    }
    if (actors.slots[2].present()) {
        return;
    }
    pending_sounds.push_back(sound_arrival);
    place_actor(2, kind_ninja);
    Actor& a = actors.slots[2];
    a.pose = 0x82;
    a.d1 = std::min(a.d1, ninja_energy_cap);
    a.step = 0x10;
    // $0E0F: face Bruce.
    a.facing = a.x < actors.slots[0].x ? 1 : 0;
    enemy_timer_for(2);
}

// $2FFE: an actor leaves.
void World::remove_actor(int slot) {
    Actor& a = actors.slots[std::size_t(slot)];
    a.flags &= std::uint8_t(~1);
    for (std::uint8_t sprite_code : {a.sprite_codes.first, a.sprite_codes.second}) {
        if (sprite_code != 0) {
            sprite_owner[std::size_t(assets.sprite_slot_of_code[std::size_t(sprite_code - 3)])] = -1;
        }
    }
    enemy_timer_for(slot);
}

// $521B
void World::probe_actors() {
    for (int slot = 2; slot >= 0; --slot) {
        Actor& a = actors.slots[std::size_t(slot)];
        if (a.present()) {
            if (before_probe) {
                before_probe(slot);
            }
            const auto& charset = charsets[std::size_t(probe_charset)];
            a.terrain = probe_terrain(field, charset, assets.masks[a.pose], a.x, a.y);
        }
    }
}

// $3129: the score, and an extra fall when it reaches the next mark. The
// test goes byte by byte from the top and only stops at a smaller byte, so
// a score past the mark in a higher byte but below it in a lower one does
// not count yet (as in the original).
void World::add_score(std::uint32_t points) {
    score = (score + points) & 0xFFFFFF;
    for (int shift = 16; shift >= 0; shift -= 8) {
        if ((score >> shift & 0xFF) < (next_life_at >> shift & 0xFF)) {
            draw_score();
            return;
        }
    }
    next_life_at = (next_life_at + assets.extra_life_step) & 0xFFFFFF;
    ++lives;
    draw_status();
}

// $3181: in play, a score above the top score becomes the top score. The
// copy takes four bytes, the fourth into the first extra fall's mark's top
// byte ($4378) from the interrupt's band counter ($44), which held 0 each
// time this was seen: the next game's mark stays (modelled so).
void World::update_top_score() {
    if (playing && score > top_score) {
        top_score = score;
        first_extra_life = (first_extra_life & 0xFFFF) | std::uint32_t(band_counter) << 16;
    }
    draw_status();
}

// The code $2E5B reads at a screen index. Past the 1000 screen cells it
// reads on in memory (only the stack slip below gets there): 16 bytes the
// player screen filled with $DC ($0EB6), the 8 sprite pointers, then the
// collision mask stored at $9000, pose 0's ($513C builds it there last).
std::uint8_t World::code_at(int index) const {
    if (index < Playfield::columns * Playfield::rows) {
        return field.at(index % Playfield::columns, index / Playfield::columns).code;
    }
    index -= Playfield::columns * Playfield::rows;
    if (index < 16) {
        return 0xDC;
    }
    if (index < 24) {
        return sprite_pointers[std::size_t(index - 16)];
    }
    const auto& mask = assets.masks[0];
    return std::size_t(index - 24) < mask.size() ? mask[std::size_t(index - 24)] : 0;
}

// $2E5B / $2E76: the three cells of one map row under an actor. The test
// flag $53 (`touching`, the feet's bit 1) shifts once per cell that is not
// an exit or room 16's extra fall; lanterns, deadly cells and crumbling
// floors count only while it is not shifted out.
World::RowScan World::scan_row(int slot, int map_row, std::uint8_t& touching, PassEvents& events) {
    Actor& a = actors.slots[std::size_t(slot)];
    const int column = (a.x - 12) >> 2; // $1C4B
    const int screen_row = map_row < 2 ? map_row : 2 * map_row - 1;
    for (int c = 0; c < 3; ++c) {
        const int index = screen_row * Playfield::columns + column + c;
        if (index < 0) {
            continue;
        }
        const bool on_screen = index < Playfield::columns * Playfield::rows;
        const int cell_column = index % Playfield::columns;
        const std::uint8_t code = code_at(index) & 0x7F;
        // $2E76: exits 2-5. An exit whose x is 0 passes on to the next
        // type's ($2F3C: BNE), type 5 is taken as it is.
        if (code >= 2 && code <= 5) {
            if (!exit_open(a)) {
                continue;
            }
            int type = code;
            const auto& exits = assets.rooms[std::size_t(room)].exits;
            while (type < 5 && exits[std::size_t(type - 2)].x == 0) {
                ++type;
            }
            if (slot != 0) {
                remove_actor(slot); // an enemy walks out
                return RowScan::enemy_left;
            }
            const auto& exit = exits[std::size_t(type - 2)];
            a.x = exit.x;
            a.y = exit.y;
            events.room_change = exit.room;
            return RowScan::room_change;
        }
        if (code == 0x62) {
            // $3109: the lift: ride it (not when a cell before has just
            // been deadly).
            if (a.move != move::dying && (a.terrain.feet & terrain::ladder)) {
                a.move = move::ride;
                a.step = 0x10;
                a.delay = 0;
                a.ride_flag = 0;
            }
        }
        if (code == 0x2D || code == 0x2E) {
            if (room == 16 && slot == 0 && a.y >= 0xA0) {
                extra_life();
            }
            continue;
        }
        touching = std::uint8_t(touching << 1);
        if (touching == 0) {
            continue;
        }
        if (code == 0x13 || code == 0x14 || code == 0x31) {
            if (slot == 0 && on_screen) {
                take_lantern(cell_column, map_row);
            }
            continue;
        }
        const bool deadly = code == 0x64 || code == 0x0F || code == 0x40 ||
                            (code >= 0x54 && code < 0x5B && code != 0x57) || (code >= 0x71 && code < 0x7D) ||
                            (code == 0x66 && room >= 10);
        if (deadly) {
            if (room == 18) {
                continue;
            }
            pending_sounds.push_back(sound_death);
            ++hazard_deaths[std::size_t(slot)];
            if (slot != 0) {
                remove_actor(slot); // the remaining cells are still tested
            } else {
                a.move = move::dying; // $406A
                a.step = 0x10;
            }
            continue;
        }
        if (code == 0x60 && on_screen) {
            mark_crumble(cell_column, map_row);
        }
    }
    return RowScan::go_on;
}

// $2E26: what the actors stand in: three cells in each of two map rows.
//
// The stack slip: an enemy leaving through an exit ($2F7B) drops one return
// address and jumps back into the actor loop, so the loop's last RTS lands
// in the row scan again. If the enemy left in its first row, Bruce's row
// one further down is scanned too (a third row, $2E4E with X = 0); past the
// bottom of the screen that reads the bytes after screen RAM (code_at).
void World::terrain_events(PassEvents& events) {
    std::uint8_t touching = 0; // $53, kept from actor to actor
    bool slipped = false;
    int bruce_next_row = -1;   // $E9 for Bruce after his scan
    for (int slot = 2; slot >= 0; --slot) {
        Actor& a = actors.slots[std::size_t(slot)];
        const int lower = room == 13 ? 4 : 0; // room 13 tests a little lower
        const int map_row = (std::uint8_t(a.y + lower) - 12) >> 4;
        if (slot == 0) {
            bruce_next_row = map_row; // as $1C4B leaves it (hypothesis when Bruce is skipped)
        }
        if (!a.present() || a.move == move::dying) {
            continue;
        }
        if (a.move == move::ride) {
            a.move = move::stand; // $2DFD
            a.step = 0;
            a.delay = 0;
        }
        touching = a.terrain.feet & 2;
        for (int r = 0; r < 2; ++r) {
            const auto result = scan_row(slot, map_row + r, touching, events);
            if (slot == 0) {
                bruce_next_row = map_row + r;
            }
            if (result == RowScan::room_change) {
                return;
            }
            if (result == RowScan::enemy_left) {
                slipped = slipped || r == 0;
                break;
            }
        }
    }
    if (slipped) {
        scan_row(0, bruce_next_row + 1, touching, events);
    }
}

// $30D1: an exit is taken only at the room's edge while moving out: left,
// right or up, or falling out at the bottom (anywhere in room 11).
bool World::exit_open(const Actor& a) const {
    if (room == 11) {
        return true;
    }
    if (a.x < 0x10 && a.steps[2]) {
        return true;
    }
    if (a.x > 0xA1 && a.steps[3]) {
        return true;
    }
    if (a.y > 0xC5 && a.move == move::fall) {
        return true;
    }
    if (a.y < 0x31 && a.steps[0]) {
        return true;
    }
    return false;
}

// $2EF1 / $3309: Bruce takes a lantern hanging at this cell.
void World::take_lantern(int column, int map_row) {
    auto& lanterns = progress[std::size_t(room)];
    const auto& list = assets.rooms[std::size_t(room)].lanterns;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (lanterns.lanterns[i] && list[i].column == column && (list[i].screen_row + 1) / 2 == map_row) {
            lanterns.lanterns[i] = false;
            --lanterns.remaining;
            const auto code = list[i].taken_code;
            field.at(column, list[i].screen_row).code = code;
            field.at(column, list[i].screen_row + 1).code = std::uint8_t(code | 0x80);
            add_score(points_lantern);
            pending_sounds.push_back(sound_lantern);
            return;
        }
    }
}

// $2FD3: room 16's extra fall (five per player and game, one per visit:
// it is drawn over once taken, and on entry when none are left).
void World::extra_life() {
    --extra_falls[std::size_t(player)];
    ++lives;
    draw_status();
    pending_sounds.push_back(sound_lantern);
    erase_extra_fall();
}

// $3068: a floor cell of code $60 an actor stands in: up to three are
// remembered (from the row and column before it, where $3215 draws), the
// cell's colour set to $FD; a cell of that colour is not taken again.
void World::mark_crumble(int column, int map_row) {
    auto& cell = field.at(column, map_row < 2 ? map_row : 2 * map_row - 1);
    if ((cell.colour & 7) == 5) {
        return;
    }
    for (auto& c : crumbles) {
        if (c.timer == 0) {
            c = {1, map_row - 1, column - 1};
            cell.colour = 0x0D;
            return;
        }
    }
}

// $29B0: who hits whom. The VIC's collision register says which sprites
// touch; for each touching pair the other actor's move decides the hit.
void World::combat(std::uint8_t collisions) {
    std::array<std::uint8_t, 3> touching{}; // $B3: bit = sprite of the other actor
    std::array<std::uint8_t, 3> hit{};      // $BC: what happened to the actor
    if (collisions == 0) {
        return;
    }
    if (collisions & 0x03) {
        if (collisions & 0x0C) {
            touching[0] |= 0x08;
            touching[1] |= 0x02;
        }
        if (collisions & 0x30) {
            touching[0] |= 0x10;
            touching[2] |= 0x02;
        }
    }
    if ((collisions & 0x0C) && (collisions & 0x30)) {
        touching[1] |= 0x10;
        touching[2] |= 0x08;
    }
    auto& s = actors.slots;
    // $2C41: the victim's distance in front of the attacker (8-bit, with
    // the original's sum when the attacker faces right).
    auto in_front = [&](int victim, int attacker, std::uint8_t reach) {
        const int raw = s[std::size_t(attacker)].x - s[std::size_t(victim)].x;
        std::uint8_t d = std::uint8_t(raw);
        if (s[std::size_t(attacker)].facing == 1) {
            const int carry = raw >= 0 ? 1 : 0;
            d = std::uint8_t((d ^ 0xFF) + 1 + carry);
        }
        const std::uint8_t r = std::uint8_t(d - reach);
        return d >= reach && !negative(r);
    };
    auto strike_lands = [&](int victim, int attacker) { // $2B2B
        hit[std::size_t(attacker)] |= 1;
        s[std::size_t(victim)].facing = s[std::size_t(attacker)].facing ^ 1;
    };
    for (int x = 2; x >= 0; --x) {
        for (int bit = 0; bit < 5; ++bit) {
            if (!(touching[std::size_t(x)] >> bit & 1)) {
                continue;
            }
            const auto m = s[std::size_t(x)].move;
            if (m == move::bump || m == move::duck || m == move::pushed_back || m == move::thrown ||
                m == move::dying || m == 0x1B) {
                continue;
            }
            const int y = sprite_owner[std::size_t(bit)];
            if (y < 0) {
                continue;
            }
            if (cheats.harmless_enemies && x == 0 && y != 0) {
                continue; // cheat: the enemies' blows do not land on Bruce
            }
            const Actor& att = s[std::size_t(y)];
            const std::uint8_t by_bruce = y == 0 ? 0x80 : 0;
            switch (att.move) {
            case move::low_strike:
                if (att.step >= 8 || !in_front(x, y, 4)) {
                    break;
                }
                pending_sounds.push_back(sound_low_hit);
                strike_lands(x, y);
                hit[std::size_t(x)] |= 2;
                break;
            case move::chop:
                if (!in_front(x, y, 6) || std::abs(att.y - s[std::size_t(x)].y) >= 0x16) {
                    break;
                }
                pending_sounds.push_back(sound_hit);
                strike_lands(x, y);
                hit[std::size_t(x)] |= std::uint8_t(4 | by_bruce);
                break;
            case move::kick_left:
            case move::kick_right:
                if (att.step >= 0x0A || !in_front(x, y, 6)) {
                    break;
                }
                pending_sounds.push_back(sound_hit);
                strike_lands(x, y);
                hit[std::size_t(x)] |= std::uint8_t(8 | by_bruce);
                break;
            case move::fall: {
                const std::uint8_t d = std::uint8_t(s[std::size_t(x)].y - att.y);
                if (d < 0x0A || negative(std::uint8_t(d - 0x0A))) {
                    break;
                }
                if (fall_hits[std::size_t(y)] == 0) {
                    break;
                }
                --fall_hits[std::size_t(y)];
                if ((hit[std::size_t(x)] & 0x10) || (hit[std::size_t(y)] & 0x10) || m == move::kick_left ||
                    m == move::kick_right || m == move::chop) {
                    break;
                }
                pending_sounds.push_back(sound_hit);
                hit[std::size_t(x)] |= std::uint8_t(0x10 | by_bruce);
                break;
            }
            default:
                break;
            }
        }
    }
    // $2B3B: the results.
    for (int x = 2; x >= 0; --x) {
        Actor& a = s[std::size_t(x)];
        const auto h = hit[std::size_t(x)];
        if (h & 1) {
            // The attacker pauses after a strike that landed.
            a.move = move::stand;
            a.delay = 4;
            a.step = 0x18;
            ++a.posed;
            const bool late = clock >= 0x20, past = clock >= 0x19;
            bool recount = x == 0;
            if (x != 0) {
                if (late) {
                    a.step = 2;
                    recount = true;
                } else if (past) {
                    a.step = 0x0A;
                } else if (flag40) {
                    a.step = 2;
                    recount = true;
                }
            }
            if (recount) {
                a.step = late ? 0x0A : past ? 0x08 : 0x03;
            }
        }
        // Each kind of hit costs energy; out of energy the actor is thrown
        // and, if Bruce did it, scores. The original then goes on at the
        // landing-on stage: a landing hit that empties the energy repeats
        // (5 more each round, its points each time) until the byte turns
        // positive again.
        auto defeated = [&] {
            if (h & 0x80) {
                add_score(x == 1 ? points_yamo_down : points_ninja_down);
            }
            a.move = move::thrown;
            a.step = 0x0C;
        };
        bool skip_to_landing = false;
        if (h & 2) {
            a.d1 = std::uint8_t(a.d1 - 2);
            if (negative(a.d1)) {
                defeated();
                skip_to_landing = true;
            } else {
                a.move = move::pushed_back;
                a.step = 3;
            }
        }
        if (!skip_to_landing && (h & 4)) {
            if (h & 0x80) {
                add_score(points_chop);
            }
            a.d1 = std::uint8_t(a.d1 - 3);
            if (negative(a.d1)) {
                defeated();
                skip_to_landing = true;
            } else {
                a.move = move::pushed_back;
                a.step = 4;
            }
        }
        if (!skip_to_landing && (h & 8)) {
            if (h & 0x80) {
                add_score(points_kick);
            }
            a.d1 = std::uint8_t(a.d1 - 7);
            if (negative(a.d1)) {
                defeated();
            } else {
                a.move = move::thrown;
                a.step = 0x0C;
            }
        }
        while (h & 0x10) {
            if (h & 0x80) {
                add_score(points_landing);
            }
            a.d1 = std::uint8_t(a.d1 - 5);
            if (!negative(a.d1)) {
                a.move = 0x1B;
                a.step = 8;
                break;
            }
            defeated();
        }
    }
}

// $2D42: enemies out of energy leave; Bruce out of energy dies.
void World::after_actors(PassEvents& events) {
    for (int slot = 2; slot >= 0; --slot) {
        Actor& a = actors.slots[std::size_t(slot)];
        if (!a.present()) {
            continue;
        }
        if ((a.move == move::thrown || a.move == move::dying) && a.step != 0) {
            continue;
        }
        if (negative(a.d1)) {
            if (slot == 0) {
                events.bruce_died = true;
                return;
            }
            remove_actor(slot);
        }
    }
}

// Screen row of a row index ($085B tables).
static int screen_row_of(int row_index) {
    return row_index < 2 ? row_index : 2 * row_index - 1;
}

void World::print(int text, int row_index, int column, bool single) {
    const auto& chars = assets.strings[std::size_t(text)];
    const int top = screen_row_of(row_index);
    const bool lower = !single && row_index >= 2;
    for (std::size_t i = 0; i < chars.size(); ++i) {
        const int at = top * Playfield::columns + column + int(i);
        const int row = at / Playfield::columns, col = at % Playfield::columns;
        if (row >= Playfield::rows) {
            break;
        }
        field.at(col, row).code = std::uint8_t(chars[i] & 0x7F);
        if (lower && row + 1 < Playfield::rows) {
            field.at(col, row + 1).code = std::uint8_t(chars[i] | 0x80);
        }
    }
}

// The status line, row 1 ($3157, $31AF, $1AB1, $1AD6, $1AE2, $1B08): digits
// are text codes $10 + digit, the score and the top score as six decimal
// digits.
void World::draw_score() {
    std::uint32_t value = score;
    for (int i = 5; i >= 0; --i) {
        field.at(8 + i, 1).code = std::uint8_t(0x10 + value % 10);
        value /= 10;
    }
}

void World::draw_status() {
    auto digits = [&](std::uint32_t value, int column, int count) {
        for (int i = count - 1; i >= 0; --i) {
            field.at(column + i, 1).code = std::uint8_t(0x10 + value % 10);
            value /= 10;
        }
    };
    draw_score();
    digits(top_score, 20, 6);
    digits(lives, 34, 2);
    print(3, 0, 0x38, true); // "TOP"
    print(5, 0, 0x44);       // "FALLS"
}

void World::new_game() {
    score = 0;
    lives = two_players ? 9 : 4;
    next_life_at = first_extra_life;
    for (std::size_t r = 0; r < progress.size() && r < assets.rooms.size(); ++r) {
        progress[r].lanterns.assign(assets.rooms[r].lanterns.size(), true);
        progress[r].remaining = assets.lanterns_per_room[r];
        first_visit[r] = {r != 0, r != 0};
    }
    flame_timers = {1, 1, 1};
    sprite_pointers.fill(0x0F); // $07A6
    probe_charset = 0;          // $07D0, as after a lost life
    // $0F69 clears the zero page ($40, the lift, the movers) and sets
    // room 13's bonuses; $0F90 the extra falls; $0808 the lift's timers.
    flag40 = false;
    drop_bonus = {{{1, 1, 1}, {1, 1, 1}}};
    extra_falls = {5, 5};
    lift = {};
    movers = {};
    slider = {};
    off_beat = false;
    countdown = 0;
    actors = Actors{};
    sprite_owner.fill(-1);
    playing = true;
    // $0755: the play screen's fixed rows.
    for (int c = 0; c < Playfield::columns; ++c) {
        field.at(c, 0) = {0x20, 0x00};
        field.at(c, 1) = {0x00, 0x00};
        field.at(c, 2) = {0x06, 0xF8 & 15};
    }
    draw_status();
    start_blink();
    place_actor(0, 0);
}

// $09AE-$0B34 and $335B: the room's map, its lanterns, its colours.
void World::enter_room(int new_room, std::uint8_t x, std::uint8_t y, bool carry_enemies) {
    // $09E8: with rooms 12, 14 and 16 cleared, room 11 leads on to room 13.
    if (new_room == 11 && cleared(12) && cleared(14) && cleared(16)) {
        enter_room(13, assets.start_x[13], assets.start_y[13], carry_enemies);
        return;
    }
    // L2F87: enemies on screen follow Bruce, keeping their energy.
    for (int slot = 2; slot >= 1; --slot) {
        Actor& a = actors.slots[std::size_t(slot)];
        if (carry_enemies && a.present()) {
            a.carried_energy = a.d1;
            a.carried_x = x;
            a.carried_y = y;
        } else {
            a.carried_x = a.carried_y = 0;
        }
        if (a.present()) {
            remove_actor(slot);
        }
    }
    room = new_room;
    const auto& r = assets.rooms[std::size_t(room)];
    shown_charset = r.charset; // $0A8D; the probe's page follows at the next frame
    Actor& bruce = actors.slots[0];
    bruce.x = x;
    bruce.y = y;
    // $0A00: the first visit to a room (per player) scores 2000.
    if (first_visit[std::size_t(room)][std::size_t(player)]) {
        first_visit[std::size_t(room)][std::size_t(player)] = false;
        add_score(0x700 + 0xD0);
    }
    // $0A1E meant to clear the step histories but starts its count at $83,
    // which ends the loop after one byte: only the Ninja's oldest entry.
    actors.slots[2].steps[43] = 0;
    clock_low = clock = 0;
    lift.down = 0;
    lift.speed = 7;
    if (assets.quiet_rooms[std::size_t(room)]) {
        actors.slots[1].carried_x = actors.slots[2].carried_x = 0;
    }
    ninja_timer = assets.ninja_entry_waits[std::size_t(room)];
    yamo_timer = assets.yamo_entry_waits[std::size_t(room)];
    if (flag40) {
        ninja_timer = yamo_timer = 3;
    }
    if (two_players) {
        yamo_timer = 8;
    }
    bruce.terrain.feet = 1;
    bruce.step = 4;
    bruce.move = move::stand;
    bruce.delay = 0;
    crumbles = {};
    // (The charsets stay as the flames and the lift left them.)
    for (auto& f : fireballs) {
        f.shape = {}; // $422C
    }
    // $0A9F: the map from row 3, two rows per cell; colour RAM below row 2
    // is the room's colour, $F8 for cells with bit 7.
    for (int row = 0; row < data::Room::rows; ++row) {
        for (int column = 0; column < data::Room::columns; ++column) {
            const auto cell = r.at(column, row);
            const std::uint8_t colour = std::uint8_t((cell & 0x80 ? 0xF8 : r.colour) & 15);
            field.at(column, 3 + 2 * row) = {std::uint8_t(cell & 0x7F), colour};
            field.at(column, 4 + 2 * row) = {std::uint8_t(cell | 0x80), colour};
        }
    }
    // $335B: lanterns already taken show their other character.
    const auto& state = progress[std::size_t(room)];
    for (std::size_t i = 0; i < r.lanterns.size(); ++i) {
        if (!state.lanterns[i]) {
            field.at(r.lanterns[i].column, r.lanterns[i].screen_row).code = r.lanterns[i].taken_code;
            field.at(r.lanterns[i].column, r.lanterns[i].screen_row + 1).code =
                std::uint8_t(r.lanterns[i].taken_code | 0x80);
        }
    }
    // Row 0 is the black bar again ($09C2).
    for (int c = 0; c < Playfield::columns; ++c) {
        field.at(c, 0).code = 0x20;
    }
    // $0B22: the room's setup, and its script runs once.
    setup_room();
    PassEvents ignored;
    room_script(ignored);
}

// $183E: a fall counted down; below zero ($28 goes negative) the game is
// over for this player. Otherwise everyone's energy is full again.
bool World::lose_life() {
    if (!cheats.infinite_lives) {
        --lives;
    }
    if (negative(lives)) {
        return false;
    }
    for (auto& a : actors.slots) {
        a.d1 = full_energy;
    }
    return true;
}

// $189E: the play screen again ($0755) and back into the room at its start.
void World::restart_room() {
    draw_status();
    start_blink();
    // $0808 (the play screen's start, which this path goes through): the
    // lift's next step comes at once.
    lift.timer = lift.rounds = 1;
    sprite_pointers.fill(0x0F); // $07A6
    // $07D0 sets $F2 = $38 and a frame's interrupt copies it to the probe's
    // page before the room is drawn: the first pass probes charset 0.
    probe_charset = 0;
    place_bruce_at_start();
    // $18A3: back in through the room-change path (L3204 -> $2F7B), so the
    // enemies still on screen follow Bruce in at his start place, with the
    // full energy they were just given.
    enter_room(room, assets.start_x[std::size_t(room)], assets.start_y[std::size_t(room)], true);
}

// $07F7 + $1AB1: the blink timer starts so that "1UP" shows at once.
void World::start_blink() {
    blink = 0x11;
    blink_status();
}

// $1AB1, every pass: "nUP" at row 1, column 4, on for 16 passes, off for 16.
void World::blink_status() {
    blink_printed = false;
    if (--blink == 0) {
        blink = 0x20;
        print(0x00, 0, 0x2C);
        blink_printed = true;
    } else if (blink == 0x10) {
        print(player + 1, 0, 0x2C);
        blink_printed = true;
    }
}

// $1B83 (the V key): row 0 shows the version line, or the bar again.
void World::toggle_version_line() {
    const bool bar = field.at(0, 0).code == 0x20;
    for (int c = 0; c < Playfield::columns; ++c) {
        field.at(c, 0).code = bar ? assets.version_line[std::size_t(c)] : std::uint8_t(0x20);
    }
}

// $187A / $0FE4 / $188D: what changes hands between the two players.
World::PlayerState World::player_state() const {
    PlayerState p;
    p.lives = lives;
    p.room = room;
    p.progress = progress;
    p.flag40 = flag40;
    p.score = score;
    p.next_life_at = next_life_at;
    return p;
}

void World::set_player_state(const PlayerState& p) {
    lives = p.lives;
    room = p.room;
    progress = p.progress;
    flag40 = p.flag40;
    score = p.score;
    next_life_at = p.next_life_at;
}

PassEvents World::pass() {
    const auto events = run_pass();
    // $2571 / $266F (in the next frame's interrupt): sprites 0-4 point at
    // the other half of the block table.
    const std::size_t half = second_blocks ? 5 : 0;
    for (std::size_t i = 0; i < 5; ++i) {
        sprite_pointers[i] = assets.sprite_blocks[half + i];
    }
    second_blocks = !second_blocks;
    return events;
}

PassEvents World::run_pass() {
    PassEvents events;
    pending_sounds.clear();
    // $1BFA: the enemies' energy bytes are masked at the start of every
    // pass (Yamo bit 6, Ninja bits 3-6 cleared): an enemy thrown with
    // negative energy keeps few of its bits.
    actors.slots[1].d1 &= 0xBF;
    actors.slots[2].d1 &= 0x87;
    yamo_arrived = false;
    blink_status(); // $1C06
    flames();
    const bool special = assets.quiet_rooms[std::size_t(room)];
    if (!special) {
        enemy_timers();
        yamo_arrives();
        ninja_arrives();
    }
    off_beat = !off_beat; // $1C19
    room_script(events);  // $3A5C
    if (events.room_change) {
        return events;
    }
    probe_actors();
    terrain_events(events);
    if (events.room_change) {
        return events;
    }
    if (!special) {
        const std::uint8_t collisions = collision_latch; // $29BB: LDA $D01E
        collision_latch = 0;
        combat(collisions);
    }
    update_top_score(); // $3181
    // $17B6
    auto aw = actor_world();
    actors.wake_enemies(aw);
    if (!playing) {
        // $17BE: the attract mode plays a recording: per pass one byte,
        // Bruce's stick in the low nibble, fire in the high one.
        const std::uint8_t b = assets.demo_byte(demo_position++);
        actors.slots[0].stick = b & 0x0F;
        actors.slots[0].fire = b >> 4;
    }
    for (int slot = 2; slot >= 0; --slot) {
        if (!actors.slots[std::size_t(slot)].present()) {
            continue;
        }
        if (room == 19) {
            actors.slots[0].stick = clock >= 2 ? stick::none : stick::up; // the last room walks Bruce
        }
        actors.update(slot, aw);
        actors.choose_pose(slot, aw);
        if (aw.death_finished) {
            events.bruce_died = true;
            return events;
        }
    }
    events.back_to_start = aw.restart_position;
    pending_sounds.insert(pending_sounds.end(), aw.sounds.begin(), aw.sounds.end());
    // $2571: the poses shown decide the sprites' widths for the next pass.
    for (auto& a : actors.slots) {
        if (a.present()) {
            a.sprite_wide = {assets.poses[a.pose].wide,
                             a.pose + 1u < assets.poses.size() && assets.poses[a.pose + 1u].wide};
        }
    }
    after_actors(events);
    return events;
}

} // namespace bruce_lee::game
