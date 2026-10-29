// The bytes the original's random numbers come from: its zero page and page
// 1, where it keeps its variables (see Random in world.hpp). The draws read
// mostly $40-$13F (the callers hold Y = $40), so the game's state gives most
// of them; temporaries the code leaves behind are taken as 0 (an
// approximation: the original's numbers follow its exact code path).
#include "bruce_lee_c64/game/world.hpp"

namespace bruce_lee::game {
namespace {

// Page 1 outside the game's variables still holds what the loader pushed:
// its return address $0342 again and again ($42 at even addresses, $03 at
// odd ones; seen in every capture).
std::uint8_t loader_leftover(int address) {
    return address & 1 ? 0x03 : 0x42;
}

} // namespace

std::uint8_t World::memory_byte(int address) const {
    address &= 0x1FF;
    const auto& s = actors.slots;
    // Per-actor tables: three bytes from `base`, by slot.
    auto actor_byte = [&](int base, auto field) -> std::optional<std::uint8_t> {
        const int slot = address - base;
        if (slot >= 0 && slot < 3) {
            return std::uint8_t(field(s[std::size_t(slot)]));
        }
        return std::nullopt;
    };
    if (address >= 0x95 && address < 0xF2) {
        const auto& b = address;
        if (auto v = actor_byte(0x95, [](const Actor& a) { return a.flags; })) return *v;
        if (auto v = actor_byte(0x98, [](const Actor& a) { return a.stick; })) return *v;
        if (auto v = actor_byte(0x9B, [](const Actor& a) { return a.fire; })) return *v;
        if (auto v = actor_byte(0x9E, [](const Actor& a) { return a.x; })) return *v;
        if (auto v = actor_byte(0xA1, [](const Actor& a) { return a.y; })) return *v;
        if (auto v = actor_byte(0xA4, [](const Actor& a) { return a.facing; })) return *v;
        if (auto v = actor_byte(0xA7, [](const Actor& a) { return a.terrain.body; })) return *v;
        if (auto v = actor_byte(0xAA, [](const Actor& a) { return a.ride_flag; })) return *v;
        if (auto v = actor_byte(0xAD, [](const Actor& a) { return a.ride_wait; })) return *v;
        if (auto v = actor_byte(0xB0, [](const Actor& a) { return a.fall_steps; })) return *v;
        if (auto v = actor_byte(0xB6, [](const Actor& a) { return a.terrain.feet; })) return *v;
        if (auto v = actor_byte(0xB9, [](const Actor& a) { return a.x; })) return *v; // the sprites' copy
        if (auto v = actor_byte(0xBF, [](const Actor& a) { return a.move; })) return *v;
        if (auto v = actor_byte(0xC2, [](const Actor& a) { return a.last_move; })) return *v;
        if (auto v = actor_byte(0xC8, [](const Actor& a) { return a.climb_lock; })) return *v;
        if (auto v = actor_byte(0xCB, [](const Actor& a) { return a.repeat; })) return *v;
        if (auto v = actor_byte(0xCE, [](const Actor& a) { return a.step; })) return *v;
        if (auto v = actor_byte(0xD1, [](const Actor& a) { return a.d1; })) return *v;
        if (auto v = actor_byte(0xD4, [](const Actor& a) { return a.sprite_wide[0] ? 1 : 0; })) return *v;
        if (auto v = actor_byte(0xD7, [](const Actor& a) { return a.sprite_wide[1] ? 1 : 0; })) return *v;
        if (auto v = actor_byte(0xE6, [](const Actor& a) { return (a.x - 12) >> 2; })) return *v;
        // $E9: the second row the terrain test looked at ($2E50 counts it on).
        if (auto v = actor_byte(0xE9, [](const Actor& a) { return (((a.y - 12) & 0xFF) >> 4) + 1; })) return *v;
        // $C5: $FF once an actor has been through $1D81; $DA/$DD: its two
        // sprites' colours ($2571); $E0: (hypothesis) the sprite's row count
        // as the records show it.
        // (A removed actor's bytes stay as they were.)
        if (auto v = actor_byte(0xC5, [](const Actor& a) { return a.flags != 0 ? 0xFF : 0x00; })) return *v;
        auto colour = [this](const Actor& a, int layer) -> int {
            const std::size_t pose = std::size_t(a.pose + layer);
            if (a.flags == 0 || pose >= assets.poses.size() || (layer && a.sprite_codes.second == 0)) {
                return 0;
            }
            return assets.poses[pose].colour;
        };
        if (auto v = actor_byte(0xDA, [&](const Actor& a) { return colour(a, 0); })) return *v;
        if (auto v = actor_byte(0xDD, [&](const Actor& a) { return colour(a, 1); })) return *v;
        if (auto v = actor_byte(0xE0, [](const Actor& a) { return a.flags == 0 ? 0 : a.kind() == 2 ? 0x11 : 0x14; })) return *v;
        if (auto v = actor_byte(0xEC, [](const Actor& a) { return a.sprite_codes.first; })) return *v;
        if (auto v = actor_byte(0xEF, [](const Actor& a) { return a.sprite_codes.second; })) return *v;
        (void)b;
        return 0;
    }
    if (address >= 0x2A && address < 0x2A + 20) {
        return progress[std::size_t(address - 0x2A)].remaining;
    }
    if (address >= 0x6E && address <= 0x79) {
        switch (address) {
        case 0x6E: return slider.at;
        case 0x6F: return slider.row;
        case 0x70: return slider.was;
        default: {
            const int i = (address - 0x71) % 3;
            const auto& m = movers[std::size_t(i)];
            return address < 0x74 ? m.column : address < 0x77 ? m.row : m.frame;
        }
        }
    }
    if (address >= 0x101 && address <= 0x103) {
        return flame_timers[std::size_t(address - 0x101)];
    }
    if (address >= 0x106 && address <= 0x108) {
        return s[std::size_t(address - 0x106)].delay;
    }
    if (address >= 0x10F && address < 0x14B) {
        if (!slides_set) {
            return loader_leftover(address);
        }
        const int i = (address - 0x10F) % 10, table = (address - 0x10F) / 10;
        if (i > 8) {
            return loader_leftover(address);
        }
        const auto& sl = slides[std::size_t(i)];
        switch (table) {
        case 0: return sl.mode;
        case 1: return sl.at;
        case 2: return sl.row;
        case 3: return sl.at; // $012D: the place before the step
        case 4: return sl.low;
        default: return sl.high;
        }
    }
    if (address >= 0x14B && address < 0x153) {
        const int p = (address - 0x14B) / 4, i = (address - 0x14B) % 4;
        return i < 3 ? drop_bonus[std::size_t(p)][std::size_t(i)] : 1;
    }
    if (address >= 0x153 && address < 0x167) {
        return assets.rooms[std::size_t(room)].header[std::size_t(address - 0x153)];
    }
    switch (address) {
    case 0x02: return 0x32;
    case 0x03: return std::uint8_t(player);
    case 0x16: return random.counter;
    case 0x1B: return std::uint8_t((s[0].x - 12) & 3);   // the probe's offsets, Bruce probed last
    case 0x1C: return std::uint8_t((s[0].y - 0x1D) & 7);
    case 0x1D: return std::uint8_t(demo_position);
    case 0x1E: return std::uint8_t(0xF3 + (demo_position >> 8));
    case 0x28: return lives;
    case 0x29: return std::uint8_t(room);
    case 0x40: return flag40 ? 1 : 0;
    case 0x41: return std::uint8_t(score >> 16);
    case 0x42: return std::uint8_t(score >> 8);
    case 0x43: return std::uint8_t(score);
    case 0x47: return 0x03;                            // $09BB: Bruce's sprites
    case 0x48: return 3;                               // $1BDF: the pass's frame wait, at its start
    case 0x4B: return 0xFF;                            // $0761: no key
    case 0x4C: case 0x4E: return 0xFF;                 // loop temporaries, as they end
    case 0x5C: return 1;
    case 0x84: return 0xFE;                            // $165D
    case 0x86: return 8;                               // the undo budget ($86) left untouched
    case 0xF3: return 0x27;
    case 0xF4: return 0x09;
    case 0xF6: case 0xF8: case 0xFA:
        return std::uint8_t(effect_streams[std::size_t((address - 0xF6) / 2)]);
    case 0xF7: case 0xF9: case 0xFB:
        return std::uint8_t(effect_streams[std::size_t((address - 0xF7) / 2)] >> 8);
    case 0xFC: case 0xFE: return 0xA4;
    case 0xFD: case 0xFF: return 0x00;
    case 0x49: return std::uint8_t(countdown);
    case 0x4A: return std::uint8_t(countdown >> 8);
    case 0x5D: return ninja_timer;
    case 0x5E: return yamo_timer;
    case 0x60: return lift.timer;
    case 0x64: return lift.phase;
    case 0x65: return lift.down;
    case 0x66: return lift.rounds;
    case 0x7A: return assets.quiet_rooms[std::size_t(room)] ? 0xFF : 0x00;
    case 0x7B: return off_beat ? 1 : 0;
    case 0x7C: return std::uint8_t(0xA0 + 8 * probe_charset);
    case 0x7F: return frames_low;
    case 0x80: return std::uint8_t(frames_high);
    case 0x81: return std::uint8_t(frames_high >> 8);
    case 0x82: return idle;
    case 0x87: return clock_low;
    case 0x88: return clock;
    case 0x8B: return s[1].carried_x;
    case 0x8C: return s[2].carried_x;
    case 0x8D: return s[1].carried_y;
    case 0x8E: return s[2].carried_y;
    case 0x8F: return s[1].carried_energy;
    case 0x90: return s[2].carried_energy;
    case 0xF2: return std::uint8_t(0x38 + 2 * shown_charset);
    case 0x100: return 0;
    case 0x104: return playing ? 1 : 0;
    case 0x105: return playing ? 0 : 1;               // $0F87 sets 1; play's $1A07 keeps bit 7 only
    case 0x109: case 0x10A: return 0xFF; // the keyboard scan's last codes
    case 0x10B: case 0x10C: return 0;
    case 0x10D: return playing ? 0 : 3;
    case 0x10E: return 1;
    default: break;
    }
    if (address >= 0x167 && address < 0x1E0) {
        return loader_leftover(address);
    }
    return 0; // temporaries, the stack
}

} // namespace bruce_lee::game
