// Verification tool for the Bruce Lee re-implementation: runs or draws the
// native game and writes what it shows, for comparison with the original
// running on the reference emulator (.analysis/bruce_lee_c64/harness).
// Reading a reference RAM dump here only sets up a situation to compare;
// the game itself never sees C64 memory.
//
//   bruce_lee_replay screen <content> <reference ram.bin> <out prefix>
//       Draws the play screen for the room, lantern states and status row
//       the dump holds; writes <out>.png and <out>.pix (the 320x200 window
//       as colour numbers, one byte each).
//
//   bruce_lee_replay probe <content> <ticks.bin from BLREF_TICKS=full>
//       For every actor in every recorded main-loop pass, computes the
//       terrain contact natively from the pass's positions, poses, screen,
//       colour RAM and charsets, and compares it with the flags the
//       original holds at the next pass.
//
//   bruce_lee_replay actor <content> <ticks.bin from BLREF_TICKS=full> [slot]
//       Per recorded pass: loads the actor's state from the record, runs the
//       native terrain probe, decision and pose for it, and compares with
//       the next record (the other actors and the passes' other routines
//       are not run, so passes where they change the actor show up too).
//
//   bruce_lee_replay run <content> <input script> <frames> <out dir>
//       Runs the native game from a new game with the input script (lines
//       "<frame> <joy> [fire]": joystick bits 1 up, 2 down, 4 left, 8
//       right, 16 fire, as blref's scripts) and writes a picture every 50
//       frames.
//
//   bruce_lee_replay lockstep <content> <ticks.bin> <pclog.txt> <input script> [first record] [tour room]
//       From one recorded pass on, runs the native game on its own state:
//       passes on the original's schedule (each in the frame after the
//       recorded pass start), the stick from the input script, collisions
//       from the native pictures, only the random numbers replayed; after
//       every pass the actors and counters are compared with the next
//       record. Stops at the first difference.
//
//   bruce_lee_replay pass <content> <ticks.bin> <pclog.txt>
//       Per recorded pass: builds the native world from the record (actors,
//       timers, score, lanterns, playfield, charsets), runs the native pass
//       with the original's random numbers and collision register value of
//       that pass (pclog.txt from BLREF_PCLOG=1bdd,29be,117e), and compares
//       the actors and counters with the next record.

#include "bruce_lee_c64/data/assets.hpp"
#include "bruce_lee_c64/game/screen.hpp"
#include "bruce_lee_c64/game/actor.hpp"
#include "bruce_lee_c64/game/terrain.hpp"
#include "bruce_lee_c64/game/front.hpp"
#include "bruce_lee_c64/game/sound.hpp"
#include "c64/sid.hpp"
#include <tuple>
#include "bruce_lee_c64/game/game.hpp"
#include "bruce_lee_c64/game/module.hpp"
#include "bruce_lee_c64/game/screen.hpp"
#include "bruce_lee_c64/game/world.hpp"
#include "bruce_lee_c64/enhanced/effects.hpp"
#include "bruce_lee_c64/enhanced/wide_view.hpp"
#include <algorithm>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include "platform/files.hpp"
#include "platform/host.hpp"
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <span>
#include <string>

namespace {
using namespace bruce_lee;

// Reference RAM locations, used only to read a dump (see game.md).
constexpr unsigned ref_room = 0x29, ref_status_row = 0x8C28, ref_lantern_table = 0x4CEF;

int screen(const std::string& content, const std::string& ram_path, const std::string& out) {
    const auto assets = data::load_assets(platform::utf8_path(content));
    const auto ram = platform::read_file(platform::utf8_path(ram_path));
    game::PlayScreen view;
    view.room = ram[ref_room];
    const unsigned list = ram[ref_lantern_table + 2 * unsigned(view.room)] |
                          ram[ref_lantern_table + 2 * unsigned(view.room) + 1] << 8;
    for (std::size_t i = 0; i < assets.rooms[std::size_t(view.room)].lanterns.size(); ++i) {
        view.lanterns_taken.push_back(ram[list + 4 * i] == 0);
    }
    for (int c = 0; c < 40; ++c) {
        view.status[std::size_t(c)] = ram[ref_status_row + unsigned(c)];
    }
    const auto picture = game::play_frame(assets, view).rasterise();
    platform::write_file(platform::utf8_path(out + ".pix"), picture.pixels);
    platform::save_png(picture.to_image(24, 35), platform::utf8_path(out + ".png"));
    std::printf("room %d\n", view.room);
    return 0;
}
// One record of blref's ticks.bin (BLREF_TICKS=full).
struct TickRecord {
    static constexpr std::size_t size = 4 + 0x200 + 0x100 + 1000 + 1000 + 0x1000 + 0x200 + 0x200;
    std::span<const std::uint8_t> bytes;
    std::uint32_t frame() const { return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | std::uint32_t(bytes[3]) << 24; }
    std::uint8_t zp(unsigned a) const { return bytes[4 + a]; }           // $0000-$01FF
    std::uint8_t page4f(unsigned a) const { return bytes[4 + 0x200 + (a - 0x4F00)]; }
    std::uint8_t screen(int i) const { return bytes[4 + 0x300 + std::size_t(i)]; }
    std::uint8_t colour(int i) const { return bytes[4 + 0x300 + 1000 + std::size_t(i)]; }
    std::uint8_t charsets(unsigned offset) const { return bytes[4 + 0x300 + 2000 + offset]; } // $A000 +
    std::uint8_t page4d(unsigned a) const { return bytes[4 + 0x300 + 2000 + 0x1000 + (a - 0x4D00)]; } // $4D00-$4EFF
    std::uint8_t page02(unsigned a) const { return bytes[4 + 0x300 + 2000 + 0x1200 + (a - 0x0200)]; } // $0200-$03FF
};

// Builds the native playfield and current charset of a record.
game::Playfield record_playfield(const TickRecord& r) {
    game::Playfield field;
    for (int row = 0; row < 25; ++row) {
        for (int c = 0; c < 40; ++c) {
            field.at(c, row) = {r.screen(row * 40 + c), std::uint8_t(r.colour(row * 40 + c) & 15)};
        }
    }
    return field;
}
data::Charset record_charset(const data::Assets& assets, const TickRecord& r, std::uint8_t d018) {
    const int set = ((d018 & 0x0E) >> 1) - 4;
    data::Charset charset = assets.charsets[std::size_t(set)];
    if (set <= 1) {
        for (int g = 0; g < 256; ++g) {
            for (int row = 0; row < 8; ++row) {
                charset[std::size_t(g)][std::size_t(row)] = r.charsets(unsigned(set * 0x800 + g * 8 + row));
            }
        }
    }
    return charset;
}

// Reference addresses of an actor's variables (verification only).
game::Actor record_actor(const TickRecord& r, int slot) {
    const unsigned s = unsigned(slot);
    game::Actor a;
    a.flags = r.zp(0x95 + s);
    a.stick = r.zp(0x98 + s);
    a.fire = r.zp(0x9B + s);
    a.x = r.zp(0x9E + s);
    a.y = r.zp(0xA1 + s);
    a.facing = r.zp(0xA4 + s);
    a.terrain = {r.zp(0xB6 + s), r.zp(0xA7 + s)};
    a.move = r.zp(0xBF + s);
    a.last_move = r.zp(0xC2 + s);
    a.repeat = r.zp(0xCB + s);
    a.step = r.zp(0xCE + s);
    a.delay = r.zp(0x106 + s);
    a.fall_steps = r.zp(0xB0 + s);
    a.climb_lock = r.zp(0xC8 + s);
    a.ride_flag = r.zp(0xAA + s);
    a.ride_wait = r.zp(0xAD + s);
    a.posed = r.page4f(0x4FE8 + s);
    for (unsigned j = 0; j < 44; ++j) {
        a.steps[j] = r.page4f(0x4F64 + s + 3 * j);
    }
    a.frame = r.page4f(0x4F55 + s);
    a.previous_frame = r.page4f(0x4F61 + s);
    a.pose = r.page4f(0x4F5B + s);
    a.sprite_wide = {r.zp(0xD4 + s) != 0, r.zp(0xD7 + s) != 0};
    a.d1 = r.zp(0xD1 + s);
    return a;
}

int actor(const std::string& content, const std::string& ticks_path, int slot) {
    const auto assets = data::load_assets(platform::utf8_path(content));
    const auto ticks = platform::read_file(platform::utf8_path(ticks_path));
    const std::size_t count = ticks.size() / TickRecord::size;
    int checked = 0, wrong = 0;
    for (std::size_t i = 0; i + 1 < count; ++i) {
        const TickRecord now{std::span(ticks).subspan(i * TickRecord::size, TickRecord::size)};
        const TickRecord next{std::span(ticks).subspan((i + 1) * TickRecord::size, TickRecord::size)};
        if (next.frame() - now.frame() > 6 || next.frame() - now.frame() < 2) {
            continue;
        }
        if (!(now.zp(0x95 + unsigned(slot)) & 1)) {
            continue;
        }
        game::Actors actors;
        for (int s = 0; s < 3; ++s) {
            actors.slots[std::size_t(s)] = record_actor(now, s);
        }
        auto& a = actors.slots[std::size_t(slot)];
        const auto field = record_playfield(now);
        const auto charset = record_charset(assets, next, now.zp(0xF2));
        a.terrain = game::probe_terrain(field, charset, assets.masks[a.pose], a.x, a.y);
        game::ActorWorld world{assets, {}, {}, {}, {}, {}, {}, {}, {}};
        world.clock = now.zp(0x88);
        world.flag40 = now.zp(0x40) != 0;
        world.lift = now.zp(0x60);
        world.lift_down = now.zp(0x65);
        actors.update(slot, world);
        actors.choose_pose(slot, world);
        const auto ref = record_actor(next, slot);
        struct Field {
            const char* name;
            int native, original;
        };
        const Field fields[] = {{"x", a.x, ref.x},         {"y", a.y, ref.y},
                                {"facing", a.facing, ref.facing}, {"move", a.move, ref.move},
                                {"last", a.last_move, ref.last_move}, {"repeat", a.repeat, ref.repeat},
                                {"step", a.step, ref.step}, {"delay", a.delay, ref.delay},
                                {"frame", a.frame, ref.frame}, {"pose", a.pose, ref.pose}};
        ++checked;
        std::string diff;
        for (const auto& f : fields) {
            if (f.native != f.original) {
                char buf[64];
                std::snprintf(buf, sizeof buf, " %s %02X/%02X", f.name, f.native, f.original);
                diff += buf;
            }
        }
        if (!diff.empty()) {
            if (++wrong <= 25) {
                std::printf("frame %u: was move %02X step %02X stick %X fire %d x %d y %d feet %X:%s\n", now.frame(),
                            now.zp(0xBF + unsigned(slot)), now.zp(0xCE + unsigned(slot)), now.zp(0x98 + unsigned(slot)),
                            now.zp(0x9B + unsigned(slot)), now.zp(0x9E + unsigned(slot)), now.zp(0xA1 + unsigned(slot)),
                            a.terrain.feet, diff.c_str());
            }
        }
    }
    std::printf("checked %d passes of slot %d, %d differ\n", checked, slot, wrong);
    return wrong ? 1 : 0;
}

// The original's random values and collision reads per pass, from a blref
// PCLOG of $1BDD (pass start), $29BE (A = $D01E) and $117E (A = value).
struct PassLog {
    std::uint64_t start_cycle{};
    std::deque<std::uint8_t> randoms;
    std::uint8_t collisions{};
    // With $16D5 and $17B6 in the log: the interrupt's stick reads between
    // the pass start and the actors ($17B6), i.e. which frame's stick the
    // pass uses (0: the pass's own frame); -1 when not logged.
    int stick_frame{-1};
    bool actors_seen{};
    // With $15C0 in the log: the interrupt's frame work runs from this pass
    // start to the next (-1 when not logged).
    int frame_works{-1};
    // With $09AE logged: the frame works before the room drawing started
    // in this window (-1: no drawing seen).
    int frame_works_before_drawing{-1};
    // With $521B logged: the frame works between the pass start and the
    // terrain probe (they change what the probe reads: its charset page).
    int frame_works_before_probe{-1};
    // With $5228 logged: per slot, the frame works before that actor's probe.
    std::array<int, 3> frame_works_before_slot_probe{-1, -1, -1};
    // With $2571 logged: the frame works before the collision read ($29BE)
    // and before the sprites take the pass's poses ($2571): pictures until
    // then show the actors as they were before the pass.
    int frame_works_before_collisions{-1};
    // With $24FB logged: the frame works before each sound request.
    std::vector<int> frame_works_before_sound;
    int frame_works_before_blocks{-1};
};
// The n-th pass start in the log is the n-th tick record (both are taken at
// the same fetch of $1BDD).
std::vector<PassLog> read_pass_log(const std::string& path) {
    std::vector<PassLog> passes;
    std::ifstream f(path);
    std::string line;
    PassLog* current = nullptr;
    std::string last_pc;
    std::uint64_t last_cycle = 0;
    bool stalled = false;
    std::string last_logged;
    std::uint64_t last_logged_cycle = 0;
    while (std::getline(f, line)) {
        std::istringstream in(line);
        std::uint64_t cycle;
        std::string pc, a_label;
        unsigned a;
        if (!(in >> cycle >> pc >> a_label >> std::hex >> a)) {
            continue;
        }
        // When the VIC stalls the CPU (a badline) at a logged fetch, the core
        // logs it on each cycle before the stall (a cycle apart) and again
        // when the CPU goes on (40-odd cycles later): all one fetch.
        bool repeated = false;
        if (pc == last_pc && cycle - last_cycle <= 4) {
            repeated = true;
            stalled = true;
        } else if (stalled && pc == last_pc && cycle - last_cycle <= 60) {
            repeated = true;
            stalled = false;
        } else {
            stalled = false;
        }
        last_pc = pc;
        last_cycle = cycle;
        if (repeated) {
            continue;
        }
        // An interrupt taken at a logged fetch ($10FA IRQ, $110D NMI, when
        // logged) makes the core log the fetch again after the RTI: the
        // fetch logged just before the interrupt entry did not run.
        // The interrupt sequence takes 7 cycles from the cut-off fetch to the
        // handler's first; a fetch that ran (the RTS at $117E: 6 cycles)
        // puts at least 13 before the handler.
        if (pc == "$10FA" || pc == "$110D") {
            const bool cut_off = cycle - last_logged_cycle <= 10;
            if (cut_off && current && last_logged == "$117E" && !current->randoms.empty()) {
                current->randoms.pop_back();
            }
            if (cut_off && current && last_logged == "$15C0" && current->frame_works > 0) {
                --current->frame_works;
            }
            if (cut_off && current && last_logged == "$16D5" && current->stick_frame > 0 && !current->actors_seen) {
                --current->stick_frame;
            }
            last_logged = pc;
            last_logged_cycle = cycle;
            continue;
        }
        last_logged = pc;
        last_logged_cycle = cycle;
        if (pc == "$1BDD") {
            passes.emplace_back();
            current = &passes.back();
            current->start_cycle = cycle;
        } else if (current && pc == "$16D5" && !current->actors_seen) {
            current->stick_frame = std::max(current->stick_frame, 0) + 1;
        } else if (current && pc == "$5228") {
            std::istringstream rest(line);
            std::string skip, x_label;
            unsigned x = 0;
            rest >> skip >> skip >> skip >> skip >> x_label >> std::hex >> x;
            if (x < 3 && current->frame_works_before_slot_probe[x] < 0) {
                current->frame_works_before_slot_probe[x] = std::max(current->frame_works, 0);
            }
        } else if (current && pc == "$521B") {
            if (current->frame_works_before_probe < 0) {
                current->frame_works_before_probe = std::max(current->frame_works, 0);
            }
        } else if (current && pc == "$09AE") {
            if (current->frame_works_before_drawing < 0) {
                current->frame_works_before_drawing = std::max(current->frame_works, 0);
            }
        } else if (current && pc == "$15C0") {
            current->frame_works = std::max(current->frame_works, 0) + 1;
        } else if (current && pc == "$17B6") {
            current->actors_seen = true;
            current->stick_frame = std::max(current->stick_frame, 0);
        } else if (current && pc == "$24FB") {
            current->frame_works_before_sound.push_back(std::max(current->frame_works, 0));
        } else if (current && pc == "$2571") {
            if (current->frame_works_before_blocks < 0) {
                current->frame_works_before_blocks = std::max(current->frame_works, 0);
            }
        } else if (current && pc == "$29BE") {
            if (current->frame_works_before_collisions < 0) {
                current->frame_works_before_collisions = std::max(current->frame_works, 0);
            }
            current->collisions = std::uint8_t(a);
        } else if (current && pc == "$117E") {
            current->randoms.push_back(std::uint8_t(a));
        }
    }
    return passes;
}

game::World record_world(const data::Assets& assets, const TickRecord& r, const TickRecord& charset_from) {
    game::World w(assets);
    w.room = r.zp(0x29);
    w.shown_charset = ((r.zp(0xF2) & 0x0E) >> 1) - 4; // $D018 copy
    w.probe_charset = (r.zp(0x7C) - 0xA0) / 8 & 3;    // the probe's page
    w.field = record_playfield(r);
    for (int set = 0; set < 2; ++set) {
        for (int g = 0; g < 256; ++g) {
            for (int row = 0; row < 8; ++row) {
                w.charsets[std::size_t(set)][std::size_t(g)][std::size_t(row)] =
                    charset_from.charsets(unsigned(set * 0x800 + g * 8 + row));
            }
        }
    }
    for (int s = 0; s < 3; ++s) {
        auto a = record_actor(r, s);
        a.carried_x = r.zp(0x8A + unsigned(s));
        a.carried_y = r.zp(0x8C + unsigned(s));
        a.carried_energy = r.zp(0x8E + unsigned(s));
        a.sprite_codes = {r.zp(0xEC + unsigned(s)), r.zp(0xEF + unsigned(s))};
        w.actors.slots[std::size_t(s)] = a;
    }
    w.playing = r.zp(0x104) != 0;
    w.flag40 = r.zp(0x40) != 0;
    w.clock_low = r.zp(0x87);
    w.clock = r.zp(0x88);
    w.ninja_timer = r.zp(0x5D);
    w.yamo_timer = r.zp(0x5E);
    w.score = std::uint32_t(r.zp(0x41)) << 16 | std::uint32_t(r.zp(0x42)) << 8 | r.zp(0x43);
    w.lives = r.zp(0x28);
    for (unsigned i = 0; i < 3; ++i) {
        w.flame_timers[i] = r.zp(0x101 + i);
        w.fall_hits[i] = r.page4f(0x4F52 + i);
    }
    for (unsigned i = 0; i < 5; ++i) {
        const auto v = r.page4f(0x4F4D + i);
        w.sprite_owner[i] = v & 0x80 ? std::int8_t(-1) : std::int8_t(v);
    }
    // The lift ($430E, its speed, is not in the records: 7 as after a room entry).
    w.lift = {r.zp(0x60), r.zp(0x64), r.zp(0x65), r.zp(0x66), 7};
    // Lantern states: the first byte of each record.
    for (std::size_t room = 0; room < assets.rooms.size(); ++room) {
        w.progress[room].remaining = r.zp(0x2A + unsigned(room));
        const auto& list = assets.rooms[room].lanterns;
        for (std::size_t i = 0; i < list.size(); ++i) {
            w.progress[room].lanterns[i] = r.page4d(list[i].address) != 0;
        }
    }
    // The room scripts' state.
    for (unsigned i = 0; i < 5; ++i) {
        w.room_counters[i] = r.page4d(0x4EA4 + i);
    }
    for (unsigned i = 0; i < 3; ++i) {
        w.movers[i] = {r.zp(0x77 + i), r.zp(0x71 + i), r.zp(0x74 + i)};
        w.crumbles[i] = {r.page4f(0x4FEB + i), r.page4f(0x4FF1 + i), r.page4f(0x4FEE + i)};
    }
    w.slider = {r.zp(0x6E), r.zp(0x70), r.zp(0x6F)};
    w.off_beat = r.zp(0x7B) != 0;
    for (unsigned i = 0; i < 9; ++i) {
        w.slides[i] = {r.zp(0x10F + i), r.zp(0x119 + i), r.zp(0x123 + i), r.zp(0x137 + i), r.zp(0x141 + i)};
    }
    for (unsigned p = 0; p < 2; ++p) {
        for (unsigned i = 0; i < 3; ++i) {
            w.drop_bonus[p][i] = r.zp(0x14B + 4 * p + i);
        }
        w.extra_falls[p] = r.page4d(0x4EA2 + p);
    }
    w.countdown = std::uint16_t(r.zp(0x49) | r.zp(0x4A) << 8);
    w.fall_hit_refresh = r.page4d(0x4EF9);
    w.frames_low = r.zp(0x7F);
    w.frames_high = std::uint16_t(r.zp(0x80) | r.zp(0x81) << 8);
    w.idle = r.zp(0x82);
    for (unsigned v = 0; v < 3; ++v) w.effect_streams[v] = std::uint16_t(r.zp(0xF6 + 2 * v) | r.zp(0xF7 + 2 * v) << 8);
    // $437F and $4375 are not in the records: the first game's mark, and a
    // top score of the current score (no new top score is noticed).
    w.next_life_at = assets.first_extra_life;
    w.top_score = w.score;
    w.random.counter = r.zp(0x16);
    w.player = r.zp(0x03);
    for (unsigned room = 0; room < 20; ++room) {
        for (unsigned p = 0; p < 2; ++p) {
            w.first_visit[room][p] = r.page02(0x300 + 2 * room + p) != 0;
        }
    }
    w.demo_position = std::uint16_t((r.zp(0x1D) | r.zp(0x1E) << 8) - 0xF300);
    return w;
}

int pass_check(const std::string& content, const std::string& ticks_path, const std::string& log_path) {
    const auto assets = data::load_assets(platform::utf8_path(content));
    const auto ticks = platform::read_file(platform::utf8_path(ticks_path));
    const std::size_t count = ticks.size() / TickRecord::size;
    auto log = read_pass_log(log_path);
    int checked = 0, wrong = 0;
    std::map<std::string, int> by_field;
    for (std::size_t i = 0; i + 1 < count; ++i) {
        const TickRecord now{std::span(ticks).subspan(i * TickRecord::size, TickRecord::size)};
        const TickRecord next{std::span(ticks).subspan((i + 1) * TickRecord::size, TickRecord::size)};
        if (next.frame() - now.frame() > 6 || next.frame() - now.frame() < 2 || now.zp(0x29) != next.zp(0x29)) {
            continue;
        }
        if (i >= log.size()) {
            continue;
        }
        auto randoms = log[i].randoms;
        auto w = record_world(assets, now, next);
        w.random.replay = [&randoms]() -> std::optional<std::uint8_t> {
            if (randoms.empty()) {
                return std::nullopt;
            }
            const auto v = randoms.front();
            randoms.pop_front();
            return v;
        };
        w.collision_latch = log[i].collisions;
        const auto events = w.pass();
        if (events.room_change || events.bruce_died || events.back_to_start) {
            continue;
        }
        ++checked;
        std::string diff;
        for (int s = 0; s < 3; ++s) {
            const auto& a = w.actors.slots[std::size_t(s)];
            const auto ref = record_actor(next, s);
            if (!a.present() && !(ref.flags & 1)) {
                continue;
            }
            const std::pair<const char*, std::pair<int, int>> fields[] = {
                {"flags", {a.flags, ref.flags}}, {"x", {a.x, ref.x}}, {"y", {a.y, ref.y}},
                {"facing", {a.facing, ref.facing}}, {"move", {a.move, ref.move}},
                {"step", {a.step, ref.step}}, {"delay", {a.delay, ref.delay}}, {"pose", {a.pose, ref.pose}},
                {"energy", {a.d1, ref.d1}}};
            for (const auto& [name, v] : fields) {
                if (v.first != v.second) {
                    char buf[64];
                    std::snprintf(buf, sizeof buf, " %d.%s %02X/%02X", s, name, v.first, v.second);
                    diff += buf;
                    ++by_field[std::to_string(s) + "." + name];
                }
            }
        }
        const std::pair<const char*, std::pair<int, int>> globals[] = {
            {"ninja_timer", {w.ninja_timer, next.zp(0x5D)}}, {"yamo_timer", {w.yamo_timer, next.zp(0x5E)}},
            {"score", {int(w.score), int(std::uint32_t(next.zp(0x41)) << 16 | std::uint32_t(next.zp(0x42)) << 8 |
                                         next.zp(0x43))}},
            {"lives", {w.lives, next.zp(0x28)}}};
        for (const auto& [name, v] : globals) {
            if (v.first != v.second) {
                char buf[64];
                std::snprintf(buf, sizeof buf, " %s %X/%X", name, v.first, v.second);
                diff += buf;
                ++by_field[name];
            }
        }
        if (!randoms.empty()) {
            diff += " (unused randoms " + std::to_string(randoms.size()) + ")";
            ++by_field["randoms_left"];
        }
        if (!diff.empty() && ++wrong <= 30) {
            std::printf("frame %u room %d:%s\n", now.frame(), now.zp(0x29), diff.c_str());
        }
    }
    std::printf("checked %d passes, %d differ\n", checked, wrong);
    for (const auto& [k, v] : by_field) {
        std::printf("  %s: %d\n", k.c_str(), v);
    }
    return wrong ? 1 : 0;
}

int run(const std::string& content, const std::string& script, int frames, const std::string& out) {
    auto module = game::start(platform::utf8_path(content));
    // BL_OPTIONS=index=value,...: the module's options (as the pause menu sets them).
    if (const char* opts = std::getenv("BL_OPTIONS")) {
        std::istringstream list(opts);
        std::string item;
        while (std::getline(list, item, ',')) {
            int index, value;
            if (std::sscanf(item.c_str(), "%d=%d", &index, &value) == 2) module->set_option(std::size_t(index), value);
        }
    }
    // BL_CANVAS=width,height: the window the picture is for (the widescreen view).
    if (const char* canvas = std::getenv("BL_CANVAS")) {
        int w, h;
        if (std::sscanf(canvas, "%d,%d", &w, &h) == 2) module->set_canvas(w, h);
    }
    std::map<int, int> changes;
    std::ifstream f(script);
    std::string line;
    while (std::getline(f, line)) {
        if (auto h = line.find('#'); h != std::string::npos) line.resize(h);
        std::istringstream in(line);
        int frame, joy;
        if (in >> frame >> joy) changes[frame] = joy;
    }
    std::filesystem::create_directories(platform::utf8_path(out));
    int joy = 0;
    for (int frame = 0; frame < frames; ++frame) {
        if (auto it = changes.find(frame); it != changes.end()) joy = it->second;
        app::GameInput in;
        in.up = joy & 1;
        in.down = joy & 2;
        in.left = joy & 4;
        in.right = joy & 8;
        in.fire = joy & 16;
        // Keys in the script's joystick number: 32 F1, 64 F3, 128 F5, 256 F7, 512 space.
        in.f1 = joy & 32;
        in.f3 = joy & 64;
        in.f5 = joy & 128;
        in.f7 = joy & 256;
        in.space = joy & 512;
        module->tick(in);
        if (frame % 50 == 0 || (std::getenv("BL_EVERY_FRAME") && frame >= std::atoi(std::getenv("BL_EVERY_FRAME")))) {
            char name[64];
            std::snprintf(name, sizeof name, "/native-%05d.png", frame);
            platform::save_png(module->picture(), platform::utf8_path(out + name));
        }
        if (!module->stopped().empty()) {
            std::printf("stopped at frame %d: %s\n", frame, module->stopped().c_str());
            break;
        }
    }
    return 0;
}

// Enhancements preview: the game with every visual upgrade (and the gore
// option) on, Bruce put into `room` at its start place once play begins,
// the enemies' hits not counting; a picture every frame from `from`.
int effects_preview(const std::string& content, const std::string& script, int frames, const std::string& out,
                    int room, int from) {
    const auto assets = data::load_assets(platform::utf8_path(content));
    game::Game g(assets);
    g.start();
    g.world.cheats.harmless_enemies = std::getenv("BL_HARMFUL") == nullptr; // BL_HARMFUL: enemy hits land
    g.world.carnage.on = std::getenv("BL_CARNAGE") != nullptr; // carnage mode
    std::uint32_t carnage_seen = 0;
    enhanced::Effects effects;
    const enhanced::Settings settings{true, true, true, true};
    std::map<int, int> changes;
    std::ifstream f(script);
    std::string line;
    while (std::getline(f, line)) {
        if (auto h = line.find('#'); h != std::string::npos) line.resize(h);
        std::istringstream in(line);
        int frame, joy;
        if (in >> frame >> joy) changes[frame] = joy;
    }
    std::filesystem::create_directories(platform::utf8_path(out));
    int joy = 0;
    bool moved = room < 0;
    int jumped = -1;
    for (int frame = 0; frame < frames; ++frame) {
        if (auto it = changes.find(frame); it != changes.end()) joy = it->second;
        app::GameInput in;
        in.up = joy & 1;
        in.down = joy & 2;
        in.left = joy & 4;
        in.right = joy & 8;
        in.fire = joy & 16;
        in.f7 = joy & 256;
        g.tick(in);
        if (!moved && g.in_play() && frame > 400) {
            // BL_AT=x,y: Bruce's place there instead of the room's start.
            int at_x = assets.start_x[std::size_t(room)], at_y = assets.start_y[std::size_t(room)];
            if (const char* at = std::getenv("BL_AT")) std::sscanf(at, "%d,%d", &at_x, &at_y);
            g.world.enter_room(room, std::uint8_t(at_x), std::uint8_t(at_y), false);
            moved = true;
            jumped = frame;
        }
        // BL_FALL_TO=room: 60 frames after the jump Bruce drops out at the
        // bottom into that room (the widescreen view's falls).
        if (const char* fall = std::getenv("BL_FALL_TO"); fall && jumped >= 0) {
            if (frame == jumped + 59) {
                g.world.actors.slots[0].y = 0xC6;
            } else if (frame == jumped + 60) {
                g.world.enter_room(std::atoi(fall), g.world.actors.slots[0].x, 0x32, false);
            }
        }
        effects.update(g, settings);
        if (std::getenv("BL_TRACE_CARNAGE")) {
            static bool bruce_flying = false;
            if (g.world.carnage.flights[0].active != bruce_flying) {
                bruce_flying = g.world.carnage.flights[0].active;
                std::printf("frame %d room %d bruce %s at %d,%d energy %d\n", frame, g.world.room,
                            bruce_flying ? "flies" : "lands", g.world.actors.slots[0].x, g.world.actors.slots[0].y,
                            g.world.actors.slots[0].d1);
            }
            static const char* kinds[] = {"launch", "impact", "smash",  "knock",    "combo",
                                          "chunk",  "landed", "wave",   "returned", "face"};
            for (const auto& e : g.world.carnage.events) {
                if (e.id > carnage_seen) {
                    std::printf("frame %d room %d %s strength %d at %.0f,%.0f\n", frame, g.world.room,
                                kinds[int(e.kind)], e.strength, e.x, e.y);
                    carnage_seen = e.id;
                }
            }
        }
        if (std::getenv("BL_TRACE_BRUCE")) {
            const auto& b = g.world.actors.slots[0];
            std::printf("frame %d room %d bruce x %d y %d move %d\n", frame, g.world.room, b.x, b.y, b.move);
        }
        if (std::getenv("BL_TRACE_DEATHS")) {
            static std::array<std::uint32_t, 3> seen{};
            for (std::size_t slot = 0; slot < 3; ++slot) {
                if (g.world.hazard_deaths[slot] != seen[slot]) {
                    std::printf("frame %d room %d hazard death slot %zu\n", frame, g.world.room, slot);
                    seen[slot] = g.world.hazard_deaths[slot];
                }
            }
        }
        for (const auto& c : g.world.crumbles) {
            if (c.timer && std::getenv("BL_TRACE_CRUMBLES")) {
                std::printf("frame %d room %d crumble timer %d row %d column %d\n", frame, g.world.room, c.timer, c.row, c.column);
            }
        }
        // BL_WIDE=width: the widescreen view of that width instead.
        static const int wide = std::getenv("BL_WIDE") ? std::atoi(std::getenv("BL_WIDE")) : 0;
        static enhanced::WideView view;
        if (frame == jumped) {
            view = {}; // (the jump is not an exit)
        }
        if (wide) {
            view.update(g, effects.picture(), wide);
        }
        if (frame >= from) {
            char name[64];
            std::snprintf(name, sizeof name, "/native-%05d.png", frame);
            platform::save_png(wide ? view.picture().to_image(0, 0) : effects.picture().to_image(32, 35),
                               platform::utf8_path(out + name));
        }
    }
    return 0;
}

std::map<int, int> read_script(const std::string& script) {
    std::map<int, int> changes;
    std::ifstream f(script);
    std::string line;
    while (std::getline(f, line)) {
        if (auto h = line.find('#'); h != std::string::npos) line.resize(h);
        std::istringstream in(line);
        int frame, joy;
        if (in >> frame >> joy) changes[frame] = joy;
    }
    return changes;
}

int joy_at(const std::map<int, int>& script, int frame) {
    auto it = script.upper_bound(frame);
    return it == script.begin() ? 0 : std::prev(it)->second;
}

int lockstep(const std::string& content, const std::string& ticks_path, const std::string& log_path,
             const std::string& script_path, std::size_t first, int tour_room) {
    auto assets = data::load_assets(platform::utf8_path(content));
    if (tour_room >= 0) {
        // The room tour (tools/tour.sh): room 0's exits poked to lead to
        // the start of another room.
        const auto r = std::size_t(tour_room);
        assets.rooms[0].exits[0] = {std::uint8_t(tour_room), assets.start_x[r], assets.start_y[r]};
        assets.rooms[0].exits[3] = assets.rooms[0].exits[0];
    }
    const auto ticks = platform::read_file(platform::utf8_path(ticks_path));
    const std::size_t count = ticks.size() / TickRecord::size;
    const auto log = read_pass_log(log_path);
    const auto script = read_script(script_path);
    auto record = [&](std::size_t i) { return TickRecord{std::span(ticks).subspan(i * TickRecord::size, TickRecord::size)}; };
    // Start at the first pass in play from `first` on.
    while (first + 1 < count && !(record(first).zp(0x95) & 1)) {
        ++first;
    }
    game::World w = record_world(assets, record(first), record(first));
    w.collision_latch = first < log.size() ? log[first].collisions : 0;
    std::deque<std::uint8_t> randoms;
    const bool trace_randoms = std::getenv("BL_TRACE_RND") != nullptr;
    w.random.replay = [&randoms, trace_randoms]() -> std::optional<std::uint8_t> {
        if (randoms.empty()) {
            if (trace_randoms) std::printf("  draw: none left\n");
            return std::nullopt;
        }
        const auto v = randoms.front();
        randoms.pop_front();
        if (trace_randoms) std::printf("  draw %02X\n", v);
        return v;
    };
    // BL_SID=writes.txt (BLREF_WRITES=D400-D418 of the same run): the native
    // effect player runs too and the SID registers are compared at every
    // pass start.
    std::vector<std::tuple<std::uint64_t, int, std::uint8_t>> sid_writes;
    if (const char* path = std::getenv("BL_SID")) {
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            unsigned long long cycle;
            char pc[16], reg[16];
            unsigned value;
            if (std::sscanf(line.c_str(), "%llu pc %15s %15s = %x", &cycle, pc, reg, &value) == 4) {
                sid_writes.emplace_back(std::uint64_t(cycle), int(std::strtoul(reg + 1, nullptr, 16) - 0xD400),
                                        std::uint8_t(value));
            }
        }
    }
    std::array<std::uint8_t, 25> original_sid{};
    std::size_t sid_next = 0;
    c64::Sid sid;
    game::EffectPlayer effects(assets);
    auto drain_sounds = [&] {
        for (auto e : w.pending_sounds) effects.request(sid, e);
        w.pending_sounds.clear();
    };
    int passes = 0;
    std::uint32_t frame = record(first).frame();
    for (std::size_t i = first; i + 1 < count && i < log.size(); ++i) {
        const auto next = record(i + 1);
        // Frames up to the frame after this pass's start: the interrupt's
        // work and the picture (collisions) each frame.
        const std::uint32_t pass_frame = record(i).frame() + 1;
        game::Actor& bruce = w.actors.slots[0];
        // The pass begins in the frame of its record (after that frame's
        // interrupt) and reads the stick in the next; that next frame's
        // interrupt work comes after the pass.
        // With the frame works logged, exactly that many native frame steps
        // follow the pass (the pass reads the per-frame state first).
        const bool counted = log[i].frame_works >= 0;
        while (frame < pass_frame) {
            ++frame;
            if (frame < pass_frame && !counted) {
                w.frame();
            }
            const int joy = joy_at(script, int(frame));
            std::uint8_t stick = 0x0F;
            if (joy & 1) stick &= 0x0E;
            if (joy & 2) stick &= 0x0D;
            if (joy & 4) stick &= 0x0B;
            if (joy & 8) stick &= 0x07;
            if (w.playing) {
                bruce.stick = stick;
                bruce.fire = joy & 16 ? 0 : 1;
            }
            if (frame < pass_frame && !counted) {
                c64::Collisions c;
                game::world_frame(w).rasterise(&c);
                w.collision_latch |= c.sprite_sprite;
            }
        }
        if (log[i].stick_frame >= 0 && w.playing) {
            // The frame whose stick the original's pass used.
            const int joy = joy_at(script, int(record(i).frame()) + log[i].stick_frame);
            std::uint8_t stick = 0x0F;
            if (joy & 1) stick &= 0x0E;
            if (joy & 2) stick &= 0x0D;
            if (joy & 4) stick &= 0x0B;
            if (joy & 8) stick &= 0x07;
            bruce.stick = stick;
            bruce.fire = joy & 16 ? 0 : 1;
        }
        // Interrupts that came between the pass start and the probe give the
        // probe the room's charset page again ($15E8).
        if (counted && log[i].frame_works_before_probe > 0) {
            w.probe_charset = w.shown_charset;
        }
        if (counted) {
            const auto slots = log[i].frame_works_before_slot_probe;
            w.before_probe = [&w, slots](int slot) {
                if (slots[std::size_t(slot)] > 0) {
                    w.probe_charset = w.shown_charset;
                }
            };
        }
        // Pictures by where the frames fall: before the collision read and
        // between it and the sprite update ($2571) they show the actors as
        // before the pass, after the update as after it.
        const bool placed = counted && log[i].frame_works_before_collisions >= 0 && log[i].frame_works_before_blocks >= 0;
        std::uint8_t before_pass = 0;
        if (placed) {
            c64::Collisions c;
            game::world_frame(w).rasterise(&c);
            before_pass = c.sprite_sprite;
            if (log[i].frame_works_before_collisions > 0) {
                w.collision_latch |= before_pass;
            }
        }
        randoms.assign(log[i].randoms.begin(), log[i].randoms.end());
        const std::uint8_t native_latch = w.collision_latch;
        if (std::getenv("BL_TRACE_LATCH")) std::printf("pass %d frame %u latch native %02X original %02X\n", passes + 1, record(i).frame(), native_latch, log[i].collisions);
        const auto events = w.pass();
        // The effect player's frames that came before each request.
        int effect_frames_done = 0;
        if (counted && !log[i].frame_works_before_sound.empty()) {
            const auto& before = log[i].frame_works_before_sound;
            for (std::size_t j = 0; j < w.pending_sounds.size(); ++j) {
                const int target = j < before.size() ? before[j] : before.back();
                while (effect_frames_done < target) {
                    effects.frame(sid);
                    ++effect_frames_done;
                }
                effects.request(sid, w.pending_sounds[j]);
            }
            w.pending_sounds.clear();
        }
        drain_sounds();
        if (events.room_change || events.bruce_died || events.back_to_start) {
            effects.silence(sid); // $09E5 / $1830
        }
        if (!counted && !(events.room_change || events.bruce_died || events.back_to_start)) {
            w.frame();
        }
        const bool redraw = events.room_change || events.bruce_died || events.back_to_start;
        auto picture = [&] {
            c64::Collisions c;
            game::world_frame(w).rasterise(&c);
            w.collision_latch |= c.sprite_sprite;
        };
        int pictures_left = int(next.frame() - record(i).frame()); // the VIC checks every frame
        if (placed) {
            if (log[i].frame_works_before_blocks > log[i].frame_works_before_collisions) {
                w.collision_latch |= before_pass;
            }
            pictures_left = log[i].frame_works - log[i].frame_works_before_blocks >= 2 ? 1 : 0;
        }
        auto frame_step = [&] {
            w.frame();
            if (effect_frames_done > 0) {
                --effect_frames_done; // ran already, before a request
            } else {
                effects.frame(sid);
            }
            if (pictures_left > 0) {
                picture();
                --pictures_left;
            }
        };
        int works_left = counted ? log[i].frame_works : 0;
        if (counted && redraw && log[i].frame_works_before_drawing > 0) {
            for (int k = 0; k < log[i].frame_works_before_drawing; ++k) {
                frame_step();
            }
            works_left -= log[i].frame_works_before_drawing;
        }
        if (events.room_change || events.bruce_died || events.back_to_start) {
            // The original draws the room with interrupts off ($2F90 SEI ..
            // $2FCF CLI) and jumps to the main loop, whose pass starts at once
            // ($48 is set at the top, waited for at the bottom): one pending
            // interrupt runs before the next pass.
            frame = std::max(frame, next.frame());
        }
        if (events.room_change) {
            w.enter_room(*events.room_change, bruce.x, bruce.y, true);
            drain_sounds();
        } else if (events.bruce_died) {
            if (!w.lose_life()) {
                std::printf("pass %d (frame %u): game over\n", passes, record(i).frame());
                return 0;
            }
            effects.silence(sid); // the player screen ($0EC7)
            w.restart_room();
            drain_sounds();
        } else if (events.back_to_start) {
            w.enter_room(w.room, assets.start_x[std::size_t(w.room)], assets.start_y[std::size_t(w.room)], false);
        }
        if (counted) {
            for (int k = 0; k < works_left; ++k) {
                frame_step();
            }
            while (pictures_left-- > 0) {
                picture();
            }
            frame = next.frame();
        } else {
            if (events.room_change || events.bruce_died || events.back_to_start) {
                w.frame(); // the interrupt pending at the CLI
            }
            c64::Collisions c;
            game::world_frame(w).rasterise(&c);
            w.collision_latch |= c.sprite_sprite;
        }
        ++passes;
        if (std::getenv("BL_TRACE")) {
            const auto& a = w.actors.slots[0];
            const auto ref = record_actor(next, 0);
            std::printf("pass %d frame %u: native x %02X y %02X feet %X pose %02X m %02X st %02X d %02X steps", passes, record(i).frame(), a.x, a.y, a.terrain.feet, a.pose,
                        a.move, a.step, a.delay);
            for (int j = 0; j < 8; ++j) std::printf(" %X", a.steps[std::size_t(j)]);
            std::printf(" | orig x %02X m %02X st %02X d %02X steps", ref.x, ref.move, ref.step, ref.delay);
            for (int j = 0; j < 8; ++j) std::printf(" %X", ref.steps[std::size_t(j)]);
            std::printf("\n");
        }
        std::string diff;
        for (int s = 0; s < 3; ++s) {
            const auto& a = w.actors.slots[std::size_t(s)];
            const auto ref = record_actor(next, s);
            if (!a.present() && !(ref.flags & 1)) continue;
            const std::pair<const char*, std::pair<int, int>> fields[] = {
                {"flags", {a.flags, ref.flags}}, {"x", {a.x, ref.x}}, {"y", {a.y, ref.y}},
                {"facing", {a.facing, ref.facing}}, {"move", {a.move, ref.move}}, {"step", {a.step, ref.step}},
                {"delay", {a.delay, ref.delay}}, {"pose", {a.pose, ref.pose}}, {"energy", {a.d1, ref.d1}}};
            for (const auto& [name, v] : fields) {
                if (v.first != v.second) {
                    char buf[64];
                    std::snprintf(buf, sizeof buf, " %d.%s %02X/%02X", s, name, v.first, v.second);
                    diff += buf;
                }
            }
            for (std::size_t j = 0; j < a.steps.size(); ++j) {
                if (a.steps[j] != ref.steps[j]) {
                    char buf[64];
                    std::snprintf(buf, sizeof buf, " %d.steps[%zu] %02X/%02X", s, j, a.steps[j], ref.steps[j]);
                    diff += buf;
                    break;
                }
            }
        }
        const std::pair<const char*, std::pair<int, int>> globals[] = {
            {"room", {w.room, next.zp(0x29)}}, {"ninja_timer", {w.ninja_timer, next.zp(0x5D)}},
            {"yamo_timer", {w.yamo_timer, next.zp(0x5E)}},
            {"score", {int(w.score), int(std::uint32_t(next.zp(0x41)) << 16 | std::uint32_t(next.zp(0x42)) << 8 | next.zp(0x43))}},
            {"lives", {w.lives, next.zp(0x28)}}};
        for (const auto& [name, v] : globals) {
            if (v.first != v.second) {
                char buf[64];
                std::snprintf(buf, sizeof buf, " %s %X/%X", name, v.first, v.second);
                diff += buf;
            }
        }
        // The room machinery and the screen.
        const std::pair<const char*, std::pair<int, int>> machinery[] = {
            {"lift.timer", {w.lift.timer, next.zp(0x60)}}, {"lift.phase", {w.lift.phase, next.zp(0x64)}},
            {"lift.down", {w.lift.down, next.zp(0x65)}}, {"lift.rounds", {w.lift.rounds, next.zp(0x66)}},
            {"off_beat", {w.off_beat, next.zp(0x7B) != 0}},
            {"countdown", {w.countdown, next.zp(0x49) | next.zp(0x4A) << 8}},
            // (per-frame state: comparable only with the frame work logged)
            {"fall_hits1", {counted ? w.fall_hits[1] : next.page4f(0x4F53), next.page4f(0x4F53)}},
            {"flame0", {w.flame_timers[0], next.zp(0x101)}}, {"flame1", {w.flame_timers[1], next.zp(0x102)}},
            {"flame2", {w.flame_timers[2], next.zp(0x103)}},
            {"counter0", {w.room_counters[0], next.page4d(0x4EA4)}}, {"counter1", {w.room_counters[1], next.page4d(0x4EA5)}},
            {"counter2", {w.room_counters[2], next.page4d(0x4EA6)}}, {"counter3", {w.room_counters[3], next.page4d(0x4EA7)}},
            {"counter4", {w.room_counters[4], next.page4d(0x4EA8)}},
            {"crumble0", {w.crumbles[0].timer, next.page4f(0x4FEB)}}, {"crumble1", {w.crumbles[1].timer, next.page4f(0x4FEC)}},
            {"crumble2", {w.crumbles[2].timer, next.page4f(0x4FED)}}};
        for (const auto& [name, v] : machinery) {
            if (v.first != v.second) {
                char buf[64];
                std::snprintf(buf, sizeof buf, " %s %X/%X", name, v.first, v.second);
                diff += buf;
            }
        }
        // The screen and the charsets: the first difference of each.
        for (int cell = 3 * 40; cell < 1000; ++cell) {
            const auto& c = w.field.at(cell % 40, cell / 40);
            if (c.code != next.screen(cell) || c.colour != (next.colour(cell) & 15)) {
                char buf[96];
                std::snprintf(buf, sizeof buf, " cell %d,%d %02X:%X/%02X:%X", cell % 40, cell / 40, c.code, c.colour,
                              next.screen(cell), next.colour(cell) & 15);
                diff += buf;
                break;
            }
        }
        bool charset_differs = false;
        for (unsigned set = 0; set < 2 && !charset_differs; ++set) {
            for (unsigned g = 0; g < 256 && !charset_differs; ++g) {
                for (unsigned row = 0; row < 8; ++row) {
                    if (w.charsets[set][g][row] != next.charsets(set * 0x800 + g * 8 + row)) {
                        char buf[64];
                        std::snprintf(buf, sizeof buf, " charset %u glyph %02X row %u", set, g, row);
                        diff += buf;
                        charset_differs = true;
                        break;
                    }
                }
            }
        }
        if (!sid_writes.empty() && i + 1 < log.size()) {
            while (sid_next < sid_writes.size() && std::get<0>(sid_writes[sid_next]) < log[i + 1].start_cycle) {
                const auto [c, reg, value] = sid_writes[sid_next++];
                if (reg >= 0 && reg < 25) original_sid[std::size_t(reg)] = value;
            }
            for (int reg = 0; reg < 25; ++reg) {
                if (sid.read_register(reg) != original_sid[std::size_t(reg)]) {
                    char buf[64];
                    std::snprintf(buf, sizeof buf, " sid $D4%02X %02X/%02X", reg, sid.read_register(reg),
                                  original_sid[std::size_t(reg)]);
                    diff += buf;
                    break;
                }
            }
        }
        if (!randoms.empty()) diff += " (unused randoms)";
        if (!diff.empty()) {
            std::printf("pass %d (frame %u, room %d, collisions %02X, native %02X): differs:%s\n", passes,
                        record(i).frame(), w.room, log[i].collisions, native_latch, diff.c_str());
            return 1;
        }
    }
    std::printf("%d passes in lockstep, no difference\n", passes);
    return 0;
}

int probe(const std::string& content, const std::string& ticks_path) {
    const auto assets = data::load_assets(platform::utf8_path(content));
    const auto ticks = platform::read_file(platform::utf8_path(ticks_path));
    const std::size_t count = ticks.size() / TickRecord::size;
    int checked = 0, wrong = 0;
    for (std::size_t i = 0; i + 1 < count; ++i) {
        const TickRecord now{std::span(ticks).subspan(i * TickRecord::size, TickRecord::size)};
        const TickRecord next{std::span(ticks).subspan((i + 1) * TickRecord::size, TickRecord::size)};
        if (now.zp(0x104) == 0 && now.zp(0x29) == 0 && now.screen(40) == 0) {
            continue; // not playing
        }
        if (next.frame() - now.frame() > 6 || next.frame() - now.frame() < 2) {
            continue; // a pause in between (a life lost, a room change): no pair
        }
        game::Playfield field;
        for (int r = 0; r < 25; ++r) {
            for (int c = 0; c < 40; ++c) {
                field.at(c, r) = {now.screen(r * 40 + c), std::uint8_t(now.colour(r * 40 + c) & 15)};
            }
        }
        const int set = ((now.zp(0xF2) & 0x0E) >> 1) - 4;
        data::Charset charset = assets.charsets[std::size_t(set)];
        // The flames ($40A9) change the charset earlier in the pass than the
        // probe runs, so the probe sees the charset of the next record.
        if (set <= 1) {
            for (int g = 0; g < 256; ++g) {
                for (int r = 0; r < 8; ++r) {
                    charset[std::size_t(g)][std::size_t(r)] = next.charsets(unsigned(set * 0x800 + g * 8 + r));
                }
            }
        }
        for (int a = 0; a < 3; ++a) {
            if (!(now.zp(0x95 + unsigned(a)) & 1)) {
                continue;
            }
            const int pose = now.page4f(0x4F5B + unsigned(a));
            const auto contact = game::probe_terrain(field, charset, assets.masks[std::size_t(pose)],
                                                     now.zp(0x9E + unsigned(a)), now.zp(0xA1 + unsigned(a)));
            const int ref_feet = next.zp(0xB6 + unsigned(a)), ref_body = next.zp(0xA7 + unsigned(a));
            ++checked;
            if (contact.feet != ref_feet || contact.body != ref_body) {
                if (++wrong <= 20) {
                    std::printf("frame %u actor %d x %d y %d pose %d: native %X/%X, original %X/%X\n", now.frame(), a,
                                now.zp(0x9E + unsigned(a)), now.zp(0xA1 + unsigned(a)), pose, contact.feet, contact.body,
                                ref_feet, ref_body);
                }
            }
        }
    }
    std::printf("checked %d actor passes, %d differ\n", checked, wrong);
    return wrong ? 1 : 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string cmd = argc > 1 ? argv[1] : "";
        if (cmd == "zpview" && argc == 4) {
            // The random source's view of pages 0 and 1 against the records.
            const auto assets = data::load_assets(platform::utf8_path(argv[2]));
            const auto ticks = platform::read_file(platform::utf8_path(argv[3]));
            const std::size_t count = ticks.size() / TickRecord::size;
            std::map<int, int> wrong;
            long total = 0, same = 0;
            for (std::size_t i = 0; i < count; i += 3) {
                const TickRecord r{std::span(ticks).subspan(i * TickRecord::size, TickRecord::size)};
                if (!(r.zp(0x95) & 1)) continue;
                const auto w = record_world(assets, r, r);
                for (int a = 0x40; a < 0x140; ++a) {
                    ++total;
                    if (w.memory_byte(a) == r.zp(unsigned(a))) ++same;
                    else ++wrong[a];
                }
            }
            std::printf("%ld of %ld bytes as recorded (%.1f%%)\n", same, total, 100.0 * same / double(total));
            std::vector<std::pair<int, int>> worst(wrong.begin(), wrong.end());
            std::sort(worst.begin(), worst.end(), [](auto& x, auto& y) { return x.second > y.second; });
            for (std::size_t k = 0; k < worst.size() && k < 25; ++k) std::printf(" $%03X:%d", worst[k].first, worst[k].second);
            std::printf("\n");
            return 0;
        }
        if (cmd == "music" && argc == 5) {
            // The title music against the original's SID writes: a pclog with
            // $06DA (the start) and $E49E (each frame's player call).
            const auto assets = data::load_assets(platform::utf8_path(argv[2]));
            std::vector<std::tuple<std::uint64_t, int, std::uint8_t>> writes;
            {
                std::ifstream f(argv[3]);
                std::string line;
                while (std::getline(f, line)) {
                    unsigned long long cycle;
                    char pc[16], reg[16];
                    unsigned value;
                    if (std::sscanf(line.c_str(), "%llu pc %15s %15s = %x", &cycle, pc, reg, &value) == 4) {
                        writes.emplace_back(std::uint64_t(cycle), int(std::strtoul(reg + 1, nullptr, 16) - 0xD400), std::uint8_t(value));
                    }
                }
            }
            std::vector<std::uint64_t> frames;
            std::uint64_t start = 0;
            {
                std::ifstream f(argv[4]);
                std::string line;
                while (std::getline(f, line)) {
                    std::istringstream in(line);
                    unsigned long long cycle;
                    std::string pc;
                    if (!(in >> cycle >> pc)) continue;
                    if (pc == "$06DA" && start == 0) start = cycle;
                    if (pc == "$E49E" && start != 0) frames.push_back(cycle);
                }
            }
            c64::Sid sid;
            game::MusicPlayer music(assets);
            std::array<std::uint8_t, 25> original{};
            std::size_t next = 0;
            auto apply_until = [&](std::uint64_t cycle) {
                while (next < writes.size() && std::get<0>(writes[next]) < cycle) {
                    const auto [c, reg, v] = writes[next++];
                    if (reg >= 0 && reg < 25) original[std::size_t(reg)] = v;
                }
            };
            apply_until(start);
            music.start_list(sid, 3, {0x00, 0xFF});
            int wrong = 0;
            for (std::size_t k = 0; k <= frames.size(); ++k) {
                if (k > 0) music.frame(sid);
                apply_until(k < frames.size() ? frames[k] : ~0ull);
                if (std::getenv("BL_MUSIC_FROM") && int(k) >= std::atoi(std::getenv("BL_MUSIC_FROM")) && int(k) < std::atoi(std::getenv("BL_MUSIC_FROM")) + 12) {
                    std::printf("frame %zu native %02X %02X %02X  original %02X %02X %02X\n", k, sid.read_register(0), sid.read_register(1), sid.read_register(4), original[0], original[1], original[4]);
                }
                for (int reg = 0; reg < 25; ++reg) {
                    if (sid.read_register(reg) != original[std::size_t(reg)]) {
                        if (++wrong <= 10) {
                            std::printf("frame %zu: $D4%02X %02X/%02X\n", k, reg, sid.read_register(reg), original[std::size_t(reg)]);
                        }
                        break;
                    }
                }
            }
            std::printf("%zu frames, %d differ\n", frames.size(), wrong);
            return wrong ? 1 : 0;
        }
        if (cmd == "front" && argc == 4) {
            // The front-end screens as pictures (game over: player 1, score 0).
            const auto assets = data::load_assets(platform::utf8_path(argv[2]));
            const std::string out = argv[3];
            platform::save_png(game::game_over_frame(assets, 0, 0).rasterise().to_image(32, 35),
                               platform::utf8_path(out + "/game_over.png"));
            platform::save_png(game::player_frame(assets, 1).rasterise().to_image(32, 35),
                               platform::utf8_path(out + "/player2.png"));
            return 0;
        }
        if (cmd == "masks" && argc == 3) {
            // The native collision masks, one line of hex per pose.
            const auto assets = data::load_assets(platform::utf8_path(argv[2]));
            for (std::size_t p = 0; p < assets.masks.size(); ++p) {
                std::printf("%zu", p);
                for (auto b : assets.masks[p]) std::printf(" %02x", b);
                std::printf("\n");
            }
            return 0;
        }
        if (cmd == "lockstep" && argc >= 6 && argc <= 8) {
            return lockstep(argv[2], argv[3], argv[4], argv[5], argc >= 7 ? std::size_t(std::atoi(argv[6])) : 0,
                            argc == 8 ? std::atoi(argv[7]) : -1);
        }
        if (cmd == "run" && argc == 6) {
            return run(argv[2], argv[3], std::atoi(argv[4]), argv[5]);
        }
        if (cmd == "effects" && argc == 8) {
            return effects_preview(argv[2], argv[3], std::atoi(argv[4]), argv[5], std::atoi(argv[6]), std::atoi(argv[7]));
        }
        if (cmd == "pass" && argc == 5) {
            return pass_check(argv[2], argv[3], argv[4]);
        }
        if (cmd == "actor" && (argc == 4 || argc == 5)) {
            return actor(argv[2], argv[3], argc == 5 ? std::atoi(argv[4]) : 0);
        }
        if (cmd == "probe" && argc == 4) {
            return probe(argv[2], argv[3]);
        }
        if (cmd == "screen" && argc == 5) {
            return screen(argv[2], argv[3], argv[4]);
        }
        std::fprintf(stderr, "usage: bruce_lee_replay screen <content> <ram.bin> <out prefix>\n");
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bruce_lee_replay: %s\n", e.what());
        return 1;
    }
}
