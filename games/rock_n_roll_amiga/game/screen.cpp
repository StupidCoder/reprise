#include "rock_n_roll_amiga/game/screen.hpp"
#include <algorithm>

namespace rocknroll::game {
namespace {

// Rows of the graphics bank ($19000 + row × 200).
constexpr int bar_row = 408;     // $2CEC0, the name bar
constexpr int panel_row = 344;   // $29CC0, the panel
constexpr std::uint32_t pillar_even = 0x37140, pillar_odd = 0x3737C; // sprite data copied at $684A8
constexpr int pillar_lines = 0x8F;
constexpr std::uint32_t frame_colours = 0x73344; // 32 words: the bar's and panel's palette
constexpr int pillar_first_line = 0x2E - 0x3D; // sprite line $2E, the display starts at $3D
constexpr int pillar_x[4] = {-1, 15, 271, 287}; // HSTART $80, $90, $190, $1A0 (display x 0 = $81)

std::uint32_t low_word(Fixed v) { return std::uint32_t(v) & 0xFFFF; }

// The bank position of a byte address in it.
struct BankSpot {
    int x, y;
};
BankSpot spot(std::uint32_t address) {
    const std::uint32_t offset = address - data::GraphicsBank::address;
    return {int(offset % data::GraphicsBank::row_bytes) * 8, int(offset / data::GraphicsBank::row_bytes)};
}

// The ball's picture and mask (state routines $6C274/$6C258/$6C266 and the
// fall $6C3D6): picture at $6C22A, mask at $6C22E, 16 x 16, all planes.
struct BallPicture {
    BankSpot picture, mask;
};

BallPicture ball_picture(const Ball& b) {
    if (b.mode == BallMode::rescued) { // $6C488: masks from $32650
        const int frame = (b.rescue_updates >> 3) & 0xFE;
        const std::uint32_t mask = 0x32650 + std::uint32_t(std::min(frame, 14));
        return {spot(mask - 0xC80), spot(mask)};
    }
    if (b.mode == BallMode::falling || b.mode == BallMode::lost) {
        const int frame = (b.fall_updates >> 2) & 0xFE;
        if (frame <= 14) {
            // Masks from $30D50 (a fall) or $34BD0 (empty energy, $6C3CC).
            const std::uint32_t mask = (b.energy_out ? 0x34BD0 : 0x30D50) + std::uint32_t(frame);
            return {spot(mask - 0xC80), spot(mask)};
        }
        return {spot(0x300DE), spot(0x30D5E)};
    }
    std::uint32_t base = 0x2F440;
    std::uint32_t phase_mask = 0x7000;
    if (b.kind == BallKind::protective) base = 0x2F460, phase_mask = 0x3000;
    if (b.kind == BallKind::ice_proof) base = 0x32660, phase_mask = 0x3000;
    const std::uint32_t fx = (std::uint32_t(b.roll_x) & phase_mask) >> 12;
    const std::uint32_t fy = (std::uint32_t(b.roll_y) & phase_mask) >> 12;
    return {spot(base + fx * 2 + fy * 0xC80), spot(0x2F450)};
}

} // namespace

Screen::Screen(const data::Assets& assets, const data::World& world)
    : world_(&world), pixels_(std::size_t(width) * height, 0) {
    for (std::uint32_t i = 0; i < 32; ++i) frame_palette_.push_back(amiga::Rgb4(assets.program.u16(frame_colours + 2 * i)));
    sprite_even_.assign(assets.program.bytes(pillar_even, pillar_lines * 4).begin(),
                        assets.program.bytes(pillar_even, pillar_lines * 4).end());
    sprite_odd_.assign(assets.program.bytes(pillar_odd, pillar_lines * 4).begin(),
                       assets.program.bytes(pillar_odd, pillar_lines * 4).end());
}

void Screen::glyph(int x, int y, int index, int lines) {
    draw_glyph(canvas(), world_->bank, x, y, index, lines, 2);
}

void Screen::draw_digits(int x, int y, std::span<const int> values) {
    constexpr int zero = '0' - 0x27, blank = 0x28 - 0x27;
    bool leading = true;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const bool last = i + 1 == values.size();
        leading = leading && values[i] == 0 && !last;
        glyph(x, y, leading ? blank : zero + values[i], 7);
        x += 8;
    }
}

void Screen::draw_bar(std::string_view level_name, std::string_view bar_text) {
    const auto& bank = world_->bank;
    for (int y = 0; y < bar_lines; ++y)
        for (int x = 0; x < width; ++x) at(x, y) = bank.at(x, bar_row + y);
    print(canvas(), world_->bank, 96, 4, level_name, 2);  // $6B110 → $6AE34: bar + row 4, byte 12
    print(canvas(), world_->bank, 248, 4, bar_text, 2);   // $6AE44: bar + row 4, byte 31
}

void Screen::draw_view(const LevelMap& map, const Ball& b) {
    const auto& bank = world_->bank;
    const int fine_x = int(low_word(b.camera_x) >> 12) & 15;
    const int fine_y = int(low_word(b.camera_y) >> 12) & 15;
    const long first = long(std::uint32_t(b.camera_y) >> 16) * map.columns + long(std::uint32_t(b.camera_x) >> 16);
    // The abyss pattern's place for this camera ($6C972).
    const std::uint32_t abyss = 0x157C0 + ((std::uint32_t(b.camera_x) >> 14) & 15) * 2 -
                                ((std::uint32_t(b.camera_y) >> 14) & 15) * 200;
    const int abyss_x = int(abyss % 200) * 8, abyss_y = int(abyss / 200);
    // Picture of one cell's tile at (tx, ty) inside it.
    const auto tile_pixel = [&](std::uint8_t cell, int tx, int ty) -> std::uint8_t {
        if (data::is_abyss(cell)) return bank.at(abyss_x + tx, abyss_y + ty);
        if (cell == 0 || cell > 0xF0) return 0;
        return bank.at(map.tiles.x(cell) + tx, map.tiles.y(cell) + ty); // the tile table ($72CBE), animated
    };
    // The view buffer: buffer x 32-287 holds 16 tile columns from the
    // camera's cell (a byte pointer, so past the last column it runs on into
    // the next row), buffer row r tile row (fine_y + r) / 16.
    std::vector<std::uint8_t> buffer(std::size_t(336) * view_lines, 0);
    const auto buf = [&](int x, int y) -> std::uint8_t& { return buffer[std::size_t(y) * 336 + std::size_t(x)]; };
    for (int line = 0; line < view_lines; ++line) {
        const int r = fine_y + line;
        for (int x = 32; x < 288; ++x) {
            const long index = first + long(r / 16) * map.columns + (x - 32) / 16;
            buf(x, line) = tile_pixel(map.cell(index), (x - 32) % 16, r % 16);
        }
    }
    // The ball ($6F7B6): cookie-cut through its mask, plane by plane, at
    // buffer x (view x tiles) × 16 + fine x of camera + view, line view y.
    const auto ball = ball_picture(b);
    const int ball_x = int(std::uint32_t(b.view_x) >> 16) * 16 + int(((low_word(b.camera_x) + low_word(b.view_x)) & 0xF000) >> 12);
    const int ball_y = int(b.view_y >> 12);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const int bx = ball_x + x, by = ball_y + y;
            if (bx < 0 || bx >= 336 || by < 0 || by >= view_lines) continue;
            const std::uint8_t mask = bank.at(ball.mask.x + x, ball.mask.y + y);
            const std::uint8_t pic = bank.at(ball.picture.x + x, ball.picture.y + y);
            auto& p = buf(bx, by);
            p = std::uint8_t((pic & mask) | (p & ~mask));
        }
    }
    // Cells of layer 2 under the ball ($DC and higher) are drawn again over
    // it ($6FCB0): the ball rolls under them.
    const long under = long(std::uint16_t((std::uint32_t(b.camera_y) >> 16) + (std::uint32_t(b.view_y) >> 16))) * map.columns +
                       long(std::uint16_t((std::uint32_t(b.camera_x) >> 16) + (std::uint32_t(b.view_x) >> 16))) - 2;
    for (const long cell : {under, under + 1, under + map.columns, under + map.columns + 1}) {
        const std::uint8_t v = map.cell(cell);
        if (v < 0xDC || v > 0xF0) continue;
        const long dx = cell - first; // in the buffer's linear cell order
        const long row = dx / map.columns, column = dx % map.columns;
        for (int ty = 0; ty < 16; ++ty) {
            const int line = int(row) * 16 + ty - fine_y;
            if (line < 0 || line >= view_lines) continue;
            for (int tx = 0; tx < 16; ++tx) {
                const int x = 32 + int(column) * 16 + tx;
                if (x >= 32 && x < 288) buf(x, line) = tile_pixel(v, tx, ty);
            }
        }
    }
    // Display x p shows buffer x 1 + fine_x + p (fetch from word 1, BPLCON1
    // delay 15 - fine_x, $7313E).
    for (int line = 0; line < view_lines; ++line) {
        for (int p = 0; p < width; ++p) {
            const int x = 1 + fine_x + p;
            at(p, bar_lines + line) = x < 336 ? buf(x, line) : 0;
        }
    }
}

// Decimal digits of a value as the program splits them: $6F5D6 (ten
// thousands … units, then a 0 byte that the score and high score show as a
// sixth digit) and $6F578 (the time: v / 6000, then minutes' tens and
// units, seconds' tens and units).
std::array<int, 6> decimal(int v) {
    const auto u = std::uint32_t(v);
    return {int(u / 10000 % 10), int(u / 1000 % 10), int(u / 100 % 10), int(u / 10 % 10), int(u % 10), 0};
}
std::array<int, 5> clock_digits(int seconds) {
    const auto u = std::uint32_t(seconds);
    return {int(u / 6000), int(u % 6000 / 600), int(u % 600 / 60), int(u % 60 / 10), int(u % 10)};
}

void Screen::draw_panel(const PanelNumbers& n) {
    const auto& bank = world_->bank;
    const int top = bar_lines + view_lines;
    for (int y = 0; y < panel_lines; ++y)
        for (int x = 0; x < width; ++x) at(x, top + y) = bank.at(x, panel_row + y);
    // Places: the destination's bank address, row - 344 and byte × 8.
    const auto time = [&](int y, int seconds, int hundredths) {
        const auto t = clock_digits(seconds);
        draw_digits(8, top + y, std::span(t).subspan(1, 2));  // minutes ($2B2F1 / $2ABE9)
        draw_digits(32, top + y, std::span(t).subspan(3, 2)); // seconds ($2B2F4 / $2ABEC)
        const int tenths[1] = {int(std::uint32_t(hundredths) / 10 % 10)}; // $6F570
        draw_digits(48, top + y, tenths);
    };
    time(19, n.record.seconds, n.record.hundredths); // record ($6F1A6)
    time(28, n.seconds, n.hundredths);               // time ($6F270, $6F178)
    draw_digits(8, top + 37, decimal(n.high_score)); // $2B9F9
    if (n.score_shown) draw_digits(8, top + 45, decimal(n.score)); // $2C039
    const auto money = decimal(n.money);
    draw_digits(112, top + 18, std::span(money).subspan(1, 4)); // $2AB2E
    // Item boxes: speed bought ($2AFEB, the byte itself as the digit) and
    // the keys ($2AFEF, $2AFF3, $2C2AF, $2C2B3).
    // Bars ($6F548): (value >> 6) + 1 pixels of colour bit 2 over three
    // lines (a long shifted left by ~(value >> 6) & 31), from x 112: the
    // energy shown ($2C29E) and the bought ball's time ($2B2FE, $6F4F4;
    // cleared when it ends).
    const auto bar = [&](int y, int value, bool on) {
        const int length = on ? 32 - (~(std::int16_t(value) >> 6) & 31) : 0;
        for (int line = 0; line < 3; ++line)
            for (int i = 0; i < 32; ++i) {
                auto& p = at(112 + i, top + y + line);
                p = std::uint8_t((p & ~4) | (i < length ? 4 : 0));
            }
    };
    bar(48, n.shown_energy, true); // every update
    if (n.kind_drawn) bar(28, n.kind_updates, n.kind_running);
    // The balls left ($69A10): six slots of 7 lines from line 51 upwards,
    // a ball (bank row 584, x 224) for each ball left, else an empty slot
    // (x 256), cut through the mask at x 240; one word, shifted 2 pixels
    // right (BLTCON0 $2FCA, BLTCON1 $2000).
    for (int slot = 0; slot < 6; ++slot) {
        const int picture_x = slot < n.balls_left ? 224 : 256;
        for (int line = 0; line < 7; ++line)
            for (int i = 0; i < 14; ++i) {
                const std::uint8_t mask = bank.at(240 + i, 584 + line);
                auto& p = at(290 + i, top + 51 - slot * 7 + line);
                p = std::uint8_t((bank.at(picture_x + i, 584 + line) & mask) | (p & ~mask));
            }
    }
    // The chosen item ($6EE76): its picture (bank row 584, 32 pixels per
    // item, the first 23 columns) at x 200, lines 42-55, and for bombs,
    // repair sets and rescues their number ($2C2AB).
    for (int line = 0; line < 14; ++line)
        for (int i = 0; i < 23; ++i) at(200 + i, top + 42 + line) = bank.at(n.item * 16 + i, 584 + line);
    if (n.item == 6 || n.item == 8 || n.item == 10) {
        const int count[1] = {int(std::uint32_t(n.item_count) % 10)};
        draw_digits(216, top + 48, count);
    }
    const int speed[1] = {n.speed_bought};
    draw_digits(216, top + 24, speed);
    static constexpr int key_x[4] = {248, 280, 248, 280}, key_y[4] = {24, 24, 48, 48};
    for (std::size_t k = 0; k < 4; ++k) {
        const int count[1] = {int(std::uint32_t(n.keys[k]) % 10)};
        draw_digits(key_x[k], top + key_y[k], count);
    }
}

void Screen::draw_pillars() {
    // Attached sprite pairs: the even sprite gives colour bits 0-1, the odd
    // one bits 2-3, colour 16 + that. Over the name bar (BPLCON2 0) the
    // playfield is in front where it is not colour 0; over the view
    // (BPLCON2 $20: a single playfield takes its priority from PF2P, 4 =
    // behind all sprites) the pillars cover the tiles, which hides the
    // columns scrolling in at the edges (checked against an FS-UAE capture).
    for (int k = 0; k < pillar_lines; ++k) {
        const int y = pillar_first_line + k;
        if (y < 0 || y >= height) continue;
        const auto word = [&](const std::vector<std::uint8_t>& d, int i) {
            return std::uint16_t(d[std::size_t(k * 4 + i)] << 8 | d[std::size_t(k * 4 + i + 1)]);
        };
        const std::uint16_t a0 = word(sprite_even_, 0), b0 = word(sprite_even_, 2);
        const std::uint16_t a1 = word(sprite_odd_, 0), b1 = word(sprite_odd_, 2);
        for (const int left : pillar_x) {
            for (int i = 0; i < 16; ++i) {
                const int x = left + i;
                if (x < 0 || x >= width) continue;
                const int bit = 15 - i;
                const int value = (a0 >> bit & 1) | (b0 >> bit & 1) << 1 | (a1 >> bit & 1) << 2 | (b1 >> bit & 1) << 3;
                if (value != 0 && (y >= bar_lines || at(x, y) == 0)) at(x, y) = std::uint8_t(16 + value);
            }
        }
    }
}

void Screen::draw(const LevelMap& map, const Ball& b, const PanelNumbers& numbers, std::string_view level_name,
                  std::string_view bar_text, const std::vector<ScreenText>& view_text) {
    draw_bar(level_name, bar_text);
    const auto level = decimal(numbers.level);
    draw_digits(40, 4, std::span(level).subspan(2, 3)); // $2D235: bar row 4, byte 5
    if (view_text.empty()) {
        draw_view(map, b);
    } else {
        // The prompts clear the view and print there ($6B8E6; $6AE54: the
        // first two bytes are the column (bytes) and row (8 lines) from the
        // view's buffer x 32, line 8).
        for (int y = 0; y < view_lines; ++y)
            for (int x = 0; x < width; ++x) at(x, bar_lines + y) = 0;
        const int fine_x = int(low_word(b.camera_x) >> 12) & 15;
        for (const auto& text : view_text) print_at(canvas(), world_->bank, 32 - 1 - fine_x, bar_lines + 8, text);
    }
    draw_panel(numbers);
    draw_pillars();
}

} // namespace rocknroll::game
