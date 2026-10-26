#include "marble_madness_amiga/game/view.hpp"
#include "marble_madness_amiga/data/preview.hpp"
#include "ui/draw.hpp"
#include <algorithm>
#include <string>

namespace marble_madness::game {
namespace {

// Program data (research layout).
constexpr std::uint32_t plain_marble = 0x1068C8;               // draw record of the rolling marble
constexpr std::array<std::uint32_t, 2> plain_ramps = {0x106828, 0x10682E}; // per player
constexpr std::array<std::uint32_t, 2> animation_ramps = {0x106834, 0x10683A};
constexpr std::uint32_t drop_mark = 0x106918;       // draw records of the drop markers
constexpr std::uint32_t drop_mark_zone3 = 0x106908;

std::array<data::Rgb4, 3> ramp_at(const amiga::LoadedImage& program, std::uint32_t at) {
    return {program.u16(at), program.u16(at + 2), program.u16(at + 4)};
}

} // namespace

View::View(const data::GameFiles& files, const data::Assets& assets, const Race& race)
    : files_(&files), assets_(&assets), map_(&assets.maps[std::size_t(race.course())]), course_(race.course()) {
    canvas_.assign(std::size_t(map_->stored_rows()) * 8 * width, 0);
    for (int row = 0; row < map_->stored_rows(); ++row) {
        paint_row(row);
    }
    top_row_ = race.scroll_lines() >> 3;
    read_display_block(race);
}

void View::read_display_block(const Race& race) {
    const auto& t = files_->track(race.course());
    base_palette_ = map_->palette;
    const std::uint32_t block = t.u32(files_->track_header(race.course()) + 0x10);
    // A copper list (graphics.library CopList: instructions at +$C, count
    // at +$1C, 6 bytes each: kind, register, value); MOVEs to COLOR00-15.
    auto moves = [&](std::uint32_t list) {
        std::vector<Move> out;
        const std::uint32_t at = t.u32(list + 0xC);
        const int count = t.s16(list + 0x1C);
        for (int i = 0; i < count; ++i) {
            const std::uint32_t ins = at + std::uint32_t(i) * 6;
            const int register_offset = t.u16(ins + 2) & 0x1FE;
            if (t.u16(ins) == 0 && register_offset >= 0x180 && register_offset < 0x1A0) {
                out.push_back({ins, (register_offset - 0x180) / 2});
            }
        }
        return out;
    };
    // The list at the top of the course view sets colours 0-9.
    for (const auto& move : moves(t.u32(t.u32(block) + 4))) {
        base_palette_[std::size_t(move.colour)] = t.u16(move.instruction + 4);
    }
    const int count = t.s16(block + 4);
    const std::uint32_t table = t.u32(block + 6);
    for (int i = 0; i < count; ++i) {
        const std::uint32_t entry = table + std::uint32_t(i) * 12;
        bands_.push_back({t.u16(entry), moves(t.u32(entry + 4))});
    }
}

void View::paint_row(int row) {
    if (row < 0 || row >= map_->stored_rows()) {
        return;
    }
    for (int column = 0; column < data::CourseMap::columns; ++column) {
        const auto& tile = map_->tiles[map_->cell(row, column)];
        for (int y = 0; y < 8; ++y) {
            std::copy_n(tile.begin() + y * 8, 8,
                        canvas_.begin() + std::ptrdiff_t((row * 8 + y) * width + column * 8));
        }
    }
}

void View::update(Race& race) {
    const int top = race.scroll_lines() >> 3;
    while (top_row_ < top) {
        ++top_row_;
        paint_row(top_row_ + 32);
    }
    while (top_row_ > top) {
        --top_row_;
        paint_row(top_row_ - 7);
    }
    for (const auto& b : race.regions().take_blits()) {
        blit(b.item, b.x, b.y, race);
    }
    // $10694C draws each dot record's two dots and marks the second one
    // drawn; without a new rotation the next frame shows only the first.
    for (int i = 0; i < race.players(); ++i) {
        auto& m = race.marbles[std::size_t(i)];
        dots_[std::size_t(i)] = m.shine_dots;
        for (auto& d : m.shine_dots) {
            d[0] = d[1] = -1;
        }
    }
}

// $11090C / $112204: a region's picture into the playfield. Rows more
// than $38 lines above the view are cut off; a cell whose mask was built
// keeps the ground where its colour is 0, any other cell is copied whole.
// The picture ends at the playfield's right edge (18 words).
void View::blit(std::uint32_t item, int x, int y, const Race& race) {
    const auto& track = files_->track(race.course());
    const std::uint32_t id = track.u32(item + 8);
    const auto& library = assets_->course_images[std::size_t(race.course())];
    if ((id >> 16) != 0 || (id & 0xFFFF) >= library.cells.size()) {
        return; // other banks are not used by course pictures
    }
    const auto& cell = library.cells[id & 0xFFFF];
    x += track.s8(item);
    y -= track.s8(item + 1);
    int skip = 0;
    if (y < -0x38) {
        skip = -0x38 - y;
        y = -0x38;
    }
    const int lines = cell.height - skip;
    if (lines < 1 || y >= 0x100) {
        return; // the ring line would belong to a row far from the view
    }
    const bool masked = (cell.flags & 1) != 0;
    const int right = std::min(cell.width, width - (x & ~15));
    for (int r = 0; r < lines; ++r) {
        const int line = race.scroll_lines() + y + r;
        if (line < 0 || line >= int(canvas_.size() / width)) {
            continue;
        }
        for (int c = 0; c < right; ++c) {
            const int px = x + c;
            if (px < 0 || px >= width) {
                continue;
            }
            const auto colour = cell.pixels[std::size_t((skip + r) * cell.width + c)];
            if (!masked || colour != 0) {
                canvas_[std::size_t(line * width + px)] = colour;
            }
        }
    }
}

std::vector<std::uint8_t> View::indices(const Race& race) const {
    std::vector<std::uint8_t> out(std::size_t(width) * height, 0);
    const int lines = int(canvas_.size() / width);
    for (int y = 0; y < height; ++y) {
        const int line = race.scroll_lines() + y;
        if (line >= 0 && line < lines) {
            std::copy_n(canvas_.begin() + std::ptrdiff_t(line * width), width, out.begin() + std::ptrdiff_t(y * width));
        }
    }
    return out;
}

platform::Image View::draw(const Race& race) const {
    platform::Image image(width, picture_height);
    const int first_line = race.scroll_lines();
    const int lines = int(canvas_.size() / width);
    for (int y = 0; y < height; ++y) {
        const int line = first_line + y;
        if (line < 0 || line >= lines) {
            continue;
        }
        // The last band starting at or above this line sets colours 10-15
        // (with what the game has written into its copper list since).
        data::Palette palette = base_palette_;
        for (const auto& band : bands_) {
            if (band.first_line > line) {
                break;
            }
            for (const auto& move : band.moves) {
                const auto changed = race.copper_colours().find(move.instruction);
                palette[std::size_t(move.colour)] = changed != race.copper_colours().end()
                                                        ? changed->second
                                                        : files_->track(race.course()).u16(move.instruction + 4);
            }
        }
        for (int x = 0; x < width; ++x) {
            image.put(x, bar_height + y, data::to_color(palette[canvas_[std::size_t(line * width + x)] & 15]));
        }
    }
    draw_bar(image, race);
    draw_regions(image, race);
    draw_drop_marks(image, race);
    for (int i = 0; i < race.players(); ++i) {
        draw_marble(image, race, race.marbles[std::size_t(i)]);
    }
    return image;
}

// The score bar: the score at the left, the seconds left in the middle
// ($10B5D6: x $80 for one player). The game writes them with the
// Kickstart's topaz font ($109C20) in red ($10BBC0) on grey; Joyport's own
// pixel font stands in for the ROM font, and the colours are approximate.
void View::draw_bar(platform::Image& image, const Race& race) const {
    const platform::Color grey{136, 136, 136, 255}, red{255, 0, 0, 255};
    for (int y = 0; y < bar_height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.put(x, y, grey);
        }
    }
    const auto& m = race.marbles[0];
    ui::text(image, std::to_string(m.score), 0x30, 1, 1, red);
    ui::text(image, std::to_string(std::max<int>(0, m.time)), 0x80, 1, 1, red);
}

// $105ECC: the marble's sprite, shown while the race's visibility rule
// ($11F486) lets it: while bouncing, breaking or appearing the current
// animation frame, otherwise the plain marble with its shine; then the wall
// covers are cut out of it. (Not rebuilt yet: the shadow and the respawn
// sparkles.)
void View::draw_marble(platform::Image& image, const Race& race, const Marble& m) const {
    if (m.status == 0 || m.sprite_flag == 0) {
        return;
    }
    const auto& program = files_->program();
    const auto& marble_data = files_->marble_data();
    const int screen_x = std::int16_t(std::uint32_t(m.screen[0]) >> 16);
    const int screen_y = std::int16_t(m.screen[0]);
    const auto player = std::size_t(m.player & 1);
    if (m.sparkle_count != 0) {
        // $111474: the respawn sparkles instead of the marble, each a
        // one-cell sprite in the marble's colours (no covers).
        const auto ramp = ramp_at(program, plain_ramps[player]);
        for (std::size_t i = 0; i < m.sparkle_count; ++i) {
            const auto& s = m.sparkles[i];
            if (s.cell < 0 || std::size_t(s.cell) >= assets_->marble.cells.size()) {
                continue;
            }
            Sprite sprite;
            sprite.x = s.x;
            sprite.y = s.y;
            sprite.columns = 1;
            const auto& cell = assets_->marble.cells[std::size_t(s.cell)];
            sprite.rows = cell.height;
            sprite.words.assign(std::size_t(sprite.rows), {});
            for (int y = 0; y < cell.height; ++y) {
                for (int x = 0; x < std::min(cell.width, 16); ++x) {
                    const auto colour = cell.pixels[std::size_t(y * cell.width + x)];
                    const auto bit = std::uint16_t(0x8000 >> x);
                    auto& w = sprite.at(0, y);
                    w[0] = std::uint16_t(w[0] | ((colour & 1) != 0 ? bit : 0));
                    w[1] = std::uint16_t(w[1] | ((colour & 2) != 0 ? bit : 0));
                }
            }
            draw_sprite(image, sprite, ramp);
        }
        return;
    }
    if (m.mode == Marble::respawning) {
        return;
    }
    // The points just won bob above the marble (the plain marble's colours).
    if (m.bonus_on && m.mode != Marble::frozen && m.mode != Marble::breaking && m.bonus_picture != 0) {
        const int lift = m.mode == Marble::finishing ? 0x20 : 0x10;
        draw_record(image, marble_data, m.bonus_picture, screen_x + m.bonus_wobble, screen_y + lift,
                    ramp_at(program, plain_ramps[player]), race.course());
    }
    const bool animating = m.mode == Marble::breaking || m.mode == Marble::appearing ||
                           m.mode == Marble::landing || m.mode == Marble::bumped;
    Sprite sprite;
    std::array<data::Rgb4, 3> ramp{};
    if (animating) {
        // (With no frame the original cuts the covers out of the last
        // sprite it built, the points picture; not copied.)
        if (m.animation == 0 ||
            !make_sprite(sprite, marble_data, marble_data.u32(m.animation), screen_x, screen_y, race.course())) {
            return;
        }
        ramp = ramp_at(program, animation_ramps[player]);
    } else {
        if (!make_sprite(sprite, program, plain_marble, screen_x, screen_y, race.course())) {
            return;
        }
        ramp = ramp_at(program, plain_ramps[player]);
        // $10694C: the shine, 2 x 2 dots in sprite colour 1 (one pixel at
        // the right edge), only on the sprite's first rows but two.
        for (const auto& d : dots_[player]) {
            for (int k : {2, 0}) {
                const int x = d[std::size_t(k)], y = d[std::size_t(k + 1)];
                if (x < 0 || x > 15 || y < 0 || y >= sprite.rows - 2) {
                    continue;
                }
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2 && x + dx < 16; ++dx) {
                        auto& w = sprite.at(0, y + dy);
                        const auto bit = std::uint16_t(0x8000 >> (x + dx));
                        w[0] = std::uint16_t(w[0] | bit);
                        w[1] = std::uint16_t(w[1] & ~bit);
                    }
                }
            }
        }
    }
    for (const auto& cover : m.covers) {
        if (cover.cell == -1) {
            break;
        }
        cut_cover(sprite, cover, race.course());
    }
    draw_sprite(image, sprite, ramp);
}

bool View::make_sprite(Sprite& sprite, const amiga::LoadedImage& source, std::uint32_t record, int screen_x,
                       int screen_y, data::Course course) const {
    if (record == 0xFFFFFFFF || !source.contains(record, 16)) {
        return false;
    }
    const std::uint32_t cell_id = source.u32(record + 8);
    if (cell_id == 0xFFFFFFFF) {
        return false;
    }
    const auto& library = (cell_id >> 16) == 1 ? assets_->marble : assets_->course_images[std::size_t(course)];
    const std::size_t first = cell_id & 0xFFFF;
    if (first >= library.cells.size()) {
        return false;
    }
    sprite.x = screen_x + source.s8(record);
    sprite.y = screen_y + source.s8(record + 1);
    sprite.columns = std::max(1, int(source.s8(record + 2)));
    sprite.rows = library.cells[first].height;
    sprite.words.assign(std::size_t(sprite.columns * sprite.rows), {});
    for (int c = 0; c < sprite.columns && first + std::size_t(c) < library.cells.size(); ++c) {
        const auto& cell = library.cells[first + std::size_t(c)];
        for (int y = 0; y < std::min(cell.height, sprite.rows); ++y) {
            auto& w = sprite.at(c, y);
            for (int x = 0; x < std::min(cell.width, 16); ++x) {
                const auto colour = cell.pixels[std::size_t(y * cell.width + x)];
                const auto bit = std::uint16_t(0x8000 >> x);
                w[0] = std::uint16_t(w[0] | ((colour & 1) != 0 ? bit : 0));
                w[1] = std::uint16_t(w[1] | ((colour & 2) != 0 ? bit : 0));
            }
        }
    }
    return true;
}

// $1114F8: clears a cover's pixels from the sprite. The cover is a 16-pixel
// two-plane cell whose rows run down from height (y + its rows). Shifted by
// 1-7 pixels the game shifts each row's two plane words as one long, so
// plane 0's last bits would fall into plane 1 (the covers are 8 pixels
// wide, so this never shows); by 8-15 pixels word by word, the rest going
// into the sprite's next column.
void View::cut_cover(Sprite& sprite, const Marble::Cover& cover, data::Course course) const {
    const auto& covers = assets_->wall_covers[std::size_t(course)];
    const auto index = std::size_t(cover.cell & 0xFFFF);
    if (index >= covers.cells.size()) {
        return;
    }
    const auto& cell = covers.cells[index];
    int dx = cover.x - sprite.x;
    if (dx < -7) {
        return;
    }
    int column = 0;
    for (; dx > 15; dx -= 16) {
        ++column;
    }
    if (column >= sprite.columns) {
        return;
    }
    const int t = sprite.y - (cell.height + cover.y);
    const int skip = std::max(0, -t);
    int rows = cell.height - skip;
    if (rows == 0 || cell.height < skip) {
        return;
    }
    const int line = std::max(0, t);
    if (line + rows > sprite.rows) {
        rows = sprite.rows - line;
    }
    if (rows < 1) {
        return;
    }
    auto cover_words = [&](int row) {
        std::array<std::uint16_t, 2> w{};
        for (int x = 0; x < std::min(cell.width, 16); ++x) {
            const auto colour = cell.pixels[std::size_t(row * cell.width + x)];
            const auto bit = std::uint16_t(0x8000 >> x);
            w[0] = std::uint16_t(w[0] | ((colour & 1) != 0 ? bit : 0));
            w[1] = std::uint16_t(w[1] | ((colour & 2) != 0 ? bit : 0));
        }
        return w;
    };
    if (dx >= 0) {
        if (dx < 8) {
            for (int i = 0; i < rows; ++i) {
                const auto c = cover_words(skip + i);
                const std::uint32_t mask = (std::uint32_t(c[0]) << 16 | c[1]) >> dx;
                auto& w = sprite.at(column, line + i);
                w[0] = std::uint16_t(w[0] & ~(mask >> 16));
                w[1] = std::uint16_t(w[1] & ~mask);
            }
            return;
        }
        for (int i = 0; i < rows; ++i) {
            const auto c = cover_words(skip + i);
            auto& w = sprite.at(column, line + i);
            w[0] = std::uint16_t(w[0] & ~(c[0] >> dx));
            w[1] = std::uint16_t(w[1] & ~(c[1] >> dx));
        }
        if (++column >= sprite.columns) {
            return;
        }
        dx -= 16;
    }
    for (int i = 0; i < rows; ++i) {
        const auto c = cover_words(skip + i);
        auto& w = sprite.at(column, line + i);
        w[0] = std::uint16_t(w[0] & ~(c[0] << -dx));
        w[1] = std::uint16_t(w[1] & ~(c[1] << -dx));
    }
}

void View::draw_sprite(platform::Image& image, const Sprite& sprite, const std::array<data::Rgb4, 3>& ramp) const {
    const int y0 = 0x108 - sprite.y;
    for (int c = 0; c < sprite.columns; ++c) {
        for (int y = 0; y < sprite.rows; ++y) {
            const auto& w = sprite.words[std::size_t(c * sprite.rows + y)];
            for (int x = 0; x < 16; ++x) {
                const auto bit = std::uint16_t(0x8000 >> x);
                const int colour = ((w[0] & bit) != 0 ? 1 : 0) | ((w[1] & bit) != 0 ? 2 : 0);
                if (colour != 0) {
                    image.put(sprite.x + c * 16 + x, y0 + y, data::to_color(ramp[std::size_t(colour - 1)]));
                }
            }
        }
    }
}

// $106396: Practice's drop markers, a sprite in the player's colours.
void View::draw_drop_marks(platform::Image& image, const Race& race) const {
    const auto& program = files_->program();
    for (int i = 0; i < race.players(); ++i) {
        const auto& mark = race.drop_marks()[std::size_t(i)];
        const int x = std::int16_t(std::uint32_t(mark.screen) >> 16), y = std::int16_t(mark.screen);
        if (mark.kind == -1 || y <= -0x40 || y >= 0x100) {
            continue;
        }
        draw_record(image, program, mark.kind == 0 ? drop_mark : drop_mark_zone3, x, y,
                    ramp_at(program, plain_ramps[std::size_t(i)]), race.course());
    }
}

// $106586: unpictured regions with a sprite (display type 5, region code
// 5: the goal flags) show their current frame's draw record, with the
// record's own colours.
void View::draw_regions(platform::Image& image, const Race& race) const {
    const auto& t = files_->track(race.course());
    for (const auto& r : race.regions().all()) {
        if (!r.active || r.kind == 1 || r.code != 5 || r.screen_y <= -0x40 || r.screen_y >= 0x100) {
            continue;
        }
        if (!t.contains(r.shown_frame, 4)) {
            continue;
        }
        const std::uint32_t record = t.u32(r.shown_frame);
        if (!t.contains(record, 16)) {
            continue;
        }
        const std::uint32_t colours = t.u32(record + 12);
        if (!t.contains(colours, 6)) {
            continue;
        }
        draw_record(image, t, record, r.screen_x - 1, r.screen_y, ramp_at(t, colours), race.course());
    }
}

void View::draw_record(platform::Image& image, const amiga::LoadedImage& source, std::uint32_t record, int screen_x,
                       int screen_y, const std::array<data::Rgb4, 3>& ramp, data::Course course) const {
    if (record == 0xFFFFFFFF || !source.contains(record, 16)) {
        return;
    }
    const std::uint32_t cell_id = source.u32(record + 8);
    if (cell_id == 0xFFFFFFFF) {
        return;
    }
    const int bank = int(cell_id >> 16);
    const auto& library = bank == 1 ? assets_->marble : assets_->course_images[std::size_t(course)];
    const int columns = std::max(1, int(source.s8(record + 2)));
    const int x0 = screen_x + source.s8(record);
    const int y0 = 0x108 - (source.s8(record + 1) + screen_y);
    for (int c = 0; c < columns; ++c) {
        const std::size_t index = (cell_id & 0xFFFF) + std::size_t(c);
        if (index >= library.cells.size()) {
            break;
        }
        const auto& cell = library.cells[index];
        for (int y = 0; y < cell.height; ++y) {
            for (int x = 0; x < cell.width; ++x) {
                const auto colour = cell.pixels[std::size_t(y * cell.width + x)] & 3;
                if (colour != 0) {
                    image.put(x0 + c * 16 + x, y0 + y, data::to_color(ramp[colour - 1]));
                }
            }
        }
    }
}

} // namespace marble_madness::game
