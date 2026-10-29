#pragma once
// The ruins of chapter 6 (loop 6400, lt5CLoopMiniRuin): a grid of rooms
// the player crosses with Layton and Randall, digging, pushing and
// switching, with mummies on the prowl. The script seq/mini_ruin runs the
// game; the loop supplies its 2110 commands (the grid, the actors, the
// floor's events and objects, the saved floor state).
//
// Reconstructed from the loop's methods (vtable 0x401004: start 0x1c6c10,
// resume 0x1c7768, end of frame 0x1c59d0), its command handlers (table in
// .analysis/adventure56/research/ruin-commands.tsv) and the actor library
// (0x2cc7b4 ..). See .analysis/adventure56/docs/ruins.md.
#include "story/a5_story.hpp"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace picarat::adventure5 {

constexpr int RuinsLoopId = 6400;

std::unique_ptr<Loop> makeRuinsLoop(Story& story, int id, std::vector<xq::Value> values);

// Read access for tools (the research autopilot): how the floor stands,
// in the loop's own terms.
class RuinsInspector {
public:
    virtual ~RuinsInspector() = default;
    virtual int floorNumber() const = 0;
    // An actor's cell; false when there is no such actor.
    virtual bool actorCell(const std::string& name, int& row, int& column) const = 0;
    // The actor's command (-1 none) and whether it is settled.
    virtual int actorAction(const std::string& name) const = 0;
    // The actor's turn about y (radians; the value and where it goes).
    virtual bool actorTurn(const std::string& name, float& now, float& to) const = 0;
    // The cell step the actor faces (as 505).
    virtual bool actorFacing(const std::string& name, int& rowStep, int& columnStep) const = 0;
    // A cell's room (5002's last value; -1 none).
    virtual int roomOf(int row, int column) const = 0;
    // A cell's kind (-1 none) and the event kind of the object there (-1).
    virtual int cellKind(int row, int column) const = 0;
    virtual int objectKind(int row, int column) const = 0;
    // Where an actor stands on a cell (114: the surface plus an object's
    // height).
    virtual float heightAt(int row, int column) const = 0;
    // Whether the actor could step between the cells now (its canStep).
    virtual bool stepPossible(const std::string& name, int r0, int c0, int r1, int c1) = 0;
    // The objects whose event's condition holds now: their cell, event
    // kind and the event they play (the second, else the first), and the
    // names in the condition (its operands, joined).
    struct Trigger {
        int row = 0, column = 0, kind = 0;
        std::string event, condition;
    };
    virtual std::vector<Trigger> triggers() const = 0;
    // The blocks (event kind 200): their actor's name, cell and home cell
    // (where they come back to after sinking).
    struct Block {
        std::string name;
        int row = 0, column = 0, homeRow = 0, homeColumn = 0;
        float y = 0;     // where its base stands
        int number = -1; // its event's number (a block's, for 613 / 614)
    };
    virtual std::vector<Block> blocks() const = 0;
    // The plates (event kind 201) still there, as blocks (home = cell).
    virtual std::vector<Block> plates() const = 0;
    // The mummies (class 3) not crushed: their cells (home = where they
    // are going).
    virtual std::vector<Block> mummies() const = 0;
    // A mummy's state for planning: noticed Layton (+0x2e0), its value
    // (+0x2d8: steps a turn) and sight (+0x2c8.., east west south north).
    struct MummyState {
        bool noticed = false;
        int value = 1;
        std::array<int, 4> sight{5, 5, 5, 5};
        bool fillsHigh = false; // +0x2c0: a hole it falls into becomes kind 303 (not walkable)
    };
    virtual bool mummyState(const std::string& name, MummyState& out) const = 0;
    // Whether a mummy sees a cell now (518).
    virtual bool mummySees(const std::string& name, int row, int column) = 0;
    // Cells taken as of another kind in the queries above (a tool's plan:
    // holes dug, filled; {row, column, kind}); none clears them.
    virtual void imagineCells(const std::vector<std::array<int, 3>>& cells) = 0;
    // Where an actor's path ends (509 / 523), else where it is going.
    virtual bool actorGoal(const std::string& name, int& row, int& column) = 0;
    // A mummy's planning path (523: 0x2c4ecc with +0x2e1) from one cell
    // to another within `radius`: whether found, and its first cell.
    virtual bool mummyPath(const std::string& name, int fromRow, int fromColumn, int toRow, int toColumn,
                           int radius, int& nextRow, int& nextColumn) = 0;
    // A cell's surface (112: its height plus its kind's model height).
    virtual float surfaceAt(int row, int column) const = 0;
    // Planning: the queries (stepPossible, objectKind, heightAt) take
    // these actors' objects as standing on the given cells instead of
    // where they are (a negative row: gone), until called again with an
    // empty list.
    virtual void imagineBlocks(const std::vector<Block>& at) = 0;
    // Planning a push: block k of `layout` (the blocks, then the plates)
    // slides by (dr, dc) as a pushed block does (state 9 of 0x1c9648:
    // drops, plates pressed, water sending it home); `layout` is updated
    // (a pressed plate's row goes -1). Returns the cells it moved.
    virtual int slide(std::vector<Block>& layout, std::size_t k, int dr, int dc) = 0;
    // An event waits or plays, or thread 1 runs.
    virtual bool eventRunning() const = 0;
    // The guide mode: offered (131), running, failed, cleared; the route
    // and phase; the shown phase's markers {action, row, column, counted}.
    struct Guide {
        bool offered = false, running = false, failed = false, cleared = false;
        int route = -1, phase = -1, routes = 0;
        std::vector<std::array<int, 4>> markers;
    };
    virtual Guide guide() const = 0;
};

// The ruins loop's inspector, or null for any other loop.
RuinsInspector* ruinsInspector(Loop& loop);

} // namespace picarat::adventure5
