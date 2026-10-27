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
#include <set>
#include <array>
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
class Recording {
public:
    static constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 4> ranges = {{
        {0x114680, 0x115F00}, {0x116C80, 0x116CE0}, {0x117080, 0x1172C0}, {0x117D30, 0x117EC0}}};

    explicit Recording(const std::string& file) : bytes_(platform::read_file(platform::utf8_path(file))) {
        if (bytes_.size() < 8 || std::string(bytes_.begin(), bytes_.begin() + 8) != "WOFREC2\n") {
            throw std::runtime_error("not a WOFREC2 recording");
        }
        for (const auto& [a, b] : ranges) size_ += b - a;
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
        for (const auto& [a, b] : ranges) {
            if (address >= a && address < b) return at + (address - a);
            at += b - a;
        }
        throw std::runtime_error("address outside the recording");
    }
    std::vector<std::uint8_t> bytes_;
    std::size_t size_{};
};

using wings::game::FlightWorld;
using wings::game::PlayerFlight;

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
    PlayerFlight p;
    load_player(rec, 0, p);
    FlightWorld w;
    w.map = map;
    int bad_ticks = 0;
    for (std::size_t k = 0; k + 1 < rec.ticks(); ++k) {
        if (resync) load_player(rec, k, p);
        load_world(rec, k, w);
        wings::game::flight_tick(p, w, tables, rec.command(k));
        PlayerFlight want;
        load_player(rec, k + 1, want);
        const auto diff = differences(p, want);
        if (!diff.empty()) {
            std::printf("tick %zu (command %02x, mode %d):", k, rec.command(k), want.mode);
            for (const auto& d : diff) std::printf(" %s", d.c_str());
            std::printf("\n");
            if (++bad_ticks >= (resync ? 40 : 1)) break;
        }
    }
    std::printf("%zu ticks, %d differ%s\n", rec.ticks() - 1, bad_ticks, resync ? " (resynced)" : "");
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
