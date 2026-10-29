// The fifth adventure's vanishing puzzles (catalogue type 27, loop 3140;
// puzzles 29, 69, 118): pieces on a grid jump over a neighbour onto the
// free cell behind it, and the piece jumped over vanishes; the puzzle is
// solved when one piece is left, on the goal cell and of the asked kind.
//
// Reconstructed from the loop's command handler 0x2d829c (vtable 0x402908,
// constructor 0x386ec0), its input hook 0x2d9bb0, its draw hook 0x2d9e4c and
// helpers: reset 0x393814, undo 0x2d9fa8, pick up 0x2da238, drop 0x2da3b4.
//
// How it plays: the board is a grid of cells (command 10: size, first cell,
// cell size, the number of directions, the marker animations), some taken
// away (11). Each cell knows its neighbours one and two cells away in the
// directions right, down, left, up and the four diagonals (the first
// `directions` of them). The pieces (40) are lower-screen objects with the
// script's normal and slide handlers; each has a model on the upper screen
// (50, "mdl_NN") standing on the cell's 3D position (12). A piece may be
// picked up (41) only when it has a jump: a neighbour holding a piece of
// kind 0 with a free cell behind it. While it is held, the marker "ori"
// stands on its cell and "dst_NN" on every cell it could jump to. Dropped
// (42), it goes to the landing cell nearest to the drop point when that is
// nearer than its own cell, else back; a jump takes the neighbour off the
// board (its object to 256, 256; its model hidden by 51 once the script has
// shrunk it) and is recorded for undo (1). The judge (22) asks for one piece
// left on the goal cell (21) whose animation is the named one.
//
// The draw hook draws the models whose colour (value of command 50) is the
// one named by command 10 itself: the first fully, the others as copies of
// its first mesh with their own transform (0x293c14). See drawCopies.
#include "puzzles/a5_special.hpp"
#include <cmath>
#include <cstdio>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using adventure56::Object3D;
using xq::Call;
using xq::Value;

// 0x4437dc (filled once, guard 0x412ae8): the directions by number; a
// number past 7 reads the ninth entry (no step).
constexpr int kStep[9][2] = {{1, 0},  {0, 1},  {-1, 0},  {0, -1}, {1, 1},
                             {1, -1}, {-1, 1}, {-1, -1}, {0, 0}};

// 0x2da8c4: where a piece jumped over is put (off the lower screen).
constexpr float kAway = 256.0f;
// 0x2da8c8: subtracted from the jump's heading (not quite a right angle).
constexpr float kHeadingOffset = 1.57f;

class VanishPuzzle final : public SpecialPuzzle {
public:
    explicit VanishPuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    std::string describe() const override;
    void input() override;

private:
    struct Cell {
        int near[8] = {-1, -1, -1, -1, -1, -1, -1, -1}; // +0x00: cells one step away
        int far[8] = {-1, -1, -1, -1, -1, -1, -1, -1};  // +0x20: two steps away
        int px = 0, py = 0;                             // +0x40: pixels
        bool removed = false;                           // +0x44
        float world[3] = {0, 0, 0};                     // +0x48: 3D position
        int piece = -1;                                 // +0x54
    };

    struct Piece {
        int kind = 0;             // +0x00: 0 may be jumped over
        std::uint32_t object = 0; // +0x04
        int homeX = 0, homeY = 0; // +0x08
        std::uint32_t model = 0;  // +0x0c
        std::string modelName;    // +0x10
        std::string stand;        // +0x24: the standing motion
        std::uint32_t colour = 0; // +0x38
        int x = -1, y = -1;       // +0x3c
    };

    // An undo record (0x14 bytes): the mover and the piece jumped over,
    // the cells they came from, and their models' turns (kept, unused).
    struct Move {
        int mover = -1, taken = -1;
        int fromX = -1, fromY = -1, takenX = -1, takenY = -1;
        float moverTurn = 0, takenTurn = 0;
    };

    int index(int x, int y) const {
        return unsigned(x) < unsigned(width_) && unsigned(y) < unsigned(height_) ? width_ * y + x
                                                                                 : -1;
    }

    Cell* cell(int i) {
        return i >= 0 && i < int(cells_.size()) ? &cells_[std::size_t(i)] : nullptr;
    }

    Cell* cellAt(int x, int y) { return cell(index(x, y)); }

    Piece* piece(int i) {
        return i >= 0 && i < int(pieces_.size()) ? &pieces_[std::size_t(i)] : nullptr;
    }

    Piece* pieceByObject(std::uint32_t hash) {
        for (Piece& p : pieces_) {
            if (p.object == hash) {
                return &p;
            }
        }
        return nullptr;
    }

    // A jump in direction d from cell `at`: the neighbour holds a piece of
    // kind 0 and the cell behind it is free (both on the board).
    bool canJump(const Cell& at, int d) {
        const Cell* over = cell(at.near[d]);
        const Cell* to = cell(at.far[d]);
        if (!over || over->removed || over->piece < 0 || !to || to->removed || to->piece >= 0) {
            return false;
        }
        // The sixth adventure (0x30c290) jumps over any piece; its fifth
        // operand of 40 is the ingredient, not a jumpability.
        const Piece* p = piece(over->piece);
        return p && (p->kind == 0 || game_.edition == Edition::Sixth);
    }

    bool anyJump(const Cell& at) {
        for (int d = 0; d < directions_; ++d) {
            if (canJump(at, d)) {
                return true;
            }
        }
        return false;
    }

    void place(std::uint32_t object, float x, float y) {
        if (Object* o = game_.scene.object(object)) {
            o->position.set({x, y, o->position[2]});
        }
    }

    void placeModel(const Piece& p, const Cell& at) {
        if (Object3D* m = game_.scene.object3D(p.model)) {
            m->position.set({at.world[0], at.world[1], at.world[2]});
        }
    }

    void show(const std::string& name, bool on) {
        if (Object* o = game_.scene.object(xq::hash(name))) {
            o->visible = on;
        }
    }

    static std::string marker(int d) {
        char name[16];
        std::snprintf(name, sizeof name, "dst_%02x", unsigned(d));
        return name;
    }

    void hideMarkers() {
        for (int d = 0; d < directions_; ++d) {
            show(marker(d), false);
        }
        show("ori", false);
    }

    // 0x2936c0 at frame 0 (6810's core), the rotation cleared, shown.
    void standModel(const Piece& p, const Cell& at, bool restart) {
        Object3D* m = game_.scene.object3D(p.model);
        if (!m) {
            return;
        }
        m->position.set({at.world[0], at.world[1], at.world[2]});
        if (restart) {
            startMotion(*m, p.stand);
        }
        m->rotation3.set({0, 0, 0});
        m->visible = true;
    }

    static void startMotion(Object3D& m, const std::string& name) {
        if (m.motion != name || m.motionDone) {
            m.motionTime = 0;
        }
        m.motion = name;
        m.motionOnce = false;
        m.motionDone = false;
    }

    int onBoard() {
        int n = 0;
        for (const Piece& p : pieces_) {
            if (cellAt(p.x, p.y)) {
                ++n;
            }
        }
        return n;
    }

    void reset();
    bool undo();
    bool pickUp(std::uint32_t object);
    Cell* drop(std::uint32_t object);
    bool solved();

    Game& game_;
    int directions_ = 0;            // +0x160
    int held_ = -1, heldCell_ = -1; // +0x162 / +0x164
    int lastMover_ = -1;            // +0x166
    std::uint32_t taken_ = 0;       // +0x168: the model jumped over
    std::string resource_;          // +0x16c
    int originX_ = 0, originY_ = 0; // +0x1ac / +0x1ae
    int size_ = 0;                  // +0x1aa
    int sizeY_ = 0;                 // sixth adventure: the cell's height
    int width_ = 0, height_ = 0;    // +0x1b0 / +0x1b2
    int goalX_ = -1, goalY_ = -1;   // +0x1b4 / +0x1b6
    std::uint32_t goalName_ = 0;    // +0x1b8
    int models_ = 0;                // +0x1bc
    std::uint32_t copyColour_ = 0;  // +0x1c0
    float jump_[4] = {0, 0, 0, 0};  // +0x1d8..0x1e4: landing x y z, heading
    std::vector<Piece> pieces_;     // +0x1f0
    std::vector<Cell> cells_;       // +0x1fc
    std::vector<Move> moves_;       // +0x208, newest first
};

// 0x393814: nothing held, no moves, every piece back on its first cell.
void VanishPuzzle::reset() {
    held_ = heldCell_ = lastMover_ = -1;
    taken_ = 0;
    moves_.clear();
    for (Cell& c : cells_) {
        c.piece = -1;
    }
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        Piece& p = pieces_[i];
        p.x = p.homeX;
        p.y = p.homeY;
        Cell* at = cellAt(p.x, p.y);
        if (!at || at->removed) {
            continue;
        }
        at->piece = int(i);
        if (game_.scene.object(p.object)) {
            place(p.object, float(at->px), float(at->py));
            standModel(p, *at, true);
        }
    }
}

// 0x2d9fa8: the newest move taken back. The mover's motion is not
// restarted (0x18806c only plays 2D animations).
bool VanishPuzzle::undo() {
    if (moves_.empty()) {
        return false;
    }
    const Move m = moves_.front();
    Piece* mover = piece(m.mover);
    if (!mover) {
        return false;
    }
    if (Cell* now = cellAt(mover->x, mover->y)) {
        now->piece = -1;
    }
    Cell* from = cellAt(m.fromX, m.fromY);
    mover->x = m.fromX;
    mover->y = m.fromY;
    if (from) {
        from->piece = m.mover;
        place(mover->object, float(from->px), float(from->py));
        standModel(*mover, *from, false);
    }
    if (Piece* taken = piece(m.taken)) {
        Cell* at = cellAt(m.takenX, m.takenY);
        taken->x = m.takenX;
        taken->y = m.takenY;
        if (at) {
            at->piece = m.taken;
            place(taken->object, float(at->px), float(at->py));
            standModel(*taken, *at, true);
        }
    }
    moves_.erase(moves_.begin());
    return true;
}

// 0x2da238: the piece (by its object) picked up when nothing is held and it
// has a jump.
bool VanishPuzzle::pickUp(std::uint32_t object) {
    if (held_ >= 0 || heldCell_ >= 0) {
        return false;
    }
    Piece* p = pieceByObject(object);
    Cell* at = p ? cellAt(p->x, p->y) : nullptr;
    if (!at || !anyJump(*at)) {
        return false;
    }
    held_ = int(p - pieces_.data());
    heldCell_ = index(p->x, p->y);
    return true;
}

// 0x2da3b4: the held piece dropped where its object now stands. Returns
// the cell it ends on.
VanishPuzzle::Cell* VanishPuzzle::drop(std::uint32_t object) {
    Piece* p = piece(held_);
    Cell* at = cell(heldCell_);
    Object* o = game_.scene.object(object);
    if (!p || p->object != object || !at || at->removed || !o) {
        return nullptr;
    }
    const int x = int(o->position[0] + o->offset[0]);
    const int y = int(o->position[1] + o->offset[1]);
    auto distance = [&](const Cell& c) {
        const int dx = std::int16_t(x - c.px), dy = std::int16_t(y - c.py);
        return dx * dx + dy * dy;
    };
    int best = distance(*at), way = -1;
    for (int d = 0; d < directions_; ++d) {
        if (canJump(*at, d)) {
            const int e = distance(cells_[std::size_t(at->far[d])]);
            if (e < best) {
                best = e;
                way = d;
            }
        }
    }
    taken_ = 0;
    at->piece = -1;
    jump_[3] = 0;
    Cell* end = at;
    if (way >= 0) {
        Cell& over = cells_[std::size_t(at->near[way])];
        Cell& to = cells_[std::size_t(at->far[way])];
        Piece& gone = pieces_[std::size_t(over.piece)];
        gone.x = gone.y = -1;
        place(gone.object, kAway, kAway);
        taken_ = gone.model;
        // The model's heading: atan2 of the step across the floor (0x2be1d0
        // with s0 = dz, s1 = -dx).
        const float dx = to.world[0] - at->world[0];
        const float dz = to.world[2] - at->world[2];
        jump_[3] = std::atan2(dz, -dx) - kHeadingOffset;
        jump_[0] = to.world[0];
        jump_[1] = to.world[1];
        jump_[2] = to.world[2];
        if (int(moves_.size()) < width_ * height_) {
            Move m;
            m.mover = held_;
            m.taken = over.piece;
            m.fromX = p->x;
            m.fromY = p->y;
            m.takenX = int(&over - cells_.data()) % width_;
            m.takenY = int(&over - cells_.data()) / width_;
            if (const Object3D* model = game_.scene.object3D(p->model)) {
                m.moverTurn = model->rotation3[1];
            }
            if (const Object3D* model = game_.scene.object3D(gone.model)) {
                m.takenTurn = model->rotation3[1];
            }
            moves_.insert(moves_.begin(), m);
        }
        over.piece = -1;
        end = &to;
    }
    const int e = int(end - cells_.data());
    p->x = e % width_;
    p->y = e / width_;
    end->piece = held_;
    lastMover_ = held_;
    held_ = heldCell_ = -1;
    hideMarkers();
    return end;
}

// The judge (22): fewer than two pieces left, and (with a goal) the first
// one left stands on the goal and plays the named animation.
bool VanishPuzzle::solved() {
    if (onBoard() >= 2) {
        return false;
    }
    if (goalX_ < 0 || goalY_ < 0) {
        return true;
    }
    for (const Piece& p : pieces_) {
        if (!cellAt(p.x, p.y)) {
            continue;
        }
        if (p.x != goalX_ || p.y != goalY_) {
            return false;
        }
        const Object* o = game_.scene.object(p.object);
        if (!goalName_ || !o) {
            return true;
        }
        // 0x2d021c: the playing animation's name.
        if (!o->animation || o->playing < 0 || o->playing >= int(o->animation->animations.size())) {
            return false;
        }
        return xq::hash(o->animation->animations[std::size_t(o->playing)].name) == goalName_;
    }
    return false;
}

// 0x2d9bb0: while a piece is held, "ori" on its cell and "dst_NN" on each
// cell it can jump to; otherwise none.
void VanishPuzzle::input() {
    if (held_ < 0) {
        hideMarkers();
        return;
    }
    const Cell* at = cell(heldCell_);
    if (!at || !game_.scene.resource(xq::hash(resource_))) {
        return;
    }
    if (Object* o = game_.scene.object(xq::hash("ori"))) {
        o->visible = true;
        o->position.set({float(at->px), float(at->py), o->position[2]});
    }
    for (int d = 0; d < directions_; ++d) {
        if (!canJump(*at, d)) {
            continue;
        }
        if (Object* o = game_.scene.object(xq::hash(marker(d)))) {
            const Cell& to = cells_[std::size_t(at->far[d])];
            o->visible = true;
            o->position.set({float(to.px), float(to.py), o->position[2]});
        }
    }
}

bool VanishPuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return c.integer(first + i);
    };
    auto text = [&](std::size_t i) {
        const std::string* t = c.text(first + i);
        return t ? *t : std::string();
    };
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    switch (number) {
    case 0:
        reset();
        return result(1);
    case 1:
        return result(undo() ? 1 : 0);
    case 10: {
        // Cells across, down; first cell x, y; cell size; directions; the
        // animation set, the "from" and "to" marker animations; the colour
        // the draw hook copies.
        // The sixth adventure (0x30acec) gives the cell's width and height
        // (operands 4, 5), then the directions (6), the set and the two
        // marker animations (7..9), and no colour.
        const bool sixth = game_.edition == Edition::Sixth;
        const std::size_t shift = sixth ? 1 : 0;
        if (count < 9 + shift - (sixth ? 1 : 0)) {
            return false;
        }
        copyColour_ = sixth ? 0 : xq::hash(text(9));
        resource_ = text(6 + shift);
        width_ = std::int16_t(integer(0));
        height_ = std::int16_t(integer(1));
        originX_ = integer(2) & 0xffff;
        originY_ = integer(3) & 0xffff;
        size_ = integer(4) & 0xffff;
        sizeY_ = sixth ? integer(5) & 0xffff : size_;
        directions_ = integer(5 + shift) & 0xffff;
        const int cells = std::max(0, width_ * height_);
        pieces_.clear();
        pieces_.reserve(std::size_t(cells));
        cells_.assign(std::size_t(cells), Cell{});
        moves_.clear();
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                Cell& at = *cellAt(x, y);
                if (sixth) {
                    // The sixth adventure (0x30b138) places the cells from
                    // fixed tables for a 5 by 5 board, whatever the
                    // operands say (the first entry past its end).
                    static constexpr std::int16_t kColumns[] = {43, 101, 159, 219, 276};
                    static constexpr std::int16_t kRows[] = {56, 86, 120, 154, 183};
                    at.px = kColumns[x < 5 ? x : 0];
                    at.py = kRows[y < 5 ? y : 0];
                } else {
                    at.px = std::int16_t(x * size_ + originX_);
                    at.py = std::int16_t(y * sizeY_ + originY_);
                }
                for (int d = 0; d < 8; ++d) {
                    const int* s = kStep[std::min(d, 8)];
                    at.near[d] = index(x + s[0], y + s[1]);
                    at.far[d] = index(x + s[0] * 2, y + s[1] * 2);
                }
            }
        }
        // The markers (screen 1, priority 111, kind 6), hidden; the message
        // object "vnsh_msg" (priority 132, kind 9) for the script.
        const std::uint32_t set = xq::hash(resource_);
        auto markerObject = [&](const std::string& name, const std::string& animation) {
            Object& o = game_.createObject(name, 1, 111, 6, {});
            game_.setAnimationSet(o, set);
            game_.playAnimation(o, Value::ofText(animation), 0);
            o.visible = false;
        };
        markerObject("ori", text(7 + shift));
        for (int d = 0; d < directions_; ++d) {
            markerObject(marker(d), text(8 + shift));
        }
        game_.createObject("vnsh_msg", 1, 132, 9, {}).visible = false;
        return result(1);
    }
    case 11: {
        // Cells taken off the board (x, y, across, down).
        if (count < 4) {
            return false;
        }
        const int x0 = std::int16_t(integer(0)), y0 = std::int16_t(integer(1));
        for (int j = 0; j < std::int16_t(integer(3)); ++j) {
            for (int i = 0; i < std::int16_t(integer(2)); ++i) {
                if (Cell* at = cellAt(std::int16_t(x0 + i), std::int16_t(y0 + j))) {
                    at->removed = true;
                }
            }
        }
        return result(1);
    }
    case 12: {
        // The cells' 3D positions: spacing across, down (along z), height;
        // the board centred on the origin.
        if (count < 3) {
            return false;
        }
        const float across = c.real(first), down = c.real(first + 1), y = c.real(first + 2);
        const float x0 = float(width_ - 1) * -(across * 0.5f);
        float z = float(height_ - 1) * -(down * 0.5f);
        for (int j = 0; j < height_; ++j) {
            float x = x0;
            for (int i = 0; i < width_; ++i) {
                if (Cell* at = cellAt(i, j)) {
                    at->world[0] = x;
                    at->world[1] = y;
                    at->world[2] = z;
                    x += across;
                }
            }
            z += down;
        }
        return true;
    }
    case 20:
        game_.setVariable(names::verdict, 1);
        return result(1);
    case 21:
        // The goal cell, optionally with the animation the last piece must
        // play.
        if (count < 2) {
            return false;
        }
        if (count == 2 || count == 3) {
            goalX_ = std::int16_t(integer(0));
            goalY_ = std::int16_t(integer(1));
            if (count == 3) {
                goalName_ = xq::hash(text(2));
            }
        }
        return result(1);
    case 22:
        return result(solved() ? 1 : 0);
    case 30:
        c.result = Value::ofInt(onBoard());
        return true;
    case 31: {
        // Pieces left that have a jump.
        int n = 0;
        for (const Piece& p : pieces_) {
            const Cell* at = cellAt(p.x, p.y);
            if (at && anyJump(*at)) {
                ++n;
            }
        }
        c.result = Value::ofInt(n);
        return true;
    }
    case 40: {
        // A piece "vnsh_NN" (x, y, animation set, animation, normal and
        // slide handler blocks, kind) on screen 1 (priority 111, kind 7),
        // touchable over its cell. The result is its name.
        if (count < 7) {
            return false;
        }
        const int x = std::int16_t(integer(0)), y = std::int16_t(integer(1));
        Cell* at = cellAt(x, y);
        if (!at) {
            return false;
        }
        char name[16];
        std::snprintf(name, sizeof name, "vnsh_%02x", unsigned(pieces_.size()));
        const std::string animation = text(3);
        const Value values[] = {Value::ofText(name), Value::ofInt(7), Value::ofInt(size_ >> 1),
                                Value::ofText(animation)};
        Object& o = game_.createObject(name, 1, 111, 7, values);
        Piece p;
        p.object = o.hash;
        p.homeX = p.x = x;
        p.homeY = p.y = y;
        p.kind = std::uint8_t(integer(6));
        pieces_.push_back(p);
        o.position.set({float(at->px), float(at->py), o.position[2]});
        const float half = float(size_ >> 1);
        o.areas.push_back({2, -half, -half, float(size_), float(size_)});
        const std::string handlers[] = {text(4), text(5), std::string()};
        game_.setHandlers(o, 3, handlers);
        game_.setAnimationSet(o, xq::hash(text(2)));
        game_.playAnimation(o, Value::ofText(animation), 0);
        at->piece = int(pieces_.size()) - 1;
        c.result = Value::ofText(name);
        return true;
    }
    case 41:
        if (count < 1) {
            return false;
        }
        return result(pickUp(xq::hash(text(0))) ? 1 : 0);
    case 42: {
        // Drop: the end cell's pixels into the two registers.
        if (count < 3) {
            return false;
        }
        const Cell* end = drop(xq::hash(text(0)));
        Value* rx = c.reg(first + 1);
        Value* ry = c.reg(first + 2);
        if (rx) {
            *rx = Value::ofInt(end ? end->px : -1);
        }
        if (ry) {
            *ry = Value::ofInt(end ? end->py : -1);
        }
        return result(end ? 1 : 0);
    }
    case 50: {
        // The next piece's model "mdl_NN" (resource, standing, jump start,
        // jump and success motions, colour, jump frames) on the upper
        // screen (priority 120, kind 5) at its cell.
        if (count < 7) {
            return false;
        }
        Piece* p = piece(models_);
        if (!p) {
            return result(1);
        }
        char name[16];
        std::snprintf(name, sizeof name, "mdl_%02x", unsigned(models_));
        p->modelName = name;
        p->model = xq::hash(name);
        p->stand = text(1);
        const std::string colour = text(5);
        p->colour = xq::hash(colour);
        Object3D& m = game_.scene.create3D(name, 0, 120, 5);
        m.values.assign(16, Value{});
        const Value values[] = {Value::ofText(name),     Value::ofInt(5),
                                Value::ofText(p->stand), Value::ofText(text(2)),
                                Value::ofText(text(3)),  Value::ofText(text(4)),
                                Value::ofInt(integer(6))};
        for (std::size_t i = 0; i < std::size(values); ++i) {
            m.values[i] = values[i];
        }
        const adventure56::Resource* r = game_.scene.resource(xq::hash(text(0)));
        m.model = r ? r->model : nullptr;
        startMotion(m, p->stand);
        Cell* at = cellAt(p->x, p->y);
        if (!at) {
            return false;
        }
        m.position.set({at->world[0], at->world[1], at->world[2]});
        // 0x39b24c (6820's core): the colour is a face motion (the model's
        // white / black texture offsets); 0x18f680 sets the face layer's
        // +0x38 to 1 (hypothesis: its speed, already 1).
        m.faces[0].motion = colour;
        m.faces[0].time = 0;
        ++models_;
        return result(1);
    }
    case 51:
        // The model jumped over hidden (the script has shrunk it).
        if (Object3D* m = game_.scene.object3D(taken_)) {
            m->visible = false;
        }
        lastMover_ = -1;
        taken_ = 0;
        return result(1);
    case 52: {
        // The piece's model name into the register; whether the drop
        // jumped.
        if (count < 2) {
            return false;
        }
        if (const Piece* p = pieceByObject(xq::hash(text(0)))) {
            if (Value* r = c.reg(first + 1)) {
                *r = Value::ofText(p->modelName);
            }
        }
        return result(taken_ ? 1 : 0);
    }
    case 53:
        // The jump's landing (x, y, z) and heading into four registers.
        if (count < 4) {
            return false;
        }
        for (std::size_t i = 0; i < 4; ++i) {
            if (Value* r = c.reg(first + i)) {
                *r = Value::ofFloat(jump_[i]);
            }
        }
        return result(1);
    case 54:
        // The name of the model jumped over into the register.
        if (count < 1) {
            return false;
        }
        if (const Object3D* m = game_.scene.object3D(taken_)) {
            if (Value* r = c.reg(first)) {
                *r = Value::ofText(m->name);
            }
        }
        return result(1);
    case 60: {
        // Sixth adventure only (0x30bce8): cell (x, y)'s links in direction d
        // reach twice as far (one step: two cells, two steps: four), so a
        // piece jumps across a gap. The direction table (0x21a458) taken as
        // the fifth's order (hypothesis).
        if (game_.edition != Edition::Sixth || count < 3) {
            return false;
        }
        const int x = std::int16_t(integer(0)), y = std::int16_t(integer(1));
        const int d = std::int8_t(integer(2));
        Cell* at = cell(index(x, y));
        if (!at || d < 0 || d >= 8) {
            return false;
        }
        const int* step = kStep[d];
        at->near[d] = index(x + step[0] * 2, y + step[1] * 2);
        at->far[d] = index(x + step[0] * 4, y + step[1] * 4);
        return result(1);
    }
    case 61: {
        // Sixth adventure only (0x30abf8): the object of the piece the
        // newest move jumped over (the undo record's +2), 0 with no moves.
        // The scripts show it going (Nazo_VanishNormal) and coming back
        // (Nazo_VanishUndo).
        if (game_.edition != Edition::Sixth) {
            return false;
        }
        const Piece* taken = moves_.empty() ? nullptr : piece(moves_.front().taken);
        c.result = Value::ofUint(taken ? taken->object : 0u);
        return true;
    }
    default:
        return false;
    }
}

// The state for research tools (A5_DESCRIBE): the grid, each cell's pixels
// and links, the pieces and the goal.
std::string VanishPuzzle::describe() const {
    std::string out;
    char line[256];
    std::snprintf(line, sizeof line, "grid %d %d goal %d %d\n", width_, height_, goalX_, goalY_);
    out += line;
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const Cell& c = cells_[i];
        std::snprintf(line, sizeof line, "cell %zu px %d %d removed %d near", i, c.px, c.py,
                      int(c.removed));
        out += line;
        for (int n : c.near) {
            out += " " + std::to_string(n);
        }
        out += " far";
        for (int n : c.far) {
            out += " " + std::to_string(n);
        }
        out += "\n";
    }
    for (const Piece& p : pieces_) {
        std::snprintf(line, sizeof line, "piece kind %d at %d %d\n", p.kind, p.x, p.y);
        out += line;
    }
    return out;
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeVanishPuzzle(Game& game) {
    return std::make_unique<VanishPuzzle>(game);
}

} // namespace picarat::adventure5
