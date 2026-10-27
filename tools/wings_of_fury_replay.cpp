// Wings of Fury research tool: checks the rebuilt game code against
// recordings of the original (.analysis/wings_of_fury_amiga/harness).
//
//   wings_of_fury_replay ffp <op> <oracle file>
//       op: add mul div flt fix; the file has lines "a b result" (hex) from
//       harness/ffpref.py. Prints the rows that differ (exponents $30-$50).
//   wings_of_fury_replay flight <content> <map> <recording> [resync]
//       runs the rebuilt flight (game/flight) on the recording's command words
//       from its first tick (harness/flightrec.py) and compares the player
//       after every tick; the world (ships, elevator) follows the recording.
//       With "resync" the player is also reloaded from the recording before
//       each tick (lists every tick that differs, not only the first).
//   wings_of_fury_replay simulate <content> <map> <recording> <script> [every]
//       runs the rebuilt flight from the recording's first tick on a command
//       script ("count:cmd,..." as flightrec.py takes it), the world frozen as
//       recorded at that tick; prints the player every `every` ticks.
//   wings_of_fury_replay play <content> <script> <out dir> <tick,tick,...>
//       drives the game module (game/module) with the script as joystick
//       input (stick bits per tick, four vertical blanks each) and saves the
//       picture at the listed ticks as PNG.

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "wings_of_fury_amiga/data/assets.hpp"
#include "wings_of_fury_amiga/game/ffp.hpp"
#include "wings_of_fury_amiga/game/flight.hpp"
#include "wings_of_fury_amiga/game/module.hpp"
#include "wings_of_fury_amiga/game/scenery.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <set>
#include <vector>
#include <cstdio>
#include <exception>
#include <sstream>
#include <string>

namespace {

namespace ffp = wings::game::ffp;

bool in_range(std::uint32_t f) {
    return f == 0 || ((f & 0x7F) >= 0x30 && (f & 0x7F) <= 0x50);
}

int check_ffp(const std::string& op, const std::string& file) {
    const auto bytes = platform::read_file(platform::utf8_path(file));
    std::istringstream lines(std::string(bytes.begin(), bytes.end()));
    int rows = 0, checked = 0, wrong = 0;
    for (std::string line; std::getline(lines, line);) {
        std::uint32_t a = 0, b = 0, want = 0;
        if (std::sscanf(line.c_str(), "%x %x %x", &a, &b, &want) != 3) continue;
        ++rows;
        std::uint32_t got = 0;
        if (op == "flt") {
            got = ffp::from_int(std::int32_t(a));
        } else {
            if (!in_range(a) || (op != "fix" && !in_range(b))) continue;
            if (op == "add") got = ffp::add(a, b);
            else if (op == "mul") got = ffp::multiply(a, b);
            else if (op == "div") got = ffp::divide(a, b);
            else if (op == "fix") got = std::uint32_t(ffp::to_int(a));
            else throw std::runtime_error("unknown op " + op);
        }
        ++checked;
        if (got != want) {
            if (++wrong <= 10) std::printf("%08x %08x: got %08x, want %08x\n", a, b, got, want);
        }
    }
    std::printf("%s: %d rows, %d checked, %d differ\n", op.c_str(), rows, checked, wrong);
    return wrong == 0 ? 0 : 1;
}

// A flightrec.py recording: per tick the command word and the snapshot
// ranges (our addresses). Research only: the rebuilt code never sees these.
// WOFREC2: per tick the command word and the ranges below. WOFREC3 adds the
// C library's random seed (in $116960..$11696F) and the splash list (84
// bytes, which the game allocates: given the made-up address $200000).
constexpr std::uint32_t splash_list = 0x200000;
constexpr std::uint32_t random_seed = 0x11696A;

class Recording {
public:
    using Range = std::pair<std::uint32_t, std::uint32_t>;
    static constexpr std::array<Range, 6> all_ranges = {{{0x114680, 0x115F00},
                                                         {0x116C80, 0x116CE0},
                                                         {0x117080, 0x1172C0},
                                                         {0x117D30, 0x117EC0},
                                                         {0x116960, 0x116970},
                                                         {splash_list, splash_list + 21 * 4}}};

    explicit Recording(const std::string& file) : bytes_(platform::read_file(platform::utf8_path(file))) {
        const std::string magic = bytes_.size() < 8 ? "" : std::string(bytes_.begin(), bytes_.begin() + 8);
        if (magic == "WOFREC3\n") {
            range_count_ = 6;
        } else if (magic != "WOFREC2\n") {
            throw std::runtime_error("not a WOFREC2/3 recording");
        }
        for (std::size_t i = 0; i < range_count_; ++i) size_ += all_ranges[i].second - all_ranges[i].first;
    }
    bool has_effects() const { return range_count_ == 6; }
    std::uint32_t u32(std::size_t tick, std::uint32_t address) const {
        return std::uint32_t(s16(tick, address) & 0xFFFF) << 16 | std::uint32_t(s16(tick, address + 2) & 0xFFFF);
    }
    std::size_t ticks() const { return (bytes_.size() - 8) / (2 + size_); }
    int command(std::size_t tick) const { return bytes_[base(tick)] << 8 | bytes_[base(tick) + 1]; }
    int s16(std::size_t tick, std::uint32_t address) const {
        const auto at = offset(tick, address);
        return std::int16_t(bytes_[at] << 8 | bytes_[at + 1]);
    }
    int u8(std::size_t tick, std::uint32_t address) const { return bytes_[offset(tick, address)]; }
    std::string name(std::size_t tick, std::uint32_t address) const {
        std::string s;
        for (std::uint32_t i = 0; i < 4; ++i) s += char(u8(tick, address + i));
        return s;
    }

private:
    std::size_t base(std::size_t tick) const { return 8 + tick * (2 + size_); }
    std::size_t offset(std::size_t tick, std::uint32_t address) const {
        std::size_t at = base(tick) + 2;
        for (std::size_t i = 0; i < range_count_; ++i) {
            const auto [a, b] = all_ranges[i];
            if (address >= a && address < b) return at + (address - a);
            at += b - a;
        }
        throw std::runtime_error("address outside the recording");
    }
    std::vector<std::uint8_t> bytes_;
    std::size_t range_count_ = 4;
    std::size_t size_{};
};

using wings::game::FlightWorld;
using wings::game::PlayerFlight;

int r16(const Recording& r, std::size_t k, std::uint32_t address) {
    return r.s16(k, address);
}

void load_player(const Recording& r, std::size_t k, PlayerFlight& p) {
    p.altitude = r.s16(k, 0x114FCC);
    p.x = r.s16(k, 0x114FCE);
    p.shape = r.name(k, 0x114FD4);
    p.mode = r.s16(k, 0x114FD8);
    p.fuel = r.s16(k, 0x114FDA);
    p.spare = r.s16(k, 0x114FDC);
    p.oil = r.s16(k, 0x114FDE);
    p.facing = r.s16(k, 0x114FE0);
    p.speed_x = r.s16(k, 0x114FE2);
    p.speed_y = r.s16(k, 0x114FE4);
    p.climb = r.s16(k, 0x115356);
    p.speed = r.s16(k, 0x115368);
    p.throttle = r.s16(k, 0x117D3E);
    p.engine_loud = r.s16(k, 0x117D3C) != 0;
    p.turn = r.s16(k, 0x115362);
    p.smoothed_climb = r.s16(k, 0x1159F6);
    p.climb_kick = r.s16(k, 0x11535C);
    p.pulling_up = r.s16(k, 0x1159FE) != 0;
    p.lift = r.s16(k, 0x1159F0) != 0;
    p.gear = r.s16(k, 0x11535E);
    p.signal = r.u8(k, 0x1152B3);
    p.fresh_on_deck = r.u8(k, 0x1152BB) != 0;
    p.wire_x = r.s16(k, 0x115360);
    p.attitude = r.s16(k, 0x1154E6);
    p.wheels = r.name(k, 0x115376);
    if (p.wheels == std::string(4, '\0')) p.wheels.clear();
    p.wheel_attitude = r.s16(k, 0x11537A);
    p.deck_low = r.s16(k, 0x115350);
    p.deck_high = r.s16(k, 0x115352);
    p.oil_counter = r.s16(k, 0x1172A4);
    p.fuel_counter = r.s16(k, 0x1172A2);
    p.fuel_period = r.s16(k, 0x1170BC);
    p.sea_step = r.s16(k, 0x1146BE);
    p.sea_counter = r.s16(k, 0x115300);
    p.sea_period = r.s16(k, 0x11533C);
    p.sea = r.s16(k, 0x115302);
    p.turn_delay = r.s16(k, 0x1159F4);
    p.extra = r.s16(k, 0x114FE6);
    p.sink_step = r.s16(k, 0x1159F8);
    p.wreck_time = r.s16(k, 0x1159FA);
    p.next_blast = r.s16(k, 0x1159FC);
}

// The blasts (-$6350: 15 x $2A bytes) and, in WOFREC3, the splash list.
constexpr std::uint32_t blast_table = 0x114C02;
void load_effects(const Recording& r, std::size_t k, wings::game::Effects& e) {
    for (std::size_t i = 0; i < e.blasts.size(); ++i) {
        const auto at = blast_table + std::uint32_t(i * 0x2A);
        auto& b = e.blasts[i];
        b.active = r.u8(k, at + 0x20) == 8;
        b.listed = r.u8(k, at + 0xC) == 8;
        b.x = r.s16(k, at);
        b.y = r.s16(k, at + 4);
        b.frame = r.u8(k, at + 0x21);
        b.water = r.u8(k, at + 0x1F) == 0;
    }
    if (!r.has_effects()) return;
    for (std::size_t i = 0; i < e.splashes.size(); ++i) {
        const auto at = splash_list + std::uint32_t(i * 4);
        auto& s = e.splashes[i];
        s.x = r.s16(k, at);
        s.left = std::int8_t(r.u8(k, at + 2));
        s.dust = r.u8(k, at + 3) == 2;
    }
}

// The effects a tick started: (kind, x, y) in slot order. Natively, slots
// that were free before the tick; in the recording, slots that were free a
// tick earlier or restarted (a lower frame, another place).
using Started = std::vector<std::array<int, 3>>;
Started started(const wings::game::Effects& before, const wings::game::Effects& after) {
    Started out;
    for (std::size_t i = 0; i < after.blasts.size(); ++i) {
        const auto &a = before.blasts[i], &b = after.blasts[i];
        if (b.active && (!a.active || b.frame < a.frame || a.x != b.x || a.y != b.y || a.water != b.water)) {
            out.push_back({b.water ? 1 : 0, b.x, b.y});
        }
    }
    for (std::size_t i = 0; i < after.splashes.size(); ++i) {
        const auto &a = before.splashes[i], &b = after.splashes[i];
        if (b.left > a.left || (b.left != 0 && a.left != 0 && a.x != b.x)) out.push_back({b.dust ? 3 : 2, b.x, 0});
    }
    std::sort(out.begin(), out.end());
    return out;
}

void load_world(const Recording& r, std::size_t k, FlightWorld& w) {
    const std::array<std::uint32_t, 5> ships = {0x1153B4, 0x1153D2, 0x1153F0, 0x11540E, 0x11542C};
    for (std::size_t i = 0; i < ships.size(); ++i) {
        auto& s = w.ships[i];
        s.first = r.s16(k, ships[i]);
        s.last = r.s16(k, ships[i] + 2);
        s.afloat = r.s16(k, ships[i] + 4) != 0;
        s.ready = r.s16(k, ships[i] + 0xC);
        s.deck = r.s16(k, ships[i] + 0xE);
        s.sunk = r.s16(k, ships[i] + 0x14);
        s.bob = r.s16(k, ships[i] + 0x1A);
    }
    w.carrier_x = r.s16(k, 0x1152E6);
    w.elevator = r.s16(k, 0x1152E8);
    w.lift = r.s16(k, 0x1152EA);
    w.menu = r.u8(k, 0x1152B8) != 0;
    w.menu_repeat = std::int8_t(r.u8(k, 0x1152C2));
    w.input_delay = r.s16(k, 0x116C92);
    w.weapon = r.s16(k, 0x1152F8);
    w.weapon_load = r.u8(k, 0x1152C1);
    w.spare_planes = std::int8_t(r.u8(k, 0x1152B0));
    w.draw_counter = r.s16(k, 0x11531C);
    w.elevator_seen = r.s16(k, 0x1172A6);
    w.caught_wire_x = r.s16(k, 0x116C8E);
    w.random_load = r.s16(k + 1 < r.ticks() ? k + 1 : k, 0x114FDC); // (the beam-position random: taken as recorded)
}

// The world fields the tick changes, compared too.
std::vector<std::string> world_differences(const FlightWorld& a, const FlightWorld& b) {
    std::vector<std::string> out;
    char line[96];
    const auto check = [&](const char* name, int x, int y) {
        if (x != y) {
            std::snprintf(line, sizeof line, "%s %d/%d", name, x, y);
            out.push_back(line);
        }
    };
    check("elevator", a.elevator, b.elevator);
    check("lift", a.lift, b.lift);
    check("menu", a.menu, b.menu);
    check("menu_repeat", a.menu_repeat, b.menu_repeat);
    check("input_delay", a.input_delay, b.input_delay);
    check("weapon", a.weapon, b.weapon);
    check("weapon_load", a.weapon_load, b.weapon_load);
    check("spares", a.spare_planes, b.spare_planes);
    check("caught_wire", a.caught_wire_x, b.caught_wire_x);
    return out;
}

// The fields compared, as (name, native, recorded).
std::vector<std::string> differences(const PlayerFlight& a, const PlayerFlight& b) {
    std::vector<std::string> out;
    char line[160];
    const auto check = [&](const char* name, int x, int y) {
        if (x != y) {
            std::snprintf(line, sizeof line, "%s %d/%d", name, x, y);
            out.push_back(line);
        }
    };
    check("altitude", a.altitude, b.altitude);
    check("x", a.x, b.x);
    check("mode", a.mode, b.mode);
    check("fuel", a.fuel, b.fuel);
    check("oil", a.oil, b.oil);
    check("facing", a.facing, b.facing);
    check("speed_x", a.speed_x, b.speed_x);
    check("speed_y", a.speed_y, b.speed_y);
    check("climb", a.climb, b.climb);
    check("speed", a.speed, b.speed);
    check("throttle", a.throttle, b.throttle);
    check("turn", a.turn, b.turn);
    check("smoothed", a.smoothed_climb, b.smoothed_climb);
    check("kick", a.climb_kick, b.climb_kick);
    check("pulling_up", a.pulling_up, b.pulling_up);
    check("lift", a.lift, b.lift);
    check("gear", a.gear, b.gear);
    check("signal", a.signal, b.signal);
    check("attitude", a.attitude, b.attitude);
    check("wheel_att", a.wheel_attitude, b.wheel_attitude);
    check("sea", a.sea, b.sea);
    check("turn_delay", a.turn_delay, b.turn_delay);
    check("sink_step", a.sink_step, b.sink_step);
    check("wreck_time", a.wreck_time, b.wreck_time);
    check("next_blast", a.next_blast, b.next_blast);
    // (+$10 goes down with hits from enemy planes, which are not rebuilt:
    // not compared.)
    check("extra", a.extra, b.extra);
    check("fresh", a.fresh_on_deck, b.fresh_on_deck);
    if (a.shape != b.shape) out.push_back("shape " + a.shape + "/" + b.shape);
    if (a.wheels != b.wheels) out.push_back("wheels " + a.wheels + "/" + b.wheels);
    return out;
}

int check_flight(const std::string& content, const std::string& map_name, const std::string& file, bool resync) {
    const auto assets = wings::data::load_assets(platform::utf8_path(content));
    const auto tables = wings::game::read_flight_tables(assets.program);
    const wings::data::MissionMap* map = nullptr;
    for (const auto& m : assets.maps) {
        if (m.name == map_name) map = &m;
    }
    if (map == nullptr) throw std::runtime_error("no map " + map_name);
    const Recording rec(file);
    const auto crew_tables = wings::game::read_crew_tables(assets.program);
    PlayerFlight p;
    load_player(rec, 0, p);
    FlightWorld w;
    w.map = map;
    int bad_ticks = 0;
    int carrier_frames = 0, carrier_bad = 0, hidden_frames = 0, hidden_bad = 0;
    int effects_started = 0, misplaced = 0, unchecked = 0;
    if (rec.has_effects()) w.random.seed = rec.u32(0, random_seed);
    for (std::size_t k = 0; k + 1 < rec.ticks(); ++k) {
        if (resync) load_player(rec, k, p);
        if (resync || k == 0) {
            load_world(rec, k, w);
        } else {
            // Only what the rebuilt code does not drive follows the recording.
            FlightWorld recorded;
            recorded.map = map;
            load_world(rec, k, recorded);
            w.ships = recorded.ships;
            w.carrier_x = recorded.carrier_x;
            w.draw_counter = recorded.draw_counter;
            w.random_load = recorded.random_load;
        }
        // The effects in play are taken from the recording each tick (the
        // original steps them as it draws, at its own frame rate); what the
        // tick starts is compared.
        load_effects(rec, k, w.effects);
        if (resync && rec.has_effects()) w.random.seed = rec.u32(k, random_seed);
        const auto effects_before = w.effects;
        const int x_before = p.x;
        const int mode_before = p.mode;
        wings::game::flight_tick(p, w, tables, rec.command(k));
        PlayerFlight want;
        load_player(rec, k + 1, want);
        FlightWorld want_world;
        want_world.map = map;
        load_world(rec, k + 1, want_world);
        if (!rec.has_effects()) p.next_blast = want.next_blast; // (it comes from the unrecorded seed)
        auto diff = differences(p, want);
        for (const auto& d : world_differences(w, want_world)) diff.push_back(d);
        wings::game::Effects recorded_before, recorded_after;
        load_effects(rec, k, recorded_before);
        load_effects(rec, k + 1, recorded_after);
        if (!rec.has_effects()) {
            w.effects.splashes = recorded_after.splashes = recorded_before.splashes = effects_before.splashes;
        }
        const auto native = started(effects_before, w.effects);
        auto original = started(recorded_before, recorded_after);
        // ($10B304's blast, placed by a memory address, is left out: a blast
        // far from the plane.)
        const auto far = [&](const std::array<int, 3>& e) {
            return e[0] == 0 && std::abs(e[1] - x_before) > 0x40 && std::abs(e[1] - want.x) > 0x40;
        };
        misplaced += int(std::count_if(original.begin(), original.end(), far));
        original.erase(std::remove_if(original.begin(), original.end(), far), original.end());
        effects_started += int(native.size());
        // Without the seed a wreck's blasts cannot be placed.
        const bool wreck = mode_before == 6 || mode_before == 8;
        if (!rec.has_effects() && wreck) {
            unchecked += int(original.size());
        } else if (native != original) {
            std::string line = "effects";
            for (const auto& e : native) line += " " + std::to_string(e[0]) + ":" + std::to_string(e[1]) + "," + std::to_string(e[2]);
            line += " /";
            for (const auto& e : original) line += " " + std::to_string(e[0]) + ":" + std::to_string(e[1]) + "," + std::to_string(e[2]);
            diff.push_back(line);
        }
        // The deck crew (-$59A8..-$5972) and the tower flag step (-$5C71) as
        // the frames between the two snapshots are drawn (the draw counter's
        // difference), when their columns are on screen (camera: the
        // plane's x after the tick).
        {
            wings::game::DeckCrew crew{r16(rec, k, 0x1155AA), r16(rec, k, 0x1155AE), r16(rec, k, 0x1155A8),
                                       r16(rec, k, 0x1155AC), int(rec.u8(k, 0x1155E0))};
            int flag = rec.u8(k, 0x1152E1);
            wings::game::BackdropState view;
            view.camera = want.x;
            view.eighth_scale = want.altitude > 0xBA;
            const auto shown = wings::game::specials_shown(*map, view);
            const int first = rec.s16(k, 0x11531C), frames = std::int16_t(rec.s16(k + 1, 0x11531C) - first);
            for (int f = 1; f <= frames; ++f) {
                if (((first + f) & 1) != 0) {
                    for (int i = 0; i < shown.us_flag; ++i) flag = flag == 0 ? 3 : flag - 1;
                }
                if (shown.deck_crew > 0) crew.step(crew_tables, want.signal);
            }
            if (frames > 0 && shown.us_flag + shown.deck_crew == 0) {
                ++hidden_frames;
                if (r16(rec, k + 1, 0x1155AA) != crew.pose_a || r16(rec, k + 1, 0x1155AE) != crew.pose_b ||
                    rec.u8(k + 1, 0x1152E1) != flag) {
                    ++hidden_bad;
                }
            }
            if (frames > 0 && shown.us_flag + shown.deck_crew > 0) {
                ++carrier_frames;
                const wings::game::DeckCrew got{r16(rec, k + 1, 0x1155AA), r16(rec, k + 1, 0x1155AE),
                                                r16(rec, k + 1, 0x1155A8), r16(rec, k + 1, 0x1155AC),
                                                int(rec.u8(k + 1, 0x1155E0))};
                if (crew.pose_a != got.pose_a || crew.pose_b != got.pose_b || crew.target_a != got.target_a ||
                    crew.target_b != got.target_b || crew.phase != got.phase || flag != rec.u8(k + 1, 0x1152E1)) {
                    if (++carrier_bad <= 5) {
                        std::printf("tick %zu carrier: crew %d %d (%d %d, %d) / %d %d (%d %d, %d), flag %d/%d\n", k,
                                    crew.pose_a, crew.pose_b, crew.target_a, crew.target_b, crew.phase, got.pose_a,
                                    got.pose_b, got.target_a, got.target_b, got.phase, flag, rec.u8(k + 1, 0x1152E1));
                    }
                }
            }
        }
        if (!diff.empty()) {
            std::printf("tick %zu (command %02x, mode %d):", k, rec.command(k), want.mode);
            for (const auto& d : diff) std::printf(" %s", d.c_str());
            std::printf("\n");
            if (++bad_ticks >= (resync ? 40 : 1)) break;
        }
    }
    std::printf("carrier animation: %d tick gaps with the crew or the flag drawn, %d differ; %d off screen, %d moved\n",
                carrier_frames, carrier_bad, hidden_frames, hidden_bad);
    std::printf("%zu ticks, %d differ%s; %d effects started (kind:x,y; %s), %d misplaced blasts left out, %d wreck blasts unchecked\n",
                rec.ticks() - 1, bad_ticks, resync ? " (resynced)" : "", effects_started,
                rec.has_effects() ? "blasts, splashes and the seed recorded" : "blasts only", misplaced, unchecked);
    return bad_ticks == 0 ? 0 : 1;
}

std::vector<int> parse_script(const std::string& text) {
    std::vector<int> out;
    std::istringstream items(text);
    for (std::string item; std::getline(items, item, ',');) {
        const auto colon = item.find(':');
        const int count = std::stoi(item.substr(0, colon));
        const int cmd = std::stoi(item.substr(colon + 1), nullptr, 16);
        out.insert(out.end(), std::size_t(count), cmd);
    }
    return out;
}

int simulate(const std::string& content, const std::string& map_name, const std::string& file,
             const std::string& script, int every) {
    const auto assets = wings::data::load_assets(platform::utf8_path(content));
    const auto tables = wings::game::read_flight_tables(assets.program);
    const wings::data::MissionMap* map = nullptr;
    for (const auto& m : assets.maps) {
        if (m.name == map_name) map = &m;
    }
    if (map == nullptr) throw std::runtime_error("no map " + map_name);
    const Recording rec(file);
    PlayerFlight p;
    load_player(rec, 0, p);
    FlightWorld w;
    w.map = map;
    load_world(rec, 0, w);
    const auto commands = parse_script(script);
    for (std::size_t k = 0; k < commands.size(); ++k) {
        wings::game::flight_tick(p, w, tables, commands[k]);
        if (int(k) % every == 0 || k + 1 == commands.size()) {
            std::printf("%4zu cmd %02x mode %d alt %4d x %6d facing %2d speed %4d climb %5d turn %2d fuel %3d %s%s\n", k,
                        commands[k], p.mode, p.altitude, p.x, p.facing, p.speed, p.climb, p.turn, p.fuel,
                        p.shape.c_str(), p.eighth_scale ? " 1/8" : "");
        }
    }
    return 0;
}

int play(const std::string& content, const std::string& script, const std::string& out,
         const std::string& tick_list) {
    std::set<int> wanted;
    std::istringstream items(tick_list);
    for (std::string item; std::getline(items, item, ',');) wanted.insert(std::stoi(item));
    auto module = wings::game::start(platform::utf8_path(content));
    const auto commands = parse_script(script);
    std::filesystem::create_directories(platform::utf8_path(out));
    for (std::size_t k = 0; k < commands.size(); ++k) {
        app::GameInput input;
        input.up = commands[k] & 1;
        input.down = commands[k] & 2;
        input.right = commands[k] & 4;
        input.left = commands[k] & 8;
        input.fire = (commands[k] & 0x30) != 0; // held for the tick: released within 10 blanks = a tap
        for (int v = 0; v < 4; ++v) module->tick(input);
        if (wanted.count(int(k))) {
            platform::save_png(module->picture(), platform::utf8_path(out) / ("tick" + std::to_string(k) + ".png"));
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::string command = argc > 1 ? argv[1] : "";
    try {
        if (command == "ffp" && argc == 4) return check_ffp(argv[2], argv[3]);
        if (command == "play" && argc == 6) return play(argv[2], argv[3], argv[4], argv[5]);
        if (command == "simulate" && (argc == 6 || argc == 7)) {
            return simulate(argv[2], argv[3], argv[4], argv[5], argc == 7 ? std::stoi(argv[6]) : 10);
        }
        if (command == "flight" && (argc == 5 || argc == 6)) {
            return check_flight(argv[2], argv[3], argv[4], argc == 6 && std::string(argv[5]) == "resync");
        }
        std::fprintf(stderr, "usage: wings_of_fury_replay ffp <op> <oracle file>\n");
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "wings_of_fury_replay: %s\n", error.what());
        return 1;
    }
}
