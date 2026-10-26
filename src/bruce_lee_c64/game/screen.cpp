#include "bruce_lee_c64/game/screen.hpp"
#include "bruce_lee_c64/game/world.hpp"
#include <algorithm>
#include <cmath>

namespace bruce_lee::game {
namespace {

// Display window raster lines: line 51 is the first of character row 0.
constexpr int first_line = 51;

// $0755: row 0 is filled with text code $20 (the solid block the init
// patched in) and row 2 with code $06 of the room charset; colour RAM is 0
// in rows 0-1 and $F8 elsewhere until the room fills its own colour.
constexpr std::uint8_t bar_code = 0x20, rule_code = 0x06;
constexpr std::uint8_t status_colour = 0x00, rule_colour = 0xF8;

// The bottom interrupt ($48AE) sets $D021 = $FC for the status rows; the
// play colours ($4788) give the border and the shared colours there.
constexpr std::uint8_t status_background = 0x0C;

// When the colour writes of a split reach the picture. $4928 waits for
// raster $42 and delays, so its writes land at the start of raster $43.
// $498F (raster interrupt at `line`) waits for line + 2, delays and writes
// $D021 and $D022 early in line + 3; its $D023 write falls behind that
// line's bad-line stall, so $D023 changes one line later. Counted from the
// code's cycles and checked on reference pictures; interrupt latency can
// move the writes by a few cycles (hypothesis: not seen to change lines).
constexpr int top_split_line = 0x43;
constexpr int band_delay = 3;

} // namespace

c64::TextFrame play_frame(const data::Assets& assets, const PlayScreen& screen) {
    const auto& room = assets.rooms[std::size_t(screen.room)];
    const auto& charset = assets.charsets[std::size_t(room.charset)];
    c64::TextFrame frame;

    for (int column = 0; column < 40; ++column) {
        frame.set(column, 0, &assets.text[bar_code], status_colour);
        frame.set(column, 1, &assets.text[screen.status[std::size_t(column)] & 0x7F], status_colour);
        frame.set(column, 2, &charset[rule_code], rule_colour);
    }
    // The room's cells, two rows each ($0A9F); bit 7 gives colour $F8 ($0B51).
    auto put_cell = [&](int column, int top_row, std::uint8_t cell) {
        const std::uint8_t colour = cell & 0x80 ? 0xF8 : room.colour;
        for (int half = 0; half < 2; ++half) {
            const int row = top_row + half;
            if (row < c64::TextFrame::rows) {
                frame.set(column, row, &charset[(cell & 0x7F) | (half ? 0x80 : 0)], colour);
            }
        }
    };
    for (int row = 0; row < data::Room::rows; ++row) {
        for (int column = 0; column < data::Room::columns; ++column) {
            put_cell(column, 3 + 2 * row, room.at(column, row));
        }
    }
    // Taken lanterns ($335B): their character over the map, colour unchanged.
    for (std::size_t i = 0; i < room.lanterns.size() && i < screen.lanterns_taken.size(); ++i) {
        if (!screen.lanterns_taken[i]) {
            continue;
        }
        const auto& lantern = room.lanterns[i];
        frame.set_glyph(lantern.column, lantern.screen_row, &charset[lantern.taken_code]);
        frame.set_glyph(lantern.column, lantern.screen_row + 1, &charset[lantern.taken_code | 0x80]);
    }

    // Colours line by line.
    c64::LineSetup setup;
    setup.border = assets.play_colours[0] & 15;
    setup.background = status_background;
    setup.shared1 = assets.play_colours[2] & 15;
    setup.shared2 = assets.play_colours[3] & 15;
    setup.multicolour = true;
    int from = 0;
    auto to_y = [](int raster) { return std::clamp(raster - first_line, 0, c64::Picture::height); };
    auto apply = [&](int raster, const std::array<std::uint8_t, 3>& colours, bool late_shared2) {
        const int y = to_y(raster);
        frame.set_lines(from, y, setup);
        setup.background = colours[0] & 15;
        setup.shared1 = colours[1] & 15;
        from = y;
        if (late_shared2 && y < c64::Picture::height) {
            frame.set_lines(y, y + 1, setup); // $D023 not written yet
            from = y + 1;
        }
        setup.shared2 = colours[2] & 15;
    };
    apply(top_split_line, room.top_colours, false);
    for (const auto& band : room.bands) {
        apply(band.line + band_delay, band.colours, true);
    }
    frame.set_lines(from, c64::Picture::height, setup);
    return frame;
}

namespace {
// The colour lines of a room ($48AE, $4928, $498F), as in play_frame.
void set_room_lines(c64::TextFrame& frame, const data::Assets& assets, const data::Room& room) {
    c64::LineSetup setup;
    setup.border = assets.play_colours[0] & 15;
    setup.background = status_background;
    setup.shared1 = assets.play_colours[2] & 15;
    setup.shared2 = assets.play_colours[3] & 15;
    setup.multicolour = true;
    int from = 0;
    auto to_y = [](int raster) { return std::clamp(raster - first_line, 0, c64::Picture::height); };
    auto apply = [&](int raster, const std::array<std::uint8_t, 3>& colours, bool late_shared2) {
        const int y = to_y(raster);
        frame.set_lines(from, y, setup);
        setup.background = colours[0] & 15;
        setup.shared1 = colours[1] & 15;
        from = y;
        if (late_shared2 && y < c64::Picture::height) {
            frame.set_lines(y, y + 1, setup);
            from = y + 1;
        }
        setup.shared2 = colours[2] & 15;
    };
    apply(top_split_line, room.top_colours, false);
    for (const auto& band : room.bands) {
        apply(band.line + band_delay, band.colours, true);
    }
    frame.set_lines(from, c64::Picture::height, setup);
}

// $268D: a pose as a sprite block: narrow poses fill the first byte of each
// row, wide ones the first two, from the top.
c64::Sprite pose_sprite(const data::Pose& pose) {
    c64::Sprite sprite;
    if (pose.rows.empty()) {
        return sprite;
    }
    for (int row = 0; row <= pose.row_count && row < 21; ++row) {
        if (pose.wide) {
            sprite.data[std::size_t(3 * row)] = pose.rows[std::size_t(2 * row)];
            sprite.data[std::size_t(3 * row + 1)] = pose.rows[std::size_t(2 * row + 1)];
        } else {
            sprite.data[std::size_t(3 * row)] = pose.rows[std::size_t(row)];
        }
    }
    sprite.shown = true;
    sprite.expand_x = true;
    sprite.colour = pose.colour & 15;
    return sprite;
}
} // namespace

c64::TextFrame world_frame(const World& world, const SpritePlaces* places) {
    const auto& assets = world.assets;
    const auto& room = assets.rooms[std::size_t(world.room)];
    const auto& charset = places && places->charset ? *places->charset
                                                    : world.charsets[std::size_t(world.charset_index())];
    c64::TextFrame frame;
    for (int row = 0; row < Playfield::rows; ++row) {
        for (int column = 0; column < Playfield::columns; ++column) {
            const auto& cell = world.field.at(column, row);
            const data::Glyph* glyph = row < 2 ? &assets.text[cell.code & 0x7F] : &charset[cell.code];
            frame.set(column, row, glyph, cell.colour);
        }
    }
    set_room_lines(frame, assets, room);
    // $25CF: sprite x = 2 * x (wide poses facing left 8 units further
    // left; the second layer of some frames likewise), y = y + $1D.
    for (int slot = 0; slot < 3; ++slot) {
        const Actor& a = world.actors.slots[std::size_t(slot)];
        if (!a.present() || (places && places->hidden[std::size_t(slot)])) {
            continue;
        }
        const auto codes = a.sprite_codes;
        // The smooth-motion layer moves the sprites by its own place (the
        // offsets of wide poses stay as the game's).
        double shift_x = 0, shown_y = a.y;
        if (places && places->actors[std::size_t(slot)]) {
            shift_x = (*places->actors[std::size_t(slot)])[0] - a.x;
            shown_y = (*places->actors[std::size_t(slot)])[1];
        }
        auto place = [&](std::uint8_t code, std::uint8_t pose, int x_units, bool force_black) {
            if (code == 0 || pose >= assets.poses.size()) {
                return;
            }
            auto sprite = pose_sprite(assets.poses[pose]);
            if (!sprite.shown) {
                return;
            }
            sprite.x = int(std::lround(2 * ((x_units & 0xFF) + shift_x)));
            sprite.y = int(std::lround(shown_y)) + 0x1D;
            if (!places) {
                sprite.y &= 0xFF;
            }
            if (force_black) {
                sprite.colour = 0;
            }
            if (places && places->actor_colour[std::size_t(slot)]) {
                sprite.colour = *places->actor_colour[std::size_t(slot)];
            }
            frame.set_sprite(assets.sprite_slot_of_code[std::size_t(code - 3)], sprite);
        };
        const bool wide = assets.poses[a.pose].wide;
        place(codes.first, a.pose, wide && a.facing != 1 ? a.x - 8 : a.x, slot == 2);
        const int second_frame = a.frame + 1;
        const bool shifted = second_frame == 0x05 || second_frame == 0x3F || second_frame == 0x43 || second_frame == 0x19;
        place(codes.second, std::uint8_t(a.pose + 1), shifted ? a.x - 8 : a.x, false);
    }
    // $41B2: room 18's fireballs, sprites 6 and 7 (four rows of a block,
    // x-expanded and single-colour like all sprites: $07E7, $3609).
    if (world.room == 18) {
        for (std::size_t i = 0; i < world.fireballs.size(); ++i) {
            const Fireball& f = world.fireballs[i];
            if (!f.active) {
                continue;
            }
            c64::Sprite sprite;
            sprite.shown = true;
            sprite.expand_x = true;
            sprite.colour = f.colour;
            for (std::size_t row = 0; row < f.shape.size(); ++row) {
                sprite.data[3 * row] = f.shape[row];
            }
            sprite.x = 2 * f.x;
            sprite.y = (f.y + 0x1D) & 0xFF;
            if (places && places->fireballs[i]) {
                sprite.x = int(std::lround(2 * (*places->fireballs[i])[0]));
                sprite.y = int(std::lround((*places->fireballs[i])[1])) + 0x1D;
            }
            frame.set_sprite(6 + int(i), sprite);
        }
        // Carnage mode: fireballs sent back at the face. Only in pictures
        // for showing (with places): the game's own picture, whose sprite
        // collisions the rules read, never has them.
        for (std::size_t i = 0; places && i < world.carnage.returned.size(); ++i) {
            const auto& r = world.carnage.returned[i];
            if (!r.active || world.fireballs[i].active) {
                continue;
            }
            c64::Sprite sprite;
            sprite.shown = true;
            sprite.expand_x = true;
            sprite.colour = r.colour;
            for (std::size_t row = 0; row < r.shape.size(); ++row) {
                sprite.data[3 * row] = r.shape[row];
            }
            sprite.x = int(std::lround(2 * r.x));
            sprite.y = int(std::lround(r.y)) + 0x1D;
            frame.set_sprite(6 + int(i), sprite);
        }
    }
    return frame;
}

} // namespace bruce_lee::game
