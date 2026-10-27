#include "marble_madness_amiga/game/view.hpp"
#include "amiga/topaz.hpp"
#include "marble_madness_amiga/data/preview.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

namespace marble_madness::game {
namespace {

// Program data (research layout).
constexpr std::uint32_t plain_marble = 0x1068C8;               // draw record of the rolling marble
constexpr std::array<std::uint32_t, 2> plain_ramps = {0x106828, 0x10682E}; // per player
constexpr std::array<std::uint32_t, 2> animation_ramps = {0x106834, 0x10683A};
constexpr std::uint32_t broken_steelie_ramp = 0x106840;
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

void View::paint_row(int row, int source) {
    if (source < 0) {
        source = row;
    }
    if (row < 0 || row >= map_->stored_rows() || source >= map_->stored_rows()) {
        return;
    }
    // On Silly the course start lays the rows into the playfield's ring for
    // a scroll one line further than the one it starts with, so its tiles
    // show a line higher than its pictures and sprites (measured: ring line
    // = scroll - $277 there, scroll itself elsewhere).
    const int shift = course_ == data::Course::silly ? -1 : 0;
    const int lines = int(canvas_.size() / width);
    for (int column = 0; column < data::CourseMap::columns; ++column) {
        const auto& tile = map_->tiles[map_->cell(source, column)];
        for (int y = 0; y < 8; ++y) {
            const int line = row * 8 + y + shift;
            if (line < 0 || line >= lines) {
                continue;
            }
            std::copy_n(tile.begin() + y * 8, 8, canvas_.begin() + std::ptrdiff_t(line * width + column * 8));
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
    // Ultimate's moving goal area: the shown picture's rows ($11E588 points
    // the display at the buffer the task rendered them into).
    if (const int shown = race.regions().picture_shown_row(); shown != picture_row_) {
        picture_row_ = shown;
        const int home = race.regions().picture_home_row();
        for (int i = 0; i < picture_rows; ++i) {
            paint_row(home + i, shown + i);
        }
    }
    for (const auto& b : race.take_piston_blits()) {
        blit(b.item, b.x, b.y, race);
    }
    for (const auto& b : race.regions().take_blits()) {
        if (b.wave_end >= 0) {
            blit_wave_end(b.item, b.x, b.y, b.wave_end, race);
        } else {
            if (b.background >= 0) {
                link(b.item, b.background, race);
            }
            blit(b.item, b.x, b.y, race);
        }
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
    const auto linked = links_.find(id & 0xFFFF);
    const data::ImageCell* background =
        linked != links_.end() && linked->second < library.cells.size() ? &library.cells[linked->second] : nullptr;
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
            } else if (background != nullptr && skip + r < background->height && c < background->width) {
                // A linked picture fills in where the cell is clear.
                canvas_[std::size_t(line * width + px)] = background->pixels[std::size_t((skip + r) * background->width + c)];
            }
        }
    }
}

// $112464: Intermediate's wave reaching the top of its ramp. The wave's
// picture is drawn without the pixels the ramp's end piece (course cell
// $F, at screen x $F0) covers, then the end piece is drawn again, lower by
// the phase's offset ($11275C) and that much shorter. The end piece's
// place is a fixed line of the playfield's ring ($158 + offset), so it
// lands where it should only with the view at the course's bottom, which
// is where the wave ends.
void View::blit_wave_end(std::uint32_t item, int x, int y, int phase, const Race& race) {
    constexpr std::uint32_t end_offsets = 0x11275C; // words by phase
    constexpr int end_cell = 0xF, end_x = 0xF0, end_ring_line = 0x158;
    const auto& track = files_->track(race.course());
    const std::uint32_t id = track.u32(item + 8);
    const auto& library = assets_->course_images[std::size_t(race.course())];
    if ((id >> 16) != 0 || (id & 0xFFFF) >= library.cells.size() || std::size_t(end_cell) >= library.cells.size()) {
        return;
    }
    const auto& wave = library.cells[id & 0xFFFF];
    const auto& end = library.cells[std::size_t(end_cell)];
    const int offset = files_->program().s16(end_offsets + std::uint32_t(phase) * 2);
    x += track.s8(item);
    y -= track.s8(item + 1);
    // The end piece's line on the screen: its ring line against the ring's
    // first line (the scroll, modulo the ring's 512 lines).
    int end_y = (end_ring_line + offset - (race.scroll_lines() & 0x1FF)) & 0x1FF;
    if (end_y >= 0x100) {
        end_y -= 0x200;
    }
    const int lines = int(canvas_.size() / width);
    auto put = [&](int sx, int sy, std::uint8_t colour) {
        const int line = race.scroll_lines() + sy;
        if (sx >= 0 && sx < width && line >= 0 && line < lines) {
            canvas_[std::size_t(line * width + sx)] = colour;
        }
    };
    auto end_covers = [&](int sx, int sy) {
        const int c = sx - end_x, r = sy - end_y;
        return c >= 0 && c < end.width && r >= 0 && r < end.height && end.pixels[std::size_t(r * end.width + c)] != 0;
    };
    for (int r = 0; r < wave.height; ++r) {
        for (int c = 0; c < wave.width; ++c) {
            const auto colour = wave.pixels[std::size_t(r * wave.width + c)];
            if (colour != 0 && !end_covers(x + c, y + r)) {
                put(x + c, y + r, colour);
            }
        }
    }
    for (int r = 0; r < end.height - offset; ++r) {
        for (int c = 0; c < end.width; ++c) {
            const auto colour = end.pixels[std::size_t(r * end.width + c)];
            if (colour != 0) {
                put(end_x + c, end_y + r, colour);
            }
        }
    }
}

// $110A46: the picture's cell takes another cell as its background (the
// link stays in the cell's record).
void View::link(std::uint32_t item, int background, const Race& race) {
    const auto& track = files_->track(race.course());
    const std::uint32_t id = track.u32(item + 8);
    if ((id >> 16) == 0) {
        links_[id & 0xFFFF] = std::size_t(background & 0xFFFF);
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

// The last band starting at or above a course line sets colours 10-15
// (with what the game has written into its copper list since).
data::Palette View::palette_at(int line, const Race& race) const {
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
    return palette;
}

void View::draw_playfield(platform::Image& image, const Race& race) const {
    const int first_line = race.scroll_lines();
    const int lines = int(canvas_.size() / width);
    for (int y = 0; y < height; ++y) {
        const int line = first_line + y;
        if (line < 0 || line >= lines) {
            continue;
        }
        const data::Palette palette = palette_at(line, race);
        for (int x = 0; x < width; ++x) {
            image.put(x, bar_height + y, data::to_color(palette[canvas_[std::size_t(line * width + x)] & 15]));
        }
    }
}

platform::Image View::draw(const Race& race) const {
    platform::Image image(width, picture_height);
    draw_playfield(image, race);
    draw_bar(image, race);
    draw_sprites(image, race);
    return image;
}

platform::Image View::draw(const Race& race, const Overlay& overlay) const {
    platform::Image image(width, picture_height);
    if (overlay.base == Overlay::Base::black) {
        return image;
    }
    if (overlay.base == Overlay::Base::cleared) {
        // SetRast with pen 4 ($1255D0).
        for (int y = 0; y < height; ++y) {
            const auto colour = data::to_color(palette_at(race.scroll_lines() + y, race)[4]);
            for (int x = 0; x < width; ++x) {
                image.put(x, bar_height + y, colour);
            }
        }
    } else {
        draw_playfield(image, race);
    }
    draw_bar(image, race, &overlay);
    if (overlay.base == Overlay::Base::course) {
        draw_sprites(image, race);
    }
    for (const auto& box : overlay.boxes) {
        const auto colour = data::to_color(palette_at(race.scroll_lines() + box.y, race)[std::size_t(box.pen & 15)]);
        for (int y = std::max(0, box.y); y < std::min(height, box.y + box.height); ++y) {
            for (int x = std::max(0, box.x); x < std::min(width, box.x + box.width); ++x) {
                image.put(x, bar_height + y, colour);
            }
        }
    }
    for (const auto& text : overlay.texts) {
        draw_text(image, text, race);
    }
    for (const auto& ball : overlay.balls) {
        draw_ball(image, ball, race);
    }
    return image;
}

// The program's texts are in the Kickstart's topaz 9 ($109C20 opens it):
// cells of 10 x 9 pixels, the baseline on the seventh line.
void View::put_text(platform::Image& image, const std::string& text, int x, int top, platform::Color pen,
                    std::optional<platform::Color> paper, int first_row, int end_row) const {
    using amiga::Topaz9;
    auto put = [&](int px, int py, platform::Color colour) {
        if (px >= 0 && px < width && py >= first_row && py < end_row) {
            image.put(px, py, colour);
        }
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int cell = x + int(i) * Topaz9::width;
        if (cell >= width) {
            break;
        }
        const auto c = static_cast<unsigned char>(text[i]);
        for (int y = 0; y < Topaz9::height; ++y) {
            for (int dx = 0; dx < Topaz9::width; ++dx) {
                if (Topaz9::pixel(c, dx, y)) {
                    put(cell + dx, top + y, pen);
                } else if (paper) {
                    put(cell + dx, top + y, *paper);
                }
            }
        }
    }
}

void View::draw_text(platform::Image& image, const Text& text, const Race& race) const {
    const data::Palette palette = palette_at(race.scroll_lines() + text.top, race);
    const auto pen = data::to_color(palette[std::size_t(text.pen & 15)]);
    std::optional<platform::Color> paper;
    if (text.paper >= 0) {
        paper = data::to_color(palette[std::size_t(text.paper & 15)]);
    }
    put_text(image, text.text, text.x, bar_height + text.top, pen, paper, bar_height, bar_height + height);
}

// Display type $2C ($111D68, $106632): the marble's draw record $106928
// with one of the colour ramps $106888 lists.
void View::draw_ball(platform::Image& image, const Overlay::Ball& ball, const Race& race) const {
    constexpr std::uint32_t ball_record = 0x106928, ball_ramps = 0x106888;
    const auto& program = files_->program();
    const std::uint32_t ramp = program.u32(ball_ramps + std::uint32_t(ball.ramp) * 4);
    draw_record(image, program, ball_record, ball.x, ball.height, ramp_at(program, ramp), race.course());
}

void View::draw_sprites(platform::Image& image, const Race& race) const {
    // $111BB4: the display list in order, the nearest last ($111C82 picks
    // the drawing by type).
    const auto& list = race.display();
    for (const auto slot : list.order()) {
        if (slot == 0xFF) {
            break;
        }
        const auto& entry = list.slot(slot);
        switch (entry.type) {
        case 1:
            if (entry.index < race.players()) {
                draw_marble(image, race, race.marbles[entry.index & 1]);
            }
            break;
        case 2:
            draw_steelie(image, race, race.steelies[entry.index & 1].body);
            break;
        case 4:
            draw_slinky(image, race, race.slinkies[entry.index & 3]);
            break;
        case 5:
            draw_region(image, race, entry.index);
            break;
        case 0xE:
            draw_acid(image, race, race.acids[entry.index % race.acids.size()]);
            break;
        case 7: case 8: case 9: {
            const auto& c = race.critters[entry.index % race.critters.size()];
            if (c.status != 0) {
                draw_creature(image, race, c.animation, c.screen, -0x10, nullptr);
            }
            break;
        }
        case 0xF: {
            const auto& b = race.birds[entry.index % race.birds.size()];
            if (b.status != 0) {
                draw_creature(image, race, b.animation, b.screen, -0x10, &b.covers);
            }
            break;
        }
        case 0x2A:
            draw_drop_mark(image, race, entry.index & 1);
            break;
        default:
            break; // types of later courses, and regions without a picture
        }
    }
}

// The score bar, a bitmap of its own in colours 10-15 (the copper moves at
// $10BBB8: 10 the grey paper, 11 and 12 the red player's clock and score,
// 13 and 14 the blue player's). $10B5D6 writes the seconds left at x $80
// for one player, two digits with zeros, in colour 11; $10A88A the score
// at the record's x (0 for the red player), seven places, in colour 12.
// Both at y 1, so the text's top is the bar's second line.
void View::draw_bar(platform::Image& image, const Race& race, const Overlay* overlay) const {
    constexpr std::uint32_t bar_colours = 0x10BBBE; // colour 10's value; 6 bytes per colour
    const auto& program = files_->program();
    std::array<platform::Color, 16> colours{};
    for (int c = 10; c < 16; ++c) {
        colours[std::size_t(c)] = data::to_color(program.u16(bar_colours + std::uint32_t(c - 10) * 6));
    }
    for (int y = 0; y < bar_height; ++y) {
        for (int x = 0; x < width; ++x) {
            image.put(x, y, colours[10]); // SetRast(10)
        }
    }
    if (overlay != nullptr && overlay->title) {
        // $10C980: the program's title, a letter every 14 pixels from x
        // $20, in colours 11, 14, 12 and 13 by turns ($10D25C). Those are
        // the bar's players' colours, pulsing on this screen every vertical
        // blank: 11 and 13 (the clocks, $10B946) through 16 shades in 32
        // blanks, 12 and 14 (the scores, $10A9AA) in 64 (tables $10B9E4,
        // $10AA7E for the red player, $10BA04, $10AA9E for the blue).
        constexpr std::uint32_t title = 0x10D258, title_colours = 0x10D25C;
        constexpr std::uint32_t red_clock = 0x10B9E4, red_score = 0x10AA7E, blue_clock = 0x10BA04,
                                blue_score = 0x10AA9E;
        const int b = overlay->title_blanks;
        auto shade = [&](std::uint32_t table, int index) { return data::to_color(program.u16(table + std::uint32_t(index) * 2)); };
        colours[11] = shade(red_clock, (b & 31) >> 1);
        colours[12] = shade(red_score, (b & 63) >> 2);
        colours[13] = shade(blue_clock, (b & 31) >> 1);
        colours[14] = shade(blue_score, (b & 63) >> 2);
        const std::uint32_t text = program.u32(title);
        for (std::uint32_t i = 0; i < 16; ++i) {
            const char c = char(program.u8(text + i));
            if (c == 0) {
                break;
            }
            put_text(image, std::string(1, c), 0x20 + int(i) * 14, 1,
                     colours[program.u8(title_colours + (i & 3)) & 15], std::nullopt, 0, bar_height);
        }
        return;
    }
    const auto& m = race.marbles[0];
    const std::int32_t score = overlay != nullptr && overlay->bar_score ? *overlay->bar_score : m.score;
    const int time = overlay != nullptr && overlay->bar_time ? *overlay->bar_time : m.time;
    // The clock flashes through $10B9E4's shades at low time; it shows at
    // most 99 ($10B654).
    constexpr std::uint32_t red_clock = 0x10B9E4;
    if (m.clock_flash >= 0) {
        colours[11] = data::to_color(program.u16(red_clock + std::uint32_t(m.clock_flash >> 1) * 2));
    }
    char text[16];
    std::snprintf(text, sizeof text, "%7d", int(score));
    put_text(image, text, 0, 1, colours[12], std::nullopt, 0, bar_height);
    std::snprintf(text, sizeof text, "%02d", std::clamp(time, 0, 99));
    put_text(image, text, 0x80, 1, colours[11], std::nullopt, 0, bar_height);
}

// $105ECC: the marble's sprite, shown while the race's visibility rule
// ($11F486) lets it: while bouncing, breaking or dissolving the current
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
    // The shatter's broom sweeping up the shards (+$6C while +$71) and the
    // dizzy marble's stars (+$DE while +$E3), in their records' own colours.
    for (const auto& [list, shown] : {std::pair{m.dust_animation, m.dust_shown}, {m.dizzy_animation, m.dizzy_shown}}) {
        if (shown != 0 && marble_data.contains(list, 4)) {
            const std::uint32_t record = marble_data.u32(list);
            if (marble_data.contains(record, 16)) {
                const std::uint32_t colours = marble_data.u32(record + 12);
                const auto& source = marble_data.contains(colours, 6) ? marble_data : program;
                if (source.contains(colours, 6)) {
                    draw_record(image, marble_data, record, screen_x, screen_y, ramp_at(source, colours),
                                race.course());
                }
            }
        }
    }
    if (m.sparkles_shown) {
        // $111474: the respawn sparkles instead of the marble, each a
        // one-cell sprite in the marble's colours (no covers); none at
        // all once they are all gone.
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
    const bool animating = m.mode == Marble::breaking || m.mode == Marble::dissolving ||
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
    cut_course_parts(sprite, race, m);
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
    const auto& library = library_of(int(cell_id >> 16), course);
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

// $105CB0: course parts drawn into the playfield that a marble passes
// behind are cut out of its sprite through their masks ($111772).
// Beginner: the drawbridge (while its region runs), when the marble is
// not past the bridge's point on both axes. Aerial: the regions of codes
// 11 and 13, then code 10, when the marble is less far down the course (y)
// than the region's point; the pistons ($11E358); the regions of
// code 12 up to 4 further on. (On Aerial the sprite's line is counted one
// lower: $100 - y rather than $FF - y.)
void View::cut_course_parts(Sprite& sprite, const Race& race, const Marble& m) const {
    const auto& regions = race.regions();
    const std::int16_t my = std::int16_t(m.y >> 16);
    auto cut_region = [&](const Region& r, int sprite_top) {
        const auto& track = files_->track(race.course());
        if (!track.contains(r.frame, 4)) {
            return;
        }
        cut_through(sprite, race, track.u32(r.frame), r.screen_x + r.width * 8,
                    r.height * 8 - r.screen_y - regions.view_scroll, sprite_top);
    };
    if (race.course() == data::Course::beginner) {
        const Region* bridge = regions.drawbridge();
        if (bridge != nullptr && ((m.y >> 16) <= (bridge->y >> 16) || (m.x >> 16) <= (bridge->x >> 16))) {
            cut_region(*bridge, 0xFF - sprite.y);
        }
        return;
    }
    if (race.course() != data::Course::aerial) {
        return;
    }
    const int sprite_top = 0x100 - sprite.y;
    const auto& all = regions.all();
    for (const int i : regions.codes11_13()) {
        if (i >= 0 && my < std::int16_t(all[std::size_t(i)].y >> 16)) {
            cut_region(all[std::size_t(i)], sprite_top);
        }
    }
    if (regions.code10() >= 0 && my < std::int16_t(all[std::size_t(regions.code10())].y >> 16)) {
        cut_region(all[std::size_t(regions.code10())], sprite_top);
    }
    // $11E358: a rising group's frame, when the marble is not past the
    // group's raised pistons (by the pattern: both axes, x, y or the diagonal).
    if (race.pistons_on()) {
        const auto& track = files_->track(race.course());
        const std::uint32_t block = track.u32(files_->track_header(race.course()) + 0x14);
        const int mx = std::int16_t(m.x >> 16) >> 3, mcy = my >> 3;
        for (std::size_t g = 0; g < race.piston_groups.size(); ++g) {
            const auto& group = race.piston_groups[g];
            if (group.timer != -1) {
                continue;
            }
            const int column = group.column + track.s8(block + 0x1AC + std::uint32_t(g) * 12);
            const int row = group.row + track.s8(block + 0x1D0 + std::uint32_t(g) * 12);
            bool behind = false;
            switch (group.pattern) {
            case 2: behind = mx <= column + 2 && mcy <= row + 2; break;
            case 1: behind = mx <= column; break;
            case 0: behind = mcy <= row; break;
            default: behind = mcy + mx < row + column + 2; break;
            }
            if (behind) {
                cut_through(sprite, race, track.u32(group.frame), (group.row - group.column) * 8 + group.x,
                            (group.column + group.row) * 4 + group.line - race.scroll_lines(), sprite_top);
            }
        }
    }
    for (const int i : regions.codes12()) {
        if (i >= 0 && my < std::int16_t((all[std::size_t(i)].y >> 16) + 4)) {
            cut_region(all[std::size_t(i)], sprite_top);
        }
    }
}

// $111772: a course picture's mask cut out of the sprite, the picture at x
// and line y as blitted (its record's offsets added), the sprite's top at
// line `sprite_top`. Only cells with a mask cut: the loader makes masks
// (the planes ORed) for scenery cells flagged so ($108224); Beginner's
// bridge frames (cells 4-7) have none of their own, and the course start
// gives them the masks of cells $11-$14, the bridge alone ($10F9E4).
void View::cut_through(Sprite& sprite, const Race& race, std::uint32_t item, int x, int y, int sprite_top) const {
    const auto& track = files_->track(race.course());
    if (item == 0xFFFFFFFF || !track.contains(item, 12)) {
        return;
    }
    const std::uint32_t id = track.u32(item + 8);
    const auto& library = assets_->course_images[std::size_t(race.course())];
    if ((id >> 16) != 0 || (id & 0xFFFF) >= library.cells.size()) {
        return;
    }
    const auto& cell = library.cells[id & 0xFFFF];
    const data::ImageCell* mask = &cell;
    const std::size_t index = id & 0xFFFF;
    if (race.course() == data::Course::beginner && index >= 4 && index < 8 && index + 0xD < library.cells.size()) {
        mask = &library.cells[index + 0xD];
    }
    if (mask->type != 0 || (mask->flags & 1) == 0) {
        return;
    }
    const int left = x + track.s8(item);
    const int top = y - track.s8(item + 1);
    for (int c = 0; c < sprite.columns; ++c) {
        for (int r = 0; r < sprite.rows; ++r) {
            const int cy = sprite_top + r - top;
            if (cy < 0 || cy >= cell.height || cy >= mask->height) {
                continue;
            }
            auto& w = sprite.at(c, r);
            for (int bit = 0; bit < 16; ++bit) {
                const int cx = sprite.x + c * 16 + bit - left;
                if (cx >= 0 && cx < cell.width && cx < mask->width &&
                    mask->pixels[std::size_t(cy * mask->width + cx)] != 0) {
                    const auto b = std::uint16_t(0x8000 >> bit);
                    w[0] = std::uint16_t(w[0] & ~b);
                    w[1] = std::uint16_t(w[1] & ~b);
                }
            }
        }
    }
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

// The image libraries by bank ($1084A0): the course's obstacles, the
// marble, the slinky, the acid, the bird, the wall covers.
const data::ImageLibrary& View::library_of(int bank, data::Course course) const {
    switch (bank) {
    case 1: return assets_->marble;
    case 2: return assets_->slinky;
    case 3: return assets_->acid;
    case 4: return assets_->bird;
    case 5: return assets_->wall_covers[std::size_t(course)];
    default: return assets_->course_images[std::size_t(course)];
    }
}

// $1062EC: a slinky (while its screen height is within -$20..$FF): its
// frame's record in its own colours, with the wall covers cut out.
void View::draw_slinky(platform::Image& image, const Race& race, const Slinky& s) const {
    const int screen_x = std::int16_t(std::uint32_t(s.screen) >> 16), screen_y = std::int16_t(s.screen);
    if (s.status == 0 || screen_y <= -0x20 || screen_y >= 0x100) {
        return;
    }
    const auto& track = files_->track(race.course());
    const auto& program = files_->program();
    if (!track.contains(s.frame, 4)) {
        return;
    }
    const std::uint32_t record = track.u32(s.frame);
    if (!track.contains(record, 16)) {
        return;
    }
    const std::uint32_t colours = track.u32(record + 12);
    const auto& source = track.contains(colours, 6) ? track : program;
    if (!source.contains(colours, 6)) {
        return;
    }
    Sprite sprite;
    if (!make_sprite(sprite, track, record, screen_x, screen_y, race.course())) {
        return;
    }
    for (const auto& cover : s.covers) {
        if (cover.cell == -1) {
            break;
        }
        cut_cover(sprite, cover, race.course());
    }
    draw_sprite(image, sprite, ramp_at(source, colours));
}

// $1065DA / $106420: Silly's critters and birds (while their screen height
// is within -$10..$FF): the frame's record in its own colours, the birds'
// covers cut out.
void View::draw_creature(platform::Image& image, const Race& race, std::uint32_t frame, std::int32_t screen,
                         int lowest, const std::array<Marble::Cover, 5>* covers) const {
    const int screen_x = std::int16_t(std::uint32_t(screen) >> 16), screen_y = std::int16_t(screen);
    if (screen_y <= lowest || screen_y >= 0x100) {
        return;
    }
    const auto& track = files_->track(race.course());
    const auto& program = files_->program();
    if (!track.contains(frame, 4)) {
        return;
    }
    const std::uint32_t record = track.u32(frame);
    if (!track.contains(record, 16)) {
        return;
    }
    const std::uint32_t colours = track.u32(record + 12);
    const auto& source = track.contains(colours, 6) ? track : program;
    if (!source.contains(colours, 6)) {
        return;
    }
    Sprite sprite;
    if (!make_sprite(sprite, track, record, screen_x, screen_y, race.course())) {
        return;
    }
    if (covers != nullptr) {
        for (const auto& cover : *covers) {
            if (cover.cell == -1) {
                break;
            }
            cut_cover(sprite, cover, race.course());
        }
    }
    draw_sprite(image, sprite, ramp_at(source, colours));
}

// $10652E: an acid puddle (while its screen height is within -$30..$11F):
// its frame's record in its own colours.
void View::draw_acid(platform::Image& image, const Race& race, const Acid& a) const {
    const int screen_x = std::int16_t(std::uint32_t(a.screen) >> 16), screen_y = std::int16_t(a.screen);
    if (a.status == 0 || screen_y <= -0x30 || screen_y >= 0x120) {
        return;
    }
    const auto& track = files_->track(race.course());
    const auto& program = files_->program();
    if (!track.contains(a.frame, 4)) {
        return;
    }
    const std::uint32_t record = track.u32(a.frame);
    if (!track.contains(record, 16)) {
        return;
    }
    const std::uint32_t colours = track.u32(record + 12);
    const auto& source = track.contains(colours, 6) ? track : program;
    if (!source.contains(colours, 6)) {
        return;
    }
    Sprite sprite;
    if (!make_sprite(sprite, track, record, screen_x, screen_y, race.course())) {
        return;
    }
    draw_sprite(image, sprite, ramp_at(source, colours));
}

// $1061C6: a steelie: its picture (marbdat table +$8A, in its own colours)
// or, broken, its animation frame in the broken colours ($106840), with the
// wall covers cut out; then the dust of its shattering.
void View::draw_steelie(platform::Image& image, const Race& race, const Marble& m) const {
    if (m.status == 0 || m.sprite_flag == 0) {
        return;
    }
    const auto& program = files_->program();
    const auto& marble_data = files_->marble_data();
    const int screen_x = std::int16_t(std::uint32_t(m.screen[0]) >> 16);
    const int screen_y = std::int16_t(m.screen[0]);
    auto colours_of = [&](std::uint32_t record) {
        const std::uint32_t colours = marble_data.u32(record + 12);
        const auto& source = marble_data.contains(colours, 6) ? marble_data : program;
        return source.contains(colours, 6) ? ramp_at(source, colours) : std::array<data::Rgb4, 3>{};
    };
    std::uint32_t record = 0;
    std::array<data::Rgb4, 3> ramp{};
    if (m.status == 2) {
        if (!marble_data.contains(m.animation, 4)) {
            return;
        }
        record = marble_data.u32(m.animation);
        ramp = ramp_at(program, broken_steelie_ramp);
    } else {
        record = marble_data.u32(marble_data.first_filled_hunk() + 0x8A);
        if (!marble_data.contains(record, 16)) {
            return;
        }
        ramp = colours_of(record);
    }
    Sprite sprite;
    if (make_sprite(sprite, marble_data, record, screen_x, screen_y, race.course())) {
        for (const auto& cover : m.covers) {
            if (cover.cell == -1) {
                break;
            }
            cut_cover(sprite, cover, race.course());
        }
        cut_course_parts(sprite, race, m);
        draw_sprite(image, sprite, ramp);
    }
    // The dust list runs on past its end while the shatter lasts; as for the
    // player's marble, a record without colours in the game's data is not
    // drawn (the original reads them from wherever the pointer leads).
    if (m.dust_shown != 0 && marble_data.contains(m.dust_animation, 4)) {
        const std::uint32_t dust = marble_data.u32(m.dust_animation);
        if (marble_data.contains(dust, 16)) {
            const std::uint32_t colours = marble_data.u32(dust + 12);
            const auto& source = marble_data.contains(colours, 6) ? marble_data : program;
            if (source.contains(colours, 6)) {
                draw_record(image, marble_data, dust, screen_x, screen_y, ramp_at(source, colours), race.course());
            }
        }
    }
}

// $106396: Practice's drop markers, a sprite in the player's colours.
void View::draw_drop_mark(platform::Image& image, const Race& race, int i) const {
    const auto& program = files_->program();
    const auto& mark = race.drop_marks()[std::size_t(i)];
    const int x = std::int16_t(std::uint32_t(mark.screen) >> 16), y = std::int16_t(mark.screen);
    if (mark.kind == -1 || y <= -0x40 || y >= 0x100) {
        return;
    }
    draw_record(image, program, mark.kind == 0 ? drop_mark : drop_mark_zone3, x, y,
                ramp_at(program, plain_ramps[std::size_t(i)]), race.course());
}

// $106586: unpictured regions with a sprite (display type 5, region code
// 5: the goal flags) show their current frame's draw record, with the
// record's own colours.
void View::draw_region(platform::Image& image, const Race& race, int index) const {
    if (index < 0 || index >= Regions::slots) {
        return;
    }
    const auto& t = files_->track(race.course());
    const auto& r = race.regions().all()[std::size_t(index)];
    if (!r.active || r.kind == 1 || r.code != 5 || r.screen_y <= -0x40 || r.screen_y >= 0x100) {
        return;
    }
    if (!t.contains(r.shown_frame, 4)) {
        return;
    }
    const std::uint32_t record = t.u32(r.shown_frame);
    if (!t.contains(record, 16)) {
        return;
    }
    const std::uint32_t colours = t.u32(record + 12);
    if (!t.contains(colours, 6)) {
        return;
    }
    draw_record(image, t, record, r.screen_x - 1, r.screen_y, ramp_at(t, colours), race.course());
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
    const auto& library = library_of(bank, course);
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
