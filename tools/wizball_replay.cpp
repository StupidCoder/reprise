// Research and verification tool for Wizball (not part of the game).
//
//   wizball_replay ramframe <ram.bin> <colour.bin> <vic.bin> <writes.txt> <out.png> [cycle]
//       Builds a Display from the original's memory and the VIC register
//       writes of one frame (harness wizref: WIZREF_DUMPS at the frame's
//       start, WIZREF_WRITES=d000-d02e,dd00-dd00), the way the VIC-II reads
//       them, and renders it, to check the renderer against the emulator's
//       picture. A write in cycle c of line l counts from line l when c is
//       below [cycle] (default 13), else from l + 1.
//
//   wizball_replay lockstep <content> <ticks.bin> <pclog.txt> <ranges> [passes] [png dir]
//       Runs the game pass by pass beside a harness recording (wizref with
//       WIZREF_PASS=640e, WIZREF_TICKS=<ranges>, WIZREF_PCLOG=6392,63ca,6893,63e0,640e,753f):
//       the original's random numbers are replayed phase by phase (level
//       start $63CA-$6893, set-up $63E0-first pass, then each pass), the
//       ports it read are fed in, and after each pass the rebuilt state is
//       compared with the original's memory at the start of the next pass.

#include "platform/files.hpp"
#include "platform/host.hpp"
#include "wizball_c64/data/assets.hpp"
#include "wizball_c64/game/display.hpp"
#include "wizball_c64/game/play.hpp"
#include "wizball_c64/game/front_end.hpp"
#include "wizball_c64/game/scores.hpp"
#include "wizball_c64/game/module.hpp"
#include "wizball_c64/enhanced/effects.hpp"
#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace wizball::game;

struct Write {
    int line, cycle;
    unsigned address;
    std::uint8_t value;
};

std::vector<Write> read_writes(const std::string& path) {
    std::vector<Write> out;
    std::ifstream in(path);
    std::string text;
    while (std::getline(in, text)) {
        // "<cycle> pc $XXXX $AAAA = VV line L cycle C"
        std::istringstream s(text);
        std::string cyc, pc, pcv, addr, eq, val, l, lv, c, cv;
        if (!(s >> cyc >> pc >> pcv >> addr >> eq >> val >> l >> lv >> c >> cv)) continue;
        out.push_back({std::stoi(lv), std::stoi(cv), unsigned(std::stoul(addr.substr(1), nullptr, 16)),
                       std::uint8_t(std::stoul(val, nullptr, 16))});
    }
    return out;
}

int ramframe(int argc, char** argv) {
    auto ram = platform::read_file(argv[2]);
    auto colour_ram = platform::read_file(argv[3]);
    const auto vic0 = platform::read_file(argv[4]);
    const auto writes = read_writes(argv[5]);
    const int threshold = argc > 7 ? std::atoi(argv[7]) : 13;

    std::array<std::uint8_t, 64> regs{};
    for (int i = 0; i < 64; ++i) regs[std::size_t(i)] = vic0[std::size_t(i)];
    std::uint8_t dd00 = vic0[64];

    Display display;
    display.screens.assign(25, {});
    std::deque<Charset> charsets;
    unsigned last_chars = ~0u;
    bool chars_dirty = true;

    std::size_t next = 0;
    for (int r = 0; r < Display::raster_lines; ++r) {
        while (next < writes.size() &&
               (writes[next].line < r || (writes[next].line == r && writes[next].cycle < threshold))) {
            const auto& w = writes[next++];
            if (w.address == 0xDD00) dd00 = w.value;
            else if (w.address >= 0xD000 && w.address < 0xD040) regs[w.address - 0xD000] = w.value;
            else if (w.address >= 0xD800 && w.address < 0xDC00) colour_ram[w.address - 0xD800] = w.value;
            else {
                ram[w.address] = w.value;
                if (w.address >= 0xC000) chars_dirty = true;
            }
        }
        const unsigned bank = unsigned(~dd00 & 3) << 14;
        const unsigned screen = bank + unsigned(regs[0x18] >> 4) * 0x400;
        const unsigned chars = bank + unsigned((regs[0x18] >> 1) & 7) * 0x800;
        if (r >= Display::text_top && r < Display::text_top + 200 && (r - Display::text_top) % 8 == 0) {
            const int row = (r - Display::text_top) / 8;
            for (int i = 0; i < 40; ++i) {
                display.screens[std::size_t(row)].codes[std::size_t(row * 40 + i)] = ram[screen + unsigned(row * 40 + i)];
                display.colours[std::size_t(row * 40 + i)] = colour_ram[std::size_t(row * 40 + i)] & 15;
            }
        }
        if (chars != last_chars || chars_dirty) {
            Charset cs{};
            for (int c = 0; c < 256; ++c)
                for (int y = 0; y < 8; ++y) cs[std::size_t(c)][std::size_t(y)] = ram[chars + unsigned(c * 8 + y)];
            charsets.push_back(cs);
            last_chars = chars;
            chars_dirty = false;
        }
        auto& line = display.line(r);
        line.border = regs[0x20] & 15;
        line.background = regs[0x21] & 15;
        line.shared1 = regs[0x22] & 15;
        line.shared2 = regs[0x23] & 15;
        line.sprite_shared1 = regs[0x25] & 15;
        line.sprite_shared2 = regs[0x26] & 15;
        line.multicolour = regs[0x16] & 0x10;
        line.columns38 = !(regs[0x16] & 0x08);
        line.xscroll = regs[0x16] & 7;
        line.charset = &charsets.back();
        line.screen = r >= Display::text_top ? std::min(24, (r - Display::text_top) / 8) : 0;
        for (int n = 0; n < 8; ++n) {
            SpriteSettings s;
            s.enabled = regs[0x15] >> n & 1;
            s.x = regs[std::size_t(2 * n)] | (regs[0x10] >> n & 1) << 8;
            s.y = regs[std::size_t(2 * n + 1)];
            const unsigned pointer = ram[screen + 0x3F8 + unsigned(n)];
            for (int i = 0; i < 63; ++i) s.shape[std::size_t(i)] = ram[(bank + pointer * 64 + unsigned(i)) & 0xFFFF];
            s.colour = regs[std::size_t(0x27 + n)] & 15;
            s.multicolour = regs[0x1C] >> n & 1;
            s.expand_x = regs[0x1D] >> n & 1;
            s.expand_y = regs[0x17] >> n & 1;
            s.behind = regs[0x1B] >> n & 1;
            display.set_sprite(n, r, s);
        }
    }
    platform::save_png(display.render(), platform::utf8_path(argv[6]));
    return 0;
}
struct Event {
    std::uint16_t pc;
    std::uint8_t a;
};

std::vector<Event> read_pclog(const std::string& path) {
    std::vector<Event> out;
    std::ifstream in(path);
    std::string text;
    while (std::getline(in, text)) {
        std::istringstream s(text);
        std::string cycle, pc, al, av;
        if (!(s >> cycle >> pc >> al >> av)) continue;
        out.push_back({std::uint16_t(std::stoul(pc.substr(1), nullptr, 16)), std::uint8_t(std::stoul(av, nullptr, 16))});
    }
    return out;
}

class ReplayRandom : public RandomSource {
public:
    std::vector<std::uint8_t> values;
    std::size_t next_index = 0;
    bool overrun = false;
    void set(std::vector<std::uint8_t> v) {
        values = std::move(v);
        next_index = 0;
        overrun = false;
    }
    std::uint8_t next() override {
        if (next_index < values.size()) return values[next_index++];
        overrun = true;
        return 0;
    }
};

struct Ranges {
    std::vector<std::pair<unsigned, unsigned>> list;
    std::size_t size = 0;
    explicit Ranges(const std::string& spec) {
        std::istringstream s(spec);
        std::string item;
        while (std::getline(s, item, ',')) {
            if (item == "c") { // colour RAM
                list.push_back({0xD800, 0xDBFF});
                size += 0x400;
                continue;
            }
            const auto dash = item.find('-');
            const unsigned lo = unsigned(std::stoul(item.substr(0, dash), nullptr, 16));
            const unsigned hi = unsigned(std::stoul(item.substr(dash + 1), nullptr, 16));
            list.push_back({lo, hi});
            size += hi - lo + 1;
        }
    }
};

std::vector<std::vector<std::uint8_t>> read_ticks(const std::string& path, const Ranges& ranges) {
    const auto bytes = platform::read_file(path);
    const std::size_t record = 4 + ranges.size;
    std::vector<std::vector<std::uint8_t>> out;
    for (std::size_t at = 0; at + record <= bytes.size(); at += record) {
        std::vector<std::uint8_t> m(65536, 0);
        std::size_t p = at + 4;
        for (auto [lo, hi] : ranges.list)
            for (unsigned a = lo; a <= hi; ++a) m[a] = bytes[p++];
        out.push_back(std::move(m));
    }
    return out;
}

bool colour_ram_recorded = false;
const wizball::data::GameImage* g_image = nullptr;

struct Compare {
    std::vector<std::string> diffs;
    void check(const std::string& name, int mine, int original) {
        if (mine != original) {
            char line[160];
            std::snprintf(line, sizeof line, "%s: rebuilt %d ($%02X), original %d ($%02X)", name.c_str(), mine, mine & 0xFFFF,
                          original, original & 0xFFFF);
            diffs.push_back(line);
        }
    }
};

void compare_state(const Play& play, const std::vector<std::uint8_t>& m, Compare& c) {
    const auto& pf = play.playfield();
    const auto pr = pf.probe();
    c.check("view $B1F1", pf.view_column(), m[0xB1F1]);
    c.check("fine $B1F2", pf.fine(), m[0xB1F2]);
    c.check("fraction $B1F3", pr.fraction, m[0xB1F3]);
    c.check("fine scroll $B1FC", pf.fine_scroll(), m[0xB1FC]);
    c.check("shown $B1FD", pf.shown_screen() * 16, m[0xB1FD]);
    c.check("wiz offset $B1F9", std::uint8_t(pf.wiz_offset()), m[0xB1F9]);
    c.check("step $B1FF", std::uint8_t(pf.step()), m[0xB1FF]);
    c.check("delta $B1FB", std::uint8_t(pf.pixel_delta()), m[0xB1FB]);
    c.check("at end $B25D", pf.at_end(), m[0xB25D]);
    c.check("tile column $B209", pr.tile_column, m[0xB209]);
    c.check("tile char $B20B", pr.tile_char, m[0xB20B]);
    c.check("star counter $B27A", pr.star_counter, m[0xB27A]);
    for (int i = 0; i < 3; ++i) {
        c.check("progress $B20" + std::to_string(i), pr.progress[std::size_t(i)], m[0xB200 + unsigned(i)]);
        c.check("role $B20" + std::to_string(3 + i), pr.role[std::size_t(i)], m[0xB203 + unsigned(i)]);
    }
    for (int k = 0; k < 5; ++k) {
        const auto& s = pf.stars()[std::size_t(k)];
        const unsigned b = 0xB1B4 + 5u * unsigned(k);
        c.check("star " + std::to_string(k) + " offset", s.offset, m[b]);
        c.check("star " + std::to_string(k) + " position", s.position, m[b + 1]);
        c.check("star " + std::to_string(k) + " twinkle", s.twinkle, m[b + 4]);
    }
    const auto& w = play.wiz();
    const auto wp = w.probe();
    c.check("wiz x $B20D", w.x(), m[0xB20D] | m[0xB20E] << 8);
    c.check("wiz y $B20F", w.y(), m[0xB20F]);
    c.check("y fraction $B210", wp.y_fraction, m[0xB210]);
    c.check("vy $B211", wp.vy, m[0xB211]);
    c.check("vy fraction $B212", wp.vy_fraction, m[0xB212]);
    c.check("falling $B217", wp.falling, m[0xB217]);
    c.check("vy direct $B218", wp.vy_direct, m[0xB218]);
    c.check("spin $B21A", wp.spin, m[0xB21A]);
    c.check("spin fraction $B21B", wp.spin_fraction, m[0xB21B]);
    c.check("vx $B21C", wp.vx, m[0xB21C]);
    c.check("last vx $B21D", wp.last_vx, m[0xB21D]);
    c.check("appear $B216", wp.appear, m[0xB216]);
    c.check("appear frame $B214", wp.appear_frame, m[0xB214]);
    c.check("appear clock $B215", wp.appear_clock, m[0xB215]);
    c.check("speed $B1F4", std::uint8_t(w.speed_index()), m[0xB1F4]);
    c.check("trail index $B243", wp.trail_index, m[0xB243]);
    for (int i = 0; i < 8; ++i) {
        c.check("trail y " + std::to_string(i), w.trail_y()[std::size_t(i)], m[0xB244 + unsigned(i)]);
        c.check("trail x " + std::to_string(i), w.trail_x()[std::size_t(i)], m[0xB24C + unsigned(i)]);
    }
    c.check("aliens $B1AF", play.aliens_left(), m[0xB1AF]);
    for (int i = 0; i < 4; ++i) {
        c.check("colour $B12" + std::string(1, "BCDE"[i]), play.colour()[std::size_t(i)], m[0xB12B + unsigned(i)]);
        c.check("cauldron clock " + std::to_string(i), play.cauldron_rows()[std::size_t(i)], m[0x9EC6 + unsigned(i)]);
    }
    c.check("level cleared $B874", play.level_cleared(), m[0xB874]);
    c.check("stick $B23F", play.wiz_stick().bits, m[0xB23F]);
    c.check("fire held $B242", play.fire_held(), m[0xB242]);
    const auto wg = play.waggle();
    c.check("waggle mask $723A", wg[0], m[0x723A]);
    c.check("waggle idle $723B", wg[1], m[0x723B]);
    c.check("waggles $723C", wg[2], m[0x723C]);
    c.check("music mode $B8F1", play.sound().mode(), m[0xB8F1]);
    c.check("music speed $B8F2", play.sound().speed(), m[0xB8F2]);
    for (int k = 0; k < 3; ++k) {
        const auto& v = play.sound().music().voices()[std::size_t(k)];
        c.check("tune voice " + std::to_string(k) + " on", v.active, m[0x59 + unsigned(k)] != 0);
        if (!v.active) continue;
        c.check("tune voice " + std::to_string(k) + " duration", v.duration, m[0x0A + unsigned(k)]);
        c.check("tune voice " + std::to_string(k) + " pointer", v.pointer, m[0x04 + 2u * unsigned(k)] | m[0x05 + 2u * unsigned(k)] << 8);
    }
    for (int k = 0; k < 3; ++k) {
        const auto& sl = play.sound().slots()[std::size_t(k)];
        const unsigned b = 0x0400 + 0x20u * unsigned(k);
        c.check("sound slot " + std::to_string(k) + " gate", sl.gate1, m[b + 2]);
        c.check("sound slot " + std::to_string(k) + " gate 2", sl.gate2, m[b + 3]);
        c.check("sound slot " + std::to_string(k) + " frequency", sl.frequency, m[b + 7] | m[b + 8] << 8);
    }
    for (int k = 0; k < 6; ++k) {
        auto o = play.objects()[std::size_t(k)];
        const unsigned b = 0x0200 + 0x2Au * unsigned(k);
        if (!o.active() && m[b + 2] == 0) continue;
        for (int f = 0; f <= 0x1C; ++f)
            c.check("object " + std::to_string(k) + " field $" + std::to_string(f), o.field(f), m[b + unsigned(f)]);
        c.check("object " + std::to_string(k) + " box left", o.box_left, m[b + 0x1D]);
        c.check("object " + std::to_string(k) + " box right", o.box_right, m[b + 0x1E]);
        c.check("object " + std::to_string(k) + " box top", o.box_top, m[b + 0x1F]);
        c.check("object " + std::to_string(k) + " box bottom", o.box_bottom, m[b + 0x20]);
        c.check("object " + std::to_string(k) + " fire clock", o.fire_clock, m[b + 0x21]);
        c.check("object " + std::to_string(k) + " fire angle", o.fire_angle, m[b + 0x22]);
        c.check("object " + std::to_string(k) + " anim clock", o.anim_clock, m[b + 0x23]);
        c.check("object " + std::to_string(k) + " frame", o.frame, m[b + 0x24]);
        c.check("object " + std::to_string(k) + " hit", o.hit, m[b + 0x25]);
    }
    for (int k = 0; k < 6; ++k) {
        const auto& sh = play.shots()[std::size_t(k)];
        const unsigned b = 0x0480 + 12u * unsigned(k);
        c.check("shot " + std::to_string(k) + " code", sh.code, m[b + 4]);
        if (sh.code == 0) continue;
        c.check("shot " + std::to_string(k) + " column", sh.column, m[b]);
        c.check("shot " + std::to_string(k) + " row", sh.row, m[b + 1]);
        c.check("shot " + std::to_string(k) + " dx", std::uint8_t(sh.dx), m[b + 2]);
    }
    for (int k = 0; k < 16; ++k) {
        const auto& e = play.enemy_shots()[std::size_t(k)];
        const unsigned b = 0x0300 + 16u * unsigned(k);
        c.check("enemy shot " + std::to_string(k) + " state", e.state, m[b + 0x0A]);
        if (e.state == 0) continue;
        c.check("enemy shot " + std::to_string(k) + " column", e.column, m[b]);
        c.check("enemy shot " + std::to_string(k) + " row", e.row, m[b + 1]);
        c.check("enemy shot " + std::to_string(k) + " sub x", e.sub_x, m[b + 2]);
        c.check("enemy shot " + std::to_string(k) + " sub y", e.sub_y, m[b + 3]);
        c.check("enemy shot " + std::to_string(k) + " dx", std::uint8_t(e.dx), m[b + 4]);
        c.check("enemy shot " + std::to_string(k) + " dy", std::uint8_t(e.dy), m[b + 5]);
    }
    c.check("cursor $B1AE", play.cursor(), m[0xB1AE]);
    c.check("sprites $E7D9", play.sprite_mask(), m[0xE7D9]);
    c.check("catellite on $B234", play.catellite_on(), m[0xB234]);
    c.check("place $B128", play.place(), m[0xB128]);
    c.check("levels done $B129", play.levels_done(), m[0xB129]);
    c.check("rounds $B12A", play.rounds(), m[0xB12A]);
    for (int p = 0; p < 8; ++p) c.check("painted $B1" + std::to_string(0x1F + p), play.painted(p), m[0xB11Fu + unsigned(p)]);
    c.check("colour run clock $EBB3", play.colour_run_clocks()[0], m[0xEBB3]);
    c.check("colour run index $EBB8", play.colour_run_clocks()[1], m[0xEBB8]);
    {
        const auto& k = play.catellite();
        const auto p = k.probe();
        c.check("catellite x $B261", k.x(), m[0xB261]);
        c.check("catellite y $B262", k.y(), m[0xB262]);
        const std::uint8_t mine[] = {p.stick_dx, p.stick_dy, p.vx, p.vy, p.anim_clock, p.anim_frame, p.offset,
                                     p.following, p.steer_clock, k.hits(), p.colour_clock};
        for (unsigned j = 0; j < 11; ++j) {
            char name[40];
            std::snprintf(name, sizeof name, "catellite $%04X", 0xB263 + j);
            c.check(name, mine[j], m[0xB263 + j]);
        }
        c.check("catellite wandering $861A", k.wandering(), m[0x861A]);
        c.check("catellite wander clock $8619", p.wander_clock, m[0x8619]);
        c.check("catellite wander stick $861B", p.wander_value, m[0x861B]);
        c.check("catellite shield $89B6", p.shield, m[0x89B6]);
        c.check("catellite shield clock $89B7", p.shield_clock, m[0x89B7]);
        c.check("catellite shield blink $89B8", p.shield_blink, m[0x89B8]);
        c.check("catellite shield warned $89B9", p.shield_warned, m[0x89B9]);
        if (m[0xB234]) {
            c.check("sprite 1 x", k.sprite().x, m[0xE7C6] | (m[0xE7D4] >> 1 & 1) << 8);
            c.check("sprite 1 pointer", k.sprite().pointer, m[0xB526]);
            c.check("sprite 1 colour", k.sprite().colour, m[0xE7EC]);
        }
        c.check("sprite 1 y", k.sprite().y, m[0xE7C7]);
    }
    for (int k = 0; k < 6; ++k) {
        if (!(m[0xE7D9] >> (k + 2) & 1)) continue;
        if (play.flying_text()) {
            const auto n = std::to_string(k + 2);
            c.check("text sprite " + n + " x", g_image->byte(std::uint16_t(0xB4E9 + 2 * k)), m[0xE7C8u + 2u * unsigned(k)]);
            c.check("text sprite " + n + " colour", play.flight_colour(), m[0xE7EDu + unsigned(k)]);
            continue;
        }
        if (play.in_lab()) {
            if (k > 2) continue;
            const auto& s = play.lab_sprite(k);
            const auto n = std::to_string(k + 2);
            c.check("lab sprite " + n + " x", s.x, m[0xE7C8u + 2u * unsigned(k)]);
            c.check("lab sprite " + n + " y", s.y, m[0xE7C9u + 2u * unsigned(k)]);
            c.check("lab sprite " + n + " pointer", s.pointer, m[0xB527u + unsigned(k)]);
            c.check("lab sprite " + n + " colour", s.colour, m[0xE7EDu + unsigned(k)]);
            continue;
        }
        const auto& o = play.object_sprite_of(k);
        const auto n = std::to_string(k + 2);
        c.check("sprite " + n + " x", o.x, m[0xE7C8u + 2u * unsigned(k)] | (m[0xE7D4] >> (k + 2) & 1) << 8);
        c.check("sprite " + n + " y", o.y, m[0xE7C9u + 2u * unsigned(k)]);
        c.check("sprite " + n + " pointer", o.pointer, m[0xB527u + unsigned(k)]);
        c.check("sprite " + n + " colour", o.colour, m[0xE7EDu + unsigned(k)]);
        c.check("sprite " + n + " multicolour", o.multicolour, m[0xB1B2] >> (k + 2) & 1);
    }
    c.check("dying $B1CF", play.dying(), m[0xB1CF]);
    c.check("death clock $B1D0", play.death_clock(), m[0xB1D0]);
    c.check("lives $B278", play.lives(1), m[0xB278]);
    c.check("token $B258", play.token_selected(), m[0xB258]);
    for (int i = 0; i < 7; ++i) c.check("score digit " + std::to_string(i), play.score(1).digits[std::size_t(i)], m[0xBFDA + unsigned(i)]);
    {
        int bad = 0, first = -1;
        for (int r = 0; r < Playfield::rows; ++r)
            for (int col = 0; col < 40; ++col)
                if ((play.cell_colour(col, r) & 15) != (m[0xD800 + unsigned((r + 3) * 40 + col)] & 15)) {
                    if (first < 0) first = r * 40 + col;
                    ++bad;
                }
        if (bad && colour_ram_recorded) {
            char line[120];
            std::snprintf(line, sizeof line, "cell colours: %d differ, first row %d column %d (rebuilt %d, original %d)", bad,
                          first / 40, first % 40, play.cell_colour(first % 40, first / 40) & 15,
                          m[0xD800 + unsigned((first / 40 + 3) * 40 + first % 40)] & 15);
            c.diffs.push_back(line);
        }
    }
    // The shown screen's playfield rows.
    const unsigned screen = 0xE000 + unsigned(m[0xB1FD]) * 64;
    int bad = 0, first = -1;
    for (int r = 0; r < Playfield::rows; ++r)
        for (int col = 0; col < 40; ++col) {
            if (pf.code(col, r) != m[screen + unsigned((r + 3) * 40 + col)]) {
                if (first < 0) first = r * 40 + col;
                ++bad;
            }
        }
    if (bad) {
        char line[120];
        std::snprintf(line, sizeof line, "screen: %d cells differ, first row %d column %d (rebuilt $%02X, original $%02X)", bad,
                      first / 40, first % 40, pf.code(first % 40, first / 40), m[screen + unsigned((first / 40 + 3) * 40 + first % 40)]);
        c.diffs.push_back(line);
    }
    bad = 0;
    for (int code = 1; code <= Playfield::sky_codes; ++code) {
        const auto g = pf.sky_glyph(code);
        for (int y = 0; y < 8; ++y) bad += g[std::size_t(y)] != m[0xF800 + unsigned(code * 8 + y)];
    }
    if (bad) c.diffs.push_back("sky characters: " + std::to_string(bad) + " bytes differ");
    for (int k = 0; k < 4; ++k) {
        const auto shape = play.sprite(0xB9 + k);
        int b = 0;
        for (int i = 0; i < 63; ++i) b += shape[std::size_t(i)] != m[0xEE40 + unsigned(64 * k + i)];
        if (b) c.diffs.push_back("cauldron " + std::to_string(k) + ": " + std::to_string(b) + " bytes differ");
    }
    const unsigned list_at[4] = {0, 0xEC00, 0xEC80, 0xEF80};
    for (int slot = 1; slot <= 3; ++slot) {
        const auto& list = play.spawn_list(slot);
        int b = 0;
        for (std::size_t i = 0; i < list.size(); ++i) {
            b += list[i].column != m[list_at[slot] + 2 + 2 * i];
            b += list[i].kind != m[list_at[slot] + 3 + 2 * i];
        }
        b += m[list_at[slot] + 2 + 2 * list.size()] != 0xFF;
        if (b) c.diffs.push_back("spawn list " + std::to_string(slot) + ": " + std::to_string(b) + " bytes differ");
    }
}

// soak <content> <frames>: the rebuilt game as the module runs it, on the
// modelled random numbers and random sticks, with paint granted now and then
// so that clears, labs and flights come round; reports the stages reached.
int soak(int argc, char** argv) {
    const auto image = wizball::data::load_game_image(platform::utf8_path(argv[2]));
    const auto assets = wizball::data::decode_assets(image);
    const long frames = std::atol(argv[3]);
    wizball::game::GameRandom random(image);
    Sound sound(image, random);
    Play play(assets, image, random, sound);
    play.new_game(std::getenv("WIZ_MODE") ? std::atoi(std::getenv("WIZ_MODE")) : 0); // WIZ_MODE: the menu's mode (0-4)
    play.set_invincible(argc > 4);
    play.start_life();
    play.begin_play();
    std::uint32_t seed = 12345;
    auto rnd = [&] { return seed = seed * 1103515245u + 12345u, std::uint8_t(seed >> 16); };
    std::uint8_t stick = 0xFF;
    int counts[6]{}, games = 1;
    auto last_stage = play.stage();
    for (long f = 0; f < frames; ++f) {
        if (f % 23 == 0) stick = std::uint8_t(0xE0 | (rnd() & 0x1F));
        if (f % 2500 == 1000 && play.stage() == Play::Stage::play)
            for (int c = 0; c < 3; ++c) play.give_paint(c, 0x14);
        Ports ports;
        ports.port2 = stick;
        play.pass(ports);
        play.frame();
        if (play.hung()) {
            std::printf("hung at frame %ld (stage %d); kept:", f, int(play.stage()));
            for (auto w : play.kept_weapons()) std::printf(" %d", w);
            std::printf("\n");
            break;
        }
        if (play.stage() != last_stage) {
            ++counts[int(play.stage())];
            last_stage = play.stage();
        }
        if (play.life_over()) {
            if (!play.end_life()) {
                play.new_game(0);
                ++games;
            }
            play.start_life();
            play.begin_play();
        }
    }
    std::printf("%ld frames, %d games; entered: vanish %d, bonus %d, lab %d, flight %d, round end %d; place %d, rounds %d\n",
                frames, games, counts[1], counts[2], counts[3], counts[4], counts[5], play.place(), play.rounds());
    return 0;
}

// front <content> <input.txt> <frames> <png dir> <frame,frame,...>: the
// game from the title as the module runs it, on the harness's input script
// (frame, port 2 bits pressed, port 1 bits pressed), saving pictures.
int front(int argc, char** argv) {
    const auto image = wizball::data::load_game_image(platform::utf8_path(argv[2]));
    const auto assets = wizball::data::decode_assets(image);
    // Lines: frame, port 2 bits, port 1 bits[, C64 key held (row * 8 + column, -1 none)].
    struct Change {
        long frame;
        int port2, port1, key;
    };
    std::vector<Change> script;
    {
        const auto text = platform::read_file(argv[3]);
        std::istringstream lines(std::string(text.begin(), text.end()));
        std::string line;
        while (std::getline(lines, line)) {
            std::istringstream in(line);
            Change ch{0, 0, 0, -1};
            if (in >> ch.frame >> ch.port2 >> ch.port1) {
                in >> ch.key;
                script.push_back(ch);
            }
        }
    }
    const long frames = std::atol(argv[4]);
    const std::string png_dir = argv[5];
    std::vector<long> wanted;
    {
        std::istringstream in(argv[6]);
        std::string item;
        while (std::getline(in, item, ',')) wanted.push_back(std::atol(item.c_str()));
    }
    wizball::game::GameRandom random(image);
    wizball::game::FrontEnd game(assets, image, random);
    if (std::getenv("WIZ_INVINCIBLE")) game.set_cheats({true, true, true});
    wizball::enhanced::Effects effects;
    const wizball::enhanced::Settings all_effects{true, true, true, true, true, true, true, true, true, true, true, true, true};
    auto shown_play = [&]() -> const Play* {
        const auto screen = game.screen();
        return screen == wizball::game::FrontEnd::Screen::play || screen == wizball::game::FrontEnd::Screen::game_over
                   ? game.play()
                   : nullptr;
    };
    std::size_t next = 0;
    wizball::game::Controls c;
    for (long f = 0; f < frames; ++f) {
        while (next < script.size() && script[next].frame <= f) {
            c.ports.port2 = std::uint8_t(0xFF & ~script[next].port2);
            c.ports.port1 = std::uint8_t(0xFF & ~script[next].port1);
            c.keys = {};
            if (script[next].key >= 0) c.keys[std::size_t(script[next].key)] = true;
            ++next;
        }
        game.tick(c);
        if (const char* give = std::getenv("WIZ_GIVE_POINTS"); give && game.play() && game.screen() == wizball::game::FrontEnd::Screen::play) {
            game.play()->give_points(std::atoi(give));
            unsetenv("WIZ_GIVE_POINTS");
        }
        if (std::getenv("WIZ_LOG_EXPLOSIONS") && game.play())
            for (int s = 0; s < 6; ++s) {
                const auto& o = game.play()->objects()[std::size_t(s)];
                if (o.active() && o.state != 0) std::printf("frame %ld: slot %d state %d frame %X anim %d\n", f, s, o.state, o.frame, o.anim_a);
            }
        if (std::getenv("WIZ_LOG_SHOTS") && game.play()) {
            int n = 0;
            for (const auto& e : game.play()->enemy_shots()) n += e.state == 1;
            if (n) std::printf("frame %ld: %d alien shots\n", f, n);
        }
        effects.update(shown_play(), all_effects);
        effects.update_front(game, all_effects);
        if (std::getenv("WIZ_LOG_STARS")) {
            const auto st = game.star_sprites();
            std::printf("frame %ld: %zu star sprites", f, st.sprites.size());
            for (std::size_t i = 0; i < std::min<std::size_t>(4, st.sprites.size()); ++i)
                std::printf(" (%d,%d,%d)", st.sprites[i][0], st.sprites[i][1], st.sprites[i][2]);
            int bits = 0;
            for (auto b : st.shape) bits += __builtin_popcount(b);
            std::printf(" shape bits %d\n", bits);
        }
        if (game.play()) { // the Wizlab's weapon
            static int last_weapon = 0;
            if (game.play()->chosen_weapon() != last_weapon)
                std::printf("frame %ld: weapon %d\n", f, last_weapon = game.play()->chosen_weapon());
        }
        if (game.play()) { // tunnels
            static int last_tunnel = 0;
            const int now = game.play()->wiz().tunnel() != 0 ? 10 : game.play()->arrival_phase();
            if (now != last_tunnel) std::printf("frame %ld: tunnel %d\n", f, now);
            last_tunnel = now;
        }
        if (game.play()) { // play stages as they change
            static int last_stage = -1;
            if (int(game.play()->stage()) != last_stage) {
                last_stage = int(game.play()->stage());
                std::printf("frame %ld: stage %d\n", f, last_stage);
            }
        }
        if (const char* every = std::getenv("WIZ_PAINT_EVERY"); every && game.play() && f > 320 && f % std::atol(every) == 0) {
            const int c = int(f / std::atol(every)) % 3; // a drop of paint into each cauldron in turn
            game.play()->give_paint(c, std::uint8_t(std::min(0x10, game.play()->colour()[std::size_t(c)] + 1)));
        }
        // WIZ_PAINT_AT=frame,frame,...: the place's colour complete then.
        if (const char* at = std::getenv("WIZ_PAINT_AT"); at && game.play()) {
            std::istringstream list(at);
            std::string item;
            while (std::getline(list, item, ','))
                if (f == std::atol(item.c_str()))
                    for (int c = 0; c < 3; ++c) game.play()->give_paint(c, 0x14);
        }
        // WIZ_PEARLS_AT=frame[:count]: pearls that move the token light on (6: to the smart bomb).
        if (const char* at = std::getenv("WIZ_PEARLS_AT"); at && game.play() && f == std::atol(at)) {
            const char* colon = std::strchr(at, ':');
            game.play()->give_pearls(std::uint8_t(colon ? std::atoi(colon + 1) : 6));
        }
        if (std::getenv("WIZ_LOG_CAT") && game.play()) {
            static int last = -1;
            const int now = game.play()->catellite_on() ? game.play()->catellite().hits() : -1;
            if (now != last) std::printf("frame %ld: catellite %d\n", f, now);
            last = now;
        }
        if (const char* every = std::getenv("WIZ_CAT_HIT_EVERY"); every && game.play() && f % std::atol(every) == 0)
            game.play()->hit_catellite();
        if (const char* at = std::getenv("WIZ_BLACKOUT_AT"); at && game.play() && f == std::atol(at))
            game.play()->blackout_for_test();
        if (std::getenv("WIZ_LOG_BLACKOUT") && effects.blackout_fade() > 0 && effects.blackout_fade() < 4)
            std::printf("frame %ld: blackout fade %d\n", f, effects.blackout_fade());
        if (const char* at = std::getenv("WIZ_TUNNEL_AT"); at && game.play() && f == std::atol(at))
            game.play()->tunnel_for_test();
        if (std::getenv("WIZ_CAT_OPEN") && game.play()) game.play()->set_catellite_protected(false);
        if (std::getenv("WIZ_DROPPERS") && game.play())
            for (int s = 0; s < 6; ++s) game.play()->make_dropper(s);
        if (std::getenv("WIZ_LOG_TEMPLATES") && game.play() && f % 500 == 0) {
            std::printf("frame %ld place %d templates' state:", f, game.play()->place());
            for (const auto& t : game.play()->templates()) std::printf(" %d", t.bytes[0x15]);
            std::printf("\n");
        }
        static int last_screen = -1;
        if (int(game.screen()) != last_screen) {
            last_screen = int(game.screen());
            std::printf("frame %ld: screen %d\n", f, last_screen);
        }
        if (std::getenv("WIZ_DUMP_RUNS") && f < std::atol(std::getenv("WIZ_DUMP_RUNS"))) {
            std::printf("%ld:", f);
            for (int a = 0xEBC1; a <= 0xEBF2; ++a) std::printf(" %X", game.colours().at(a));
            std::printf(" | front %d %d", game.colours().right_run().clock, game.colours().right_run().index);
            if (game.play()) {
                const auto run = game.play()->right_run();
                std::printf(" | play %d %d:", run.clock, run.index);
                for (auto c : run.colours) std::printf(" %X", c);
            }
            std::printf("\n");
        }
        if (std::find(wanted.begin(), wanted.end(), f) != wanted.end()) {
            char name[64];
            std::snprintf(name, sizeof name, "/front-%ld.png", f);
            // WIZ_WIDE: side columns; WIZ_EFFECTS: all effects drawn.
            const bool fx = std::getenv("WIZ_EFFECTS") != nullptr;
            const auto display = game.picture(std::getenv("WIZ_WIDE") ? std::atoi(std::getenv("WIZ_WIDE")) : 0,
                                              fx ? effects.blackout_fade() : 0);
            auto picture = display.render();
            if (fx && shown_play()) effects.draw(picture, display, *shown_play(), all_effects);
            if (fx) effects.draw_front(picture, display, game);
            platform::save_png(picture, platform::utf8_path(png_dir + name));
            std::printf("frame %ld: screen %d\n", f, int(game.screen()));
        }
    }
    return 0;
}

// scores-test <content>: the score tables with a few made-up games.
// run <content> <input.txt> <frames> <png dir> <frame,frame,...>: the game
// module itself (as the launcher runs it) on a front-style input script,
// with WIZ_OPTIONS=index=value,... and WIZ_CANVAS=width,height (widescreen).
int run_module(int, char** argv) {
    auto module = wizball::game::start(platform::utf8_path(argv[2]));
    struct Change {
        long frame;
        int port2, port1, key;
    };
    std::vector<Change> script;
    {
        const auto text = platform::read_file(argv[3]);
        std::istringstream lines(std::string(text.begin(), text.end()));
        std::string line;
        while (std::getline(lines, line)) {
            std::istringstream in(line);
            Change ch{0, 0, 0, -1};
            if (in >> ch.frame >> ch.port2 >> ch.port1) {
                in >> ch.key;
                script.push_back(ch);
            }
        }
    }
    if (const char* o = std::getenv("WIZ_OPTIONS")) {
        std::istringstream in(o);
        std::string item;
        while (std::getline(in, item, ',')) {
            const auto eq = item.find('=');
            module->set_option(std::size_t(std::atoi(item.substr(0, eq).c_str())), std::atoi(item.substr(eq + 1).c_str()));
        }
    }
    if (const char* c = std::getenv("WIZ_CANVAS")) {
        const std::string text = c;
        module->set_canvas(std::atoi(text.c_str()), std::atoi(text.substr(text.find(',') + 1).c_str()));
    }
    const long frames = std::atol(argv[4]);
    const std::string png_dir = argv[5];
    std::vector<long> wanted;
    {
        std::istringstream in(argv[6]);
        std::string item;
        while (std::getline(in, item, ',')) wanted.push_back(std::atol(item.c_str()));
    }
    std::size_t next = 0;
    app::GameInput in;
    for (long f = 0; f < frames; ++f) {
        while (next < script.size() && script[next].frame <= f) {
            const int p2 = script[next].port2, p1 = script[next].port1;
            in = {};
            in.up = p2 & 1, in.down = p2 & 2, in.left = p2 & 4, in.right = p2 & 8, in.fire = p2 & 16;
            in.up2 = p1 & 1, in.down2 = p1 & 2, in.left2 = p1 & 4, in.right2 = p1 & 8, in.fire2 = p1 & 16;
            if (script[next].key >= 0) in.c64_keys[std::size_t(script[next].key)] = true;
            ++next;
        }
        module->tick(in);
        if (std::find(wanted.begin(), wanted.end(), f) != wanted.end()) {
            char name[64];
            std::snprintf(name, sizeof name, "/run-%ld.png", f);
            platform::save_png(module->picture(), platform::utf8_path(png_dir + name));
        }
    }
    return 0;
}

// savetest <content> <frames to the save> <frames held after> : quick save
// and load through the module: one game saves at a frame, runs on (with the
// stick moving), loads, and must then show the same pictures and play the
// same sound as a game that went on from the save directly.
int save_test(int, char** argv) {
    const auto content = platform::utf8_path(argv[2]);
    const int at = std::atoi(argv[3]), after = std::atoi(argv[4]);
    // Start a game from the title, then fly right with fire now and then.
    auto input = [](int f) {
        app::GameInput in;
        if ((f >= 88 && f < 92) || (f >= 301 && f < 305)) in.fire = true;
        if (f > 320) {
            in.right = (f / 97) % 3 != 0;
            in.left = (f / 97) % 3 == 0;
            in.fire = f % 23 < 3;
        }
        return in;
    };
    auto a = wizball::game::start(content), b = wizball::game::start(content);
    std::vector<float> sa(1764), sb(1764);
    for (int f = 0; f < at; ++f) {
        a->tick(input(f));
        b->tick(input(f));
        a->audio(sa, 44100);
        b->audio(sb, 44100);
    }
    a->save();
    for (int f = at; f < at + 400; ++f) { // a wanders off
        a->tick(input(f + 1000));
        a->audio(sa, 44100);
    }
    if (!std::getenv("WIZ_NO_LOAD")) a->load(); // (the check that the check can fail)
    int differing = 0, sound_differing = 0;
    for (int k = 0; k < after; ++k) {
        a->tick(input(at + k));
        b->tick(input(at + k));
        a->audio(sa, 44100);
        b->audio(sb, 44100);
        const auto pa = a->picture(), pb = b->picture();
        bool same = pa.width == pb.width && pa.height == pb.height;
        for (std::size_t i = 0; same && i < pa.pixels.size(); ++i)
            same = pa.pixels[i].r == pb.pixels[i].r && pa.pixels[i].g == pb.pixels[i].g && pa.pixels[i].b == pb.pixels[i].b;
        if (!same) ++differing;
        if (sa != sb) ++sound_differing;
    }
    std::printf("after the load: %d of %d frames differ in the picture, %d in the sound\n", differing, after, sound_differing);
    return differing || sound_differing ? 1 : 0;
}

int scores_test(int, char** argv) {
    const auto image = wizball::data::load_game_image(platform::utf8_path(argv[2]));
    wizball::game::Scores scores(image);
    auto show = [&](const char* what) {
        std::printf("%s:", what);
        for (std::uint16_t a = 0xBFE8; a < 0xBFF2; ++a) std::printf("%c", scores.block_byte(a));
        std::printf("\n");
        for (int row = 0; row < 6; ++row) {
            std::printf("  ");
            for (int c = 0; c < 2; ++c) {
                for (int i = 0; i < 14; ++i) std::printf("%c", scores.block_byte(std::uint16_t(0xBEDF + 40 * row + 20 * c + i)));
                std::printf(" | ");
            }
            std::printf("\n");
        }
    };
    show("start");
    auto game = [&](bool one_table, std::array<std::uint8_t, 6> digits, const char* name) {
        wizball::game::Scores::Entry e{one_table, digits};
        const auto ranks = scores.enter(e, nullptr);
        std::array<std::uint8_t, 7> n{};
        for (int i = 0; i < 7; ++i) n[std::size_t(i)] = std::uint8_t(name[i]);
        std::printf("rank %d\n", ranks[0]);
        if (ranks[0]) scores.name(0x31, n);
        show(name);
    };
    game(true, {0, 3, 0, 0, 0, 0}, "AAA    ");
    game(true, {0, 9, 0, 0, 0, 0}, "BBB    ");
    game(false, {0, 2, 5, 0, 0, 0}, "CCC DDD");
    game(true, {0, 1, 0, 0, 0, 0}, "LOW    ");
    return 0;
}

int lockstep(int argc, char** argv) {
    const auto content = platform::utf8_path(argv[2]);
    const auto image = wizball::data::load_game_image(content);
    g_image = &image;
    const auto assets = wizball::data::decode_assets(image);
    const Ranges ranges(argv[5]);
    const auto ticks = read_ticks(argv[3], ranges);
    for (auto [lo, hi] : ranges.list) colour_ram_recorded = colour_ram_recorded || lo == 0xD800;
    std::vector<int> frames;
    {
        const auto raw = platform::read_file(argv[3]);
        for (std::size_t at = 0; at + 4 + ranges.size <= raw.size(); at += 4 + ranges.size)
            frames.push_back(int(raw[at] | raw[at + 1] << 8 | raw[at + 2] << 16 | raw[at + 3] << 24));
    }
    const auto events = read_pclog(argv[4]);
    const int max_passes = argc > 6 ? std::atoi(argv[6]) : 1000000;
    const std::string png_dir = argc > 7 ? argv[7] : "";
    const int png_from = std::getenv("WIZ_PNG_FROM") ? std::atoi(std::getenv("WIZ_PNG_FROM")) : 0;

    // Passes from the PC log: a pass runs from one loop top (the play loop
    // $640E, the place's end $64CF, the bonus wave $6576, the Wizlab $AB7B)
    // to the next; its events (interrupts and the markers the rebuilt game
    // calls back at) and random numbers in order.
    const auto loop_top = [](std::uint16_t pc) {
        return pc == 0x640E || pc == 0x64CF || pc == 0x6576 || pc == 0xAB7B || pc == 0xAE9C || pc == 0x6A94;
    };
    const auto interrupt = [](std::uint16_t pc) { return pc == 0xB90C || pc == 0xB8F3 || pc == 0xB8FC || pc == 0xB903; };
    std::size_t i = 0;
    auto find = [&](std::uint16_t pc) {
        while (i < events.size() && events[i].pc != pc) ++i;
        return i < events.size();
    };
    struct Stretch {
        std::uint16_t top{};
        std::vector<std::uint8_t> random;
        std::vector<std::uint16_t> events;
    };
    // From the event at i to the next loop top (or a level start).
    auto collect = [&](bool stop_at_top) {
        Stretch st;
        st.top = events[i].pc;
        ++i;
        while (i < events.size() && events[i].pc != 0x63CA && events[i].pc != 0x6392 &&
               !(stop_at_top && loop_top(events[i].pc)) && !(!stop_at_top && events[i].pc == 0x6893)) {
            const auto pc = events[i].pc;
            if (pc == 0x753F) st.random.push_back(events[i].a);
            else st.events.push_back(pc);
            ++i;
        }
        return st;
    };
    if (!find(0x6392)) throw std::runtime_error("no game start in the PC log");
    // Lives: a level start ($63CA) to the player screen ($6893), the set-up
    // ($63E0) to the first pass, then passes until the next level start.
    struct Life {
        Stretch level, setup;
        std::vector<Stretch> passes;
    };
    std::vector<Life> lives;
    while (find(0x63CA)) {
        Life life;
        life.level = collect(false);
        if (!find(0x63E0)) break;
        life.setup = collect(true);
        while (i < events.size() && loop_top(events[i].pc)) life.passes.push_back(collect(true));
        lives.push_back(std::move(life));
    }
    if (lives.empty()) throw std::runtime_error("no life in the PC log");

    ReplayRandom random;
    Sound sound(image, random);
    Play play(assets, image, random, sound);
    play.new_game(std::getenv("WIZ_MODE") ? std::atoi(std::getenv("WIZ_MODE")) : 0); // WIZ_MODE: the menu's mode (0-4)
    if (std::getenv("WIZ_INVINCIBLE")) play.set_invincible(true); // runs with the cheat poked in ($BB07)
    {
        Play::Clocks clocks{ticks[0][0xB88F], ticks[0][0xB890], ticks[0][0x770A], ticks[0][0x770B], ticks[0][0xEBB3],
                            ticks[0][0xEBB8], {}};
        for (int i = 0; i < 13; ++i) clocks.run[std::size_t(i)] = ticks[0][0xEBD7u + unsigned(i)];
        play.set_clocks(clocks);
    }
    int matched = 0, sound_resyncs = 0, tick = 0, count = 0;
    bool failed = false;
    // The interrupts recorded between the markers are replayed when the
    // rebuilt game reaches the same marker.
    const Stretch* current = nullptr;
    std::size_t cursor = 0;
    std::vector<std::string> marker_misses;
    int waits_seen = 0;
    auto run_interrupt = [&](std::uint16_t pc) {
        if (pc == 0xB90C) play.frame();
        else play.sound().extra_interrupt(pc);
    };
    play.set_marker_hook([&](std::uint16_t pc) {
        if (!current) return;
        std::size_t j = cursor;
        while (j < current->events.size() && current->events[j] != pc) ++j;
        if (j == current->events.size()) {
            char text[64];
            std::snprintf(text, sizeof text, "the rebuilt game passed $%04X, the original did not", pc);
            marker_misses.push_back(text);
            return;
        }
        for (; cursor < j; ++cursor)
            if (interrupt(current->events[cursor])) run_interrupt(current->events[cursor]);
        cursor = j + 1;
        if (pc == 0x6E92) {
            // A sequence frame shows the step before it with this frame's
            // interrupts (the steps run at line 266).
            if (!png_dir.empty() && waits_seen > 0 && tick >= png_from) {
                char name[64];
                std::snprintf(name, sizeof name, "/pass%04d-%02d.png", tick, waits_seen);
                platform::save_png(play.picture().render(), platform::utf8_path(png_dir + name));
            }
            ++waits_seen;
        }
    });
    auto finish_stretch = [&] {
        if (!current) return;
        for (; cursor < current->events.size(); ++cursor)
            if (interrupt(current->events[cursor])) run_interrupt(current->events[cursor]);
        current = nullptr;
    };
    for (std::size_t l = 0; l < lives.size() && !failed; ++l) {
        auto& life = lives[l];
        if (l > 0 && !play.end_life()) {
            std::printf("the rebuilt game ended after life %zu\n", l);
            break;
        }
        random.set(life.level.random);
        play.start_life();
        std::printf("life %zu: level start: %zu random numbers, rebuilt used %zu%s\n", l + 1, life.level.random.size(),
                    random.next_index, random.overrun ? " (ran out)" : "");
        random.set(life.setup.random);
        play.begin_play();
        if (l > 0) {
            // The player screen between lives (not rebuilt yet) runs the
            // title's colour run; take it over.
            const auto& m = ticks[std::size_t(tick)];
            Play::Clocks clocks{m[0xB88F], m[0xB890], m[0x770A], m[0x770B], m[0xEBB3], m[0xEBB8], {}};
            for (int i = 0; i < 13; ++i) clocks.run[std::size_t(i)] = m[0xEBD7u + unsigned(i)];
            play.set_clocks(clocks);
        }
        std::printf("life %zu: set-up: %zu random numbers, rebuilt used %zu%s; %zu passes\n", l + 1,
                    life.setup.random.size(), random.next_index, random.overrun ? " (ran out)" : "", life.passes.size());
        Compare c0;
        compare_state(play, ticks[std::size_t(tick)], c0);
        for (const auto& d : c0.diffs) std::printf("  before the life's first pass: %s\n", d.c_str());
        for (std::size_t k = 0; k < life.passes.size(); ++k, ++tick) {
            if (count >= max_passes || std::size_t(tick + 1) >= ticks.size()) break;
            // WIZ_SEARCH=pass: from that pass on, try the stick held right
            // (or left) for n passes in the rebuilt game and report which
            // runs take the wiz into a tunnel (to script recordings).
            if (const char* from = std::getenv("WIZ_SEARCH"); from && tick == std::atoi(from)) {
                play.set_marker_hook(nullptr);
                for (int dir : {8, 4})
                    for (int hold = 0; hold < 900; hold += 3) {
                        Play trial = play;
                        trial.set_invincible(true);
                        for (int p = 0; p < 4000; ++p) {
                            Ports in;
                            if (p < hold) in.port2 = std::uint8_t(0xFF & ~dir);
                            trial.pass(in);
                            trial.frame();
                            if (trial.wiz().tunnel() != 0) {
                                std::printf("%s for %d passes: tunnel at pass %d (place %d)\n", dir == 8 ? "right" : "left",
                                            hold, tick + p, trial.place());
                                break;
                            }
                        }
                    }
                return 0;
            }
            ++count;
            // The ports as the pass read them: $B256/$B257 hold the last
            // reading, at the end of a play or lab pass; the bonus wave reads
            // at its start ($697D), so the reading before it counts.
            const auto& port_tick = ticks[std::size_t(play.stage() == Play::Stage::bonus && !play.waiting() ? tick : tick + 1)];
            Ports ports;
            ports.port2 = port_tick[0xB256];
            ports.port1 = port_tick[0xB257];
            Compare c;
            // The life's last pass runs on into the life's end ($6E38 on),
            // which is checked by the next life's start instead.
            const bool last = k + 1 == life.passes.size() && l + 1 < lives.size();
            // Test runs may poke pearls into the original ($B259); take them over.
            if (std::getenv("WIZ_ADOPT_PEARLS") && ticks[std::size_t(tick)][0xB259] != play.pearls())
                play.give_pearls(ticks[std::size_t(tick)][0xB259]);

            const auto& pass = life.passes[k];
            // A side's last life ends in the game-over message ($69F2, loop
            // $6A94), also between lives when the other side plays on.
            if (pass.top == 0x6A94 && play.stage() != Play::Stage::game_over && play.life_over()) play.start_game_over();
            const auto stage = play.stage();
            const std::uint16_t expected_top = stage == Play::Stage::play     ? 0x640E
                                               : stage == Play::Stage::vanish ? 0x64CF
                                               : stage == Play::Stage::bonus  ? 0x6576
                                               : stage == Play::Stage::lab    ? 0xAB7B
                                               : stage == Play::Stage::game_over ? 0x6A94
                                                                              : 0xAE9C;
            if (pass.top != expected_top && !play.waiting()) {
                char text[96];
                std::snprintf(text, sizeof text, "the original runs the loop at $%04X, the rebuilt one $%04X", pass.top,
                              expected_top);
                c.diffs.push_back(text);
            }
            random.set(pass.random);
            current = &pass;
            cursor = 0;
            waits_seen = 0;
            marker_misses.clear();
            play.step(ports);
            int frames_in_pass = 1;
            while (play.waiting()) {
                play.step(ports);
                ++frames_in_pass;
            }
            if (frames_in_pass > 1) std::printf("  pass %d: %d frames (a sequence)\n", tick, frames_in_pass);
            // The pass a side's last life ends in runs on into the
            // game-over message's set-up ($69F2) before its loop ($6A94);
            // the set-up starts the tune at its very end ($6A91), after
            // the frames it takes.
            if (k + 1 < life.passes.size() && life.passes[k + 1].top == 0x6A94 && play.stage() != Play::Stage::game_over &&
                play.life_over()) {
                finish_stretch();
                play.start_game_over();
            }
            // Paint ($B12B-$B12D) poked late in pass WIZ_ADOPT_PAINT is taken over after it.
            if (const char* at = std::getenv("WIZ_ADOPT_PAINT")) {
                // "auto": all three counts jumping to 20 is the poke.
                const auto& next = ticks[std::size_t(tick + 1)];
                const auto& now = ticks[std::size_t(tick)];
                const bool poked = std::string(at) == "auto"
                                       ? next[0xB12B] == 0x14 && next[0xB12C] == 0x14 && next[0xB12D] == 0x14 &&
                                             (now[0xB12B] != 0x14 || now[0xB12C] != 0x14 || now[0xB12D] != 0x14)
                                       : tick == std::atoi(at);
                if (poked)
                    for (int p = 0; p < 3; ++p) play.give_paint(p, next[0xB12Bu + unsigned(p)]);
            }
            // Painted places ($B11F + place) poked during the pass, the same way.
            if (std::getenv("WIZ_ADOPT_PAINTED"))
                for (int p = 0; p < 8; ++p) {
                    const auto was = ticks[std::size_t(tick)][0xB11Fu + unsigned(p)];
                    const auto now = ticks[std::size_t(tick + 1)][0xB11Fu + unsigned(p)];
                    if (now == 3 && was != 3 && play.painted(p) != 3) play.give_painted(p, now);
                }
            if (!last) finish_stretch();
            current = nullptr;
            for (const auto& m : marker_misses) c.diffs.push_back(m);
            if (!last && (random.next_index != random.values.size() || random.overrun))
                c.diffs.push_back("random numbers: original " + std::to_string(random.values.size()) + ", rebuilt " +
                                  std::to_string(random.next_index));
            if (!last) compare_state(play, ticks[std::size_t(tick + 1)], c);
            else if (!play.life_over() && !play.game_over_done())
                c.diffs.push_back("the original's life ended here, the rebuilt one goes on");
            if (!png_dir.empty() && tick >= png_from) {
                char name[64];
                std::snprintf(name, sizeof name, "/pass%04d.png", tick);
                platform::save_png(play.picture().render(), platform::utf8_path(png_dir + name));
            }
            // Where in a pass the frame's sound interrupt falls depends on the
            // CPU's timing; a difference only in the voice slots is noted and
            // the slots taken over from the original.
            // So does an interrupt that lands inside a tune's start ($45F1):
            // a voice set up after it misses that tick (a duration one off).
            bool sound_only = !c.diffs.empty();
            for (const auto& d : c.diffs)
                if (d.rfind("sound slot", 0) != 0 &&
                    !(d.rfind("tune voice", 0) == 0 && d.find(" duration:") != std::string::npos))
                    sound_only = false;
            if (sound_only) {
                if (std::getenv("WIZ_SOUND_DIFFS") && sound_resyncs < 12)
                    for (const auto& d : c.diffs) std::printf("  pass %d (timing): %s\n", tick, d.c_str());
                ++sound_resyncs;
                const auto& m = ticks[std::size_t(tick + 1)];
                for (int s = 0; s < 3; ++s) play.sound().set_slot(s, &m[0x0400 + 0x20u * unsigned(s)]);
                for (int v = 0; v < 3; ++v) play.sound().music().set_duration(v, m[0x0Au + unsigned(v)]);
                c.diffs.clear();
            }
            if (!c.diffs.empty() && std::getenv("WIZ_DUMP_SCREENS")) {
                const auto pr = play.playfield().probe();
                std::printf("  rebuilt: shown %d, roles %d %d %d, progress %d %d %d\n", play.playfield().shown_screen(), pr.role[0],
                            pr.role[1], pr.role[2], pr.progress[0], pr.progress[1], pr.progress[2]);
                for (int sc = 0; sc < 3; ++sc) {
                    std::printf("  screen %d:", sc);
                    for (int col = 0; col < 40; col += 4) std::printf(" %02X", play.playfield().code_on(sc, col, 15));
                    std::printf(" |");
                    for (int col = 0; col < 40; col += 4) std::printf(" %02X", play.playfield().code_on(sc, col, 19));
                    std::printf("\n");
                }
            }
            if (!c.diffs.empty()) {
                std::printf("pass %d (life %zu, pass %zu, loop $%04X) differs:\n", tick, l + 1, k, pass.top);
                for (std::size_t j = 0; j < c.diffs.size() && j < 40; ++j) std::printf("  %s\n", c.diffs[j].c_str());
                failed = true;
                break;
            }
            ++matched;
        }
    }
    std::printf("%d of %d passes match (%d sound interrupt timing differences taken over)\n", matched, count, sound_resyncs);
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string cmd = argc > 1 ? argv[1] : "";
        if (cmd == "ramframe" && argc >= 7) return ramframe(argc, argv);
        if (cmd == "lockstep" && argc >= 6) return lockstep(argc, argv);
        if (cmd == "soak" && argc >= 4) return soak(argc, argv);
        if (cmd == "front" && argc >= 7) return front(argc, argv);
        if (cmd == "scores-test" && argc >= 3) return scores_test(argc, argv);
        if (cmd == "savetest" && argc >= 5) return save_test(argc, argv);
        if (cmd == "run" && argc >= 7) return run_module(argc, argv);
        std::fprintf(stderr, "usage: wizball_replay ramframe <ram> <colour> <vic> <writes> <out.png> [cycle]\n");
        return 2;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "wizball_replay: %s\n", e.what());
        return 1;
    }
}
