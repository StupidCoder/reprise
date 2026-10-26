#pragma once
#include "bruce_lee_c64/game/game.hpp"
#include "c64/text_frame.hpp"
#include <array>
#include <utility>
#include <vector>

// The widescreen view: the picture fills the window to its edges, and where
// the C64 shows its border the rooms beside the one in play show through,
// dimmed and still, placed so that their doors meet this room's. Taking an
// exit scrolls the view over to the next room, which lights up as the old
// one dims. The game plays as the original; only the picture is wider.
//
// The rooms do not make one consistent map (room 11's floor leads into
// three rooms, room 13 takes its place once they are cleared, and some
// doors meet at different heights), so each room shows only the
// neighbours its own doors lead to: on each side the room behind a door on
// that edge (or a room whose door on the facing edge leads here). Where a
// room has two doors into the same neighbour at different heights from
// that room's two, the neighbour slides up or down with Bruce's height
// between them, so it meets whichever door he reaches.
namespace bruce_lee::enhanced {

struct WideSize {
    int width{};    // of the picture to show (0: not widescreen)
    int margin_y{}; // border above and below, in windows narrower than the
                    // display window's shape
};
// For a canvas (window) of this size, with C64 pixels this wide for their
// height: up to 21:9; the narrowest is the original screen with its border.
WideSize wide_size(int canvas_width, int canvas_height, double pixel_aspect);

class WideView {
public:
    // Once per frame, after the game's tick: `centre` is the room as shown
    // (the game's picture or the enhanced one), `width` the picture to make.
    void update(const game::Game&, const c64::Picture& centre, int width);
    const c64::Picture& picture() const { return picture_; }

private:
    // A room beside the one in play, drawn as it is now (lanterns taken,
    // walls its script has opened) without anyone in it.
    struct Glimpse {
        int room{-1};
        int dy{};             // its rows drawn this many lines lower
        c64::Picture picture = c64::Picture();
        std::vector<std::pair<int, int>> anchors; // Bruce's y here at a door, dy there
        double shown_dy{};
    };
    // Something drawn into the view: a room picture with its top-left corner
    // at x, y, lit from 0 (dimmed) to 1.
    struct Placed {
        const c64::Picture* picture{};
        int x{}, y{};
        double lit{};
    };
    struct Transition {
        bool active{};
        int frame{};
        int dx{}, dy{};             // where the new room lies from the old one
        c64::Picture from = c64::Picture(); // the old room, without anyone in it
        Glimpse far;                // the old room's neighbour on the other side
        int came_side{-1};          // the new room's side the old one is on
    };

    Glimpse glimpse(const game::World&, int side, int hint_y, const Glimpse* forced) const;
    c64::Picture snapshot(const game::World&, int room) const;
    static double anchored_dy(const Glimpse&, int y);
    void place_glimpses(const game::World&, int hint_y, const Glimpse* came_from, int came_side);
    void draw(const Placed&, int left);

    std::array<Glimpse, 2> sides_{}; // west, east
    Transition transition_{};
    bool known_{};
    int room_{-1};
    int remaining_{-1};               // lanterns left in all rooms, when the glimpses were drawn
    std::uint8_t bruce_x_{}, bruce_y_{};
    c64::Picture last_centre_;
    c64::Picture picture_;
};

} // namespace bruce_lee::enhanced
