// The fifth adventure's sliding penguin puzzles (catalogue type 35, loop
// 3180; puzzles 44, 119, 137): penguins on an ice field slide in a straight
// line until another penguin stops them - or off the edge into the water.
// The puzzle is solved when the named penguin stands on the goal cell.
//
// Reconstructed from the loop's command handler 0x1faadc (vtable 0x401634,
// constructor 0x37c2d8; the input hook is the base's) and its helpers:
// reset 0x385234, undo 0x1fbe6c, taking cells 0x28ab20, the cell at
// 0x2d03d4, the pick-up 0x1fc00c, the aim 0x1fc3b4, the release 0x1fc23c
// and the slide 0x28aa38.
//
// How it plays: the field (command 10: cells across and down, first cell,
// cell size) gets a ring of blocked cells around it (the water); 11 blocks
// more. Penguins (40, "slip_NN") are lower-screen objects with the script's
// normal and slide handlers; each has a model (50, "mdl_NN") on the cell's
// 3D position (12). Picking one up (41) tells which of the four directions
// let it move (the arrows); dragging (43) at least a cell's size away picks
// the direction of the larger component: the slide goes cell by cell while
// the cells are free, stops before a penguin, or runs into the ring (the
// penguin falls in). The release (42) moves it there, records the move for
// undo (30 moves) and reports a fall and a penguin already in that ring
// cell. 31 counts the penguins still on the ice (none once the goal penguin
// has fallen); 22 checks the goal (21).
#include "puzzles/a5_special.hpp"
#include <cmath>
#include <cstdio>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using adventure56::Object3D;
using adventure56::wrapAngle;
using xq::Call;
using xq::Value;

constexpr float kAway = 1.57f; // 0x1fc5e4: subtracted from the heading
constexpr int kMoves = 30;     // the undo list (0x1e records)

class SlipPuzzle final : public SpecialPuzzle {
public:
    explicit SlipPuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;

private:
    struct Cell {
        int px = 0, py = 0;         // +0x00: pixels
        bool blocked = false;       // +0x04
        int piece = -1;             // +0x06
        float world[3] = {0, 0, 0}; // +0x08
    };

    struct Piece {
        std::uint32_t object = 0; // +0x00
        int homeX = 0, homeY = 0; // +0x04
        std::uint32_t model = 0;  // +0x08
        std::string modelName;    // +0x0c
        std::string stand;        // +0x20
        int x = 0, y = 0;         // +0x34
    };

    struct Move {
        int pushed = -1, piece = -1;
        int fromX = 0, fromY = 0;
        float turn = 0;
    };

    int index(int x, int y) const {
        return unsigned(x) < unsigned(cols_) && unsigned(y) < unsigned(rows_) ? cols_ * y + x : -1;
    }

    Cell* cell(int i) {
        return i >= 0 && i < int(cells_.size()) ? &cells_[std::size_t(i)] : nullptr;
    }

    Cell* cellAt(int x, int y) { return cell(index(x, y)); }

    Piece* piece(int i) {
        return i >= 0 && i < int(pieces_.size()) ? &pieces_[std::size_t(i)] : nullptr;
    }

    Piece* pieceByObject(std::uint32_t h) {
        for (Piece& p : pieces_) {
            if (p.object == h) {
                return &p;
            }
        }
        return nullptr;
    }

    std::uint32_t playingHash(std::uint32_t object) const {
        const Object* o = game_.scene.object(object);
        if (!o || !o->animation || o->playing < 0 ||
            o->playing >= int(o->animation->animations.size())) {
            return 0;
        }
        return o->animation->animations[std::size_t(o->playing)].hash;
    }

    // 0x28ab20: cells (x, y, across, down) blocked.
    void block(int x0, int y0, int w, int h) {
        for (int j = 0; j < h; ++j) {
            for (int i = 0; i < w; ++i) {
                if (Cell* c = cellAt(x0 + i, y0 + j)) {
                    c->blocked = true;
                }
            }
        }
    }

    // 0x28aa38: from (x, y) one step at a time towards (dx, dy); `end`
    // the last free cell (or the ring cell run into); `how` 1 stopped by a
    // penguin, 2 into the ring; the steps taken.
    int slide(int x, int y, int dx, int dy, int& endX, int& endY, int& how) {
        how = 0;
        int steps = 0;
        for (;;) {
            x += dx;
            y += dy;
            const Cell* c = cellAt(x, y);
            if (c && c->blocked) {
                endX = x;
                endY = y;
                how = 2;
                return steps + 1;
            }
            if (c && piece(c->piece)) {
                how = 1;
                return steps;
            }
            endX = x;
            endY = y;
            ++steps;
        }
    }

    void place(std::uint32_t object, const Cell& c) {
        if (Object* o = game_.scene.object(object)) {
            o->position.set({float(c.px), float(c.py), o->position[2]});
        }
    }

    static void startMotion(Object3D& m, const std::string& name) {
        if (m.motion != name || m.motionDone) {
            m.motionTime = 0;
        }
        m.motion = name;
        m.motionOnce = false;
        m.motionDone = false;
    }

    void reset();
    bool undo();
    Piece* pickUp(std::uint32_t object, bool open[4]);
    int aim(std::uint32_t object, int tx, int ty, int& outX, int& outY);
    Cell* release(std::uint32_t object, int& pushed);

    Game& game_;
    int held_ = -1, heldCell_ = -1, endCell_ = -1; // +0x160 / +0x162 / +0x164
    std::vector<Piece> pieces_;                    // +0x170
    std::vector<Cell> cells_;                      // +0x17c
    std::vector<Move> moves_;                      // +0x188, newest first
    int size_ = 0;                                 // +0x196
    int originX_ = 0, originY_ = 0;                // +0x198 / +0x19a
    int cols_ = 0, rows_ = 0;                      // +0x19c / +0x19e (with the ring)
    int goalX_ = -1, goalY_ = -1;                  // +0x1a0 / +0x1a2
    std::uint32_t goalName_ = 0;                   // +0x1a4
    int how_ = 0;                                  // +0x1a8
    int models_ = 0;                               // +0x1ac
    float jump_[4] = {0, 0, 0, 0};                 // +0x1c4..0x1d0: end x y z, heading
    int steps_ = 0;                                // +0x1d4
};

// 0x385234: nothing held, no moves, the penguins back on their cells.
void SlipPuzzle::reset() {
    held_ = heldCell_ = endCell_ = -1;
    how_ = 0;
    moves_.clear();
    for (Cell& c : cells_) {
        c.piece = -1;
    }
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        Piece& p = pieces_[i];
        p.x = p.homeX;
        p.y = p.homeY;
        Cell* c = cellAt(p.x, p.y);
        if (!c || c->blocked) {
            continue;
        }
        c->piece = int(i);
        if (game_.scene.object(p.object)) {
            place(p.object, *c);
            if (Object3D* m = game_.scene.object3D(p.model)) {
                m->position.set({c->world[0], c->world[1], c->world[2]});
                startMotion(*m, p.stand);
                m->rotation3.set({0, 0, 0});
                m->visible = true;
            }
        }
    }
}

// 0x1fbe6c: the newest move taken back (the penguin found in the ring cell
// back in it, its model back at the height of a first fall).
bool SlipPuzzle::undo() {
    if (moves_.empty()) {
        return false;
    }
    const Move m = moves_.front();
    Piece* p = piece(m.piece);
    if (!p) {
        return false;
    }
    if (Cell* now = cellAt(p->x, p->y)) {
        now->piece = m.pushed;
    }
    Cell* from = cellAt(m.fromX, m.fromY);
    if (from) {
        from->piece = m.piece;
    }
    p->x = m.fromX;
    p->y = m.fromY;
    if (from) {
        place(p->object, *from);
        if (Object3D* model = game_.scene.object3D(p->model)) {
            model->position.set({from->world[0], from->world[1], from->world[2]});
            startMotion(*model, p->stand);
            model->rotation3.set({0.0f, wrapAngle(m.turn), 0.0f});
            model->visible = true;
        }
    }
    if (Piece* q = piece(m.pushed)) {
        if (Object3D* model = game_.scene.object3D(q->model)) {
            const auto pos = model->position.value();
            model->position.set({pos[0], -6.0f, pos[2]});
        }
    }
    moves_.erase(moves_.begin());
    return true;
}

// 0x1fc00c: the penguin picked up when nothing is held; the directions it
// can move (left, right, up, down); none: not held.
SlipPuzzle::Piece* SlipPuzzle::pickUp(std::uint32_t object, bool open[4]) {
    for (int i = 0; i < 4; ++i) {
        open[i] = false;
    }
    if (held_ >= 0 || heldCell_ >= 0) {
        return nullptr;
    }
    how_ = 0;
    Piece* p = pieceByObject(object);
    if (!p || !cellAt(p->x, p->y)) {
        return nullptr;
    }
    held_ = int(p - pieces_.data());
    heldCell_ = index(p->x, p->y);
    endCell_ = -1;
    static constexpr int kDirs[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    bool any = false;
    for (int d = 0; d < 4; ++d) {
        int ex = 0, ey = 0, how = 0;
        open[d] = slide(p->x, p->y, kDirs[d][0], kDirs[d][1], ex, ey, how) != 0;
        any = any || open[d];
    }
    if (!any) {
        held_ = heldCell_ = endCell_ = -1;
        return nullptr;
    }
    return p;
}

// 0x1fc3b4: the drag at (tx, ty): once a cell's size from the penguin, the
// slide along the larger component; the end cell's pixels; the steps.
int SlipPuzzle::aim(std::uint32_t object, int tx, int ty, int& outX, int& outY) {
    Piece* p = pieceByObject(object);
    const Cell* at = p ? cellAt(p->x, p->y) : nullptr;
    if (!at) {
        return 0;
    }
    const int dy = ty - at->py, dx = tx - at->px;
    const float dist = std::sqrt(float(dy * dy + dx * dx));
    if (!(float(size_) <= dist)) {
        return 0;
    }
    const int ax = std::abs(dx), ay = std::abs(dy);
    int vy = ax < ay ? 1 : 0;
    int vx = ay < ax ? 1 : 0;
    if (dx < 0) {
        vx = -vx;
    }
    if (dy < 0) {
        vy = -vy;
    }
    int ex = p->x, ey = p->y;
    const int steps = slide(p->x, p->y, vx, vy, ex, ey, how_);
    const Cell* end = cellAt(ex, ey);
    if (!end) {
        return 0;
    }
    endCell_ = index(ex, ey);
    outX = std::int16_t(originX_ + ex * size_);
    outY = std::int16_t(originY_ + ey * size_);
    jump_[0] = end->world[0];
    jump_[1] = end->world[1];
    jump_[2] = end->world[2];
    jump_[3] = std::atan2(float(vy), float(-vx)) - kAway;
    steps_ = steps;
    return steps;
}

// 0x1fc23c: the held penguin moved to the aimed cell; `pushed` the penguin
// already there (one fallen into that ring cell before).
SlipPuzzle::Cell* SlipPuzzle::release(std::uint32_t object, int& pushed) {
    Piece* p = piece(held_);
    Cell* from = cell(heldCell_);
    if (!p || p->object != object || !from) {
        return nullptr;
    }
    if (Cell* end = cell(endCell_)) {
        pushed = end->piece;
        if (int(moves_.size()) >= kMoves) {
            moves_.pop_back();
        }
        Move m;
        m.pushed = pushed;
        m.piece = held_;
        if (heldCell_ >= 0 && heldCell_ < int(cells_.size())) {
            m.fromX = heldCell_ % cols_;
            m.fromY = heldCell_ / cols_;
        }
        if (const Object3D* model = game_.scene.object3D(p->model)) {
            m.turn = model->rotation3[1];
        }
        moves_.insert(moves_.begin(), m);
        if (endCell_ >= 0 && endCell_ < int(cells_.size())) {
            p->x = endCell_ % cols_;
            p->y = endCell_ / cols_;
        }
        from->piece = -1;
        end->piece = held_;
    }
    held_ = heldCell_ = endCell_ = -1;
    how_ = 0;
    return from;
}

bool SlipPuzzle::command(int number, Call& c, std::size_t first) {
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
    auto setReg = [&](std::size_t i, const Value& v) {
        if (Value* r = c.reg(first + i)) {
            *r = v;
        }
    };
    switch (number) {
    case 0:
        reset();
        return result(1);
    case 1:
        return result(undo() ? 1 : 0);
    case 10: {
        // Cells across, down; first cell x, y; cell size - with a ring of
        // blocked cells around, and the message object "slip_msg".
        if (count < 5) {
            return false;
        }
        cols_ = std::int16_t(integer(0) + 2);
        rows_ = std::int16_t(integer(1) + 2);
        size_ = integer(4) & 0xffff;
        originX_ = std::int16_t(integer(2) - size_);
        originY_ = std::int16_t(integer(3) - size_);
        const int n = std::max(0, cols_ * rows_);
        pieces_.clear();
        pieces_.reserve(std::size_t(n));
        cells_.assign(std::size_t(n), Cell{});
        moves_.clear();
        for (int y = 0; y < rows_; ++y) {
            for (int x = 0; x < cols_; ++x) {
                Cell& at = *cellAt(x, y);
                at.px = std::int16_t(x * size_ + originX_);
                at.py = std::int16_t(y * size_ + originY_);
            }
        }
        block(0, 0, cols_, 1);
        block(0, rows_ - 1, cols_, 1);
        block(0, 0, 1, rows_);
        block(cols_ - 1, 0, 1, rows_);
        game_.createObject("slip_msg", 1, 132, 9, {}).visible = false;
        return result(1);
    }
    case 11:
        if (count < 4) {
            return false;
        }
        block(std::int16_t(integer(0) + 1), std::int16_t(integer(1) + 1), std::int16_t(integer(2)),
              std::int16_t(integer(3)));
        return result(1);
    case 12: {
        // The cells' 3D positions (spacing across, down, height), centred.
        if (count < 3) {
            return false;
        }
        const float across = c.real(first), down = c.real(first + 1), y = c.real(first + 2);
        const float x0 = float(cols_ - 1) * -(across * 0.5f);
        float z = float(rows_ - 1) * -(down * 0.5f);
        for (int j = 0; j < rows_; ++j) {
            float x = x0;
            for (int i = 0; i < cols_; ++i) {
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
        if (count < 3) {
            return false;
        }
        goalX_ = std::int16_t(integer(0) + 1);
        goalY_ = std::int16_t(integer(1) + 1);
        goalName_ = xq::hash(text(2));
        return result(1);
    case 22: {
        int solved = 0;
        if (goalX_ >= 0 && goalY_ >= 0) {
            if (const Cell* g = cellAt(goalX_, goalY_)) {
                if (const Piece* p = piece(g->piece); p && game_.scene.object(p->object)) {
                    solved = playingHash(p->object) == goalName_ ? 1 : 0;
                }
            }
        }
        return result(solved);
    }
    case 30: {
        int n = 0;
        for (const Piece& p : pieces_) {
            if (cellAt(p.x, p.y)) {
                ++n;
            }
        }
        c.result = Value::ofInt(n);
        return true;
    }
    case 31: {
        // Penguins still on the ice; none once the goal penguin is in.
        int n = 0;
        for (const Piece& p : pieces_) {
            const Cell* at = cellAt(p.x, p.y);
            if (at && !at->blocked) {
                ++n;
            } else if (at && game_.scene.object(p.object) && playingHash(p.object) == goalName_) {
                n = 0;
                break;
            }
        }
        c.result = Value::ofInt(n);
        return true;
    }
    case 40: {
        // A penguin "slip_NN" (x, y, animation set, animation, normal and
        // slide handler blocks), screen 1, priority 111, kind 7.
        if (count < 6) {
            return false;
        }
        const int x = std::int16_t(integer(0) + 1), y = std::int16_t(integer(1) + 1);
        Cell* at = cellAt(x, y);
        if (!at) {
            return false;
        }
        char name[16];
        std::snprintf(name, sizeof name, "slip_%02x", unsigned(pieces_.size()));
        const std::string animation = text(3);
        const Value values[] = {Value::ofText(name), Value::ofInt(7), Value::ofText(animation)};
        Object& o = game_.createObject(name, 1, 111, 7, values);
        Piece p;
        p.object = o.hash;
        p.homeX = p.x = x;
        p.homeY = p.y = y;
        pieces_.push_back(p);
        at->piece = int(pieces_.size()) - 1;
        o.position.set({float(at->px), float(at->py), o.position[2]});
        const float half = float(size_ >> 1);
        o.areas.push_back({2, -half, -half, float(size_), float(size_)});
        const std::string handlers[] = {text(4), text(5), std::string()};
        game_.setHandlers(o, 3, handlers);
        game_.setAnimationSet(o, xq::hash(text(2)));
        game_.playAnimation(o, Value::ofText(animation), 0);
        c.result = Value::ofText(name);
        return true;
    }
    case 41: {
        // Pick up: the four directions into the registers.
        if (count < 5) {
            return false;
        }
        bool open[4];
        const Piece* p = pickUp(xq::hash(text(0)), open);
        for (int i = 0; i < 4; ++i) {
            setReg(std::size_t(1 + i), Value::ofInt(open[i] ? 1 : 0));
        }
        return result(p ? 1 : 0);
    }
    case 42: {
        // Release: whether it fell in, the penguin found there.
        if (count < 3) {
            return false;
        }
        const int how = how_;
        int pushed = -1;
        if (!release(xq::hash(text(0)), pushed)) {
            return result(0);
        }
        setReg(1, Value::ofInt(how == 2 ? 1 : 0));
        if (const Piece* q = piece(pushed)) {
            setReg(2, Value::ofText(q->modelName));
        }
        return result(1);
    }
    case 43: {
        // The drag: the end cell's pixels and the steps.
        if (count < 6) {
            return false;
        }
        int ox = 0, oy = 0;
        const int steps = aim(xq::hash(text(0)), integer(1), integer(2), ox, oy);
        if (steps == 0) {
            return result(0);
        }
        setReg(3, Value::ofInt(ox));
        setReg(4, Value::ofInt(oy));
        setReg(5, Value::ofInt(steps));
        return result(1);
    }
    case 44:
        held_ = heldCell_ = -1;
        return true;
    case 45: {
        // The pixels of the cell (dx, dy) from a penguin.
        if (count < 5) {
            return false;
        }
        const Piece* p = pieceByObject(xq::hash(text(0)));
        if (!p) {
            return false;
        }
        const Cell* at = cellAt(p->x + std::int16_t(integer(3)), p->y + std::int16_t(integer(4)));
        if (!at) {
            return false;
        }
        setReg(1, Value::ofInt(at->px));
        setReg(2, Value::ofInt(at->py));
        return result(1);
    }
    case 50: {
        // The next penguin's model "mdl_NN" (resource, standing, slide
        // start, sliding, slide end and joy motions), screen 0, priority
        // 120, kind 5, at its cell.
        if (count < 6) {
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
        Object3D& m = game_.scene.create3D(name, 0, 120, 5);
        m.values.assign(16, Value{});
        const Value values[] = {Value::ofText(name),     Value::ofInt(5),
                                Value::ofText(p->stand), Value::ofText(text(2)),
                                Value::ofText(text(3)),  Value::ofText(text(4)),
                                Value::ofText(text(5))};
        for (std::size_t i = 0; i < std::size(values); ++i) {
            m.values[i] = values[i];
        }
        const adventure56::Resource* r = game_.scene.resource(xq::hash(text(0)));
        m.model = r ? r->model : nullptr;
        startMotion(m, p->stand);
        const Cell* at = cellAt(p->x, p->y);
        if (!at) {
            return false;
        }
        m.position.set({at->world[0], at->world[1], at->world[2]});
        ++models_;
        return result(1);
    }
    case 51:
        // The penguin's model name into the register.
        if (count < 2) {
            return false;
        }
        if (const Piece* p = pieceByObject(xq::hash(text(0)))) {
            setReg(1, Value::ofText(p->modelName));
        }
        return result(1);
    case 52:
        // The slide's end (x, y, z), heading and steps into registers.
        if (count < 5) {
            return false;
        }
        for (std::size_t i = 0; i < 4; ++i) {
            setReg(i, Value::ofFloat(jump_[i]));
        }
        setReg(4, Value::ofInt(steps_));
        return result(1);
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeSlipPuzzle(Game& game) {
    return std::make_unique<SlipPuzzle>(game);
}

} // namespace picarat::adventure5
