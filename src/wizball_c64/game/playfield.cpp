#include "wizball_c64/game/playfield.hpp"

namespace wizball::game {
namespace {
constexpr int first_star_code = 0x49, last_star_code = 0x5A, star_columns = 18;
// $79C2: the sky pattern's column offset in tile rows 1-3 (code constants).
constexpr int tile_row_sky_offset[4] = {0, 7, 0x10, 0x0B};
} // namespace

Playfield::Playfield(const data::Assets&, const data::GameImage& image) : image_(image) {
    tile_base_ = image.word(0xB62B);
    star_counter_ = image.byte(0xB27A);
    for (int k = 0; k < sky_bands; ++k) {
        const auto r = image.bytes(std::uint16_t(0xB1B4 + 5 * k), 5);
        stars_[std::size_t(k)] = {r[0], r[1], r[2], r[3], r[4]};
    }
    role_ = {0, 1, 2};
    progress_ = {4, 4, 4};
}

void Playfield::sky_put(int offset, std::uint8_t value) {
    const int i = offset - 8;
    if (i >= 0 && i < int(sky_.size())) sky_[std::size_t(i)] = value;
}

Glyph Playfield::sky_glyph(int code) const {
    Glyph g{};
    for (int y = 0; y < 8; ++y) g[std::size_t(y)] = sky_[std::size_t((code - 1) * 8 + y)];
    return g;
}

// The map through its row pointers ($0460/$0470), indexed with the 8-bit
// column as the game does ($78E4: LDA ($60),Y), so column $FF of a left
// half reads 255 bytes on, not the tile before.
std::uint8_t Playfield::map_tile(int row, std::uint8_t column) const {
    const auto pointer = std::uint16_t(image_.byte(std::uint16_t(0x0460 + row)) | image_.byte(std::uint16_t(0x0470 + row)) << 8);
    return image_.byte(std::uint16_t(pointer + column));
}

std::array<std::uint8_t, Playfield::rows> Playfield::map_column(std::uint8_t tile_column, int tile_char) const {
    std::array<std::uint8_t, rows> out{};
    for (int k = 0; k < 4; ++k) {
        const int tile = map_tile(map_row_ + k, tile_column);
        const auto address = std::uint16_t(tile_base_ + 25 * tile + tile_char);
        for (int y = 0; y < 5; ++y) out[std::size_t(k * 5 + y)] = image_.byte(std::uint16_t(address + 5 * y));
    }
    return out;
}

// $79C2: empty cells take the sky code of this column: in each tile row the
// bottom character row gets the counter's code (moved on by the tile row's
// offset), each row above it 18 codes lower.
void Playfield::fill_sky(std::array<std::uint8_t, rows>& column, std::uint8_t counter) const {
    for (int k = 0; k < 4; ++k) {
        int code = counter + tile_row_sky_offset[k];
        if (code > last_star_code) code -= star_columns;
        for (int y = 4; y >= 0; --y) {
            auto& cell = column[std::size_t(k * 5 + y)];
            if (cell == 0) cell = std::uint8_t(code);
            code -= star_columns;
        }
    }
}

void Playfield::step_counter(int direction) {
    if (direction > 0) {
        if (++star_counter_ > last_star_code) star_counter_ = first_star_code;
    } else {
        if (--star_counter_ < first_star_code) star_counter_ = last_star_code;
    }
}

void Playfield::clear_sky(RandomSource& random) {
    sky_.fill(0);
    for (int k = 0; k < sky_bands; ++k) { // $7540: one fixed dot per band
        const auto shape = image_.byte(std::uint16_t(0xB891 + (random.next() & 7)));
        sky_put(8 + 0x90 * k + (random.next() & 0x7F), shape);
    }
}

void Playfield::start(int place, RandomSource& random) {
    // $7586: look for two empty tiles side by side in the strip's top row,
    // from a random column (right, then left), and start the view there.
    const int entry = 0xB2F0 + 4 * place;
    const int row = image_.byte(std::uint16_t(entry + 1));
    const auto pointer = std::uint16_t(image_.byte(std::uint16_t(0x0460 + row)) | image_.byte(std::uint16_t(0x0470 + row)) << 8);
    const auto base = std::uint16_t(pointer + std::uint8_t(image_.byte(std::uint16_t(entry)) + 6));
    std::uint8_t y = random.next() & 0x1F;
    bool leftwards = false;
    for (;;) {
        if (image_.byte(std::uint16_t(base + y)) == 0 && image_.byte(std::uint16_t(base + std::uint8_t(y + 1))) == 0) {
            ++y; // the game stops on the second empty tile ($75B4: INY before the test)
            break;
        }
        if (leftwards) {
            if (--y == 0xFF) {
                y = 0;
                leftwards = false;
            }
        } else if (++y == 0x24) {
            leftwards = true;
        }
    }
    view_column_ = std::uint8_t(y * 5 + 2);
    screen_view_ = view_column_;
    wiz_offset_ = 0;
    wrap_left_ = 0xFF;
}

void Playfield::draw_first_screen(int place, int wrap_tiles, bool blank) {
    wrap_left_ = std::uint8_t(wrap_tiles);
    blank_ = blank;
    screen_view_ = view_column_;
    // $75EA: draw into the screen not shown now; the other two are prepared.
    int target;
    if (shown_ != 0) {
        target = 0;
        role_ = {0, 1, 2};
        progress_ = {4, 0, 0};
    } else {
        target = 1;
        role_ = {1, 0, 2};
        progress_ = {0, 4, 0};
    }
    const int entry = 0xB2F0 + 4 * place;
    tile_column_ = image_.byte(std::uint16_t(entry));
    map_row_ = image_.byte(std::uint16_t(entry + 1));
    tile_char_ = image_.byte(std::uint16_t(entry + 2));
    // $7644: the start position plus the view column, in tiles and characters.
    unsigned position = std::uint8_t(tile_char_ + view_column_);
    tile_column_ = std::uint8_t(tile_column_ + position / 5);
    tile_char_ = std::uint8_t(position % 5);

    auto column = tile_column_;
    int ch = tile_char_;
    auto row = map_row_;
    auto wrap = wrap_left_;
    auto& screen = screens_[std::size_t(target)];
    for (int s = 0; s < columns; ++s) {
        if (++ch >= 5) { // $768A
            ++column;
            if (--wrap == 0) {
                row = std::uint8_t(row + 4);
                column = std::uint8_t(column - 4);
            }
            ch = 0;
        }
        const auto saved_row = map_row_;
        map_row_ = row;
        auto cells = map_column(column, ch);
        if (blank) cells.fill(0); // $766B: a cleared place shows no landscape
        map_row_ = saved_row;
        step_counter(+1); // $7978
        fill_sky(cells, star_counter_);
        for (int r = 0; r < rows; ++r) screen.codes[std::size_t(r * columns + s)] = cells[std::size_t(r)];
    }
    map_row_ = row; // the wrap may have moved it (bonus stage)
    wrap_left_ = wrap;
    screen.left = 0;
    shown_ = target; // $76C9 (the fine scroll $B1FC stays as it was)
}

void Playfield::build_prepared(int index) {
    auto& target = screens_[std::size_t(index)];
    const auto& from = screens_[std::size_t(shown_)];
    std::array<std::uint8_t, rows> cells;
    if (role_[std::size_t(index)] == 2) { // shifted left, new column on the right ($78AE, $799A)
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c + 1 < columns; ++c)
                target.codes[std::size_t(r * columns + c)] = from.codes[std::size_t(r * columns + c + 1)];
        int ch = tile_char_ + 1;
        auto column = std::uint8_t(tile_column_ + 8);
        if (ch >= 5) {
            ++column;
            ch -= 5;
        }
        cells = map_column(column, ch);
        step_counter(+1);
        auto counter = star_counter_;
        if (++counter > last_star_code) counter = first_star_code;
        fill_sky(cells, counter);
        for (int r = 0; r < rows; ++r) target.codes[std::size_t(r * columns + columns - 1)] = cells[std::size_t(r)];
    } else { // shifted right, new column on the left ($78CF, $7989)
        for (int r = 0; r < rows; ++r)
            for (int c = columns - 1; c > 0; --c)
                target.codes[std::size_t(r * columns + c)] = from.codes[std::size_t(r * columns + c - 1)];
        cells = map_column(tile_column_, tile_char_);
        step_counter(-1);
        fill_sky(cells, star_counter_);
        for (int r = 0; r < rows; ++r) target.codes[std::size_t(r * columns)] = cells[std::size_t(r)];
    }
}

void Playfield::prepare_step() {
    for (int index = 2; index >= 0; --index) { // $77F7 looks at the third screen first
        if (progress_[std::size_t(index)] >= 4) continue;
        const int chunk = progress_[std::size_t(index)]++;
        if (chunk == 3) build_prepared(index);
        return;
    }
}

int Playfield::step_fine(int speed) {
    // $7BCB: 8.8 steps from the speed tables.
    int pixel = fine_ & 7;
    if (speed >= 0) {
        const unsigned sum = fraction_ + image_.byte(std::uint16_t(0xB33D + speed));
        fraction_ = std::uint8_t(sum);
        if (sum > 0xFF) {
            ++fine_;
            ++pixel;
        }
        const auto whole = image_.byte(std::uint16_t(0xB344 + speed));
        fine_ = std::uint8_t(fine_ + whole);
        pixel += whole;
    } else {
        const int y = speed + 6;
        const unsigned sum = fraction_ + image_.byte(std::uint16_t(0xB34B + y));
        fraction_ = std::uint8_t(sum);
        if (sum > 0xFF) {
            --fine_;
            --pixel;
        }
        const auto whole = image_.byte(std::uint16_t(0xB351 + y));
        fine_ = std::uint8_t(fine_ - whole);
        pixel -= whole;
    }
    return pixel;
}

void Playfield::scroll(int speed) {
    step_ = 0;
    int direction = 0;
    int pixel = step_fine(speed);
    pixel_delta_ = std::int8_t(std::uint8_t(fine_ - last_fine_));
    last_fine_ = fine_;
    // $7AB9: crossing a character boundary moves the view column; inside the
    // range 0-$BA it also moves the map position and switches screens.
    const auto signed_pixel = std::int8_t(std::uint8_t(pixel));
    if (signed_pixel < 0) {
        --view_column_;
        if (view_column_ < 0xBB && !at_end_) {
            direction = 1;
            if (--tile_char_ == 0xFF) {
                --tile_column_;
                tile_char_ = 4;
            }
        }
    } else if (std::int8_t(std::uint8_t(pixel - 8)) >= 0) {
        ++view_column_;
        if (view_column_ < 0xBB && !at_end_) {
            direction = 2;
            if (++tile_char_ == 5) {
                ++tile_column_;
                tile_char_ = 0;
            }
        }
    }
    if (view_column_ >= 0xBB) { // $7B8B: the view stops; the wiz moves on screen instead
        at_end_ = true;
        if (view_column_ >= 0xDC) {
            const int a = ((0xFF - view_column_) * 8 + ((fine_ & 7) ^ 7)) & 0xFF;
            wiz_offset_ = std::int8_t(std::uint8_t(-a));
        } else {
            wiz_offset_ = std::int8_t(std::uint8_t((view_column_ - 0xBB) * 8 + (fine_ & 7)));
        }
        return;
    }
    fine_scroll_ = fine_ & 7;
    at_end_ = false;
    if (direction == 0) return;
    screen_view_ = view_column_;
    // $7B2A: the screen prepared for this direction is shown; the old one
    // serves the other direction; the third is rebuilt for this direction.
    const int opposite = direction == 2 ? 1 : 2;
    const auto roles = role_;
    for (int i = 0; i < 3; ++i) {
        if (roles[std::size_t(i)] == 0) role_[std::size_t(i)] = std::uint8_t(opposite);
        else if (roles[std::size_t(i)] == direction) {
            role_[std::size_t(i)] = 0;
            shown_ = i;
        } else if (roles[std::size_t(i)] == opposite) {
            role_[std::size_t(i)] = std::uint8_t(direction);
            progress_[std::size_t(i)] = 0;
        }
    }
    step_ = direction == 2 ? 2 : -1;
}

void Playfield::animate_stars(int speed, RandomSource& random) {
    for (int k = 0; k < sky_bands; ++k) {
        auto& s = stars_[std::size_t(k)];
        const int base = 0x0A + 0x90 * k;
        sky_put(base + s.offset, 0);
        if (speed >= 0) {
            const int y = s.speed_row | speed;
            const auto v = std::uint8_t(s.position - image_.byte(std::uint16_t(0xB8A1 + y)));
            s.position = v & 0x7F;
            if (s.position != v) {
                s.offset = std::uint8_t(s.offset - 8);
                if (s.offset >= 0x90) s.offset = 0x88;
            }
        } else {
            const int y = ((std::uint8_t(speed) ^ 0xFF) | s.speed_row) + 1;
            const auto v = std::uint8_t(s.position + image_.byte(std::uint16_t(0xB8A1 + y)));
            s.position = v & 0x7F;
            if (s.position != v) {
                s.offset = std::uint8_t(s.offset + 8);
                if (s.offset >= 0x90) s.offset = 0;
            }
        }
        if ((++s.clock & 3) == 0) s.twinkle = random.next() & 0x0C;
        const int pixel = (s.position >> 5) & 3;
        sky_put(base + s.offset, image_.byte(std::uint16_t(0xB891 + (pixel | s.twinkle))));
    }
}

std::uint8_t Playfield::code(int column, int row) const {
    return screens_[std::size_t(shown_)].codes[std::size_t(row * columns + column)];
}

void Playfield::put(int screen, int column, int row, std::uint8_t value) {
    if (column < 0 || column >= columns || row < 0 || row >= rows) return;
    screens_[std::size_t(screen)].codes[std::size_t(row * columns + column)] = value;
}

std::uint8_t Playfield::code_on(int screen, int column, int row) const {
    return screens_[std::size_t(screen)].codes[std::size_t(row * columns + column)];
}

int Playfield::sky_counter_at(int column) const {
    // fill_sky backwards from any sky cell of the column.
    const auto& screen = screens_[std::size_t(shown_)];
    for (int k = 0; k < 4; ++k)
        for (int y = 0; y < 5; ++y) {
            const int code = screen.codes[std::size_t((k * 5 + y) * columns + column)];
            if (code < 1 || code > last_star_code) continue;
            int counter = code + star_columns * (4 - y) - tile_row_sky_offset[k];
            while (counter < first_star_code) counter += star_columns;
            while (counter > last_star_code) counter -= star_columns;
            return counter;
        }
    return -1;
}

std::array<std::uint8_t, Playfield::rows> Playfield::side_column(int column) const {
    std::array<std::uint8_t, rows> cells{};
    const int view = screen_view_ + column; // the place's character column, as the view counts
    if (!blank_) {
        if (view < 0 || view > 0xBA + columns - 1) return cells;
        // The screen's first column is one character on from the map position.
        const int position = tile_char_ + 1 + column;
        const int tiles = position >= 0 ? position / 5 : -((4 - position) / 5);
        cells = map_column(std::uint8_t(tile_column_ + tiles), position - 5 * tiles);
    }
    // (A cleared place, the bonus wave's too, is all sky, as $766B leaves it.)
    // The sky pattern runs on from the nearest edge column that shows sky.
    const int edge = column < 0 ? 0 : columns - 1, inward = column < 0 ? 1 : -1;
    int counter = -1, from = edge;
    for (; from >= 0 && from < columns && counter < 0; from += inward) counter = sky_counter_at(from);
    if (counter < 0) counter = star_counter_;
    else counter += column - (from - inward);
    counter = first_star_code + ((counter - first_star_code) % star_columns + star_columns) % star_columns;
    fill_sky(cells, std::uint8_t(counter));
    return cells;
}

Playfield::Probe Playfield::probe() const {
    return {tile_column_, tile_char_, fraction_, star_counter_, progress_, role_};
}

} // namespace wizball::game
