#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/game/tower_map.hpp"
#include <array>
#include <cstdint>
#include <vector>

// The running tower: its cells, the object in each cell, the objects and the
// spiders' programs (what $13278 builds from a tower record and the rules
// then change).
//
// Cells are addressed as the rules address them: a 10 x 10 grid per floor
// with a border of non-floor cells round the 8 x 8 tower, so that a step in
// a compass direction is +10, +1, -10 or -1 (the table $23E4) and a floor up
// is +100. Floor -1 is the ground (only its lit flag). The original keeps
// the object map right after floor 3, so "cells" above floor 3 read the
// object map; that is kept (`cell` does it), although towers never reach it.
namespace babel::game {

using CellIndex = int; // (floor + 1) * 100 + (z + 1) * 10 + x + 1

constexpr CellIndex cell_index(int floor, int x, int z) { return (floor + 1) * 100 + (z + 1) * 10 + x + 1; }
constexpr std::array<int, 4> step_offset = {10, 1, -10, -1}; // heading 0 north (z+), 1 east (x+), 2 south, 3 west

// An object ($15DA + 32 n): the three spiders are slots 4-6.
struct TowerObject {
    int type{-1};          // +$00: kind table word 0; >= $1A gone or exploding, < 0 ends the list
    std::uint32_t shape{}; // +$02: the shape shown
    int x{}, y{}, z{};     // +$06/+$08/+$0A
    int heading{};         // +$0C
    int state{};           // +$0E: move kind (0-3 walk, 4-7 pushed, 8-11 lift, 12+ others)
    int progress{};        // +$10: frames left of the move (0 = standing)
    int flags{};           // +$12: what the beams do to it (low byte: 0 destroyed by a zap,
                           // 1 reflects, 2 pushable, 3 grabbable Klondike, 4 convertor,
                           // 5 prism, 6 forcer, 7 freezer/wiper/exchanger; high byte: 0
                           // Klondike taken, 1 second particle cloud, 3 a zap reverses its move)
    CellIndex cell{};      // +$14: the cell it stands in
    CellIndex target{};    // +$16: the cell it is moving into (0 = none)
    int slot{};            // +$18
    int counter_1a{}, counter_1c{}, counter_1e{};

    template <class A> void persist(A& ar) {
        ar(type, shape, x, y, z, heading, state, progress, flags, cell, target, slot, counter_1a, counter_1c, counter_1e);
    }
};

// A spider's program and command state ($156A + 16 n).
struct Program {
    int command{-1};   // +$00: the move under way (0 forward, 1 back, -1 none), for the panel
    int word2{};       // +$02
    int mode{-1};      // +$04: 1 running, 2 running all, 3 writing, -1 idle
    int position{};    // +$06: the next command (writing: the cursor)
    std::array<std::uint8_t, 8> commands{}; // 1 forward, 2 back, 3 left, 4 right, 5 fire, 0 end

    template <class A> void persist(A& ar) { ar(command, word2, mode, position, commands); }
};

// What the panel shows lit ($1558-$1568); the rules set these.
struct PanelLights {
    int move{-1};     // $1558
    int turn{-1};     // $155C
    int program{-1};  // $1560
    int program_record{-1}; // $1564 (the program shown in the strip: its owner's slot)

    template <class A> void persist(A& ar) { ar(move, turn, program, program_record); }
};

class TowerState {
public:
    static constexpr int objects_max = 52; // the room before the cells in the original (the ground row starts at $15DA + 52 x 32)

    std::uint8_t cell(CellIndex i) const;
    void set_cell(CellIndex i, std::uint8_t value);
    int object_at(CellIndex i) const;            // -1 none
    void set_object_at(CellIndex i, int slot);    // -1 clears

    std::array<TowerObject, objects_max> objects{};
    std::array<Program, 7> programs{};           // slots 0-6 (0-3 the cameras' records)
    PanelLights panel;
    int active{4};             // $1568: the camera or spider the view follows
    int lift_moves{};          // $1216: lifts under way
    int turn_flags{};          // $1218: per spider, a reversed lift ride (animation)
    bool backwards_allowed{};  // $110E >= 0 (option)
    int clock{-1};             // $11E4: >= 0 the tower runs; the freezer lowers it
    bool running() const { return clock >= 0; }
    // The objective and the end ($11D8, $11DA, $E30, $E32, $11E0, $11E2, $FC0).
    int klondikes{}, destroyed{}, klondikes_needed{}, destroy_needed{};
    int end_countdown{};       // frames until the tower ends (0 = not ending)
    int end_reason{};          // 1 completed, 3 all spiders destroyed, 4 Grabber destroyed
    int blink{};               // $FC0: 6 down to 0 each frame
    int frame{};               // $FBE: frames since the tower started (animation, rotating objects)
    bool started{};            // $11E6 >= 0: a direct command started the tower
    int time_left{};           // $121A: seconds left, 4 BCD digits (0 = no limit)
    // Tower options the objects read: footstep sounds ($A6 = 0; hypothesis:
    // a sound setting), mines leave a hole ($E2A >= 0: flag 8 clear), the
    // time bombs' speed ($E2E: + 1-4 a frame towards 800).
    bool footsteps{true};
    bool mine_holes{true};
    int bomb_speed{};
    std::vector<int> sounds;   // effects started this frame (their records' addresses)
    // Forcers under way ($BCE4): frames left ($1212), direction ($1214,
    // >= 0 up), the cells they change ($11E8, $1210 of them).
    int forcer_frames{}, forcer_direction{};
    std::vector<CellIndex> forcer_cells;

    // The tower as $13278 unpacks it from its record: cells in the bordered
    // layout, objects placed in their slots with their kind's type, shape,
    // heading and flags, the four cameras, empty programs, the first spider
    // the tower has selected, the timer, the objective and the end cleared.
    static TowerState unpack(const data::Tower&, const data::Memory& program);
    // The cells as the view draws them.
    TowerMap map() const;

    // Events several rules share.
    void explode(int slot);          // the object becomes a particle cloud (type $1A, 32 frames)
    void check_complete();           // $BBE2: everything collected and destroyed: won
    void spider_lost();              // $BC16: a spider went: the tower may be lost
    void remove_floor(CellIndex);    // $C924: Lizards and Wipers take floor away

    // Quick saves: every field, by name in this order.
    template <class A> void persist(A& ar) {
        ar(objects, programs, panel, active, lift_moves, turn_flags, backwards_allowed, clock, klondikes, destroyed,
           klondikes_needed, destroy_needed, end_countdown, end_reason, blink, frame, started, time_left, footsteps,
           mine_holes, bomb_speed, sounds, forcer_frames, forcer_direction, forcer_cells, cells_, objects_at_);
    }

private:
    std::array<std::uint8_t, 500> cells_{}; // floors -1..3
    std::array<std::int8_t, 500> objects_at_{}; // floors 0..3 at 100..499 (the rest unused)
};

} // namespace babel::game
