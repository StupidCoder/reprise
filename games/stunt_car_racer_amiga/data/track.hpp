#pragma once
#include "stunt_car_racer_amiga/data/assets.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The eight circuits, decoded from the track bank in the game program.
//
// A circuit is laid out on a 16 x 16 grid of square cells, each cell_size
// units across. It is a closed loop of segments, one per cell visited; a
// segment has a plan shape (straight, curve, diagonal...) turned to one of
// four headings, and each road edge rises and falls along it by a height
// profile. Plan units: x grows to the right, z towards the onlooker in the
// game's own track pictures. Heights are a quarter of plan units in the
// race view (see game.md, "Tracks").
namespace stuntcar::data {

constexpr int cell_size = 0x800;
constexpr int grid_cells = 16;

struct PlanPoint {
    int x{}, z{};
};

// A plan shape in its own cell frame, heading +z: the left and right road
// edge at each step along it. A segment of this shape has steps.size() - 1
// steps.
struct Shape {
    std::vector<std::array<PlanPoint, 2>> steps;

    // Figures the race uses to place a car on the piece (game.md, "Car
    // physics: placement"). `header` holds the bytes before the description
    // (from byte 1): byte 0 the placement kind (bit 7 arc, bit 6 diagonal,
    // bits 0-1 the arc's scale), then little-endian figures read by kind.
    // `description` holds the bytes after the point count.
    std::vector<std::uint8_t> header;
    std::array<std::uint8_t, 6> description{};

    std::uint8_t placement() const { return header.at(0); }
    std::int16_t figure(int offset) const; // little-endian word at header byte `offset` - 1
    std::uint8_t figure_byte(int offset) const { return header.at(std::size_t(offset - 1)); }
    bool mirrored() const { return description[0] & 1; } // the curve turns the other way
    std::uint8_t across_gain() const { return description[1]; }
    std::uint8_t along_gain() const { return description[2]; }
    std::uint8_t steer_gain() const { return description[5]; }
};

// How one road edge rises along a segment, step by step. The same bytes
// read in two ways: narrow (one byte a step: 4 bits whole, 3 bits eighths)
// for low tracks, wide (two bytes a step) for tall ones. The top bit of a
// step's (first) byte is a mark, not height: on the left edge it splits the
// road surface there for the race view (game.md, "Race view").
struct Profile {
    std::vector<std::uint8_t> bytes;
    int offset(int step, bool wide) const;
    bool marked(int step, bool wide) const;
};

struct Segment {
    int cell_x{}, cell_z{};       // grid cell, 0..15
    int heading{};                // quarter turns: 0 +z, 1 +x, 2 -z, 3 -x
    bool reversed{};              // the shape is driven from its far end
    int shape{};                  // index into Circuits::shapes
    int left_profile{}, right_profile{};
    bool wide_profiles{};
    bool kerb_phase{};            // which colour the kerb stripes start with
    int left_base{}, right_base{}; // edge heights where the profiles read 0
    int steps{};
};

struct Track {
    std::string name;
    int start_height{};
    std::vector<Segment> segments;
    int car_start_segment{};  // where the car is lowered onto the track
    int start_line_segment{}; // the start/finish line is its last cross-section
    int halfway_segment{};    // a lap only counts after passing it
    // Stored with the track and used by the game, not yet interpreted
    // (game.md lists what is known of each).
    std::array<std::uint8_t, 4> trailer{};
    std::vector<std::pair<std::uint8_t, std::uint8_t>> segment_marks;
    std::vector<std::uint8_t> segment_list;
};

struct Circuits {
    std::vector<Shape> shapes;     // 16 slots; empty where the table names none
    std::vector<Profile> profiles; // 128
    std::vector<Track> tracks;     // 8, in the game's order
};

// Throws std::runtime_error when the bank does not decode as expected.
Circuits load_circuits(const Program&);

// One cross-section of the road: both edges in plan units with their heights.
struct CrossSection {
    PlanPoint left, right;
    int left_height{}, right_height{};
    int segment{};
    bool marked{}; // the left profile's mark at this step
};

// The road of `track` in driving order, segment by segment (each segment's
// first cross-section repeats the previous segment's last).
std::vector<CrossSection> road(const Circuits&, const Track&);

} // namespace stuntcar::data
