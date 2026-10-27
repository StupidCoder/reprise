#pragma once
#include "wings_of_fury_amiga/data/assets.hpp"
#include <cstdint>
#include <vector>

// The play view's backdrop: sky, the map's scenery, the wave strip and the
// islands' sand, drawn the way `wings` draws them each frame (.analysis/
// wings_of_fury_amiga/formats.md, "Maps"). Objects (planes, ships' guns,
// soldiers, flags) are drawn by other parts of the game on top.
//
// All of it is placed relative to the camera: a world x appears at screen
// x = world x - camera + 160, minus the shape's handle.
namespace wings::game {

// A picture as colour indices, the way the game's 5-plane view holds it.
struct IndexedCanvas {
    int width{};
    int height{};
    std::vector<std::uint8_t> pixels;

    IndexedCanvas(int w, int h) : width(w), height(h), pixels(std::size_t(w * h)) {}
    std::uint8_t at(int x, int y) const { return pixels[std::size_t(y * width + x)]; }
    void fill(int x0, int y0, int x1, int y1, std::uint8_t colour); // inclusive, clipped
    void draw(const data::Shape&, int x, int y);                     // top-left corner, clipped
};

// The islands of a map: each runs from a column drawn with `bchl` (scenery
// index 1) to the next drawn with `bchr` (index 2); the load scan $102DE8
// counts them at the right ends, and the sand drawer reads the edges as the
// columns' byte offsets ($1040E8: world x = offset x 4 = column x 8).
struct Island {
    int left_column{};
    int right_column{};
};
std::vector<Island> islands(const data::MissionMap&);

// What the backdrop needs from the running game.
struct BackdropState {
    int camera{};       // world x at the screen's x 160 (-$41A2)
    int wave_frame{};   // 11 .. 0, one step every second frame (-$5C70, $10105E)
    int vertical_scroll{}; // -$41A8: added to columns on a ship
    // Ships the scenery rides on ($104A4E: four enemy slots, each with an
    // active flag, then the US carrier). A ship covers the byte offsets
    // first..last of the map (column x 2); a surface-1 column c is on the
    // first listed ship covering (c + 1) x 2 and is drawn its bob (+$1A) +
    // the vertical scroll lower; on no ship, it is drawn unmoved.
    struct Ship {
        int first{}, last{}, bob{};
    };
    std::vector<Ship> ships; // active ones, in the game's search order
    // Map specials ($103B1C), state kept by the game:
    int us_flag_step{};        // 0..3 (-$5C71): the tower flag's frame step
    int crew_pose_a{};         // deck crew poses (-$59A8, -$59A4), stepped by the
    int crew_pose_b{};         //   deck phase each frame ($103BCE)
    int enemy_flag_counter{};  // 0..2 (-$5C7E) before the frame; each enemy
                               //   flag column steps it once
    std::vector<bool> island_flag_up; // per island (-$5BAE words); an island not listed = up
};

constexpr int view_width = 320;
constexpr int view_height = 162;
constexpr int ground_line = 0x97; // the scenery line and the sea's surface (-$5C5E, -$60CC)

// Draws the backdrop into a 320 x 162 canvas, in the game's order:
//   $103784  sky: colour 1 from the top to the ground line + 1
//   $1037BC  the map's scenery columns, each followed by its special:
//            $22 the waving US flag on the tower ($103D10), $9F the deck
//            crew ($103BCE), both moved with the US carrier; $113 an island's enemy flag or bare pole
//            ($103B52); index 5 ($104E18) is not rebuilt yet
//   $103E6C  the wave strip: five copies of the frame, 96 pixels apart
//   $1040E8  each island's sand (colour 17, the ground line and 4 rows
//            below) and its beach ends
IndexedCanvas draw_backdrop(const data::Assets&, const data::MissionMap&, const BackdropState&);

} // namespace wings::game
