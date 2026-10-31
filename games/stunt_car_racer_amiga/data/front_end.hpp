#pragma once
#include "stunt_car_racer_amiga/data/assets.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// The front end's artwork and texts (game.md, "Front end"): the menu
// picture (which also holds the menu bars' colour strips and the six-pixel
// name font on its first plane), the stadium frame around the track
// picture, the Hall of Fame, the drivers' portraits, the four pictures
// after a race, and the message tables the menus and league screens print.
namespace stuntcar::data {

// A text as the game prints it: characters, and 0x1F x y to move the
// cursor to column x, row y.
using Text = std::string;
constexpr char move_cursor = 0x1F;

// The game's message tables, each a run of 0xFF-ended texts named by
// where they start.
enum class Texts : std::uint32_t {
    menu = 0x5A69A,      // division menu, load/save, super league ($5A656)
    main = 0x5EB7F,      // SELECT, the league choice, track lists ($5A67C)
    records = 0x5EC92,   // track records, Hall of Fame ($5EC7E)
    table = 0x5915E,     // the standings screen ($5914A)
    season = 0x64354,    // division, race, result, changes ($64340)
};

// After a race: the crowd cheering, the crowd turning away, the wreck, the
// promotion trophy (the picture table at $56FF8, entries 1-4).
enum class Scene { won, lost, wrecked, promoted };

// Where portraits go ($58AA4, from entry 12 on), by the game's numbers.
enum class Place {
    fixture_first = 0, fixture_second = 1,   // the race's pair
    result_winner = 2, result_fastest = 3,  // the RESULT screen
    first = 4, second = 5, third = 6,        // the standings
    division_grid = 7,                       // + place 0-11 of the divisions screen
};

struct Point {
    int x{}, y{};
};

struct FrontEndArt {
    Picture menu;          // $23C30
    Picture stadium;       // $2B974 (colours), $2B994 (packed planes)
    Picture hall_of_fame;  // $30BD0, $30BF0
    Picture portraits;     // $35A76, $35A96: the divisions screen as first drawn
    std::array<Picture, 4> scenes;
    std::array<std::array<std::uint8_t, 8>, 96> font{};      // the dashboard font from ' ' (7 pixels wide)
    std::array<std::array<std::uint8_t, 8>, 96> name_font{}; // six pixels wide, bit 7 the left
    // Portraits: each driver's portrait ($58B20), where each portrait lies
    // in the sheet and where they are drawn on screen ($58AA4).
    std::array<std::uint8_t, 12> portrait_of_driver{};
    std::array<Point, 12> portrait_in_sheet{};
    std::array<Point, 19> portrait_places{};
    // The rows the menu bars' strips start on in the menu picture ($64C1A),
    // and the text rows bars are drawn on, by bar number ($64AF4).
    std::array<int, 5> strip_rows{};
    std::array<std::uint8_t, 26> bar_rows{};
    const Picture& scene(Scene s) const { return scenes[std::size_t(s)]; }
    Point place(Place p, int k = 0) const { return portrait_places[std::size_t(int(p) + k)]; }
};

FrontEndArt load_front_end_art(const Program&);

// The text starting at `index` in a table (without its 0xFF).
Text text(const Program&, Texts, int index);

// A driver's name (12 characters, the player's in slot 11) and a track's
// (15 characters), as stored ($1ECAA, $1EDAA; 16 bytes apart).
std::string driver_name(const Program&, int driver);
std::string track_name(const Program&, int track);

} // namespace stuntcar::data
