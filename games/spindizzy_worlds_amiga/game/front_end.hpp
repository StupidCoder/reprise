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
    // The clusters drawn with the map's turn as it stands.
    ClusterChoice(const data::Assets&, const view::ClusterView::Turn&);
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
    // `breaking`: the world just done, shown breaking up ($11838).
    WorldMap(const data::Assets&, bool easy, std::uint32_t worlds_left, const view::ClusterView::Turn&, int breaking = -1);
    // −1 still choosing, else the world number entered.
    int frame(std::uint8_t joystick);
    // $11838: the map after a world for $40 − $36B2 + 1 frames, the done
    // planet breaking up (its frames stepped once a frame plus once each
    // for $36B2 ≥ $10 and ≥ $20); the joystick still turns the cluster,
    // nothing is chosen. True once the frames are over.
    bool break_frame(std::uint8_t joystick, bool slow);
    view::ClusterView::Turn turn() const { return cluster_.turn(); }
    // $11798: a locked world's message over the map until fire.
    void refuse() { refused_ = true; released_ = false; }
    void draw(view::Screen&) const;

private:
    const data::Assets& assets_;
    std::vector<view::Planet> planets_;
    std::vector<bool> shown_;
    view::ClusterView cluster_;
    int size_;            // $36B2: twice the cluster's worlds (Easy $12, Hard $2E; $11E30)
    int break_frames_{};
    int chosen_{-1};
    bool released_{};
    bool refused_{};
};

// An effect asked of the effects player (the request block $36D2).
struct EffectRequest { int channel, effect, volume; };

// $17CBE, once every world of the cluster is done: "Well done!" over the
// last map, effect 1 on channels 0-2 ($17CA0), 151 frames. Easy ($17D74,
// "Now try the Hard game.") then waits 51 frames and for fire. Hard
// ($17DA7) blows the screen up: the effects again, then for 48 frames
// a break-up picture at the centre ($16B62: pictures 4-11 in turn) and
// the centre's 160 × 100 pixels doubled to the whole screen ($17DFC),
// each frame on the last one; the frames' effects from $17C62; then 9
// frames of the doubling alone.
class Ending {
public:
    Ending(const data::Assets&, const view::Screen& shown, bool easy);
    // True once it is over.
    bool frame(std::uint8_t joystick);
    void draw(view::Screen& s) const { s = screen_; }
    std::vector<EffectRequest> sounds; // this frame's, taken by the module

private:
    void effects_on_three();          // $17CA0
    const data::Assets& assets_;
    view::Screen screen_;
    bool easy_;
    int frames_{};
    int picture_{};                   // d0 of $17CEE: 0-7
    bool fire_held_{true};
};

// A world done as $16B8C records it ($36B6 lists): its number, the fuel
// and reserve then (whole parts) and the jewels found and taken.
struct WorldRecord {
    int world{};
    int fuel{}, reserve{};
    int found{}, taken{};
};

// $16DA4 ("Check performance", and after each world): "You have not
// started", or the worlds done with fuel and jewels, then the reserve.
void draw_statistics(view::Screen&, const data::Assets&, const std::vector<WorldRecord>&);

// The palettes the front end sets: the title's (attract, menu) and the
// map's ($11666), each ST word doubled ($10A46).
void set_title_palette(view::Screen&, const data::Assets&);
void set_map_palette(view::Screen&, const data::Memory& main);

} // namespace spindizzy::game
