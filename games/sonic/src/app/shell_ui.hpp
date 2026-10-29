#pragma once
#include "app/game_module.hpp"
#include "app/shell_style.hpp"
#include "gamegear/music_player.hpp"
#include "platform/image.hpp"
#include <array>
#include <optional>
#include <span>
#include <string>

// The app's library: hardware generations, the game catalog, and the
// screens drawn by the shell (library, level selector, sound test, pause and
// enhancement menus, title cards). All shell artwork is drawn from simple
// shapes and bundled Lato typography; no game graphics are used.
namespace shell {

// Games are grouped by hardware generation. Each generation has its own visual
// theme; adding one later means a new entry here and its games in `catalog`.
struct Generation {
    const char* bits;
    const char* systems;
    style::Theme theme;
    bool available;
};

inline constexpr std::array<Generation, 3> generations = {{
    {"8-bit",
     "Game Gear",
     {{12, 22, 39},
      {19, 32, 51},
      {113, 229, 226},
      {70, 128, 255},
      {9, 27, 41},
      {235, 241, 248},
      {17, 35, 57},
      {81, 103, 125},
      0,
      1,
      10},
     true},
    {"16-bit",
     "Mega Drive / CD",
     {{12, 22, 39},
      {19, 32, 51},
      {255, 175, 117},
      {255, 110, 82},
      {37, 27, 26},
      {235, 241, 248},
      {17, 35, 57},
      {81, 103, 125},
      1,
      1,
      10},
     true},
    {"32-bit",
     "GBA / DS",
     {{12, 22, 39},
      {19, 32, 51},
      {190, 180, 255},
      {114, 119, 255},
      {26, 24, 47},
      {235, 241, 248},
      {17, 35, 57},
      {81, 103, 125},
      2,
      1,
      10},
     true},
}};

struct CatalogEntry {
    const char* title;
    const char* subtitle;
    const char* platform;
    int generation;
    platform::Color art; // card colour of the original geometric artwork
    bool implemented;
    const char* platform_short = nullptr; // where `platform` does not fit (small cards)
};

inline constexpr std::array<CatalogEntry, 13> catalog = {{
    {"Sonic 1", "", "Game Gear", 0, {43, 111, 224}, true},
    {"Sonic 2", "", "Game Gear", 0, {226, 112, 42}, true},
    {"Triple Trouble", "", "Game Gear", 0, {138, 63, 209}, true},
    {"Sonic 1", "", "Mega Drive", 1, {36, 87, 214}, false},
    {"Sonic 2", "", "Mega Drive", 1, {224, 64, 47}, false},
    {"Sonic 3", "", "Mega Drive", 1, {27, 158, 122}, false},
    {"Sonic", "& Knuckles", "Mega Drive", 1, {214, 58, 90}, false},
    {"Sonic 3", "& Knuckles", "Mega Drive", 1, {123, 63, 214}, false},
    {"Sonic CD", "", "Sega CD", 1, {31, 143, 214}, false},
    {"Sonic Advance", "", "Game Boy Advance", 2, {38, 120, 230}, true, "GBA"},
    {"Sonic Advance 2", "", "Game Boy Advance", 2, {230, 150, 30}, false, "GBA"},
    {"Sonic Advance 3", "", "Game Boy Advance", 2, {200, 52, 120}, false, "GBA"},
    {"Sonic Rush", "", "Nintendo DS", 2, {40, 170, 150}, false, "DS"},
}};
// Catalog index of a generation's first game, and its number of games.
int first_game(int generation);
int game_count(int generation);

struct LibraryView {
    int selection{};       // catalog index
    int generation{};      // focused generation
    int generation_from{}; // the one focused before, for the panels' resize
    bool zone{};           // false: choose a generation; true: browse its games
    std::array<int, generations.size()> picks{0, 3, 9}; // last game per generation (catalog index)
    bool ready{}, importing{}, choosing{};              // ready: the selected game is installed
    std::array<bool, catalog.size()> installed{};       // per catalog entry
    bool sound_test{};                                  // the selected game offers the sound test
    unsigned progress{};
    std::string message;
    double time{};          // shell clock (seconds) for animation
    double moved_at{-10.0}; // time of the last focus change
    double slide_from{};    // previous carousel position within the generation
};

// Hit testing returns a catalog index or one of these codes.
inline constexpr int hit_generation = 100, hit_back = 200, hit_action = 201;
int library_hit(const LibraryView&, int x, int y);
bool library_music_hit(const LibraryView&, int x, int y);
platform::Image library_image(const LibraryView&);

struct LevelLayout;
platform::Image level_select_image(const LevelLayout& layout, std::string_view title, int selection,
                                   std::string_view message = {}, double time = 0);
int music_hit(const gg::MusicPlayer&, int selection, int x, int y);
platform::Image music_image(const gg::MusicPlayer&, std::string_view game, int selection, int focus,
                            bool muted, double time = 0);
int pause_hit(int x, int y);
// A row of the enhancements page (0..count-1), count for Back, or -1.
int enhancements_hit(int x, int y, int count);

// What the in-game overlay shows: the pause menu (or its enhancements page),
// or just a message while playing.
struct PauseMenuView {
    std::string_view status;  // What is running, e.g. "Green Hill / Act 1"
    std::string_view message; // A notice such as "Quick saved."
    bool paused{};
    bool muted{};
    bool saved{};             // A quick save exists
    bool enhancements_open{}; // Showing the enhancements page
    int selection{};          // Selected pause menu item
    int enhancement_selection{};
    double since{1};                      // Seconds since the menu opened, for its slide-in
    std::string_view game;                // The running game, e.g. "Sonic 2 / Game Gear"
    std::span<const Enhancement> offered; // The game's enhancements, in menu order
    Enhancements enhancements;
};

platform::Image game_overlay(const PauseMenuView&);
platform::Image frame_rate_image(std::optional<double> fps, int canvas_height);

// Launch title card with transparent background. enter: 0 (off-screen) to 1
// (fully covering); leave: 0 (covering) to 1 (gone).
platform::Image title_card(int width, int height, std::string_view title, std::string_view subtitle,
                           int generation, double enter, double leave);
// Draws the remaining part of `from` over `to` behind a diagonal wipe.
void wipe(platform::Image& to, const platform::Image& from, double progress, int direction);
void composite(platform::Image& base, const platform::Image& layer);

} // namespace shell
