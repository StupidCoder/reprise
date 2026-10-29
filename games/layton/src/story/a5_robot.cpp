// The robot game (loop 6100, lt5CLoopMiniRobot; creator 0x37d008, vtable
// 0x401c78: start 0x207f30, commands 0x207f7c, frame end 0x207a30, reset
// 0x20d100).
//
// A script loop (seq/mini_robot) over a board it cannot hold itself: piece
// types and stages from res/sub/lt5_sub_robot.xs, one list per stage from
// res/sub/lt5_sub_robot_stg.xb (pieces, enemy routes, the minimap's
// points), up to 256 pieces on two 16 x 16 grids, a move that brings pieces
// from one cell to the next over a number of frames and tells them when they
// meet, and a history 32 turns deep for undoing. A piece's behaviour is its
// type's script blocks (START, PRE_MOVE, MOVE, HIT, IDLE, PRE_MEV, END_MEV),
// which the board calls on the piece's 2D object's handler thread. The
// script draws and moves the objects; the commands (2110, vtable +0x4c,
// a table of 68 at RAM 0x443bec) are below.
// Docs: .analysis/adventure56/docs/robot.md.
#include "story/a5_robot.hpp"
#include "import/a56_formats.hpp"
#include "system/a5_game.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

namespace picarat::adventure5 {

namespace {

using xq::Value;

constexpr std::uint16_t kNone = 0xffff;
constexpr int kGrid = 16;      // a grid's side (16 x 16 cells, x major)
constexpr int kPieces = 256;   // B+0x24 .. B+0xf024
constexpr int kPairs = 64;     // +0x158cc
constexpr int kHistory = 32;   // turns kept per piece
constexpr int kRoutes = 64;    // B+0xfc28
constexpr int kPoints = 256;   // B+0xfa28
constexpr int kListMax = 256;  // B+0xf424 (commands 30..33)

// The two kinds of an 8017 file name (the table at RAM 0x20ce7c points to;
// the order is a hypothesis from the strings' order at 0x366960 / 0x366978:
// the script loads list 1 as models, 3702).
const std::string kFileKinds[2] = {
    "\x32\x44\x83\x41\x83\x6a\x83\x81\x20\x83\x74\x83\x40\x83\x43\x83\x8b\x96\xbc", // 2D animation
    "\x33\x44\x83\x4c\x83\x83\x83\x89\x20\x83\x74\x83\x40\x83\x43\x83\x8b\x96\xbc", // 3D model
};

struct Cell {
    std::int8_t x = 0, y = 0;
    bool operator==(const Cell&) const = default;
};

// A piece type (8010; 0x88 bytes from loop +0x10ea8): the type name and up
// to 16 values: the blocks START, PRE_MOVE, MOVE, HIT, IDLE, PRE_MEV,
// END_MEV and the display name its model is found by.
struct PieceType {
    std::uint32_t hash = 0;
    std::string name;
    std::array<Value, 16> values{};
};
enum Block {
    Start = 0,
    PreMove = 1,
    Move = 2,
    Hit = 3,
    Idle = 4,
    PreMev = 5,
    EndMev = 6,
    Display = 7,
};

// A stage (8011; 0x18 bytes from +0x2204 of the tables).
struct StageRecord {
    std::uint32_t name = 0, edge = 0, set = 0, goal = 0; // hashes
    std::uint8_t number = 0, width = 0, height = 0, fuel = 0, a = 0, b = 0;
};

// A model set (8012 .. 8014): display names to model names.
struct ModelSet {
    std::uint32_t name = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs; // up to 32
};

// A model (8015): its name and two further names (the second the file).
struct ModelFile {
    std::array<std::uint32_t, 3> names{};
};

// A stage set's files to load (8016 .. 8018): list 0 2D animations, list 1
// 3D models.
struct FileLists {
    std::uint32_t name = 0;
    std::array<std::vector<std::string>, 2> lists; // up to 32 each
};

// A piece (0xf0 bytes from loop +0x184; defaults 0x2876e4).
struct Piece {
    std::uint32_t type = 0;  // +0x00
    std::uint32_t route = 0; // +0x04
    Cell start;              // +0x08 (its objects' names)
    Cell from;               // +0x0a: where the step starts
    Cell to;                 // +0x0c: where it ends
    Cell marked;             // +0x0e: where the board last marked it
    float unknown10 = 0;     // +0x10: no command writes it (201 reads it)
    std::int8_t ox = 0, oy = 0, width = 1, height = 1; // +0x14: the footprint
    float radius = 0;        // +0x18
    std::array<Value, 8> values{}; // +0x1c: 8000's further operands
    std::array<Cell, kHistory> history; // +0x5c
    // +0x9c..+0xdd: three bytes (commands 112 / 221) over the histories
    // of the first two (+0x9e.., +0xbe..): the third byte is the first
    // history entry of the first, as in the original.
    std::array<std::uint8_t, 0x42> bytes{};
    std::uint32_t mergeSet = 0, mergeCell = 0; // +0xe0 / +0xe4 (113)
    std::uint8_t ring = 0;      // +0xe8: the history index
    std::uint8_t countdown = 0; // +0xe9
    std::uint8_t order = 0xff;  // +0xea: the update order key
    std::uint8_t layer = 0xff;  // +0xeb: 1 grid A, else grid B; 2 the player
    std::int8_t extra = -1;     // +0xec (211)
    std::uint8_t flags = 3;     // +0xed: bit 0 on the board, bit 1 updated
    std::uint8_t nameKind = 0;  // +0xee
    std::uint8_t direction = 0; // +0xef low nibble
    bool merged = false;        // +0xef bit 4: drawn into a merge object
    bool shown = false;         // +0xef bit 5: its 2D object's value 11

    Piece() { history.fill({-1, -1}); }

    std::uint8_t& historyA(int i) { return bytes[std::size_t(2 + i)]; }  // +0x9e + i
    std::uint8_t& historyB(int i) { return bytes[std::size_t(34 + i)]; } // +0xbe + i
};

// An enemy's route (8005 .. 8007; 20 bytes from B+0xfc28).
struct Route {
    std::uint32_t hash = 0;
    std::array<std::int8_t, 15> steps{};
    std::uint8_t count = 0;

    Route() { steps.fill(-1); }
};

// Two pieces that meet in a move (+0x158cc, 12 bytes).
struct Pair {
    std::uint16_t a = kNone, b = kNone;
    std::uint8_t cellA = 0, cellB = 0, flag = 0;
    bool done = false;
};

// A merge object (command 40; B+0x10d28): pieces of a set drawn into it.
struct Merge {
    std::uint32_t object = 0, resource = 0;
};

using Grid = std::array<std::uint16_t, kGrid * kGrid>;

Value valueOf(const import::adventure56::DataList& list, const import::adventure56::Operand& o) {
    switch (o.type & 15) {
    case 1:
        return Value::ofInt(std::int32_t(o.value));
    case 2:
        return Value::ofUint(o.value);
    case 3: {
        float f;
        std::memcpy(&f, &o.value, sizeof f);
        return Value::ofFloat(f);
    }
    case 8: {
        if (o.value >= list.strings.size()) {
            return Value::ofText({});
        }
        const auto* s = reinterpret_cast<const char*>(list.strings.data() + o.value);
        return Value::ofText(std::string(s, strnlen(s, list.strings.size() - o.value)));
    }
    default:
        return {};
    }
}

std::string textOfValue(const Value& v) { return v.isText() ? v.text : std::string(); }

std::string resName(std::uint32_t hash) {
    char s[16];
    std::snprintf(s, sizeof s, "RES_%08X", hash);
    return s;
}

class RobotLoop final : public ScriptLoop {
public:
    RobotLoop(Story& story, int id, std::vector<Value> values)
        : ScriptLoop(story, id, std::move(values), "mini_robot") {
        resetBoard();
    }

    bool command(int number, xq::Call& c, std::size_t first) override;

    void afterFrame() override {
        // 0x207a30: the base end of the frame, then the move's frame, the
        // pieces' objects shown by their state and a requested ControlIntr.
        ScriptLoop::afterFrame();
        frameDelta_ = 1.0f; // the frame's step (60 Hz frames)
        if (running_ && !paused_) {
            stepMove();
        }
        showPieces();
        if (controlRequest_ && threads[0]) {
            const std::array<Value, 1> args{Value::ofUint(turns_)};
            threads[0]->callback("ControlIntr", args);
            controlRequest_ = false;
        }
    }

private:
    // ---- the tables (command 13)
    void loadTables(const std::string& path);
    const PieceType* typeOf(std::uint32_t hash) const {
        for (const auto& t : types_) {
            if (t.hash == hash) {
                return &t;
            }
        }
        return nullptr;
    }
    const StageRecord* stageOf(int number) const {
        for (const auto& s : stages_) {
            if (s.number == std::uint8_t(number)) {
                return &s;
            }
        }
        return nullptr;
    }
    const FileLists* filesOf(std::uint32_t name) const {
        for (const auto& f : files_) {
            if (f.name == name) {
                return &f;
            }
        }
        return nullptr;
    }
    std::optional<std::string> modelName(std::size_t piece, int stage, int which) const;

    // ---- the board (B = loop +0x160)
    void resetBoard();
    bool addPiece(const Piece& p);
    bool removePiece(std::uint32_t id);
    bool loadStage(const std::string& file);
    Grid& gridOf(const Piece& p) { return p.layer == 1 ? gridA_ : gridB_; }
    static Cell footprintCell(const Piece& p, Cell at, int k) {
        return {std::int8_t(at.x + p.ox + k % std::max<int>(1, p.width)),
                std::int8_t(at.y + p.oy + k / std::max<int>(1, p.width))};
    }
    static bool inGrid(int x, int y) { return x >= 0 && x < kGrid && y >= 0 && y < kGrid; }
    static std::uint16_t& cellOf(Grid& g, int x, int y) { return g[std::size_t(x * kGrid + y)]; }
    void fill(Grid& g, const Piece& p, std::uint16_t id, Cell at);
    void clearOwn(Grid& g, const Piece& p, std::uint16_t id, Cell at);
    void mark(std::uint16_t id);
    void markAll();
    std::uint16_t meets(std::uint32_t id, int board, int k, std::uint8_t* cell, bool any);
    void pairUp(std::uint16_t id, int flag);
    void sortUpdates();
    void rebuildUpdates();
    const Route* routeOf(std::uint32_t hash) const {
        for (const auto& r : routes_) {
            if (r.hash == hash) {
                return &r;
            }
        }
        return nullptr;
    }

    // ---- the move
    void startMove(int a, int b, int state, float frames);
    void stepMove();
    void clearPairs() {
        pairs_.fill({});
        pairCount_ = 0;
    }
    bool collide(const Pair& pair, float t);
    void meet(std::uint16_t a, std::uint16_t b);
    void turnEnd();
    void undo();

    // ---- the pieces' objects
    std::string objectName(const Piece& p, bool solid) const {
        char s[48];
        std::snprintf(s, sizeof s, solid ? "OBJ_3D_%02d_%02d_%02d" : "OBJ_2D_%02d_%02d_%02d",
                      int(p.nameKind), int(p.start.x), int(p.start.y));
        return s;
    }
    adventure56::Object* objectOf(std::size_t id, bool solid = false) {
        if (id >= pieces_.size()) {
            return nullptr;
        }
        return game_.scene.find(solid, xq::hash(objectName(pieces_[id], solid)));
    }
    // A type block called on the piece's 2D object (when it is shown).
    void callBlock(std::size_t id, Block block, std::span<const Value> args);
    void buildDrawList();
    void showPieces();
    void rebuildMerges();
    void drawMinimap(const std::string& name, std::uint32_t set);

    // ---- commands' operands
    static std::size_t operands(const xq::Call& c, std::size_t first) {
        return c.count() > first ? c.count() - first : 0;
    }
    static void put(xq::Call& c, std::size_t i, Value v) {
        if (Value* r = i < c.count() ? c.reg(i) : nullptr) {
            *r = std::move(v);
        }
    }
    static bool result(xq::Call& c, std::int32_t v) {
        c.result = Value::ofInt(v);
        return true;
    }
    static std::uint32_t hashOf(const xq::Call& c, std::size_t i) {
        const std::string* t = c.text(i);
        return xq::hash(t ? *t : std::string());
    }
    Piece* piece(const xq::Call& c, std::size_t i) {
        const std::uint32_t id = c.unsignedInt(i);
        return id < pieces_.size() ? &pieces_[id] : nullptr;
    }

    std::vector<PieceType> types_;    // T+0 (count T+0x2200, up to 64)
    std::vector<StageRecord> stages_; // T+0x2204 (up to 64)
    std::vector<ModelSet> sets_;      // T+0x2808 (up to 16)
    std::vector<ModelFile> models_;   // T+0x388c (up to 16)
    std::vector<FileLists> files_;    // T+0x3950 (up to 16)
    bool tablesLoaded_ = false;
    std::string stageFolder_;         // the .xb's folder (+0x15bf4)

    std::vector<Piece> pieces_;            // B+0x24 (count B+0)
    std::array<std::uint16_t, kPieces> updates_{}; // U: B+0xf224 (count B+4)
    std::array<std::uint16_t, kPieces> sorted_{};  // S: B+0xf024 (U sorted, U's count)
    int updateCount_ = 0;
    std::int8_t width_ = 0, height_ = 0;   // B+0x0c / +0x0d: the stage's size + 2
    std::uint16_t player_ = kNone;         // B+0x14
    bool afterBelt_ = false;               // B+0x12 (+0x172): a conveyor move happened
    std::uint8_t undos_ = 0;               // B+0x18 (+0x178)
    std::uint8_t fuel_ = 0;                // B+0x19 (+0x179)
    std::uint8_t turns_ = 0;               // B+0x1a (+0x17a)
    std::vector<std::uint16_t> list_;      // B+0xf424 (count B+0x1b; 30..33)
    std::vector<Merge> merges_;            // B+0x10d28 (count B+0x1c, up to 4)
    std::vector<Route> routes_;            // B+0xfc28 (count B+8)
    std::size_t openRoute_ = 0;
    std::vector<Cell> points_;             // B+0xfa28 (8009)
    Grid gridA_{}, gridB_{};               // B+0xf624 / B+0xf824
    std::vector<std::uint16_t> drawList_;  // B+0x10128 (command 3; count B+0x1e)
    int stageNumber_ = 0;                  // +0x15bfc

    // The move (+0x158c0 ..).
    float frameDelta_ = 1.0f, elapsed_ = 0, duration_ = 0;
    bool running_ = false, paused_ = false; // +0x15bcc / +0x15bd0
    bool hitsDone_ = false, countdownDone_ = false, repaired_ = false; // +0x15bd4 / d8 / dc
    bool controlRequest_ = false;          // +0x15be4
    std::uint8_t moveA_ = 0, moveB_ = 0, state_ = 0; // +0x15be5 .. 7
    std::array<Pair, kPairs> pairs_{};
    int pairCount_ = 0;                    // +0x15be8
};

void RobotLoop::loadTables(const std::string& path) {
    // 0x20d3b4 over the list with the parsers at RAM 0x443f64.
    std::string stem = path;
    if (stem.size() > 3 && stem.substr(stem.size() - 3) == ".xs") {
        stem.resize(stem.size() - 3);
    }
    const auto file = game_.pack.find(stem, ".xs");
    if (file.empty()) {
        return;
    }
    const auto list = import::adventure56::parseXscr(game_.pack.read(file));
    std::optional<ModelSet> openSet;
    std::optional<FileLists> openFiles;
    for (const auto& c : list.commands) {
        std::vector<Value> op;
        for (unsigned i = 0; i < c.count; ++i) {
            op.push_back(valueOf(list, list.operands[c.first + i]));
        }
        auto hashAt = [&op](std::size_t i) {
            return i < op.size() ? xq::hash(textOfValue(op[i])) : 0u;
        };
        auto byteAt = [&op](std::size_t i) {
            return i < op.size() ? std::uint8_t(op[i].toUint()) : std::uint8_t(0);
        };
        switch (c.opcode) {
        case 8010: { // 0x20ce80 {type name, up to 16 values}
            if (types_.size() >= 64 || op.empty() || op.size() > 16 + 1 || !op[0].isText()) {
                break;
            }
            PieceType t;
            t.hash = xq::hash(op[0].text);
            t.name = op[0].text;
            for (std::size_t i = 1; i < op.size(); ++i) {
                const auto k = op[i].kind;
                if (k == Value::Kind::Int || k == Value::Kind::Uint || k == Value::Kind::Float ||
                    k == Value::Kind::Text) {
                    t.values[i - 1] = op[i];
                }
            }
            types_.push_back(std::move(t));
            break;
        }
        case 8011: { // 0x20cff4 {number, name, w, h, fuel, edge, a, b, set, goal}
            if (stages_.size() >= 64 || op.size() < 10) {
                break;
            }
            StageRecord s;
            s.number = byteAt(0);
            s.name = hashAt(1);
            s.width = byteAt(2);
            s.height = byteAt(3);
            s.fuel = byteAt(4);
            s.edge = hashAt(5);
            s.a = byteAt(6);
            s.b = byteAt(7);
            s.set = hashAt(8);
            s.goal = hashAt(9);
            stages_.push_back(s);
            break;
        }
        case 8012: // 0x13557c {set name}: a set opens
            if (sets_.size() < 16 && !op.empty()) {
                openSet = ModelSet{hashAt(0), {}};
            }
            break;
        case 8013: // 0x1274e4 {display name, model name}
            if (openSet && op.size() >= 2 && openSet->pairs.size() < 32) {
                openSet->pairs.emplace_back(hashAt(0), hashAt(1));
            }
            break;
        case 8014: // 0x135620: the set closes (counted when named)
            if (openSet && openSet->name != 0 && sets_.size() < 16) {
                sets_.push_back(*openSet);
            }
            openSet.reset();
            break;
        case 8015: // 0x127428 {model name, name, file}
            if (models_.size() < 16 && op.size() >= 3) {
                models_.push_back({{hashAt(0), hashAt(1), hashAt(2)}});
            }
            break;
        case 8016: // 0x14092c {set name}: a file list opens
            if (files_.size() < 16 && !op.empty()) {
                openFiles = FileLists{hashAt(0), {}};
            }
            break;
        case 8017: { // 0x20cd8c {kind, file}
            if (!openFiles || op.size() < 2) {
                break;
            }
            const std::uint32_t kind = hashAt(0);
            for (int k = 0; k < 2; ++k) {
                auto& files = openFiles->lists[std::size_t(k)];
                if (kind == xq::hash(kFileKinds[k]) && files.size() < 32) {
                    files.push_back(textOfValue(op[1]));
                }
            }
            break;
        }
        case 8018: // 0x1409f8: the list closes (counted when named)
            if (openFiles && openFiles->name != 0 && files_.size() < 16) {
                files_.push_back(*openFiles);
            }
            openFiles.reset();
            break;
        default:
            break;
        }
    }
    tablesLoaded_ = true;
}

std::optional<std::string> RobotLoop::modelName(std::size_t id, int stage, int which) const {
    // 0x348d54: the piece type's display name -> the stage's set -> the
    // model name -> its model record -> "RES_%08X" of the record's name 1
    // or 2.
    if (id >= pieces_.size() || which >= 2) {
        return std::nullopt;
    }
    const PieceType* t = typeOf(pieces_[id].type);
    const StageRecord* s = stageOf(stage);
    if (!t || !s) {
        return std::nullopt;
    }
    const std::uint32_t display = xq::hash(textOfValue(t->values[Display]));
    for (const auto& set : sets_) {
        if (set.name != s->set) {
            continue;
        }
        for (const auto& [shown, model] : set.pairs) {
            if (shown != display) {
                continue;
            }
            for (const auto& m : models_) {
                if (m.names[0] == model) {
                    return resName(m.names[std::size_t(which + 1)]);
                }
            }
            return std::nullopt;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

void RobotLoop::resetBoard() {
    // 0x392fe0 (command 2).
    pieces_.clear();
    updates_.fill(kNone);
    sorted_.fill(kNone);
    updateCount_ = 0;
    player_ = kNone;
    afterBelt_ = false;
    undos_ = fuel_ = turns_ = 0;
    list_.clear();
    merges_.clear();
    routes_.clear();
    openRoute_ = 0;
    points_.clear();
    gridA_.fill(kNone);
    gridB_.fill(kNone);
    drawList_.clear();
}

bool RobotLoop::addPiece(const Piece& p) {
    // 0x20ccfc: inside the board, and not a second piece of the player's
    // type.
    if (pieces_.size() >= kPieces) {
        return false;
    }
    if (player_ != kNone && player_ < pieces_.size() && pieces_[player_].type == p.type) {
        return false;
    }
    if (p.start.x >= width_ || p.start.y >= height_) {
        return false;
    }
    pieces_.push_back(p);
    return true;
}

bool RobotLoop::removePiece(std::uint32_t id) {
    // 0x20c7e4 (command 110). The original lets id == count through; here
    // that is refused (no script asks for it).
    if (id >= pieces_.size()) {
        return false;
    }
    if (player_ == id) {
        player_ = kNone;
    }
    if (player_ != kNone && id < player_) {
        --player_;
    }
    if (const std::uint32_t r = pieces_[id].route; r != 0) {
        const auto it = std::find_if(routes_.begin(), routes_.end(),
                                     [r](const Route& x) { return x.hash == r; });
        if (it != routes_.end()) {
            routes_.erase(it);
        }
    }
    pieces_.erase(pieces_.begin() + std::ptrdiff_t(id));
    gridA_.fill(kNone);
    gridB_.fill(kNone);
    markAll();
    // The sorted list is made from the update list before it is rebuilt
    // (so it keeps the old ids until command 15 sorts again), as in the
    // original.
    sortUpdates();
    rebuildUpdates();
    return true;
}

bool RobotLoop::loadStage(const std::string& name) {
    // 0x20d3b4 with the stage file parsers (RAM 0x443f1c) over a file of
    // the .xb folder.
    std::string stem = stageFolder_ + name;
    if (stem.size() > 3 && stem.substr(stem.size() - 3) == ".xs") {
        stem.resize(stem.size() - 3);
    }
    const auto file = game_.pack.find(stem, ".xs");
    if (file.empty()) {
        return false;
    }
    const auto list = import::adventure56::parseXscr(game_.pack.read(file));
    for (const auto& c : list.commands) {
        std::vector<Value> op;
        for (unsigned i = 0; i < c.count; ++i) {
            op.push_back(valueOf(list, list.operands[c.first + i]));
        }
        switch (c.opcode) {
        case 8000: { // 0x20c430 {type, x, y, route [, values]}
            if (op.size() < 4) {
                break;
            }
            Piece p;
            p.type = xq::hash(textOfValue(op[0]));
            const Cell at{std::int8_t(op[1].toInt()), std::int8_t(op[2].toInt())};
            p.start = p.from = p.to = p.marked = at;
            p.history[0] = at;
            p.route = op[3].toUint();
            // Up to 13 further operands in the original (past the eight
            // values into the history); the stages have none.
            for (std::size_t i = 4; i < op.size() && i - 4 < p.values.size(); ++i) {
                p.values[i - 4] = op[i];
            }
            addPiece(p);
            break;
        }
        case 8001: // 0x122bd8 {w, h} (no stage uses it)
            if (op.size() >= 2 && op[0].toInt() * op[1].toInt() <= 256) {
                width_ = std::int8_t(op[0].toInt());
                height_ = std::int8_t(op[1].toInt());
            }
            break;
        case 8005: // 0x121628 {route}: a route opens
            if (routes_.size() < kRoutes && !op.empty()) {
                Route r;
                r.hash = op[0].toUint();
                routes_.push_back(r);
                openRoute_ = routes_.size() - 1;
            }
            break;
        case 8006: // 0x165eac {direction}: a step of the open route
            if (openRoute_ < routes_.size() && !op.empty()) {
                Route& r = routes_[openRoute_];
                if (r.count < r.steps.size()) {
                    r.steps[r.count++] = std::int8_t(op[0].toInt());
                }
            }
            break;
        case 8007: // 0x121694: the route closes
            openRoute_ = routes_.size();
            break;
        case 8008: // 0x165f00 {fuel} (no stage uses it)
            if (!op.empty()) {
                fuel_ = std::uint8_t(op[0].toUint());
            }
            break;
        case 8009: // 0x1245e0 {x, y}: a minimap point
            if (points_.size() < kPoints && op.size() >= 2) {
                points_.push_back({std::int8_t(op[0].toInt()), std::int8_t(op[1].toInt())});
            }
            break;
        default: // 8002 / 8003 (B+0x0e.., B+0x10..): no stage uses them
            break;
        }
    }
    return true;
}

void RobotLoop::fill(Grid& g, const Piece& p, std::uint16_t id, Cell at) {
    // 0x287278: the footprint at `at` set to the piece.
    for (int ix = p.ox; ix < p.ox + p.width; ++ix) {
        for (int iy = p.oy; iy < p.oy + p.height; ++iy) {
            const int x = at.x + ix, y = at.y + iy;
            if (inGrid(x, y)) {
                cellOf(g, x, y) = id;
            }
        }
    }
}

void RobotLoop::clearOwn(Grid& g, const Piece& p, std::uint16_t id, Cell at) {
    // 0x2871d0: the footprint's cells at `at` that hold the piece cleared.
    for (int ix = p.ox; ix < p.ox + p.width; ++ix) {
        for (int iy = p.oy; iy < p.oy + p.height; ++iy) {
            const int x = at.x + ix, y = at.y + iy;
            if (inGrid(x, y) && cellOf(g, x, y) == id) {
                cellOf(g, x, y) = kNone;
            }
        }
    }
}

void RobotLoop::mark(std::uint16_t id) {
    // 0x287124: off the step's start, onto its end (while bit 0 is set).
    if (id >= pieces_.size()) {
        return;
    }
    Piece& p = pieces_[id];
    Grid& g = gridOf(p);
    clearOwn(g, p, id, p.from);
    p.marked = p.to;
    if (p.flags & 1) {
        fill(g, p, id, p.to);
    } else {
        clearOwn(g, p, id, p.to);
    }
}

void RobotLoop::markAll() {
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        mark(std::uint16_t(i));
    }
}

std::uint16_t RobotLoop::meets(std::uint32_t id, int board, int k, std::uint8_t* cell, bool any) {
    // 0x348ff0: the piece another occupies at cell k of this piece's
    // target footprint (grid A for board 1, else grid B), when that one
    // comes the other way (they swap) or goes to the same cell (or with
    // `any`, whatever it does); its own cell index in `cell`.
    if (id >= pieces_.size() || !(pieces_[id].flags & 1)) {
        return kNone;
    }
    const Piece& p = pieces_[id];
    Cell target = p.to, current = p.from;
    if (k != 0 || p.ox != 0 || p.oy != 0) {
        target = footprintCell(p, p.to, k);
        current = footprintCell(p, p.from, k);
    }
    if (!inGrid(target.x, target.y)) {
        return kNone;
    }
    const std::uint16_t other = cellOf(board == 1 ? gridA_ : gridB_, target.x, target.y);
    if (other == id || other == kNone || other >= pieces_.size()) {
        return kNone;
    }
    const Piece& o = pieces_[other];
    const int cells = int(o.width) * int(o.height);
    for (int j = 0; j < cells; ++j) {
        const Cell oc = footprintCell(o, o.from, j);
        const Cell ot{std::int8_t(oc.x + (o.to.x - o.from.x)),
                      std::int8_t(oc.y + (o.to.y - o.from.y))};
        if ((target == oc && ot == current) || ot == target || any) {
            if (cell) {
                *cell = std::uint8_t(j);
            }
            return other;
        }
    }
    return kNone;
}

void RobotLoop::pairUp(std::uint16_t id, int flag) {
    // 0x287400: every piece this one meets at a cell of its footprint,
    // paired once; with flag 0 a piece that meets none is marked.
    if (id == kNone || id >= pieces_.size()) {
        return;
    }
    bool alone = flag == 0;
    const Piece& p = pieces_[id];
    const int cells = int(p.width) * int(p.height);
    for (int k = 0; k < cells; ++k) {
        std::uint8_t cell = 0;
        const std::uint16_t other = meets(id, flag, k, &cell, false);
        if (other == kNone || pairCount_ >= kPairs) {
            continue;
        }
        alone = false;
        bool known = false;
        for (int i = 0; i < pairCount_; ++i) {
            const Pair& q = pairs_[std::size_t(i)];
            if ((q.a == id && q.b == other) || (q.a == other && q.b == id)) {
                known = true;
                break;
            }
        }
        if (known) {
            continue;
        }
        pairs_[std::size_t(pairCount_++)] =
            Pair{id, other, std::uint8_t(k), cell, std::uint8_t(flag), false};
    }
    if (alone) {
        mark(id);
    }
}

void RobotLoop::sortUpdates() {
    // 0x20c6ac: S = U, shell-sorted (gaps 3h + 1, halved) by the order
    // key; equal keys trade places, as in the original.
    const int n = updateCount_;
    for (int i = 0; i < n; ++i) {
        sorted_[std::size_t(i)] = updates_[std::size_t(i)];
    }
    auto key = [this](std::uint16_t id) {
        return id < pieces_.size() ? pieces_[id].order : std::uint8_t(0);
    };
    int h = 0;
    for (int next = 1; next <= n; next = h * 3 + 1) {
        h = next;
    }
    for (; h >= 1; h >>= 1) {
        for (int s = 0; s < h; ++s) {
            for (int j = s; j < n; j += h) {
                const std::uint8_t v = key(sorted_[std::size_t(j)]);
                for (int k = j - h; k >= s; k -= h) {
                    if (key(sorted_[std::size_t(k)]) < v) {
                        break;
                    }
                    std::swap(sorted_[std::size_t(k)], sorted_[std::size_t(k + h)]);
                }
            }
        }
    }
}

void RobotLoop::rebuildUpdates() {
    // 0x20b79c: U = the pieces with bit 1, in index order.
    updateCount_ = 0;
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        if (pieces_[i].flags & 2) {
            updates_[std::size_t(updateCount_++)] = std::uint16_t(i);
        }
    }
}

void RobotLoop::callBlock(std::size_t id, Block block, std::span<const Value> args) {
    // 0x290f5c + 0x2877b8 on the piece's 2D object, while it is shown.
    if (id >= pieces_.size()) {
        return;
    }
    adventure56::Object* o = objectOf(id);
    const PieceType* t = typeOf(pieces_[id].type);
    if (!o || !o->visible || !t) {
        return;
    }
    game_.callObjectBlock(*o, textOfValue(t->values[std::size_t(block)]), args);
}

void RobotLoop::startMove(int a, int b, int state, float frames) {
    // 0x20d12c (command 18).
    running_ = true;
    hitsDone_ = countdownDone_ = repaired_ = false;
    moveA_ = std::uint8_t(a);
    moveB_ = std::uint8_t(b);
    state_ = std::uint8_t(state);
    duration_ = frames;
    elapsed_ = 0;
    pairCount_ = 0;
    // 0x2094f8: the MOVE blocks of the sorted update list (with b set,
    // only of the pieces still counting down).
    const int n = updateCount_;
    for (int i = 0; i < n; ++i) {
        const std::uint16_t id = sorted_[std::size_t(i)];
        if (id >= pieces_.size()) {
            continue;
        }
        if (moveB_ != 0 && pieces_[id].countdown == 0) {
            continue;
        }
        const std::array<Value, 4> args{Value::ofUint(id), Value::ofUint(moveA_),
                                        Value::ofUint(moveB_), Value::ofUint(state_)};
        callBlock(id, Move, args);
    }
    clearPairs();
    for (int i = 0; i < n; ++i) {
        pairUp(sorted_[std::size_t(i)], 0);
    }
}

bool RobotLoop::collide(const Pair& pair, float t) {
    // 0x20a9d0: both cells where the step has got to; a hit when closer
    // than the two radii.
    if (pair.a >= pieces_.size() || pair.b >= pieces_.size()) {
        return false;
    }
    auto at = [t](const Piece& p, int k) {
        const int w = std::max<int>(1, p.width);
        const float x = float(p.to.x) * t + float(p.from.x) * (1 - t) + float(k % w + p.ox);
        const float y = float(p.to.y) * t + float(p.from.y) * (1 - t) + float(k / w + p.oy);
        return std::array<float, 2>{x, y};
    };
    const Piece& a = pieces_[pair.a];
    const Piece& b = pieces_[pair.b];
    const auto pa = at(a, pair.cellA), pb = at(b, pair.cellB);
    const float dx = pa[0] - pb[0], dy = pa[1] - pb[1];
    const float r = a.radius + b.radius;
    if (dx * dx + dy * dy < r * r) {
        meet(pair.a, pair.b);
        return true;
    }
    return false;
}

void RobotLoop::meet(std::uint16_t a, std::uint16_t b) {
    // 0x207cb0: HIT on each with (itself, the other, the move's values).
    const std::array<Value, 5> first{Value::ofUint(a), Value::ofUint(b), Value::ofUint(moveA_),
                                     Value::ofUint(moveB_), Value::ofUint(state_)};
    callBlock(a, Hit, first);
    const std::array<Value, 5> second{Value::ofUint(b), Value::ofUint(a), Value::ofUint(moveA_),
                                      Value::ofUint(moveB_), Value::ofUint(state_)};
    callBlock(b, Hit, second);
}

void RobotLoop::stepMove() {
    // 0x208d78.
    if (state_ == 6) {
        // A conveyor: the player's object starts the belt's event.
        if (player_ >= pieces_.size() || pieces_[player_].layer != 2) {
            return;
        }
        const std::array<Value, 3> args{Value::ofUint(player_), Value::ofUint(moveA_),
                                        Value::ofUint(moveB_)};
        adventure56::Object* o = objectOf(player_);
        if (o && o->visible && !game_.handlerRunning(*o)) {
            game_.callObjectBlock(*o, "ConveyorStartEvent", args);
        }
        return;
    }
    if (state_ == 7) {
        return;
    }
    elapsed_ += frameDelta_;
    float t = 1.0f;
    if (duration_ != 0 && elapsed_ / duration_ < 1.0f) {
        t = elapsed_ / duration_;
    }
    const int n = updateCount_;
    if (t >= 1.0f) {
        if (!hitsDone_) {
            // The pairs that did not meet on the way meet at the end.
            for (int i = 0; i < pairCount_; ++i) {
                if (!pairs_[std::size_t(i)].done) {
                    meet(pairs_[std::size_t(i)].a, pairs_[std::size_t(i)].b);
                }
            }
            hitsDone_ = true;
        }
        if (!countdownDone_ && (state_ == 1 || state_ == 3)) {
            if (state_ == 1) {
                state_ = 5;
            }
            for (int i = 0; i < n; ++i) {
                const std::uint16_t id = sorted_[std::size_t(i)];
                if (id == kNone || id >= pieces_.size()) {
                    continue;
                }
                Piece& p = pieces_[id];
                if (p.countdown != 0) {
                    const std::uint8_t was = p.countdown--;
                    if (was == 1) {
                        if (!afterBelt_ || p.layer != 2) {
                            const std::array<Value, 2> args{Value::ofUint(id),
                                                            Value::ofUint(moveA_)};
                            callBlock(id, Idle, args);
                        }
                    } else {
                        state_ = 1;
                    }
                }
                if (id < pieces_.size() && (pieces_[id].flags & 1)) {
                    mark(id);
                    pieces_[id].from = pieces_[id].to;
                }
            }
            countdownDone_ = true;
        }
        if (!repaired_) {
            repaired_ = true;
            clearPairs();
            for (int i = 0; i < n; ++i) {
                pairUp(sorted_[std::size_t(i)], 1);
            }
        }
    }
    for (int i = 0; i < pairCount_; ++i) {
        Pair& p = pairs_[std::size_t(i)];
        if (p.done || (t < 1.0f && p.flag == 1)) {
            continue;
        }
        const bool hit = collide(p, t);
        if (state_ == 6) {
            // A HIT started a conveyor move.
            countdownDone_ = false;
            afterBelt_ = true;
            return;
        }
        pairs_[std::size_t(i)].done = hit;
    }
    if (state_ == 5) {
        state_ = 3;
    }
    if (elapsed_ >= duration_) {
        running_ = false;
        const std::uint8_t was = state_;
        state_ = 4;
        for (int i = 0; i < updateCount_; ++i) {
            const std::uint16_t id = sorted_[std::size_t(i)];
            if (id != kNone && id < pieces_.size() && pieces_[id].countdown != 0) {
                state_ = was;
            }
        }
        if (afterBelt_) {
            for (std::size_t id = 0; id < pieces_.size(); ++id) {
                if (pieces_[id].layer == 1) {
                    const std::array<Value, 1> args{Value::ofUint(std::uint32_t(id))};
                    callBlock(id, PreMev, args);
                }
            }
        }
    }
}

void RobotLoop::turnEnd() {
    // 0x208708 (command 19): after a conveyor, END_MEV for the pieces of
    // layer 1; every piece's history moves on.
    if (afterBelt_) {
        for (std::size_t id = 0; id < pieces_.size(); ++id) {
            if (pieces_[id].layer == 1) {
                const std::array<Value, 1> args{Value::ofUint(std::uint32_t(id))};
                callBlock(id, EndMev, args);
            }
        }
    }
    afterBelt_ = false;
    for (auto& p : pieces_) {
        p.ring = std::uint8_t((p.ring + 1) & (kHistory - 1));
        p.history[p.ring] = p.from;
        p.historyA(p.ring) = p.bytes[0];
        p.historyB(p.ring) = p.bytes[1];
    }
    ++undos_;
    ++turns_;
}

void RobotLoop::undo() {
    // 0x20d284 (command 5): a turn back.
    if (undos_ == 0) {
        return;
    }
    for (auto& p : pieces_) {
        p.ring = std::uint8_t((p.ring + kHistory - 1) & (kHistory - 1));
        p.from = p.to = p.history[p.ring];
        p.marked = p.from;
        p.bytes[0] = p.historyA(p.ring);
        p.bytes[1] = p.historyB(p.ring);
    }
    gridA_.fill(kNone);
    gridB_.fill(kNone);
    markAll();
    --undos_;
    --turns_;
}

void RobotLoop::buildDrawList() {
    // 0x209714 (command 3): the pieces whose model is loaded ("RES_..."
    // of the stage's model files), once per model part in the original
    // and sorted by part and piece (0x20c578); here once per piece in
    // piece order (the list only decides which pieces' objects the frame
    // end shows).
    drawList_.clear();
    for (std::size_t id = 0; id < pieces_.size(); ++id) {
        const auto name = modelName(id, stageNumber_, 1);
        if (name && game_.scene.resource(*name)) {
            drawList_.push_back(std::uint16_t(id));
        }
    }
    rebuildMerges();
}

void RobotLoop::showPieces() {
    // 0x207a30 / 0x208360: a listed piece's 2D object is drawn while its
    // value 11 is set (one drawn into a merge object is not, and a change
    // of its value rebuilds the merges); its 3D twin, which the original
    // draws itself (the +0x5c hook), while its value 15 is set.
    bool changed = false;
    for (const std::uint16_t id : drawList_) {
        if (id >= pieces_.size()) {
            continue;
        }
        Piece& p = pieces_[id];
        if (adventure56::Object* o = objectOf(id)) {
            const bool on = game_.objectValue(*o, 11).truthy();
            if (!p.merged) {
                o->drawn = on;
            } else {
                o->drawn = false;
                changed = changed || on != p.shown;
            }
            p.shown = on;
        }
        if (adventure56::Object* twin = objectOf(id, true)) {
            twin->drawn = twin->visible && game_.objectValue(*twin, 15).truthy();
        }
    }
    if (changed) {
        rebuildMerges();
    }
}

void RobotLoop::rebuildMerges() {
    // 0x20cb2c: each merge object's pieces made anew: its "dummy" cell,
    // then the cell of every shown piece of its set at the piece's object.
    for (const Merge& m : merges_) {
        adventure56::Object* o = game_.scene.object(m.object);
        if (!o || o->content != 9 || !o->animation) {
            continue;
        }
        auto cellIndex = [o](std::uint32_t hash) {
            for (std::size_t i = 0; i < o->animation->cells.size(); ++i) {
                if (o->animation->cells[i].hash == hash) {
                    return int(i);
                }
            }
            return -1;
        };
        o->pieces.clear();
        o->pieceCapacity = 1024;
        if (const int k = cellIndex(xq::hash("dummy")); k >= 0) {
            o->pieces.push_back({k, 0, 0});
        }
        for (std::size_t id = 0; id < pieces_.size(); ++id) {
            const Piece& p = pieces_[id];
            adventure56::Object* po = objectOf(id);
            if (!po || p.mergeSet != m.resource || !game_.objectValue(*po, 11).truthy()) {
                continue;
            }
            if (const int k = cellIndex(p.mergeCell); k >= 0) {
                const auto& at = po->position.value();
                o->pieces.push_back({k, at[0], at[1]});
            }
        }
    }
}

void RobotLoop::drawMinimap(const std::string& name, std::uint32_t set) {
    // 0x20a350: an object of the upper screen (priority 0x6f, over the
    // map's 110) at (27, 83) with a quad per icon (0x20a0fc), each a cell
    // of the set's texture "MINI_MAP" at its own size (the cell's box), its
    // corner at (cell x 13 - 12, cell y 13 - 12): "ENM_MAP" over each 8009
    // point, then each piece's type name at its step's start, a small or
    // middle enemy's with its route's first direction (D, U, L, R). Types
    // without a cell of that name show nothing. The port bakes the cells
    // as the object's pieces (their meshes lie on their texels: hypothesis
    // from the boxes, which the original uses as texture coordinates).
    adventure56::Object* o = game_.scene.object(xq::hash(name));
    if (!o) {
        o = &game_.createObject(name, 0, 0x6f, 0, {});
    }
    o->touchOn = false;
    o->drawn = true;
    if (!game_.setAnimationSet(*o, set) || !o->animation) {
        return;
    }
    o->pieces.clear();
    o->pieceCapacity = 1024;
    auto add = [o](const std::string& cellName, Cell at) {
        const std::uint32_t h = xq::hash(cellName);
        const auto& cells = o->animation->cells;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (cells[i].hash == h) {
                const float x = float(at.x * 13 - 12), y = float(at.y * 13 - 12);
                o->pieces.push_back({int(i), x - cells[i].box[0], y - cells[i].box[1]});
                return;
            }
        }
    };
    for (const Cell& p : points_) {
        add("ENM_MAP", p);
    }
    static const char kDirections[4] = {'D', 'U', 'L', 'R'};
    for (const Piece& p : pieces_) {
        const PieceType* t = typeOf(p.type);
        if (!t) {
            continue;
        }
        std::string icon = t->name;
        if (t->name == "TYP_CHR_ENM_DFT" || t->name == "TYP_CHR_ENM_MDL") {
            const Route* r = routeOf(p.route);
            if (!r) {
                continue;
            }
            icon = t->name == "TYP_CHR_ENM_DFT" ? "TYP_CHR_ENM_SML_" : "TYP_CHR_ENM_MDL_";
            if (r->steps[0] >= 0 && r->steps[0] < 4) {
                icon += kDirections[r->steps[0]];
            }
        }
        add(icon, p.from);
    }
    o->position.set({27, 83, o->position.value()[2]});
}

bool RobotLoop::command(int number, xq::Call& c, std::size_t first) {
    const std::size_t n = operands(c, first);
    auto u = [&c, first](std::size_t i) { return c.unsignedInt(first + i); };
    auto s = [&c, first](std::size_t i) { return c.integer(first + i); };
    auto out = [&c, first](std::size_t i, Value v) { put(c, first + i, std::move(v)); };
    auto outU = [&out](std::size_t i, std::uint32_t v) { out(i, Value::ofUint(v)); };
    auto outI = [&out](std::size_t i, std::int32_t v) { out(i, Value::ofInt(v)); };
    switch (number) {
    case 1: { // {stage file, number}: the board's edge ring and the stage's pieces
        const std::string* file = n >= 2 ? c.text(first) : nullptr;
        const int stage = int(u(1) & 0xff);
        const StageRecord* rec = file ? stageOf(stage) : nullptr;
        if (!rec || !typeOf(rec->edge)) {
            return result(c, 0);
        }
        width_ = std::int8_t(rec->width + 2);
        height_ = std::int8_t(rec->height + 2);
        fuel_ = rec->fuel;
        {
            auto edge = [this, rec](int x, int y) {
                Piece p;
                p.type = rec->edge;
                p.start = p.from = p.to = p.marked = {std::int8_t(x), std::int8_t(y)};
                p.history[0] = p.start;
                addPiece(p);
            };
            for (int x = 1; x < width_ - 1; ++x) {
                edge(x, 0);
                edge(x, height_ - 1);
            }
            for (int y = 1; y < height_ - 1; ++y) {
                edge(0, y);
                edge(width_ - 1, y);
            }
        }
        stageNumber_ = stage;
        if (loadStage(*file)) {
            return true; // the original leaves the result as it was
        }
        return result(c, 0);
    }
    case 2: // the board cleared
        resetBoard();
        return result(c, 1);
    case 3: // the draw list and the merges
        buildDrawList();
        return result(c, 1);
    case 4: // {pause}: the move's frames stop
        if (n == 0) {
            return result(c, 0);
        }
        paused_ = c.at(first).truthy();
        return result(c, 1);
    case 5:
        undo();
        return result(c, 1);
    case 10: // {-> pieces}
        if (n == 0) {
            return false;
        }
        outU(0, std::uint32_t(pieces_.size()));
        return true;
    case 13: { // {types (.xs), stages (.xb)}: the two data files, once
        const std::string* types = n >= 2 ? c.text(first) : nullptr;
        if (!types || tablesLoaded_) {
            return result(c, 0);
        }
        loadTables(*types);
        if (const std::string* folder = c.text(first + 1)) {
            const auto slash = folder->rfind('/');
            stageFolder_ =
                slash == std::string::npos ? std::string() : folder->substr(0, slash + 1);
        }
        return result(c, tablesLoaded_ ? 1 : 0);
    }
    case 14: { // {type (name or hash), value index, -> value}
        if (n < 3) {
            return false;
        }
        const Value& v = c.at(first);
        const std::uint32_t h =
            v.isText() ? xq::hash(v.text) : v.kind == Value::Kind::Uint ? v.bits : 0u;
        const std::uint32_t k = u(1);
        const PieceType* t = h != 0 && k < 16 ? typeOf(h) : nullptr;
        if (!t) {
            return false;
        }
        const Value& x = t->values[k];
        if (x.kind == Value::Kind::Int || x.kind == Value::Kind::Uint ||
            x.kind == Value::Kind::Float || x.kind == Value::Kind::Text) {
            out(2, x);
            return result(c, 1);
        }
        return false;
    }
    case 15: // the update list sorted
        sortUpdates();
        return result(c, 1);
    case 16: { // {stage number, -> name, edge, set, goal, number, w, h, fuel, a, b}
        const StageRecord* r = n >= 10 ? stageOf(int(u(0))) : nullptr;
        if (!r) {
            return false;
        }
        const std::uint32_t v[10] = {r->name, r->edge,   r->set,  r->goal, r->number,
                                     r->width, r->height, r->fuel, r->a,    r->b};
        for (std::size_t i = 0; i < 10; ++i) {
            outU(1 + i, v[i]);
        }
        return result(c, 1);
    }
    case 17: { // {piece, block (0..6), -> its name}
        const Piece* p = n >= 2 ? piece(c, first) : nullptr;
        const std::uint32_t k = n >= 2 ? u(1) : 7;
        const PieceType* t = p && k < 7 ? typeOf(p->type) : nullptr;
        if (!t) {
            return false;
        }
        out(2, Value::ofText(textOfValue(t->values[k])));
        return true;
    }
    case 18: // {a, b, state, frames}: a move starts
        if (n < 4) {
            return result(c, 0);
        }
        startMove(int(u(0) & 0xff), int(u(1) & 0xff), int(u(2) & 0xff), c.real(first + 3));
        return result(c, 1);
    case 19:
        turnEnd();
        return result(c, 1);
    case 20: // {-> moving}
        if (n == 0) {
            return result(c, 0);
        }
        outI(0, running_ ? 1 : 0);
        return result(c, 1);
    case 21:
        rebuildUpdates();
        return result(c, 1);
    case 22: // {-> types}
        if (n == 0) {
            return false;
        }
        outU(0, std::uint32_t(types_.size()));
        return true;
    case 24: // {-> the move's state}
        if (n == 0) {
            return result(c, 0);
        }
        outU(0, state_);
        return result(c, 1);
    case 26: // {-> the frame's step}
        if (n == 0) {
            return false;
        }
        out(0, Value::ofFloat(frameDelta_));
        return result(c, 1);
    case 30: { // {v}: kept when larger than the count (as the original compares)
        const std::uint32_t v = n ? (u(0) & 0xffff) : 0;
        if (n == 0 || list_.size() >= v || list_.size() >= kListMax) {
            return false;
        }
        list_.push_back(std::uint16_t(v));
        return result(c, 1);
    }
    case 31:
        if (n == 0) {
            return false;
        }
        outU(0, std::uint32_t(list_.size()));
        return result(c, 1);
    case 32: { // {i, -> v}
        if (n < 2) {
            return false;
        }
        const std::uint32_t i = u(0);
        const std::uint16_t v = i < list_.size() ? list_[i] : kNone;
        outU(1, v);
        return v == kNone ? false : result(c, 1);
    }
    case 33:
        list_.clear();
        return result(c, 1);
    case 34: case 35: case 36: { // {stage, list (0 2D, 1 3D), [i,] -> count / file / "RES_..."}
        const std::size_t need = number == 34 ? 3 : 4;
        const StageRecord* r = n >= need ? stageOf(int(u(0))) : nullptr;
        const std::uint32_t k = n >= need ? u(1) : 2;
        const FileLists* f = r && k < 2 ? filesOf(r->set) : nullptr;
        if (!f) {
            return false;
        }
        const auto& list = f->lists[k];
        if (number == 34) {
            outU(2, std::uint32_t(list.size()));
            return result(c, 1);
        }
        const std::uint32_t i = u(2);
        if (i >= list.size()) {
            return false;
        }
        out(3, Value::ofText(number == 35 ? list[i] : resName(xq::hash(list[i]))));
        return result(c, 1);
    }
    case 37: // {-> minimap points}
        if (n == 0) {
            return false;
        }
        outU(0, std::uint32_t(points_.size()));
        return result(c, 1);
    case 38: { // {i, -> x, -> y}
        const std::uint32_t i = n >= 3 ? u(0) : kPoints;
        if (i >= points_.size()) {
            return false;
        }
        outU(1, std::uint32_t(std::int32_t(points_[i].x)));
        outU(2, std::uint32_t(std::int32_t(points_[i].y)));
        return result(c, 1);
    }
    case 39: // {object, animation set}: the bonus section's minimap
        if (n < 2) {
            return false;
        }
        if (const std::string* name = c.text(first)) {
            drawMinimap(*name, hashOf(c, first + 1));
        }
        return result(c, 1);
    case 40: { // {object, set}: a merge object
        if (n < 2 || merges_.size() >= 4) {
            return false;
        }
        merges_.push_back({hashOf(c, first), hashOf(c, first + 1)});
        return result(c, 1);
    }
    case 41: // the move stops
        running_ = false;
        return result(c, 1);
    case 42: // ControlIntr after the frame
        controlRequest_ = true;
        return result(c, 1);

    // ---- a piece's state
    case 100: { // {piece}: onto its step's end at once
        const std::uint32_t id = n ? u(0) : kNone;
        if (id >= pieces_.size()) {
            return false;
        }
        if (pieces_[id].flags & 1) {
            mark(std::uint16_t(id));
            pieces_[id].from = pieces_[id].to;
        }
        return result(c, 1);
    }
    case 101: case 103: case 104: case 112: { // {piece, [k,] v}
        Piece* p = n >= (number == 112 ? 3u : 2u) ? piece(c, first) : nullptr;
        if (!p) {
            return false;
        }
        if (number == 101) {
            p->countdown = std::uint8_t(u(1));
        } else if (number == 103) {
            p->direction = std::uint8_t(u(1) & 15);
        } else if (number == 104) {
            p->order = std::uint8_t(u(1));
        } else {
            const std::uint32_t k = u(1);
            if (k >= 3) {
                return false;
            }
            p->bytes[k] = std::uint8_t(s(2));
        }
        return result(c, 1);
    }
    case 102: { // {piece, x, y}: where its step ends
        const std::uint32_t id = n >= 3 ? u(0) : kNone;
        if (id >= pieces_.size()) {
            return false;
        }
        const int x = s(1), y = s(2);
        if (x >= kGrid || y >= kGrid || x >= width_ || y >= height_) {
            return false;
        }
        pieces_[id].to = {std::int8_t(x), std::int8_t(y)};
        if (running_ && !hitsDone_) {
            pairUp(std::uint16_t(id), 0);
        }
        return result(c, 1);
    }
    case 105: { // {piece, layer}: onto its grid (2: the player)
        const std::uint32_t id = n >= 2 ? u(0) : kNone;
        if (id >= pieces_.size()) {
            return false;
        }
        Piece& p = pieces_[id];
        p.layer = std::uint8_t(u(1));
        if (p.layer == 2) {
            player_ = std::uint16_t(id);
        }
        if (p.layer <= 2) {
            fill(p.layer == 1 ? gridA_ : gridB_, p, std::uint16_t(id), p.from);
        }
        return result(c, 1);
    }
    case 106: { // {piece, type}
        Piece* p = n >= 2 ? piece(c, first) : nullptr;
        if (!p) {
            return false;
        }
        p->type = hashOf(c, first + 1);
        return result(c, 1);
    }
    case 107: case 108: { // {piece, on}: on the board (and marked) / updated
        const std::uint32_t id = n >= 2 ? u(0) : kNone;
        if (id >= pieces_.size()) {
            return false;
        }
        const std::uint8_t bit = number == 107 ? 1 : 2;
        Piece& p = pieces_[id];
        p.flags = c.at(first + 1).truthy() ? std::uint8_t(p.flags | bit)
                                           : std::uint8_t(p.flags & ~bit);
        if (number == 107) {
            mark(std::uint16_t(id));
        }
        return result(c, 1);
    }
    case 109: { // {x, y, type index, history index}: a piece added on an empty inner cell
        if (n < 4) {
            return false;
        }
        const int x = s(0), y = s(1);
        const std::uint32_t k = u(2);
        if (k >= types_.size() || x <= 0 || y <= 0 || x >= width_ - 1 || y >= height_ - 1 ||
            !inGrid(x, y)) {
            return false;
        }
        if (cellOf(gridB_, x, y) != kNone || cellOf(gridA_, x, y) != kNone) {
            return result(c, 0);
        }
        Piece p;
        p.type = types_[k].hash;
        p.start = p.from = p.to = p.marked = {std::int8_t(x), std::int8_t(y)};
        p.history.fill(p.start);
        p.ring = std::uint8_t(u(3) & (kHistory - 1));
        return addPiece(p) ? result(c, 1) : false;
    }
    case 110: // {piece}: removed
        return n && removePiece(u(0)) ? result(c, 1) : false;
    case 111: { // {piece, ox, oy, w, h, radius}: its footprint and size
        Piece* p = n >= 6 ? piece(c, first) : nullptr;
        if (!p) {
            return false;
        }
        p->ox = std::int8_t(s(1));
        p->oy = std::int8_t(s(2));
        p->width = std::int8_t(s(3));
        p->height = std::int8_t(s(4));
        p->radius = c.real(first + 5) * 0.5f; // 0x209f50
        return result(c, 1);
    }
    case 113: { // {piece, set, cell}: drawn into the merge object of the set
        Piece* p = n >= 3 ? piece(c, first) : nullptr;
        if (!p) {
            return false;
        }
        p->mergeSet = hashOf(c, first + 1);
        p->mergeCell = hashOf(c, first + 2);
        p->merged = true;
        return result(c, 1);
    }

    // ---- reading a piece
    case 200: case 206: { // {piece, -> x, -> y}: the step's start / end
        const Piece* p = n >= 3 ? piece(c, first) : nullptr;
        if (!p) {
            return false;
        }
        const Cell at = number == 200 ? p->from : p->to;
        outU(1, std::uint32_t(std::int32_t(at.x)));
        outU(2, std::uint32_t(std::int32_t(at.y)));
        return result(c, 1);
    }
    case 201: { // {piece, -> +0x10}
        const Piece* p = n >= 2 ? piece(c, first) : nullptr;
        if (!p) {
            return false;
        }
        out(1, Value::ofFloat(p->unknown10));
        return result(c, 1);
    }
    case 202: { // {piece, stage, which, -> "RES_..."}: its model's name
        if (n < 4) {
            return false;
        }
        const auto name = modelName(u(0), int(u(1) & 0xff), int(u(2)));
        if (!name) {
            return false;
        }
        out(3, Value::ofText(*name));
        return result(c, 1);
    }
    case 203: case 204: case 207: case 213: case 220: { // {piece, -> v}
        const Piece* p = n >= 2 ? piece(c, first) : nullptr;
        if (!p) {
            return false;
        }
        const std::uint32_t v = number == 203   ? p->route
                                : number == 204 ? p->countdown
                                : number == 207 ? p->direction
                                : number == 213 ? p->layer
                                                : std::uint32_t(int(p->width) * int(p->height));
        outU(1, v);
        return result(c, 1);
    }
    case 205: case 216: { // {piece, -> its 2D / 3D object's name}
        if (n < 2) {
            return false;
        }
        const Piece* p = piece(c, first);
        if (!p) {
            c.result = Value::ofText("Error");
            return true;
        }
        out(1, Value::ofText(objectName(*p, number == 216)));
        return result(c, 1);
    }
    case 208: // {-> the player}
        if (n == 0) {
            return false;
        }
        outU(0, player_);
        return result(c, 1);
    case 209: case 215: { // {i, -> the sorted / update list's i-th}
        if (n < 2) {
            return false;
        }
        const std::uint32_t i = u(0);
        if (number == 209 ? i >= pieces_.size() : i >= std::uint32_t(updateCount_)) {
            return false;
        }
        outU(1, number == 209 ? sorted_[i] : updates_[i]);
        return result(c, 1);
    }
    case 210: { // {piece, board, cell, -> the piece it meets}
        if (n < 4) {
            return false;
        }
        outU(3, meets(u(0), int(u(1)), int(u(2)), nullptr, false));
        return result(c, 1);
    }
    case 211: { // {piece, -> +0xec}
        const Piece* p = n >= 2 ? piece(c, first) : nullptr;
        if (!p) {
            return false;
        }
        outI(1, p->extra);
        return result(c, 1);
    }
    case 212: { // {piece, i, -> its route's step i (wrapping)}
        const Piece* p = n >= 3 ? piece(c, first) : nullptr;
        const Route* r = p ? routeOf(p->route) : nullptr;
        if (!r || r->count == 0) {
            return false;
        }
        outU(2, std::uint8_t(r->steps[u(1) % r->count]));
        return result(c, 1);
    }
    case 214: // {-> the update list's count}
        if (n == 0) {
            return false;
        }
        outU(0, std::uint32_t(updateCount_));
        return result(c, 1);
    case 217: { // {x, y, -> the piece on an inner cell (grid B first)}
        if (n < 3) {
            return false;
        }
        const int x = s(0), y = s(1);
        if (x <= 0 || y <= 0 || x >= width_ - 1 || y >= height_ - 1 || !inGrid(x, y)) {
            return false;
        }
        std::uint16_t id = cellOf(gridB_, x, y);
        if (id == kNone) {
            id = cellOf(gridA_, x, y);
        }
        if (id == kNone) {
            return false;
        }
        outU(2, id);
        return result(c, 1);
    }
    case 218: { // {piece, -> its route's length}
        const Piece* p = n >= 2 ? piece(c, first) : nullptr;
        const Route* r = p ? routeOf(p->route) : nullptr;
        if (!r) {
            return false;
        }
        outU(1, r->count);
        return result(c, 1);
    }
    case 219: { // {piece, -> its type's name}
        const Piece* p = n >= 2 ? piece(c, first) : nullptr;
        const PieceType* t = p ? typeOf(p->type) : nullptr;
        if (!t) {
            return false;
        }
        out(1, Value::ofText(t->name));
        return result(c, 1);
    }
    case 221: { // {piece, k, -> byte k}
        const Piece* p = n >= 3 ? piece(c, first) : nullptr;
        const std::uint32_t k = n >= 3 ? u(1) : 3;
        if (!p || k >= 3) {
            return false;
        }
        outI(2, std::int8_t(p->bytes[k]));
        return result(c, 1);
    }
    case 300: // {text, -> its hash}
        if (n < 2) {
            return false;
        }
        outU(1, hashOf(c, first));
        return result(c, 1);
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<Loop> makeRobotLoop(Story& story, int id, std::vector<Value> values) {
    return std::make_unique<RobotLoop>(story, id, std::move(values));
}

} // namespace picarat::adventure5
