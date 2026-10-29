// The fifth adventure's flipping puzzles (catalogue type 36, loop 3190;
// puzzles 45, 120, 138): cats on a grid jump over a line of cats onto the
// free cell right behind it, and every cat jumped over turns to the other
// colour; the puzzle is solved when every cat shows the asked colour.
//
// Reconstructed from the loop's command handler 0x2e6cb0 (vtable 0x402ed0,
// constructor 0x37ea9c), its input hook 0x2e7f6c, its draw hook 0x2e819c
// and helpers: reset 0x386f78, undo 0x2e87c0, pick up 0x2e896c (landing
// cells 0x2e85d0 / 0x2e84b8), drop 0x2e8af0, turning 0x279cb0.
//
// How it plays: the board is a grid of cells (command 10: size, first cell,
// cell size, the animation set, the white and black cat animations, the
// marker animations and the white and black colour motions). The cats (40)
// are lower-screen objects with the script's normal and slide handlers;
// each has a model on the upper screen (50, "mdl_NN") standing on the
// cell's 3D position (12). A cat may be picked up (41) only when it can
// jump: in one of the eight directions its neighbour holds a cat, and the
// line of cats there ends in a free cell on the board. Otherwise it plays
// its "<animation>_non" animation. While a cat is held, the marker "ori"
// stands on its cell and "dst_NN" on every landing cell. Dropped (42), it
// goes to the landing cell nearest to the drop point when that is nearer
// than its own cell, else back. A jump turns the cats jumped over at once
// on the lower screen and is recorded for undo (1; at most 30 moves); the
// model jumps over 24 frames, and once it has landed (51) the script asks
// for the models jumped over to change colour (52). The judge (22) asks
// that every cat plays the animation named by command 21.
//
// The draw hook draws the model that just jumped fully, then for each of
// the two colours the first model of that colour fully and the others as
// copies of its first mesh with their own transform (0x140b84); the port
// draws every model fully, which shows the same picture.
#include "puzzles/a5_special.hpp"
#include <cstdio>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using adventure56::Object3D;
using xq::Call;
using xq::Value;

// 0x4437a4 (filled once by 0x2e85d0 / 0x12b404, guard 0x412a80): the eight
// directions by number.
constexpr int kStep[8][2] = {{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};

// The undo list's size (0x2e8af0: the oldest of 30 moves is dropped).
constexpr std::size_t kMoves = 30;

// 0x2e8ea0 / 0x2e8ea4 / 0x2e7f44: the jump's frames (position, curve 0),
// the turn back to straight ahead after a drop that did not jump and after
// landing.
constexpr float kJumpFrames = 24.0f;
constexpr float kTurnFrames = 5.0f;

class FlipPuzzle final : public SpecialPuzzle {
public:
    explicit FlipPuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    void input() override;

private:
    // A cell (0x34 bytes).
    struct Cell {
        int px = 0, py = 0;                                // +0x00: pixels
        int cat = -1;                                      // +0x04
        int landing[8] = {-1, -1, -1, -1, -1, -1, -1, -1}; // +0x06: per direction
        int over[8] = {0, 0, 0, 0, 0, 0, 0, 0};            // +0x16: cats jumped over
        // Sixth adventure only (commands 60, 61): no landing on this cell
        // (0x21701c); a direction with a second, doubled jump (0x326494;
        // kept, not applied: no script sends 61).
        bool blocked = false;       // +0x26
        int longJump = -1;          // +0x27
        float world[3] = {0, 0, 0}; // +0x28: 3D position (62 sets y)
    };

    // A cat (0x68 bytes).
    struct Cat {
        std::uint32_t animation = 0; // +0x00: the first animation
        std::uint32_t object = 0;    // +0x04
        int homeX = 0, homeY = 0;    // +0x08
        std::uint32_t model = 0;     // +0x0c
        std::string modelName;
        std::string stand; // +0x10: standing motion
        std::string jump;  // +0x24
        std::string white; // +0x38: colour motions
        std::string black; // +0x4c
        // The sixth adventure names the colours by mesh hash (cat +0x14,
        // +0x18): the model shows one costume mesh and hides the other.
        std::uint32_t whiteMesh = 0, blackMesh = 0;
        int x = -1, y = -1;   // +0x60
        bool isBlack = false; // +0x64
    };

    // An undo record (10 bytes).
    struct Move {
        int over = 0, direction = 0, cat = -1, fromX = 0, fromY = 0;
    };

    int index(int x, int y) const {
        return unsigned(x) < unsigned(width_) && unsigned(y) < unsigned(height_) ? width_ * y + x
                                                                                 : -1;
    }

    Cell* cell(int i) {
        return i >= 0 && i < int(cells_.size()) ? &cells_[std::size_t(i)] : nullptr;
    }

    Cell* cellAt(int x, int y) { return cell(index(x, y)); }

    Cat* cat(int i) { return i >= 0 && i < int(cats_.size()) ? &cats_[std::size_t(i)] : nullptr; }

    void place(std::uint32_t object, const Cell& at) {
        if (Object* o = game_.scene.object(object)) {
            o->position.set({float(at.px), float(at.py), o->position[2]});
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
        for (int d = 0; d < 8; ++d) {
            show(marker(d), false);
        }
        show("ori", false);
    }

    // 0x2936c0 (6810's core) with flags and the start frame 0.
    static void startMotion(Object3D& m, const std::string& name, std::uint32_t flags) {
        if (m.motion != name || (flags & 4) || m.motionDone) {
            m.motionTime = 0;
        }
        m.motion = name;
        m.motionOnce = (flags & 8) != 0;
        m.motionDone = false;
    }

    // 0x39b24c (6820's core): the colour is a face motion.
    static void setFace(Object3D& m, const std::string& name) {
        m.faces[0].motion = name;
        m.faces[0].time = 0;
    }

    // A model's colour. Fifth adventure: the colour's face motion. Sixth
    // (turning 0x217260, model creation 0x325e38): the cat's state set
    // shows its mesh +0x14 and hides +0x18, clear the reverse.
    void setColour(Object3D& m, const Cat& k, bool black) const {
        if (game_.edition != Edition::Sixth) {
            setFace(m, black ? k.black : k.white);
            return;
        }
        const std::uint32_t show = black ? k.whiteMesh : k.blackMesh;
        const std::uint32_t hide = black ? k.blackMesh : k.whiteMesh;
        m.hiddenMeshes.erase(show);
        m.hiddenMeshes.insert(hide);
    }

    void reset();
    bool undo();
    bool landings(Cell& at);
    bool pickUp(std::uint32_t object);
    Cell* drop(std::uint32_t object);
    void turn(int x, int y, int direction, int count, bool cats, bool models);
    bool solved();

    Game& game_;
    int held_ = -1, heldCell_ = -1; // +0x160 / +0x162
    int jumper_ = -1;               // +0x164: the cat whose model jumps
    std::vector<Cat> cats_;         // +0x170
    std::vector<Cell> cells_;       // +0x17c
    std::vector<Move> moves_;       // +0x188, newest first
    int originX_ = 0, originY_ = 0; // +0x198 / +0x19a
    int size_ = 0;                  // +0x196
    int width_ = 0, height_ = 0;    // +0x19c / +0x19e
    std::uint32_t set_ = 0;         // +0x1a0: the animation set
    std::uint32_t whiteCat_ = 0;    // +0x1b4
    std::uint32_t blackCat_ = 0;    // +0x1b8
    std::uint32_t goal_ = 0;        // +0x1bc
    std::string whiteName_;         // +0x1c0
    std::string blackName_;         // +0x1d4
    int models_ = 0;                // +0x1f4
};

// 0x386f78: nothing held, no moves, every cat back on its first cell with
// its first animation, its model standing and showing the white colour.
void FlipPuzzle::reset() {
    held_ = heldCell_ = jumper_ = -1;
    moves_.clear();
    for (Cell& c : cells_) {
        c.cat = -1;
    }
    for (std::size_t i = 0; i < cats_.size(); ++i) {
        Cat& k = cats_[i];
        k.x = k.homeX;
        k.y = k.homeY;
        Cell* at = cellAt(k.x, k.y);
        if (!at) {
            continue;
        }
        at->cat = int(i);
        Object* o = game_.scene.object(k.object);
        if (!o) {
            continue;
        }
        place(k.object, *at);
        game_.playAnimation(*o, Value::ofUint(k.animation), 0);
        k.isBlack = blackCat_ == k.animation;
        if (Object3D* m = game_.scene.object3D(k.model)) {
            m->position.set({at->world[0], at->world[1], at->world[2]});
            startMotion(*m, k.stand, 0);
            m->rotation3.set({0, 0, 0});
            if (game_.edition == Edition::Sixth) {
                setColour(*m, k, k.isBlack); // hypothesis: the reset shows each cat's own
            } else {
                setFace(*m, k.white);
            }
        }
    }
}

// 0x279cb0: the `count` cats from (x, y) on in `direction` change colour:
// `cats` turns their lower-screen animation (and their colour), `models`
// shows their model's colour motion.
void FlipPuzzle::turn(int x, int y, int direction, int count, bool cats, bool models) {
    const int* s = kStep[direction & 7];
    for (int i = 0; i < count; ++i) {
        x = std::int16_t(x + s[0]);
        y = std::int16_t(y + s[1]);
        Cell* at = cellAt(x, y);
        Cat* k = at ? cat(at->cat) : nullptr;
        if (!k) {
            continue;
        }
        if (cats) {
            Object* o = game_.scene.object(k->object);
            if (!o) {
                continue;
            }
            game_.playAnimation(*o, Value::ofUint(k->isBlack ? whiteCat_ : blackCat_), 0);
            k->isBlack = !k->isBlack;
        }
        if (models) {
            if (Object3D* m = game_.scene.object3D(k->model)) {
                setColour(*m, *k, k->isBlack);
            }
        }
    }
}

// 0x2e87c0: the newest move taken back; the cats jumped over turn back.
// The cat's standing motion is asked for with 0x18806c, which only plays
// 2D animations, so the model's motion goes on.
bool FlipPuzzle::undo() {
    if (moves_.empty()) {
        return false;
    }
    const Move m = moves_.front();
    Cat* k = cat(m.cat);
    if (!k) {
        return false;
    }
    if (Cell* now = cellAt(k->x, k->y)) {
        now->cat = -1;
    }
    Cell* from = cellAt(m.fromX, m.fromY);
    if (from) {
        from->cat = m.cat;
    }
    k->x = m.fromX;
    k->y = m.fromY;
    if (from) {
        place(k->object, *from);
        if (Object3D* model = game_.scene.object3D(k->model)) {
            model->position.set({from->world[0], from->world[1], from->world[2]});
            model->rotation3.set({0, 0, 0});
        }
    }
    turn(m.fromX, m.fromY, m.direction, m.over, true, true);
    moves_.erase(moves_.begin());
    return true;
}

// 0x2e85d0 / 0x2e84b8: the landing cell in each direction from `at` (the
// first free cell behind one or more cats) and how many cats lie between.
bool FlipPuzzle::landings(Cell& at) {
    const int i = int(&at - cells_.data());
    const int x0 = i % width_, y0 = i / width_;
    bool any = false;
    for (int d = 0; d < 8; ++d) {
        at.landing[d] = -1;
        at.over[d] = 0;
        int x = std::int16_t(x0 + kStep[d][0]), y = std::int16_t(y0 + kStep[d][1]);
        const Cell* next = cellAt(x, y);
        if (!next || next->cat < 0) {
            continue;
        }
        ++at.over[d];
        for (;;) {
            x = std::int16_t(x + kStep[d][0]);
            y = std::int16_t(y + kStep[d][1]);
            next = cellAt(x, y);
            if (!next) {
                break;
            }
            if (next->cat >= 0) {
                ++at.over[d];
                continue;
            }
            if (next->blocked) {
                break;
            }
            at.landing[d] = index(x, y);
            any = true;
            break;
        }
    }
    return any;
}

// 0x2e896c: the cat (by its object) picked up when nothing is held and it
// can jump; otherwise it plays "<white or black animation>_non".
bool FlipPuzzle::pickUp(std::uint32_t object) {
    if (held_ >= 0 || heldCell_ >= 0) {
        return false;
    }
    Cat* k = nullptr;
    for (Cat& each : cats_) {
        if (each.object == object) {
            k = &each;
            break;
        }
    }
    Cell* at = k ? cellAt(k->x, k->y) : nullptr;
    if (!at) {
        return false;
    }
    held_ = int(k - cats_.data());
    heldCell_ = index(k->x, k->y);
    if (landings(*at)) {
        return true;
    }
    held_ = heldCell_ = -1;
    if (Object* o = game_.scene.object(k->object)) {
        const std::string name = (k->isBlack ? blackName_ : whiteName_) + "_non";
        game_.playAnimation(*o, Value::ofText(name), 0);
    }
    return false;
}

// 0x2e8af0: the held cat dropped where its object now stands. Returns the
// cell it ends on.
FlipPuzzle::Cell* FlipPuzzle::drop(std::uint32_t object) {
    Cat* k = cat(held_);
    Cell* at = cell(heldCell_);
    Object* o = k && k->object == object ? game_.scene.object(object) : nullptr;
    if (!k || !at || !o) {
        return nullptr;
    }
    const int x = int(o->position[0] + o->offset[0]);
    const int y = int(o->position[1] + o->offset[1]);
    auto distance = [&](const Cell& c) {
        const int dx = std::int16_t(x - c.px), dy = std::int16_t(y - c.py);
        return dx * dx + dy * dy;
    };
    int best = distance(*at), way = -1;
    for (int d = 0; d < 8; ++d) {
        if (const Cell* to = cell(at->landing[d])) {
            const int e = distance(*to);
            if (e < best) {
                best = e;
                way = d;
            }
        }
    }
    Cell* end = at;
    if (way >= 0) {
        at->cat = -1;
        const int fromX = heldCell_ % width_, fromY = heldCell_ / width_;
        if (moves_.size() >= kMoves) {
            moves_.pop_back();
        }
        moves_.insert(moves_.begin(), Move{at->over[way], way, held_, fromX, fromY});
        turn(fromX, fromY, way, at->over[way], true, false);
        end = &cells_[std::size_t(at->landing[way])];
        // The cat's cell from the landing cell's number, divided by the
        // board's height (0x2bf5b4 with +0x19e; the boards are square).
        const int e = int(end - cells_.data());
        k->x = e % height_;
        k->y = e / height_;
        end->cat = held_;
    }
    jumper_ = -1;
    if (Object3D* m = game_.scene.object3D(k->model)) {
        if (way < 0) {
            m->rotation3.start({0, 0, 0}, kTurnFrames, 0);
        } else {
            m->position.start({end->world[0], end->world[1], end->world[2]}, kJumpFrames, 0);
            startMotion(*m, k->jump, 8);
            jumper_ = held_;
        }
    }
    held_ = heldCell_ = -1;
    hideMarkers();
    return end;
}

// The judge (22): every cat whose object is there plays the goal
// animation (0x2d021c: the playing animation's name).
bool FlipPuzzle::solved() {
    for (const Cat& k : cats_) {
        const Object* o = game_.scene.object(k.object);
        if (!o) {
            continue;
        }
        if (!o->animation || o->playing < 0 || o->playing >= int(o->animation->animations.size())) {
            return false;
        }
        if (o->animation->animations[std::size_t(o->playing)].hash != goal_) {
            return false;
        }
    }
    return true;
}

// 0x2e7f6c: while a cat is held, "ori" on its cell and "dst_NN" on each
// landing cell; otherwise none.
void FlipPuzzle::input() {
    if (held_ < 0) {
        hideMarkers();
        return;
    }
    const Cell* at = cell(heldCell_);
    if (!at || !game_.scene.resource(set_)) {
        return;
    }
    Object* o = game_.scene.object(xq::hash("ori"));
    if (!o) {
        return;
    }
    o->visible = true;
    o->position.set({float(at->px), float(at->py), o->position[2]});
    for (int d = 0; d < 8; ++d) {
        const Cell* to = cell(at->landing[d]);
        if (!to) {
            continue;
        }
        if (Object* m = game_.scene.object(xq::hash(marker(d)))) {
            m->visible = true;
            m->position.set({float(to->px), float(to->py), m->position[2]});
        }
    }
}

bool FlipPuzzle::command(int number, Call& c, std::size_t first) {
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
        // Cells across, down; first cell x, y; cell size; the animation set;
        // the white and black cat animations; the "from" and "to" marker
        // animations; the white and black colour motions (kept for the
        // draw hook).
        if (count < 10) {
            return false;
        }
        set_ = xq::hash(text(5));
        width_ = std::int16_t(integer(0));
        height_ = std::int16_t(integer(1));
        originX_ = integer(2) & 0xffff;
        originY_ = integer(3) & 0xffff;
        size_ = integer(4) & 0xffff;
        const int cells = std::max(0, width_ * height_);
        cats_.clear();
        cats_.reserve(std::size_t(cells));
        cells_.assign(std::size_t(cells), Cell{});
        moves_.clear();
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                Cell& at = *cellAt(x, y);
                at.px = std::int16_t(x * size_ + originX_);
                at.py = std::int16_t(y * size_ + originY_);
            }
        }
        whiteName_ = text(6);
        blackName_ = text(7);
        whiteCat_ = xq::hash(whiteName_);
        blackCat_ = xq::hash(blackName_);
        // The markers (screen 1, priority 111, kind 9), hidden; the message
        // object "othl_msg" (priority 132, kind 9).
        auto markerObject = [&](const std::string& name, const std::string& animation) {
            Object& o = game_.createObject(name, 1, 111, 9, {});
            game_.setAnimationSet(o, set_);
            game_.playAnimation(o, Value::ofText(animation), 0);
            o.visible = false;
        };
        markerObject("ori", text(8));
        for (int d = 0; d < 8; ++d) {
            markerObject(marker(d), text(9));
        }
        game_.createObject("othl_msg", 1, 132, 9, {}).visible = false;
        return result(1);
    }
    case 11:
        // Taken but unused (four operands).
        if (count < 4) {
            return false;
        }
        return result(1);
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
        // The animation every cat must play.
        if (count < 1) {
            return false;
        }
        goal_ = xq::hash(text(0));
        return result(1);
    case 22:
        return result(solved() ? 1 : 0);
    case 30: {
        // Cats on the board.
        int n = 0;
        for (const Cat& k : cats_) {
            if (cellAt(k.x, k.y)) {
                ++n;
            }
        }
        c.result = Value::ofInt(n);
        return true;
    }
    case 31:
        c.result = Value::ofInt(0);
        return true;
    case 40: {
        // A cat "othl_NN" (x, y, animation set, animation, normal and slide
        // handler blocks) on screen 1 (priority 111, kind 7), touchable over
        // its cell. The result is its name.
        if (count < 6) {
            return false;
        }
        const int x = std::int16_t(integer(0)), y = std::int16_t(integer(1));
        Cell* at = cellAt(x, y);
        if (!at) {
            return false;
        }
        char name[16];
        std::snprintf(name, sizeof name, "othl_%02x", unsigned(cats_.size()));
        const Value values[] = {Value::ofText(name), Value::ofInt(7), Value::ofInt(size_ >> 1)};
        Object& o = game_.createObject(name, 1, 111, 7, values);
        const std::string animation = text(3);
        Cat k;
        k.animation = xq::hash(animation);
        k.object = o.hash;
        k.homeX = k.x = x;
        k.homeY = k.y = y;
        at->cat = int(cats_.size());
        o.position.set({float(at->px), float(at->py), o.position[2]});
        const float half = float(size_ >> 1);
        o.areas.push_back({2, -half, -half, float(size_), float(size_)});
        const std::string handlers[] = {text(4), text(5), std::string()};
        game_.setHandlers(o, 3, handlers);
        game_.setAnimationSet(o, xq::hash(text(2)));
        game_.playAnimation(o, Value::ofText(animation), 0);
        k.isBlack = blackCat_ == k.animation;
        cats_.push_back(k);
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
        // The next cat's model "mdl_NN" (resource, standing, jump, white and
        // black motions) on the upper screen (priority 120, kind 5) at its
        // cell, showing the white colour.
        if (count < 5) {
            return false;
        }
        Cat* k = cat(models_);
        if (!k) {
            return false;
        }
        char name[16];
        std::snprintf(name, sizeof name, "mdl_%02x", unsigned(models_));
        k->modelName = name;
        k->model = xq::hash(name);
        const adventure56::Resource* r = game_.scene.resource(xq::hash(text(0)));
        // The sixth adventure passes the motions as hashes: their names
        // from the model.
        auto motionName = [&](std::size_t i) {
            const Value& v = c.at(first + i);
            if (v.isText() || !r || !r->model) {
                return text(i);
            }
            for (const auto& mo : r->model->motions) {
                if (mo.hash == v.toUint() || xq::hash(mo.name) == v.toUint()) {
                    return mo.name;
                }
            }
            return std::string();
        };
        auto hashOf = [&](std::size_t i) {
            const Value& v = c.at(first + i);
            return v.isText() ? xq::hash(v.text) : v.toUint();
        };
        k->stand = motionName(1);
        k->jump = motionName(2);
        k->white = text(3);
        k->black = text(4);
        k->whiteMesh = hashOf(3);
        k->blackMesh = hashOf(4);
        Object3D& m = game_.scene.create3D(name, 0, 120, 5);
        m.values.assign(16, Value{});
        const Value values[] = {Value::ofText(name),     Value::ofInt(5),
                                Value::ofText(k->stand), Value::ofText(k->jump),
                                Value::ofText(k->white), Value::ofText(k->black)};
        for (std::size_t i = 0; i < std::size(values); ++i) {
            m.values[i] = values[i];
        }
        m.model = r ? r->model : nullptr;
        startMotion(m, k->stand, 0);
        Cell* at = cellAt(k->x, k->y);
        if (!at) {
            return false;
        }
        m.position.set({at->world[0], at->world[1], at->world[2]});
        setColour(m, *k, false);
        ++models_;
        return result(1);
    }
    case 51: {
        // While the jumping model is on its way 1; once it has landed it
        // stands and turns straight ahead (0).
        const Cat* k = cat(jumper_);
        Object3D* m = k ? game_.scene.object3D(k->model) : nullptr;
        if (!m) {
            return result(0);
        }
        if (m->position.moving()) {
            return result(1);
        }
        startMotion(*m, k->stand, 0);
        m->rotation3.start({0, 0, 0}, kTurnFrames, 0);
        return result(0);
    }
    case 52:
        // The models jumped over in the newest move show their colour.
        if (jumper_ >= 0 && !moves_.empty()) {
            const Move& m = moves_.front();
            turn(m.fromX, m.fromY, m.direction, m.over, false, true);
        }
        jumper_ = -1;
        return result(1);
    case 60:
    case 61:
    case 62: {
        // Sixth adventure only (0x325fd4 / 0x326034 / 0x3260ac): a cell
        // (x, y) blocked for landing; its long-jump direction (x, y, d); its
        // height (x, y, h: the 3D position's y, h read as an integer).
        if (game_.edition != Edition::Sixth || count < (number == 60 ? 2u : 3u)) {
            return false;
        }
        Cell* at = cellAt(std::int16_t(c.integer(first)), std::int16_t(c.integer(first + 1)));
        if (!at) {
            return false;
        }
        if (number == 60) {
            at->blocked = true;
        } else if (number == 61) {
            at->longJump = std::int8_t(c.integer(first + 2));
        } else {
            at->world[1] = float(c.integer(first + 2));
        }
        return result(1);
    }
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeFlipPuzzle(Game& game) {
    return std::make_unique<FlipPuzzle>(game);
}

} // namespace picarat::adventure5
