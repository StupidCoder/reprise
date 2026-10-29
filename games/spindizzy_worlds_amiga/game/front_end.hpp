#pragma once
#include "spindizzy_worlds_amiga/data/assets.hpp"
#include "spindizzy_worlds_amiga/view/front.hpp"
#include "spindizzy_worlds_amiga/view/screen.hpp"
#include <cstdint>
#include <vector>

// The front end ($18950 … $18A78, $116F0): the title and its attract
// pages, the menu, the Easy/Hard cluster choice and the world map. Each
// screen runs one frame at a time on the joystick byte and draws the
// whole screen.
namespace spindizzy::game {

// $1771C: the title for 500 frames, then the pages (each the title again
// with its text, block pictures and animated sprites) for 500 frames each;
// fire or a direction ends it ($17C52).
class Attract {
public:
    explicit Attract(const data::Assets&);
    // True when the player pressed something.
    bool frame(std::uint8_t joystick);
    void draw(view::Screen&) const;

private:
    void start_page();

    const data::Assets& assets_;
    int page_{-1};   // −1 the title alone
    int frames_{};
    view::Screen picture_;
    std::vector<view::AttractSprite> sprites_;
};

// $17E46 over the menu at $18B44: items with their own cursor positions;
// up and down choose, fire takes it; the chosen item flashes orange
// ($37B2 bit 6). "Slow Mode?" shows its bit ($24A6) and left/right set it.
class Menu {
public:
    Menu(const data::Assets&, const view::Screen& behind, std::uint32_t items, std::uint32_t header, int timeout);
    // −2 still choosing, −1 timed out, else the item taken.
    int frame(std::uint8_t joystick);
    void draw(view::Screen&) const;
    bool slow_mode{true};

private:
    const data::Assets& assets_;
    view::Screen behind_;
    std::uint32_t items_, header_;
    std::vector<std::uint32_t> item_at_;
    int chosen_{};
    int timeout_;
    int counter_{};    // $37B2
    std::uint8_t last_{0xFF}; // what opened the menu must be let go first
};

// $11E52: both clusters drawn small; holding left or right lights one,
// fire pressed and released with the direction still held takes it.
class ClusterChoice {
public:
    explicit ClusterChoice(const data::Assets&);
    // 0 still choosing, 1 Easy, 2 Hard ($36B4).
    int frame(std::uint8_t joystick);
    void draw(view::Screen&) const;

private:
    const data::Assets& assets_;
    view::Screen picture_;
    int lit_{};       // 1 Easy, 2 Hard
    bool fire_down_{};
};

// $116F0: the chosen cluster turning in the box; the nearest planet is
// the world fire enters; its name above. Worlds done are gone.
class WorldMap {
public:
    WorldMap(const data::Assets&, bool easy, std::uint32_t worlds_left);
    // −1 still choosing, else the world number entered.
    int frame(std::uint8_t joystick);
    void draw(view::Screen&) const;

private:
    const data::Assets& assets_;
    std::vector<view::Planet> planets_;
    std::vector<bool> shown_;
    view::ClusterView cluster_;
    int chosen_{-1};
    bool released_{};
};

// The palettes the front end sets: the title's (attract, menu) and the
// map's ($11666), each ST word doubled ($10A46).
void set_title_palette(view::Screen&, const data::Assets&);
void set_map_palette(view::Screen&, const data::Memory& main);

} // namespace spindizzy::game
