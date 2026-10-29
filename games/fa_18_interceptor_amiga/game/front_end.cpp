#include "fa_18_interceptor_amiga/game/front_end.hpp"
#include "fa_18_interceptor_amiga/game/aircraft.hpp"
#include <algorithm>

namespace interceptor::game {
namespace {

constexpr int menu_palette = 0;  // $119FCC
constexpr int map_palette = 2;   // $11A04C
// The title stays while the original loads (about 22 s on the A500 from
// the tune's start to the main loop): here 22 s, or until a key.
constexpr int title_frames = 22 * 50;
constexpr int title_fade_steps = 16;   // $107D48
constexpr int credits_block = 15, first_tour_block = 1, welcome_block = 2, enlisted_block = 3;
constexpr int menu_title_block = 6, menu_first_block = 100, menu_last_block = 109;
constexpr int log_block = 87, start_map_block = 7, aircraft_block = 75;
constexpr int call_sign_length = 20;   // $13AAE5
constexpr int welcome_name_at = 8;     // "WELCOME " ($1344D7 + 10)
// Free flight's way to the place choice in map frames: countdown 4, the
// palette steps ($13AB90, 45 blanks), countdowns 5, 3 and 2.
constexpr int map_frames_to_choice = 5 + 14 + 6 + 4 + 3;
constexpr int map_bottom = 179;        // the view's last row; below it colour 3
constexpr int strip_colour = 3, marker_colour = 9;
// The map camera ($10D3D8): high over the bay, looking straight down.
constexpr double map_x = 0x10800000 / 256.0, map_y = 0x3000000 / 256.0, map_z = 0x10C00000 / 256.0;

data::Colour darker(data::Colour c) {
    const int r = std::max(0, (c >> 8 & 15) - 1), g = std::max(0, (c >> 4 & 15) - 1), b = std::max(0, (c & 15) - 1);
    return data::Colour(r << 8 | g << 4 | b);
}

} // namespace

FrontEnd::FrontEnd(const data::Assets& assets, const Scene& scene, Sounds& sounds, PilotRecord& pilot,
                   std::function<void()> write_pilot)
    : assets_(assets), scene_(scene), sounds_(sounds), pilot_(pilot), write_pilot_(std::move(write_pilot)),
      text_(assets) {
    for (const auto& p : assets.pictures) {
        if (p.file != "pix/splsh") continue;
        for (std::size_t i = 0; i < title_palette_.size() && i < p.picture.palette.size(); ++i)
            title_palette_[i] = p.picture.palette[i];
    }
}

data::TextBlock FrontEnd::block(int number) const {
    const auto found = std::find_if(assets_.texts.begin(), assets_.texts.end(),
                                    [&](const data::TextBlock& b) { return b.number == number; });
    return found != assets_.texts.end() ? *found : data::TextBlock{};
}

void FrontEnd::start_title() {
    text_.clear();
    sounds_.start_tune(); // (after a short wait at the title, $1008DA)
    fade_ = 0;
    frame_ = 0;
    go(Screen::title);
}

// $101A70 / $101D30: the tune back on (at half volume), the menu typed.
void FrontEnd::to_menu() {
    sounds_.start_tune();
    sounds_.set_tune_target(0x1F);
    menu_screen();
}

void FrontEnd::menu_screen() {
    text_.clear();
    palette_ = menu_palette;
    text_.set_line_spacing(12);
    text_.queue(menu_title_block);
    for (int b = menu_first_block; b <= menu_last_block; ++b) text_.queue(b);
    go(Screen::menu);
}

void FrontEnd::to_start_map() {
    text_.clear();
    palette_ = map_palette;
    place_ = -1;
    asking_aircraft_ = false;
    frame_ = 0;
    go(Screen::map_wait);
}

// $1193EE: the log's numbers written into block 87, right-aligned.
void FrontEnd::flight_log() {
    auto b = block(log_block);
    const auto put = [&](std::size_t line, std::size_t column, int width, unsigned value) {
        if (line >= b.lines.size()) return;
        auto& text = b.lines[line].text;
        const auto digits = std::to_string(value);
        if (text.size() < column + std::size_t(width)) text.resize(column + std::size_t(width), ' ');
        for (int i = 0; i < width; ++i) text[column + std::size_t(i)] = ' ';
        const auto n = std::min<std::size_t>(digits.size(), std::size_t(width));
        text.replace(column + std::size_t(width) - n, n, digits.substr(digits.size() - n));
    };
    put(2, 0x25, 3, pilot_.missions_started);
    put(3, 0x25, 3, pilot_.missions_completed);
    put(6, 0x17, 5, pilot_.cannon_fired);
    put(6, 0x20, 4, pilot_.cannon_hits);
    put(7, 0x17, 5, pilot_.amraams_fired);
    put(7, 0x20, 4, pilot_.amraam_hits);
    put(8, 0x17, 5, pilot_.sidewinders_fired);
    put(8, 0x20, 4, pilot_.sidewinder_hits);
    put(10, 0x17, 4, pilot_.flight_seconds / 3600);
    put(10, 0x21, 2, pilot_.flight_seconds % 3600 / 60);
    put(11, 0x17, 4, pilot_.crashes);
    put(12, 0x17, 4, pilot_.missile_hits);
    text_.clear();
    palette_ = menu_palette;
    text_.queue(std::move(b));
    go(Screen::log);
}

// $10EACC: a menu key. Free flight (2) and the flight log (8) are rebuilt;
// the other items show their line and come back to the menu.
void FrontEnd::choose(const FrontKeys& keys) {
    const int d = keys.digit;
    int line = 0;
    switch (d) {
    case 1: line = 101; break;              // demo ($7F)
    case 2: line = 102; break;              // free flight (1)
    case 3: line = 103; break;              // training demo (2)
    case 4: line = 104; break;              // training practice ($7D)
    case 5: line = 105; break;              // qualification (9)
    case 6: sounds_.menu_key(); return;     // the missions list (-1): not rebuilt
    case 7:                                 // the next advanced mission
        sounds_.menu_key();
        if (!pilot_.qualified) return;
        line = 107;
        break;
    case 8:                                 // the flight log (-2)
        sounds_.menu_key();
        flight_log();
        return;
    default: return;
    }
    if (d != 7) sounds_.menu_key();
    choice_ = d;
    unbuilt_ = d != 2;
    // $101E04: only the chosen line again, the tune fading out.
    text_.clear();
    text_.set_line_spacing(12);
    text_.queue(line);
    sounds_.set_tune_target(0);
    countdown_ = sounds_.has_tune() ? 0xD2 : 0x96;
    go(Screen::chosen);
}

std::optional<FlightStart> FrontEnd::tick(const FrontKeys& keys) {
    ++frame_;
    --countdown_;
    // The runner clicks for each typed character, but not while the tune
    // started at the title plays ($13AAC7 = 2: the credits).
    const bool silent = screen_ == Screen::credits || screen_ == Screen::credits_wait;
    if (text_.tick() && !silent) sounds_.click();
    switch (screen_) {
    case Screen::title:
        if (frame_ >= title_frames || keys.any) {
            frame_ = 0;
            go(Screen::title_fade);
        }
        break;
    case Screen::title_fade:
        // Each colour component one lower a step.
        for (auto& c : title_palette_) c = darker(c);
        if (++fade_ >= title_fade_steps) {
            countdown_ = 3; // $101962
            palette_ = menu_palette;
            go(Screen::credits);
        }
        break;
    case Screen::credits:
        if (countdown_ < 0) {
            text_.queue(credits_block);
            go(Screen::credits_wait);
        }
        break;
    case Screen::credits_wait:
        if (keys.any) {
            // $1035C8: the tune to half volume, the record read.
            sounds_.set_tune_target(0x1F);
            text_.clear();
            palette_ = map_palette;
            countdown_ = 5;
            go(Screen::pilot_check);
        }
        break;
    case Screen::pilot_check:
        if (countdown_ < 0) {
            palette_ = menu_palette;
            if (pilot_.launches == 0) {
                text_.queue(first_tour_block);
                go(Screen::first_tour);
            } else {
                auto b = block(welcome_block);
                if (!b.lines.empty()) {
                    auto& t = b.lines[0].text;
                    const auto name = pilot_.call_sign.substr(0, 24);
                    if (t.size() < welcome_name_at + name.size()) t.resize(welcome_name_at + name.size(), ' ');
                    t.replace(welcome_name_at, name.size(), name);
                }
                text_.queue(std::move(b));
                go(Screen::welcome);
            }
        }
        break;
    case Screen::first_tour:
        if (text_.done()) {
            // $10381E: block 3 below block 1, its first line the field.
            ++pilot_.launches;
            text_.begin_input(call_sign_length);
            text_.queue(enlisted_block);
            go(Screen::call_sign);
        }
        break;
    case Screen::call_sign:
        if (text_.inputting()) {
            if (keys.typed && text_.type(keys.typed)) sounds_.click();
            if (keys.backspace) text_.backspace();
            if (keys.enter) {
                pilot_.call_sign = text_.input();
                text_.enter();
                write_pilot_(); // $10858A
            }
        } else if (text_.done()) {
            to_menu();
        }
        break;
    case Screen::welcome:
        if (text_.done() || keys.any) {
            ++pilot_.launches;
            to_menu();
        }
        break;
    case Screen::menu:
        if (keys.digit > 0) choose(keys);
        else if (keys.escape) menu_screen();
        break;
    case Screen::chosen:
        if (countdown_ < 0) {
            // $10201E: the tune stops, the item starts.
            sounds_.stop_tune();
            if (unbuilt_) {
                to_menu();
            } else {
                sounds_.ambience(8);
                to_start_map();
            }
        }
        break;
    case Screen::log:
        if (keys.escape) {
            to_menu();
        } else if (keys.digit == 1) {
            write_pilot_();
            if (pilot_.launches <= 1) { // $101FC8: a new pilot signs in again
                text_.clear();
                countdown_ = 5;
                go(Screen::pilot_check);
            }
        } else if (keys.shift_2) {
            pilot_.reset();
            flight_log();
        }
        break;
    case Screen::map_wait:
        if (keys.shift_escape) {
            to_menu();
        } else if (frame_ >= map_frames_to_choice) {
            text_.queue(start_map_block);
            go(Screen::map);
        }
        break;
    case Screen::map:
        if (keys.shift_escape) {
            to_menu();
        } else if (!asking_aircraft_ && keys.digit >= 1 && keys.digit <= 3) {
            place_ = keys.digit - 1;
            asking_aircraft_ = true;
            text_.clear();
            text_.queue(aircraft_block);
        } else if (!asking_aircraft_ && keys.digit == 4) {
            // $102BB6: on the carrier there is no aircraft to choose.
            text_.clear();
            return FlightStart{3, 0x11};
        } else if (!asking_aircraft_ && keys.digit == 5) {
            sounds_.refuse(); // (place 5, record 15, is empty in free flight)
        } else if (asking_aircraft_ && (keys.digit == 1 || keys.digit == 2)) {
            text_.clear();
            return FlightStart{place_, keys.digit == 1 ? 0x11 : 0x10};
        }
        break;
    }
    return std::nullopt;
}

const data::Palette& FrontEnd::palette() const {
    if (screen_ == Screen::title || screen_ == Screen::title_fade) return title_palette_;
    return assets_.palettes.at(std::size_t(palette_));
}

void FrontEnd::draw(IndexedImage& image, int margin) const {
    if (screen_ == Screen::title || screen_ == Screen::title_fade) {
        for (const auto& p : assets_.pictures) {
            if (p.file != "pix/splsh") continue;
            const auto& pic = p.picture;
            for (int y = 0; y < std::min(pic.height, image.height); ++y)
                for (int x = 0; x < pic.width; ++x) image.put(margin + x, y, pic.pixels[std::size_t(y * pic.width + x)]);
        }
        return;
    }
    if (screen_ == Screen::map || screen_ == Screen::map_wait) {
        Camera c;
        c.position = {map_x, map_y, map_z};
        c.matrix = {0xA8 / 256.0, 0, 0, 0, 0, 0xFC / 256.0, 0, -0x80 / 256.0, 0};
        const int centre = image.width / 2 - 1;
        scene_.draw(image, centre, map_bottom, c);
        for (int y = map_bottom + 1; y < image.height; ++y)
            for (int x = 0; x < image.width; ++x) image.put(x, y, strip_colour);
        // $11FB9E: each runway's digit where it lies, projected as the
        // original does it (positions in 1/64 units, integer division) and
        // drawn by the panel's number drawer ($127640: 4-pixel steps, the
        // digit 8 pixels on).
        const auto& font = assets_.small_font;
        for (int p = 0; p < 4; ++p) {
            if (asking_aircraft_ && p != place_) continue;
            // (Place 4 is the carrier, record 14: its own position, $11FC44.)
            const auto at = p == 3 ? carrier_ : place_position(assets_.program, p);
            const std::int32_t dx = (at[0] - 0x10800000) >> 14, dy = -0x3000000 >> 14, dz = (at[1] - 0x10C00000) >> 14;
            const int xc = (0xA8 * dx) >> 8, yc = (0xFC * dz) >> 8, zc = (-0x80 * dy) >> 8;
            const int sx = 159 - 0xA0 * xc / zc + margin, sy = 90 - 0x5A * yc / zc;
            const auto* glyph = font.glyph(char('1' + p));
            if (!glyph) continue;
            const int left = (sx & ~3) + 8;
            for (int r = 0; r < font.rows && r < int(glyph->size()); ++r)
                for (int b = 0; b < 8; ++b)
                    if ((*glyph)[std::size_t(r)] & (0x80 >> b)) image.put(left + b, sy + r, marker_colour);
        }
    }
    text_.draw(image, margin);
}

} // namespace interceptor::game
