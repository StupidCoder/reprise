#include "mindwalker_amiga/psyche/psyche_stage.hpp"
#include "amiga/intuition_imagery.hpp"
#include "mindwalker_amiga/screen/area_fill.hpp"
#include <algorithm>
#include <map>

namespace mindwalker::psyche {
namespace {

// P's data (research layout).
constexpr std::uint32_t palette = 0x5002F4;       // h3: 32 colours
constexpr std::uint32_t new_window = 0x500BAA, screen_title = 0x500B66;
constexpr std::uint32_t first_life_data = 0x500A88, pause_box_data = 0x500A4A, knob_data = 0x500A36;
constexpr std::uint32_t s_life = 0x700A2C, s_pause_box = 0x700A40, s_knob = 0x700A54;
constexpr std::uint32_t button_x = 0x501D52, button_y = 0x501D6E; // the hint buttons (7 longs each)
constexpr std::uint32_t button_data = 0x501D8A, big_data = 0x501EDA; // h8's images' data
constexpr std::uint32_t speeds = 0x502108;        // point 0's new speeds (8 longs)
constexpr std::uint32_t home_column = 0x502404, home_row = 0x50242E; // each tile's place (42 bytes each)
constexpr std::uint32_t shuffle_extra = 0x502400; // the shuffle's second generator
constexpr std::uint32_t wobble_seed = 0x501BE8, steer_seed = 0x501BEC, flip_seed = 0x501BF0;
constexpr std::uint32_t score_label = 0x5068C6, bonus_label = 0x506910, number_text = 0x5068A6,
                        whole_texts = 0x506A74;   // IntuiTexts
constexpr std::uint32_t cursor_a = 0x506514, cursor_b = 0x506574; // sprites 2 and 3, 22 rows
constexpr std::uint32_t portrait_red = 0x5057D0, portrait_green = 0x5057D6, portrait_blue = 0x5057DC;
constexpr int pause_gadget = 0x10;
constexpr int title_text_pen = 0, title_bar_pen = 0x12; // P's NewScreen's DetailPen and BlockPen
constexpr int red = 0xF00;

std::vector<std::uint8_t> bytes_at(const amiga::LoadedImage& program, std::uint32_t at, std::size_t count) {
    std::vector<std::uint8_t> out(count);
    for (std::size_t i = 0; i < count; ++i) out[i] = program.u8(at + std::uint32_t(i));
    return out;
}

// PrintIText: a chain of IntuiTexts (JAM2 cells in their BackPen), topaz 8.
void print_itext(screen::Canvas& canvas, const amiga::LoadedImage& p, std::uint32_t at, int x, int y,
                 const std::string* replacement = nullptr) {
    for (; at != 0; at = p.u32(at + 16)) {
        std::string text;
        if (replacement) {
            text = *replacement;
        } else {
            for (std::uint32_t c = p.u32(at + 12); p.u8(c) != 0; ++c) text.push_back(char(p.u8(c)));
        }
        canvas.text(x + p.s16(at + 4), y + p.s16(at + 6) + 6, text, p.u8(at), screen::TextFont::topaz8, screen::plain,
                    p.u8(at + 2) == 1, p.u8(at + 1));
    }
}

data::Bitmap sprite_image(const amiga::LoadedImage& p, std::uint32_t at, int rows) {
    data::Bitmap b{16, rows, 2, std::vector<std::uint8_t>(std::size_t(16 * rows))};
    for (int y = 0; y < rows; ++y) {
        const auto a = p.u16(at + 4 + std::uint32_t(y * 4)), c = p.u16(at + 6 + std::uint32_t(y * 4));
        for (int x = 0; x < 16; ++x) b.pixels[std::size_t(y * 16 + x)] = std::uint8_t((a >> (15 - x) & 1) | (c >> (15 - x) & 1) << 1);
    }
    return b;
}

} // namespace

PsycheStage::PsycheStage(const data::GameFiles& files, const data::Assets& assets, const Start& start)
    : p_(files.program(data::Stage::psyche)), assets_(assets), stage_(start.stage), lives_(start.lives),
      score_(start.score), bonus_(start.bonus), time_(start.time),
      sound_(p_, assets.samples[3].data, assets.samples[4].data,
             std::vector<std::vector<std::int8_t>>{assets.samples[5].data, assets.samples[6].data, assets.samples[7].data,
                                                   assets.samples[8].data, assets.samples[9].data, assets.samples[10].data}) {
    // $500698: the window's gadgets (S's icons; lives as in the others).
    const auto& s = files.program(data::Stage::setup);
    std::map<std::uint32_t, std::vector<std::uint8_t>> icons = {{pause_box_data, bytes_at(s, s_pause_box, 20)},
                                                                {knob_data, bytes_at(s, s_knob, 24)}};
    for (std::uint32_t i = 0; i < 5; ++i) icons[first_life_data + i * 20] = bytes_at(s, s_life, 20);
    bar_ = screen::GadgetBar(p_, new_window, std::move(icons));
    for (auto& gadget : bar_.gadgets()) {
        if (gadget.proportional) gadget.horizontal_pot = std::uint16_t(stage_ * 0x618);
        if (gadget.images.size() == 5) // $50039C: figure i while i + 1 < lives
            for (std::size_t i = 0; i < 5; ++i) {
                gadget.images[i].depth = int(i) + 1 < lives_ ? 1 : 0;
                gadget.images[i].plane_pick = int(i) + 1 < lives_ ? 8 : 0;
            }
    }
    for (std::uint32_t i = 0; i < 32; ++i) colours_[i] = p_.u16(palette + i * 2);
    button_image_ = data::planar_bitmap(bytes_at(p_, button_data, 0x150), 19, 21, 4);
    big_image_ = data::planar_bitmap(bytes_at(p_, big_data, 0x22E), 34, 31, 3);
    cursor_ = {sprite_image(p_, cursor_a, 22), sprite_image(p_, cursor_b, 22)};
    // $500F6E: the tiles laid out, the panel, the picture cleared, the
    // points at the corner moving down and right, the generators seeded
    // from the world's seed.
    held_ = 0xFF;
    humming_ = -1;
    layout(start.world_seed);
    draw_panel();
    back_.clear();
    vx_.fill(1);
    vy_.fill(1);
    wobble_.state = p_.u32(wobble_seed) ^ (start.world_seed >> 4);
    steer_.state = p_.u32(steer_seed) ^ (start.world_seed >> 6);
    flip_.state = p_.u32(flip_seed) ^ (start.world_seed << 5);
}

// h11 / $5022EA: the 42 tiles shuffled by the world's seed; the first
// (progress - 7) of the shuffle go home, the next seven into the slots.
void PsycheStage::layout(std::uint32_t world_seed) {
    shuffle_random_.state = world_seed;
    shuffle_extra_.state = p_.u32(shuffle_extra);
    target_ = std::max(stage_, 7);
    bar_pace_ = 7 - target_ / 10;
    place_.fill(0xFF);
    order_.fill(0xFF);
    for (int k = 0; k < 42; ++k) {
        int c = 0;
        for (int tries = 1;; ++tries) {
            c = shuffle_random_.next();
            if (tries > 0x32) c ^= shuffle_extra_.next();
            c &= 0x3F;
            if (c > 0x29) c -= 0x1E;
            if (order_[std::size_t(c)] == 0xFF) break;
        }
        order_[std::size_t(c)] = std::uint8_t(k);
    }
    for (auto& column : grid_) column.fill(0xFF);
    const int base = target_ - 7;
    for (int i = 0; i < 7; ++i) {
        const auto tile = order_[std::size_t(i + base)];
        slot_[std::size_t(i)] = tile;
        place_[tile] = std::uint8_t(i);
    }
    for (int j = 0; j < base; ++j) {
        const auto tile = order_[std::size_t(j)];
        const int column = p_.u8(home_column + tile), row = p_.u8(home_row + tile);
        place_[tile] = std::uint8_t(row << 3 | column | 0x80);
        grid_[std::size_t(column)][std::size_t(row)] = tile;
    }
}

int PsycheStage::gadget_at(int x, int y) const {
    if (y < 0 || y >= 10) return -1;
    if (x >= 0 && x < amiga::WindowCloseGadget::width) return -2;
    for (const auto& gadget : bar_.gadgets())
        if (x >= gadget.x() && x < gadget.x() + gadget.width && y >= gadget.top && y < gadget.top + gadget.height)
            return gadget.id;
    return -1;
}

int PsycheStage::portrait() const { // h32: by the progress, MindF1-6
    return std::clamp(stage_ / 7 - 1, 0, 5);
}

std::uint16_t PsycheStage::portrait_colour() const {
    const auto k = std::uint32_t(portrait());
    return std::uint16_t((p_.u8(portrait_red + k) & 0xF) << 8 | (p_.u8(portrait_green + k) & 0xF) << 4 |
                         (p_.u8(portrait_blue + k) & 0xF));
}

int PsycheStage::hint_height() const {
    return humming_ >= 0 ? hint_[std::size_t(humming_)] : 0;
}

void PsycheStage::image(const data::Bitmap& b, int plane_pick, int x, int y) { // DrawImage, PlaneOnOff 0
    for (int j = 0; j < b.height; ++j)
        for (int i = 0; i < b.width; ++i)
            screen_.plot(x + i, y + j, (screen_.pen(x + i, y + j) & ~plane_pick) | (b.at(i, j) & plane_pick));
}

// $5010AC: the frame, the picture's grid and the left panel: the slots,
// the colour bar, the big creature and the hint buttons, the scores; each
// slot and cell cleared (pen 0) or empty (pen 16).
void PsycheStage::draw_panel() {
    screen_.fill(0, top_, 0x13F, 199, 0x14);
    screen_.fill(1, top_ + 1, 0x13E, 0xC6, 0x10);
    screen_.fill(0xAB, 0x47, 0x138, 0xC0, 0x14);
    screen_.fill(0xAC, 0x48, 0x137, 0xBF, 0);
    screen_.fill(7, 0x12, 0xA3, 0xC0, 0x14);
    if (!whole_) {
        screen_.fill(8, 0x13, 0xA2, 0xBF, 0x10);
        screen_.fill(9, 0x2A, 0xA1, 0xBE, 0);
        for (int i = 0; i < 7; ++i) draw_colour(i);
        draw_images();
        draw_scores();
    }
    for (int i = 0; i < 7; ++i) {
        draw_slot(i, slot_[std::size_t(i)] == 0xFF ? 0x10 : 0);
        for (int r = 0; r < 6; ++r) draw_cell(i, r, grid_[std::size_t(i)][std::size_t(r)] == 0xFF ? 0x10 : 0);
    }
    if (whole_) draw_whole();
}

void PsycheStage::draw_slot(int slot, int pen) {
    const int x = slot * 0x16 + 9;
    screen_.fill(x, 0x14, x + 0x13, 0x27, pen);
}

void PsycheStage::draw_cell(int column, int row, int pen) {
    const int x = column * 0x14 + 0xAC, y = row * 0x14 + 0x48;
    screen_.fill(x, y, x + 0x13, y + 0x13, pen);
}

void PsycheStage::draw_colour(int slot) {
    const int x = slot * 0x16 + 8;
    screen_.fill(x, 0x33, x + 0x16, 0x3B, slot + 0x19);
}

void PsycheStage::draw_images() { // $501042 (the Images' LeftEdge/TopEdge: 0, 0 and -2, -15)
    image(big_image_, 7, 0x45, 0x9C);
    for (std::uint32_t i = 0; i < 7; ++i) image(button_image_, 15, p_.s32(button_x + i * 4) - 2, p_.s32(button_y + i * 4) - 15);
}

void PsycheStage::draw_scores() {
    print_itext(screen_, p_, score_label, 0xAC, 0x14);
    draw_number(score_, 0xAC, 0x1E);
    draw_bonus();
}

void PsycheStage::draw_bonus() {
    print_itext(screen_, p_, bonus_label, 0xAC, 0x2D);
    draw_number(bonus_, 0xE4, 0x37);
}

void PsycheStage::draw_number(std::uint32_t value, int x, int y) { // without leading zeros; nothing for 0
    const std::string digits = value == 0 ? std::string() : std::to_string(value);
    print_itext(screen_, p_, number_text, x, y, &digits);
}

void PsycheStage::draw_whole() { // $506694
    screen_.fill(8, 0x13, 0xA2, 0xBF, 0x10);
    print_itext(screen_, p_, whole_texts, 0xD, 0x19);
    draw_number(lives_bonus_, 0xD, 0x2D);
    draw_number(time_bonus_, 0xD, 0x4B);
    draw_scores();
}

void PsycheStage::frame(const PsycheReader& read, int gadget) {
    if (!sequence_.empty()) {
        const auto step = std::move(sequence_.front());
        sequence_.erase(sequence_.begin());
        step();
        return;
    }
    if (outcome_ != Outcome::playing) return;
    if (paused_) {
        if (gadget == pause_gadget) resume();
        else if (gadget == -2) outcome_ = Outcome::quit;
        return;
    }
    if (gadget == -2) { // CLOSEWINDOW
        outcome_ = Outcome::quit;
        sound_.stop();
        return;
    }
    if (gadget == pause_gadget) {
        pause();
        return;
    }
    sound_.frame(hint_height());
    if (whole_) {
        // The last picture's passes: one each time the timer comes back
        // (60 ms), the whole picture copied at once.
        if (++whole_wait_ < 3) return;
        whole_wait_ = 0;
        button(read);
        kaleidoscope();
        for (int y = 0; y < 120; ++y)
            for (int x = 0; x < 140; ++x) screen_.plot(0xAC + x, 0x48 + y, (screen_.pen(0xAC + x, 0x48 + y) & 16) | back_.pen(x, y));
        end_of_pass();
        return;
    }
    // $500040's pass: the button, the kaleidoscope, then the tiles, four
    // between vertical blanks ($50198E waits before tiles 0, 4, 8, ...).
    for (;;) {
        if (!pass_started_) {
            button(read);
            kaleidoscope();
            pass_started_ = true;
            next_tile_ = 0;
            pass_home_ = 0;
        }
        while (next_tile_ < 42) {
            const int t = next_tile_;
            const int at = place_[std::size_t(t)];
            if (at == 0xFF) {
                ++next_tile_;
                continue;
            }
            if ((t & 3) == 0 && !waited_) {
                waited_ = true;
                return;
            }
            waited_ = false;
            ++blits_;
            if ((at & 0x80) && (at & 7) == p_.u8(home_column + std::uint32_t(t)) && (at >> 3 & 7) == p_.u8(home_row + std::uint32_t(t)))
                ++pass_home_;
            blit_tile(t);
            button(read);
            ++next_tile_;
            after_tile();
            if (!sequence_.empty() || outcome_ != Outcome::playing) return;
        }
        end_of_pass();
        pass_started_ = false;
        if (outcome_ != Outcome::playing || whole_) return;
    }
}

// A tile's piece of the picture to where it lies (planes 0-3).
void PsycheStage::blit_tile(int tile) {
    const int at = place_[std::size_t(tile)];
    const int sx = p_.u8(home_column + std::uint32_t(tile)) * 0x14, sy = p_.u8(home_row + std::uint32_t(tile)) * 0x14;
    const int dx = (at & 0x80) ? (at & 7) * 0x14 + 0xAC : (at & 7) * 0x16 + 9;
    const int dy = (at & 0x80) ? (at >> 3 & 7) * 0x14 + 0x48 : 0x14;
    for (int y = 0; y < 0x14; ++y)
        for (int x = 0; x < 0x14; ++x) screen_.plot(dx + x, dy + y, (screen_.pen(dx + x, dy + y) & 16) | back_.pen(sx + x, sy + y));
}

// $500418: a press of the button acts where the pointer is ($505852).
void PsycheStage::button(const PsycheReader& read) {
    const auto in = read();
    mouse_x_ = in.x;
    mouse_y_ = in.y;
    if (in.button != button_down_ && in.button && !paused_) click(in.x, in.y);
    button_down_ = in.button;
}

// $505852 / $505FD2: a slot (x from 9, 22 apart, y $14-$27) or a cell of the
// grid (from $AC, $48, 20 apart): a tile picked up, or the held one put
// down where nothing is; elsewhere, with nothing held, the hint buttons.
void PsycheStage::click(int x, int y) {
    int column = 0, row = 0;
    bool grid = false;
    if (x < 0xAC) {
        column = x - 9;
        if (column >= 0) column /= 0x16;
        row = (y < 0x14 || y > 0x27) ? -1 : 0;
    } else {
        grid = true;
        column = x - 0xAC;
        if (column >= 0) column /= 0x14;
        row = y - 0x48;
        if (row >= 0) row /= 0x14;
    }
    const bool valid = row >= 0 && row <= 5 && column >= 0 && column <= 6;
    if (!whole_) {
        if (!valid) {
            if (held_ == 0xFF) { // $5058C2
                for (std::uint32_t i = 0; i < 7; ++i) {
                    const int bx = p_.s32(button_x + i * 4), by = p_.s32(button_y + i * 4);
                    if (bx + 4 < x && x < bx + 0x12 && by - 3 < y && y < by + 8) {
                        if (hint_[i] == 0) {
                            hint_[i] = 1;
                            humming_ = int(i);
                        }
                        break;
                    }
                }
            }
        } else {
            auto& spot = grid ? grid_[std::size_t(column)][std::size_t(row)] : slot_[std::size_t(column)];
            if (held_ == 0xFF) { // $505DD6: picked up
                if (spot != 0xFF) {
                    held_ = spot;
                    place_[std::size_t(held_)] = 0xFF;
                    if (grid) draw_cell(column, row, 0x10);
                    else draw_slot(column, 0x10);
                    spot = 0xFF;
                }
            } else if (spot == 0xFF) { // $505CD8: put down
                if (grid) draw_cell(column, row, 0);
                else draw_slot(column, 0);
                spot = std::uint8_t(held_);
                place_[std::size_t(held_)] = std::uint8_t((grid ? 0x80 : 0) | row << 3 | column);
                held_ = 0xFF;
            }
        }
        place_cursor();
    }
}

// $50627E: the hand (sprites 2 and 3) at the pointer while a tile is held.
void PsycheStage::place_cursor() {
    if (held_ == 0xFF) {
        cursor_x_ = -0x5A;
        cursor_y_ = 0x14;
    } else {
        cursor_x_ = mouse_x_ - 10;
        cursor_y_ = mouse_y_ - 0x14;
    }
}

// $505970: after each tile: the hand; every sixth tile the hint lines climb
// a step, and every (pace + 1) such steps the colour bar turns.
void PsycheStage::after_tile() {
    place_cursor();
    if (blits_ > 5) {
        blits_ = 0;
        hints(0);
    }
}

void PsycheStage::after_hints() {
    if (++bar_wait_ > bar_pace_) {
        bar_wait_ = 0;
        rotate_bar();
    }
}

// $5059B2: each climbing line a step (10 pixels) up from its button, in a
// colour of its own; passing the colour bar (y $32-$3C) its slot's colour
// must be red, or the line is wiped; above $28 the slot's tile goes home.
void PsycheStage::hints(int from) {
    for (int i = from; i < 7; ++i) {
        auto& h = hint_[std::size_t(i)];
        if (h == 0) continue;
        const int bx = p_.s32(button_x + std::uint32_t(i * 4)), by = p_.s32(button_y + std::uint32_t(i * 4));
        const int x = bx + 0xB, y = by - h * 10;
        if (y < 0x32 || y > 0x3C || colours_[std::size_t(25 + i)] == red) {
            if (y < 0x28) {
                h = 0;
                humming_ = -1;
                screen_.line(x, 0x18, x, 0x29, 0x10);
                screen_.line(x, 0x2A, x, by - 1, 0);
                draw_colour(i);
                image(button_image_, 15, bx - 2, by - 15);
                hint_home(i);
                if (!sequence_.empty()) {
                    sequence_.push_back([this, i] { hints(i + 1); });
                    return;
                }
            } else {
                screen_.line(x, y, x, y + 9, ((i * 4 - h) & 0xF) + 1);
                ++h;
            }
        } else {
            h = 0;
            humming_ = -1;
            screen_.line(x, 0x3C, x, by - 1, 0);
            image(button_image_, 15, bx - 2, by - 15);
        }
    }
    after_hints();
}

// $505E98: a hint's tile home: the slot emptied, the hand flies from the
// slot to the cell ($506332: a pixel a step on each axis, a frame after
// every few steps, faster and faster), the music hurried; then the cell
// cleared, the tile that lay there (if any) into the slot, the bonus 1,000
// less.
void PsycheStage::hint_home(int slot) {
    const int tile = slot_[std::size_t(slot)];
    if (tile == 0xFF) return;
    const int column = p_.u8(home_column + std::uint32_t(tile)), row = p_.u8(home_row + std::uint32_t(tile));
    const int before = grid_[std::size_t(column)][std::size_t(row)];
    grid_[std::size_t(column)][std::size_t(row)] = std::uint8_t(tile);
    slot_[std::size_t(slot)] = 0xFF;
    place_[std::size_t(tile)] = std::uint8_t(row << 3 | column | 0x80);
    draw_slot(slot, 0x10);
    sound_.set_fast(true);
    int x = slot * 0x16 + 9, y = 0x14;
    const int tx = column * 0x14 + 0xAC, ty = row * 0x14 + 0x48 - 10;
    int steps = 0, pace = 0x20;
    for (int reached = 0; reached < 2;) {
        reached = 0;
        if (x == tx) ++reached;
        else x += tx < x ? -1 : 1;
        if (y == ty) ++reached;
        else y += ty < y ? -1 : 1;
        if (pace >> 3 < ++steps) {
            steps = 0;
            sequence_.push_back([this, x, y] {
                cursor_x_ = x;
                cursor_y_ = y;
                sound_.frame(0);
            });
            if (pace > 8) --pace;
        }
    }
    sequence_.push_back([this, slot, column, row, before, x, y] {
        cursor_x_ = x;
        cursor_y_ = y;
        sound_.set_fast(false);
        bonus_ = bonus_ < 1000 ? 0 : bonus_ - 1000; // $506796
        draw_bonus();
        place_cursor();
        draw_cell(column, row, 0);
        if (before != 0xFF) {
            slot_[std::size_t(slot)] = std::uint8_t(before);
            place_[std::size_t(before)] = std::uint8_t(slot);
            draw_slot(slot, 0);
        }
    });
}

void PsycheStage::rotate_bar() { // $505C5E: colours 25-31 turn
    const auto last = colours_[31];
    for (int k = 31; k > 25; --k) colours_[std::size_t(k)] = colours_[std::size_t(k - 1)];
    colours_[25] = last;
}

// The end of $50198E's pass: colours 1-15 turn; enough tiles home: done,
// or at the game's last picture ($5024..: 42) the whole picture stays.
void PsycheStage::end_of_pass() {
    const auto last = colours_[15];
    for (int k = 15; k > 1; --k) colours_[std::size_t(k)] = colours_[std::size_t(k - 1)];
    colours_[1] = last;
    if (whole_ || target_ > pass_home_) return;
    if (target_ == 42) {
        become_whole();
        return;
    }
    // $500040's end: silence, the screen cleared, colour 0 black (the portrait follows).
    outcome_ = Outcome::done;
    sound_.stop();
    screen_.fill(0, 0, 0x13F, 199, 0);
    colours_[0] = 0;
}

// h41: the last picture whole: 20,000 a life, 24 a second under 90
// minutes, the self-development bonus; the finale.
void PsycheStage::become_whole() {
    humming_ = -1;
    sound_.set_fast(false);
    lives_bonus_ = std::uint32_t(lives_) * 20000;
    time_bonus_ = time_ < 0x1518 ? 0x18 * (0x1518 - time_) : 0;
    score_ += time_bonus_ + lives_bonus_ + bonus_;
    whole_ = true;
    draw_whole();
    sound_.start_finale();
}

// $501424: each point drifts; now and then (a draw & 15 == 0) one gets a new
// speed (point 0 from $502108) across or down; all move with point 0's speed
// but every eighth pass each with its own, bouncing off the picture's
// edges. Then the four-sided shape, mirrored four ways, in the pen, which
// counts 1-15 round, 0 (wiping) for 16 passes in 64.
void PsycheStage::kaleidoscope() {
    if (++pen_ > 0xF) pen_ = 1;
    ++kaleido_count_;
    if ((kaleido_count_ & 0x30) == 0) pen_ = 0;
    if ((kaleido_count_ & 0x3F) == 0) write_word();
    for (int i = 0; i < 4; ++i) {
        const int bounce = (steer_.next() & 7) + 1;
        if ((wobble_.next() & 0xF) == 0) {
            int v = steer_.next() & 7;
            if (i == 0) v = p_.s32(speeds + std::uint32_t(v * 4));
            if (steer_.next() & 0x400) v = -v;
            if ((flip_.next() & 1) == 0) vy_[std::size_t(i)] = v;
            else vx_[std::size_t(i)] = v;
        }
        int j = 0;
        if ((kaleido_count_ & 7) == 0) {
            j = i;
        } else if (vx_[0] == 0 && vy_[0] == 0) {
            vx_[0] = 1;
        }
        auto& x = x_[std::size_t(i)];
        auto& y = y_[std::size_t(i)];
        x += vx_[std::size_t(j)];
        if (x < 0) {
            x = 0;
            vx_[std::size_t(i)] = bounce;
        } else if (x > 0x8B) {
            x = 0x8B;
            vx_[std::size_t(i)] = -bounce;
        }
        y += vy_[std::size_t(j)];
        if (y < 0) {
            y = 0;
            vy_[std::size_t(i)] = bounce;
        } else if (y > 0x77) {
            y = 0x77;
            vy_[std::size_t(i)] = -bounce;
        }
    }
    const auto shape = [this](bool flip_x, bool flip_y) { // $5016A6
        std::vector<screen::AreaPoint> points;
        for (int i = 0; i < 4; ++i)
            points.push_back({flip_x ? 0x8B - x_[std::size_t(i)] : x_[std::size_t(i)],
                              flip_y ? 0x77 - y_[std::size_t(i)] : y_[std::size_t(i)]});
        screen::fill_area(points, [this](int x, int y) { back_.plot(x, y, pen_); });
    };
    shape(false, false);
    shape(true, false);
    shape(true, true);
    shape(false, true);
}

// $500E3C: one of P's 32 words at a random place in the picture, kept
// inside its right edge, in pen 7 or 15.
void PsycheStage::write_word() {
    const auto& word = assets_.words[std::size_t(flip_.next() & 0x1F)];
    const int length = int(std::min<std::size_t>(word.size(), 20));
    const int y = (wobble_.next() & 0x3F) + 0x1C;
    int x = steer_.next() & 0x7F;
    if (length * 8 + x > 0x8B) x = 0x8B - length * 8;
    const int pen = (flip_.next() & 0x200) ? 0xF : 7;
    back_.text(x, y, word.substr(0, std::size_t(length)), pen);
}

// $500590: silence, the window down under the screen's title.
void PsycheStage::pause() {
    paused_ = true;
    sound_.stop();
}

// $5005F0: the window back up, 30 frames, the screen cleared and the panel
// drawn again; the sound back.
void PsycheStage::resume() {
    paused_ = false;
    for (int f = 0; f < 29; ++f) sequence_.push_back([] {});
    sequence_.push_back([this] {
        screen_.fill(0, top_, 0x13F, 199, 0);
        draw_panel();
        sound_.start();
    });
}

platform::Image PsycheStage::picture(bool sprites) const {
    std::vector<screen::Rgb4> colours;
    for (const auto c : colours_) colours.push_back(c & 0xFFF);
    screen::Canvas canvas = screen_;
    screen::Canvas bar;
    bar_.draw(bar, title_bar_pen);
    const auto& close = amiga::window_close_gadget();
    for (int y = 0; y < close.height; ++y)
        for (int x = 0; x < close.width; ++x)
            bar.plot(x, y, (close.planes[0][std::size_t(y)] >> (15 - x) & 1) | (close.planes[1][std::size_t(y)] >> (15 - x) & 1) << 1);
    if (paused_) {
        // The screen's title bar (rows 0-9), the window moved down over rows 10-21.
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 320; ++x) canvas.plot(x, y + 10, bar.pen(x, y));
        canvas.fill(0, 0, 319, 9, title_bar_pen);
        std::string title;
        for (std::uint32_t c = screen_title; p_.u8(c) != 0; ++c) title.push_back(char(p_.u8(c)));
        canvas.text(2, 7, title, title_text_pen);
        for (const auto& gadget : amiga::screen_depth_gadgets()) {
            const int x0 = 319 + gadget.left;
            for (int y = 0; y < gadget.height; ++y)
                for (int x = 0; x < gadget.width; ++x)
                    canvas.plot(x0 + x, y, int(gadget.planes[0][std::size_t(y)] >> (31 - x) & 1) |
                                               int(gadget.planes[1][std::size_t(y)] >> (31 - x) & 1) << 1);
        }
        return canvas.picture(colours);
    }
    if (outcome_ == Outcome::playing)
        for (int y = 0; y < top_; ++y)
            for (int x = 0; x < 320; ++x) canvas.plot(x, y, bar.pen(x, y));
    auto image = canvas.picture(colours);
    if (!sprites || outcome_ != Outcome::playing) return image;
    // The hand: sprites 2 and 3 side by side (colours 21-23).
    for (int s = 0; s < 2; ++s)
        for (int y = 0; y < 22; ++y)
            for (int x = 0; x < 16; ++x) {
                const int v = cursor_[std::size_t(s)].at(x, y), sx = cursor_x_ + s * 16 + x, sy = cursor_y_ + top_ + y;
                if (v && sx >= 0 && sx < 320 && sy >= 0 && sy < 200) image.put(sx, sy, screen::to_color(colours[std::size_t(20 + v)]));
            }
    return image;
}

void PsycheStage::describe(const std::function<void(const std::string&, long)>& report) const {
    for (int t = 0; t < 42; ++t) report("place " + std::to_string(t), place_[std::size_t(t)]);
    for (int i = 0; i < 7; ++i) report("slot " + std::to_string(i), slot_[std::size_t(i)]);
    for (int c = 0; c < 7; ++c)
        for (int r = 0; r < 6; ++r) report("grid " + std::to_string(c) + "," + std::to_string(r), grid_[std::size_t(c)][std::size_t(r)]);
    report("target", target_);
    report("held", held_);
    for (int i = 0; i < 7; ++i) report("hint " + std::to_string(i), hint_[std::size_t(i)]);
    report("bar wait", bar_wait_);
    report("pace", bar_pace_);
    for (int i = 0; i < 4; ++i) {
        const auto n = std::to_string(i);
        report("x " + n, x_[std::size_t(i)]);
        report("y " + n, y_[std::size_t(i)]);
        report("vx " + n, vx_[std::size_t(i)]);
        report("vy " + n, vy_[std::size_t(i)]);
    }
    report("pen", pen_);
    report("kaleido count", kaleido_count_);
    report("whole", whole_);
    report("blits", blits_);
    report("wobble", long(wobble_.state));
    report("steer", long(steer_.state));
    report("flip", long(flip_.state));
    report("shuffle", long(shuffle_random_.state));
    report("shuffle extra", long(shuffle_extra_.state));
    report("cursor x", cursor_x_);
    report("cursor y", cursor_y_);
    report("score", long(score_));
    report("bonus", long(bonus_));
    for (int c = 0; c < 32; ++c) report("colour " + std::to_string(c), colours_[std::size_t(c)]);
}

} // namespace mindwalker::psyche
