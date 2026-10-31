#include "stunt_car_racer_amiga/game/front_end.hpp"
#include <algorithm>

namespace stuntcar::game {
namespace {

using data::Texts;
using render::Strip;

// The menus ($5BCD0): each a run of entries, the text of each entry as an
// index into the menu or main table.
constexpr std::uint32_t menu_entries = 0x5BCD0;
constexpr int division_menu = 0x00, load_save_menu = 0x04, select_menu = 0x10, practise_division_menu = 0x18,
              practise_track_menu = 0x1C;
// Texts: SELECT is the first of either table; the season table's
// DIVISION, RACE, " of ", " V ", Track:, The, RESULT, the changes and NAME?
// ($64354), the menu table's SUPER DIVISION, the records' Race Time and
// Best Lap ($5EC92), the standings' labels ($5915E).
constexpr int select_text = 0x00;
constexpr int division_text = 0x00, race_text = 0x0D, of_text = 0xF4, versus_text = 0x28, the_text = 0x23,
              caption_text = 0x58, result_text = 0x60, changes_text = 0xD7, promotion_text = 0xB7,
              relegation_text = 0xC7, name_text = 0xE0, fire_text = 0x2C;
constexpr int super_division_text = 0xBB, super_league_text = 0xA7, well_done_text = 0xCE, track_text = 0x93;
constexpr int race_time_text = 0x23, best_lap_text = 0x2F, super_league_title = 0x4B, hall_header = 0x5B;
// The Hall of Fame: track initials ($5FDE0, two letters each from Little
// Ramp), rows from 9 two apart, names in colour 7, the header in 8.
constexpr std::uint32_t hall_initials = 0x5FDE0;
constexpr int hall_first_row = 9;
constexpr std::uint8_t hall_name_ink = 7, hall_header_ink = 8, hall_super_ink = 7;
constexpr int fixture_v_text = 0x00, points_text = 0x05, places_text = 0x49, raced_text = 0x25, wins_text = 0x2E,
              laps_text = 0x37, points_label_text = 0x40;
// The column the track's name starts on, by track ($6129A; "Track:  The"
// starts six columns earlier).
constexpr std::uint32_t track_name_columns = 0x6129A;
// Colours: white, black, the light grey of the bars, the orange titles.
constexpr std::uint8_t white = 1, black = 0, bar_grey = 2, orange = 15, line_colour = 10;
// The name: 12 characters after '>' on row 16, underlined at y 133 from x
// 106 to 190 ($5B648).
constexpr int name_length = 12, name_column = 14, name_row = 16, underline_y = 133, underline_left = 106,
              underline_right = 190;
// Menu bars stay brown this long when chosen ($5BA42 with 15), and the
// joystick repeats this often while held (50 Hz ticks).
constexpr int taken_ticks = 15, repeat_ticks = 10;
// The standings' columns and rows ($58F44).
constexpr int standings_columns[] = {4, 15, 26};
constexpr int standings_shifts[] = {14, 9, 4};
constexpr int raced_row = 19, wins_row = 21, laps_row = 22, points_row = 24;
// The records' box: green bar number 3, two lines ($5F074).
constexpr int records_bar = 3;

// C64-style key matrix positions of the keys a name is typed with
// (app::GameInput::c64_keys).
struct Key {
    int index;
    char lower, upper;
};
constexpr Key name_keys[] = {
    {10, 'a', 'A'}, {28, 'b', 'B'}, {20, 'c', 'C'}, {18, 'd', 'D'}, {14, 'e', 'E'}, {21, 'f', 'F'}, {26, 'g', 'G'},
    {29, 'h', 'H'}, {33, 'i', 'I'}, {34, 'j', 'J'}, {37, 'k', 'K'}, {42, 'l', 'L'}, {36, 'm', 'M'}, {39, 'n', 'N'},
    {38, 'o', 'O'}, {41, 'p', 'P'}, {62, 'q', 'Q'}, {17, 'r', 'R'}, {13, 's', 'S'}, {22, 't', 'T'}, {30, 'u', 'U'},
    {31, 'v', 'V'}, {9, 'w', 'W'},  {23, 'x', 'X'}, {25, 'y', 'Y'}, {12, 'z', 'Z'}, {35, '0', '0'}, {56, '1', '1'},
    {59, '2', '2'}, {8, '3', '3'},  {11, '4', '4'}, {16, '5', '5'}, {19, '6', '6'}, {24, '7', '7'}, {27, '8', '8'},
    {32, '9', '9'}, {44, '.', '.'}, {55, '/', '/'},
};
constexpr int key_backspace = 0, key_return = 1;
// Number keys move the highlight to an entry ($60C8D: 1-4; fire takes it).
constexpr int number_keys[] = {56, 59, 8, 11};
// The league opponents' habits ($1ED8A), by driver.
constexpr std::uint32_t driver_habits = 0x1ED8A;

} // namespace

FrontEnd::FrontEnd(const data::Program& program, const data::FrontEndArt& art, race::Random& random,
                   TrackPicture track_picture)
    : program_(program), art_(art), random_(random), track_picture_(std::move(track_picture)), league_(program) {
    league_.new_game(random_);
    open_menu(select_menu, 3, true);
}

std::string FrontEnd::name(int driver) const {
    if (driver != league::player) return data::driver_name(program_, driver);
    auto s = player_name_;
    s.resize(name_length, ' ');
    return s;
}

void FrontEnd::show(Screen s) {
    screen_ = s;
    repeat_ = 0;
}

void FrontEnd::open_menu(int base, int count, bool main, int chosen) {
    menu_ = {base, count, chosen, main};
    show(base == select_menu                ? Screen::select
         : base == division_menu            ? Screen::division_menu
         : base == practise_division_menu   ? Screen::practise_divisions
         : base == practise_track_menu      ? Screen::practise_tracks
                                            : Screen::load_save);
}

std::optional<RaceRequest> FrontEnd::take_race() {
    auto r = pending_race_;
    pending_race_.reset();
    return r;
}

// --- Input ---------------------------------------------------------------

void FrontEnd::tick(const app::GameInput& input) {
    const bool fire = input.fire || input.c64_keys[key_return] || input.space;
    const bool fire_pressed = fire && !fire_was_;
    switch (screen_) {
    case Screen::select:
    case Screen::division_menu:
    case Screen::practise_divisions:
    case Screen::practise_tracks:
    case Screen::load_save:
        menu_tick(input);
        break;
    case Screen::name:
        name_tick(input);
        break;
    case Screen::divisions:
        if (fire_pressed) open_menu(division_menu, 4, false, division_choice_);
        break;
    case Screen::hall_of_fame:
        if (fire_pressed) open_menu(division_menu, 4, false, division_choice_);
        break;
    case Screen::track_picture: {
        // Left and right walk round the stadium ($5D980).
        if (input.left && !left_was_) picture_view_ = (picture_view_ + 3) & 3;
        if (input.right && !right_was_) picture_view_ = (picture_view_ + 1) & 3;
        if (fire_pressed) {
            RaceRequest r;
            r.track = picture_track_;
            r.super_league = league_.super_league();
            r.roll_bar_marks = league_.roll_bar_marks();
            if (picture_league_) {
                const int opponent = league_.opponent();
                r.opponent = race::Driver{std::uint8_t(opponent), program_.u8(driver_habits + std::uint32_t(opponent))};
            }
            pending_race_ = r;
            show(Screen::racing);
        }
        break;
    }
    case Screen::racing:
        break;
    case Screen::fixture:
        if (fire_pressed) {
            // F1 held as fire is pressed passes the race by: lost ($58580).
            if (input.f1) {
                league_.play_round(random_, league::PlayerResult{true, true});
                if (league_.next_race()) next_league_race();
                else after_season();
            } else {
                picture_track_ = league_.track(league_.division_of_player());
                picture_view_ = 0;
                picture_league_ = true;
                show(Screen::track_picture);
            }
        }
        break;
    case Screen::scene:
        if (fire_pressed) show(Screen::result);
        break;
    case Screen::result:
        if (fire_pressed) show(Screen::standings);
        break;
    case Screen::standings:
        if (fire_pressed) {
            if (league_.next_race()) next_league_race();
            else after_season();
        }
        break;
    case Screen::changes:
        if (fire_pressed) {
            if (changes_.promoted == league::player) {
                scene_ = data::Scene::promoted;
                show(Screen::promotion);
            } else {
                show(Screen::divisions);
            }
        }
        break;
    case Screen::promotion:
        if (fire_pressed) show(Screen::divisions);
        break;
    }
    fire_was_ = fire;
    up_was_ = input.up, down_was_ = input.down, left_was_ = input.left, right_was_ = input.right;
    keys_was_ = input.c64_keys;
    space_was_ = input.space;
}

void FrontEnd::menu_tick(const app::GameInput& input) {
    if (menu_.taken >= 0) {
        if (++menu_.taken >= taken_ticks) {
            menu_.taken = -1;
            menu_chosen(menu_.chosen);
        }
        return;
    }
    const bool fire = input.fire || input.c64_keys[key_return] || input.space;
    // The joystick moves the highlight, repeating while held ($5B9F8).
    const bool up = input.up && (!up_was_ || repeat_ == 0);
    const bool down = input.down && (!down_was_ || repeat_ == 0);
    if (input.up || input.down) repeat_ = (repeat_ + 1) % repeat_ticks;
    else repeat_ = 0;
    if (up && menu_.chosen > 0) --menu_.chosen;
    if (down && menu_.chosen < menu_.count - 1) ++menu_.chosen;
    for (int k = 0; k < std::min(menu_.count, 4); ++k)
        if (input.c64_keys[std::size_t(number_keys[k])] && !keys_was_[std::size_t(number_keys[k])]) menu_.chosen = k;
    if (fire && !fire_was_) menu_.taken = 0;
}

void FrontEnd::menu_chosen(int item) {
    switch (screen_) {
    case Screen::select:
        // Only the single-player league is here; Multiplayer and Computer
        // Link stay on the menu.
        if (item == 0) {
            player_name_.clear();
            show(Screen::name);
        }
        break;
    case Screen::division_menu:
        division_choice_ = item;
        if (item == 0) show(Screen::hall_of_fame);
        else if (item == 1) open_menu(practise_division_menu, 4, true, 3 - league_.division_of_player());
        else if (item == 2) {
            league_.new_season();
            next_league_race();
        } else open_menu(load_save_menu, 4, false, 3);
        break;
    case Screen::practise_divisions:
        practise_division_ = 3 - item;
        open_menu(practise_track_menu, 3, false);
        break;
    case Screen::practise_tracks:
        if (item < 2) {
            picture_track_ = league_.track_of(practise_division_, item);
            picture_view_ = 0;
            picture_league_ = false;
            show(Screen::track_picture);
        } else {
            open_menu(division_menu, 4, false, division_choice_);
        }
        break;
    case Screen::load_save:
        open_menu(division_menu, 4, false, division_choice_);
        break;
    default:
        break;
    }
}

void FrontEnd::name_tick(const app::GameInput& input) {
    const auto pressed = [&](int k) { return input.c64_keys[std::size_t(k)] && !keys_was_[std::size_t(k)]; };
    if (pressed(key_backspace) && !player_name_.empty()) player_name_.pop_back();
    if (input.space && !space_was_ && !player_name_.empty() && int(player_name_.size()) < name_length)
        player_name_.push_back(' ');
    for (const auto& k : name_keys)
        if (pressed(k.index) && int(player_name_.size()) < name_length)
            player_name_.push_back(input.shift ? k.upper : k.lower);
    const bool done = pressed(key_return) || (input.fire && !fire_was_);
    if (!done) return;
    // A touch screen has no keys to type with: a name is given.
    if (player_name_.empty()) player_name_ = "Player";
    while (!player_name_.empty() && player_name_.back() == ' ') player_name_.pop_back();
    show(Screen::divisions);
}

// The season ($5D068): races without the player are decided at once; the
// player's own are shown.
void FrontEnd::next_league_race() {
    while (!league_.player_races()) {
        league_.play_round(random_, std::nullopt);
        if (!league_.next_race()) {
            after_season();
            return;
        }
    }
    show(Screen::fixture);
}

void FrontEnd::race_over(const RaceResult& result) {
    // Track records from the player's laps and, once all are done, the race.
    const bool super = league_.super_league();
    const auto holder = name(league::player);
    race::Time total{};
    for (const auto& lap : result.laps) {
        records_.offer_lap(super, picture_track_, holder, lap);
        total = league::add_times(total, lap);
    }
    if (result.finished && !result.laps.empty()) records_.offer_race(super, picture_track_, holder, total);
    if (!picture_league_) {
        open_menu(division_menu, 4, false, division_choice_);
        return;
    }
    league_.set_roll_bar_marks(result.roll_bar_marks);
    last_outcome_ = league_.play_round(random_, league::PlayerResult{!result.won, !result.fastest_lap});
    scene_ = result.wrecked ? data::Scene::wrecked : result.won ? data::Scene::won : data::Scene::lost;
    show(Screen::scene);
}

void FrontEnd::after_season() {
    changes_ = league_.end_season();
    show(Screen::changes);
}

// --- Drawing -------------------------------------------------------------

platform::Image FrontEnd::picture() const {
    render::FrontEndCanvas canvas(art_);
    switch (screen_) {
    case Screen::select:
    case Screen::division_menu:
    case Screen::practise_divisions:
    case Screen::practise_tracks:
    case Screen::load_save:
        draw_menu(canvas);
        break;
    case Screen::name:
        draw_name(canvas);
        break;
    case Screen::divisions:
        draw_divisions(canvas);
        break;
    case Screen::hall_of_fame:
        draw_hall(canvas);
        break;
    case Screen::track_picture:
    case Screen::racing:
        draw_track_picture(canvas);
        break;
    case Screen::fixture:
        draw_fixture(canvas);
        break;
    case Screen::scene:
    case Screen::promotion:
        canvas.picture(art_.scene(scene_));
        break;
    case Screen::result:
        draw_result(canvas);
        break;
    case Screen::standings:
        draw_standings(canvas);
        break;
    case Screen::changes:
        draw_changes(canvas);
        break;
    }
    return canvas.image();
}

// The panel with the division's name above it ($645C6).
void FrontEnd::draw_panel_title(render::FrontEndCanvas& c) const {
    c.menu_panel();
    c.offset(0, 0);
    c.ink(orange);
    if (league_.super_league()) {
        c.print(data::text(program_, Texts::menu, super_division_text));
    } else {
        c.print(data::text(program_, Texts::season, division_text));
    }
    c.print_number(4 - league_.division_of_player());
    c.ink(white);
}

void FrontEnd::draw_menu(render::FrontEndCanvas& c) const {
    const bool titled = !menu_.main;
    if (titled) draw_panel_title(c);
    else c.menu_panel();
    const auto table = menu_.main ? Texts::main : Texts::menu;
    c.offset(0, 0);
    c.ink(white);
    c.print(data::text(program_, table, select_text));
    c.offset(0, 4);
    for (int i = 0; i < menu_.count; ++i) {
        const int row = art_.bar_rows[std::size_t(i)];
        const Strip strip = i != menu_.chosen ? Strip::plain : menu_.taken >= 0 ? Strip::taken : Strip::highlight;
        c.bar(row, strip);
        c.ink(black);
        c.paper(std::nullopt);
        c.move(5, row);
        c.print_number(i + 1);
        c.print(". ");
        c.print(data::text(program_, table, program_.u8(menu_entries + std::uint32_t(menu_.base + i))));
        if (menu_.base == practise_division_menu) c.print_number(i + 1);
        if (menu_.base == practise_track_menu && i < 2) {
            c.print(data::text(program_, Texts::season, the_text));
            c.print(data::track_name(program_, league_.track_of(practise_division_, i)));
        }
    }
}

void FrontEnd::draw_name(render::FrontEndCanvas& c) const {
    c.menu_panel();
    c.offset(0, 0);
    c.ink(white);
    c.print(data::text(program_, Texts::season, name_text));
    c.offset(0, 4);
    c.bar(art_.bar_rows[1], Strip::plain);
    c.fill(underline_left, underline_y, underline_right - underline_left, 1, line_colour);
    c.ink(black);
    c.paper(bar_grey);
    c.move(name_column, name_row);
    c.print(">");
    c.print(player_name_);
    c.paper(std::nullopt);
}

// The divisions ($58888): the portrait sheet, each place's driver drawn
// over it; the player's helmet carries the name in the small font.
void FrontEnd::draw_divisions(render::FrontEndCanvas& c) const {
    c.picture(art_.portraits);
    for (int slot = 0; slot < league::drivers; ++slot) {
        const int driver = league_.driver_in(slot);
        const auto at = art_.place(data::Place::division_grid, slot);
        c.portrait(driver, at);
        if (driver == league::player) {
            // Centred by half the name's trailing spaces, three pixels in,
            // 44 rows down ($59250).
            const auto n = name(driver);
            const auto used = n.find_last_not_of(' ');
            const int trailing = used == std::string::npos ? name_length : name_length - int(used) - 1;
            c.name_font(true);
            c.menu_rows(false);
            c.ink(white);
            c.paper(std::nullopt);
            c.offset(at.x + 3, at.y + 44);
            c.move(trailing / 2, 0);
            c.print(n);
            c.name_font(false);
            c.menu_rows(true);
            c.offset(0, 4);
        }
    }
}

// "RACE  n of 6" on `row` ($58E30).
void FrontEnd::draw_race_heading(render::FrontEndCanvas& c, int row) const {
    c.ink(white);
    auto t = data::text(program_, Texts::season, race_text);
    t[2] = char(row);
    c.print(t);
    c.print_number(league_.race() + 1);
    c.print(data::text(program_, Texts::season, of_text));
    c.print_number(league::races_per_season);
}

void FrontEnd::draw_fixture(render::FrontEndCanvas& c) const {
    draw_panel_title(c);
    c.offset(0, 0);
    draw_race_heading(c, 11);
    c.print(data::text(program_, Texts::table, fixture_v_text));
    const auto pair = league_.pair(league_.division_of_player());
    c.portrait(pair[0], art_.place(data::Place::fixture_first));
    c.portrait(pair[1], art_.place(data::Place::fixture_second));
    // "Track:  The NAME" in orange ($5EAE0).
    const int track = league_.track(league_.division_of_player());
    c.ink(orange);
    c.offset(0, 0);
    c.move(program_.u8(track_name_columns + std::uint32_t(track)) - 6, 20);
    c.print(data::text(program_, Texts::main, track_text));
    c.print(data::track_name(program_, track));
    // The track's records in a green box ($5F074).
    const bool super = league_.super_league();
    const int row = art_.bar_rows[records_bar];
    c.bar(row, Strip::green, 1);
    c.offset(0, 4);
    c.ink(black);
    c.move(5, row);
    c.print(data::text(program_, Texts::records, race_time_text));
    print_holder(c, records_.race(super, track));
    c.print(" ");
    print_time(c, records_.race(super, track).time);
    c.move(5, row + 1);
    c.print(data::text(program_, Texts::records, best_lap_text));
    print_holder(c, records_.lap(super, track));
    c.print(" ");
    print_time(c, records_.lap(super, track).time);
}

// A record's holder, or dashes ($5EC48, $5EC55).
void FrontEnd::print_holder(render::FrontEndCanvas& c, const league::Record& r) const {
    c.print(r.time ? r.holder : std::string(name_length, '-'));
}

// M:SS.hh, or seven dashes when there is no time ($5ED40).
void FrontEnd::print_time(render::FrontEndCanvas& c, const std::optional<race::Time>& t) const {
    if (!t) {
        c.print("-------");
        return;
    }
    c.print_number(race::decimal(t->minutes));
    c.print(":");
    const auto two = [&](std::uint8_t bcd) {
        c.print(std::string{char('0' + (bcd >> 4)), char('0' + (bcd & 15))});
    };
    two(t->seconds);
    c.print(".");
    two(t->hundredths);
}

void FrontEnd::draw_result(render::FrontEndCanvas& c) const {
    draw_panel_title(c);
    c.offset(0, 4);
    draw_race_heading(c, 10);
    // The pair on a bar ($648D2), then RESULT and the two portraits.
    const auto pair = league_.pair(league_.division_of_player());
    const int row = art_.bar_rows[17];
    c.bar(row, Strip::plain);
    c.ink(black);
    c.move(5, row);
    c.print(" " + name(pair[0]));
    c.print(data::text(program_, Texts::season, versus_text));
    c.print(" " + name(pair[1]));
    c.offset(0, 0);
    c.ink(white);
    c.print(data::text(program_, Texts::season, result_text));
    c.offset(0, 4);
    c.print(data::text(program_, Texts::table, points_text));
    c.portrait(last_outcome_.winner, art_.place(data::Place::result_winner));
    c.portrait(last_outcome_.fastest, art_.place(data::Place::result_fastest));
}

void FrontEnd::draw_standings(render::FrontEndCanvas& c) const {
    draw_panel_title(c);
    const auto order = league_.standings(league_.division_of_player());
    c.trimmed_portrait(order[0], art_.place(data::Place::first), standings_shifts[0]);
    c.trimmed_portrait(order[1], art_.place(data::Place::second), standings_shifts[1]);
    c.trimmed_portrait(order[2], art_.place(data::Place::third), standings_shifts[2]);
    c.offset(4, 4);
    c.ink(white);
    c.print(data::text(program_, Texts::table, places_text));
    for (int k = 0; k < 3; ++k) {
        const int driver = order[std::size_t(k)];
        const auto line = [&](int row, int dy, int label, int value) {
            c.offset(0, dy);
            c.ink(orange);
            c.move(standings_columns[k], row);
            c.print(data::text(program_, Texts::table, label));
            c.ink(white);
            if (value < 10) c.print(" ");
            c.print_number(value);
        };
        line(raced_row, 4, raced_text, league_.raced(driver));
        line(wins_row, 0, wins_text, league_.wins(driver));
        line(laps_row, 4, laps_text, league_.fastest_laps(driver));
        line(points_row, 0, points_label_text, league_.points(driver));
    }
}

// The season's end ($64916): " CHANGES", then a bar for who goes up and
// one for who goes down.
void FrontEnd::draw_changes(render::FrontEndCanvas& c) const {
    draw_panel_title(c);
    c.offset(0, 0);
    c.ink(white);
    c.move(15, 12);
    c.print(data::text(program_, Texts::season, changes_text));
    c.offset(0, 4);
    int bar = 1;
    if (changes_.league_won) {
        c.bar(art_.bar_rows[std::size_t(bar)], Strip::plain);
        c.ink(black);
        c.move(5, art_.bar_rows[std::size_t(bar)]);
        c.print(data::text(program_, Texts::menu, well_done_text));
        return;
    }
    if (changes_.promoted) {
        const int row = art_.bar_rows[std::size_t(bar++)];
        c.bar(row, Strip::plain);
        c.ink(black);
        c.move(5, row);
        c.print(data::text(program_, Texts::season, promotion_text));
        c.print(" " + name(*changes_.promoted));
        if (changes_.to_super_league) {
            const int next = art_.bar_rows[std::size_t(bar++)];
            c.bar(next, Strip::plain);
            c.move(5, next);
            c.print(data::text(program_, Texts::menu, super_league_text));
        }
    }
    if (changes_.relegated) {
        const int row = art_.bar_rows[std::size_t(bar)];
        c.bar(row, Strip::plain);
        c.ink(black);
        c.move(5, row);
        c.print(data::text(program_, Texts::season, relegation_text));
        c.print(" " + name(*changes_.relegated));
    }
}

// The track picture ($5D20C): the stadium, the circuit drawn into its
// window, the track's name, and the prompt.
void FrontEnd::draw_track_picture(render::FrontEndCanvas& c) const {
    c.picture(art_.stadium);
    if (track_picture_) track_picture_(c, picture_track_, picture_view_);
    c.menu_rows(false);
    c.offset(0, 4);
    c.ink(15);
    c.move(program_.u8(track_name_columns + std::uint32_t(picture_track_)), 21);
    c.print(data::text(program_, Texts::season, caption_text).substr(3));
    c.print(data::track_name(program_, picture_track_));
    // The cracked copy replaced the start of this line with its own credit;
    // only the prompt is the game's.
    c.ink(3);
    const auto line = data::text(program_, Texts::season, fire_text);
    const auto prompt = line.find("Hit fire");
    if (prompt != std::string::npos) {
        c.offset(0, 0);
        c.move(3 + int(prompt) - 3, 24);
        c.print(line.substr(prompt));
    }
}

// The Hall of Fame ($5FBC6): each track's lap and race records, the
// tracks from Draw Bridge down to Little Ramp.
void FrontEnd::draw_hall(render::FrontEndCanvas& c) const {
    c.picture(art_.hall_of_fame);
    c.menu_rows(false);
    c.paper(std::nullopt);
    const bool super = league_.super_league();
    c.offset(2, 0);
    if (super) {
        c.ink(hall_super_ink);
        c.print(data::text(program_, Texts::records, super_league_title));
    }
    c.ink(hall_header_ink);
    c.print(data::text(program_, Texts::records, hall_header));
    for (int track = league::Records::tracks - 1; track >= 0; --track) {
        c.offset(1, 0);
        c.move(0, hall_first_row + 2 * (league::Records::tracks - 1 - track));
        c.ink(orange);
        c.print(std::string{char(program_.u8(hall_initials + std::uint32_t(track * 2))),
                            char(program_.u8(hall_initials + std::uint32_t(track * 2 + 1))), ' '});
        int dx = 4;
        for (const auto* record : {&records_.lap(super, track), &records_.race(super, track)}) {
            c.offset(dx, 0);
            c.ink(hall_name_ink);
            print_holder(c, *record);
            c.ink(orange);
            c.print(" ");
            c.offset(dx - 1, 0);
            print_time(c, record->time);
            c.print("  ");
            dx = 2;
        }
    }
}

} // namespace stuntcar::game
