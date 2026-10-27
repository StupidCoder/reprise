#pragma once
#include "amiga/hunk.hpp"
#include "mindwalker_amiga/world/world.hpp"
#include <array>
#include <cstdint>
#include <functional>

// The landscape stage's view of the world: the 4 x 4 cells in front of the
// walker, built from the world's generator (A $2086B0, called whenever the
// walker enters another cell). Each cell gets a terrain word and an object
// word; the view also gets its 32 colours, which the position chooses.
namespace mindwalker::landscape {

// A cell's terrain word ($202F50): bits 14-15 the surface kind (0 ground,
// $4000/$8000/$C000 the platforms' kinds; $C000 is also water), bits 0-7 the
// level, and markers: $400 the start cell or a found landmark, $2000, $1000,
// $800/$A00 (by the stage's parity), $100 the marked block. The meanings of
// the markers beyond what the builder sets are open.
// A cell's object word ($202FD0): 1 the cell is on the walker's map of
// visited cells, 2 a beacon, 4 a creature's place, $10/$20 a find, $40 an
// empty cell. (Names hypotheses; the bits are the builder's.)
struct ViewCells {
    std::array<std::array<std::uint16_t, 4>, 4> terrain{}; // [x][y], x = column of the view
    std::array<std::array<std::uint16_t, 4>, 4> objects{};
    std::array<std::uint16_t, 32> colours{};               // 16-bit as stored; LoadRGB4 uses the low 12
    bool water{};          // $2099A4: a cell of kind $C000
    bool marker_2000{};    // $2099A6
    bool beacon{};         // $2099A8: an unmarked beacon in view
    bool find{};           // $2099AA
    int creature_places{}; // $2099AC
    int landmarks{};       // $2099B8: cells marked $400
};

// What the builder takes besides the world and the cell: the stage (+$5C), the
// cell it was last built for ($209980/$209982), whether each of the two kinds
// of find has been taken ($2099BC/$2099BE), colours 17 and 18 as they stand,
// the start/landmark cell ($209984/$209986) and the walker's map of visited
// cells ($205B10).
struct ViewInputs {
    int stage{};
    std::uint16_t previous_x{}, previous_y{};
    std::array<bool, 2> found{};
    std::uint16_t colour17{}, colour18{};
    std::uint16_t home_x{}, home_y{};
    std::function<bool(std::uint16_t x, std::uint16_t y)> visited = [](std::uint16_t, std::uint16_t) { return false; };
};

// The view whose first column is x and first row y (the original calls the
// routine with the walker's cell; the window is x..x+3, y..y+3). `colours`
// starts from the colours as they stand (entry 16 is kept).
ViewCells build_view(const world::World&, const amiga::LoadedImage& landscape, std::uint16_t x, std::uint16_t y,
                     const ViewInputs&, const std::array<std::uint16_t, 32>& colours);

// A's colour table as stored ($2004AC), the starting point of the colours.
std::array<std::uint16_t, 32> stored_colours(const amiga::LoadedImage& landscape);

} // namespace mindwalker::landscape
