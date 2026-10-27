#pragma once
#include "amiga/iff.hpp"
#include "cannon_fodder_amiga/game/map_view.hpp"
#include "cannon_fodder_amiga/game/objects.hpp"
#include <cstdint>
#include <vector>

// Objects drawn over the map background (.analysis/cannon_fodder_amiga/
// game.md "Drawing objects").
//
// The original draws them into the back buffer once per tick ($A38D6 →
// $AB828 draw list, $AB9B6 positions, $A378C blitter cookie-cut) and erases
// them next time ($AB974). What reaches the screen: each object's frame
// from its sheet, where the sheet's colour is not 0, placed by the object's
// position, the frame's offsets, its height and the object's height above
// ground, in the draw list's order.
namespace fodder::game {

// The landscape's two object sheets (`<prefix>army.lbm` and
// `<prefix>copt.lbm`), as loaded by the original (exact copies of the
// pictures; their own colour maps are not used: objects take the play
// view's colours).
struct ObjectSheets {
    amiga::IlbmPicture army, vehicles;
};

// The draw list ($AB828), built from the camera snapshot of the last
// snapshot tick (game/camera.hpp) and its directions: objects with x in
// [camera.x - 30, camera.x + 314] (moving left: [camera.x - 36, ...]) and y in
// [max(camera.y - 4, 0), camera.y + 331], shifted by 4 x the vertical
// direction; layer < 0 in slot order, then layer 0 ordered by the
// original's exchange sort on y, then layer > 0 in slot order. Slots 0-42
// only. Removed objects are freed here.
std::vector<int> draw_list(MissionObjects& objects, Camera snapshot, int direction_x, int direction_y);

// Where objects are drawn: colour indices of a picture whose pixel (0, 0)
// shows map pixel (origin_x, origin_y); columns left of first_column are
// left alone.
struct ObjectTarget {
    std::vector<std::uint8_t>& pixels;
    int width{}, height{};
    int origin_x{}, origin_y{};
    int first_column{};
};

// Draws the listed objects. Lines cut by an object's hidden_lines come off
// the frame's bottom; the frame's last 16 pixels of width are never drawn
// (the blit's last-word mask is 0, that word only carries the shift).
void draw_objects(ObjectTarget target, const MissionObjects& objects, const std::vector<int>& order,
                  const FrameTables& frames, const ObjectSheets& sheets);

// The same onto a 320 x 224 view (view_indices layout) for the camera.
void draw_objects(std::vector<std::uint8_t>& view, Camera camera, const MissionObjects& objects,
                  const std::vector<int>& order, const FrameTables& frames, const ObjectSheets& sheets);

} // namespace fodder::game
