#pragma once
#include "gamegear/force_field.hpp"
#include "gamegear/wide_view.hpp"
#include <optional>
#include <vector>

// Widescreen enhancement, the parts both games apply the same way. None of
// this is cartridge behaviour: it adapts the reconstructed rules to a
// presented view larger than the 160x144 LCD. With the original view size
// every function here reproduces the LCD crop. Each game decides where its
// rules are bent (its enhancements/widescreen.cpp); the view size and its
// aspect limits are shared with Sonic 1 (gamegear/wide_view.hpp).
namespace gg_engine {

// The LCD shows lines 24-167 and columns 48-207 of the 256x192 picture.
inline constexpr int lcd_left = 48, lcd_top = 24;

// The presented view in picture coordinates (the LCD crop is 48,24 and
// 160x144); x/y may be negative or reach past the 256x192 picture.
struct ViewRect {
    int x{lcd_left}, y{lcd_top}, width{160}, height{144};
};

// Where the camera can go in an act (world pixels of its top left corner).
struct CameraArea {
    int left{}, top{}, right{}, bottom{};
};

// The view is centred on the LCD crop, then clamped so it never reveals more
// than the LCD could show anywhere in the act (`area` plus the crop). When
// that area is smaller than the view, the area is centred instead. The LCD
// crop itself always remains inside the view. `camera_x`, `camera_y`: the
// camera the picture's scroll was set for; in world pixels the picture
// starts at camera + (1, 17) in both games (the scroll is -(camera X + 1)
// and camera Y + 17), so the LCD crop starts at camera + (49, 41).
ViewRect wide_view_rect(const CameraArea& area, int camera_x, int camera_y, int width,
                        int height);

// The value congruent to `value` modulo `period` that is nearest `near`.
int lift(int value, int period, int near);

// What the arena barrier needs to know of a frame of play.
struct ArenaView {
    int act;      // a new act clears the field
    int camera_x; // world X of the camera
    // Whether the screen limits keep the player inside the LCD on each side
    // (where the camera cannot scroll on), and how far the view reaches past
    // the LCD there.
    bool left_wall, right_wall;
    int wide_left, wide_right;
    bool finished; // the player runs off through the wall (after the sign)
    bool alive;
    int player_x, player_y; // world pixels
    bool pressing_left, pressing_right;
    // Bosses, which the screen limits do not hold: they cross the line and
    // the field lets them through with the same ripple.
    struct Body {
        int x, y, half_width, half_height;
    };
    std::vector<Body> bosses;
};

// Force field drawn where a locked arena's invisible walls fall inside the
// larger view. The walls are the player's screen limits (screen X 38 and C7:
// the LCD's edges; Sonic 2 3BF5, Triple Trouble B2A7), which remain where
// the camera cannot scroll on; nothing here changes them. Cosmetic:
// independent of the game state.
class ArenaBarrier {
public:
    void reset();
    // `view`: the frame of play, or nothing outside play or without the
    // enhancement (the field clears).
    void tick(const std::optional<ArenaView>& view);
    // `view_x`, `view_y`: the world position (object coordinates) shown at
    // the image's top left.
    void draw(gg::Screen&, int view_x, int view_y) const;

    // Calls f on each part of the state (quick saves).
    template <class F>
    void for_each_state(F&& f) {
        field_.for_each_state(f);
        f(act_);
    }

private:
    gg::ForceField field_;
    int act_{-1};
};

} // namespace gg_engine
