#include "stunt_car_racer_amiga/render/race_screen.hpp"
#include <algorithm>
#include <cstdlib>
#include <string>

namespace stuntcar::render {
namespace {

using data::Item;
using data::Overlay;

constexpr int screen_width = 320, screen_height = 200;
constexpr int view_x = 32, view_y = 16; // the view's place on screen

// Front wheels reach down to this row ($9E: the wheel items are cut short
// there each frame); a wheel above row $7E shows the raised tread.
constexpr int wheel_bottom = 0x9F, raised_wheel = 0x7E;
// The crane's chains: links every 8 rows, upper and lower alternating, from
// (bottom - $30) up to the top of the view.
constexpr int chain_offset = 0x30, link_rows = 8, chain_top = 16;
// Damage marks along the roll bar: ten, mark n at half-column 3n + 6; the
// first `intact_marks` whole, the others holes ($60988).
constexpr int damage_marks = 10;
// The speed bar: two rows under the speedometer scale, 128 pixels from x 96.
constexpr int speed_bar_x = 96, speed_bar_y = 174, speed_bar_rows = 2, speed_bar_length = 128;
constexpr std::uint8_t speed_bar_lit = 3, speed_bar_dark = 0;
// Dashboard text: 7-pixel columns and 8-pixel rows; a character fills its
// cell with the boxes' colour and draws its glyph in colour 0 (':' and '.'
// in the timer draw the glyph alone).
constexpr int text_column = 7, text_row = 8;
constexpr std::uint8_t text_colour = 0, text_box = 14, message_white = 15;
// Dust: three pixels in colour 3 and the upper right one white ($60EF2).
constexpr std::uint8_t spark_colour = 3, spark_white = 15;
// The tread sprites line up with their columns (observed).
constexpr int sprite_shift = 0;

class Screen {
  public:
    explicit Screen(const data::CockpitArt& art) : pixels_(art.picture) {}

    void put(int x, int y, std::uint8_t c) {
        if (x >= 0 && x < screen_width && y >= 0 && y < screen_height) pixels_[std::size_t(y * screen_width + x)] = c;
    }

    // An overlay at (x, y), optionally cut off at row `bottom` and kept
    // within a clip rectangle.
    void draw(const Overlay& item, int x, int y, int bottom = screen_height, int clip_x0 = 0, int clip_y0 = 0,
              int clip_x1 = screen_width, int clip_y1 = screen_height) {
        for (int row = 0; row < item.height; ++row) {
            const int sy = y + row;
            if (sy >= bottom || sy < clip_y0 || sy >= clip_y1) continue;
            for (int column = 0; column < item.width; ++column) {
                const int sx = x + column;
                if (sx < clip_x0 || sx >= clip_x1) continue;
                const auto c = item.pixels[std::size_t(row * item.width + column)];
                if (c != Overlay::clear) put(sx, sy, c);
            }
        }
    }
    void draw(const Overlay& item) { draw(item, item.x, item.y); }

    void character(const data::CockpitArt& art, int x, int y, char ch, bool fill_cell = true,
                   std::uint8_t ink = text_colour) {
        const int code = static_cast<unsigned char>(ch) - ' ';
        if (code < 0 || code >= int(art.font.size())) return;
        for (int row = 0; row < text_row; ++row)
            for (int bit = 0; bit < text_column; ++bit) {
                if (art.font[std::size_t(code)][std::size_t(row)] & (0x80 >> bit)) put(x + bit, y + row, ink);
                else if (fill_cell) put(x + bit, y + row, text_box);
            }
    }

    // Characters one cell apart, the pixel offset growing by one each (as
    // the game's number printer does).
    void number(const data::CockpitArt& art, int x, int y, const std::string& s) {
        for (const char ch : s) {
            character(art, x, y, ch);
            x += text_column + 1;
        }
    }

    std::vector<std::uint8_t> pixels_;
};

} // namespace

platform::Image draw_race_screen(const data::CockpitArt& art, const View& view, const Palette& palette,
                                 const Cockpit& cockpit) {
    Screen screen(art);
    for (const auto& p : cockpit.crack) screen.put(p.x, p.y, p.colour);
    for (int y = 0; y < view_height; ++y)
        for (int x = 0; x < view_width; ++x) screen.put(view_x + x, view_y + y, view.pixels[std::size_t(y * view_width + x)]);

    // Dust and smoke, drawn after the view and before the car.
    for (const auto& s : cockpit.sparks) {
        const int x = view_x + s.x, y = view_y + s.y;
        screen.put(x, y, spark_colour);
        screen.put(x + 1, y, spark_colour);
        screen.put(x, y - 1, spark_colour);
        screen.put(x + 1, y - 1, spark_white);
    }
    for (const auto& puff : cockpit.smoke) {
        const auto& item = art.item(Item::smoke, std::clamp(puff.size, 0, 7));
        screen.draw(item, puff.x, puff.y, screen_height, view_x, view_y, view_x + view_width, view_y + view_height);
    }

    // The crane's chains while it holds the car.
    if (cockpit.chain_bottom) {
        for (int at = *cockpit.chain_bottom;; at -= 2 * link_rows) {
            const int upper = at - chain_offset, lower = upper + link_rows;
            if (lower < chain_top) break;
            screen.draw(art.item(Item::chain_left_lower), art.item(Item::chain_left_lower).x, lower);
            screen.draw(art.item(Item::chain_right_lower), art.item(Item::chain_right_lower).x, lower);
            if (upper < chain_top) break;
            screen.draw(art.item(Item::chain_left_upper), art.item(Item::chain_left_upper).x, upper);
            screen.draw(art.item(Item::chain_right_upper), art.item(Item::chain_right_upper).x, upper);
        }
    }

    screen.draw(art.item(Item::left_corner));
    screen.draw(art.item(Item::right_corner));
    const int frame = std::clamp(cockpit.wheel_frame, 0, 2);
    const auto& right = art.item(Item::right_wheel, frame);
    const auto& left = art.item(Item::left_wheel, 2 - frame);
    screen.draw(right, right.x, cockpit.right_wheel_y, wheel_bottom);
    screen.draw(left, left.x, cockpit.left_wheel_y, wheel_bottom);
    screen.draw(art.item(Item::engine));
    screen.draw(art.item(Item::left_sill));
    screen.draw(art.item(Item::right_sill));
    if (cockpit.boost) {
        const int phase = std::clamp(cockpit.boost_phase, 0, 2);
        screen.draw(phase == 2 ? art.item(Item::flame_third_left) : art.item(Item::left_flame, phase));
        screen.draw(phase == 2 ? art.item(Item::flame_third_right) : art.item(Item::right_flame, phase));
    }
    if (cockpit.message_box) screen.draw(art.item(Item::message_box));
    if (cockpit.message)
        for (const auto& g : cockpit.message->glyphs)
            screen.character(art, g.x, g.y, g.c, false, cockpit.message_white ? message_white : text_colour);

    // Dashboard.
    for (int n = 0; n < damage_marks; ++n) {
        const int half_columns = 3 * n + 6;
        const int kind = n < cockpit.intact_marks ? 4 : (n == cockpit.intact_marks && cockpit.cracking ? 2 : 0);
        const auto& mark = art.item(Item::damage_mark, kind + (half_columns & 1));
        screen.draw(mark, (half_columns >> 1) * 16, mark.y);
    }
    screen.draw(art.item(cockpit.flag_lamp ? Item::flag_lamp_on : Item::flag_lamp_off));
    const int lit = std::clamp(cockpit.speed_bar, 0, speed_bar_length - 1);
    for (int row = 0; row < speed_bar_rows; ++row)
        for (int x = 0; x < speed_bar_length; ++x)
            screen.put(speed_bar_x + x, speed_bar_y + row, x >= 1 && x <= lit ? speed_bar_lit : speed_bar_dark);
    screen.draw(art.item(cockpit.right_lamp ? Item::right_lamp_on : Item::right_lamp_off));
    screen.character(art, 5 * text_column + 2, 22 * text_row + 2, 'L');
    if (cockpit.lap > 0) screen.character(art, 6 * text_column + 3, 22 * text_row + 2, char('0' + cockpit.lap % 10));
    screen.character(art, 8 * text_column + 4, 22 * text_row + 2, 'B');
    if (cockpit.boost_left) {
        const int left = std::clamp(*cockpit.boost_left, 0, 99);
        screen.number(art, 9 * text_column + 5, 22 * text_row + 2, std::string{char('0' + left / 10), char('0' + left % 10)});
    }
    const int distance = std::clamp(cockpit.distance, -9999, 9999);
    std::string digits = std::to_string(std::abs(distance));
    digits = std::string(4 - digits.size(), '0') + digits;
    screen.number(art, 6 * text_column + 2, 23 * text_row + 4, (distance < 0 ? "-" : " ") + digits);
    // Lap times (routine $5E326): M:SS. then hundredths, from column 33.
    const auto time = [&](const Cockpit::Time& t, int row, int offset) {
        const int y = row * text_row + offset;
        const auto digit = [](int v) { return char('0' + v % 10); };
        screen.character(art, 34 * text_column + 5, y, ':', false);
        screen.character(art, 37 * text_column + 2, y - 2, '.', false);
        screen.character(art, 33 * text_column + 6, y, digit(t.minutes));
        screen.character(art, 35 * text_column + 3, y, digit(t.seconds / 10));
        screen.character(art, 36 * text_column + 3, y, digit(t.seconds));
        screen.character(art, 37 * text_column + 7, y, t.show_hundredths ? digit(t.hundredths / 10) : ' ');
        screen.character(art, 38 * text_column + 7, y, t.show_hundredths ? digit(t.hundredths) : ' ');
    };
    if (cockpit.lap_time) time(*cockpit.lap_time, 22, 2);
    if (cockpit.lower_time) time(*cockpit.lower_time, 23, 4);

    // The tyre treads (hardware sprites, in front of everything).
    const int raised_right = cockpit.right_wheel_y < raised_wheel ? 6 : 0;
    const int raised_left = cockpit.left_wheel_y < raised_wheel ? 6 : 0;
    const auto& right_tread = art.item(Item::tread, frame + raised_right);
    const auto& left_tread = art.item(Item::tread, 5 - frame + raised_left);
    screen.draw(right_tread, right_tread.x + sprite_shift, cockpit.right_wheel_y);
    screen.draw(left_tread, left_tread.x + sprite_shift, cockpit.left_wheel_y);

    platform::Image image(screen_width, screen_height);
    for (std::size_t i = 0; i < screen.pixels_.size(); ++i) {
        const auto c = screen.pixels_[i] < 16 ? palette[screen.pixels_[i]] : art.palette[screen.pixels_[i]];
        image.pixels[i] = {c.r, c.g, c.b, 255};
    }
    return image;
}

platform::Image cockpit_overlay(const data::CockpitArt& art, const Palette& palette, const Cockpit& cockpit) {
    View zero, one; zero.pixels.fill(0); one.pixels.fill(1);
    auto overlay=draw_race_screen(art,zero,palette,cockpit);
    const auto mask=draw_race_screen(art,one,palette,cockpit);
    for(std::size_t i=0;i<overlay.pixels.size();++i) {
        auto& a=overlay.pixels[i]; const auto b=mask.pixels[i];
        if(a.r!=b.r || a.g!=b.g || a.b!=b.b) a={0,0,0,0};
    }
    return overlay;
}

platform::Image start_screen(const data::Program& program, const data::Circuits& circuits, const data::Scenery& scenery,
                             const data::CockpitArt& art, const data::Track& track) {
    // DROP / START (message $2C, four groups), the chains at their lowest
    // ($B0), the opponent to the left (across 76, as seen at the start).
    constexpr int drop_start = 0x2C, drop_start_groups = 4, chains_down = 0xB0, opponent_left = 76;
    const auto camera = start_camera(circuits, track);
    const int steps = track.segments[std::size_t(track.car_start_segment)].steps;
    Opponent opponent;
    opponent.segment = track.car_start_segment;
    opponent.step = std::min(steps / 2 + 1.5, steps - 0.6);
    opponent.across = opponent_left;
    const auto view = draw_race_view(circuits, scenery, track, track.car_start_segment, camera, opponent);
    Cockpit cockpit;
    cockpit.chain_bottom = chains_down;
    cockpit.message = data::decode_message(program, drop_start, drop_start_groups);
    return draw_race_screen(art, view, race_palette(program), cockpit);
}

} // namespace stuntcar::render
