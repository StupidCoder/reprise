// The sixth adventure's rope puzzles (loop 3230, class "Cut"; catalogue
// type 10, puzzles 137 and 155): a grid of posts, rope drawn between
// neighbouring posts by sliding the stylus, jewels and thieves on the
// squares. The rope has to make one closed loop through every post that
// shuts the thieves in without a jewel beside them.
//
// Reconstructed from the class's command handler 0x29bb7c (vtable
// 0x3c708c), its input hook 0x29c4fc and judge 0x29d470, and from the rope
// board the sixth adventure's line puzzles share (the member at +0x1d8:
// set-up 0x2305e8, press 0x205110, slide 0x292ad0, release 0x2928f0, update
// and drawing 0x230f48, reset 0x2317b8; also used by four other classes).
//
// How it plays: a press within the snap radius of a post that has an
// allowed edge starts a stroke there. While sliding, the neighbouring post
// nearest the pen becomes the target; once the pen is nearer to it than to
// the stroke's post, the edge between them flips between drawn and not
// drawn (fixed rope stays) and the stroke moves on. Each step plays the draw
// or erase sound, calls the script's step block (command 20) with whether
// the edge is drawn now, and redraws the rope as sprites. The judge button
// stays off while the player has drawn nothing.
//
// Not reconstructed: the board's other modes (flags +0xb8: start points,
// at most two rope ends per post, tapping an edge to erase it, commands 4..13);
// the rope puzzles never turn them on.
#include "puzzles/a5_special.hpp"
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using xq::Call;
using xq::Value;

// The rope board (the member at +0x1d8): posts in a grid and the edges
// between neighbours.
class RopeBoard {
public:
    // Edge flags (+0xc).
    enum : std::uint8_t { kAllowed = 1, kDrawn = 2, kFixed = 8 };

    // An edge's loop state (+8): 1 on a closed loop that misses posts, 2 on
    // the loop through every post, 0 otherwise.
    struct Edge {
        int a = -1, b = -1; // posts; a is the upper or left one
        std::uint8_t flags = 0;
        std::uint8_t loop = 0;
    };

    struct Post {
        int x = 0, y = 0;                         // pixels
        std::array<int, 4> edges{-1, -1, -1, -1}; // up, down, left, right
    };

    // 0x2305e8: posts at origin + size x their index (truncated), the snap
    // radius for presses.
    void setup(int originX, int originY, int across, int down, float size, float snap) {
        across_ = across;
        down_ = down;
        snap_ = snap;
        posts_.assign(std::size_t(std::max(0, (across + 1) * (down + 1))), Post{});
        for (int y = 0; y <= down; ++y) {
            for (int x = 0; x <= across; ++x) {
                Post& p = posts_[std::size_t(post(x, y))];
                p.x = originX + int(size * float(x));
                p.y = originY + int(size * float(y));
            }
        }
        // 0x2301d0: the across x (down + 1) horizontal edges first, then the
        // (across + 1) x down vertical ones.
        edges_.assign(std::size_t(std::max(0, across * (down + 1) + (across + 1) * down)), Edge{});
        for (int y = 0; y <= down; ++y) {
            for (int x = 0; x <= across; ++x) {
                Post& p = posts_[std::size_t(post(x, y))];
                p.edges = {y > 0 ? vertical(x, y - 1) : -1, y < down ? vertical(x, y) : -1,
                           x > 0 ? horizontal(x - 1, y) : -1, x < across ? horizontal(x, y) : -1};
                if (x < across) {
                    edges_[std::size_t(horizontal(x, y))] = {post(x, y), post(x + 1, y), 0, 0};
                }
                if (y < down) {
                    edges_[std::size_t(vertical(x, y))] = {post(x, y), post(x, y + 1), 0, 0};
                }
            }
        }
        reset(true);
    }

    int across() const { return across_; }

    int down() const { return down_; }

    const std::vector<Post>& posts() const { return posts_; }

    const std::vector<Edge>& edges() const { return edges_; }

    int drawnCount() const { return drawnCount_; }

    // The edge between two neighbouring posts (grid coordinates), -1 for none.
    int edgeBetween(int x1, int y1, int x2, int y2) const {
        if (std::min(x1, x2) < 0 || std::min(y1, y2) < 0 || std::max(x1, x2) > across_ ||
            std::max(y1, y2) > down_) {
            return -1;
        }
        if (y1 == y2 && std::abs(x1 - x2) == 1) {
            return horizontal(std::min(x1, x2), y1);
        }
        if (x1 == x2 && std::abs(y1 - y2) == 1) {
            return vertical(x1, std::min(y1, y2));
        }
        return -1;
    }

    void setFlag(int edge, std::uint8_t flag, bool on) {
        if (edge >= 0 && std::size_t(edge) < edges_.size()) {
            auto& f = edges_[std::size_t(edge)].flags;
            f = std::uint8_t(on ? f | flag : f & ~flag);
        }
    }

    // 0x2317b8: every edge undrawn (fixed rope only with `everything`), no
    // stroke.
    void reset(bool everything) {
        for (Edge& e : edges_) {
            if (everything || !(e.flags & kFixed)) {
                e.flags &= std::uint8_t(~kDrawn);
            }
        }
        drawnCount_ = 0;
        current_ = -1;
        stroking_ = false;
    }

    // The step a stroke made this frame (Update: sound, script block,
    // loops), cleared at each input.
    struct Step {
        int edge = -1;
        bool drawn = false;
    };

    const Step& step() const { return step_; }

    // 0x230360 with 0x205110 / 0x292ad0 / 0x2928f0.
    void input(bool pressed, bool touching, bool released, int x, int y) {
        step_ = {};
        if (pressed) {
            press(x, y);
        } else if (touching) {
            slide(x, y);
        } else if (released) {
            current_ = -1;
            stroking_ = false;
        }
    }

    // The update's loop pass (0x230f48, after a step): drawn edges on a
    // closed loop of posts that each have two drawn edges get 1, or 2 when
    // that loop runs through every post.
    void classifyLoops() {
        for (Edge& e : edges_) {
            if (e.flags & kDrawn) {
                e.loop = 0;
            }
        }
        std::vector<bool> seen(posts_.size(), false);
        for (std::size_t start = 0; start < posts_.size(); ++start) {
            if (seen[start]) {
                continue;
            }
            std::vector<int> loop;
            std::vector<bool> visited(posts_.size(), false);
            const std::vector<int> walk = walkFrom(int(start), visited, &loop, true);
            if (walk.empty()) {
                continue;
            }
            bool every = true;
            for (std::size_t i = 0; i < posts_.size(); ++i) {
                every = every && visited[i];
                seen[i] = seen[i] || visited[i];
            }
            for (const int e : loop) {
                edges_[std::size_t(e)].loop = every ? 2 : 1;
            }
        }
    }

    // A walk along drawn edges from `start` (the judge's and the loop
    // pass's): at each post the first drawn edge, in the order up, down,
    // left, right, to a post not visited yet or back to the start. Returns
    // the posts walked when it gets back to the start, empty when it stops
    // (with `everyDegree`, also when a post on the way has other than two
    // drawn edges; the judge checks that only for the start).
    std::vector<int> walkFrom(int start, std::vector<bool>& visited, std::vector<int>* edgesWalked,
                              bool everyDegree) const {
        std::vector<int> path{start};
        visited[std::size_t(start)] = true;
        int at = start, previous = -1;
        while (true) {
            if ((everyDegree || at == start) && degree(at) != 2) {
                return {};
            }
            int next = -1, through = -1;
            for (const int e : posts_[std::size_t(at)].edges) {
                if (e < 0 || !(edges_[std::size_t(e)].flags & kDrawn)) {
                    continue;
                }
                const Edge& edge = edges_[std::size_t(e)];
                const int other = edge.a == at ? edge.b : edge.a;
                if (other == previous || (visited[std::size_t(other)] && other != start)) {
                    continue;
                }
                next = other;
                through = e;
                break;
            }
            if (next < 0) {
                return {};
            }
            if (edgesWalked) {
                edgesWalked->push_back(through);
            }
            if (next == start) {
                return path;
            }
            visited[std::size_t(next)] = true;
            path.push_back(next);
            previous = at;
            at = next;
        }
    }

    int degree(int p) const {
        int n = 0;
        for (const int e : posts_[std::size_t(p)].edges) {
            n += e >= 0 && (edges_[std::size_t(e)].flags & kDrawn) ? 1 : 0;
        }
        return n;
    }

private:
    int post(int x, int y) const { return y * (across_ + 1) + x; }

    int horizontal(int x, int y) const { return y * across_ + x; }

    int vertical(int x, int y) const { return across_ * (down_ + 1) + y * (across_ + 1) + x; }

    static float distance2(int ax, int ay, int bx, int by) {
        const float dx = float(std::int16_t(ax - bx)), dy = float(std::int16_t(ay - by));
        return dx * dx + dy * dy;
    }

    // 0x234e90: the post nearest (x, y) within `radius` (0: anywhere).
    int nearestPost(int x, int y, float radius) const {
        float best = radius == 0 ? FLT_MAX : radius * radius;
        int found = -1;
        for (std::size_t i = 0; i < posts_.size(); ++i) {
            const float d = distance2(posts_[i].x, posts_[i].y, x, y);
            if (d <= best) {
                best = d;
                found = int(i);
            }
        }
        return found;
    }

    // 0x205110: a stroke starts at a post with at least one allowed edge.
    void press(int x, int y) {
        const int p = nearestPost(x, y, snap_);
        if (p < 0) {
            return;
        }
        bool any = false;
        for (const int e : posts_[std::size_t(p)].edges) {
            any = any || (e >= 0 && (edges_[std::size_t(e)].flags & kAllowed));
        }
        if (any) {
            current_ = p;
            stroking_ = true;
        }
    }

    // 0x292ad0: towards the neighbour nearest the pen, once the pen is
    // nearer to it than to the stroke's post.
    void slide(int x, int y) {
        if (!stroking_ || current_ < 0) {
            return;
        }
        float best = FLT_MAX;
        int target = -1, through = -1;
        for (const int e : posts_[std::size_t(current_)].edges) {
            if (e < 0) {
                continue;
            }
            const Edge& edge = edges_[std::size_t(e)];
            const int other = edge.a == current_ ? edge.b : edge.a;
            const float d =
                distance2(x, y, posts_[std::size_t(other)].x, posts_[std::size_t(other)].y);
            if (d <= best) {
                best = d;
                target = other;
                through = e;
            }
        }
        if (target < 0 || !(edges_[std::size_t(through)].flags & kAllowed)) {
            return;
        }
        const Post& at = posts_[std::size_t(current_)];
        if (distance2(x, y, at.x, at.y) <= best) {
            return;
        }
        Edge& edge = edges_[std::size_t(through)];
        if (!(edge.flags & kFixed)) {
            edge.flags ^= kDrawn;
        }
        // Over fixed rope the count still goes up (it reads the drawn bit).
        drawnCount_ += (edge.flags & kDrawn) ? 1 : -1;
        step_ = {through, bool(edge.flags & kDrawn)};
        current_ = target;
    }

    int across_ = 0, down_ = 0;
    float snap_ = 0;          // +0x2c
    std::vector<Post> posts_; // +4
    std::vector<Edge> edges_; // +0x10
    int current_ = -1;        // +0x30: the stroke's post
    bool stroking_ = false;   // +0x38
    int drawnCount_ = 0;      // +0x5c
    Step step_;               // +0x7c / +0x80 / +0x88
};

// The rope sprites' names (table 0x3d9d28, filled at start-up 0x38d60c).
constexpr const char* kRopeObject = "line_obj";
constexpr const char* kPostObject = "ple2";
constexpr const char* kRopeCell[3] = {"line_00", "line_01",
                                      "line_02"}; // drawn, partial loop, fixed
constexpr const char* kPostCell = "p00_02";

class RopePuzzle final : public SpecialPuzzle {
public:
    explicit RopePuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    void input() override;
    std::string describe() const override;

private:
    struct Cell {
        int value = 0;          // the script's: 1 a jewel, -1 a thief
        std::uint8_t walls = 0; // the judge's: rope along its top 1, bottom 2, left 4, right 8
    };

    Cell* cell(int x, int y) {
        if (x < 0 || y < 0 || x >= board_.across() || y >= board_.down()) {
            return nullptr;
        }
        const std::size_t i = std::size_t(y * board_.across() + x);
        return i < cells_.size() ? &cells_[i] : nullptr;
    }

    void setFlag(const char* name, bool on) {
        if (const int id = game_.flagId(name); id >= 0) {
            game_.setFlag(id, on);
        }
    }

    bool markLine(int x1, int y1, int x2, int y2, bool fixed);
    void refresh();
    void redraw();
    int judge();

    Game& game_;
    RopeBoard board_;
    std::vector<Cell> cells_;     // +0x2d4
    int drawSound_ = 0;           // +0x1a0
    int eraseSound_ = 0;          // +0x1a4
    std::uint32_t stepBlock_ = 0; // +0x2e0
    bool active_ = false;         // +0x1b4: set by fixing rope or a reset
};

// Commands 2 and 3 (0x29cae4 / 0x29cc84): the edges along a straight line
// between two posts become allowed, or (3) drawn fixed rope.
bool RopePuzzle::markLine(int x1, int y1, int x2, int y2, bool fixed) {
    const bool inside = std::min({x1, y1, x2, y2}) >= 0 && std::max(x1, x2) <= board_.across() &&
                        std::max(y1, y2) <= board_.down();
    if (!inside || (x1 != x2 && y1 != y2) || (x1 == x2 && y1 == y2)) {
        return false;
    }
    const int dx = x2 > x1 ? 1 : x2 < x1 ? -1 : 0, dy = y2 > y1 ? 1 : y2 < y1 ? -1 : 0;
    for (int x = x1, y = y1; x != x2 || y != y2; x += dx, y += dy) {
        const int e = board_.edgeBetween(x, y, x + dx, y + dy);
        if (fixed) {
            board_.setFlag(e, RopeBoard::kDrawn, true);
            board_.setFlag(e, RopeBoard::kFixed, true);
        } else {
            board_.setFlag(e, RopeBoard::kAllowed, true);
        }
    }
    return true;
}

// 0x230f48: after a step, its sound (none over fixed rope), the script's
// step block and the loop pass; then the rope's sprites.
void RopePuzzle::refresh() {
    const RopeBoard::Step& step = board_.step();
    if (step.edge >= 0) {
        const auto& edge = board_.edges()[std::size_t(step.edge)];
        const int sound = step.drawn ? drawSound_ : eraseSound_;
        if (sound != 0 && !(edge.flags & RopeBoard::kFixed) && !game_.effectsSounding(sound)) {
            game_.playEffect(sound);
        }
        if (stepBlock_ != 0 && active_) {
            for (const xq::Program* p = game_.newestScript(); p; p = p->next) {
                if (const xq::Program::Block* b = p->block(stepBlock_)) {
                    if (xq::Thread* t = game_.loopThreads[0]) {
                        const Value args[] = {Value::ofInt(step.drawn ? 1 : 0)};
                        t->callback(b->name, args);
                    }
                    break;
                }
            }
        }
        board_.classifyLoops();
    }
    redraw();
}

// The rope as sprites: each drawn edge at its first post, turned a quarter
// for vertical edges (fixed rope, a partial loop, other rope), and the
// player's rope ends marked on "ple2".
void RopePuzzle::redraw() {
    auto cellIndex = [](const Object& o, const char* name) {
        const std::uint32_t h = xq::hash(name);
        for (std::size_t i = 0; o.animation && i < o.animation->cells.size(); ++i) {
            if (o.animation->cells[i].hash == h) {
                return int(i);
            }
        }
        return -1;
    };
    const auto& posts = board_.posts();
    if (Object* o = game_.scene.object(xq::hash(kRopeObject))) {
        o->pieces.clear();
        for (const auto& e : board_.edges()) {
            if (!(e.flags & RopeBoard::kDrawn)) {
                continue;
            }
            const int which = (e.flags & RopeBoard::kFixed) ? 2 : e.loop == 1 ? 1 : 0;
            const auto& a = posts[std::size_t(e.a)];
            const auto& b = posts[std::size_t(e.b)];
            Object::Piece piece{cellIndex(*o, kRopeCell[which]), float(a.x), float(a.y)};
            piece.angle = a.x == b.x ? 1.5707964f : 0.0f;
            if (piece.cell >= 0) {
                o->pieces.push_back(piece);
            }
        }
    }
    if (Object* o = game_.scene.object(xq::hash(kPostObject))) {
        o->pieces.clear();
        const int marker = cellIndex(*o, kPostCell);
        for (const auto& e : board_.edges()) {
            if ((e.flags & RopeBoard::kDrawn) && !(e.flags & RopeBoard::kFixed) && marker >= 0) {
                for (const int p : {e.a, e.b}) {
                    o->pieces.push_back(
                        {marker, float(posts[std::size_t(p)].x), float(posts[std::size_t(p)].y)});
                }
            }
        }
    }
}

// 0x29c4fc: no input while the puzzle's input is off; the stroke; the judge
// button off while the player has drawn nothing; the update.
void RopePuzzle::input() {
    if (game_.objectTouchLocked) {
        return;
    }
    if (const int id = game_.flagId(names::inputOff); id >= 0 && game_.flag(id)) {
        return;
    }
    board_.input(game_.pressed(), game_.input.touching, game_.released(), game_.input.x,
                 game_.input.y);
    if (const int id = game_.flagId(names::judgeOff); id >= 0) {
        game_.setFlag(id, board_.drawnCount() == 0);
    }
    refresh();
}

// 0x29d470: the rope must make one loop through every post from the first
// (the start with exactly two rope ends); walking it marks the squares'
// sides along the rope. Then the first thief, by columns, must be shut in
// with no jewel: a walk along the walls from its square (0x29c8b0) fails on
// a jewel's square or off the board. Every post on the loop leaves each
// region a tree of squares, so the walk sees the same squares a fill would.
int RopePuzzle::judge() {
    if (board_.posts().size() > 64) {
        return 0;
    }
    for (Cell& c : cells_) {
        c.walls = 0;
    }
    std::vector<bool> visited(board_.posts().size(), false);
    std::vector<int> walked;
    if (board_.walkFrom(0, visited, &walked, false).empty()) {
        return 0;
    }
    for (const bool v : visited) {
        if (!v) {
            return 0;
        }
    }
    // 0x29cfe8: a horizontal edge walls the square below (top) and above
    // (bottom); a vertical one the square to its right (left) and left
    // (right).
    const auto& posts = board_.posts();
    const int across = board_.across();
    for (const int e : walked) {
        const auto& edge = board_.edges()[std::size_t(e)];
        const int a = edge.a;
        const int x = a % (across + 1), y = a / (across + 1);
        if (posts[std::size_t(edge.a)].y == posts[std::size_t(edge.b)].y) {
            if (Cell* c = cell(x, y)) {
                c->walls |= 1;
            }
            if (Cell* c = cell(x, y - 1)) {
                c->walls |= 2;
            }
        } else {
            if (Cell* c = cell(x, y)) {
                c->walls |= 4;
            }
            if (Cell* c = cell(x - 1, y)) {
                c->walls |= 8;
            }
        }
    }
    for (int x = 0; x < board_.across(); ++x) {
        for (int y = 0; y < board_.down(); ++y) {
            if (cell(x, y)->value != -1) {
                continue;
            }
            std::vector<bool> reached(cells_.size(), false);
            std::vector<std::pair<int, int>> open{{x, y}};
            reached[std::size_t(y * across + x)] = true;
            while (!open.empty()) {
                const auto [cx, cy] = open.back();
                open.pop_back();
                const Cell* c = cell(cx, cy);
                if (c->value == 1) {
                    return 0;
                }
                const int steps[4][3] = {{0, -1, 1}, {0, 1, 2}, {-1, 0, 4}, {1, 0, 8}};
                for (const auto& s : steps) {
                    if (c->walls & s[2]) {
                        continue;
                    }
                    const int nx = cx + s[0], ny = cy + s[1];
                    if (!cell(nx, ny)) {
                        return 0; // off the board
                    }
                    if (!reached[std::size_t(ny * across + nx)]) {
                        reached[std::size_t(ny * across + nx)] = true;
                        open.push_back({nx, ny});
                    }
                }
            }
            return 1;
        }
    }
    return 0;
}

bool RopePuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return int(std::int16_t(c.integer(first + i)));
    };
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    switch (number) {
    case 1:
        // 0x29c6cc: the rope's colour (RGB555 at +0x1b0); the sprites do
        // not use it.
        return count >= 3 ? result(1) : false;
    case 2:
    case 3: {
        if (count < 4 || !markLine(integer(0), integer(1), integer(2), integer(3), number == 3)) {
            return false;
        }
        if (number == 3) {
            active_ = true;
            refresh();
        }
        return result(1);
    }
    case 14: {
        // 0x29d824: the squares' values, empty (across x down by default).
        int n = board_.across() * board_.down();
        if (count == 1) {
            n = integer(0);
        } else if (count >= 2) {
            n = integer(0) * integer(1);
        }
        cells_.assign(std::size_t(std::max(0, n)), Cell{});
        return result(1);
    }
    case 15: {
        // A row of values: y, then one per square across.
        if (count == 0 || int(count) - 1 != board_.across()) {
            return false;
        }
        for (int x = 0; x < board_.across(); ++x) {
            if (Cell* s = cell(x, integer(0))) {
                s->value = c.integer(first + 1 + std::size_t(x));
            }
        }
        return result(1);
    }
    case 16:
        if (count < 3) {
            return false;
        }
        if (Cell* s = cell(integer(0), integer(1))) {
            s->value = c.integer(first + 2);
        }
        return result(1);
    case 17: {
        if (count < 2) {
            return false;
        }
        const Cell* s = cell(integer(0), integer(1));
        return result(s ? s->value : 0);
    }
    case 18: {
        // 0x29d16c: first post x, y; squares across, down; post spacing; a
        // float (+0x19c); snap radius; draw and erase sounds. The judge
        // button starts off.
        if (count < 8) {
            return false;
        }
        board_.setup(integer(0), integer(1), integer(2), integer(3), float(c.integer(first + 4)),
                     float(c.integer(first + 6)));
        drawSound_ = c.integer(first + 7);
        eraseSound_ = count > 8 ? c.integer(first + 8) : 0;
        setFlag(names::judgeOff, true);
        return result(1);
    }
    case 19:
        refresh();
        return result(1);
    case 20: {
        // The block run at each step (a name or its hash).
        if (count == 0) {
            return false;
        }
        const Value& v = c.at(first);
        stepBlock_ = v.isText() ? xq::hash(v.text) : v.toUint();
        return result(1);
    }
    case 21:
        // The player's rope gone (fixed rope stays), the judge button off.
        setFlag(names::judgeOff, true);
        board_.reset(false);
        active_ = true;
        refresh();
        return result(1);
    case 22:
        return result(judge());
    default:
        return false;
    }
}

std::string RopePuzzle::describe() const {
    std::string out;
    char line[96];
    std::snprintf(line, sizeof line, "rope %d %d drawn %d\n", board_.across(), board_.down(),
                  board_.drawnCount());
    out += line;
    const auto& posts = board_.posts();
    for (const auto& e : board_.edges()) {
        if (e.flags & (RopeBoard::kDrawn | RopeBoard::kAllowed)) {
            std::snprintf(line, sizeof line, "edge %d,%d %d,%d flags %d loop %d\n",
                          posts[std::size_t(e.a)].x, posts[std::size_t(e.a)].y,
                          posts[std::size_t(e.b)].x, posts[std::size_t(e.b)].y, e.flags, e.loop);
            out += line;
        }
    }
    for (int y = 0; y < board_.down(); ++y) {
        out += "cells";
        for (int x = 0; x < board_.across(); ++x) {
            const std::size_t i = std::size_t(y * board_.across() + x);
            out += ' ' + std::to_string(i < cells_.size() ? cells_[i].value : 0);
        }
        out += '\n';
    }
    return out;
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeRopePuzzle(Game& game) {
    return std::make_unique<RopePuzzle>(game);
}

} // namespace picarat::adventure5
