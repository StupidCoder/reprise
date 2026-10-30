#include "beach_volley_amiga/front/venue_card.hpp"
#include "platform/files.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace beach_volley::front {
namespace {

using Op = VenueCard::Op;
using Kind = Op::Kind;
using Palette = std::array<std::uint16_t, 16>;

constexpr int row_pixels = 400, rows = 200; // the screen: 25 words a row
constexpr int row_bytes = 50, plane_bytes = 10000;
constexpr int strip_row = 130;              // the strip at the bottom: rows 130-199
constexpr std::uint32_t strip_at = 0x1F40;  // $4BE60 in the anim file (loaded at $49F20)

// Blanks the program's CPU loops and waits take (measured on the original;
// game.md "Venue card").
// What the drawing takes, per venue, measured on the original
// (tools/card_timing.py): the map's fades (a wait before the first step,
// then each step's: a step's blits about fill a frame, and with the music's
// load one takes one blank or two), the tiles, the picture, and $3CB72's
// pauses (157 or 158 blanks).
struct Measured {
    int venue;
    std::uint8_t fade_in[17], fade_out[17];
    int tiles, picture;
    int pauses[2];
    int scroll_first[2]; // the scrolls' first steps (left, right): 0 when the wait before has just ended
};
constexpr Measured measured[8] = {
    {1, {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 1}, {0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1}, 1, 1, {158}, {1, 0}},
    {2, {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 1, 1, {159}, {1, 0}},
    {3, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 2, 1, {158}, {1, 0}},
    {4, {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 1, 1, {158}, {1, 0}},
    {5, {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 1, 1, {158}, {1, 0}},
    {6, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 1, 1, {159}, {1, 0}},
    {7, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 1, 1, {158, 159}, {0, 0}},
    {8, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, 1, 1, {}, {1, 0}},
};
constexpr int dot_blanks = 13;        // $3BFC8's delay and the wait
constexpr int fade_blanks = 45;       // $3C836: 121 waits for line $C8
constexpr int voice_blanks = 206;     // $3BF98
constexpr int stop_blanks = 0;        // $3BF8A
constexpr int sweep_short = 4, sweep_long = 8; // venue 8: two delays ($3E72E, $3E7A2: 5,001 and 9,001 x 11 loops), 12 blanks measured

std::vector<std::uint8_t> read_installed(const std::filesystem::path& content, const std::string& name) {
    try {
        return platform::read_file(content / name);
    } catch (const std::exception&) {
        throw std::runtime_error("The imported file " + name + " is missing; import the disk again.");
    }
}

// Builds the card's drawing as the program does it. Addresses name the
// program's tables in animation.code and its routines.
class Script {
public:
    Script(const CardFiles& files, std::vector<Op>& tape, std::vector<Palette>& palettes)
        : files_(files), tape_(tape), palettes_(palettes) {}

    // A picture in the anim file: its offset there and size.
    struct Sprite {
        int x{}, y{}, w{}, h{};
        std::uint32_t at{};
    };

    void run(int venue);

private:
    int word(std::uint32_t address) const { return std::int16_t(files_.code.u16(address)); }
    // A table the scenes step through, ended by $FF (or not: some are read
    // on past their end, as the program does).
    int step(std::uint32_t table, int& at, bool ended = true) {
        if (ended && word(table + std::uint32_t(at)) == 0xFF) at = 0;
        const int v = word(table + std::uint32_t(at));
        at += 2;
        return v;
    }
    void push(Kind k, int a = 0, int b = 0, int c = 0, int d = 0, int e = 0, std::uint32_t at = 0) {
        tape_.push_back({k, a, b, c, d, e, at});
    }
    void wait(int blanks) {
        if (blanks > 0) push(Kind::wait, blanks);
    }
    void frame() { wait(1); } // $3BFBA: the wait for line $FA
    void strip() { push(Kind::strip); }
    void draw(const Sprite& s, bool clip = true) { push(Kind::sprite, s.x, s.y, s.w, s.h, clip ? 1 : 0, s.at); }
    Palette palette(std::uint32_t address) const {
        Palette p{};
        for (std::uint32_t i = 0; i < 16; ++i) p[i] = files_.code.u16(address + i * 2);
        return p;
    }
    int keep(const Palette& p) {
        palettes_.push_back(p);
        return int(palettes_.size()) - 1;
    }
    // $3C81A: the strip's own palette from the copper's split at row 130.
    void strip_palette(std::uint32_t address) { push(Kind::lower, keep(palette(address)), strip_row); }

    // $3BFDA (animated, a dot each frame) and $3C00C (into the map at once).
    void route(std::uint32_t at, bool animated) {
        for (; files_.code.u16(at) != 0xFFFF; at += 4) {
            push(Kind::dot, word(at), word(at + 2), animated ? 0 : 1);
            if (animated) wait(dot_blanks);
        }
    }
    // $3C034: 16 steps, each showing one more pixel column of every 16.
    void dissolve_in() {
        const auto& steps = measured[files_.venue - 1].fade_in;
        wait(steps[0]);
        for (int mask = 0, k = 0; k < 16; ++k) {
            mask = mask << 1 | 1;
            push(Kind::dissolve, mask);
            wait(steps[k + 1]);
        }
    }
    // $3C0CA: the map again, one column fewer each step, to black.
    void dissolve_out() {
        const auto& steps = measured[files_.venue - 1].fade_out;
        wait(steps[0]);
        for (int mask = 0xFFFF, k = 0; k < 16; ++k) {
            mask >>= 1;
            push(Kind::dissolve, mask);
            wait(steps[k + 1]);
        }
    }
    // $3CB72: a CPU loop.
    void pause() { wait(measured[files_.venue - 1].pauses[pauses_++]); }
    // $3C162 / $3C1B6: the screen scrolls a pixel a frame (the fine scroll
    // wrapping into the bitplane pointers every 16), `steps + 1` frames.
    // A step's wait for line $FA passes at once when the last wait has just
    // ended (after the route's last dot): `first` is the first step's.
    void scroll(int steps, int direction, int first) {
        for (int k = 0; k <= steps; ++k) {
            if (direction < 0) {
                fine_ -= 0x11;
                if (fine_ < 0) {
                    fine_ = 0xFF;
                    pointer_ += 2;
                }
            } else {
                fine_ += 0x11;
                if (fine_ >= 0x110) {
                    fine_ = 0;
                    pointer_ -= 2;
                }
            }
            push(Kind::scroll, pointer_, fine_ & 15);
            wait(k == 0 ? first : 1);
        }
    }
    // $3ED2C: tiles of 16 x 16 from `tile`, at the places in the list for
    // `pattern` (each a move from the last: words across, 16-row steps).
    void tiles(int pattern, std::uint32_t tile) {
        std::uint32_t list = files_.code.u32(0x3EDE0 + std::uint32_t(pattern) * 4);
        int at = 2; // $32002
        for (;; list += 2) {
            const auto across = files_.code.at(list, 2);
            if (across[0] == 0xFF) return;
            at += across[0] * 2 + across[1] * 800;
            push(Kind::tile, at, 0, 0, 0, 0, tile);
        }
    }
    // The ending of every scene: the map fades out, the frame of tiles and
    // the picture, the picture's palette fading in; then fire, the voice.
    void picture(int pattern, std::uint32_t tile, const Sprite& p, std::uint32_t palette_at, int voice) {
        push(Kind::lower, keep({}), -1); // $3C46E: the split below the screen
        dissolve_out();
        push(Kind::upper, keep(Palette{0x004, 0x004, 0x004, 0x004, 0x004, 0x004, 0x004, 0x004, 0x004, 0x004, 0x004,
                                       0x004, 0x004, 0x004, 0x004, 0x004})); // $3F61A
        tiles(pattern, tile);
        wait(measured[files_.venue - 1].tiles);
        draw(p, false);
        wait(measured[files_.venue - 1].picture);
        // $3C836, $3F676: from the target's every gun less 15 up to it.
        const Palette target = palette(palette_at);
        for (int k = 0; k < 16; ++k) {
            Palette c{};
            for (std::size_t i = 0; i < 16; ++i) {
                for (int shift = 0; shift < 12; shift += 4) {
                    const int gun = std::max(0, (target[i] >> shift & 15) - (15 - k));
                    c[i] = std::uint16_t(c[i] | gun << shift);
                }
            }
            push(Kind::upper, keep(c));
            frame();
        }
        wait(fade_blanks - 16);
        voice_ = voice;
    }

    void london();
    void new_york();
    void bahamas();
    void cairo();
    void sydney();
    void tokyo();
    void moscow();
    void paris();

    const CardFiles& files_;
    std::vector<Op>& tape_;
    std::vector<Palette>& palettes_;
    int pointer_{}, fine_{};
    int voice_{};
    int pauses_{};
};

// The routes between the venues ($3C6D6...), one per venue after the first.
constexpr std::uint32_t routes[7] = {0x3C6D6, 0x3C706, 0x3C712, 0x3C742, 0x3C77E, 0x3C79E, 0x3C7CA};

void Script::run(int venue) {
    // $3BCB0: the routes flown so far into the map, the map fades in, then
    // the next route and the venue's scene.
    for (int k = 0; k + 2 < venue; ++k) route(routes[k], false);
    dissolve_in();
    if (venue == 5 || venue == 6 || venue == 7) scroll(venue == 5 ? 50 : 60, -1, measured[venue - 1].scroll_first[0]);
    if (venue >= 2) route(routes[venue - 2], true);
    if (venue == 5 || venue == 6 || venue == 7) scroll(venue == 5 ? 50 : 60, +1, measured[venue - 1].scroll_first[1]);
    switch (venue) {
    case 1: london(); break;
    case 2: new_york(); break;
    case 3: bahamas(); break;
    case 4: cairo(); break;
    case 5: sydney(); break;
    case 6: tokyo(); break;
    case 7: moscow(); break;
    default: paris(); break;
    }
    // $3BF4A: fire; the song stops, the voice, a pause.
    push(Kind::wait_fire);
    push(Kind::stop_music);
    wait(stop_blanks);
    push(Kind::voice, voice_);
    wait(voice_blanks);
    push(Kind::end);
}

// London ($3C862): no scene, the pause and the picture.
void Script::london() {
    pause();
    picture(5, strip_at, {0x71, 0x21, 0x70, 0x8E, 0}, 0x3C5AE, 0xF71);
}

// New York ($3C8DC): a plane crosses the strip from the right, rising and
// falling by the table at $3CB84; a second one follows and comes down by
// the Statue of Liberty.
void Script::new_york() {
    const std::uint32_t s = strip_at + 0x2BC0;
    Sprite plane{0x150, 0x96, 0x20, 0x1A, s};
    const Sprite front{0x1F, 0x9F, 0x40, 0x29, s + 0x1A0};      // drawn over the second plane
    const Sprite behind{0x41, 0xAB, 0x20, 0x1E, s + 0x1A0 + 0x520}; // drawn under it
    Sprite trail{0, 0, 0x20, 0x0B, s + 0x1A0 + 0x520 + 0x1E0};
    int x = 0x150, y = 0x82, x2 = 0x150, y2 = 0x82, bob = 0;
    strip_palette(0x3C4CE);
    strip();
    do {
        frame();
        strip();
        plane.x = x;
        plane.y = y;
        draw(plane);
        if (x2 >= 0x82) {
            plane.x = x2;
            plane.y = y2;
            draw(plane);
        }
        if (x < 0xC8) --x2;
        if (x >= -16) --x;
        if (x2 < 0x82) {
            draw(behind);
            plane.x = x2;
            plane.y = y2;
            draw(plane);
            draw(front);
            trail.x = x2 + 10;
            trail.y = y2 - 15;
            if (x2 >= 0x6E) draw(trail);
        }
        const int d = step(0x3CB84, bob);
        y += d;
        y2 += d;
    } while (x2 >= 0x25);
    strip();
    pause();
    picture(0, strip_at + 0x3510, {0x82, 0x1E, 0x70, 0x8E, 0}, 0x3C5CE, 0xFB1);
}

// Bahamas ($3CED4): windsurfers cross the bay.
void Script::bahamas() {
    const std::uint32_t s = strip_at + 0x2BC0;
    Sprite a{0x150, 0x96, 0x20, 0x21, s};
    Sprite b{0x20, 0x9E, 0x50, 0x1C, s + 0x210};
    Sprite c{0x41, 0xA9, 0x20, 0x15, s + 0x210 + 0x460};
    int x = 0x150, y = 0x82, x2 = 0x150, y2 = 0xBE, bx = 0xBE, by = 0xC8;
    strip_palette(0x3C4EE);
    strip();
    int count = -40, table = 0;
    do {
        frame();
        strip();
        a.x = x;
        a.y = y;
        draw(a);
        if (x < 0xC8) {
            a.x = x2;
            a.y = y2;
            draw(a);
            --x2;
        }
        if (++count == 6) {
            count = 0;
            ++y;
            --y2;
        }
        --x;
    } while (x >= 0x64);
    do {
        frame();
        strip();
        a.x = x;
        a.y = y;
        draw(a);
        b.x = bx;
        b.y = by;
        draw(b);
        c.x = bx - 0x1E;
        c.y = by + 2;
        draw(c);
        a.x = x2;
        a.y = y2;
        draw(a);
        --x2;
        if (by < 0xB9) --y2;
        --by;
    } while (by >= 0xAF);
    do {
        frame();
        strip();
        a.x = x;
        a.y = y;
        draw(a);
        b.x = bx;
        b.y = by;
        draw(b);
        c.x = bx - 0x1E;
        c.y = by + 2;
        draw(c);
        a.x = b.x + 0x14;
        a.y = b.y - 0x1A;
        draw(a);
        by += step(0x3D298, table);
        if (bx >= 0x13B) ++by;
        ++bx;
    } while (bx < 0x14F);
    pause();
    picture(2, 0x52C0, {0x6E, 0x42, 0xA0, 0x64, 0}, 0x3C5EE, 0xF21);
}

// Cairo ($3D30C): camel riders over the dunes, the dunes' front drawn last.
void Script::cairo() {
    const std::uint32_t s = strip_at + 0x2BC0;
    const Sprite back{0x10, 0x95, 0x140, 0x21, s};
    Sprite a{0x150, 0x64, 0x30, 0x17, s + 0x14A0};
    Sprite b{0x41, 0xA9, 0x10, 0x1D, s + 0x14A0 + 0x228};
    Sprite c{0x41, 0xA9, 0x20, 0x19, s + 0x14A0 + 0x228 + 0xE8};
    int x = 0x150, y = 0x9F, x2 = 0x150, y2 = 0x8C, y3 = 0x8C;
    strip_palette(0x3C50E);
    strip();
    int t1 = 0, t2 = 8, hold = 0x14, t3 = 0;
    do {
        frame();
        strip();
        a.x = x;
        a.y = y;
        draw(a);
        if (x >= 0xBE) --x;
        else if (--hold < 0) x -= 2;
        x2 = a.x + 0xF;
        b.x = x2;
        y2 += step(x >= 0xC3 ? 0x3D65E : 0x3D67E, t1);
        b.y = y2;
        draw(b);
        if (x >= 0xAF) {
            b.x = x2 + 0xC;
            y3 += step(0x3D65E, t2);
            b.y = y3;
            draw(b);
        }
        if (x < 0xA0) {
            c.x = a.x + 0x30;
            c.y = a.y - 8 + step(0x3D64A, t3);
            draw(c);
        }
        draw(back);
    } while (x >= -70);
    pause();
    picture(3, 0x6440, {0x70, 0x22, 0x70, 0x8E, 0}, 0x3C60E, 0xFB1);
}

// Sydney ($3D69E): a boat pulls a water-skier across the harbour.
void Script::sydney() {
    const std::uint32_t s = strip_at + 0x2BC0;
    Sprite a{0x10, 0x95, 0x60, 0x24, s};
    Sprite b{0x150, 0x64, 0x50, 0x23, s + 0x6C0};
    Sprite c{0x41, 0xA9, 0x50, 0x0B, s + 0x6C0 + 0x578};
    Sprite d{0x41, 0xA9, 0x20, 0x10, s + 0x6C0 + 0x578 + 0x1B8};
    Sprite e{0x41, 0xA9, 0x20, 0x3C, s + 0x6C0 + 0x578 + 0x1B8 + 0x100};
    int ax = -80, ay = 0x96, bx = -60, by = 0x93, cx = -60, cy = 0xA0, dx = 0x138, dy = 0xB8, ex = 0x138,
        ey = 0x82;
    strip_palette(0x3C52E);
    do {
        frame();
        strip();
        c.x = cx;
        c.y = cy;
        draw(c);
        if (ax < 0x14A) {
            a.x = ax;
            a.y = ay;
            draw(a);
        }
        b.x = bx;
        b.y = by;
        draw(b);
        if (ax >= 8) ++cx;
        if (cx >= 1) ++bx;
        ++ax;
    } while (bx < 0x118);
    int hold = 0x14;
    do {
        frame();
        strip();
        e.x = ex;
        e.y = ey;
        draw(e);
        d.x = dx;
        d.y = dy;
        draw(d);
        if (--hold < 0) ++ey;
    } while (ey < 0xC8);
    pause();
    picture(5, strip_at + 0x3E70, {0x60, 0x20, 0xA0, 0x64, 0}, 0x3C62E, 0xF91);
}

// Tokyo ($3D978): a rider crosses the bridge.
void Script::tokyo() {
    const std::uint32_t s = strip_at + 0x2BC0;
    Sprite a{0x10, 0x95, 0x30, 0x1C, s};
    Sprite c{0x41, 0xA9, 0x20, 0x1F, s + 0x2A0 + 0x2D0};
    int ax = 0x15C, ay = 0xA0, cx = 0x14A, cy = 0xAA;
    strip_palette(0x3C54E);
    int table = 0, beat = 0;
    bool second = false, moved = word(0x3DD06) != 0;
    do {
        frame();
        strip();
        if (++beat >= 6) {
            beat = 0;
            second = !second;
            moved = false;
        }
        if (second) {
            c.at = strip_at + 0x3130;
            c.h = 0x1F;
            a.at = strip_at + 0x2BC0;
            a.h = 0x1C;
            if (!moved) {
                moved = true;
                cx += 7;
                --cy;
                --ax;
            }
        } else {
            c.at = strip_at + 0x3320;
            c.h = 0x1E;
            a.at = strip_at + 0x2E60;
            a.h = 0x1E;
            if (!moved) {
                moved = true;
                cx -= 7;
                ++cy;
                ++ax;
            }
        }
        c.x = cx;
        c.y = cy;
        draw(c);
        a.x = ax;
        a.y = cy + 0xD;
        draw(a);
        cy += step(0x3DD0A, table, false); // read on past the table, as the program does
        --ax;
        --cx;
    } while (cx >= -5);
    ax = a.x;
    ay = a.y;
    table = 0x34;
    do {
        frame();
        strip();
        a.x = ax;
        a.y = ay;
        draw(a);
        ay -= std::int16_t(step(0x3DD0A, table, false) * 2);
        ax += 2;
    } while (ax < 0x154);
    pause();
    picture(4, strip_at + 0x3500, {0x80, 0x21, 0x70, 0x8E, 0}, 0x3C64E, 0xFA1);
}

// Moscow ($3DF9E): skiers come down the mountain.
void Script::moscow() {
    const std::uint32_t s = strip_at + 0x2BC0;
    Sprite a{0x10, 0x95, 0x20, 0x1D, s};
    Sprite b{0x150, 0x64, 0x20, 0x1F, s + 0x3E0};
    const Sprite c{0x41, 0xA9, 0x40, 0x32, s + 0x3E0 + 0x5D0};
    int ax = 0x14E, ay = 0x78, bx = 0x14A, by = 0x82;
    strip_palette(0x3C56E);
    strip();
    pause();
    bool resting = false;
    int slow = 0, walk = 0, frame_of = 0, table = 0, sink = 0;
    do {
        frame();
        strip();
        if (!resting) {
            a.at = strip_at + 0x2D90;
            a.w = 0x20;
            a.h = 0x21;
        } else {
            a.at = strip_at + 0x2BC0;
            a.w = 0x20;
            a.h = 0x1D;
        }
        if (ax == 0xE1) resting = true;
        if (ax == 0xB4) resting = false;
        if (!resting && ++slow >= 2) {
            slow = 0;
            ++ay;
            --ax;
        }
        a.x = ax;
        a.y = ay;
        draw(a);
        if (ax < 0xC8 && bx >= 0x122) {
            b.at = strip_at + 0x2D90;
            b.x = bx;
            b.y = by;
            b.w = 0x20;
            b.h = 0x21;
            draw(b);
            --bx;
            if (++sink >= 3) {
                sink = 0;
                ++by;
            }
        } else if (bx < 0x122) {
            if (++walk >= 4) {
                walk = 0;
                if (++frame_of == 4) frame_of = 1;
            }
            if (frame_of == 1) b.at = strip_at + 0x2FA0, b.h = 0x1F;
            if (frame_of == 2) b.at = strip_at + 0x3190, b.h = 0x1F;
            if (frame_of == 3) b.at = strip_at + 0x3380, b.h = 0x1F;
            b.x = bx;
            b.y = by;
            draw(b);
            by += step(0x3E374, table, false);
            bx -= 2;
        }
        Sprite fixed = c;
        fixed.x = 0x10A;
        fixed.y = 0x83;
        draw(fixed);
        --ax;
    } while (ax >= -20);
    pause();
    picture(5, strip_at + 0x3BB0, {0x86, 0x1E, 0x70, 0x8E, 0}, 0x3C66E, 0xF31);
}

// Paris ($3E430): balloons drift in; one lands on the Eiffel Tower.
void Script::paris() {
    Sprite a{0, 0, 0x20, 0x22, strip_at + 0x2BC0};
    Sprite b{0, 0, 0x20, 0x15, strip_at + 0x3580};
    Sprite flag{0x39, 0x97, 0x10, 0x07, strip_at + 0x36D0};
    int x = 0x15E, y = 0x4B, x2 = 0x15E, y2 = 0x4B;
    strip_palette(0x3C58E);
    strip();
    int t1 = 0, t2 = 0, beat = 0, t3 = 0;
    bool wave = false, landed = false;
    const auto pair = [&](int px, int py, int bdx, int bdy) {
        a.x = px;
        a.y = py;
        b.x = px + bdx;
        b.y = py + bdy;
        draw(a);
        draw(b);
    };
    do {
        frame();
        strip();
        if (++beat >= 9) {
            beat = 0;
            wave = !wave;
        }
        if (wave) {
            flag.at = strip_at + 0x36D0;
            flag.x = 0x3A;
        } else {
            flag.at = strip_at + 0x3708;
            flag.x = 0x39;
        }
        flag.y = 0x97;
        draw(flag);
        if (x >= -20) {
            pair(x, y, 6, 0x22);
            y += step(0x3E908, t1);
        }
        if (x2 >= 0x37) {
            if (x < 0xC8) {
                pair(x2, y2, 6, 0x22);
                --x2;
                y2 += step(0x3E908, t2);
            }
        } else if (!landed) {
            a.w = 0x30;
            a.h = 0x2C;
            a.at = strip_at + 0x2DE0;
            a.x = 0x1E;
            a.y = 0x87;
            draw(a);
            b.x = a.x + 0x1C;
            b.y = a.y + 0x2C;
            draw(b);
            wait(sweep_short);
            strip();
            a.w = 0x20;
            a.h = 0x38;
            a.at = strip_at + 0x3200;
            a.x = 0x27;
            a.y = 0x7B;
            draw(a);
            b.x = a.x + 0x13;
            b.y = a.y + 0x38;
            draw(b);
            wait(sweep_long);
            landed = true;
        } else {
            if (word(0x3E8D0 + std::uint32_t(t3)) == 0xFF) t3 = 0;
            a.x += std::int16_t(word(0x3E8D0 + std::uint32_t(t3)) * 5);
            a.y += std::int16_t(word(0x3E8D0 + std::uint32_t(t3) + 2) * 4);
            t3 += 4;
            draw(a);
            b.x = a.x + 0x13;
            b.y = a.y + 0x38;
            draw(b);
        }
        --x;
    } while (x >= -250);
    picture(5, strip_at + 0x3740, {0x86, 0x1E, 0x70, 0x8E, 0}, 0x3C68E, 0xFA0);
}

// Planar pictures to colour indices: `planes` planes of `w` x `h` one after
// the other at `at` (`row_bytes` a row, `plane_bytes` apart).
int pixel(const std::vector<std::uint8_t>& bytes, std::uint32_t at, int row_bytes, int plane_bytes, int x, int y) {
    int v = 0;
    for (int p = 0; p < 4; ++p) {
        const std::size_t i = std::size_t(at) + std::size_t(p * plane_bytes + y * row_bytes + (x >> 3));
        if (i < bytes.size()) v |= (bytes[i] >> (7 - (x & 7)) & 1) << p;
    }
    return v;
}

} // namespace

CardFiles load_card_files(const std::filesystem::path& content, int venue) {
    if (venue < 1 || venue > 8) throw std::runtime_error("There is no such venue card.");
    CardFiles f{venue, data::CodeFile(read_installed(content, "animation.code"), data::animation_base), {}, {}, {}, {},
                read_installed(content, "instruments")};
    // $3F716's loads: the file names follow the code ("df0:anim0.cru"...),
    // per venue the picture file, the song and the voice.
    const auto name_at = [&](std::uint32_t at) {
        std::string name;
        for (auto c = f.code.at(at, 1)[0]; c != 0; c = f.code.at(++at, 1)[0]) name.push_back(char(c));
        if (name.rfind("df0:", 0) != 0) throw std::runtime_error("animation.code's file names are not as expected.");
        name = name.substr(4);
        if (name.ends_with(".cru")) name.resize(name.size() - 4);
        return name;
    };
    constexpr std::uint32_t names[8][3] = {
        {0x3FBCA, 0x3FC4A, 0x3FCC6}, {0x3FBD8, 0x3FC59, 0x3FCCF}, {0x3FBE6, 0x3FC68, 0x3FCD8}, {0x3FBF4, 0x3FC78, 0x3FCE1},
        {0x3FC02, 0x3FC88, 0x3FCEA}, {0x3FC10, 0x3FC98, 0x3FCF3}, {0x3FC1E, 0x3FCA8, 0x3FCFC}, {0x3FC2C, 0x3FCB7, 0x3FD05}};
    f.map = read_installed(content, name_at(0x3FBBC));
    f.anim = read_installed(content, name_at(names[venue - 1][0]));
    f.song = sound::Song::parse(read_installed(content, name_at(names[venue - 1][1])));
    f.voice = read_installed(content, name_at(names[venue - 1][2]));
    if (f.map.size() < 40000) throw std::runtime_error("The map (carte) is cut short.");
    return f;
}

VenueCard::VenueCard(const CardFiles& files, amiga::PaulaChannels& paula)
    : files_(&files), paula_(&paula),
      player_(sound::read_player_tables(files.code, 0x40150, 0x4018C, files.instruments.size()), 0x7, 0x7, true),
      screen_(std::size_t(plane_bytes * 4 + 16)), map_(files.map.begin(), files.map.begin() + plane_bytes * 4) {
    // The copper list's colours as loaded ($3C430...).
    for (std::uint32_t i = 0; i < 16; ++i) upper_[i] = files.code.u16(0x3C430 + i * 4);
    bind(files);
}

void VenueCard::begin() {
    player_.start(files_->song, *paula_); // $3BC9A, then the music on
    music_on_ = true;
}

void VenueCard::bind(const CardFiles& files) {
    files_ = &files;
    tape_.clear();
    palettes_.clear();
    Script(files, tape_, palettes_).run(files.venue);
    memory_.clear();
    for (const auto b : files.instruments) memory_.push_back(std::int8_t(b));
    for (const auto b : files.voice) memory_.push_back(std::int8_t(b));
    if (memory_.size() >= files.instruments.size() + 2) { // $3C7E2: the voice's first word cleared
        memory_[files.instruments.size()] = 0;
        memory_[files.instruments.size() + 1] = 0;
    }
    paula_->set_memory(memory_);
}

void VenueCard::tick(bool fire) {
    if (music_on_) player_.tick(*paula_); // the card's level-3 handler ($3C2EA)
    if (finished_) return;
    if (wait_ > 0) {
        --wait_;
        return;
    }
    while (at_ < int(tape_.size())) {
        const Op& op = tape_[std::size_t(at_)];
        if (op.kind == Kind::wait) {
            ++at_;
            wait_ = op.a - 1;
            return;
        }
        if (op.kind == Kind::wait_fire) {
            if (!fire) return;
            ++at_;
            continue;
        }
        apply(op);
        ++at_;
        if (finished_) return;
    }
}

void VenueCard::apply(const Op& op) {
    // A pixel at `linear` = row * 400 + x, written plane by plane: beyond a
    // plane's end it lands in the next plane's start, before the screen's
    // start nowhere.
    const auto put = [&](std::vector<std::uint8_t>& to, long linear, int v) {
        if (linear < 0) return;
        for (int p = 0; p < 4; ++p) {
            const std::size_t at = std::size_t(p * plane_bytes + (linear >> 3));
            if (at >= to.size()) return;
            const auto bit = std::uint8_t(0x80 >> (linear & 7));
            to[at] = std::uint8_t(v >> p & 1 ? to[at] | bit : to[at] & ~bit);
        }
    };
    const auto& anim = files_->anim;
    const auto anim_byte = [&](std::size_t at) -> std::uint8_t { return at < anim.size() ? anim[at] : 0; };
    switch (op.kind) {
    case Kind::dot: {
        // A 4 x 4 dot: colour 15 round its edge, 14 inside.
        static constexpr const char* dot[4] = {".OO.", "OXXO", "OXXO", ".OO."};
        for (int r = 0; r < 4; ++r)
            for (int i = 0; i < 4; ++i)
                if (dot[r][i] != '.')
                    put(op.c ? map_ : screen_, long(op.b + r) * row_pixels + op.a + i, dot[r][i] == 'O' ? 15 : 14);
        break;
    }
    case Kind::dissolve:
        // Every word of every plane: the map's, through the mask.
        for (int i = 0; i < plane_bytes * 4; i += 2) {
            screen_[std::size_t(i)] = std::uint8_t(map_[std::size_t(i)] & op.a >> 8);
            screen_[std::size_t(i + 1)] = std::uint8_t(map_[std::size_t(i + 1)] & op.a);
        }
        break;
    case Kind::scroll:
        pointer_bytes_ = op.a;
        fine_scroll_ = op.b;
        break;
    case Kind::strip:
        // 70 rows of 40 bytes (planes $AF0 apart) to the screen's row 130,
        // two bytes (and the scroll's) in.
        for (int p = 0; p < 4; ++p)
            for (int y = 0; y < 70; ++y)
                for (int i = 0; i < 40; ++i)
                    screen_[std::size_t(p * plane_bytes + (strip_row + y) * row_bytes + 2 + pointer_bytes_ + i)] =
                        anim_byte(strip_at + std::size_t(p * 0xAF0 + y * 40 + i));
        break;
    case Kind::sprite: {
        // $3F3B4: cookie-cut (colour 0 clear), the image's planes one after
        // the other; the blitter ignores an odd address's low bit. Only the
        // rows from 130 to 199 are drawn unless e is 0; x is not clipped
        // (a row's overflow is the next row's start).
        const int w = op.c, h = op.d;
        const std::uint32_t at = op.at & ~1u;
        int first = 0, last = h;
        if (op.e) {
            if (op.b < strip_row) first = strip_row - op.b;
            if (op.b >= rows) last = 0;
            last = std::min(last, rows - op.b);
        }
        for (int r = first; r < last; ++r)
            for (int i = 0; i < w; ++i) {
                const int v = pixel(anim, at, w / 8, w * h / 8, i, r);
                if (v) put(screen_, long(op.b + r) * row_pixels + op.a + i, v);
            }
        break;
    }
    case Kind::tile:
        // $3ED70: 16 rows of a word per plane, at the byte op.a.
        for (int p = 0; p < 4; ++p)
            for (int r = 0; r < 16; ++r)
                for (int i = 0; i < 2; ++i) {
                    const std::size_t to = std::size_t(p * plane_bytes + op.a + r * row_bytes + i);
                    if (to < screen_.size()) screen_[to] = anim_byte(op.at + std::size_t(p * 32 + r * 2 + i));
                }
        break;
    case Kind::upper: upper_ = palettes_[std::size_t(op.a)]; break;
    case Kind::lower:
        lower_ = palettes_[std::size_t(op.a)];
        split_row_ = op.b;
        break;
    case Kind::stop_music:
        music_on_ = false;
        paula_->write_dmacon(0x000F);
        for (int n = 0; n < 4; ++n) paula_->set_volume(n, 0);
        paula_->set_length(3, 1);
        break;
    case Kind::voice: {
        const auto start = std::uint32_t(files_->instruments.size());
        paula_->set_location(3, start);
        paula_->set_length(3, std::uint16_t(op.a));
        paula_->set_period(3, 0x1AC);
        paula_->set_volume(3, 64);
        paula_->write_dmacon(0x8008);
        paula_->set_length(3, 1);
        break;
    }
    case Kind::end:
        paula_->write_dmacon(0x000F); // $3BFA8
        finished_ = true;
        break;
    case Kind::wait:
    case Kind::wait_fire: break;
    }
}

std::vector<std::uint8_t> VenueCard::screen() const {
    std::vector<std::uint8_t> out(std::size_t(row_pixels * rows));
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < row_pixels; ++x) out[std::size_t(y * row_pixels + x)] = std::uint8_t(pixel(screen_, 0, row_bytes, plane_bytes, x, y));
    return out;
}

platform::Image VenueCard::picture() const {
    platform::Image out(width, height);
    for (int y = 0; y < height; ++y) {
        const auto& palette = split_row_ >= 0 && y >= split_row_ ? lower_ : upper_;
        for (int x = 0; x < width; ++x) {
            // The window shows the screen from its second word (the fetch
            // starts a word early for the scroll), shifted by the scroll.
            const long bx = long(y) * row_pixels + 16 + pointer_bytes_ * 8 + x - fine_scroll_;
            const int v = bx >= 0 && bx < row_pixels * rows ? pixel(screen_, 0, row_bytes, plane_bytes, int(bx % row_pixels), int(bx / row_pixels)) : 0;
            const auto c = palette[std::size_t(v)];
            out.put(x, y, platform::Color{std::uint8_t((c >> 8 & 15) * 17), std::uint8_t((c >> 4 & 15) * 17),
                                          std::uint8_t((c & 15) * 17), 255});
        }
    }
    return out;
}

} // namespace beach_volley::front
