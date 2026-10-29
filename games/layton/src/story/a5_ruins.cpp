// The ruins of chapter 6 (loop 6400, lt5CLoopMiniRuin). See a5_ruins.hpp
// and .analysis/adventure56/docs/ruins.md.
#include "story/a5_ruins.hpp"

#include "graphics/a56_model.hpp"
#include "import/a56_formats.hpp"
#include "system/a5_game.hpp"
#include "system/a5_progress.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <unordered_map>
#include <optional>
#include <string>

namespace picarat::adventure5 {

namespace {

using xq::Value;
namespace fs = std::filesystem;

// The grid (+0x7bd4): 70 rows of 50 cells, a cell 8 units wide.
constexpr int kRows = 70;
constexpr int kColumns = 50;
constexpr float kCellSize = 8.0f;
constexpr int kRooms = 15;   // +0x45448, 0x20 bytes each (5014)
constexpr int kEvents = 120; // +0x28f8, 100 bytes each (5010 / 5011)
constexpr int kObjects = 100; // +0x57d8 / +0x2f08, 0x5c bytes each (5012 / 5013)
constexpr int kActors = 70;   // +0x2470, 12 bytes each
// The guide's "none" for a facing, a marker's lift and delay (FLT_MAX).
constexpr float kNoAngle = std::numeric_limits<float>::max();

// The actors the script names.
const std::string kLayton = "\x83\x7e\x83\x6a\x83\x8c\x83\x43\x83\x67\x83\x93"; // ミニレイトン
const std::string kRandall = "\x83\x7e\x83\x6a\x83\x89\x83\x93\x83\x68";          // ミニランド
const std::string kMummy = "\x83\x7e\x83\x43\x83\x89";                                    // ミイラ

bool prefix(const std::string& s, const std::string& p) {
    // 0x292f94: the name begins so.
    return s.compare(0, p.size(), p) == 0;
}

// ＜ナゾ＞あとでとけない: a puzzle may not be left for later.
const std::string kCannotLater = "\x81\x83\x83\x69\x83\x5d\x81\x84\x82\xa0\x82\xc6\x82\xc5\x82\xc6\x82\xaf\x82\xc8\x82\xa2";

// Cell kinds (5002's first value).
constexpr int kStartCell = 1003;   // Layton's start (command 110)
constexpr int kSecondCell = 1004;  // command 111

// A row of an XS data list as values (5000.. records, as a5_progress).
std::vector<Value> rowOf(const import::adventure56::DataList& list,
                         const import::adventure56::DataCommand& c) {
    std::vector<Value> row;
    for (unsigned i = 0; i < c.count; ++i) {
        const auto& o = list.operands[c.first + i];
        switch (o.type & 15) {
        case 1:
            row.push_back(Value::ofInt(std::int32_t(o.value)));
            break;
        case 2:
            row.push_back(Value::ofUint(o.value));
            break;
        case 3: {
            float f;
            std::memcpy(&f, &o.value, sizeof f);
            row.push_back(Value::ofFloat(f));
            break;
        }
        case 8: {
            const auto* p = reinterpret_cast<const char*>(list.strings.data() + o.value);
            row.push_back(Value::ofText(std::string(p, strnlen(p, list.strings.size() - o.value))));
            break;
        }
        default:
            row.push_back(Value{});
            break;
        }
    }
    return row;
}

int intAt(const std::vector<Value>& row, std::size_t i) {
    return i < row.size() ? row[i].toInt() : 0;
}

float realAt(const std::vector<Value>& row, std::size_t i) {
    return i < row.size() ? row[i].toFloat() : 0.0f;
}

std::string textAt(const std::vector<Value>& row, std::size_t i) {
    return i < row.size() && row[i].isText() ? row[i].text : std::string();
}

// A grid position: row (z / 8) and column (x / 8).
struct CellIndex {
    int row = -1, column = -1;

    bool valid() const { return row >= 0 && row < kRows && column >= 0 && column < kColumns; }
    bool operator==(const CellIndex&) const = default;
};

// A world position to its cell (0x2cce34: (v + 4) / 8, truncated).
CellIndex cellAt(float x, float z) {
    return {int((z + 4.0f) * (1.0f / kCellSize)), int((x + 4.0f) * (1.0f / kCellSize))};
}

// ---------------------------------------------------------------- floor

// A cell (5002 {kind, height, row, column, room}; 0x1c7f90 / 0x1cbe98):
// its world position is (column, height, row) x 8.
struct Cell {
    int kind = -1;
    float x = 0, y = 0, z = 0;
    int originalKind = -1; // +0x40: the kind it was loaded with (a hole goes back to it)
    int room = -1;
};

// A model definition (5001 {id, model, motion, motion, 4 reals, 4 flags};
// 0x1d1d14, 0xf8 bytes): ids 100..199 are the rooms' shells (shown while
// the room's bit of +0x21d4 is clear, 0x1c59d0), the others the parts
// events and objects use (0x1d2408's table).
struct PartDef {
    int id = -1;
    std::string model, motionA, motionB;
    std::array<float, 4> reals{};
    std::array<int, 4> flags{};
    std::string resource; // the model resource loaded for it ("" when none)
    std::string object;   // its 3D object (the room shells: one each)
};

// An event (5011, 0x1d2b64, 100 bytes): its kind (+8) and number (+0x10)
// tie it to the objects that carry it (5013); a trigger condition and its
// three alternatives (a number, or a flag name), the event names it plays.
struct EventDef {
    int kind = -1;
    int flag = 0; // +0xc
    int number = 0;
    int conditionCode = -1, conditionCount = 0; // +0x14, +0x18 (a progress condition, -1 always)
    struct Alternative {
        bool isNumber = false;
        int number = 0;
        std::string name;
    };
    std::array<Alternative, 3> before; // +0x1c..+0x30: the condition's operands
    std::string eventA, eventB;        // +0x34, +0x38: the events it plays
    int byteA = 0;                     // +0x3c
    int notice = 0;                    // +0x40: the "!" notice before eventB
    int effectCode = -1, effectCount = 0; // +0x44, +0x48: a progress effect
    std::array<Alternative, 3> after;  // +0x4c..+0x60: its operands
};

// An object on the grid (5013 {event kind, event number, row, column,
// angle}; 0x1d1a6c, 0x5c bytes, named "evt_obj_<slot>").
struct GridObject {
    std::string name;
    float x = 0, y = 0, z = 0;
    float angle = 0;     // radians (5013's degrees x 0x1d1ce4)
    int event = -1;      // into Floor::events (+0x50)
    int actor = -1;      // into World::actors (+0x54)
    int actorModel = -1; // the model id the actor shows (+0x58)
};

// A room (5014 {index, map x, map y, row0, column0, row1, column1}; the
// cell rectangle as the camera's clamp 0x293fc0 reads it).
struct Room {
    std::array<int, 6> values{};
    // An object of event kind 1000 (+0x18) / 1001 (+0x1c) in the room: the
    // camera views it in mode 4 / 3 (0x1c59d0); the event runner and 123 /
    // 124 read them too.
    bool marker1000 = false;
    bool marker1001 = false;

    int row0() const { return values[2]; }
    int column0() const { return values[3]; }
    int row1() const { return values[4]; }
    int column1() const { return values[5]; }
};

struct Floor {
    std::array<std::array<Cell, kColumns>, kRows> cells;
    std::vector<PartDef> parts;
    std::vector<EventDef> events;
    std::vector<GridObject> objects;
    std::array<Room, kRooms> rooms;

    Cell* cell(CellIndex i) { return i.valid() ? &cells[std::size_t(i.row)][std::size_t(i.column)] : nullptr; }
    const Cell* cellAt(CellIndex i) const {
        return i.valid() ? &cells[std::size_t(i.row)][std::size_t(i.column)] : nullptr;
    }
    const PartDef* part(int id) const {
        for (const auto& p : parts) {
            if (p.id == id) {
                return &p;
            }
        }
        return nullptr;
    }
};

// ---------------------------------------------------------------- actors

// An actor (0x2cc8ec's classes: 0 plain, 1 Layton, 2 Randall, 3 a mummy,
// 4..9 the floor's movable objects; 0x2cc7b4 gives it a 3D object of its
// own name and model, priority 0x6e).
// A command to an actor (0x3c bytes: the pending one at +0xe4, the
// active one at +0xa8; 0x2ca6e8 moves the first into the second).
struct Order {
    CellIndex target;              // +0: the cell a walk goes to
    CellIndex step{0, 0};          // +8: a pushed block's direction (rows, columns)
    std::array<float, 3> facing{}; // +0x10: the direction a face (3) turns to
    float turn = 0;                // +0x1c: a turn's angle (4), added to the current one
    float moveFrames = 0;          // +0x20: frames a step takes
    float faceFrames = 0;          // +0x24: frames turning towards it takes
    bool onward = false;           // +0x38: a walk (1) moves on along the path
};

struct Actor {
    int klass = 0;
    std::string name;
    std::uint32_t hash = 0; // +0x80
    int code = 0;           // the object record's id (600 Layton, 0x2c4 Randall, the model id)
    float climb = 0;        // +8: a step must change the height by less (0x2cc8ec: 6, 3 mummies, 2 class 4)
    bool ontoHoles = false; // +0xc: may step onto kind 300 cells (mummies)
    float factor = 0;       // +0x10 (0.65, 0.75 mummies, 1 class 4, 0.9 class 5)
    // +0x14 / +0x18: the motions resting and stepping (0x292eb0: 立ち and
    // 走り; mummies walk, 歩き; Layton picks his by the shovel, 0x1cc344).
    std::string standMotion = "\x97\xa7\x82\xbf"; // 立ち
    std::string moveMotion = "\x91\x96\x82\xe8";  // 走り
    CellIndex dest;         // +0x98: where the actor goes (a walk's target; at rest its cell)
    CellIndex last;         // +0xa0: the cell before its last step
    CellIndex target;       // +0x88
    // +0x30: the command under way (0 rest, 1 a step, 2 a step along the
    // path, 3 face, 4 turn, 5.. the class's own; -1 none); +0x31 the
    // pending one's (-1 none).
    int action = -1;
    int state = -1;
    Order active, pending;  // +0xa8, +0xe4
    bool began = false;     // +0x34: the active command began this frame
    bool beganSettled = false; // +0x38: the actor was settled when it began
    bool settled = false;   // +0x39
    bool idle = false;      // +0x3c
    bool frozen = false;    // +0x2c bit 0: the tick skips it (0x1c798c)
    bool dormant = false;   // +0x1c: the tick skips it (0x2cc8ec: classes 5, 6 and 9)
    bool deferred = false;  // +0x20: an arrival waits for the guide's +0x2f to clear
    const EventDef* arrival = nullptr; // +0x90: the event of the cell it walks onto
    int arrivalObject = -1;            // +0x94
    std::vector<CellIndex> path;       // +0x120: at most 50 steps (0x2c4ecc)
    std::size_t pathIndex = 0;         // +0x2b0
    int gridObject = -1;    // the grid object it shows (0x1d2004)
    // Layton's push (action 6, 0x1cc344): the block (+0xd8), the push
    // under way (+0x2b8), a push that failed (+0x2ba).
    Actor* pushing = nullptr;
    bool pushStarted = false;
    bool pushFailed = false;
    // A block (class 4, 0x1c9648): its steps this push (+0x2b8) and who
    // pushes it (+0x2bc).
    int steps = 0;
    Actor* pushedBy = nullptr;
    // Sinking and coming back (states 10..12): the depth it vanishes at
    // (+0x2c0), the wait (+0x2e0), where it drops from and to (+0x2c4,
    // +0x2d0), the touch lock kept meanwhile (+0x2dc), whether it comes
    // back (+0x2e4).
    float sinkMark = 0;
    float wait = 0;
    std::array<float, 3> dropFrom{}, dropTo{};
    bool lockKept = false;
    bool comesBack = true;
    // Switches and levers (classes 6..8): pressed (+0x2b8), a pair's
    // number (+0x2c0, class 8) and the pair done (+0x2bc).
    bool pressed = false;
    int pairNumber = -1;
    bool linked = false;
    // Mummies (class 3, 0x2cc8ec): how far they see each way (+0x2c8..
    // +0x2d4, 99 until a look 518 narrows them by walls), +0x2d8 (1), the
    // hole they fill (+0x2c0: kind 303 or 304), a quiet appearance
    // (+0x2c4), noticed Layton (+0x2e0), a path being planned (+0x2e1),
    // a step being checked (+0x2e4, also 527).
    std::array<int, 4> sight{99, 99, 99, 99};
    CellIndex home; // +0x2b8 / +0x2bc: where 512 made it
    int mummyValue = 1;
    bool fillsHigh = false;
    bool quietAppear = false;
    bool noticed = false;
    bool planning = false;
    bool checking = false;
    // Layton's dig (action 5): the cell whose kind changes (+0xd0), the
    // treasure's grid object (+0xd4) and whether it came up (+0x2b9); the
    // effect object (+0xdc). A failed dig or push sets pushFailed (+0x2ba).
    CellIndex digCell{-1, -1};
    int treasure = -1; // into Floor::objects
    bool treasureDug = false;
    std::string effect;
};

// ---------------------------------------------------------------- loop

class RuinsLoop final : public Loop, public RuinsInspector {
public:
    int floorNumber() const override { return floorNumber_; }
    bool actorCell(const std::string& name, int& row, int& column) const override {
        const Actor* a = const_cast<RuinsLoop*>(this)->actor(name);
        if (!a) {
            return false;
        }
        const CellIndex at = cellOf(*a);
        row = at.row;
        column = at.column;
        return true;
    }
    int actorAction(const std::string& name) const override {
        const Actor* a = const_cast<RuinsLoop*>(this)->actor(name);
        return a ? a->action : -1;
    }
    bool actorTurn(const std::string& name, float& now, float& to) const override {
        const Actor* a = const_cast<RuinsLoop*>(this)->actor(name);
        auto* o = a ? const_cast<adventure56::Scene&>(game_.scene).object3D(a->hash) : nullptr;
        if (!o) {
            return false;
        }
        now = o->rotation3[1];
        to = o->rotation3.target()[1];
        return true;
    }
    bool actorFacing(const std::string& name, int& rowStep, int& columnStep) const override {
        const Actor* a = const_cast<RuinsLoop*>(this)->actor(name);
        auto* o = a ? const_cast<adventure56::Scene&>(game_.scene).object3D(a->hash) : nullptr;
        if (!o) {
            return false;
        }
        // (Where a turn under way ends: the tools act on the final facing.)
        const float turn = o->rotation3.target()[1];
        rowStep = int(std::cos(turn) + 1.5f) - 1;
        columnStep = int(std::sin(turn) + 1.5f) - 1;
        return true;
    }
    int roomOf(int row, int column) const override {
        const Cell* cell = floor_.cellAt({row, column});
        return cell && cell->kind != -1 ? cell->room : -1;
    }
    int cellKind(int row, int column) const override {
        const Cell* cell = floor_.cellAt({row, column});
        return cell ? cell->kind : -1;
    }
    int objectKind(int row, int column) const override {
        const Inspecting guard(*this);
        const GridObject* g = objectAt({row, column}, 0);
        return g && g->event >= 0 ? floor_.events[std::size_t(g->event)].kind : -1;
    }
    float heightAt(int row, int column) const override {
        const Inspecting guard(*this);
        return standHeight({row, column});
    }
    bool stepPossible(const std::string& name, int r0, int c0, int r1, int c1) override {
        const Inspecting guard(*this);
        Actor* a = actor(name);
        float h = 0.0f;
        return a && canStep(*a, {r0, c0}, {r1, c1}, h);
    }
    bool eventRunning() const override { return eventBusy(); }
    Guide guide() const override {
        Guide g;
        g.offered = guide_.b4 != 0;
        g.running = guide_.b5 != 0;
        g.failed = guide_.b7 != 0;
        g.cleared = guide_.b8 != 0;
        g.route = guide_.route;
        g.phase = guide_.phase;
        g.routes = int(guide_.routes.size());
        if (guide_.route >= 0 && std::size_t(guide_.route) < guide_.routes.size()) {
            const auto& r = guide_.routes[std::size_t(guide_.route)];
            if (guide_.phase >= 0 && std::size_t(guide_.phase) < r.phases.size()) {
                for (const auto& s : r.phases[std::size_t(guide_.phase)]) {
                    g.markers.push_back({s.action, s.cell.row, s.cell.column, s.counted ? 1 : 0});
                }
            }
        }
        return g;
    }
    std::vector<Block> blocks() const override {
        std::vector<Block> list;
        for (const auto& g : floor_.objects) {
            if (g.name.empty() || g.event < 0 || floor_.events[std::size_t(g.event)].kind != 200) {
                continue;
            }
            Actor* a = actorOf(&g);
            if (!a) {
                continue;
            }
            const CellIndex at = cellOf(*a), home = cellAt(g.x, g.z);
            const auto* o = const_cast<adventure56::Scene&>(game_.scene).object3D(a->hash);
            list.push_back({a->name, at.row, at.column, home.row, home.column,
                            o ? o->position.value()[1] + o->offset.value()[1] : g.y,
                            floor_.events[std::size_t(g.event)].number});
        }
        return list;
    }
    std::vector<Block> plates() const override {
        std::vector<Block> list;
        for (const auto& g : floor_.objects) {
            if (g.name.empty() || g.event < 0 || floor_.events[std::size_t(g.event)].kind != 201) {
                continue;
            }
            if (Actor* a = actorOf(&g)) {
                const CellIndex at = cellOf(*a);
                list.push_back({a->name, at.row, at.column, at.row, at.column, g.y});
            }
        }
        return list;
    }
    std::vector<Block> mummies() const override {
        std::vector<Block> list;
        for (const auto& a : actors_) {
            if (a && a->klass == 3 && a->action != 20 && !a->idle) {
                const CellIndex at = cellOf(*a);
                list.push_back({a->name, at.row, at.column, a->dest.row, a->dest.column, 0.0f});
            }
        }
        return list;
    }
    bool mummyState(const std::string& name, MummyState& out) const override {
        const Actor* a = const_cast<RuinsLoop*>(this)->actor(name);
        if (!a || a->klass != 3) {
            return false;
        }
        out.noticed = a->noticed;
        out.value = a->mummyValue;
        out.sight = a->sight;
        out.fillsHigh = a->fillsHigh;
        return true;
    }
    bool mummySees(const std::string& name, int row, int column) override {
        const Inspecting guard(*this);
        Actor* a = actor(name);
        return a && a->klass == 3 && sees(*a, {row, column});
    }
    bool actorGoal(const std::string& name, int& row, int& column) override {
        Actor* a = actor(name);
        if (!a) {
            return false;
        }
        const CellIndex at = !a->path.empty() && a->pathIndex < a->path.size() ? a->path.back() : a->dest;
        row = at.row;
        column = at.column;
        return true;
    }
    bool mummyPath(const std::string& name, int fromRow, int fromColumn, int toRow, int toColumn, int radius,
                   int& nextRow, int& nextColumn) override {
        const Inspecting guard(*this);
        Actor* a = actor(name);
        if (!a || a->klass != 3) {
            return false;
        }
        a->planning = true;
        bool found = false;
        const auto path = findPath(*a, {fromRow, fromColumn}, {toRow, toColumn}, std::min(radius, 50), false, found);
        a->planning = false;
        nextRow = path.empty() ? -1 : path.front().row;
        nextColumn = path.empty() ? -1 : path.front().column;
        return found;
    }
    float surfaceAt(int row, int column) const override {
        const Inspecting guard(*this);
        return surface({row, column});
    }
    int slide(std::vector<Block>& layout, std::size_t k, int dr, int dc) override {
        const Inspecting guard(*this);
        // blockUpdate's state 9 decisions over a layout: the support under
        // the block (its cell's surface, or the top of a block below it),
        // the cell ahead and what stands on it (a block's top, else an
        // object with an actor), plates pressed, a drop, water.
        Actor* self = actor(layout[k].name);
        if (!self) {
            return 0;
        }
        constexpr float kTop = 8.0f; // a block's height (its model's fourth real)
        const float climb = self->climb;
        const auto otherBlockTop = [&](CellIndex at, float below, bool any) {
            float top = -1e9f;
            for (std::size_t i = 0; i < layout.size(); ++i) {
                const Actor* b = i == k ? nullptr : actor(layout[i].name);
                if (b && b->klass == 4 && layout[i].row == at.row && layout[i].column == at.column &&
                    (any || layout[i].y + kTop <= below)) {
                    top = std::max(top, layout[i].y + kTop);
                }
            }
            return top;
        };
        int moved = 0;
        for (int guard = 0; guard < 200; ++guard) {
            imagineBlocks(layout);
            const CellIndex cur{layout[k].row, layout[k].column};
            const CellIndex next{cur.row + dr, cur.column + dc};
            float& y = layout[k].y;
            const Cell* curCell = floor_.cellAt(cur);
            const Cell* nextCell = floor_.cellAt(next);
            if (curCell && curCell->kind == 1005) {
                // Sinks; back home.
                layout[k].row = layout[k].homeRow;
                layout[k].column = layout[k].homeColumn;
                const CellIndex home{layout[k].row, layout[k].column};
                y = std::max(surface(home), otherBlockTop(home, 1e9f, true));
                break;
            }
            float support = std::max(surface(cur), otherBlockTop(cur, y + 1.0f, false));
            if (support - y < -climb) {
                y = support; // drops (8 frames), then on
                continue;
            }
            const PartDef* nextPart = nextCell ? floor_.part(nextCell->kind) : nullptr;
            // What stands ahead: a block's top, else an object with an actor.
            float hNext = surface(next);
            bool nextWalkable = true;
            const float blockTop = otherBlockTop(next, 1e9f, true);
            bool pressed = false;
            if (blockTop > -1e8f) {
                hNext = blockTop;
            } else if (const GridObject* g = objectAt(next, 1)) {
                const PartDef* op = floor_.part(g->actorModel);
                const EventDef* e = g->event >= 0 ? &floor_.events[std::size_t(g->event)] : nullptr;
                if (e && e->kind == 201 && (surface(next) + (op ? op->reals[3] : 0.0f) < surface(cur) || moved > 0)) {
                    pressed = true;
                    for (std::size_t i = 0; i < layout.size(); ++i) {
                        if (Actor* p = actorOf(g); p && layout[i].name == p->name) {
                            layout[i].row = -1;
                        }
                    }
                } else if (op) {
                    hNext += op->reals[3];
                    nextWalkable = op->flags[3] != 0;
                }
            }
            if (!(nextCell && nextCell->kind == 1005)) {
                if (!nextPart) {
                    break;
                }
                if (nextPart->flags[3] == 0 || !(hNext - y < climb)) {
                    if (nextPart->id != 300) {
                        break;
                    }
                } else if (!pressed && !nextWalkable) {
                    break;
                }
            }
            layout[k].row = next.row;
            layout[k].column = next.column;
            ++moved;
        }
        imagineBlocks({});
        return moved;
    }
    void imagineCells(const std::vector<std::array<int, 3>>& cells) override {
        imaginedCells_.clear();
        for (const auto& c : cells) {
            imaginedCells_[{c[0], c[1]}] = c[2];
        }
    }
    void imagineBlocks(const std::vector<Block>& at) override {
        imagined_.clear();
        ++imagineGeneration_;
        for (const auto& b : at) {
            if (const Actor* a = actor(b.name)) {
                imagined_[a] = {b.row, b.column};
            }
        }
    }
    // (A paired switch whose own event played still counts while its
    // pair's is pending: both must be down for that one.)
    bool partnerPending(const GridObject& g) const {
        const Actor* a = actorOf(&g);
        if (!a || a->klass != 8 || a->pairNumber == -1 || a->linked) {
            return false;
        }
        const int want = (a->pairNumber & 1) == 0 ? a->pairNumber - 1 : a->pairNumber + 1;
        for (const auto& o : floor_.objects) {
            const Actor* b = o.name.empty() || o.event < 0 ? nullptr : actorOf(&o);
            if (b && b != a && b->klass == 8 && b->pairNumber == want) {
                return holds(floor_.events[std::size_t(o.event)]);
            }
        }
        return false;
    }
    std::vector<Trigger> triggers() const override {
        std::vector<Trigger> list;
        for (const auto& g : floor_.objects) {
            if (g.name.empty() || g.event < 0) {
                continue;
            }
            const EventDef& e = floor_.events[std::size_t(g.event)];
            if (!holds(e) && !(e.kind == 302 && partnerPending(g))) {
                continue;
            }
            float x = g.x, z = g.z;
            if (Actor* a = actorOf(&g)) {
                if (auto* o = const_cast<adventure56::Scene&>(game_.scene).object3D(a->hash)) {
                    x = o->position[0];
                    z = o->position[2];
                }
            }
            const CellIndex at = cellAt(x, z);
            std::string names;
            for (const auto& b : e.before) {
                if (!b.isNumber) {
                    names += b.name + " ";
                }
            }
            list.push_back({at.row, at.column, e.kind, e.eventB.empty() ? e.eventA : e.eventB, names});
        }
        return list;
    }

    RuinsLoop(Story& story, int id, std::vector<Value> values)
        : Loop(story, id, std::move(values)), playback_(game_) {
        // 0x383e60: the floor (value 0, +0x21f4) makes 現在のプレイス
        // (floor + 10) x 10, the ruins place; value 1 goes to +0x21f8.
        floorNumber_ = valueInt(this->values[0], 0);
        startByte_ = valueInt(this->values[1], 0);
        game_.setVariable("\x8c\xbb\x8d\xdd\x82\xcc\x83\x76\x83\x8c\x83\x43\x83\x58", // 現在のプレイス
                          (floorNumber_ + 10) * 10);
    }

    void start() override {
        // 0x1c6c10: the scope, the command table (the switch below), the
        // working copy of the ruins' record and a blank second one.
        Loop::start();
        record_ = game_.ruins;
        blank_ = Game::RuinRecord{};
    }

    // Init (+0x30 = 0x2ad130) looks for "Init" in the scripts of the
    // loop's scope before the first resume; mini_ruin, loaded by the
    // resume, has an empty one. Hypothesis: nothing runs here (the list
    // 0x32b780 searches is not pinned down; see docs/ruins.md).
    void init() override { Loop::init(); }

    void resume() override {
        // 0x1c7768: the on-top scope; the lower screen's area map goes
        // (0x29e22c, both slots); seq/mini_ruin; two display frames per
        // game frame (0x2d3af8 2); スキップ禁止 cleared. The draw pool it
        // allocates (0x29738c) is not modelled; the three camera slots go
        // off (0x2c3d54 on +0x2214).
        Loop::resume();
        // Hypothesis (0x29345c -> 0x292aa8 -> 0x2d1b00 take the depth from
        // 0x3509b8, which prefers the level of the object at game +0x1dc0,
        // most likely the draw pool the resume makes): the actors' objects
        // belong to the loop's level, not to an event's scope that makes
        // them (512 from a challenge event; console not checked).
        actorLevel_ = game_.scene.scopeDepth();
        game_.releaseAreaMap(3);
        game_.loadScript("seq/mini_ruin");
        game_.displayFramesPerFrame = 2;
        if (const int f = game_.flagId("\x83\x58\x83\x4c\x83\x62\x83\x76\x8b\xd6\x8e\x7e"); f >= 0) { // スキップ禁止
            game_.setFlag(f, false);
        }
        for (int i = 0; i < 3; ++i) {
            setCameraMode(i, -1, 0.0f, 0, nullptr, true);
        }
    }

    void suspend() override;

    void begin() override {
        // 0x1c52f4 -> 0x2ad1f8: Main (RPG_MainLoop) afresh.
        Loop::begin();
        startBlock(0, "Main");
    }

    bool command(int number, xq::Call& c, std::size_t first) override;

private:
    static int valueInt(const Value& v, int fallback) {
        return v.kind == Value::Kind::None ? fallback : v.toInt();
    }

    bool result(xq::Call& c, Value v) {
        c.result = std::move(v);
        return true;
    }
    void put(xq::Call& c, std::size_t i, Value v) {
        if (Value* r = c.reg(i)) {
            *r = std::move(v);
        }
    }
    std::string textOf(const xq::Call& c, std::size_t i) const {
        const std::string* t = c.text(i);
        return t ? *t : std::string();
    }

    bool loadFloor();
    std::optional<CellIndex> findCell(int kind) const;
    std::shared_ptr<adventure56::Model> modelFromArchive(int slot, const std::string& file) const;

    Actor* actor(const std::string& name);
    Actor* newActor(int klass);
    bool attach(Actor& a, const std::string& name, const std::string& model, int code);
    float surface(CellIndex at) const;
    float standHeight(CellIndex at) const;
    void place(Actor& a, CellIndex at, float frames = 0.0f);
    CellIndex cellOf(const Actor& a) const;
    void settle(Actor& a);
    bool eventBusy() const;
    const GridObject* objectAt(CellIndex at, int which, int mode = 0, float limit = 0.0f,
                               const Actor* except = nullptr) const;
    const PartDef* partOn(CellIndex at, int mode = 0, float limit = 0.0f) const;
    Actor* actorOf(const GridObject* g) const;
    bool finish(Actor& a);
    bool laytonAction(Actor& a);
    bool digAction(Actor& a, adventure56::Object3D& o);
    std::string spawnEffect(const std::string& name, const std::string& resource, const std::string& motion,
                            const std::string& materialMotion, const std::array<float, 3>& at,
                            const std::array<float, 3>& turn);
    void dropEffect(std::string& name);
    void noteEvent(const EventDef& e, int value);
    void removeActor(Actor& a);
    void freeRemovals();
    bool blockUpdate(Actor& a);
    bool blockReturn(Actor& a, adventure56::Object3D& o);
    void dropBack(Actor& a, const std::array<float, 3>& from, const std::array<float, 3>& to);
    bool switchUpdate(Actor& a);
    bool mummyUpdate(Actor& a);
    std::vector<CellIndex> findPath(Actor& a, CellIndex from, CellIndex to, int radius, bool extend, bool& found);
    int lookAlong(const Actor& a, CellIndex step, int most);
    bool sees(Actor& m, CellIndex target);
    Actor* blockOf(int number);
    void walkToGroup(Actor& a, CellIndex at, float moveFrames, float faceFrames);
    Actor* actorAt(CellIndex at, const Actor* except);
    bool requestWalk(Actor& a, CellIndex to, float moveFrames, float faceFrames);
    bool accepts(const Actor& a, int kind) const;
    void beginPending(Actor& a);
    bool canStep(const Actor& a, CellIndex from, CellIndex to, float& height);
    bool baseUpdate(Actor& a);
    bool classUpdate(Actor& a);
    void arrive(Actor& a);
    void tickActor(Actor& a);
    void runActors();
    void buildWorld(const Game::RuinRecord& record);
    void storeStands(Game::RuinRecord& record);
    void storeFloor(int room, bool blank);
    void storeEntry();
    int restore();
    bool holds(const EventDef& e) const;
    void applyEffect(const EventDef& e);
    void queueEvent(const std::string& name, bool notice, const EventDef* effects);
    void stepRunner();
    void endEvent();
    int roomEvent(const std::string& name);
    void afterFrame() override;
    void setCameraMode(int slot, int mode, float frames, int curve, Actor* subject, bool freeze);
    void pickRoomCamera();
    struct CameraSlot;
    bool viewOf(CameraSlot& slot, const Actor& subject, std::array<float, 3>& eye, std::array<float, 3>& target);
    void followCamera();

    int floorNumber_ = 1;                 // +0x21f4
    int startByte_ = 0;                   // +0x21f8 (command 104)
    std::array<std::string, 3> archives_; // +0x4ccb4: the parts archives' folders (800)
    Floor floor_;
    std::array<std::uint8_t, 13> roomHidden_{}; // +0x21d4: a bit per shell model 100..
    std::vector<std::unique_ptr<Actor>> actors_; // +0x2470 (at most 70)
    Game::RuinRecord record_; // +0x245c: the working copy of the ruins' record
    Game::RuinRecord blank_;  // +0x2468
    bool useBlank_ = false;   // +0x2208
    bool restoring_ = false;  // +0x21fc (command 130)
    // +0x62c (700 / 701): Randall follows Layton (RPG_Rand_MoveProc). The
    // loop's memory comes from a frame heap without clearing (0x2d0420)
    // and only a restored record sets it; hypothesis: on (the first
    // floor's events move the two as a pair, Randall a cell behind).
    bool randallFollows_ = true;
    bool actorsRun_ = false;    // +0x21f0 (500): the actors' update runs (0x1c798c)
    bool eventQueued_ = false;  // +0x2204 (120)
    bool retry_ = false;        // +0x2200 (105 / 134): the next restore is a retry
    std::string guideName_;     // +0x4c3c4 (134)
    std::vector<Actor*> removals_; // +0x27b8
    std::string resumeEvent_;   // +0x220c: the event playing as the loop was suspended
    bool resumeEventFlag_ = false; // +0x2210: ＜ナゾ＞あとでとけない then
    // The event runner (+0x28d0, 0x1ce15c): an event of the floor played
    // inside this loop (its script on thread 1).
    struct Runner {
        bool playing = false;          // +0x28d0: its Main runs
        bool queued = false;           // +0x28d4: one waits to start
        std::string name;              // +0x28dc: the event by name
        std::string lastName;          // +0x28d8
        int number = -1;               // +0x28e0
        int stage = 0;                 // +0x7bc8
        float noticeTime = 0;          // +0x7bcc
        const EventDef* effects = nullptr; // +0x7bd0: its effect applies as it starts
    } runner_;
    EventPlayback playback_; // +0x28e8
    bool somethingHappened_ = false; // +0x628 (697), cleared each frame by 0x1c798c
    // The guide mode (+0x4c38c): the paid hint of a floor. Its routes come
    // from mini/ruin_guide_0N.xs (0x1d1564; records 5100..5112). Standing
    // on a route's start cell offers the guide; once paid (134
    // "Run_GuideMode"), Layton and Randall start from the route's cells and
    // the route's markers show, phase by phase, where to walk, dig and push.
    // A move against the markers is a failure (+0x2b), the last phase done
    // calls Clear_GuideMode.
    struct GuideStep {         // 0x4c bytes (0x1d0448)
        std::string name;      // +0: "room%02d_phase_%02d_%02d" (route, phase, step); its marker's name
        int action = -1;       // +0x24: 1500 walk, 1501 goal, 1502 dig, 1503..1506 push, 1508 walk
        int look = 0xff;       // +0x26: the marker's look: 0 dim, 1 coming, 2 shown, 3 going
        CellIndex cell;        // +0x28
        std::array<float, 3> turn{}; // +0x30: radians (5106's turn codes)
        float lift = kNoAngle;       // +0x3c: added to the marker's height (none: FLT_MAX)
        float delay = kNoAngle;      // +0x40: frames before the marker shows
        bool counted = false;        // +0x44: a walk step Layton must reach (5106's fourth operand)
        int sound = 0;               // +0x48: an effect as the marker shows
    };
    struct GuideRoute {        // 0x44 bytes (0x1cea30)
        CellIndex telopCell;   // +0 / +4 (5111): where the room's telop shows
        std::string telop;     // +8: its picture
        bool telopShown = false;     // +0xc (142)
        CellIndex laytonCell;  // +0x10 (5107), turn +0x30
        CellIndex randallCell; // +0x18 (5108), turn +0x34
        float laytonTurn = 0, randallTurn = 0;
        CellIndex start;       // +0x20 (5109): standing here offers the guide
        std::string intro;     // +0x28 (5110): the event as the guide starts
        std::string failure;   // +0x2c: the event after a failure (144)
        std::vector<std::vector<GuideStep>> phases; // +0x38 (5102; 5104 .. 5105 each)
        std::size_t built = 0; // +0x3c: phases read so far
        float wait = 0;        // +0x40 (5112): frames before the first markers
    };
    struct GuideMode {
        std::vector<GuideRoute> routes; // +0x5c / +0x58
        int route = 0;         // +0x1c: the route offered or run
        int phase = -1;        // +0x1e: the phase shown
        int blink = 0;         // +0x20: the walk marker whose look turns
        int result = 1;        // +0x24: 140's verdict (1 none, bit 1 wrong, bit 2 right)
        CellIndex cell;        // +0x10: where Layton acted (140) ..
        float angle = kNoAngle; // +0x18: .. facing (degrees)
        float wait = 0;        // +0x34: the route's wait running out
        std::string lastMarker; // +0x60: a marker fading (143)
        // +0x28 offered (131), +0x29 running (132 / 136), +0x2a starting
        // (134), +0x2b failed (137), +0x2c cleared (138), +0x2d the phase's
        // markers all shown (145), +0x2e the intro event played, +0x2f a
        // telop showing (148), +0x30 the wait runs (146), +0x31 (147).
        std::int8_t b4 = 0, b5 = 0, b6 = 0, b7 = 0, b8 = 0, b9 = 0, be = 0, bb = 0, bc = 0, bd = 0;
    } guide_;
    void loadGuide();
    void guideDelays(std::vector<GuideStep>& steps, bool built);
    adventure56::Object3D* guideMarker(const GuideStep& s);
    void guideSpawnMarkers();
    void guideNextPhase();
    void guideActed(bool right);
    void guideOffer();
    void guideStart();
    void guideEnd();
    int guideCheck(int action);
    std::string guideTelopAt(CellIndex at) const;
    void guideFrame();
    bool flag630_ = false;      // +0x630 (704 / 705): Layton may not step onto Randall
    bool flag634_ = false;
    std::unordered_map<const Actor*, CellIndex> imagined_; // RuinsInspector::imagineBlocks
    // For the inspector's queries only (tools): where each grid object
    // stands, kept for one frame and one imagined layout.
    struct Placed {
        const GridObject* object;
        Actor* actor;
        CellIndex cell;
        float y;
    };
    mutable std::vector<Placed> placed_;
    mutable long placedFrame_ = -1, placedGeneration_ = -1;
    long imagineGeneration_ = 0;
    mutable int inspecting_ = 0;
    struct Inspecting {
        const RuinsLoop& loop;
        explicit Inspecting(const RuinsLoop& l) : loop(l) { ++loop.inspecting_; }
        ~Inspecting() { --loop.inspecting_; }
    };
    bool placeOf(const GridObject& o, Actor* a, float& x, float& y, float& z) const;
    std::map<std::pair<int, int>, int> imaginedCells_; // RuinsInspector::imagineCells
    int kindFor(const Cell& c, CellIndex at) const {
        if (!imaginedCells_.empty() && inspecting_ > 0) {
            if (const auto it = imaginedCells_.find({at.row, at.column}); it != imaginedCells_.end()) {
                return it->second;
            }
        }
        return c.kind;
    }
    int actorLevel_ = -1; // the scope level the actors' objects go to (resume)
    // +0x67c: the cells' kinds (their model rows' ids, -1 none) as the
    // loop was suspended; the next restore (101) puts them back.
    std::array<std::array<int, kColumns>, kRows> cellKindsKept_ = [] {
        std::array<std::array<int, kColumns>, kRows> k{};
        for (auto& row : k) {
            row.fill(-1);
        }
        return k;
    }();      // +0x634 (702 / 703): Randall walks his path (608)
    // The camera control (+0x2214; 0x2c3d54 sets a slot, 0x1c5f30 follows):
    // three slots of camera modes, the highest one set showing.
    struct CameraSlot {
        int mode = -1;           // +4 (-1 off)
        float frames = 0;        // +8: frames a switch to it takes
        int curve = 0;           // +0xc: their curve
        Actor* subject = nullptr; // +0x10: whom it views (Layton when none)
        // Mode 5 (0x1c566c): the target's and eye's offsets from the
        // subject when it took over (+0x18, +0x24; +0x14 marks the first
        // frame) and a shift easing the view onto the subject (+0x30).
        bool first = false;
        std::array<float, 3> targetOffset{}, eyeOffset{};
        adventure56::Tween<3> shift;
    };
    struct CameraControl {
        std::array<CameraSlot, 3> slots;
        int current = -1;         // +0x214
        bool switching = false;   // +0x218: the next follow eases to the new view
        bool settling = false;    // +0x21c: an ease runs
        bool freeze = false;      // +0x23c: Layton and Randall wait meanwhile
        bool fromNone = false;    // +0x238: the first view shows at once
        std::array<float, 3> lastEye{}, lastTarget{}; // +0x220, +0x22c
    } camera_;
};

std::shared_ptr<adventure56::Model> RuinsLoop::modelFromArchive(int slot, const std::string& file) const {
    // 0x29a1b4 / 0x2bfc90: a file of the archive by its name (the name
    // without a folder, 0x292d2c).
    if (slot < 0 || slot >= int(archives_.size()) || archives_[std::size_t(slot)].empty()) {
        return nullptr;
    }
    std::string base = file;
    if (const auto slash = base.rfind('/'); slash != std::string::npos) {
        base = base.substr(slash + 1);
    }
    if (const auto dot = base.rfind(".xc"); dot != std::string::npos && dot + 3 == base.size()) {
        base = base.substr(0, dot);
    }
    const fs::path folder = game_.pack.find(archives_[std::size_t(slot)] + "/" + base, ".xc");
    if (folder.empty()) {
        return nullptr;
    }
    auto model = adventure56::loadModel(game_.pack, folder);
    model->path = archives_[std::size_t(slot)] + "/" + base;
    return model;
}

std::optional<CellIndex> RuinsLoop::findCell(int kind) const {
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kColumns; ++c) {
            if (floor_.cells[std::size_t(r)][std::size_t(c)].kind == kind) {
                return CellIndex{r, c};
            }
        }
    }
    return std::nullopt;
}

bool RuinsLoop::loadFloor() {
    // 0x1c92b8: mini/$/ruin_floor_0N.xs through the record handlers of
    // 0x1d0b3c (5000..5014); the guide file mini/ruin_guide_0N.xs follows
    // (0x1d1564, not read yet).
    // "$" is the console region's folder (0x2b58c4 -> 0x3ac9fc: the table
    // 0x411e28 "[jp]" "[na]" "[eu]" "[cn]" "[kr]" "[tw]"); this European
    // release's is "[eu]" (as the puzzles' fonts, a5_puzzle.cpp).
    char path[48];
    std::snprintf(path, sizeof path, "mini/[eu]/ruin_floor_%02d", floorNumber_);
    const fs::path file = game_.pack.find(path, ".xs");
    if (file.empty()) {
        return false;
    }
    floor_ = Floor{};
    const auto list = import::adventure56::parseXscr(game_.pack.read(file));
    for (const auto& c : list.commands) {
        const auto row = rowOf(list, c);
        switch (c.opcode) {
        case 5000: // the model table's size
            floor_.parts.reserve(std::size_t(std::max(0, intAt(row, 0))));
            break;
        case 5001: { // 0x1d1d14 (10 values or more)
            if (row.size() < 10) {
                break;
            }
            PartDef p;
            p.id = intAt(row, 0);
            p.model = textAt(row, 1);
            p.motionA = textAt(row, 2);
            p.motionB = textAt(row, 3);
            for (std::size_t i = 0; i < 4; ++i) {
                p.reals[i] = realAt(row, 4 + i);
                p.flags[i] = i + 8 < row.size() ? intAt(row, 8 + i) : 0;
            }
            floor_.parts.push_back(std::move(p));
            break;
        }
        case 5002: { // 0x1c7f90 (5 values or more)
            if (row.size() < 5) {
                break;
            }
            const CellIndex at{intAt(row, 2), intAt(row, 3)};
            Cell* cell = floor_.cell(at);
            if (!cell) {
                break;
            }
            cell->kind = intAt(row, 0);
            cell->originalKind = cell->kind;
            cell->x = float(at.column) * kCellSize;
            cell->y = float(intAt(row, 1)) * kCellSize;
            cell->z = float(at.row) * kCellSize;
            cell->room = intAt(row, 4);
            // Kind 1006 (0x3ee) takes its height from the working record's
            // list for this floor when the cell is in it (0x186b58: entries
            // {row, column, height in cells}).
            if (cell->kind == 1006) {
                const unsigned f = unsigned(floorNumber_ - 1) & 0xff;
                if (f < 8) {
                    const auto& list = record_.raisedHeights[f];
                    if (const auto it = list.find({at.row, at.column}); it != list.end()) {
                        cell->y = float(it->second) * kCellSize;
                    }
                }
            }
            break;
        }
        case 5010: // the event slots reset (at most 120)
            floor_.events.clear();
            break;
        case 5011: { // 0x1d2b64 (23 values or more)
            if (row.size() < 23 || floor_.events.size() >= std::size_t(kEvents)) {
                break;
            }
            EventDef e;
            e.kind = intAt(row, 0);
            e.flag = intAt(row, 1);
            e.number = intAt(row, 2);
            e.conditionCode = intAt(row, 3);
            e.conditionCount = intAt(row, 4);
            auto alternative = [&](std::size_t i) {
                EventDef::Alternative a;
                a.isNumber = intAt(row, i) != 0;
                if (a.isNumber) {
                    a.number = intAt(row, i + 1);
                } else {
                    a.name = textAt(row, i + 1);
                }
                return a;
            };
            e.before = {alternative(5), alternative(7), alternative(9)};
            e.eventA = textAt(row, 11);
            e.eventB = textAt(row, 12);
            e.byteA = intAt(row, 13);
            e.notice = intAt(row, 14);
            e.effectCode = intAt(row, 15);
            e.effectCount = intAt(row, 16);
            e.after = {alternative(17), alternative(19), alternative(21)};
            floor_.events.push_back(std::move(e));
            break;
        }
        case 5012: // the object slots reset (at most 100)
            floor_.objects.clear();
            break;
        case 5013: { // 0x1d1a6c (5 values or more)
            if (row.size() < 5 || floor_.objects.size() >= std::size_t(kObjects)) {
                break;
            }
            const int kind = intAt(row, 0), number = intAt(row, 1);
            const CellIndex at{intAt(row, 2), intAt(row, 3)};
            const Cell* cell = floor_.cell(at);
            if (!cell) {
                break;
            }
            GridObject o;
            char name[24];
            std::snprintf(name, sizeof name, "evt_obj_%d", int(floor_.objects.size()));
            o.name = name;
            o.x = cell->x;
            o.y = cell->y;
            o.z = cell->z;
            o.angle = realAt(row, 4) * float(M_PI / 180.0); // 0x1d1ce4 (pi / 180)
            for (std::size_t i = 0; i < floor_.events.size(); ++i) {
                if (floor_.events[i].kind == kind && floor_.events[i].number == (number & 0xff)) {
                    o.event = int(i);
                    break;
                }
            }
            if (o.event >= 0 && cell->room >= 0 && cell->room < kRooms) {
                const int k = floor_.events[std::size_t(o.event)].kind;
                if (k == 1000) {
                    floor_.rooms[std::size_t(cell->room)].marker1000 = true;
                } else if (k == 1001) {
                    floor_.rooms[std::size_t(cell->room)].marker1001 = true;
                }
            }
            floor_.objects.push_back(std::move(o));
            break;
        }
        case 5014: { // 0x15ba5c (7 values or more)
            if (row.size() < 7) {
                break;
            }
            const int index = intAt(row, 0);
            if (index >= 0 && index < kRooms) {
                for (std::size_t i = 0; i < 6; ++i) {
                    floor_.rooms[std::size_t(index)].values[i] = intAt(row, 1 + i);
                }
            }
            break;
        }
        default:
            break;
        }
    }
    loadGuide();
    return true;
}

// ---------------------------------------------------------------- guide

void playOnce(adventure56::Object3D& o, const std::string& name);

void RuinsLoop::loadGuide() {
    // 0x1d1564 after the floor (0x1c92b8): mini/ruin_guide_0N.xs through the
    // record handlers 5100..5112. The guide's own state (the route offered,
    // its flags) stays: a paid guide restarts the loop and reloads the
    // floor (134).
    guide_.routes.clear();
    char path[40];
    std::snprintf(path, sizeof path, "mini/ruin_guide_%02d", floorNumber_);
    const fs::path file = game_.pack.find(path, ".xs");
    if (file.empty()) {
        return;
    }
    const auto list = import::adventure56::parseXscr(game_.pack.read(file));
    std::size_t routes = 0; // +0x58 while reading: the route being read
    auto route = [&]() -> GuideRoute* {
        return routes < guide_.routes.size() ? &guide_.routes[routes] : nullptr;
    };
    auto building = [&]() -> std::vector<GuideStep>* {
        GuideRoute* r = route();
        return r && r->built < r->phases.size() ? &r->phases[r->built] : nullptr;
    };
    for (const auto& c : list.commands) {
        const auto row = rowOf(list, c);
        switch (c.opcode) {
        case 5100: // 0x165c20 -> 0x1cea30: the routes {count}
            if (!row.empty()) {
                guide_.routes.assign(std::size_t(std::max(0, intAt(row, 0))), GuideRoute{});
                routes = 0;
            }
            break;
        case 5102: // 0x161eec: the route's phases {name, count}
            if (GuideRoute* r = row.size() >= 2 ? route() : nullptr) {
                r->phases.assign(std::size_t(std::max(0, intAt(row, 1) & 0xffff)), {});
                r->built = 0;
            }
            break;
        case 5104: // 0x163128 -> 0x1cf5d8: a phase {steps}
            if (auto* steps = row.empty() ? nullptr : building()) {
                steps->clear();
                steps->reserve(std::size_t(std::max(0, intAt(row, 0) & 0xffff)));
            }
            break;
        case 5106: { // 0x1cc12c -> 0x1d0448: a step {action, row, column [, counted [, turn y [, turn x [, lift [, sound]]]]]}
            auto* steps = row.size() >= 3 ? building() : nullptr;
            if (!steps) {
                break;
            }
            GuideStep s;
            char name[32];
            std::snprintf(name, sizeof name, "room%02d_phase_%02d_%02d", int(routes), int(route()->built),
                          int(steps->size()));
            s.name = name;
            s.action = std::int16_t(intAt(row, 0));
            s.cell = {intAt(row, 1) & 0xffff, intAt(row, 2) & 0xffff};
            s.counted = row.size() > 3 ? row[3].truthy() : true;
            // The turn codes: 1514 pi, 1515 0, 1516 -pi/2, 1517 pi/2 about
            // y; 1519 -pi/4, 1520 pi/4 about x (others keep 0).
            constexpr float kPi = 3.14159274f;
            if (row.size() > 4) {
                switch (intAt(row, 4)) {
                case 1514: s.turn[1] = kPi; break;
                case 1515: s.turn[1] = 0.0f; break;
                case 1516: s.turn[1] = -kPi / 2; break;
                case 1517: s.turn[1] = kPi / 2; break;
                default: break;
                }
            }
            if (row.size() > 5) {
                switch (intAt(row, 5)) {
                case 1519: s.turn[0] = -kPi / 4; break;
                case 1520: s.turn[0] = kPi / 4; break;
                default: break;
                }
            }
            s.lift = row.size() > 6 ? realAt(row, 6) : kNoAngle;
            s.sound = row.size() > 7 ? intAt(row, 7) : 0;
            s.look = 2;
            steps->push_back(std::move(s));
            break;
        }
        case 5105: // 0x160550 -> 0x1cf0c0: the phase ends
            if (auto* steps = building()) {
                guideDelays(*steps, true);
                ++route()->built;
            }
            break;
        case 5103: // 0x15e0a8: the route ends
            ++routes;
            break;
        case 5107:   // 0x1630a8: Layton's cell and turn {row, column, degrees}
        case 5108: { // 0x1604d0: Randall's
            GuideRoute* r = row.size() >= 3 ? route() : nullptr;
            if (!r) {
                break;
            }
            const CellIndex at{intAt(row, 0) & 0xffff, intAt(row, 1) & 0xffff};
            const float turn = realAt(row, 2) * 0.017453292f;
            (c.opcode == 5107 ? r->laytonCell : r->randallCell) = at;
            (c.opcode == 5107 ? r->laytonTurn : r->randallTurn) = turn;
            break;
        }
        case 5109: // 0x161e84: the start cell {row, column}
            if (GuideRoute* r = row.size() >= 2 ? route() : nullptr) {
                r->start = {intAt(row, 0) & 0xffff, intAt(row, 1) & 0xffff};
            }
            break;
        case 5110: // 0x161e00: the events {intro [, failure]}
            if (GuideRoute* r = row.empty() ? nullptr : route()) {
                r->intro = textAt(row, 0);
                r->failure = row.size() > 1 ? textAt(row, 1) : std::string();
            }
            break;
        case 5111: // 0x161d84: the telop {row, column, picture}
            if (GuideRoute* r = row.size() >= 3 ? route() : nullptr) {
                r->telopCell = {intAt(row, 0) & 0xffff, intAt(row, 1) & 0xffff};
                r->telop = textAt(row, 2);
                r->telopShown = false;
            }
            break;
        case 5112: // 0x15e058: the wait before the markers {frames}
            if (GuideRoute* r = row.empty() ? nullptr : route()) {
                r->wait = float(std::uint32_t(intAt(row, 0)));
            }
            break;
        default: // 5101 (0x1656d8): the reading ends
            break;
        }
    }
}

void RuinsLoop::guideDelays(std::vector<GuideStep>& steps, bool built) {
    // When markers show: the walk markers (1500, 1501, 1508) one after the
    // other every 5 frames from 10, the action markers with the last walk
    // marker before them, others at 10. As a phase is read (0x1cf0c0) the
    // walk markers come in (look 3; the last one shown, 2) and the dig and
    // push markers (1502..1506) are dim (0); a guide starting (0x1cf218)
    // only times them, dig markers with the walk markers.
    int walks = 0, lastWalk = -1;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        GuideStep& s = steps[i];
        const bool walk = s.action == 1500 || s.action == 1501 || s.action == 1508;
        const bool acted = built ? s.action >= 1502 && s.action <= 1506 : s.action == 1502;
        if (walk) {
            s.delay = float(walks * 5 + 10);
            if (built) {
                s.look = 3;
                lastWalk = int(i);
            }
            ++walks;
        } else if (acted) {
            s.delay = float(std::max(walks - 1, 0) * 5 + 10);
            if (built) {
                s.look = 0;
            }
        } else {
            s.delay = 10.0f;
        }
    }
    if (lastWalk >= 0) {
        steps[std::size_t(lastWalk)].look = 2;
    }
}

adventure56::Object3D* RuinsLoop::guideMarker(const GuideStep& s) {
    return game_.scene.object3D(xq::hash(s.name));
}

void RuinsLoop::guideSpawnMarkers() {
    // 0x1cf700: every marker of the route from the effect pool (0x292dec),
    // by its action: r402 walk (1500, 1508), r405 goal (1501), r403 dig
    // (1502), r401 push (1503..1506, turned to the push: pi, 0, 3pi/2,
    // pi/2). It stands on its cell (a raised cell kind 1002 one unit up,
    // 1001 / 1007 six, an actor's part 411 / 412 half a unit), unseen
    // (alpha 0, switched off: 0x2bd3cc shows a phase), lifted by the
    // step's lift, turned about y and then x.
    if (guide_.route < 0 || std::size_t(guide_.route) >= guide_.routes.size()) {
        return;
    }
    constexpr float kPi = 3.14159274f;
    for (auto& phase : guide_.routes[std::size_t(guide_.route)].phases) {
        for (GuideStep& s : phase) {
            std::string resource;
            float baseTurn = 0.0f;
            switch (s.action) {
            case 1500: case 1508: resource = "r402"; break;
            case 1501: resource = "r405"; break;
            case 1502: resource = "r403"; break;
            case 1503: resource = "r401"; baseTurn = kPi; break;
            case 1504: resource = "r401"; break;
            case 1505: resource = "r401"; baseTurn = 3 * kPi / 2; break;
            case 1506: resource = "r401"; baseTurn = kPi / 2; break;
            default: continue;
            }
            const Cell* cell = floor_.cellAt(s.cell);
            std::array<float, 3> at{};
            if (cell) {
                at = {cell->x, cell->y, cell->z};
                if (cell->kind == 1002) {
                    at[1] += 1.0f;
                }
                if (cell->kind == 1001 || cell->kind == 1007) {
                    at[1] += 6.0f;
                }
                if (const PartDef* p = partOn(s.cell); p && (p->id == 411 || p->id == 412)) {
                    at[1] += 0.5f;
                }
            }
            if (spawnEffect(s.name, resource, "", "", at, {0.0f, 0.0f, 0.0f}).empty()) {
                continue;
            }
            adventure56::Object3D* o = guideMarker(s);
            if (!o) {
                continue;
            }
            if (cell) {
                o->alpha.set({0.0f});
            }
            o->touchOn = false;
            o->switch14 = false;
            o->drawn = false;
            if (s.lift != kNoAngle) {
                auto p = o->position.value();
                p[1] += s.lift;
                o->position.set(p);
            }
            if (s.turn == std::array<float, 3>{0.0f, 0.0f, 0.0f}) {
                o->rotation3.set({0.0f, baseTurn, 0.0f});
            } else {
                // The quaternion of y by (turn y + base) times that of x by
                // turn x, as the objects' X, Y, Z angles (Scene::detach3D).
                const float y = s.turn[1] + baseTurn, x = s.turn[0];
                const float cy = std::cos(y), sy = std::sin(y), cx = std::cos(x), sx = std::sin(x);
                // Ry * Rx = [[cy, sy sx, sy cx], [0, cx, -sx], [-sy, cy sx, cy cx]].
                const float ex = std::atan2(sx, cy * cx);
                const float ey = std::asin(std::clamp(sy * cx, -1.0f, 1.0f));
                const float ez = std::atan2(-sy * sx, cy);
                o->rotation3.set({ex, ey, ez});
            }
        }
    }
}

void RuinsLoop::guideNextPhase() {
    // 0x2bd3cc: the next phase: the shown phase's markers leave the view's
    // care (no longer drawn), the next one's are drawn, those already due
    // fading in over 15 frames. Past the last phase the route is cleared
    // (Clear_GuideMode).
    if (guide_.route < 0 || std::size_t(guide_.route) >= guide_.routes.size()) {
        return;
    }
    GuideRoute& r = guide_.routes[std::size_t(guide_.route)];
    const int shown = guide_.phase++;
    if (shown >= 0 && std::size_t(shown) < r.phases.size()) {
        for (const GuideStep& s : r.phases[std::size_t(shown)]) {
            if (auto* o = guideMarker(s)) {
                o->drawn = false;
            }
        }
    }
    if (std::size_t(guide_.phase) < r.built) {
        for (const GuideStep& s : r.phases[std::size_t(guide_.phase)]) {
            if (auto* o = guideMarker(s)) {
                // (The ruins' view care, 0x1c8270, switches a marker on
                // while it is in view; the port draws it throughout.)
                o->touchOn = true;
                o->switch14 = true;
                o->drawn = true;
                if (s.delay < 0.0f) {
                    o->alpha.start({1.0f}, 15.0f, 0);
                }
            }
        }
    }
    if (int(r.built) <= guide_.phase) {
        guide_.b8 = 1;
        threads[0]->callback("Clear_GuideMode", {});
    }
    guide_.blink = 0;
    guide_.b9 = 0;
}

void RuinsLoop::guideActed(bool right) {
    // 0x1cecb8 (140, after a dig or push the markers judged): the phase's
    // markers fade out over 6 frames (143 waits for the last), where Layton
    // stands and faces is kept for 141's judgement; a right action in the
    // last phase clears the route.
    if (guide_.route < 0 || std::size_t(guide_.route) >= guide_.routes.size()) {
        return;
    }
    GuideRoute& r = guide_.routes[std::size_t(guide_.route)];
    if (guide_.phase >= 0 && std::size_t(guide_.phase) < r.phases.size()) {
        for (const GuideStep& s : r.phases[std::size_t(guide_.phase)]) {
            if (auto* o = guideMarker(s)) {
                o->alpha.start({0.0f}, 6.0f, 0);
                guide_.lastMarker = s.name;
            }
        }
    }
    float angle = 0.0f;
    if (Actor* l = actor(kLayton)) {
        guide_.cell = cellOf(*l);
        if (auto* o = game_.scene.object3D(l->hash)) {
            angle = o->rotation3.value()[1];
        }
    }
    guide_.angle = angle * 57.29578f;
    if (int(r.built) <= guide_.phase + 1 && right) {
        guide_.b8 = 1;
        threads[0]->callback("Clear_GuideMode", {});
    }
}

void RuinsLoop::guideOffer() {
    // 0x1cef58 (139, and each frame): while no event plays, Layton on a
    // route's start cell offers that route (+0x28, 131); standing on an
    // object whose event is of kind 400 withdraws the offer.
    if (runner_.playing || runner_.queued) {
        return;
    }
    Actor* l = actor(kLayton);
    if (!l) {
        return;
    }
    const CellIndex at = cellOf(*l);
    if (guide_.b4 == 0) {
        for (std::size_t i = 0; i < guide_.routes.size(); ++i) {
            if (guide_.routes[i].start == at) {
                guide_.route = int(i);
                guide_.b4 = 1;
                break;
            }
        }
    }
    const GridObject* o = objectAt(at, 0);
    if (o && o->event >= 0 && std::size_t(o->event) < floor_.events.size() &&
        floor_.events[std::size_t(o->event)].kind == 400) {
        guide_.b4 = 0;
    }
}

void RuinsLoop::guideStart() {
    // 0x1cf218 (139 after 134 restarted the loop for the guide): the
    // markers' times afresh, Layton and Randall to the route's cells and
    // turns, ResetMirraRoom when Layton's room holds a mummy's marker (an
    // object of event kind 1001), the guide's block (134's name: "Run_
    // GuideMode"), the route's wait.
    if (guide_.route < 0 || std::size_t(guide_.route) >= guide_.routes.size()) {
        return;
    }
    GuideRoute& r = guide_.routes[std::size_t(guide_.route)];
    for (std::size_t i = 0; i < r.built; ++i) {
        guideDelays(r.phases[i], false);
    }
    const auto setUp = [this](const std::string& name, CellIndex at, float turn) {
        Actor* a = actor(name);
        if (!a) {
            return;
        }
        place(*a, at);
        if (auto* o = game_.scene.object3D(a->hash)) {
            o->rotation3.set({0.0f, turn, 0.0f});
        }
    };
    setUp(kLayton, r.laytonCell, r.laytonTurn);
    setUp(kRandall, r.randallCell, r.randallTurn);
    if (const Cell* cell = floor_.cellAt(r.laytonCell);
        cell && cell->room >= 0 && cell->room < kRooms && floor_.rooms[std::size_t(cell->room)].marker1001) {
        threads[0]->callback("ResetMirraRoom", std::array<Value, 1>{Value::ofInt(floorNumber_)});
    }
    if (!guideName_.empty()) {
        threads[0]->callback(guideName_, {});
    }
    guide_.b5 = guide_.b6 = guide_.b7 = guide_.b8 = guide_.b9 = guide_.be = guide_.bc = 0;
    guide_.wait = r.wait;
    guide_.phase = -1;
    guide_.cell = {};
    guide_.angle = kNoAngle;
    guide_.lastMarker.clear();
    guide_.result = 1;
}

void RuinsLoop::guideEnd() {
    // 0x1ceb2c (133): the route's markers go back to the pool; the guide
    // stops. A cleared route passes the offer to the next one.
    if (guide_.route >= 0 && std::size_t(guide_.route) < guide_.routes.size()) {
        for (auto& phase : guide_.routes[std::size_t(guide_.route)].phases) {
            for (const GuideStep& s : phase) {
                std::string name = s.name;
                if (guideMarker(s)) {
                    dropEffect(name);
                }
            }
        }
    }
    guide_.b5 = guide_.b6 = guide_.b7 = 0;
    guide_.phase = -1;
    guide_.cell = {};
    guide_.angle = kNoAngle;
    if (guide_.b8) {
        ++guide_.route;
        guide_.b4 = 0;
    }
    guide_.b8 = guide_.b9 = guide_.be = guide_.bc = 0;
    guide_.result = 1;
    guide_.lastMarker.clear();
}

int RuinsLoop::guideCheck(int action) {
    // 0x2bbf8c: Layton against the shown phase's markers of an action
    // (bits: 1 nothing to say, 2 wrong, 4 right). Layton's cell and facing
    // (degrees, floored) are those kept by 140 when set, else his own;
    // he must be at rest... (class 1: Layton).
    if (guide_.route < 0 || std::size_t(guide_.route) >= guide_.routes.size() || guide_.phase == -1) {
        return 1;
    }
    Actor* l = actor(kLayton);
    if (!l || l->klass != 1) {
        return 1;
    }
    CellIndex at = cellOf(*l);
    float angle = 0.0f;
    if (auto* o = game_.scene.object3D(l->hash)) {
        angle = o->rotation3.value()[1];
    }
    if (guide_.cell.row != -1 || guide_.cell.column != -1) {
        at = guide_.cell;
    }
    angle *= 57.29578f;
    if (guide_.angle != kNoAngle) {
        angle = guide_.angle;
    }
    angle = std::floor(angle);
    GuideRoute& r = guide_.routes[std::size_t(guide_.route)];
    if (int(r.built) <= guide_.phase) {
        return 1;
    }
    const auto& steps = r.phases[std::size_t(guide_.phase)];
    const auto near = [](float a, float want) { return want <= a + 5.0f && a - 5.0f <= want; };
    if (action == 1500 || action == 1501) {
        // Walks: right on the last counted walk marker of a phase that has
        // only walks; wrong at rest anywhere else; uncounted ones say
        // nothing.
        bool hit = false, others = false;
        int last = -1, hitAt = -1, uncounted = 0, walks = 0;
        for (std::size_t i = 0; i < steps.size(); ++i) {
            const GuideStep& s = steps[i];
            if (s.action != 1500 && s.action != 1501) {
                others = true;
                continue;
            }
            if (!s.counted) {
                ++uncounted;
            } else {
                last = int(i);
                if (s.cell == at) {
                    hit = true;
                    hitAt = int(i);
                }
            }
            ++walks;
        }
        if (uncounted == walks) {
            hit = true;
        }
        if (last < 0) {
            return 1;
        }
        if (!hit) {
            return 2;
        }
        const bool atLast = hitAt < 0 ? hitAt == 0 : last == hitAt;
        return atLast && !others ? 5 : 1;
    }
    if (action == 1508) {
        int result = 1;
        for (const GuideStep& s : steps) {
            if (s.action == 1508 && s.cell == at) {
                result |= 4;
            }
        }
        return result;
    }
    // Digs and pushes: in a room with a mummy's marker anything else is
    // wrong (a dig only when the phase has no 1508 walk).
    int result = 1;
    if (const Cell* cell = floor_.cellAt(at);
        cell && cell->room >= 0 && cell->room < kRooms && floor_.rooms[std::size_t(cell->room)].marker1001) {
        bool wrong = true;
        if (action == 1502) {
            for (const GuideStep& s : steps) {
                if (s.action == 1508) {
                    wrong = false;
                }
            }
        }
        if (wrong) {
            result = 2;
        }
    }
    const GuideStep* step = nullptr;
    for (const GuideStep& s : steps) {
        if (s.action == action) {
            step = &s;
            break;
        }
    }
    if (!step) {
        return result;
    }
    const CellIndex m = step->cell;
    const bool south = at == CellIndex{m.row + 1, m.column}, north = at == CellIndex{m.row - 1, m.column};
    const bool west = at == CellIndex{m.row, m.column - 1}, east = at == CellIndex{m.row, m.column + 1};
    if (!south && !north && !west && !east) {
        if (action != 1502 && l->state == 6) {
            return 2; // a push asked for beside no push marker
        }
        return result;
    }
    if (action == 1502) {
        // A dig: beside the marker, facing it.
        if (south && (near(angle, 180.0f) || near(angle, -180.0f))) {
            return 5;
        }
        if (north && near(angle, 0.0f)) {
            return 5;
        }
        if (west && near(angle, 90.0f)) {
            return 5;
        }
        if (east && near(angle, -90.0f)) {
            return 5;
        }
        return 2;
    }
    if (action < 1503 || action > 1506) {
        return result;
    }
    // A push: beside the marker facing its direction (1503 180, 1504 0,
    // 1505 -90, 1506 90 degrees; the original compares every side with
    // that one facing).
    const float want = action == 1503 ? 180.0f : action == 1505 ? -90.0f : action == 1506 ? 90.0f : 0.0f;
    if (south && (near(angle, want) || near(angle, -want))) {
        return 5;
    }
    if ((north || east || west) && near(angle, want)) {
        return 5;
    }
    return 2;
}

std::string RuinsLoop::guideTelopAt(CellIndex at) const {
    // 0x1d05bc: in a room not yet seen on this floor, the telop of a route
    // whose telop cell Layton stands on and that has not shown it.
    const Cell* cell = floor_.cellAt(at);
    if (!cell) {
        return {};
    }
    const unsigned room = unsigned(std::max(0, cell->room)) & 0xff;
    const unsigned f = unsigned(floorNumber_ - 1) & 0xff;
    if (f < 8 && room < 32 && ((record_.roomsSeen[f] >> room) & 1u) != 0) {
        return {};
    }
    for (const GuideRoute& r : guide_.routes) {
        if (r.telopCell == at && !r.telopShown) {
            return r.telop;
        }
    }
    return {};
}

void RuinsLoop::guideFrame() {
    // 0x1cfca0, each frame while no event plays: a room's telop
    // (DrawGuideTelop, once: 《ＤＸ》テロップフラグ), the offer, and the
    // running guide: its intro event, the wait, the markers phase by phase
    // and Layton's walks judged against them.
    if (runner_.playing || runner_.queued) {
        return;
    }
    constexpr float kStep = 1.0f; // the loop's frame step (as the actors')
    static const std::string kTelopFlag =
        "\x81\x73\x82\x63\x82\x77\x81\x74\x83\x65\x83\x8d\x83\x62\x83\x76\x83\x74\x83\x89\x83\x4f"; // 《ＤＸ》テロップフラグ
    if (Actor* l = actor(kLayton)) {
        const int flag = game_.flagId(kTelopFlag);
        const bool told = flag >= 0 && game_.flag(flag);
        if (!told && guide_.bb == 0) {
            const std::string telop = guideTelopAt(cellOf(*l));
            if (!telop.empty() &&
                threads[0]->callback("DrawGuideTelop", std::array<Value, 1>{Value::ofText(telop)})) {
                if (flag >= 0) {
                    game_.setFlag(flag, true);
                }
                guide_.bb = 1;
            }
        }
    }
    guideOffer();
    if (guide_.b5 == 0 || guide_.b8 != 0 || guide_.route < 0 ||
        std::size_t(guide_.route) >= guide_.routes.size()) {
        return;
    }
    GuideRoute& r = guide_.routes[std::size_t(guide_.route)];
    if (!r.intro.empty() && guide_.be == 0) {
        queueEvent(r.intro, false, nullptr);
        guide_.be = 1;
        return;
    }
    if (guide_.bc == 0) {
        return;
    }
    if (guide_.wait > 0.0f) {
        guide_.wait = std::max(guide_.wait - kStep, 0.0f);
        return;
    }
    if (guide_.phase < 0) {
        guideSpawnMarkers();
        guideNextPhase();
    }
    if (guide_.phase < 0 || std::size_t(guide_.phase) >= r.phases.size()) {
        return;
    }
    auto& steps = r.phases[std::size_t(guide_.phase)];
    bool allDue = true;
    int walk = 0;
    for (GuideStep& s : steps) {
        if (s.delay < 0.0f) {
            continue;
        }
        s.delay -= kStep;
        if (s.delay >= 0.0f) {
            allDue = false;
            continue;
        }
        s.delay = -1.0f;
        const bool walkMarker = s.action == 1500 || s.action == 1508;
        if (!walkMarker) {
            s.look = 2;
        } else {
            const int index = walk++;
            if (guide_.b9 != 0) {
                // The phase is all shown: every 10 frames the walk markers
                // turn in order, one at a time (dim -> coming -> going).
                s.delay = 10.0f;
                if (guide_.blink == index) {
                    if (s.look == 2) {
                        guide_.blink = 0;
                        continue;
                    }
                    if (s.look == 0 || s.look == 3) {
                        s.look = 1;
                    } else {
                        ++guide_.blink;
                        s.look = 3;
                    }
                } else if (s.look == 3) {
                    continue;
                } else if (s.look < 2) {
                    s.look = 3;
                }
            }
        }
        adventure56::Object3D* o = guideMarker(s);
        if (o) {
            if (guide_.b9 == 0) {
                o->alpha.set({1.0f});
            }
            const auto material = [o](const std::string& name, bool once) {
                o->materialMotion = name;
                o->materialMotionTime = 0;
                o->materialMotionOnce = once;
                o->materialMotionDone = false;
            };
            if (walkMarker) {
                switch (s.look) {
                case 1: material("001_\x97\xa7\x82\xbfIN", true); break;   // 001_立ちIN
                case 2: material("002_\x97\xa7\x82\xbfLOOP", false); break; // 002_立ちLOOP
                case 3: material("003_\x97\xa7\x82\xbfOUT", true); break;  // 003_立ちOUT
                case 0: material("004_\x88\xc3LOOP", true); break;          // 004_暗LOOP
                default: break;
                }
            } else if (s.action == 1501) {
                material("r405_001", false);
            } else if (s.look == 2) {
                playOnce(*o, "001_\x97\xa7\x82\xbf"); // 001_立ち (0x2936c0)
            }
        }
        if (guide_.b9 == 0 && s.sound != 0) {
            game_.playEffect(s.sound);
        }
    }
    if (guide_.b9 == 0 && allDue) {
        guide_.b9 = 1;
        for (GuideStep& s : steps) {
            s.delay = 0.0f;
        }
    }
    const int verdict = guideCheck(1500) | guideCheck(1501) | guideCheck(1508);
    if (verdict & 2) {
        guide_.b7 = 1;
    }
    if (verdict & 4) {
        guideNextPhase();
    }
}

Actor* RuinsLoop::actor(const std::string& name) {
    // 0x2cdaf0: by the name's hash.
    const std::uint32_t h = xq::hash(name);
    for (auto& a : actors_) {
        if (a && a->hash == h) {
            return a.get();
        }
    }
    return nullptr;
}

Actor* RuinsLoop::newActor(int klass) {
    // 0x2cc8ec: a free slot of the 70 (the classes differ in what they do
    // each frame, not in what they keep).
    if (klass == -1 || actors_.size() >= std::size_t(kActors)) {
        return nullptr;
    }
    auto a = std::make_unique<Actor>();
    a->klass = klass;
    // 0x2cc8ec: the classes' constants.
    switch (klass) {
    case 1:
    case 2:
        a->climb = 6.0f, a->factor = 0.65f;
        break;
    case 3:
        a->climb = 3.0f, a->factor = 0.75f, a->ontoHoles = true;
        a->moveMotion = "\x95\xe0\x82\xab"; // 歩き
        break;
    case 4:
        a->climb = 2.0f, a->factor = 1.0f;
        break;
    case 5:
        a->factor = 0.9f;
        a->dormant = true;
        break;
    case 6:
    case 9:
        a->dormant = true;
        break;
    default:
        break;
    }
    actors_.push_back(std::move(a));
    return actors_.back().get();
}

bool RuinsLoop::attach(Actor& a, const std::string& name, const std::string& model, int code) {
    // 0x2cc7b4 -> 0x29345c: the actor's 3D object (named as the actor,
    // screen 0, priority 0x6e, kind 2) with the model resource, shown.
    adventure56::Resource* r = game_.scene.resource(model);
    if (name.empty() || !r || !r->model) {
        return false;
    }
    auto& o = game_.scene.create3D(name, 0, 0x6e, 2, actorLevel_);
    o.model = r->model;
    o.visible = true;
    a.name = name;
    a.hash = xq::hash(name);
    a.code = code;
    return true;
}

float RuinsLoop::surface(CellIndex at) const {
    // 0x2c3c5c: the cell's height plus its kind's model height (+0xdc, the
    // fourth real); 0 when the kind has no model row.
    const Cell* cell = floor_.cellAt(at);
    if (!cell) {
        return 0.0f;
    }
    const PartDef* p = floor_.part(cell->kind);
    return p ? cell->y + p->reals[3] : 0.0f;
}

float RuinsLoop::standHeight(CellIndex at) const {
    // 0x2cc658: the surface, plus the height of the object an actor shows
    // on that cell (0x2be608: the grid object there with an actor, its
    // model row's fourth real).
    float h = surface(at);
    if (const GridObject* o = objectAt(at, 1)) {
        if (const PartDef* p = floor_.part(o->actorModel)) {
            h += p->reals[3];
        }
    }
    return h;
}

const GridObject* RuinsLoop::objectAt(CellIndex at, int which, int mode, float limit, const Actor* except) const {
    // 0x2c0158: a grid object on a cell, where its actor stands for one
    // with an actor (0x293f78), else where it was placed; `which` 1 takes
    // only objects with an actor, 2 only those without; `except` skips one
    // actor's. Mode 0 the first; 1 the highest at or below `limit`; 2 the
    // lowest at or above it.
    const GridObject* found = nullptr;
    float foundY = 0.0f;
    const auto consider = [&](const GridObject& o, Actor* a, CellIndex cell, float y) {
        if ((which == 1 && !a) || (which == 2 && a) || (except && a == except) || !(cell == at)) {
            return false;
        }
        if (mode == 0) {
            found = &o;
            return true;
        }
        if (mode == 1 && y <= limit && (!found || y >= foundY)) {
            found = &o, foundY = y;
        } else if (mode == 2 && y >= limit && (!found || y <= foundY)) {
            found = &o, foundY = y;
        }
        return false;
    };
    if (inspecting_ > 0) {
        // (A tool's queries: the places kept for this frame and layout.)
        if (placedFrame_ != long(game_.frame) || placedGeneration_ != imagineGeneration_) {
            placed_.clear();
            for (const auto& o : floor_.objects) {
                Actor* a = actorOf(&o);
                float x = 0, y = 0, z = 0;
                if (placeOf(o, a, x, y, z)) {
                    placed_.push_back({&o, a, cellAt(x, z), y});
                }
            }
            placedFrame_ = long(game_.frame);
            placedGeneration_ = imagineGeneration_;
        }
        for (const auto& p : placed_) {
            if (consider(*p.object, p.actor, p.cell, p.y)) {
                return found;
            }
        }
        return found;
    }
    for (const auto& o : floor_.objects) {
        Actor* a = actorOf(&o);
        if ((which == 1 && !a) || (which == 2 && a) || (except && a == except)) {
            continue;
        }
        float x = 0, y = 0, z = 0;
        if (!placeOf(o, a, x, y, z)) {
            continue;
        }
        if (consider(o, a, cellAt(x, z), y)) {
            return found;
        }
    }
    return found;
}

bool RuinsLoop::placeOf(const GridObject& o, Actor* a, float& x, float& y, float& z) const {
    // Where a grid object stands: its actor's 3D object for one with an
    // actor, else where it was placed; false when it is gone (its name
    // cleared, 0x2bccec against "", or a tool's plan took it away).
    if (o.name.empty()) {
        return false;
    }
    x = o.x, y = o.y, z = o.z;
    const auto imagined = a && !imagined_.empty() ? imagined_.find(a) : imagined_.end();
    if (imagined != imagined_.end()) {
        // A tool's plan (RuinsInspector::imagineBlocks).
        if (imagined->second.row < 0) {
            return false;
        }
        if (const Cell* c = floor_.cellAt(imagined->second)) {
            if (auto* a3 = const_cast<adventure56::Scene&>(game_.scene).object3D(a->hash)) {
                y = a3->position[1] + a3->offset[1];
            }
            x = c->x, z = c->z;
        }
    } else if (a) {
        if (auto* a3 = const_cast<adventure56::Scene&>(game_.scene).object3D(a->hash)) {
            x = a3->position[0] + a3->offset[0];
            y = a3->position[1] + a3->offset[1];
            z = a3->position[2] + a3->offset[2];
        }
    }
    return true;
}

const PartDef* RuinsLoop::partOn(CellIndex at, int mode, float limit) const {
    // 0x2be608: the model row of the actor's object on a cell (0x2c0158
    // over objects with an actor).
    const GridObject* o = objectAt(at, 1, mode, limit);
    return o ? floor_.part(o->actorModel) : nullptr;
}

Actor* RuinsLoop::actorOf(const GridObject* g) const {
    return g && g->actor >= 0 && std::size_t(g->actor) < actors_.size() ? actors_[std::size_t(g->actor)].get()
                                                                          : nullptr;
}

Actor* RuinsLoop::actorAt(CellIndex at, const Actor* except) {
    // 0x2bd710: the first actor (but `except`) standing on the cell.
    for (auto& a : actors_) {
        if (a && a.get() != except && cellOf(*a) == at) {
            return a.get();
        }
    }
    return nullptr;
}

void RuinsLoop::place(Actor& a, CellIndex at, float frames) {
    // 0x2cc658: the object to the cell (at once, or over the frames), the
    // actor's cells to it.
    const Cell* cell = floor_.cellAt(at);
    if (!cell) {
        return;
    }
    if (auto* o = game_.scene.object3D(a.hash)) {
        const std::array<float, 3> to{cell->x, standHeight(at), cell->z};
        if (frames == 0.0f) {
            o->position.set(to);
        } else {
            o->position.start(to, frames, 0);
        }
    }
    a.dest = at;
    a.last = at;
}

CellIndex RuinsLoop::cellOf(const Actor& a) const {
    // 0x2cce34: from the object's position.
    if (auto* o = const_cast<adventure56::Scene&>(game_.scene).object3D(a.hash)) {
        const auto p = o->position.value();
        return cellAt(p[0], p[2]);
    }
    return a.dest;
}

void RuinsLoop::settle(Actor& a) {
    // The common "stop" of the commands (510, 602 ..): unless the actor is
    // marked idle (+0x3c), its state goes to 0, settled, its target back to
    // where it stands.
    if (!a.idle) {
        a.state = 0;
        a.settled = true;
        a.target = a.dest;
    }
}

// ---------------------------------------------------------------- actor commands

// 0x2c1128 with no flags: a model motion by name (as 6810: it restarts
// when it changes or a played-once one ended). The blend frames it takes
// (16 resting, 5 stepping) are not modelled, as for 6810.
void playMotion(adventure56::Object3D& o, const std::string& name) {
    if (name.empty()) {
        return;
    }
    if (o.motion != name || o.motionDone) {
        o.motionTime = 0;
    }
    o.motion = name;
    o.motionOnce = false;
    o.motionDone = false;
}

// 0x263044 (as 6223): the rotation towards `to` over `frames`, each angle
// wrapped and taken the short way round.
void turnTo(adventure56::Object3D& o, std::array<float, 3> to, float frames) {
    auto from = o.rotation3.value();
    for (std::size_t i = 0; i < 3; ++i) {
        from[i] = adventure56::wrapAngle(from[i]);
        to[i] = adventure56::wrapAngle(to[i]);
        const float d = to[i] - from[i];
        if (std::fabs(d) > 3.14159274f) { // 0x40490fdb
            from[i] = d >= 0.0f ? from[i] + 6.28318548f : from[i] - 6.28318548f;
        }
    }
    o.rotation3.set(from);
    o.rotation3.start(to, frames, 0);
}

// 0x2be1d0 behind the check that either operand is non-zero.
float heading(float y, float x) {
    if (((std::bit_cast<std::uint32_t>(y) | std::bit_cast<std::uint32_t>(x)) & 0x7fffffffu) >> 23 == 0) {
        return 0.0f;
    }
    return std::atan2(y, x);
}

// 0x292fe8: the object turned to face a direction (0x36dc5c: its angles
// about x, y and z; the first and last only with an upward part).
void face(adventure56::Object3D& o, const std::array<float, 3>& d, float frames) {
    const float ax = d[1] != 0.0f ? heading(d[1], d[2]) : 0.0f;
    const float ay = heading(d[0], d[2]);
    const float az = d[1] != 0.0f ? heading(d[0], d[1]) : 0.0f;
    turnTo(o, {ax, ay, az}, frames);
}

bool RuinsLoop::eventBusy() const {
    // The runner's two flags (+0x28d4 waiting, +0x28d0 playing) or thread
    // 1 running (0x2cce08).
    return runner_.queued || runner_.playing || threads[1]->running();
}

bool RuinsLoop::requestWalk(Actor& a, CellIndex to, float moveFrames, float faceFrames) {
    // 0x2c3e70: not for an idle actor nor while an event runs; to its own
    // cell it settles; else a pending step (1) to the cell.
    if (a.idle || eventBusy()) {
        return false;
    }
    if (cellOf(a) == to) {
        settle(a);
        return false;
    }
    a.state = 1;
    a.settled = false;
    a.target = to;
    a.dest = to;
    a.pending = Order{};
    a.pending.target = to;
    a.pending.moveFrames = moveFrames;
    a.pending.faceFrames = faceFrames;
    return true;
}

bool RuinsLoop::accepts(const Actor& a, int kind) const {
    // vtable +0xc: 0x1d4114 takes any command; Layton's (0x1cc2c4) takes
    // no step (1) while a class 4 actor (a block) is busy.
    if (a.klass == 1 && kind == 1) {
        for (const auto& b : actors_) {
            if (b && b->klass == 4 && b->action != -1 && b->action != 0) {
                return false;
            }
        }
    }
    return true;
}

void RuinsLoop::beginPending(Actor& a) {
    // 0x2ca6e8: at rest (action -1 or 0) a pending command the class takes
    // becomes the active one; it began this frame.
    if (a.action != -1 && a.action != 0) {
        return;
    }
    if (!accepts(a, a.state)) {
        return;
    }
    a.began = true;
    a.action = a.state;
    a.state = -1;
    a.beganSettled = a.settled;
    a.settled = false;
    a.active = a.pending;
    a.pending = Order{};
}

bool RuinsLoop::canStep(const Actor& a, CellIndex from, CellIndex to, float& height) {
    // vtable +8. 0x26078c: both cells have a model row; the target's is
    // walkable (its flag 3; kind 300 for actors that may) and, with flag
    // 1 / 2 on either cell, in the same column / row; an object with an
    // actor on it must be walkable too; the heights (surface plus such an
    // object's height) differ by less than the actor's climb.
    const Cell* fc = floor_.cellAt(from);
    const Cell* tc = floor_.cellAt(to);
    const PartDef* fp = fc ? floor_.part(kindFor(*fc, from)) : nullptr;
    const PartDef* tp = tc ? floor_.part(kindFor(*tc, to)) : nullptr;
    if (!fp || !tp) {
        return false;
    }
    if (a.klass == 4) {
        // 0x346e1c, a block: onto a hole (300), kind 1005 or a walkable
        // cell; an object there must be walkable; it may drop any height
        // and rise less than its climb.
        if (!(tp->id == 300 || tp->id == 1005 || tp->flags[3] != 0)) {
            return false;
        }
        const PartDef* op = partOn(to);
        if (op && op->flags[3] == 0) {
            return false;
        }
        const float h = surface(to) + (op ? op->reals[3] : 0.0f);
        if (!(a.climb > h - surface(from))) {
            return false;
        }
        height = h;
        return true;
    }
    if (!((a.ontoHoles && tp->id == 300) || tp->flags[3] != 0)) {
        return false;
    }
    if ((fp->flags[1] != 0 || tp->flags[1] != 0) && to.column != from.column) {
        return false;
    }
    if ((fp->flags[2] != 0 || tp->flags[2] != 0) && to.row != from.row) {
        return false;
    }
    const GridObject* onTarget = objectAt(to, 1);
    const PartDef* op = onTarget ? floor_.part(onTarget->actorModel) : nullptr;
    if (onTarget && (!op || op->flags[3] == 0)) {
        return false;
    }
    float hTo = surface(to);
    if (op) {
        hTo += op->reals[3];
    }
    float hFrom = surface(from);
    if (const GridObject* under = objectAt(from, 1)) {
        if (const PartDef* up = floor_.part(under->actorModel)) {
            hFrom += up->reals[3];
        }
    }
    if (!(a.climb > std::fabs(hTo - hFrom))) {
        return false;
    }
    switch (a.klass) {
    case 1:
        // 0x347358: with +0x630 set, not onto Randall (an actor whose name
        // begins ミニランド).
        if (flag630_) {
            if (Actor* b = actorAt(to, nullptr); b && prefix(b->name, kRandall)) {
                return false;
            }
        }
        break;
    case 2:
        // 0x346d6c: not where another actor is going, unless it is a grid
        // object's ("evt_obj_").
        for (const auto& b : actors_) {
            if (b && b.get() != &a && b->dest == to) {
                if (!prefix(b->name, "evt_obj_")) {
                    return false;
                }
                break;
            }
        }
        break;
    case 3: {
        // 0x346f0c, a mummy (+0x2e4 set while it checks): unless planning
        // a path (+0x2e1), not where another mummy is going when Layton's
        // goal lies on that mummy's row or column, nor unless his goal
        // lies on this one's; and a mummy standing there must be able to
        // move on (and not be checking itself).
        Actor& m = const_cast<Actor&>(a);
        m.checking = true;
        bool ok = true;
        if (!a.planning) {
            Actor* other = nullptr;
            for (const auto& b : actors_) {
                if (b && b.get() != &a && b->dest == to) {
                    other = b.get();
                    break;
                }
            }
            if (other && prefix(other->name, kMummy)) {
                Actor* l = actor(kLayton);
                const CellIndex oc = cellOf(*other), mc = cellOf(a);
                if (!l || l->dest.row == oc.row || l->dest.column == oc.column ||
                    (l->dest.row != mc.row && l->dest.column != mc.column)) {
                    ok = false;
                }
            }
            if (ok) {
                if (Actor* b = actorAt(to, &a); b && prefix(b->name, kMummy)) {
                    float h2 = 0.0f;
                    ok = !b->checking && canStep(*b, cellOf(*b), b->dest, h2);
                }
            }
        }
        m.checking = false;
        if (!ok) {
            return false;
        }
        break;
    }
    default:
        break;
    }
    height = hTo;
    return true;
}

bool RuinsLoop::baseUpdate(Actor& a) {
    // 0x2936f4, the classes' common update; true when the command is done
    // (the action goes to -1 and the tick runs again).
    adventure56::Object3D* o = game_.scene.object3D(a.hash);
    if (!o) {
        return false;
    }
    const auto moving = [o] { return o->position.moving() || o->offset.moving(); }; // 0x2bec64
    switch (a.action) {
    case 0: // rest: the resting motion, the cells to where it stands
        playMotion(*o, a.standMotion);
        a.dest = cellOf(a);
        a.last = a.dest;
        break;
    case 1:
    case 2: { // a step (2: along the path)
        if (moving()) {
            return false;
        }
        if (a.began) {
            const CellIndex to = a.action == 2 && a.pathIndex < a.path.size() ? a.path[a.pathIndex]
                                                                               : a.active.target;
            const CellIndex from = cellOf(a);
            if (!(to == from)) {
                const auto at = o->position.value();
                std::array<float, 3> goal{float(to.column) * kCellSize, 0.0f, float(to.row) * kCellSize};
                face(*o, {goal[0] - at[0], 0.0f, goal[2] - at[2]}, a.active.faceFrames);
                float h = 0.0f;
                if (canStep(a, from, to, h)) {
                    a.last = cellOf(a);
                    goal[1] = h;
                    o->position.start(goal, a.active.moveFrames, 0); // 0x2c9eac
                    playMotion(*o, a.moveMotion);
                    if (a.action == 2) {
                        o->motionSpeed = 1.5f; // 0x18f904
                    }
                    if (a.klass == 1) {
                        somethingHappened_ = true; // +0x628
                    }
                    return false;
                }
            }
        }
        o->motionSpeed = 1.0f;
        if (a.action == 2) {
            ++a.pathIndex;
            if (a.pathIndex < a.path.size()) {
                a.pending = a.active;
                a.state = 2;
                a.settled = false;
                a.target = a.path[a.pathIndex];
                a.dest = a.path[a.pathIndex];
            }
        } else if (a.active.onward && a.pathIndex + 1 < a.path.size()) {
            ++a.pathIndex;
        }
        if (a.state == -1 && !a.idle) {
            a.state = 0;
            a.settled = true;
            a.target = a.dest;
        }
        break;
    }
    case 3: // face a direction
        if (moving()) {
            return false;
        }
        if (a.began) {
            face(*o, a.active.facing, a.active.faceFrames);
            return false;
        }
        break;
    case 4: // turn by an angle
        if (moving()) {
            return false;
        }
        if (a.began) {
            const float y = adventure56::wrapAngle(o->rotation3[1] + a.active.turn);
            turnTo(*o, {0.0f, y, 0.0f}, a.active.faceFrames); // 0x26303c
            return false;
        }
        break;
    default:
        return false;
    }
    a.action = -1;
    return true;
}

bool RuinsLoop::classUpdate(Actor& a) {
    // vtable +0x10 per class (vtables 0x403e98 ..): 0, 2 (0x1c94ac) and 9
    // the common update; Layton (0x1cc344) first picks his motions by the
    // shovel (遺跡スコップあり).
    switch (a.klass) {
    case 0:
    case 2:
    case 9:
        return baseUpdate(a);
    case 1: {
        const bool shovel = game_.variable("\x88\xe2\x90\xd5\x83\x58\x83\x52\x83\x62\x83\x76\x82\xa0\x82\xe8") != 0;
        a.standMotion = shovel ? "\x97\xa7\x82\xbf"                                 // 立ち
                               : "\x83\x58\x83\x52\x83\x62\x83\x76\x82\xc8\x82\xb5\x97\xa7\x82\xbf"; // スコップなし立ち
        a.moveMotion = shovel ? "\x91\x96\x82\xe8"                                  // 走り
                              : "\x83\x58\x83\x52\x83\x62\x83\x76\x82\xc8\x82\xb5\x91\x96\x82\xe8"; // スコップなし走り
        if (baseUpdate(a)) {
            return true;
        }
        return laytonAction(a);
    }
    case 3:
        return mummyUpdate(a);
    case 4:
        return blockUpdate(a);
    case 5:
    case 6:
    case 7:
    case 8:
        return switchUpdate(a);
    default:
        // Mummies (0x1cac54) and the floor's objects (0x1c9648, 0x1ca6dc,
        // 0x1c8100, 0x1cb6dc, 0x1cb890): not ported yet.
        ++game_.missing[2120000 + a.klass];
        return false;
    }
}

// ---------------------------------------------------------------- Layton's actions and blocks

// The motion names of Layton's actions (0x1cc344's lazily hashed table).
const std::string kPushMotion = "\x89\x9f\x82\xb5";                  // 押し
const std::string kCannotPushMotion = "\x89\x9f\x82\xb9\x82\xc8\x82\xa2"; // 押せない

// 0x2c1128 with flag 8: a motion played once (it holds its last frame and
// sets the layer's end bit, 0x3403f0). Its blend frames are not modelled.
void playOnce(adventure56::Object3D& o, const std::string& name) {
    o.motion = name;
    o.motionTime = 0;
    o.motionOnce = true;
    o.motionDone = false;
}

// 0x2bc954 -> 0x340958: how far the layer playing the motion is, from 0 to
// its length (0 when it does not play it). Hypothesis: the layer's span is
// the clip's frame count.
float motionProgress(const adventure56::Object3D& o, const std::string& name) {
    if (!o.model || o.motion != name) {
        return 0.0f;
    }
    const adventure56::Motion* m = o.model->motion(name);
    if (!m) {
        return 0.0f;
    }
    return o.motionTime / float(std::max(1, m->last - m->first + 1));
}

bool RuinsLoop::finish(Actor& a) {
    // 0x1cdf88 / 0x1ca630: the command ends; without a pending one the
    // actor settles.
    if (a.state == -1 && !a.idle) {
        a.state = 0;
        a.settled = true;
        a.target = a.dest;
    }
    a.action = -1;
    return true;
}

bool RuinsLoop::laytonAction(Actor& a) {
    // 0x1cc344 after the common update: actions 5 (dig / bury), 6 (push),
    // 7 (the lever), 8 (caught).
    adventure56::Object3D* o = game_.scene.object3D(a.hash);
    if (!o) {
        return false;
    }
    switch (a.action) {
    case 6: {
        if (a.began) {
            // The block on the target cell (an object of event kind 200
            // with an actor): "押し" when it can move on beyond, else
            // "押せない" (the push failed).
            const GridObject* g = objectAt(a.active.target, 0);
            const EventDef* e = g && g->event >= 0 ? &floor_.events[std::size_t(g->event)] : nullptr;
            Actor* block = e && e->kind == 200 ? actorOf(g) : nullptr;
            if (!block) {
                return finish(a);
            }
            const CellIndex at = cellOf(a);
            const CellIndex beyond{2 * a.active.target.row - at.row, 2 * a.active.target.column - at.column};
            float h = 0.0f;
            if (canStep(*block, a.active.target, beyond, h)) {
                playOnce(*o, kPushMotion);
            } else {
                playOnce(*o, kCannotPushMotion);
                a.pushFailed = true;
            }
            a.pushing = block;
            return false;
        }
        if (o->motionDone && (!a.pushing || a.pushing->action == -1 || a.pushing->action == 0)) {
            a.pushing = nullptr;
            a.pushStarted = false;
            if (!a.pushFailed) {
                somethingHappened_ = true; // +0x628
            }
            a.pushFailed = false;
            place(a, cellOf(a));
            return finish(a);
        }
        if (a.pushStarted || !a.pushing) {
            return false;
        }
        // At 0.45 of "押し" a resting block takes the push (state 9) in
        // Layton's direction.
        Actor* block = a.pushing;
        if (motionProgress(*o, kPushMotion) >= 0.45f && block->klass == 4 &&
            (block->action == -1 || block->action == 0)) {
            const CellIndex at = cellOf(a);
            const CellIndex d{a.active.target.row - at.row, a.active.target.column - at.column};
            if (!block->idle && block->action != 9) {
                const CellIndex b = cellOf(*block);
                block->state = 9;
                block->settled = false;
                block->target = b;
                block->dest = {b.row + d.row, b.column + d.column};
                block->pending.step = d;
                block->steps = 0;
                block->pushedBy = &a;
            }
            a.pushStarted = true;
        }
        return false;
    }
    case 5:
        return digAction(a, *o);
    case 7: {
        // The lever: a lever (event kind 300, class 6) on the cell ahead
        // gets its pull (state 14, woken: +0x1c); "レバースイッチ操作".
        if (a.began) {
            const GridObject* g = objectAt(a.active.target, 0);
            const EventDef* e = g && g->event >= 0 ? &floor_.events[std::size_t(g->event)] : nullptr;
            Actor* lever = e && e->kind == 300 ? actorOf(g) : nullptr;
            if (lever && lever->klass == 6 && !lever->idle) {
                const CellIndex at = cellOf(*lever);
                lever->state = 14;
                lever->settled = false;
                lever->target = at;
                lever->dest = at;
                lever->dormant = false;
            }
            playOnce(*o, "\x83\x8c\x83\x6f\x81\x5b\x83\x58\x83\x43\x83\x62\x83\x60\x91\x80\x8d\xec"); // レバースイッチ操作
            return false;
        }
        if (!o->motionDone) {
            return false;
        }
        somethingHappened_ = true;
        place(a, cellOf(a));
        return finish(a);
    }
    case 8: {
        // Caught (612): "失敗"; each mummy running at him (25) turns him its
        // way (5 frames) and goes back to its last cell (16 frames); one
        // that has caught before (+0x2d8 above 1) and faces his way within
        // a quarter turn keeps him where he is, else he goes back too.
        playMotion(*o, "\x8e\xb8\x94\x73"); // 失敗
        bool held = false;
        for (const auto& b : actors_) {
            if (!b || !prefix(b->name, kMummy)) {
                continue;
            }
            auto* m = game_.scene.object3D(b->hash);
            if (b->action == 25 && m) {
                if (b->klass == 3 && b->mummyValue > 1 &&
                    std::fabs(m->rotation3[1] - o->rotation3[1]) < 1.57079637f) {
                    held = true;
                }
                face(*o,
                     {m->position[0] + m->offset[0] - (o->position[0] + o->offset[0]), 0.0f,
                      m->position[2] + m->offset[2] - (o->position[2] + o->offset[2])},
                     5.0f);
            }
            place(*b, b->last, 16.0f);
        }
        place(a, held ? cellOf(a) : a.last, 16.0f);
        return false;
    }
    default:
        return false;
    }
}

// Layton's dig motions (0x1cc344's table).
const std::string kDigMotion = "\x83\x58\x83\x52\x83\x62\x83\x76\x8c\x40\x82\xe9";         // スコップ掘る
const std::string kCannotDigMotion = "\x83\x58\x83\x52\x83\x62\x83\x76\x8c\x40\x82\xea\x82\xc8\x82\xa2"; // スコップ掘れない
const std::string kBuryMotion = "\x83\x58\x83\x52\x83\x62\x83\x76\x8c\x8a\x96\x84\x82\xdf\x82\xe9"; // スコップ穴埋める
const std::string kHitMotion = "\x83\x71\x83\x62\x83\x67";                                  // ヒット

std::string RuinsLoop::spawnEffect(const std::string& name, const std::string& resource, const std::string& motion,
                                   const std::string& materialMotion, const std::array<float, 3>& at,
                                   const std::array<float, 3>& turn) {
    // 0x293574 / 0x29345c: an effect object from the pool (+0x4c3f0, 140):
    // a 3D object of the name with the model resource (screen 0, priority
    // 0x6e), shown at the place and turn, its motion played once and its
    // material motion (0x18d864) once.
    adventure56::Resource* r = game_.scene.resource(resource);
    if (!r || !r->model) {
        return {};
    }
    auto& o = game_.scene.create3D(name, 0, 0x6e, 2, actorLevel_);
    o.model = r->model;
    o.visible = true;
    o.drawn = true;
    o.position.set(at);
    o.rotation3.set({adventure56::wrapAngle(turn[0]), adventure56::wrapAngle(turn[1]),
                     adventure56::wrapAngle(turn[2])});
    if (!motion.empty()) {
        playOnce(o, motion);
    }
    if (!materialMotion.empty()) {
        o.materialMotion = materialMotion;
        o.materialMotionTime = 0;
        o.materialMotionOnce = true;
        o.materialMotionDone = false;
    }
    return name;
}

void RuinsLoop::dropEffect(std::string& name) {
    // The effect goes back to the pool (the list +0x4ca84, emptied by the
    // end of frame 0x1c59d0).
    if (!name.empty()) {
        game_.scene.remove(xq::hash(name));
        name.clear();
    }
}

bool RuinsLoop::digAction(Actor& a, adventure56::Object3D& o) {
    // 0x1cc344, action 5: dig the cell ahead (a diggable cell, flag 0 of
    // its model row: it becomes a hole, kind 300) or bury a hole (it goes
    // back to its own kind), both only onto a level cell (heights within
    // 2) with no actor but a treasure (class 9) on it; else the spade hits
    // stone ("スコップ掘れない", sparks).
    const auto at3 = std::array<float, 3>{o.position[0] + o.offset[0], o.position[1] + o.offset[1],
                                          o.position[2] + o.offset[2]};
    if (a.began) {
        const CellIndex cur = cellOf(a);
        const CellIndex to = a.active.target;
        Cell* toCell = floor_.cell(to);
        const PartDef* toPart = toCell ? floor_.part(toCell->kind) : nullptr;
        bool done = false;
        if (floor_.cellAt(cur) && toPart) {
            const PartDef* onCur = partOn(cur);
            const PartDef* onTo = partOn(to);
            const float hCur = surface(cur) + (onCur ? onCur->reals[3] : 0.0f);
            const float hTo = surface(to) + (onTo ? onTo->reals[3] : 0.0f);
            const bool level = std::fabs(hTo - hCur) <= 2.0f;
            Actor* there = actorAt(to, nullptr);
            const bool clear = !there || there->klass == 9;
            const bool ok = level && clear;
            const std::array<float, 3> place{at3[0], toCell->y, at3[2]};
            const auto turn = o.rotation3.value();
            if (toPart->id == 300 && ok) {
                // Bury: "bary" (r308, its motion and "r308_002").
                a.effect = spawnEffect("bary", "res_eff_digged", kBuryMotion, "r308_002", place, turn);
                if (auto* e = game_.scene.object3D(xq::hash(a.effect))) {
                    e->motionSpeed = 1.0f;
                }
                playOnce(o, kBuryMotion);
                o.motionSpeed = 1.0f;
                done = true;
            } else if (toPart->flags[0] != 0 && ok) {
                // Dig: a treasure (event kind 500) there comes up; the
                // cell's own kind becomes plain diggable ground (200).
                a.treasureDug = false;
                const GridObject* g = objectAt(to, 0);
                if (g && g->event >= 0 && floor_.events[std::size_t(g->event)].kind == 500) {
                    a.treasure = int(g - floor_.objects.data());
                    toCell->originalKind = 200;
                    a.treasureDug = true;
                }
                a.effect = spawnEffect("dig", "res_eff_digged", kDigMotion, "r308_001", place, turn);
                playOnce(o, kDigMotion);
                done = true;
            }
            a.digCell = to;
        }
        if (done) {
            return false;
        }
        // The spade hits: sparks "hibana" (r311 when Layton faces along
        // a whole half turn, else r312) and "スコップ掘れない".
        const float turnY = o.rotation3[1];
        int degrees = int(turnY * 0.159154937f * 360.0f + 360.5f);
        degrees %= 360;
        const std::string sparks = degrees % 180 == 0 ? "res_eff_hibana_1" : "res_eff_hibana_2";
        a.effect = spawnEffect("hibana", sparks, kHitMotion, kHitMotion, at3, o.rotation3.value());
        playOnce(o, kCannotDigMotion);
        a.arrival = nullptr;
        a.pushFailed = true;
        return false;
    }
    if (!o.motionDone) {
        // The cell changes partway: a buried hole at 0.6 of "穴埋める", a
        // dug one at 0.55 of "掘る" (the treasure shows); the spade's
        // sounds in their windows.
        if (a.digCell.valid()) {
            Cell* cell = floor_.cell(a.digCell);
            if (motionProgress(o, kBuryMotion) >= 0.6f) {
                if (cell) {
                    if (const PartDef* p = floor_.part(cell->originalKind)) {
                        cell->kind = p->id;
                    }
                }
                a.digCell = {-1, -1};
            } else if (motionProgress(o, kDigMotion) >= 0.55f) {
                if (cell) {
                    cell->kind = 300;
                }
                a.digCell = {-1, -1};
                if (a.treasure >= 0) {
                    if (Actor* t = actorOf(&floor_.objects[std::size_t(a.treasure)])) {
                        if (auto* t3 = game_.scene.object3D(t->hash)) {
                            t3->drawn = true;
                        }
                    }
                }
            }
        }
        const auto inWindow = [](float v, float lo, float hi) { return v >= lo && v <= hi; };
        const auto once = [this](int effect) {
            if (!game_.effectPlaying(effect)) {
                game_.playEffect(effect);
            }
        };
        if (a.digCell.valid() && inWindow(motionProgress(o, kBuryMotion), 0.15f, 0.3f)) {
            once(0x14e);
        }
        if (a.digCell.valid() && inWindow(motionProgress(o, kDigMotion), 0.26f, 0.6f)) {
            once(0x14d);
        }
        if (inWindow(motionProgress(o, kCannotDigMotion), 0.35f, 0.6f)) {
            once(0x14f);
        }
        return false;
    }
    // The motion ended. A treasure that came up pops up over the dug
    // cell (once): "!" for a puzzle (its event names one), the item for an
    // item (its ruins flag is set: ＜遺跡取得＞ + the item's place after
    // ＜アイテム＞), else a coin.
    auto* excl = game_.scene.object(std::string("plc_obj_excl"));
    auto* item = game_.scene.object(std::string("plc_obj_item"));
    auto* coin = game_.scene.object(std::string("plc_obj_coin"));
    bool puzzle = false;
    if (a.treasureDug && a.treasure >= 0) {
        const GridObject& g = floor_.objects[std::size_t(a.treasure)];
        const EventDef* e = g.event >= 0 ? &floor_.events[std::size_t(g.event)] : nullptr;
        const EventRecord* rec = e ? game_.story.event(e->eventB) : nullptr;
        if (rec) {
            const auto p = game_.projectToScreen(0, float(a.active.target.column) * kCellSize, at3[1],
                                                 float(a.active.target.row) * kCellSize);
            const float x = 200.0f + (p[0] * 400.0f - 200.0f) * 1.1f;
            const float y = p[1] * 240.0f;
            const auto pop = [this, x, y](adventure56::Object* w, const char* animation, int sound, float rise,
                                          float frames, float lift) {
                if (!w) {
                    return;
                }
                game_.playEffect(sound);
                w->visible = true;
                w->position.set({x, y, 0.0f});
                w->offset.set({0.0f, 0.0f, 0.0f});
                w->position.start({x, y + rise, 0.0f}, frames, 5);
                w->offset.start({0.0f, lift, 0.0f}, 24.0f, 1);
                game_.playAnimation(*w, Value::ofText(animation), 0xc);
            };
            if (!rec->puzzle.empty() && game_.defs.puzzleByName(rec->puzzle)) {
                pop(excl, "excl", 100, -50.0f, 24.0f, -48.0f);
                puzzle = true;
            } else if (const int flag = game_.flagId(rec->note); flag >= 0) {
                pop(item, "item", 0x7a, -55.0f, 32.0f, -16.0f);
                const int first = game_.flagId("\x81\x83\x83\x41\x83\x43\x83\x65\x83\x80\x81\x84"); // ＜アイテム＞
                const int ruins = game_.flagId("\x81\x83\x88\xe2\x90\xd5\x8e\xe6\x93\xbe\x81\x84"); // ＜遺跡取得＞
                if (first >= 0 && ruins >= 0) {
                    game_.setFlag(ruins + flag - first, true);
                }
            } else {
                pop(coin, "coin", 0x66, -55.0f, 32.0f, -16.0f);
            }
        }
    }
    // Done when no pop-up shows, or a shown one has risen and played
    // through (0x2becb4 / 0x340030).
    bool shown = false, settled = false;
    for (adventure56::Object* w : {excl, item, coin}) {
        if (w && w->visible) {
            shown = true;
            if (!w->position.moving() && !w->offset.moving() && (w->passes > 0 || w->finished)) {
                settled = true;
            }
        }
    }
    const bool finished = !shown || settled;
    if (finished) {
        if (!a.pushFailed) {
            somethingHappened_ = true;
        }
        a.pushFailed = false;
        place(a, cellOf(a));
        for (adventure56::Object* w : {excl, item, coin}) {
            if (w) {
                w->visible = false;
            }
        }
    }
    // The effect goes; the treasure is noted in the floor's state (unless
    // a puzzle showed), its actor and object go.
    dropEffect(a.effect);
    if (a.treasure >= 0) {
        GridObject& g = floor_.objects[std::size_t(a.treasure)];
        if (!puzzle && g.event >= 0) {
            noteEvent(floor_.events[std::size_t(g.event)], 1);
        }
        if (Actor* t = actorOf(&g)) {
            removeActor(*t);
        }
        g.actor = -1;
        g.name.clear();
    }
    a.treasureDug = false;
    a.treasure = -1;
    return finished ? finish(a) : false;
}

void RuinsLoop::noteEvent(const EventDef& e, int value) {
    // 0x1d3960: the floor's note of the event (its number) in the working
    // record: the value set, or a new note.
    const unsigned f = unsigned(floorNumber_ - 1) & 0xff;
    if (f < 8) {
        record_.eventNotes[f][e.number & 0xff] = value;
    }
}

void RuinsLoop::removeActor(Actor& a) {
    // Onto the removal list (+0x27b8, 70): the end of frame frees it.
    if (std::find(removals_.begin(), removals_.end(), &a) == removals_.end()) {
        removals_.push_back(&a);
    }
}

void RuinsLoop::freeRemovals() {
    // 0x1c59d0: each listed actor resets (0x294388: its object goes) and
    // its slot frees.
    for (Actor* a : removals_) {
        for (auto& slot : actors_) {
            if (slot.get() == a) {
                game_.scene.remove(a->hash);
                slot.reset();
            }
        }
    }
    removals_.clear();
}

bool RuinsLoop::switchUpdate(Actor& a) {
    // The floor's switches and levers. Classes 6 (a lever, 0x1c8100) and
    // 5 (a cracking plate, 0x1ca6dc) start from the common update; 7 (a
    // floor switch, 0x1cb6dc) and 8 (a paired floor switch, 0x1cb890) look
    // at who stands on them first.
    adventure56::Object3D* o = game_.scene.object3D(a.hash);
    const auto standing = [this, &a](bool blocksToo) {
        const CellIndex at = cellOf(a);
        for (const auto& b : actors_) {
            if (b && (b->klass == 1 || b->klass == 2 || (blocksToo && b->klass == 4)) && cellOf(*b) == at) {
                return true;
            }
        }
        return false;
    };
    const auto request = [this, &a](int kind) {
        const CellIndex at = cellOf(a);
        a.state = kind;
        a.settled = false;
        a.target = at;
        a.dest = at;
    };
    switch (a.klass) {
    case 5: {
        if (baseUpdate(a)) {
            return true;
        }
        if (!o || a.action != 13) {
            return false;
        }
        if (a.began) {
            // Pressed by a block: "001_ダメージ", effect 0x163.
            const GridObject* g = objectAt(cellOf(a), 0);
            a.treasure = g ? int(g - floor_.objects.data()) : -1;
            if (g) {
                a.factor = 0.0f;
                playOnce(*o, "001_\x83\x5f\x83\x81\x81\x5b\x83\x57");
                game_.playEffect(0x163);
                return false;
            }
            return finish(a);
        }
        if (!o->motionDone) {
            return false;
        }
        // Broken: the plate's actor and object go.
        if (a.treasure >= 0) {
            GridObject& g = floor_.objects[std::size_t(a.treasure)];
            g.actor = -1;
            g.name.clear();
            a.treasure = -1;
            a.action = -1;
            removeActor(a);
            return true;
        }
        return finish(a);
    }
    case 6:
        if (baseUpdate(a)) {
            return true;
        }
        if (!o || a.action != 14) {
            return false;
        }
        if (a.began && !a.pressed) {
            playOnce(*o, "move");
            return false;
        }
        if (!o->motionDone) {
            // The lever's sound at 0.365..0.6 of "move".
            const float t = motionProgress(*o, "move");
            if (t >= 0.365f && t <= 0.6f && !game_.effectPlaying(0x14c)) {
                game_.playEffect(0x14c);
            }
            return false;
        }
        a.pressed = true;
        playOnce(*o, "end");
        a.action = -1;
        return true;
    case 7: {
        if (standing(false) && !a.pressed && !a.idle) {
            request(16);
        }
        if (!o || a.action != 16) {
            return false;
        }
        if (!a.began || a.pressed) {
            a.pressed = true;
            playOnce(*o, "end");
            a.action = -1;
            return true;
        }
        playOnce(*o, "move");
        game_.playEffect(0x159);
        return false;
    }
    case 8: {
        if (!a.linked) {
            const bool on = standing(true);
            if (!on && a.pressed && !a.idle) {
                request(19);
            } else if (on && !a.pressed && !a.idle) {
                request(18);
            }
        }
        if (!o) {
            return false;
        }
        if (a.action == 19) {
            if (a.began && a.pressed) {
                playOnce(*o, "start");
                return false;
            }
            a.pressed = false;
            playOnce(*o, "start");
            a.action = -1;
            return true;
        }
        if (a.action != 18) {
            return false;
        }
        if (a.began && !a.pressed) {
            playOnce(*o, "move");
            game_.playEffect(0x159);
            return false;
        }
        // Down: with its pair (the switch numbered one apart: an even one
        // pairs with the one below, an odd one with the one above) up,
        // the odd switch's event (its own, or the pair's) plays; with both
        // down, the even one's, and the pair is done.
        a.pressed = true;
        playOnce(*o, "end");
        const EventDef* mine = a.arrival;
        a.arrival = nullptr;
        a.action = -1;
        if (a.pairNumber == -1 || eventBusy()) {
            return true;
        }
        const int want = (a.pairNumber & 1) == 0 ? a.pairNumber - 1 : a.pairNumber + 1;
        Actor* partner = nullptr;
        for (const auto& b : actors_) {
            if (b && b.get() != &a && b->klass == 8 && b->pairNumber == want) {
                partner = b.get();
            }
        }
        if (!partner) {
            return true;
        }
        const EventDef* theirs = nullptr;
        for (const auto& e : floor_.events) {
            if (e.kind == 302 && e.number == (partner->pairNumber & 0xff)) {
                theirs = &e;
                break;
            }
        }
        // The odd-numbered switch's event with the pair up, the even one's
        // with both down.
        const EventDef* even = (a.pairNumber & 1) == 0 ? mine : theirs;
        const EventDef* odd = (a.pairNumber & 1) == 0 ? theirs : mine;
        const auto settleBoth = [this] {
            for (const std::string& n : {kLayton, kRandall}) {
                if (Actor* b = actor(n); b && !b->idle) {
                    b->state = 0;
                    b->settled = true;
                    b->target = b->dest;
                }
            }
        };
        if (!partner->pressed) {
            if (odd && holds(*odd)) {
                settleBoth();
                a.arrival = odd;
            }
        } else if (even && holds(*even)) {
            settleBoth();
            a.linked = true;
            partner->linked = true;
            a.arrival = even;
        }
        return true;
    }
    default:
        return false;
    }
}

std::vector<CellIndex> RuinsLoop::findPath(Actor& a, CellIndex from, CellIndex to, int radius, bool extend,
                                           bool& found) {
    // 0x2c4ecc: a best-first search from `to` back to `from` inside the
    // square of `radius` around `to` (scores in the grid's search table,
    // +0x3da58: g the steps from `to`, h to `from`, each plus one per axis
    // that differs; the open cell of least g + h in row then column order
    // expands to its four neighbours the actor can step between); the path
    // is read from `from` along the parents to `to` (at most `radius`
    // cells), with `extend` one cell more beyond `to`.
    found = false;
    std::vector<CellIndex> path;
    if (from == to) {
        found = true;
        return path;
    }
    const int r0 = std::max(0, to.row - radius), r1 = std::min(kRows - 1, to.row + radius);
    const int c0 = std::max(0, to.column - radius), c1 = std::min(kColumns - 1, to.column + radius);
    struct Node {
        int g = 0, h = 0;
        CellIndex parent{0, 0};
        int state = 0; // 0 none, 1 open, 2 closed
    };
    std::map<std::pair<int, int>, Node> table;
    const auto node = [&table](CellIndex c) -> Node& { return table[{c.row, c.column}]; };
    for (int r = r0; r <= r1; ++r) {
        for (int c = c0; c <= c1; ++c) {
            Node& n = node({r, c});
            n = Node{};
            if (!floor_.part(floor_.cells[std::size_t(r)][std::size_t(c)].kind)) {
                n.state = 2;
            }
        }
    }
    node(to).state = 1;
    node(to).g = 0xff;
    node(to).h = 0xff;
    const auto score = [](CellIndex p, CellIndex q) {
        return std::abs(p.row - q.row) + std::abs(p.column - q.column) + (p.row != q.row ? 1 : 0) +
               (p.column != q.column ? 1 : 0);
    };
    while (true) {
        int best = 0xffff;
        CellIndex cur{-1, -1};
        for (int r = r0; r <= r1; ++r) {
            for (int c = c0; c <= c1; ++c) {
                const Node& n = node({r, c});
                const int f = (n.g + n.h) & 0xffff;
                if (n.state == 1 && f < best) {
                    best = f;
                    cur = {r, c};
                }
            }
        }
        if (!cur.valid()) {
            break;
        }
        static constexpr int kSteps[4][2] = {{-1, 0}, {0, -1}, {0, 1}, {1, 0}};
        for (const auto& d : kSteps) {
            const CellIndex nb{cur.row + d[0], cur.column + d[1]};
            if (nb.row < r0 || nb.row > r1 || nb.column < c0 || nb.column > c1) {
                continue;
            }
            Node& n = node(nb);
            if (n.state == 2) {
                continue;
            }
            float h = 0.0f;
            if (canStep(a, cur, nb, h)) {
                n.g = score(nb, to);
                n.h = score(nb, from);
                n.parent = cur;
                n.state = 1;
            }
        }
        node(cur).state = 2;
        if (cur == from) {
            break;
        }
    }
    CellIndex at = from, before = from;
    while (true) {
        if (int(path.size()) >= radius || node(at).state == 0) {
            return path;
        }
        before = at;
        at = node(at).parent;
        path.push_back(at);
        if (at == to) {
            break;
        }
    }
    if (extend) {
        const CellIndex beyond{2 * at.row - before.row, 2 * at.column - before.column};
        if (beyond.row >= r0 && beyond.row <= r1 && beyond.column >= c0 && beyond.column <= c1) {
            path.push_back(beyond);
        }
    }
    found = true;
    return path;
}

int RuinsLoop::lookAlong(const Actor& a, CellIndex step, int most) {
    // 0x29303c: cells along a direction while what stands there stays
    // below the actor's eyes (its height plus 8).
    const auto* o = game_.scene.object3D(a.hash);
    const float eyes = (o ? o->position[1] + o->offset[1] : 0.0f) + 8.0f;
    CellIndex at = cellOf(a);
    int n = 0;
    while (n < most) {
        at = {at.row + step.row, at.column + step.column};
        const Cell* cell = floor_.cellAt(at);
        const PartDef* on = partOn(at);
        const float h = surface(at) + (on ? on->reals[3] : 0.0f);
        if (!cell || cell->kind == -1 || !(eyes > h)) {
            break;
        }
        ++n;
    }
    return n;
}

bool RuinsLoop::sees(Actor& m, CellIndex target) {
    // 0x1ca908 (518): a mummy sees the cell along its row or column within
    // its sight that way (turned with the mummy: 90, 180, 270 degrees
    // rotate the four values); before it noticed Layton the walls narrow
    // them (0x29303c each way).
    const CellIndex at = cellOf(m);
    const int dRow = target.row - at.row, dCol = target.column - at.column;
    int east = m.sight[0], west = m.sight[1], south = m.sight[2], north = m.sight[3];
    float turn = 0.0f;
    if (auto* o = game_.scene.object3D(m.hash)) {
        turn = o->rotation3[1];
    }
    const int degrees = int(turn * 0.159154937f * 360.0f + 360.5f) % 360;
    if (degrees == 90) {
        north = m.sight[1], west = m.sight[2], east = m.sight[3], south = m.sight[0];
    } else if (degrees == 180) {
        north = m.sight[2], south = m.sight[3], east = m.sight[1], west = m.sight[0];
    } else if (degrees == 270) {
        north = m.sight[0], south = m.sight[1], east = m.sight[3], west = m.sight[2];
    }
    if (!m.noticed) {
        east = lookAlong(m, {1, 0}, east);
        west = lookAlong(m, {-1, 0}, west);
        south = lookAlong(m, {0, 1}, south);
        north = lookAlong(m, {0, -1}, north);
    }
    if (at.row == target.row) {
        return -north <= dCol && dCol <= south;
    }
    if (at.column == target.column) {
        return -east <= -dRow && (dRow == -west || -dRow < west);
    }
    return false;
}

bool RuinsLoop::mummyUpdate(Actor& a) {
    // 0x1cac54, a mummy: after a step onto a hole (kind 300) it falls in
    // (21, idle); then its own states: 20 crushed (つぶれる, gone), 21 in the
    // hole (穴に落ちる, smoke "r309_001" from 0.4 on, the hole fills: kind 304,
    // 303 for +0x2c0; gone), 22 notices (気づく, a "_notice" mark), 23 looks
    // around (探す), 24 appears (出現), 25 runs at Layton (走り, turned to him).
    if (baseUpdate(a)) {
        const Cell* cell = floor_.cellAt(cellOf(a));
        if (cell && cell->kind == 300) {
            const CellIndex at = cellOf(a);
            a.state = 21;
            a.settled = true;
            a.target = at;
            a.dest = at;
            a.idle = true;
        }
        return true;
    }
    adventure56::Object3D* o = game_.scene.object3D(a.hash);
    if (!o) {
        return false;
    }
    bool gone = false, done = false;
    switch (a.action) {
    case 20:
        if (a.began) {
            playOnce(*o, "\x82\xc2\x82\xd4\x82\xea\x82\xe9"); // つぶれる
            game_.playEffect(0x153);
            return false;
        }
        if (!o->motionDone) {
            return false;
        }
        gone = true;
        break;
    case 21: {
        const std::string fall = "\x8c\x8a\x82\xc9\x97\x8e\x82\xbf\x82\xe9"; // 穴に落ちる
        if (a.began) {
            playOnce(*o, fall);
            const auto at = o->position.value();
            a.effect = spawnEffect("smoke", "res_eff_kemuri", "", "", at, {0.0f, 0.0f, 0.0f});
            a.factor = 0.0f;
            game_.playEffect(0x152);
            return false;
        }
        if (!o->motionDone) {
            if (a.effect.empty() || motionProgress(*o, fall) < 0.4f) {
                return false;
            }
            if (auto* e = game_.scene.object3D(xq::hash(a.effect))) {
                e->position.set({o->position[0] + o->offset[0], o->position[1] + o->offset[1] + 3.0f,
                                 o->position[2] + o->offset[2]});
                e->rotation3.set({-1.57079637f, 0.0f, 0.0f});
                playOnce(*e, "r309_001");
                e->materialMotion = "r309_001";
                e->materialMotionTime = 0;
                e->materialMotionOnce = true;
                e->materialMotionDone = false;
            }
            if (Cell* cell = floor_.cell(cellOf(a))) {
                cell->kind = a.fillsHigh ? 303 : 304;
            }
            return false;
        }
        dropEffect(a.effect);
        gone = true;
        break;
    }
    case 22:
        if (o->position.moving() || o->offset.moving()) {
            return false;
        }
        if (a.began) {
            face(*o, a.active.facing, a.active.faceFrames);
            playOnce(*o, "\x8b\x43\x82\xc3\x82\xad"); // 気づく
            a.effect = spawnEffect(a.name + "_notice", "r404", "001_\x97\xa7\x82\xbf", "",
                                   {o->position[0] + o->offset[0], o->position[1] + o->offset[1],
                                    o->position[2] + o->offset[2]},
                                   {0.0f, 0.0f, 0.0f});
            a.noticed = true;
            game_.playEffect(0x151);
            return false;
        }
        if (!o->motionDone) {
            return false;
        }
        dropEffect(a.effect);
        done = true;
        break;
    case 23:
        if (a.began) {
            playOnce(*o, "\x92\x54\x82\xb7"); // 探す
            a.noticed = false;
            game_.playEffect(0x165);
            return false;
        }
        if (!o->motionDone) {
            return false;
        }
        done = true;
        break;
    case 24:
        if (a.began) {
            playOnce(*o, "\x8f\x6f\x8c\xbb"); // 出現
            if (!a.idle) {
                a.state = 0;
                a.settled = true;
                a.target = a.dest;
            }
            if (!a.quietAppear && !game_.effectPlaying(0x160)) {
                game_.playEffect(0x160);
            }
            return false;
        }
        if (!o->motionDone) {
            return false;
        }
        done = true;
        break;
    case 25:
        playMotion(*o, "\x91\x96\x82\xe8"); // 走り
        if (Actor* l = actor(kLayton)) {
            if (auto* lo = game_.scene.object3D(l->hash)) {
                face(*o,
                     {lo->position[0] + lo->offset[0] - (o->position[0] + o->offset[0]),
                      lo->position[1] + lo->offset[1] - (o->position[1] + o->offset[1]),
                      lo->position[2] + lo->offset[2] - (o->position[2] + o->offset[2])},
                     5.0f);
            }
        }
        return false;
    default:
        return false;
    }
    if (gone) {
        removeActor(a);
        a.idle = false;
    }
    (void)done;
    return finish(a);
}

Actor* RuinsLoop::blockOf(int number) {
    // 0x1d39e0 / 0x15f404: the actor of the object whose event is kind 200
    // with the number.
    for (const auto& g : floor_.objects) {
        if (!g.name.empty() && g.event >= 0) {
            const EventDef& e = floor_.events[std::size_t(g.event)];
            if (e.kind == 200 && e.number == (number & 0xff)) {
                return actorOf(&g);
            }
        }
    }
    return nullptr;
}

void RuinsLoop::walkToGroup(Actor& a, CellIndex at, float moveFrames, float faceFrames) {
    // 0x1d3e00: not idle nor on a path (state / action 2) nor with path
    // steps left: the decoration (event kind 400) on the cell names a
    // group; the first other object of the group elsewhere is walked to
    // along a path (radius 50, one beyond), as a path walk (2).
    if (a.idle || a.state == 2 || a.action == 2 || a.pathIndex < a.path.size()) {
        return;
    }
    int group = -1;
    if (const GridObject* g = objectAt(at, 0)) {
        if (g->event < 0 || floor_.events[std::size_t(g->event)].kind != 400) {
            return;
        }
        group = floor_.events[std::size_t(g->event)].number;
    }
    for (const auto& g : floor_.objects) {
        if (g.name.empty() || g.event < 0) {
            continue;
        }
        const EventDef& e = floor_.events[std::size_t(g.event)];
        if (e.kind != 400 || e.number != group) {
            continue;
        }
        float x = g.x, z = g.z;
        if (Actor* b = actorOf(&g)) {
            if (auto* o = game_.scene.object3D(b->hash)) {
                x = o->position[0] + o->offset[0];
                z = o->position[2] + o->offset[2];
            }
        }
        const CellIndex there = cellAt(x, z);
        if (there == at) {
            continue;
        }
        bool found = false;
        a.path = findPath(a, at, there, 50, true, found);
        if (a.path.empty()) {
            return;
        }
        a.pathIndex = 0;
        a.state = 2;
        a.settled = false;
        a.target = a.path[0];
        a.dest = a.path[0];
        a.pending = Order{};
        a.pending.moveFrames = moveFrames;
        a.pending.faceFrames = faceFrames;
        a.pending.onward = true;
        return;
    }
}

bool RuinsLoop::blockUpdate(Actor& a) {
    // 0x1c9648, a block: the common update, then state 9 (pushed: a cell
    // per 12 frames until it stops), 10..12 (not ported yet).
    if (baseUpdate(a)) {
        return true;
    }
    adventure56::Object3D* o = game_.scene.object3D(a.hash);
    if (!o) {
        return false;
    }
    if (a.action == 10 || a.action == 11 || a.action == 12) {
        return blockReturn(a, *o);
    }
    if (a.action != 9) {
        return false;
    }
    bool stop = false;
    if (o->position.moving() || o->offset.moving()) {
        // Sliding on: after one step (three across) the camera follows the
        // block (slot 1, mode 5). The block's roll (a node turned by a
        // quarter per step, the loop's list +0x174) is not ported.
        const int n = a.active.step.column != 0 ? 3 : 1;
        if (a.steps >= n) {
            setCameraMode(1, 5, 0.0f, 0, &a, true);
        }
        return false;
    }
    if (a.began) {
        game_.playEffect(0x15d);
    }
    const CellIndex cur = cellOf(a);
    const CellIndex next{cur.row + a.active.step.row, cur.column + a.active.step.column};
    const auto pos = o->position.value();
    const float hCurSurface = surface(cur);
    const float hNextSurface = surface(next);
    const PartDef* onNext = partOn(next);
    const float nextObject = onNext ? onNext->reals[3] : 0.0f;
    const float hNext = hNextSurface + nextObject;
    // What holds it up: its cell's surface plus the highest block below it.
    float hCur = hCurSurface;
    if (const PartDef* under = partOn(cur, 1, pos[1] - 1.0f)) {
        hCur += under->reals[3];
    }
    const bool falling = hCur - pos[1] < -a.climb;
    bool pressed = false;
    if (!falling) {
        // A plate (event kind 201, a class 5 actor) ahead, lower than here
        // or the block already moving: pressed (state 13).
        const GridObject* g = objectAt(next, 0);
        const EventDef* e = g && g->event >= 0 ? &floor_.events[std::size_t(g->event)] : nullptr;
        Actor* plate = e && e->kind == 201 ? actorOf(g) : nullptr;
        if (plate && (hNextSurface + nextObject < hCurSurface || a.steps > 0) && plate->klass == 5) {
            if (!plate->idle) {
                const CellIndex at = cellOf(*plate);
                plate->state = 13;
                plate->settled = true;
                plate->target = at;
                plate->dest = at;
                plate->dormant = false;
            }
            pressed = true;
        }
    }
    bool move = true;
    const std::array<float, 3> goal{float(next.column) * kCellSize, pos[1], float(next.row) * kCellSize};
    Cell* curCell = floor_.cell(cur);
    Cell* nextCell = floor_.cell(next);
    const PartDef* curPart = curCell ? floor_.part(curCell->kind) : nullptr;
    const PartDef* nextPart = nextCell ? floor_.part(nextCell->kind) : nullptr;
    if (curPart && curPart->id == 300 && curCell) {
        // Leaving a hole: the cell's own kind comes back (+0x40).
        curCell->kind = curCell->originalKind;
    }
    if (curCell && curCell->kind == 1005) {
        // On kind 1005 it stops and sinks (state 10).
        stop = true;
        if (!a.idle && a.action != 10) {
            a.state = 10;
            a.settled = true;
            a.target = cur;
            a.dest = cur;
        }
    } else if (!(nextCell && nextCell->kind == 1005)) {
        if (falling) {
            // Dropping onto what holds it up, over 8 frames.
            o->position.start({pos[0], hCur, pos[2]}, 8.0f, 0);
            move = false;
        } else if (!nextPart) {
            stop = true;
        } else if (nextPart->flags[3] == 0 || !(hNext - pos[1] < a.climb)) {
            if (nextPart->id != 300) {
                stop = true;
            }
        } else if (!pressed && onNext && onNext->flags[3] == 0) {
            stop = true;
        }
    }
    if (move && !stop) {
        o->position.start(goal, 12.0f, 0);
        ++a.steps;
        // A mummy on the cell ahead is crushed (state 20).
        if (Actor* m = actorAt(next, nullptr); m && m->klass == 3) {
            const CellIndex at = cellOf(*m);
            m->state = 20;
            m->settled = true;
            m->target = at;
            m->dest = at;
            m->idle = true;
        }
    }
    if (!stop) {
        return false;
    }
    // It stops: the camera's slot 1 goes, the sliding sound fades (30
    // frames), an object of event kind 202 under it whose condition holds
    // plays its event as the block rests.
    setCameraMode(1, -1, 0.0f, 3, nullptr, true);
    game_.stopEffect(0x15d, 30);
    if (const GridObject* g = objectAt(cellOf(a), 2, 2, pos[1])) {
        if (g->event >= 0) {
            const EventDef& e = floor_.events[std::size_t(g->event)];
            if (e.kind == 202 && holds(e)) {
                a.arrival = &e;
            }
        }
    }
    a.steps = 0;
    a.pushedBy = nullptr;
    a.comesBack = true;
    return finish(a);
}

void RuinsLoop::dropBack(Actor& a, const std::array<float, 3>& from, const std::array<float, 3>& to) {
    // 0x293178: unless idle or already so, the actor drops from `from` to
    // `to` (state 11).
    if (a.idle || a.action == 11) {
        return;
    }
    const CellIndex at = cellOf(a);
    a.state = 11;
    a.settled = true;
    a.target = at;
    a.dest = at;
    a.dropFrom = from;
    a.dropTo = to;
}

bool RuinsLoop::blockReturn(Actor& a, adventure56::Object3D& o) {
    // 0x1c9648, states 10..12 of a block. 10: on a kind 1005 cell it sinks
    // 40 over 15 frames (curve 1, effect 0x164), fading out (5 frames) once
    // 8 down; 90 frames later it drops back onto its own cell (11, from 40
    // above). 11: input off meanwhile; from the drop place it falls over 18
    // frames fading in (15 frames, effect 0x162); a block on the cell is
    // crushed (12). 12: the "crush" effect (r314_001), then it too drops
    // back home.
    const bool moving = o.position.moving() || o.offset.moving();
    const std::array<float, 3> at{o.position[0] + o.offset[0], o.position[1] + o.offset[1],
                                  o.position[2] + o.offset[2]};
    const auto home = [this, &a]() -> const GridObject* {
        for (const auto& g : floor_.objects) {
            if (actorOf(&g) == &a) {
                return &g;
            }
        }
        return nullptr;
    };
    switch (a.action) {
    case 10:
        if (moving) {
            if (o.alpha.moving() || at[1] > a.sinkMark) {
                return false;
            }
            o.alpha.start({0.0f}, 5.0f, 0);
            return false;
        }
        if (a.began) {
            a.sinkMark = at[1] - 8.0f;
            o.position.start({at[0], at[1] - 40.0f, at[2]}, 15.0f, 1);
            game_.playEffect(0x164);
            a.wait = 90.0f;
            return false;
        }
        a.wait -= 1.0f;
        if (a.wait > 0.0f) {
            return false;
        }
        a.wait = 0.0f;
        if (a.comesBack) {
            if (const GridObject* g = objectAt(cellOf(a), 1)) {
                if (Actor* b = actorOf(g)) {
                    dropBack(*b, {g->x, g->y + 40.0f, g->z}, {g->x, g->y, g->z});
                }
            }
        }
        a.steps = 0;
        a.pushedBy = nullptr;
        a.comesBack = true;
        return finish(a);
    case 11:
        if (!moving) {
            if (a.began) {
                a.lockKept = game_.objectTouchLocked;
                game_.objectTouchLocked = true;
                o.position.set(a.dropFrom);
                o.position.start(a.dropTo, 18.0f, 1);
                o.alpha.start({1.0f}, 15.0f, 0);
                game_.playEffect(0x162);
                return false;
            }
            game_.objectTouchLocked = a.lockKept;
            if (a.pushing && a.pushing->action != -1 && a.pushing->action != 0) {
                return false;
            }
            a.pushing = nullptr;
            a.steps = 0;
            a.comesBack = true;
            return finish(a);
        }
        if (a.pushing) {
            return false;
        }
        // A block where it lands is crushed.
        if (const GridObject* g = objectAt(cellOf(a), 1, 0, 0.0f, &a)) {
            const EventDef* e = g->event >= 0 ? &floor_.events[std::size_t(g->event)] : nullptr;
            Actor* b = e && e->kind == 200 ? actorOf(g) : nullptr;
            if (b) {
                auto* bo = game_.scene.object3D(b->hash);
                const float by = bo ? bo->position[1] + bo->offset[1] : 0.0f;
                if (std::fabs(at[1] - by) < 16.0f) {
                    if (!b->idle && b->action != 12) {
                        const CellIndex c = cellOf(*b);
                        b->state = 12;
                        b->settled = true;
                        b->target = c;
                        b->dest = c;
                    }
                    a.pushing = b;
                }
            }
        }
        return false;
    case 12:
        if (moving) {
            return false;
        }
        if (a.began) {
            if (a.effect.empty()) {
                a.effect = spawnEffect("crush", "res_eff_rock_crush", "r314_001", "", at, {0.0f, 0.0f, 0.0f});
            }
            if (const GridObject* g = home()) {
                dropBack(a, {g->x, g->y + 40.0f, g->z}, {g->x, g->y, g->z});
            }
            return false;
        }
        if (!a.effect.empty()) {
            auto* e = game_.scene.object3D(xq::hash(a.effect));
            if (e && !e->motionDone) {
                return false;
            }
            dropEffect(a.effect);
        }
        a.steps = 0;
        a.comesBack = true;
        return finish(a);
    default:
        return false;
    }
}

void RuinsLoop::arrive(Actor& a) {
    // 0x1d411c after the update: the event of the cell the actor walked
    // onto plays (its second event, with its notice and effect) unless one
    // runs; while one does, the actor settles.
    const EventDef* e = a.arrival;
    queueEvent(e->eventB, e->notice != 0, e);
    if ((runner_.queued || runner_.playing) && !a.idle) {
        a.state = 0;
        a.settled = true;
        a.target = a.dest;
    }
    a.arrival = nullptr;
    a.arrivalObject = -1;
}

void RuinsLoop::tickActor(Actor& a) {
    // 0x1d411c, an actor's frame, repeated while its update finishes a
    // command (so the next one begins in the same frame).
    struct Trigger {
        int action, kind;
    };
    // 0x3ed1f4: the commands that, as they begin, trigger the target
    // cell's event of the kind.
    static constexpr Trigger kTriggers[] = {{1, 100}, {2, 100}, {5, 101}, {5, 500},
                                            {9, 200}, {14, 300}, {16, 301}, {18, 302}};
    bool again = false;
    do {
        a.began = false;
        // Randall waits while an event is queued; class 8 never waits.
        const bool held = a.klass == 8 || (a.klass == 2 && runner_.queued);
        if (held || a.settled || !eventBusy()) {
            beginPending(a);
            if (a.began && !a.arrival) {
                const GridObject* g = objectAt(a.target, 0);
                const EventDef* e = g && g->event >= 0 ? &floor_.events[std::size_t(g->event)] : nullptr;
                bool triggers = false;
                for (const auto& t : kTriggers) {
                    triggers = triggers || (e && t.action == a.action && t.kind == e->kind);
                }
                if (triggers && holds(*e) && guide_.b7 == 0) {
                    a.arrival = e;
                    a.arrivalObject = int(g - floor_.objects.data());
                    queueEvent(e->eventA, e->notice != 0, nullptr);
                }
            }
        }
        endEvent();
        if (!held && !a.beganSettled && !a.settled && eventBusy()) {
            return;
        }
        again = classUpdate(a);
        if (!a.deferred || guide_.bb != 0) {
            if ((a.action == -1 || a.action == 0) && !a.beganSettled && a.arrival) {
                if (guide_.bb == 0) {
                    arrive(a);
                } else {
                    a.deferred = true;
                }
            }
        } else if (a.arrival) {
            arrive(a);
            a.deferred = false;
        } else {
            a.deferred = false;
        }
    } while (again);
}

void RuinsLoop::runActors() {
    // 0x1c798c, while the actors run (+0x21f0): +0x628 cleared, every
    // actor not frozen or dormant ticks in slot order, then the stands go
    // into the working record (0x2c8378). The queued event of +0x220c is
    // not ported (docs/ruins.md).
    if (!actorsRun_) {
        return;
    }
    somethingHappened_ = false;
    // While the camera switches (and the slot asks), Layton and Randall
    // wait in their resting motions.
    const bool wait = (camera_.switching || camera_.settling) && camera_.freeze;
    Actor* pair[] = {actor(kLayton), actor(kRandall)};
    for (Actor* a : pair) {
        if (a) {
            a->frozen = wait;
        }
    }
    for (std::size_t i = 0; i < actors_.size(); ++i) {
        Actor* a = actors_[i].get();
        if (a && !a->frozen && !a->dormant) {
            tickActor(*a);
        }
    }
    // The event playing when the loop was suspended plays again, with the
    // flag ＜ナゾ＞あとでとけない as it was.
    if (!resumeEvent_.empty()) {
        if (const int later = game_.flagId(kCannotLater); later >= 0) {
            game_.setFlag(later, resumeEventFlag_);
        }
        resumeEventFlag_ = false;
        queueEvent(resumeEvent_, false, nullptr);
    }
    resumeEvent_.clear();
    if (camera_.switching || camera_.settling) {
        for (Actor* a : pair) {
            if (a) {
                if (auto* o = game_.scene.object3D(a->hash)) {
                    playMotion(*o, a->standMotion);
                }
            }
        }
    }
    storeStands(record_);
}

void RuinsLoop::buildWorld(const Game::RuinRecord& record) {
    // 0x1d2004. Every model row with a model gets an object of the model's
    // name (0x292aa8: screen 0, priority 0x6e, kind 2) at the origin, its
    // model from the parts archive (0x29a1b4 / 0x292ca0); the rooms' shells
    // (ids 100..199) are switched on (vtable +0x10, +0x14, +0x18), the
    // others off; the rows' motions play (0x18d72c) and their material
    // motions (0x18d90c).
    for (auto& p : floor_.parts) {
        if (p.model.empty()) {
            continue;
        }
        auto model = modelFromArchive(0, p.model);
        if (!model) {
            continue;
        }
        adventure56::Resource r;
        r.kind = adventure56::Resource::Kind::Model;
        r.path = model->path;
        r.model = model;
        p.resource = p.model;
        game_.scene.addResource(p.resource, std::move(r));
        auto& o = game_.scene.create3D(p.model, 0, 0x6e, 2);
        o.model = model;
        o.position.set({0.0f, 0.0f, 0.0f});
        const bool shell = unsigned(p.id - 100) < 100;
        o.touchOn = shell;
        o.switch14 = shell;
        o.drawn = shell;
        if (!p.motionA.empty()) {
            o.motion = p.motionA;
        }
        if (!p.motionB.empty()) {
            o.materialMotion = p.motionB;
        }
        p.object = p.model;
    }
    // The grid objects. By their event kind (the table 0x1d2408 {kind,
    // model id, actor class}) they get an actor of the object's name
    // showing that model, scaled by the row's first three reals (0x2c9ef4),
    // turned by the object's angle, at the object's place; first the
    // floor's record: a broken plate (201) or a dug treasure (500: a
    // kind 301 cell under it becomes plain ground, 200) is gone (its name
    // cleared); switches and levers take their pressed state ("end" or
    // "start"); a moved block (200) stands where it was left.
    struct KindModel {
        int kind, model, klass;
    };
    static constexpr KindModel kKinds[] = {{200, 400, 4}, {201, 901, 5}, {400, 320, 0}, {500, 900, 9},
                                           {300, 410, 6}, {301, 411, 7}, {302, 412, 8}};
    const unsigned f = unsigned(floorNumber_ - 1) & 0xff;
    const auto note = [f](const std::array<std::map<int, int>, 8>& lists, int number) -> const int* {
        if (f >= 8) {
            return nullptr;
        }
        const auto it = lists[f].find(number & 0xff);
        return it == lists[f].end() ? nullptr : &it->second;
    };
    for (std::size_t i = 0; i < floor_.objects.size(); ++i) {
        GridObject& g = floor_.objects[i];
        if (g.name.empty() || g.event < 0) {
            continue;
        }
        const EventDef& e = floor_.events[std::size_t(g.event)];
        int model = -1, klass = -1;
        for (const auto& k : kKinds) {
            if (k.kind == e.kind) {
                model = k.model;
                klass = k.klass;
            }
        }
        if (e.kind == 201) {
            if (const int* v = note(record.plateNotes, e.number); v && *v == 0) {
                g.actor = -1;
                g.name.clear();
                continue;
            }
        }
        if (e.kind == 500) {
            if (const int* v = note(record.eventNotes, e.number); v && *v != 0) {
                if (Cell* cell = floor_.cell(cellAt(g.x, g.z)); cell && cell->kind == 301) {
                    cell->kind = 200;
                    cell->originalKind = 200;
                }
                g.actor = -1;
                g.name.clear();
                continue;
            }
        }
        if (model == -1) {
            continue;
        }
        const PartDef* part = floor_.part(model);
        if (!part || part->resource.empty()) {
            continue;
        }
        Actor* a = newActor(klass);
        if (!a || !attach(*a, g.name, part->resource, model)) {
            continue;
        }
        g.actor = int(actors_.size()) - 1;
        g.actorModel = model;
        a->gridObject = int(i);
        auto* o = game_.scene.object3D(a->hash);
        if (o) {
            // The ruins draw their actors themselves (the draw pool of the
            // resume, 0x29738c): shown, but a treasure stays hidden until
            // it is dug up (hypothesis: the pool entry's bit 1).
            o->drawn = klass != 9;
            o->scale.set({part->reals[0], part->reals[1], part->reals[2]});
            o->rotation3.set({0.0f, g.angle, 0.0f});
            if (!part->motionA.empty()) {
                o->motion = part->motionA;
            }
            if (!part->motionB.empty()) {
                o->materialMotion = part->motionB;
            }
        }
        if (model == 412 || model == 411 || model == 410) {
            if (model == 412) {
                a->pairNumber = e.number;
            }
            const int* v = note(record.switchNotes, e.number);
            a->pressed = v && *v != 0;
            if (model == 412) {
                a->linked = v && *v != 0;
            }
            if (o) {
                playOnce(*o, a->pressed ? "end" : "start");
            }
        }
        std::array<float, 3> at{g.x, g.y, g.z};
        if (e.kind == 200 && f < 8) {
            if (const auto it = record.blockNotes[f].find(e.number & 0xff); it != record.blockNotes[f].end()) {
                const CellIndex c{it->second.row, it->second.column};
                at = {float(c.column) * kCellSize, surface(c), float(c.row) * kCellSize};
                if (it->second.sunk && klass == 4) {
                    // A block that was dropping back does so again
                    // (0x293178 from 40 above, faded out: 0x1a803c).
                    dropBack(*a, {g.x, g.y + 40.0f, g.z}, {g.x, g.y, g.z});
                    if (o) {
                        o->alpha.set({0.0f});
                    }
                }
            }
        }
        if (o) {
            o->position.set(at);
        }
    }
    // The shadow "丸影" (res_shadow), all switches off (+0x640 500, +0x664..).
    if (adventure56::Resource* r = game_.scene.resource(std::string("res_shadow")); r && r->model) {
        auto& o = game_.scene.create3D("\x8a\xdb\x89\x65", 0, 0x6e, 2);
        o.model = r->model;
        o.touchOn = false;
        o.switch14 = false;
        o.drawn = false;
    }
}

void RuinsLoop::storeFloor(int room, bool blank) {
    // 0x2c7c20: the floor's objects into the record (the working one, or
    // the blank one), those in `room` or all (-1): switches and levers
    // (300..302) their pressed state, blocks (200) where they stand and
    // whether they sank (state 11), plates (201) whether they are there,
    // treasures (500) dug ones (working record) or all (blank); then the
    // raised cells' (kind 1006) heights.
    Game::RuinRecord& record = blank ? blank_ : record_;
    const unsigned f = unsigned(floorNumber_ - 1) & 0xff;
    if (f >= 8) {
        return;
    }
    for (const auto& g : floor_.objects) {
        if (g.event < 0) {
            continue;
        }
        const EventDef& e = floor_.events[std::size_t(g.event)];
        const bool gone = g.name.empty();
        Actor* a = actorOf(&g);
        float x = g.x, z = g.z;
        if (a) {
            if (auto* o = game_.scene.object3D(a->hash)) {
                x = o->position[0] + o->offset[0];
                z = o->position[2] + o->offset[2];
            }
        }
        const CellIndex placed = cellAt(g.x, g.z);
        const CellIndex now = cellAt(x, z);
        const Cell* cell = floor_.cellAt(placed);
        if (!cell || cell->room < 0 || (room >= 0 && cell->room != room)) {
            continue;
        }
        const int number = e.number & 0xff;
        switch (e.kind) {
        case 300:
        case 301:
        case 302:
            record.switchNotes[f][number] = a && a->pressed ? 1 : 0;
            break;
        case 200:
            record.blockNotes[f][number] = {a && a->action == 11, now.row, now.column};
            break;
        case 201:
            record.plateNotes[f][number] = gone ? 0 : 1;
            break;
        case 500:
            if (!blank) {
                if (gone) {
                    record.eventNotes[f][number] = 1;
                }
            } else {
                record.eventNotes[f][number] = gone ? 1 : 0;
            }
            break;
        default:
            break;
        }
    }
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kColumns; ++c) {
            const Cell& cell = floor_.cells[std::size_t(r)][std::size_t(c)];
            if (cell.kind == 1006 && (room < 0 || cell.room == room)) {
                record.raisedHeights[f][{r, c}] = int(cell.y * (1.0f / kCellSize));
            }
        }
    }
}

void RuinsLoop::storeStands(Game::RuinRecord& record) {
    // 0x2c8378: the floor, Layton's and Randall's cells and turns, and the
    // two flags into the record.
    if (Actor* l = actor(kLayton)) { // ミニレイトン
        const CellIndex at = cellOf(*l);
        record.floor = floorNumber_;
        record.layton.row = at.row;
        record.layton.column = at.column;
        if (auto* o = game_.scene.object3D(l->hash)) {
            record.layton.angle = o->rotation3.value()[1];
        }
    }
    if (Actor* r = actor(kRandall)) { // ミニランド
        const CellIndex at = cellOf(*r);
        record.randall.row = at.row;
        record.randall.column = at.column;
        if (auto* o = game_.scene.object3D(r->hash)) {
            record.randall.angle = o->rotation3.value()[1];
        }
        record.flags = (record.flags & ~1u) | (randallFollows_ ? 0u : 1u);
        record.flags = (record.flags & ~2u) | (flag630_ ? 2u : 0u);
    }
}

void RuinsLoop::suspend() {
    // 0x1c7c90: with 《ＤＸ》遺跡回想 the stands and the floor go into the
    // blank record and the working one into the game's; an event playing
    // is kept to start again (+0x220c); the cells' kinds are kept; the
    // world goes (0x1c5308: the archives, the actors, the runner, the
    // floor, the guide, the effects); the actors stop; the base suspend.
    const int dx = game_.flagId("\x81\x73\x82\x63\x82\x77\x81\x74\x88\xe2\x90\xd5\x89\xf1\x91\x7a"); // 《ＤＸ》遺跡回想
    if (dx >= 0 && game_.flag(dx)) {
        storeStands(blank_);
        storeFloor(-1, true);
        game_.ruins = record_;
    }
    if (runner_.playing) {
        resumeEvent_ = runner_.lastName;
    }
    const int later = game_.flagId(kCannotLater);
    if (!resumeEvent_.empty() && later >= 0) {
        resumeEventFlag_ = game_.flag(later);
    }
    // 0x1ce954 (from 0x1c5308): an event playing is left and closed with
    // its scope; its script does not go on. The console shows it after
    // the fourth floor's puzzle (docs/ruins.md): NazoConnect's 2100 / 2102
    // / 10000 never run, the ruins resume and play the event again.
    if (runner_.playing) {
        threads[1]->stop();
        playback_.leave();
        playback_.close();
        game_.closeScope();
    }
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kColumns; ++c) {
            const PartDef* p = floor_.part(floor_.cells[std::size_t(r)][std::size_t(c)].kind);
            cellKindsKept_[std::size_t(r)][std::size_t(c)] = p ? p->id : -1;
        }
    }
    for (auto& a : actors_) {
        if (a) {
            dropEffect(a->effect);
            game_.scene.remove(a->hash);
        }
    }
    actors_.clear();
    removals_.clear();
    archives_ = {};
    runner_ = Runner{};
    floor_ = Floor{};
    camera_ = CameraControl{};
    actorsRun_ = false;
    Loop::suspend();
}

int RuinsLoop::restore() {
    // 0x1d0de0 (command 101). The record: the working one (the blank one
    // once after 107).
    Game::RuinRecord& record = useBlank_ ? blank_ : record_;
    useBlank_ = false;
    const auto clearKept = [this] {
        for (auto& row : cellKindsKept_) {
            row.fill(-1);
        }
    };
    if (restoring_) {
        // After a floor change or an event that restarted the loop: with
        // Layton in a marked room not yet seen, the stands and the floor's
        // entry go into the record (and the game's); then the world.
        restoring_ = false;
        Actor* l = actor(kLayton);
        const Cell* cell = l ? floor_.cellAt(cellOf(*l)) : nullptr;
        if (!cell) {
            return 0;
        }
        const unsigned room = unsigned(std::max(0, cell->room)) & 0xff;
        const bool marked = cell->room >= 0 && cell->room < kRooms && floor_.rooms[std::size_t(cell->room)].marker1000;
        const unsigned f = unsigned(floorNumber_ - 1) & 0xff;
        const bool seen = f < 8 && room < 32 && ((record.roomsSeen[f] >> room) & 1u) != 0;
        storeStands(record_);
        if (marked && !seen) {
            storeEntry();
            game_.ruins = record_;
        }
        buildWorld(record);
        clearKept();
        return 1;
    }
    Game::RuinRecord::Stand layton = record.layton, randall = record.randall;
    int floor = record.floor;
    std::uint32_t flags = record.flags;
    if (retry_) {
        // A retry: the game's record again, from where the floor was
        // entered; the kept cells go.
        record = game_.ruins;
        floor = record.retryFloor;
        layton = record.retryLayton;
        randall = record.retryRandall;
        flags = record.retryFlags;
        retry_ = false;
        clearKept();
    }
    buildWorld(record);
    // The kinds kept by the last suspend, but on cells that were kind 301.
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kColumns; ++c) {
            const int kept = cellKindsKept_[std::size_t(r)][std::size_t(c)];
            Cell& cell = floor_.cells[std::size_t(r)][std::size_t(c)];
            if (kept != -1 && cell.originalKind != 301) {
                if (const PartDef* p = floor_.part(kept)) {
                    cell.kind = p->id;
                }
            }
        }
    }
    if (floor == 0) {
        storeStands(record_);
        game_.ruins = record_;
        return 0;
    }
    Actor* l = actor(kLayton);
    if (!l) {
        return 0;
    }
    place(*l, {layton.row, layton.column});
    if (auto* o = game_.scene.object3D(l->hash)) {
        o->rotation3.set({0.0f, layton.angle, 0.0f});
    }
    if (Actor* r = actor(kRandall)) {
        place(*r, {randall.row, randall.column});
        if (auto* o = game_.scene.object3D(r->hash)) {
            o->rotation3.set({0.0f, randall.angle, 0.0f});
        }
    }
    randallFollows_ = (flags & 1) == 0;
    flag630_ = (flags & 2) != 0;
    for (int i = 0; i < 3; ++i) {
        setCameraMode(i, -1, 0.0f, 0, nullptr, true);
    }
    return 1;
}

void RuinsLoop::storeEntry() {
    // 0x1d188c (106): where the floor is entered, unless Layton is already
    // noted there: the floor, his cell and turn, Randall's goal and turn,
    // the flags; the game's record too.
    if (Actor* l = actor(kLayton)) {
        const CellIndex at = cellOf(*l);
        if (record_.retryFloor == floorNumber_ && record_.retryLayton.row == at.row &&
            record_.retryLayton.column == at.column) {
            return;
        }
        record_.retryFloor = floorNumber_;
        record_.retryLayton.row = at.row;
        record_.retryLayton.column = at.column;
        if (auto* o = game_.scene.object3D(l->hash)) {
            record_.retryLayton.angle = o->rotation3[1];
        }
    }
    if (Actor* r = actor(kRandall)) {
        record_.retryRandall.row = r->dest.row;
        record_.retryRandall.column = r->dest.column;
        if (auto* o = game_.scene.object3D(r->hash)) {
            record_.retryRandall.angle = o->rotation3[1];
        }
        record_.retryFlags = (randallFollows_ ? 0u : 1u) | (flag630_ ? 2u : 0u);
    }
    game_.ruins = record_;
}

bool RuinsLoop::holds(const EventDef& e) const {
    // 0x293214: the event's condition, a progress condition (its code at
    // +0x14, its operands the alternatives up to +0x18 of them; -1 holds).
    if (e.conditionCode == -1) {
        return true;
    }
    Rule r;
    r.code = e.conditionCode;
    for (int i = 0; i < e.conditionCount && i < 3; ++i) {
        const auto& a = e.before[std::size_t(i)];
        r.args.push_back(a.isNumber ? Value::ofInt(a.number) : Value::ofText(a.name));
    }
    return picarat::adventure5::holds(game_, r);
}

void RuinsLoop::applyEffect(const EventDef& e) {
    // 0x34710c: the event's effect, a progress effect (code +0x44, the
    // operands the after-alternatives up to +0x48 of them).
    if (e.effectCode < 0) {
        return;
    }
    Rule r;
    r.code = e.effectCode;
    for (int i = 0; i < e.effectCount && i < 3; ++i) {
        const auto& a = e.after[std::size_t(i)];
        r.args.push_back(a.isNumber ? Value::ofInt(a.number) : Value::ofText(a.name));
    }
    apply(game_, r);
}

void RuinsLoop::queueEvent(const std::string& name, bool notice, const EventDef* effects) {
    // 0x2c831c: only while nothing plays or waits: the event by name, the
    // loop takes no input (+0x1a), the runner at its notice stage (1) or
    // straight at its start (3).
    if (runner_.playing || runner_.queued || name.empty()) {
        return;
    }
    runner_.name = name;
    runner_.number = -1;
    game_.objectTouchLocked = true;
    runner_.queued = true;
    runner_.stage = notice ? 1 : 3;
    runner_.effects = effects;
}

void RuinsLoop::stepRunner() {
    // 0x1ce15c, while an event waits.
    if (!runner_.queued) {
        return;
    }
    Actor* layton = actor(kLayton);
    Actor* randall = actor(kRandall);
    switch (runner_.stage) {
    case 1: {
        // The "!" notice ("plc_obj_notice") over Layton: his place on the
        // top screen, spread out from the middle by 1.1 and 30 pixels up,
        // slides 15 more up over 5 frames playing "big_ntc"; effect 14.
        auto* notice = game_.scene.object(std::string("plc_obj_notice"));
        if (notice && layton) {
            if (auto* o = game_.scene.object3D(layton->hash)) {
                const auto at = o->position.value();
                const auto p = game_.projectToScreen(0, at[0], at[1], at[2]);
                float x = p[0] * 400.0f, y = p[1] * 240.0f;
                x = 200.0f + (x - 200.0f) * 1.1f;
                game_.playEffect(14);
                y += -30.0f;
                notice->visible = true;
                notice->position.set({x, y, 0.0f});
                notice->position.start({x, y - 15.0f, 0.0f}, 5.0f, 0);
                game_.playAnimation(*notice, Value::ofText("big_ntc"), 0xc);
                runner_.noticeTime = 30.0f;
            }
            for (Actor* a : {layton, randall}) {
                if (a && !a->standMotion.empty()) {
                    if (auto* o = game_.scene.object3D(a->hash)) {
                        o->motion = a->standMotion;
                    }
                }
            }
        }
        runner_.stage = 2;
        break;
    }
    case 2: {
        auto* notice = game_.scene.object(std::string("plc_obj_notice"));
        if (!notice) {
            runner_.stage = 3;
        } else if (!notice->position.moving()) {
            runner_.noticeTime -= 1.0f;
            if (runner_.noticeTime < 0.0f) {
                runner_.noticeTime = 0.0f;
                notice->visible = false;
                runner_.stage = 3;
            }
        }
        break;
    }
    case 3:
        // The script's preparation (on thread 0, as a call).
        threads[0]->callback("RPG_EvtPreStart", {});
        runner_.stage = 4;
        break;
    case 4: {
        // Once Randall rests: the stands go into the record (unless the
        // room still hides a treasure the player has not seen), a scope,
        // the event opened by name or number.
        if (randall && randall->action != 0 && randall->action != -1) {
            break;
        }
        if (layton) {
            const Cell* cell = floor_.cellAt(cellOf(*layton));
            if (cell) {
                const int room = cell->room;
                const unsigned f = unsigned(floorNumber_ - 1) & 0xff;
                const unsigned r = unsigned(std::max(room, 0)) & 0xff;
                const bool seen = f < 8 && r < 32 && ((record_.roomsSeen[f] >> r) & 1u) != 0;
                bool hides = false;
                if (room >= 0 && room < kRooms) {
                    hides = floor_.rooms[std::size_t(room)].marker1000 ||
                            floor_.rooms[std::size_t(room)].marker1001;
                }
                if (!hides || seen) {
                    for (const auto& a : actors_) {
                        if (a && a->klass == 5 && a->action != 0 && a->action != -1) {
                            return;
                        }
                    }
                    // 0x1ce15c: the stands (0x2c8378), then the room's
                    // objects and the raised cells' heights (0x2c7c20).
                    storeStands(record_);
                    storeFloor(std::max(room, 0), false);
                }
            }
        }
        game_.openScope(true);
        const EventRecord* e = runner_.name.empty() ? game_.story.event(runner_.number)
                                                    : game_.story.event(runner_.name);
        if (!e || !playback_.open(e, -1)) {
            runner_.queued = false;
            runner_.stage = 0;
            return;
        }
        runner_.lastName = runner_.name;
        runner_.name.clear();
        runner_.number = -1;
        runner_.stage = 5;
        break;
    }
    case 5:
        // Nothing loading, no fade: the event enters and its Main starts
        // on thread 1; its effect applies.
        if (game_.scene.brightness[0].moving() || game_.scene.brightness[1].moving()) {
            break;
        }
        playback_.enter();
        playback_.run(*this, 1, false);
        runner_.playing = true;
        runner_.queued = false;
        runner_.stage = 0;
        if (runner_.effects) {
            applyEffect(*runner_.effects);
        }
        runner_.effects = nullptr;
        break;
    default:
        break;
    }
}

void RuinsLoop::endEvent() {
    // 0x1d411c's check: no event waiting, thread 1 idle while one played:
    // it closes (稼働中のイベント, the branch's closing effects) and its scope
    // goes.
    if (runner_.queued || threads[1]->running() || !runner_.playing) {
        return;
    }
    runner_.lastName.clear();
    playback_.leave();
    playback_.close();
    game_.closeScope();
    runner_.playing = false;
}

int RuinsLoop::roomEvent(const std::string& name) {
    // 0x1d1384 (command 120): the room the actor stands in; the first cell
    // of it whose object carries an event of kind 102 that holds plays
    // that event (its eventB, no notice, its effect).
    Actor* a = actor(name);
    if (!a) {
        return 0;
    }
    const Cell* here = floor_.cellAt(cellOf(*a));
    if (!here) {
        return 0;
    }
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kColumns; ++c) {
            const Cell& cell = floor_.cells[std::size_t(r)][std::size_t(c)];
            if (cell.room != here->room) {
                continue;
            }
            for (const auto& o : floor_.objects) {
                if (o.event < 0 || !(cellAt(o.x, o.z) == CellIndex{r, c})) {
                    continue;
                }
                const EventDef& e = floor_.events[std::size_t(o.event)];
                if (e.kind != 102 || !holds(e)) {
                    continue;
                }
                if (!runner_.playing && !runner_.queued && !e.eventB.empty()) {
                    runner_.name = e.eventB;
                    runner_.number = -1;
                    game_.objectTouchLocked = true;
                    runner_.queued = true;
                    runner_.effects = &e;
                    runner_.stage = 3;
                }
                return 1;
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------- camera

void RuinsLoop::setCameraMode(int slot, int mode, float frames, int curve, Actor* subject, bool freeze) {
    // 0x2c3d54: the slot's mode, frames, curve and subject. A changed mode
    // with no higher slot on picks the highest slot on as the shown one:
    // the next follow eases to it (at once when none was shown before).
    if (slot < 0 || slot > 2) {
        return;
    }
    CameraSlot& s = camera_.slots[std::size_t(slot)];
    const int before = s.mode;
    s.mode = mode;
    s.frames = frames;
    s.curve = curve;
    s.subject = subject;
    if (before == mode) {
        return;
    }
    for (int i = slot + 1; i < 3; ++i) {
        if (camera_.slots[std::size_t(i)].mode != -1) {
            return;
        }
    }
    const bool none = camera_.current < 0;
    camera_.switching = true;
    camera_.settling = false;
    camera_.current = -1;
    for (int i = 2; i >= 0; --i) {
        if (camera_.slots[std::size_t(i)].mode != -1) {
            camera_.current = i;
            camera_.slots[std::size_t(i)].first = true;
            break;
        }
    }
    if (none && camera_.current >= 0) {
        camera_.fromNone = true;
    }
    camera_.freeze = freeze;
}

void RuinsLoop::pickRoomCamera() {
    // 0x1c59d0, while no event plays: slot 0 by Layton's room: mode 4 for
    // a room with a kind 1000 marker, 3 for kind 1001, else 1 (even rooms)
    // or 2 (odd); 60 frames, curve 3, Layton the subject, the pair waits.
    if (runner_.playing) {
        return;
    }
    Actor* layton = actor(kLayton);
    if (!layton) {
        return;
    }
    const Cell* cell = floor_.cellAt(cellOf(*layton));
    if (!cell) {
        return;
    }
    const unsigned room = unsigned(cell->room);
    const bool m1000 = room < unsigned(kRooms) && floor_.rooms[room].marker1000;
    const bool m1001 = room < unsigned(kRooms) && floor_.rooms[room].marker1001;
    const int mode = m1000 ? 4 : m1001 ? 3 : (room & 1) == 0 ? 1 : 2;
    setCameraMode(0, mode, 60.0f, 3, layton, true);
}

bool RuinsLoop::viewOf(CameraSlot& slot, const Actor& subject, std::array<float, 3>& eye,
                       std::array<float, 3>& target) {
    const int mode = slot.mode;
    // The modes' views: the target on the subject (position plus offset,
    // 0x293f78), the eye a distance away turned down about x (0x2c1d30
    // axis-angle: 0 and 1/2 0x293e9c 100 at 55 degrees; 3 0x1c5568 130 at
    // 60; 4 0x1c5464 110 at 65); 1..4 then keep the target inside the
    // room (0x293fc0, moving the eye alike). Mode 5 (0x1c566c, a tweened
    // offset from the camera) is not ported.
    const adventure56::Object3D* o = game_.scene.object3D(subject.hash);
    if (!o) {
        return false;
    }
    target = {o->position[0] + o->offset[0], o->position[1] + o->offset[1], o->position[2] + o->offset[2]};
    if (mode == 5) {
        // 0x1c566c: on its first frame the view's offsets from the subject
        // are kept and a shift eases from 0 to minus the target's offset
        // over 60 frames (curve 3), so the view comes onto the subject and
        // then follows it.
        const adventure56::Camera& cam = game_.scene.cameras[0];
        if (slot.first) {
            const auto t = cam.target.value(), e = cam.eye.value();
            for (std::size_t i = 0; i < 3; ++i) {
                slot.targetOffset[i] = t[i] - target[i];
                slot.eyeOffset[i] = e[i] - target[i];
            }
            slot.shift.set({0.0f, 0.0f, 0.0f});
            slot.shift.start({-slot.targetOffset[0], -slot.targetOffset[1], -slot.targetOffset[2]}, 60.0f, 3);
            slot.first = false;
        }
        const auto d = slot.shift.value();
        for (std::size_t i = 0; i < 3; ++i) {
            eye[i] = target[i] + slot.eyeOffset[i] + d[i];
            target[i] = target[i] + slot.targetOffset[i] + d[i];
        }
        return true;
    }
    float distance = 0, angle = 0;
    // The clamp's extents and margins (x, z).
    float spanX = 0, spanZ = 0, marginX = 0, marginZ = 0;
    switch (mode) {
    case 0:
        distance = 100.0f, angle = -0.959931076f; // 0x293f6c / 0x293f70
        break;
    case 1:
    case 2:
        distance = 100.0f, angle = -0.959931076f;
        spanX = spanZ = 32.0f, marginX = marginZ = 16.0f; // 0x1c6314 / 0x1c6310
        break;
    case 3:
        distance = 130.0f, angle = -1.04719758f; // 0x1c5654 / 0x1c5658
        spanX = 64.0f, spanZ = 64.0f, marginX = 32.0f, marginZ = 24.0f;
        break;
    case 4:
        distance = 110.0f, angle = -1.13446403f; // 0x1c5550 / 0x1c5554
        spanX = 64.0f, spanZ = 32.0f, marginX = 16.0f, marginZ = 16.0f;
        break;
    default:
        ++game_.missing[2130000 + mode];
        return false;
    }
    // (0, 0, d) turned by `angle` about x.
    eye = {target[0], target[1] - distance * std::sin(angle), target[2] + distance * std::cos(angle)};
    if (mode == 0) {
        return true;
    }
    const CellIndex at = cellAt(target[0], target[2]);
    const Cell* cell = floor_.cellAt(at);
    if (!cell || cell->room == -1) {
        return true;
    }
    Room r;
    if (unsigned(cell->room) < unsigned(kRooms)) {
        r = floor_.rooms[std::size_t(cell->room)];
    }
    const float z0 = float(r.row0()) * kCellSize, z1 = float(r.row1()) * kCellSize;
    const float x0 = float(r.column0()) * kCellSize, x1 = float(r.column1()) * kCellSize;
    const std::array<float, 3> before = target;
    if (z1 - z0 < spanZ) {
        target[2] = (z1 + z0) * 0.5f;
    } else {
        target[2] = std::max(target[2], z0 + marginZ);
        target[2] = std::min(target[2], z1 - marginZ);
    }
    if (x1 - x0 < spanX) {
        target[0] = (x1 + x0) * 0.5f;
    } else {
        target[0] = std::max(target[0], x0 + marginX);
        target[0] = std::min(target[0], x1 - marginX);
    }
    for (std::size_t i = 0; i < 3; ++i) {
        eye[i] += target[i] - before[i];
    }
    return true;
}

void RuinsLoop::followCamera() {
    // 0x1c5f30: screen 0's camera on the shown slot's view. After a switch
    // it eases there (from the last view, over the slot's frames and
    // curve, unless it is near: 2 units or less) and holds until the ease
    // ends; the last eye and target are kept each frame.
    adventure56::Camera& cam = game_.scene.cameras[0];
    if (camera_.settling) {
        if (cam.eye.moving() || cam.target.moving()) {
            return;
        }
        camera_.switching = false;
        camera_.settling = false;
    }
    if (camera_.current < 0) {
        return;
    }
    CameraSlot& slot = camera_.slots[std::size_t(camera_.current)];
    if (slot.mode == -1) {
        return;
    }
    Actor* subject = slot.subject ? slot.subject : actor(kLayton);
    if (!subject) {
        return;
    }
    if (slot.mode == 6) {
        camera_.switching = false;
    } else {
        std::array<float, 3> eye{}, target{};
        if (!viewOf(slot, *subject, eye, target)) {
            return;
        }
        if (!camera_.switching) {
            cam.eye.set(eye);
            cam.target.set(target);
        } else {
            const float frames = camera_.fromNone ? 0.0f : slot.frames;
            camera_.fromNone = false;
            const auto far = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
                const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
                return std::sqrt(dx * dx + dy * dy + dz * dz) > 2.0f;
            };
            if (!far(camera_.lastEye, eye) || frames == 0.0f) {
                cam.eye.set(eye);
            } else {
                cam.eye.set(camera_.lastEye);
                cam.eye.start(eye, frames, slot.curve);
            }
            if (!far(camera_.lastTarget, target) || frames == 0.0f) {
                cam.target.set(target);
            } else {
                cam.target.set(camera_.lastTarget);
                cam.target.start(target, frames, slot.curve);
            }
            camera_.settling = true;
        }
    }
    slot.shift.step(); // 0x2b2044 on the slot's tweens
    camera_.lastEye = cam.eye.value();
    camera_.lastTarget = cam.target.value();
}

void RuinsLoop::afterFrame() {
    // 0x1c59d0: the base end of frame (the loop ends when done), the
    // shells shown as their bits of +0x21d4 say, the actors (0x1c798c),
    // the event runner (0x1ce15c). The camera modes after them are not
    // ported (docs/ruins.md).
    Loop::afterFrame();
    for (const auto& p : floor_.parts) {
        const unsigned i = unsigned(p.id - 100);
        if (p.object.empty() || i >= 100) {
            continue;
        }
        const bool hidden = i < 101 && (roomHidden_[i >> 3] & (1u << (i & 7))) != 0;
        if (auto* o = game_.scene.object3D(xq::hash(p.object))) {
            o->touchOn = !hidden;
            o->switch14 = !hidden;
            o->drawn = !hidden;
        }
    }
    guideFrame();
    freeRemovals();
    runActors();
    endEvent();
    stepRunner();
    pickRoomCamera();
    followCamera();
}

bool RuinsLoop::command(int number, xq::Call& c, std::size_t first) {
    const std::size_t n = c.count() > first ? c.count() - first : 0;
    switch (number) {
    case 100: // 0x1463f4 -> 0x1c92b8: the floor's files
        loadFloor();
        return result(c, Value::ofInt(1));
    case 101: // 0x1d0ddc -> 0x1d0de0
        return result(c, Value::ofInt(restore()));
    case 103: // 0x156060: the floor number (+0x21f4)
        return result(c, Value::ofInt(floorNumber_));
    case 104: // 0x150798: +0x21f8 (the loop's second value)
        return result(c, Value::ofInt(std::int8_t(startByte_)));
    case 110:   // 0x162894: the start cell (kind 1003) {-> row, column}
    case 111: { // 0x161624: kind 1004
        if (n < 2) {
            return false;
        }
        const auto at = findCell(number == 110 ? kStartCell : kSecondCell);
        if (!at) {
            return result(c, Value::ofInt(0));
        }
        put(c, first, Value::ofInt(at->row));
        put(c, first + 1, Value::ofInt(at->column));
        return result(c, Value::ofInt(1));
    }
    case 112:   // 0x146378: a cell's surface {row, column} (a real)
    case 114: { // 0x1591b0: where an actor stands on it (plus an object's height)
        if (n < 2) {
            return false;
        }
        const CellIndex at{c.integer(first), c.integer(first + 1)};
        return result(c, Value::ofFloat(number == 112 ? surface(at) : standHeight(at)));
    }
    case 116: { // 0x1590f4: the event kind of the object on a cell {row, column} (-1)
        // The first grid object there (0x2c0158 mode 0: an actor's object
        // where the actor stands), its event (+0x50).
        if (n < 2) {
            return false;
        }
        const GridObject* o = objectAt({c.integer(first), c.integer(first + 1)}, 0);
        return result(c, Value::ofInt(o && o->event >= 0 ? floor_.events[std::size_t(o->event)].kind : -1));
    }
    case 119: { // 0x1560fc: a room seen on this floor {room} (0x2bf3e4)
        if (n < 1) {
            return false;
        }
        const unsigned room = unsigned(std::max(0, c.integer(first))) & 0xff;
        const unsigned f = unsigned(floorNumber_ - 1) & 0xff;
        const bool seen = f < 8 && room < 32 && ((record_.roomsSeen[f] >> room) & 1u) != 0;
        return result(c, Value::ofInt(seen ? 1 : 0));
    }
    case 123:   // 0x159338: a room holds a treasure (an object of kind 1000) {room}
    case 124: { // 0x1560a4: one of kind 1001
        if (n < 1) {
            return false;
        }
        const unsigned room = unsigned(c.integer(first));
        bool v = false;
        if (room < unsigned(kRooms)) {
            v = number == 123 ? floor_.rooms[room].marker1000 : floor_.rooms[room].marker1001;
        }
        return result(c, Value::ofInt(v ? 1 : 0));
    }
    case 131: // 0x14c0b4: the guide's +0x28
        return result(c, Value::ofInt(guide_.b4));
    case 135: // 0x15cd94: not the guide's +0x2a
        return result(c, Value::ofInt(guide_.b6 ^ 1));
    case 136: // 0x15cd70: the guide's +0x29
        return result(c, Value::ofInt(guide_.b5));
    case 137: // 0x15f680: +0x2b
        return result(c, Value::ofInt(guide_.b7));
    case 138: // 0x15f638: +0x2c
        return result(c, Value::ofInt(guide_.b8));
    case 145: // 0x15f65c: +0x2d
        return result(c, Value::ofInt(guide_.b9));
    case 146: // 0x1639f4: the guide's +0x30 set
        guide_.bc = 1;
        return result(c, Value::ofInt(1));
    case 147: // 0x161778: +0x31 {value}
    case 148: // 0x161730: +0x2f {value}
        if (n < 1) {
            return false;
        }
        (number == 147 ? guide_.bd : guide_.bb) = std::int8_t(c.at(first).truthy() ? 1 : 0);
        return result(c, Value::ofInt(1));
    case 200: { // 0x159264: a model row's flags {id, -> real, -> 4 flags}
        if (n < 6) {
            return false;
        }
        if (const PartDef* p = floor_.part(c.integer(first))) {
            put(c, first + 1, Value::ofFloat(p->reals[0]));
            for (std::size_t i = 0; i < 4; ++i) {
                put(c, first + 2 + i, Value::ofInt(std::int8_t(p->flags[i])));
            }
        }
        return true;
    }
    case 300: { // 0x142a14: an event runs (the runner +0x28d0 / +0x28d4) or thread 1 does
        const bool busy = runner_.queued || runner_.playing || threads[1]->running();
        return result(c, Value::ofInt(busy ? 1 : 0));
    }
    case 302: { // 0x14bfe8: while an event waits or plays, the byte +0x3c of the
                // floor's event that names it (as its first or second event); else 0
        int v = 0;
        if ((runner_.queued || runner_.playing) && !runner_.lastName.empty()) {
            for (const auto& e : floor_.events) {
                if (e.kind != -1 && (e.eventA == runner_.lastName || e.eventB == runner_.lastName)) {
                    v = std::int8_t(e.byteA);
                    break;
                }
            }
        }
        return result(c, Value::ofInt(v));
    }
    case 400: // 0x146274: a camera slot's mode {slot, mode, frames, curve}
        if (n < 4) {
            return false;
        }
        setCameraMode(c.integer(first), std::int8_t(c.integer(first + 1)), c.real(first + 2),
                      std::int8_t(c.integer(first + 3)), nullptr, true);
        return true;
    case 500: // 0x155f2c: the actors' update on / off {flag} (+0x21f0)
        if (n < 1) {
            return false;
        }
        actorsRun_ = c.at(first).truthy();
        return true;
    case 505: { // 0x131134: where an actor faces {name, -> row step, -> column step}
        if (n < 3) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        float turn = 0.0f;
        if (auto* o = game_.scene.object3D(a->hash)) {
            turn = o->rotation3.value()[1];
        }
        // cos / sin (0x2d57f8 / 0x2d5974) + 1.5, truncated, - 1.
        put(c, first + 1, Value::ofFloat(float(int(std::cos(turn) + 1.5f) - 1)));
        put(c, first + 2, Value::ofFloat(float(int(std::sin(turn) + 1.5f) - 1)));
        return true;
    }
    case 515: { // 0x14bf88: how many mummies (class 3)
        int count = 0;
        for (const auto& a : actors_) {
            count += a && a->klass == 3 ? 1 : 0;
        }
        return result(c, Value::ofInt(count));
    }
    case 516: { // 0x146204: an actor is at its action 2 or its state 2 {name}
        if (n < 1) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        return result(c, Value::ofInt(a->action == 2 || a->state == 2 ? 1 : 0));
    }
    case 696: { // 0x158f38 -> 0x3473cc: any actor busy (its action neither 0 nor -1)
        bool busy = false;
        for (const auto& a : actors_) {
            busy = busy || (a && a->action != 0 && a->action != -1);
        }
        return result(c, Value::ofInt(busy ? 1 : 0));
    }
    case 697: // 0x161520: +0x628 (cleared each frame)
        return result(c, Value::ofInt(somethingHappened_ ? 1 : 0));
    case 120: { // 0x159084: the room's event of kind 102 {name} (unless +0x2204)
        if (n < 1) {
            return false;
        }
        const int v = eventQueued_ ? 0 : roomEvent(textOf(c, first));
        eventQueued_ = false;
        return result(c, Value::ofInt(v));
    }
    case 107: // 0x140f80: the bag (loop 2100) over a blank record of now
        eventQueued_ = true;
        blank_ = Game::RuinRecord{}; // 0x2c81c4
        storeStands(blank_);
        storeFloor(-1, true);
        useBlank_ = true;
        story_.push(2100, {});
        return true;
    case 118: { // 0x15952c: a room seen {room, flag}: noted, the floor stored, the
                // game's record updated
        if (n < 2) {
            return false;
        }
        const int room = std::max(0, c.integer(first));
        if (c.at(first + 1).truthy()) {
            const unsigned f = unsigned(floorNumber_ - 1) & 0xff;
            if (f < 8 && room < 32) {
                record_.roomsSeen[f] |= 1u << room;
            }
            storeFloor(room, false);
            game_.ruins = record_;
            guide_.b4 = 0; // +0x4c3b4
        }
        return true;
    }
    case 121: { // 0x1593fc: the stands and the actor's room stored {name [, copy]}
        if (n < 1) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        const Cell* cell = a ? floor_.cellAt(cellOf(*a)) : nullptr;
        if (!cell) {
            return false;
        }
        storeStands(record_);
        storeFloor(std::max(0, cell->room), false);
        if (n < 2 || c.at(first + 1).truthy()) {
            game_.ruins = record_;
        }
        return result(c, Value::ofInt(1));
    }
    case 128: { // 0x1595f4: a room's map bit in the game's record {room, flag}
        if (n < 2) {
            return false;
        }
        const unsigned room = unsigned(std::max(0, c.integer(first)));
        const unsigned f = unsigned(floorNumber_ - 1) & 0xff;
        if (f < 8 && room < 32) {
            if (c.at(first + 1).truthy()) {
                game_.ruins.roomsMapped[f] |= 1u << room;
            } else {
                game_.ruins.roomsMapped[f] &= ~(1u << room);
            }
        }
        return true;
    }
    case 102: { // 0x1505e8 -> 0x1d372c: to another floor {floor [, byte]}: going up, the
                // room Layton left (if marked) is seen and the floor stored; the loop
                // starts again on the new floor
        if (n < 1) {
            return false;
        }
        const int to = c.integer(first);
        Actor* l = actor(kLayton);
        const Cell* cell = l ? floor_.cellAt(l->last) : nullptr;
        if (!cell) {
            return false;
        }
        if (floorNumber_ < to) {
            const unsigned room = unsigned(cell->room);
            const bool marked = room < unsigned(kRooms) && (floor_.rooms[room].marker1000 || floor_.rooms[room].marker1001);
            if (marked) {
                const unsigned f = unsigned(floorNumber_ - 1) & 0xff;
                if (f < 8 && room < 32) {
                    record_.roomsSeen[f] |= 1u << room;
                }
            }
            storeFloor(cell->room, false);
            game_.ruins = record_;
        }
        story_.restart();
        floorNumber_ = to;
        game_.setVariable("\x8c\xbb\x8d\xdd\x82\xcc\x83\x76\x83\x8c\x83\x43\x83\x58", (to + 10) * 10); // 現在のプレイス
        startByte_ = n > 1 && c.at(first + 1).truthy() ? 1 : 0;
        restoring_ = true;
        guide_.b4 = 0;
        return result(c, Value::ofInt(1));
    }
    case 105: // 0x146414: retry the floor [flag +0x4c3b4]: the loop starts again
        story_.restart();
        retry_ = true;
        if (n > 0) {
            guide_.b4 = std::int8_t(c.at(first).truthy() ? 1 : 0);
        }
        return true;
    case 106: // 0x1d1888 -> 0x1d188c: where the floor was entered (the retry stands)
        storeEntry();
        return result(c, Value::ofInt(1));
    case 113: { // 0x146464: a cell's height {row, column, height}
        if (n < 3) {
            return false;
        }
        Cell* cell = floor_.cell({c.integer(first), c.integer(first + 1)});
        if (!cell) {
            return false;
        }
        cell->y = c.real(first + 2);
        return result(c, Value::ofInt(1));
    }
    case 122: { // 0x1561e4: a 3D object's name noted in the record (16 at most) {name}
        if (n < 1) {
            return false;
        }
        const std::string name = textOf(c, first);
        if (!game_.scene.object3D(xq::hash(name))) {
            return false;
        }
        const std::uint32_t h = xq::hash(name);
        auto& list = record_.notedNames;
        if (std::find(list.begin(), list.end(), h) == list.end()) {
            if (auto it = std::find(list.rbegin(), list.rend(), 0u); it != list.rend()) {
                *it = h;
            }
        }
        return result(c, Value::ofInt(1));
    }
    case 125: { // 0x14c190 -> 0x1d31fc: a treasure (event kind 500 by number) dug by the
                // script: noted, its actor and object gone, its cell a hole (kind 300,
                // plain ground after)
        if (n < 1) {
            return false;
        }
        const int number = c.integer(first) & 0xff;
        for (auto& g : floor_.objects) {
            if (g.name.empty() || g.event < 0) {
                continue;
            }
            const EventDef& e = floor_.events[std::size_t(g.event)];
            if (e.kind != 500 || e.number != number) {
                continue;
            }
            noteEvent(e, 1);
            float x = g.x, z = g.z;
            if (Actor* a = actorOf(&g)) {
                if (auto* o = game_.scene.object3D(a->hash)) {
                    x = o->position[0] + o->offset[0];
                    z = o->position[2] + o->offset[2];
                }
                removeActor(*a);
            }
            g.actor = -1;
            g.name.clear();
            if (Cell* cell = floor_.cell(cellAt(x, z))) {
                cell->kind = 300;
                cell->originalKind = 200;
            }
            break;
        }
        return result(c, Value::ofInt(1));
    }
    case 126: { // 0x163a20: a room's place on the map {room, -> x, -> y}
        if (n < 3) {
            return false;
        }
        const unsigned room = unsigned(c.integer(first));
        if (room >= unsigned(kRooms)) {
            return false;
        }
        put(c, first + 1, Value::ofInt(floor_.rooms[room].values[0]));
        put(c, first + 2, Value::ofInt(floor_.rooms[room].values[1]));
        return true;
    }
    case 127: { // 0x1506d4: a switch's pressed state on a cell {row, column}
        if (n < 2) {
            return false;
        }
        int v = 0;
        if (const GridObject* g = objectAt({c.integer(first), c.integer(first + 1)}, 0)) {
            if (Actor* a = actorOf(g); a && g->event >= 0 && (a->klass == 6 || a->klass == 7 || a->klass == 8)) {
                v = a->pressed ? 1 : 0;
            }
        }
        return result(c, Value::ofInt(v));
    }
    case 129: { // 0x156168: a room's map bit in the game's record {room}
        if (n < 1) {
            return false;
        }
        const unsigned room = unsigned(std::max(0, c.integer(first)));
        const unsigned f = unsigned(floorNumber_ - 1) & 0xff;
        const bool v = f < 8 && room < 32 && ((game_.ruins.roomsMapped[f] >> room) & 1u) != 0;
        return result(c, Value::ofInt(v ? 1 : 0));
    }
    case 132: // 0x159070: the guide's +0x29 set
        guide_.b5 = 1;
        return true;
    case 202: { // 0x163900: the decorations' (event kind 400) alpha {value, frames, curve}
        if (n < 2) {
            return false;
        }
        for (const auto& g : floor_.objects) {
            if (g.name.empty() || g.event < 0 || floor_.events[std::size_t(g.event)].kind != 400) {
                continue;
            }
            if (Actor* a = actorOf(&g)) {
                if (auto* o = game_.scene.object3D(a->hash)) {
                    o->alpha.start({c.real(first)}, c.real(first + 1), n > 2 ? std::int8_t(c.integer(first + 2)) : 0);
                }
            }
        }
        return result(c, Value::ofInt(1));
    }
    case 301: { // 0x12b85c -> 0x1ce010: an event (by name, or by number: its own when
                // listed, else the hundred) plays, no notice
        if (n < 1) {
            return false;
        }
        if (c.at(first).isText()) {
            queueEvent(textOf(c, first), false, nullptr);
        } else {
            int number = c.integer(first);
            if (!game_.story.event(number)) {
                number = number / 100 * 100;
            }
            if (!runner_.playing && !runner_.queued && number >= 0) {
                runner_.name.clear();
                runner_.number = number;
                game_.objectTouchLocked = true;
                runner_.queued = true;
                runner_.effects = nullptr;
                runner_.stage = 3;
            }
        }
        return result(c, Value::ofInt(1));
    }
    case 305: { // 0x140ec8: an event by name plays {name or its hash}
        // (144 hands over the guide's failure event as a hash, as the
        // game keeps it.)
        if (n < 1) {
            return false;
        }
        std::string name = textOf(c, first);
        if (name.empty() && !c.at(first).isText()) {
            const std::uint32_t h = c.at(first).toUint();
            for (const auto& e : game_.story.events) {
                if (h != 0 && xq::hash(e.name) == h) {
                    name = e.name;
                    break;
                }
            }
        }
        queueEvent(name, false, nullptr);
        return result(c, Value::ofInt(1));
    }
    case 401: { // 0x14631c: screen 0's depth to 78 and strength to 1 over 30 frames
        auto& cam = game_.scene.cameras[0];
        cam.stereoDistance.start({78.0f}, 30.0f, 3);
        cam.stereoStrength.start({1.0f}, 30.0f, 3);
        return result(c, Value::ofInt(1));
    }
    case 402: { // 0x156014: either still moving
        const auto& cam = game_.scene.cameras[0];
        return result(c, Value::ofInt(cam.stereoDistance.moving() || cam.stereoStrength.moving() ? 1 : 0));
    }
    case 134: // 0x159390: the guide mode named {name}: the loop starts again
        story_.restart();
        retry_ = true;
        guideName_ = n > 0 ? textOf(c, first) : std::string();
        guide_.b6 = 1; // +0x4c3b6
        return true;
    case 133: // 0x150604 -> 0x1ceb2c: the guide ends (End_GuideMode)
        guideEnd();
        return result(c, Value::ofInt(1));
    case 139: // 0x15f580: a guide being started begins (0x1cf218); the offer (0x1cef58)
        if (guide_.b6) {
            guideStart();
        }
        guideOffer();
        return result(c, Value::ofInt(1));
    case 140: { // 0x161544: Layton dug (1), pushed (2) or switched (3) {kind}
        // While the guide runs the action is judged against the shown
        // phase's markers (a switch is always right); a right or wrong one
        // fades the markers (0x1cecb8). 141 reads the verdict.
        if (n < 1) {
            return false;
        }
        int verdict = 1;
        if (guide_.b5) {
            switch (c.integer(first)) {
            case 1:
                verdict = guideCheck(1502);
                break;
            case 2:
                verdict = guideCheck(1503) | 1 | guideCheck(1504) | guideCheck(1505) | guideCheck(1506);
                break;
            case 3:
                verdict = 5;
                break;
            default:
                break;
            }
            if (verdict & 6) {
                guideActed((verdict & 2) == 0);
            }
        }
        guide_.result = verdict;
        return result(c, Value::ofInt(1));
    }
    case 141: { // 0x15ccb0: an actor's move ended {name, n}
        // Layton's after a judged action: right goes to the next phase,
        // wrong is a failure (+0x2b).
        if (n < 2) {
            return false;
        }
        if (guide_.b5 && xq::hash(textOf(c, first)) == xq::hash(kLayton)) {
            if (guide_.result & 4) {
                guideNextPhase();
                guide_.cell = {};
                guide_.angle = kNoAngle;
            } else if (guide_.result & 2) {
                guide_.b7 = 1;
            }
        }
        guide_.result = 1;
        return result(c, Value::ofInt(1));
    }
    case 142: { // 0x15f714: the routes' telops shown {shown} or the one at a cell {row, column, shown}
        if (n == 0) {
            return false;
        }
        if (n < 2) {
            const bool shown = c.at(first).truthy();
            for (GuideRoute& r : guide_.routes) {
                r.telopShown = shown;
            }
        }
        if (n > 2) {
            const CellIndex at{c.integer(first), c.integer(first + 1)};
            const bool shown = c.at(first + 2).truthy();
            for (GuideRoute& r : guide_.routes) {
                if (r.telopCell == at) {
                    r.telopShown = shown;
                }
            }
        }
        return true;
    }
    case 143: { // 0x159308: the last faded marker still fading (Clear_GuideMode waits)
        auto* o = guide_.lastMarker.empty() ? nullptr : game_.scene.object3D(xq::hash(guide_.lastMarker));
        return result(c, Value::ofInt(o && o->alpha.moving() ? 1 : 0));
    }
    case 144: { // 0x15f5b4: the route's failure event {-> its name's hash} (0 when none)
        if (n < 1) {
            return false;
        }
        Value v = Value::ofInt(0);
        if (guide_.b5 && !guide_.b8 && guide_.route >= 0 && std::size_t(guide_.route) < guide_.routes.size()) {
            const std::string& failure = guide_.routes[std::size_t(guide_.route)].failure;
            v = Value::ofInt(failure.empty() ? 0 : std::int32_t(xq::hash(failure))); // (a hash, as 5110 keeps it)
        }
        put(c, first, v);
        return result(c, Value::ofInt(1));
    }
    case 130: // 0x156080: +0x21fc (an event resumed the loop)
        return result(c, Value::ofInt(restoring_ ? 1 : 0));
    case 115: { // 0x15061c: a cell's kind {row, column} (-1 none)
        if (n < 2) {
            return false;
        }
        const Cell* cell = floor_.cell({c.integer(first), c.integer(first + 1)});
        return result(c, Value::ofInt(cell ? cell->kind : -1));
    }
    case 117: { // 0x14c0d8: a cell's room {row, column}
        if (n < 2) {
            return false;
        }
        const Cell* cell = floor_.cell({c.integer(first), c.integer(first + 1)});
        if (!cell) {
            return false;
        }
        return result(c, Value::ofInt(cell->room));
    }
    case 201: { // 0x15f6a4: a shell's hidden bit {model id - 100, 1 shown}
        if (n < 2) {
            return false;
        }
        const unsigned i = unsigned(c.integer(first)) & 0xff;
        if (i / 8 < roomHidden_.size()) {
            const auto bit = std::uint8_t(1u << (i & 7));
            if (c.integer(first + 1) == 1) {
                roomHidden_[i / 8] &= std::uint8_t(~bit);
            } else {
                roomHidden_[i / 8] |= bit;
            }
        }
        return true;
    }
    case 501:   // 0x130f9c: an actor {name, model resource, code [, row, column]}
    case 510:   // 0x155de0: Layton (class 1) {name, model [, row, column]}
    case 511: { // 0x14be38: Randall (class 2)
        const std::size_t minimum = number == 501 ? 3 : 2;
        if (n < minimum) {
            return false;
        }
        const int klass = number == 501 ? 0 : number == 510 ? 1 : 2;
        Actor* a = newActor(klass);
        if (!a) {
            return false;
        }
        const int code = number == 501 ? c.integer(first + 2) : number == 510 ? 600 : 0x2c4;
        if (!attach(*a, textOf(c, first), textOf(c, first + 1), code)) {
            actors_.pop_back(); // the slot goes back (the removal list +0x27b8)
            return false;
        }
        const std::size_t at = number == 501 ? 3 : 2;
        if (n > at + 1 || (number != 501 && n > at)) {
            place(*a, {c.integer(first + at), c.integer(first + at + 1)});
        }
        settle(*a);
        return true;
    }
    case 512: { // 0x150440 -> 0x1d342c: a mummy {name, model resource [, row, column [, sight east,
                // west, south, north [, +0x2d8 [, fills high [, quiet]]]]]}
        // A class 3 actor with its 3D object (code 0x25a; the red model
        // 0x25b, the needle one 0x25c); on its cell (also its home), its
        // values; then it appears (state 24). A model that does not load
        // gives the slot back.
        if (n < 2) {
            return false;
        }
        Actor* a = newActor(3);
        if (!a) {
            return result(c, Value::ofInt(0));
        }
        const std::string model = textOf(c, first + 1);
        const int code = model == "res_mr_r" ? (0x25a | (0x25a >> 3)) : model == "res_mr_b_needle" ? 0x25c : 0x25a;
        if (!attach(*a, textOf(c, first), model, code)) {
            actors_.pop_back();
            return result(c, Value::ofInt(0));
        }
        if (n > 2) {
            a->home = {c.integer(first + 2), c.integer(first + 3)};
            place(*a, a->home);
        }
        if (n > 4) {
            for (std::size_t i = 0; i < 4; ++i) {
                a->sight[i] = c.integer(first + 4 + i);
            }
        }
        if (n > 8) {
            a->mummyValue = c.integer(first + 8);
        }
        if (n > 9) {
            a->fillsHigh = c.at(first + 9).truthy();
        }
        if (n > 10) {
            a->quietAppear = c.at(first + 10).truthy();
        }
        const CellIndex at = cellOf(*a);
        a->state = 24;
        a->settled = true;
        a->target = at;
        a->dest = at;
        return result(c, Value::ofInt(1));
    }
    case 600:   // 0x1460c8: an actor steps to a cell {name, row, column, step frames, turn frames}
    case 601: { // 0x145fec: to a cell beside it {name, row step, column step, ..}
        if (n < 5) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        CellIndex to{c.integer(first + 1), c.integer(first + 2)};
        if (number == 601) {
            const CellIndex at = cellOf(*a);
            to.row += at.row;
            to.column += at.column;
        }
        requestWalk(*a, to, c.real(first + 3), c.real(first + 4));
        return true;
    }
    case 507: { // 0x15045c: an actor's cell before its last step {name, -> row, -> column}
        if (n < 3) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        put(c, first + 1, Value::ofInt(a->last.row));
        put(c, first + 2, Value::ofInt(a->last.column));
        return true;
    }
    case 514: { // 0x140db4: the actor is there (and not on the removal list) {name}
        if (n < 1) {
            return false;
        }
        return result(c, Value::ofInt(actor(textOf(c, first)) ? 1 : 0));
    }
    case 602: { // 0x1359c0: an actor stops {name}
        if (n < 1) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        settle(*a);
        return true;
    }
    case 604:   // 0x130e28: Layton digs (5) at the cell beside him {name, row step, column step}
    case 605:   // 0x135898: pushes (6)
    case 609: { // 0x145ec4: works a lever (7)
        if (n < 3) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a || a->klass != 1) {
            return false;
        }
        const CellIndex at = cellOf(*a);
        const CellIndex to{at.row + c.integer(first + 1), at.column + c.integer(first + 2)};
        if (a->idle || eventBusy()) {
            return true;
        }
        // The pending command's target alone is set (its other fields stay).
        a->state = number == 604 ? 5 : number == 605 ? 6 : 7;
        a->settled = false;
        a->target = to;
        a->dest = at;
        a->pending.target = to;
        return true;
    }
    case 607: { // 0x142830: an actor turns by an angle {name, angle, frames}
        if (n < 3) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        if (!a->idle) {
            const CellIndex at = cellOf(*a);
            a->state = 4;
            a->settled = false;
            a->target = at;
            a->dest = at;
            a->pending.turn = c.real(first + 1);
            a->pending.faceFrames = c.real(first + 2);
        }
        return true;
    }
    case 608: { // 0x150388: an actor steps on along its path {name, step frames, turn frames}
        if (n < 3) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        if (!a->idle) {
            // The path's current step (unset steps are cell 0, 0).
            const CellIndex to = a->pathIndex < a->path.size() ? a->path[a->pathIndex] : CellIndex{0, 0};
            requestWalk(*a, to, c.real(first + 1), c.real(first + 2));
            a->pending.onward = true;
        }
        return true;
    }
    case 699: { // 0x140cc0: an actor is busy (its action neither -1 nor 0) {name}
        if (n < 1) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        return result(c, Value::ofInt(a->action != -1 && a->action != 0 ? 1 : 0));
    }
    case 700: // 0x140f48: +0x62c {flag}
    case 702: // 0x135b48: +0x634
    case 704: // 0x1505b0: +0x630
        if (n < 1) {
            return false;
        }
        (number == 700 ? randallFollows_ : number == 702 ? flag634_ : flag630_) = c.at(first).truthy();
        return true;
    case 701: // 0x140f24
        return result(c, Value::ofInt(randallFollows_ ? 1 : 0));
    case 703: // 0x135b24
        return result(c, Value::ofInt(flag634_ ? 1 : 0));
    case 705: // 0x150590
        return result(c, Value::ofInt(flag630_ ? 1 : 0));
    case 506: { // 0x14618c: where an actor is going {name, -> row, -> column}
        if (n < 3) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        put(c, first + 1, Value::ofInt(a->dest.row));
        put(c, first + 2, Value::ofInt(a->dest.column));
        return true;
    }
    case 508: { // 0x140e44: an actor onto its cell, the stands stored {name}
        if (n < 1) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        place(*a, cellOf(*a));
        storeStands(record_);
        return true;
    }
    case 509:   // 0x142904: a path for an actor to a cell {name, row, column, most [, -> steps]}
    case 523: { // 0x15ca48: a mummy's path, planning (+0x2e1) {name, row, column, most, -> steps}
        if (n < (number == 509 ? 4u : 5u)) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a || (number == 523 && a->klass != 3)) {
            return false;
        }
        a->planning = number == 523;
        const CellIndex to{c.integer(first + 1), c.integer(first + 2)};
        bool found = false;
        a->path = findPath(*a, cellOf(*a), to, std::min(c.integer(first + 3), 50), false, found);
        a->pathIndex = 0;
        if (n > 4) {
            put(c, first + 4, Value::ofInt(int(a->path.size())));
        }
        a->planning = false;
        return result(c, Value::ofInt(found ? 1 : 0));
    }
    case 521: { // 0x159054 -> 0x1d3b88: a block drops onto a cell {block number, row, column}
        // The event of kind 200 with that number (the first of the 120
        // slots), the live object carrying it with an actor: from 40 above
        // the cell's position it drops there (0x293178), no longer frozen,
        // and takes a frame at once.
        if (n < 3) {
            return false;
        }
        const int number = c.integer(first) & 0xff;
        int event = -1;
        for (std::size_t i = 0; i < floor_.events.size(); ++i) {
            if (floor_.events[i].kind == 200 && floor_.events[i].number == number) {
                event = int(i);
                break;
            }
        }
        if (event < 0) {
            return result(c, Value::ofInt(0));
        }
        for (auto& g : floor_.objects) {
            Actor* a = actorOf(&g);
            if (g.name.empty() || g.event != event || !a) {
                continue;
            }
            const Cell* cell = floor_.cellAt({c.integer(first + 1), c.integer(first + 2)});
            if (!cell) {
                continue;
            }
            dropBack(*a, {cell->x, cell->y + 40.0f, cell->z}, {cell->x, cell->y, cell->z});
            a->frozen = false;
            tickActor(*a);
            return result(c, Value::ofInt(1));
        }
        return result(c, Value::ofInt(0));
    }
    case 513: // 0x15cc1c: every mummy crushed (20, idle)
        for (const auto& a : actors_) {
            if (a && a->klass == 3) {
                const CellIndex at = cellOf(*a);
                a->state = 20;
                a->settled = true;
                a->target = at;
                a->dest = at;
                a->idle = true;
            }
        }
        return true;
    case 517: { // 0x15f4fc: a mummy's home {name, -> row, -> column}
        // (+0x2b8 / +0x2bc, set by 512; the actor must be of kind 3.)
        if (n < 3) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a || a->klass != 3) {
            return false;
        }
        put(c, first + 1, Value::ofInt(a->home.row));
        put(c, first + 2, Value::ofInt(a->home.column));
        return true;
    }
    case 518: { // 0x163850: a mummy sees a cell {name, row, column}
        if (n < 3) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a || a->klass != 3) {
            return false;
        }
        return result(c, Value::ofInt(sees(*a, {c.integer(first + 1), c.integer(first + 2)}) ? 1 : 0));
    }
    case 519:   // 0x158fe8: a mummy noticed Layton (+0x2e0) {name}
    case 520: { // 0x158f80: its +0x2d8
        if (n < 1) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a || a->klass != 3) {
            return false;
        }
        return result(c, Value::ofInt(number == 519 ? (a->noticed ? 1 : 0) : a->mummyValue));
    }
    case 522: { // 0x15cb68: the actor on a cell is the one named {row, column, name}
        if (n < 3) {
            return false;
        }
        Actor* on = actorAt({c.integer(first), c.integer(first + 1)}, nullptr);
        Actor* named = actor(textOf(c, first + 2));
        return result(c, Value::ofInt(on && named && on == named ? 1 : 0));
    }
    case 524: { // 0x1310b8: an actor onto the removal list {name}
        if (n < 1) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        removeActor(*a);
        return result(c, Value::ofInt(1));
    }
    case 525: { // 0x135a34: whether an actor could step {name, from row, from column, to row, to column}
        if (n < 5) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        float h = 0.0f;
        const bool ok = a && canStep(*a, {c.integer(first + 1), c.integer(first + 2)},
                                     {c.integer(first + 3), c.integer(first + 4)}, h);
        return result(c, Value::ofInt(ok ? 1 : 0));
    }
    case 526: { // 0x1504d4: a mummy that noticed Layton {-> its name hash}
        if (n < 1) {
            return false;
        }
        for (const auto& a : actors_) {
            if (a && a->klass == 3 && a->noticed) {
                put(c, first, Value::ofUint(a->hash));
                return result(c, Value::ofInt(1));
            }
        }
        put(c, first, Value::ofUint(0));
        return result(c, Value::ofInt(0));
    }
    case 527: { // 0x155f6c: a mummy's +0x2e4 {name, flag}
        if (n < 2) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a || a->klass != 3) {
            return false;
        }
        a->checking = c.at(first + 1).truthy();
        return true;
    }
    case 603: { // 0x1d2fa4: an actor faces a direction {name, x, z, frames}: 1 when it does
        if (n < 4) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        const float dx = c.real(first + 1), dz = c.real(first + 2);
        float turn = 0.0f;
        if (auto* o = game_.scene.object3D(a->hash)) {
            turn = o->rotation3[1];
        }
        const auto degrees = [](float t) { return int(t * 0.159154937f * 360.0f + 360.5f) % 360; };
        if (degrees(turn) == degrees(heading(dx, dz))) {
            return result(c, Value::ofInt(1));
        }
        if (!a->idle) {
            const CellIndex at = cellOf(*a);
            a->state = 3;
            a->settled = false;
            a->target = at;
            a->dest = at;
            a->pending.facing = {dx, 0.0f, dz};
            a->pending.faceFrames = c.real(first + 3);
        }
        return result(c, Value::ofInt(0));
    }
    case 606: { // 0x135678 -> 0x1d3e00: Randall to the decoration group on a cell
                // {name, step frames, turn frames}
        if (n < 3) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        walkToGroup(*a, cellOf(*a), c.real(first + 2), c.real(first + 1));
        return true;
    }
    case 610: { // 0x142758 -> 0x1cab1c: a mummy notices Layton (22), facing his goal
        if (n < 1) {
            return false;
        }
        Actor* m = actor(textOf(c, first));
        Actor* l = actor(kLayton);
        if (!m || m->klass != 3 || !l) {
            return false;
        }
        const CellIndex at = cellOf(*m);
        m->state = 22;
        m->settled = true;
        m->target = at;
        m->dest = at;
        float x = float(l->dest.column - at.column), z = float(l->dest.row - at.row);
        const float len = x * x + z * z;
        if (len <= 0.0f) {
            x = z = 0.0f;
        } else {
            const float k = 1.0f / std::sqrt(len);
            x *= k;
            z *= k;
        }
        m->pending.facing = {x, 0.0f, z};
        m->pending.faceFrames = 5.0f;
        return true;
    }
    case 611: { // 0x1357f4: a mummy looks around (23)
        if (n < 1) {
            return false;
        }
        Actor* m = actor(textOf(c, first));
        if (!m || m->klass != 3) {
            return false;
        }
        const CellIndex at = cellOf(*m);
        m->state = 23;
        m->settled = true;
        m->target = at;
        m->dest = at;
        return true;
    }
    case 612:   // 0x135724: Layton caught (8) at once, idle
    case 615: { // 0x1426ac: a mummy runs at him (25) at once, idle
        if (n < 1 && number == 615) {
            return false;
        }
        Actor* a = number == 612 ? actor(kLayton) : actor(textOf(c, first));
        if (!a || a->klass != (number == 612 ? 1 : 3)) {
            return result(c, Value::ofInt(0));
        }
        const CellIndex at = cellOf(*a);
        a->state = number == 612 ? 8 : 25;
        a->settled = false;
        a->target = at;
        a->dest = at;
        a->action = -1;
        beginPending(*a);
        a->idle = true;
        return result(c, Value::ofInt(1));
    }
    case 613: { // 0x158f64 -> 0x1d39e0: a block (event kind 200 by number) pushed
                // {number, row step, column step, pusher}
        if (n < 4) {
            return false;
        }
        Actor* b = blockOf(c.integer(first));
        if (!b || b->klass != 4) {
            return false;
        }
        if (!b->idle && b->action != 9) {
            const CellIndex at = cellOf(*b);
            const CellIndex d{c.integer(first + 1), c.integer(first + 2)};
            b->state = 9;
            b->settled = true;
            b->target = at;
            b->dest = {at.row + d.row, at.column + d.column};
            b->pending.step = d;
            b->steps = 0;
            b->pushedBy = actor(textOf(c, first + 3));
        }
        return result(c, Value::ofInt(1));
    }
    case 614: { // 0x15f404: a block (by number) busy
        if (n < 1) {
            return false;
        }
        Actor* b = blockOf(c.integer(first));
        if (!b) {
            return false;
        }
        return result(c, Value::ofInt(b->action != -1 && b->action != 0 ? 1 : 0));
    }
    case 503: { // 0x1312bc: an actor to a cell {name, row, column}
        if (n < 3) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        place(*a, {c.integer(first + 1), c.integer(first + 2)});
        return true;
    }
    case 504: { // 0x131230: an actor's cell {name, -> row, -> column}
        if (n < 3) {
            return false;
        }
        Actor* a = actor(textOf(c, first));
        if (!a) {
            return false;
        }
        const CellIndex at = cellOf(*a);
        put(c, first + 1, Value::ofInt(at.row));
        put(c, first + 2, Value::ofInt(at.column));
        return true;
    }
    case 800: { // 0x12b794: a parts archive {slot, path}
        if (n < 2) {
            return false;
        }
        const int slot = c.integer(first);
        if (slot < 0 || slot >= int(archives_.size())) {
            return false;
        }
        std::string path = textOf(c, first + 1);
        if (const auto dot = path.rfind(".xb"); dot != std::string::npos) {
            archives_[std::size_t(slot)] = path; // the archive is a folder of the pack
        }
        return result(c, Value::ofInt(1));
    }
    case 801: { // 0x140d4c: the archive is loaded (its state 2)
        if (n < 1) {
            return false;
        }
        const int slot = c.integer(first);
        return result(c, Value::ofInt(slot >= 0 && slot < int(archives_.size()) &&
                                              !archives_[std::size_t(slot)].empty()
                                          ? 1
                                          : 0));
    }
    case 802: { // 0x1d1eac: a model resource from an archive {slot, name, file}
        if (n < 3) {
            return false;
        }
        auto model = modelFromArchive(c.integer(first), textOf(c, first + 2));
        if (model) {
            adventure56::Resource r;
            r.kind = adventure56::Resource::Kind::Model;
            r.path = model->path;
            r.model = model;
            game_.scene.addResource(textOf(c, first + 1), std::move(r));
        }
        return true;
    }
    case 803: { // 0x130f50: the archive goes
        if (n < 1) {
            return false;
        }
        const int slot = c.integer(first);
        if (slot >= 0 && slot < int(archives_.size())) {
            archives_[std::size_t(slot)].clear();
        }
        return result(c, Value::ofInt(1));
    }
    default:
        break;
    }
    // Not ported yet: counted with the missing services (2110000 + the
    // command) for the research runners.
    ++game_.missing[2110000 + number];
    return false;
}

} // namespace

RuinsInspector* ruinsInspector(Loop& loop) {
    return dynamic_cast<RuinsInspector*>(&loop);
}

std::unique_ptr<Loop> makeRuinsLoop(Story& story, int id, std::vector<Value> values) {
    return std::make_unique<RuinsLoop>(story, id, std::move(values));
}

} // namespace picarat::adventure5
