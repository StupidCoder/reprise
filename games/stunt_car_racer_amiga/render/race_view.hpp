#pragma once
#include "platform/image.hpp"
#include "stunt_car_racer_amiga/data/assets.hpp"
#include "stunt_car_racer_amiga/data/scenery.hpp"
#include "stunt_car_racer_amiga/data/track.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// The view through the windscreen: sky, ground, and the circuit ahead as
// filled polygons in the game's 16 race colours (game.md, "Race view").
//
// The game projects by angles, not by division: a point's place on screen is
// its bearing and its angle below the eye, each relative to the car's own,
// at 2048 pixels per full turn (the view is 256 x 128 pixels, so about 45
// degrees across), turned about the centre by the car's roll.
namespace stuntcar::render {

constexpr int view_width = 256, view_height = 128;

// Angles count 65536 to the turn. Yaw 0 looks along +z, a quarter turn along
// +x; pitch is positive nose down; heights are in track height units (four
// to a plan unit).
struct Camera {
    double x{}, z{}; // plan units
    double eye{};    // height units
    int yaw{}, pitch{}, roll{};
};

// The 16 race colours (index 1/2 road, 7 sky, 13 ground...; see game.md).
using Palette = std::array<data::Colour, 16>;
Palette race_palette(const data::Program&);

struct View {
    std::array<std::uint8_t, view_width * view_height> pixels{}; // colour indices
};

// The opponent's car on the road (game.md, "Opponent car"): its rear axle
// at `step` cross-sections into `segment` (fractions between), the front
// axle half a step further on; `across` places its middle from the road's
// left edge (0) to its right edge (256); its body's corners sit `lift`
// height units above the road (about 15 at rest, observed; rear left, rear
// right, front left, front right).
struct Opponent {
    int segment{};
    double step{};
    double across{128};
    std::array<double, 4> lift{15, 15, 15, 15};
};

// Whether the car is beside the road ($1BB9A set: its contact points are
// past the edge) and on which side ($1BC5C); this changes the drawing order.
enum class Beside { no, left, right };

// Draws the hills and `track` as seen from `camera`, the car being on
// `segment`, with the opponent's car where given.
View draw_race_view(const data::Circuits&, const data::Scenery&, const data::Track&, int segment, const Camera&,
                    const std::optional<Opponent>& opponent = std::nullopt, Beside beside = Beside::no);

platform::Image to_image(const View&, const Palette&);

// A camera on the start line of `track`, at the eye height of a car resting
// on the road, looking along the track.
Camera start_camera(const data::Circuits&, const data::Track&);

} // namespace stuntcar::render
