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
    // The blitter routines' clip window (-$46A2 top, -$46A0 bottom, -$469E
    // left, -$469C right; bottom and right exclusive), set with -$7C98.
    struct Clip {
        int left{}, top{}, right{}, bottom{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(left, top, right, bottom); }
    };

    int width{};
    int height{};
    std::vector<std::uint8_t> pixels;
    Clip clip;
    // Where x 0 lies in `pixels`: a widescreen view is the original's view
    // with `margin` more columns on each side, so the game still draws in
    // its own x (0 .. 319) and what lies beyond reaches into the margins.
    int origin_x{};

    IndexedCanvas(int w, int h, int margin = 0)
        : width(w + 2 * margin), height(h), pixels(std::size_t((w + 2 * margin) * h)), clip{-margin, 0, w + margin, h},
          origin_x(margin) {}
    // (In pixels, from the canvas's left edge.)
    std::uint8_t at(int x, int y) const { return pixels[std::size_t(y * width + x)]; }
    // $105174 and the other object drawers skip a picture whose screen x is
    // outside -$80..$1C0, the view's width + $A0; here as far beyond the
    // clip window's sides.
    bool beyond(int x) const { return x < clip.left - 0x80 || x > clip.right + 0xA0; }
    void fill(int x0, int y0, int x1, int y1, std::uint8_t colour); // inclusive, clipped
    void draw(const data::Shape&, int x, int y);                     // top-left corner, clipped
    // The shape mirrored left to right, as `wings` $105B58 leaves it in
    // memory (rows reversed; the handle becomes width - 1 - offset x, which
    // the caller applies).
    void draw_mirrored(const data::Shape&, int x, int y);
    // $110E24: no mask; the bitplanes of byte 13 are inverted over the
    // shape's whole rectangle, then each stored plane is XORed into its
    // bitplanes (BLTCON0 minterm $6A with A = $FFFF: D = C xor B).
    void draw_xor(const data::Shape&, int x, int y);
    // $110B0C entered without a mask (-$7CE0): the whole rectangle is
    // written (cleared/set planes and the stored planes everywhere).
    void draw_opaque(const data::Shape&, int x, int y);
    // $111318: a line in one colour, its ends first moved onto the clip
    // window's edges (left, right, top, bottom in turn, by integer
    // proportion), then drawn by the blitter's line mode (Bresenham, both
    // ends included).
    void line(int x0, int y0, int x1, int y1, std::uint8_t colour);

private:
    bool inside(int x, int y) const { return x >= clip.left && x < clip.right && y >= clip.top && y < clip.bottom; }

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(width, height, pixels, clip); }
};

// The islands of a map: each runs from a column drawn with `bchl` (scenery
// index 1) to the next drawn with `bchr` (index 2); the load scan $102DE8
// counts them at the right ends, and the sand drawer reads the edges as the
// columns' byte offsets ($1040E8: world x = offset x 4 = column x 8).
struct Island {
    int left_column{};
    int right_column{};

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(left_column, right_column); }
};
std::vector<Island> islands(const data::MissionMap&);

// What the backdrop needs from the running game.
struct BackdropState {
    int camera{};       // world x at the screen's x 160 (-$41A2)
    int margin{};       // widescreen columns on each side of the original's 320
    // The scenery line and the sea's surface (-$5C5E = -$60CC): $97, or
    // $97 + altitude - $83 while the plane is above $83 ($100286), so the
    // world scrolls down under a high plane. See ground_line_for.
    int ground_line{0x97};
    // The 1/8-scale view (-$60C8 = 1, switched on high up): one pixel per
    // map column, 8thscale.shp shapes on the line $97, no specials, the sea a
    // plain band and each island's sand one row.
    bool eighth_scale{};
    int wave_frame{};   // 11 .. 0, one step every second frame (-$5C70, $10105E)
    int vertical_scroll{}; // -$41A8: added to columns on a ship
    // Ships the scenery rides on ($104A4E: four enemy slots, each with an
    // active flag, then the US carrier). A ship covers the byte offsets
    // first..last of the map (column x 2); a surface-1 column c is on the
    // first listed ship covering (c + 1) x 2 and is drawn its bob (+$1A) +
    // the vertical scroll lower; on no ship, it is drawn unmoved.
    struct Ship {
        int first{}, last{}, bob{};

      public:
        // Save schema v1: explicit values; imported resources stay bound to this instance.
        template <class A> void persist(A &ar) { ar(first, last, bob); }
    };
    std::vector<Ship> ships; // active ones, in the game's search order
    // Map specials ($103B1C), state kept by the game:
    int us_flag_step{};        // 0..3 (-$5C71): the tower flag's frame step
    int crew_pose_a{};         // deck crew poses (-$59A8, -$59A4), stepped by the
    int crew_pose_b{};         //   deck phase each frame ($103BCE)
    int enemy_flag_counter{};  // 0..2 (-$5C7E) before the frame; each enemy
                               //   flag column steps it once
    std::vector<bool> island_flag_up; // per island (-$5BAE words); an island not listed = up

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) {
        ar(camera, ground_line, eighth_scale, wave_frame, vertical_scroll, ships, us_flag_step, crew_pose_a,
           crew_pose_b, enemy_flag_counter, island_flag_up);
    }
};

// How many of each animated special the next backdrop draws: the flags and
// the crew step only as they are drawn.
struct SpecialsShown {
    int us_flag{}, deck_crew{}, enemy_flag{};
    std::vector<int> wrecked_huts; // the drawn wrecked huts' column offsets ($104E18, in both views)

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(us_flag, deck_crew, enemy_flag, wrecked_huts); }
};
SpecialsShown specials_shown(const data::MissionMap&, const BackdropState&);

constexpr int view_width = 320;
constexpr int view_height = 162;
constexpr int normal_ground_line = 0x97;
constexpr int ground_line_for(int altitude) {
    return altitude > 0x83 ? normal_ground_line + altitude - 0x83 : normal_ground_line;
}

// Draws the backdrop into a 320 x 162 canvas, in the game's order:
//   $103784  sky: colour 1 from the top to the ground line + 1
//   $1037BC  the map's scenery columns, each followed by its special:
//            $22 the waving US flag on the tower ($103D10), $9F the deck
//            crew ($103BCE), both moved with the US carrier; $113 an island's enemy flag or bare pole
//            ($103B52); index 5, a wrecked hut ($104E18): its smoke (islands)
//   $103E6C  the wave strip: five copies of the frame, 96 pixels apart
//   $1040E8  each island's sand (colour 17, the ground line and 4 rows
//            below) and its beach ends
// The objects ($103ABC … $10391E: parked planes, the player, ships' guns,
// enemies …) are drawn between the two passes, so the waves and the sand
// cover what reaches below the surface.
IndexedCanvas draw_backdrop(const data::Assets&, const data::MissionMap&, const BackdropState&);
// The two passes: sky and scenery columns; then the waves and the sand.
IndexedCanvas draw_sky_and_scenery(const data::Assets&, const data::MissionMap&, const BackdropState&);
void draw_sea_and_sand(IndexedCanvas&, const data::Assets&, const data::MissionMap&, const BackdropState&);

} // namespace wings::game
