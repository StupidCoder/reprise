// The sixth adventure's bridge labyrinth (loop 3330, class
// "BridgeLabyrinth"; catalogue type 28: 086, 090, "lead Luke over all
// switches"): the player walks a graph of nodes in the dark with the arrow
// buttons. A step runs from a stop node one way to the next stop node;
// the bridge cubes behind the player fall, so no node can be walked twice.
// The judge wants the player on the goal with every switch walked.
//
// Reconstructed from the class's command method 0x339ea4 (vtable 0x3ca2bc;
// its input and update are the base loop's) and its helpers: the node list
// 0x33aafc (command 10), nodes 0x33b42c (11), links 0x33b5a8 (12), node
// kinds 0x33c210 (14), the bridge cubes 0x33b1a8 (15), the step 0x33b6c0
// (30) with the falling cubes 0x33ae40 / 0x33b31c, the undo 0x33a8b8 (22)
// and the grid helpers 0x213480 (cell -> world: origin + size x cell),
// 0x2132d0 (the way between two cells), 0x2131e8 (a cell n steps on),
// 0x213354 / 0x2134ac / 0x213568 (finding nodes). The script walks the
// player along the step's waypoints (31) and plays the switches and the
// falling cubes per cell (33, 46).
//
// Ways: 0 -x, 1 -z, 2 +x, 3 +z. Not reconstructed: the player's shadow
// mesh (commands 50 / 51, 0x339580 / 0x33c33c / 0x339b38: a six-vertex
// mesh on "PlayersShadow" bent over steps); they are taken without
// drawing it.
#include "puzzles/a5_special.hpp"
#include <array>
#include <cmath>
#include <cstdio>

namespace picarat::adventure5 {
namespace {

using xq::Call;
using xq::Value;

class BridgePuzzle final : public SpecialPuzzle {
public:
    BridgePuzzle() = default;

    bool command(int number, Call& c, std::size_t first) override;
    std::string describe() const override;

private:
    // Node flags (+0).
    enum : std::uint16_t {
        kStepCube = 0x04, // a bridge cube between nodes (command 15)
        kNode = 0x02,     // a node of the graph
        kSolid = 0x08,    // does not fall
        kFalls = 0x10,    // falls once walked off
        kStop = 0x20,     // a step ends here
        kPass = 0x40,     // a step runs through
        kSwitch = 0x80,
        kGoal = 0x100,
    };

    // Node state (+0x14).
    enum : std::uint8_t { kWalked = 1, kFalling = 8 };

    struct Vec {
        float x = 0, y = 0, z = 0;
        bool operator==(const Vec&) const = default;
    };

    struct Node { // 0x30 bytes in the list at +0x198
        std::uint16_t flags = 0;
        std::uint32_t name = 0;                  // +4: the name's hash
        Vec at;                                  // +8: the cell (x, height, z)
        std::array<int, 4> link{-1, -1, -1, -1}; // +0x1c, by way
        std::uint8_t state = 0;                  // +0x14 (bits 1-2: its turn)
        std::array<bool, 4> cubed{};             // +0x15: cubes made towards the link
        std::uint8_t kind = 0;                   // +0x2c: low 3 bits its kind, bits 3-4 more
    };

    struct Waypoint { // +0x1b0, ten at most
        Vec at;
        int frames = 0;     // +0xc
        bool stair = false; // +0xe
    };

    struct Cell { // per cell walked (+0x250 / +0x251.., 17 bytes, eleven at most)
        int switchIndex = -1;
        std::vector<int> falls; // indexes among the falling nodes, fifteen at most
    };

    struct Record { // the history (+0x1a4, 30 at most, newest first)
        int way = -1;
        float turn = 0;
        std::vector<int> falls;    // fifteen at most
        std::vector<int> switches; // fifteen at most
    };

    Vec world(const Vec& cell) const {
        return {origin_.x + size_ * cell.x, origin_.y + size_ * cell.y, origin_.z + size_ * cell.z};
    }

    static Vec stepped(Vec v, int way, int n) {
        static constexpr std::array<std::array<float, 3>, 4> kUnit{
            {{-1, 0, 0}, {0, 0, -1}, {1, 0, 0}, {0, 0, 1}}};
        v.x += kUnit[std::size_t(way)][0] * float(n);
        v.z += kUnit[std::size_t(way)][2] * float(n);
        return v;
    }

    // 0x2132d0: the way from one cell to another in line with it.
    static int wayBetween(const Vec& a, const Vec& b) {
        const int dx = int(b.x - a.x), dz = int(b.z - a.z);
        if (dx > 0 && dz == 0) {
            return 2;
        }
        if (dx < 0 && dz == 0) {
            return 0;
        }
        if (dx == 0 && dz < 0) {
            return 1;
        }
        if (dx == 0 && dz > 0) {
            return 3;
        }
        return -1;
    }

    int byName(std::uint32_t name) const { // 0x213568
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if ((nodes_[i].flags & kNode) && nodes_[i].name == name) {
                return int(i);
            }
        }
        return -1;
    }

    // 0x213354: a node of the flags on the cell (x, z, truncated).
    int at(const Vec& cell, std::uint16_t mask) const {
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            const Node& n = nodes_[i];
            if ((n.flags & mask) && std::int16_t(int(n.at.x)) == std::int16_t(int(cell.x)) &&
                std::int16_t(int(n.at.z)) == std::int16_t(int(cell.z))) {
                return int(i);
            }
        }
        return -1;
    }

    // The k-th node of the flags (-1 for none) and a node's place among
    // them (0x2134ac by position).
    int nth(std::uint16_t mask, int k) const {
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if ((nodes_[i].flags & mask) && k-- == 0) {
                return int(i);
            }
        }
        return -1;
    }

    int indexAmong(std::uint16_t mask, int node) const {
        int k = 0;
        for (int i = 0; i < int(nodes_.size()); ++i) {
            if (nodes_[std::size_t(i)].flags & mask) {
                if (nodes_[std::size_t(i)].at == nodes_[std::size_t(node)].at) {
                    return k;
                }
                ++k;
            }
        }
        return -1;
    }

    // The stop node a step the way reaches (none when a link is missing).
    int stepEnd(int from, int way) const {
        if (from < 0 || way < 0 || way > 3 || !(nodes_[std::size_t(from)].flags & kNode)) {
            return -1;
        }
        for (int n = nodes_[std::size_t(from)].link[std::size_t(way)]; n >= 0;
             n = nodes_[std::size_t(n)].link[std::size_t(way)]) {
            if (nodes_[std::size_t(n)].flags & kStop) {
                return n;
            }
        }
        return -1;
    }

    // Whether the step's nodes after the start are all unwalked.
    bool open(int from, int to, int way) const {
        for (int n = from; n != to;) {
            n = nodes_[std::size_t(n)].link[std::size_t(way)];
            if (n < 0 || (nodes_[std::size_t(n)].state & kWalked)) {
                return false;
            }
        }
        return true;
    }

    void makeCubes(int node);
    void dropBehind(const Vec& left, const Vec& now, Cell& cell);
    void gatherFall(const Vec& from, std::vector<int>& falls, bool& unsupported);
    void step(int way, float turn, int framesPerCell);
    void undo(Call& c, std::size_t first, std::size_t count);

    std::vector<Node> nodes_;
    std::size_t capacity_ = 0;
    Vec origin_;                      // +0x318
    float size_ = 1;                  // +0x324
    int start_ = -1;                  // +0x310
    int goal_ = -1;                   // +0x314
    int at_ = -1;                     // +0x32c
    std::vector<Waypoint> waypoints_; // +0x1b0 (count +0x30b)
    std::vector<Cell> cells_;         // +0x250 (count +0x30c)
    std::vector<Record> history_;     // +0x1a4, newest first
};

// 0x33b1a8 (command 15): from the start, along every link not done yet,
// a bridge cube on each cell between the two nodes.
void BridgePuzzle::makeCubes(int node) {
    Node& a = nodes_[std::size_t(node)];
    if (!(a.flags & kNode)) {
        return;
    }
    a.state |= kFalling;
    for (int way = 0; way < 4; ++way) {
        const int other = nodes_[std::size_t(node)].link[std::size_t(way)];
        if (other < 0 || nodes_[std::size_t(node)].cubed[std::size_t(way)]) {
            continue;
        }
        const Vec from = nodes_[std::size_t(node)].at, to = nodes_[std::size_t(other)].at;
        const int between =
            int(std::fabs(to.x - from.x) + std::fabs(to.z - from.z)) - 1; // cells strictly between
        for (int k = 1; k <= between && nodes_.size() < capacity_; ++k) {
            Node cube;
            cube.flags = kPass | kFalls | kStepCube;
            cube.at = stepped(from, way, k);
            nodes_.push_back(cube);
        }
        nodes_[std::size_t(node)].cubed[std::size_t(way)] = true;
        nodes_[std::size_t(other)].cubed[std::size_t((way + 2) % 4)] = true;
        makeCubes(other);
    }
}

// 0x33b31c: the unwalked, not yet falling cubes joined to a cell, unless a
// solid node holds them.
void BridgePuzzle::gatherFall(const Vec& from, std::vector<int>& falls, bool& unsupported) {
    for (int way = 0; way < 4; ++way) {
        const int n = at(stepped(from, way, 1), 0xfe);
        if (n < 0) {
            continue;
        }
        Node& k = nodes_[std::size_t(n)];
        if (k.flags & kSolid) {
            unsupported = false;
            continue;
        }
        if ((k.state & kFalling) || ((k.flags & kNode) && (k.kind & 7) == 4) ||
            (k.state & kWalked)) {
            continue;
        }
        if (falls.size() < 15) {
            falls.push_back(indexAmong(kFalls, n));
        }
        k.state |= kFalling;
        gatherFall(k.at, falls, unsupported);
    }
}

// 0x33ae40: the player steps from `left` to `now`; the falling node on the
// cell left falls (it counts as walked), and so does each group of cubes
// beside it that nothing solid holds any more.
void BridgePuzzle::dropBehind(const Vec& left, const Vec& now, Cell& cell) {
    const int n = at(left, kFalls);
    if (n < 0) {
        return;
    }
    if (cell.falls.size() < 15) {
        cell.falls.push_back(indexAmong(kFalls, n));
    }
    nodes_[std::size_t(n)].state |= kWalked | kFalling;
    const int ahead = wayBetween(left, now);
    for (Node& k : nodes_) {
        k.state &= std::uint8_t(~kFalling);
        k.cubed = {};
    }
    for (int way = 0; way < 4; ++way) {
        if (way == ahead) {
            continue;
        }
        const int m = at(stepped(left, way, 1), 0xfe);
        if (m < 0) {
            continue;
        }
        Node& k = nodes_[std::size_t(m)];
        if (k.flags & kSolid) {
            continue;
        }
        if ((k.state & kFalling) || ((k.flags & kNode) && (k.kind & 7) == 4) ||
            (k.state & kWalked)) {
            continue;
        }
        std::vector<int> group{indexAmong(kFalls, m)};
        k.state |= kFalling;
        bool unsupported = true;
        gatherFall(k.at, group, unsupported);
        if (unsupported) {
            for (int g : group) {
                if (cell.falls.size() < 15) {
                    cell.falls.push_back(g);
                }
            }
        }
    }
}

// 0x33b6c0 (command 30): a step the way, with its waypoints (stairs, kind
// 1, by their middles; lowered nodes, kind 2, drawn one lower) and its cells.
void BridgePuzzle::step(int way, float turn, int framesPerCell) {
    const int to = stepEnd(at_, way);
    if (at_ < 0 || to < 0 || !open(at_, to, way)) {
        return;
    }
    waypoints_.clear();
    cells_.clear();
    auto push = [this](Vec v, bool stair) {
        if (waypoints_.size() < 10) {
            waypoints_.push_back({v, 0, stair});
        }
    };
    auto shown = [](const Node& n) {
        Vec v = n.at;
        if ((n.kind & 7) == 2) {
            v.y -= 1;
        }
        return v;
    };
    int a = at_;
    while (a != to) {
        const int b = nodes_[std::size_t(a)].link[std::size_t(way)];
        const Node& na = nodes_[std::size_t(a)];
        const Node& nb = nodes_[std::size_t(b)];
        if ((nb.kind & 7) == 1) {
            const Vec wa = world(shown(na)), wb = world(shown(nb));
            const Vec half{(wb.x - wa.x) * 0.5f, 0, (wb.z - wa.z) * 0.5f};
            push({wa.x + half.x, wa.y, wa.z + half.z}, true);
            Vec up{wb.x + half.x, wb.y, wb.z + half.z};
            if (shown(na).y == nb.at.y) {
                up.y -= size_;
            }
            push(up, true);
        } else if ((nb.kind & 7) != 2) {
            const Vec wb = world(nb.at);
            if (!waypoints_.empty() && !waypoints_.back().stair) {
                waypoints_.back().at = wb; // a straight run: one waypoint
            } else {
                push(wb, false);
            }
        }
        // The cells walked, each with the cubes that fall behind and the
        // switch on it.
        Vec cell = na.at;
        const Vec end = nb.at;
        while (!(cell.x == end.x && cell.z == end.z)) {
            const Vec next = stepped(cell, way, 1);
            Cell walked;
            dropBehind(cell, next, walked);
            if (const int s = at(next, kSwitch); s >= 0) {
                walked.switchIndex = indexAmong(kSwitch, s);
            }
            if (cells_.size() < 11) {
                cells_.push_back(walked);
            }
            cell = next;
        }
        a = b;
        nodes_[std::size_t(a)].state |= kWalked;
    }
    // Frames per waypoint: its share of cells x the frames per cell.
    Vec last = world(nodes_[std::size_t(at_)].at);
    for (Waypoint& w : waypoints_) {
        const float dx = w.at.x - last.x, dy = w.at.y - last.y, dz = w.at.z - last.z;
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > 0 && size_ > 0) {
            w.frames = int(d / size_ * float(framesPerCell));
            last = w.at;
        }
    }
    at_ = to;
    Record r;
    r.way = way;
    r.turn = turn;
    for (const Cell& c : cells_) {
        for (int f : c.falls) {
            if (r.falls.size() < 15) {
                r.falls.push_back(f);
            }
        }
        if (c.switchIndex >= 0 && r.switches.size() < 15) {
            r.switches.push_back(c.switchIndex);
        }
    }
    if (history_.size() >= 30) {
        history_.pop_back();
    }
    history_.insert(history_.begin(), std::move(r));
}

// 0x33a8b8 (command 22): the newest step undone -> the turn it kept.
void BridgePuzzle::undo(Call& c, std::size_t first, std::size_t count) {
    c.result = Value::ofInt(0);
    if (count == 0 || history_.empty() || at_ < 0) {
        return;
    }
    const Record r = history_.front();
    const int back = (r.way + 2) % 4;
    int n = at_;
    int found = -1;
    for (int k = nodes_[std::size_t(n)].link[std::size_t(back)]; k >= 0;
         k = nodes_[std::size_t(k)].link[std::size_t(back)]) {
        if (nodes_[std::size_t(k)].flags & kStop) {
            found = k;
            break;
        }
    }
    if (found < 0) {
        return;
    }
    for (int k = at_; k != found; k = nodes_[std::size_t(k)].link[std::size_t(back)]) {
        nodes_[std::size_t(k)].state &= std::uint8_t(~kWalked);
    }
    at_ = found;
    if (Value* reg = c.reg(first)) {
        *reg = Value::ofFloat(r.turn);
    }
    for (int f : r.falls) {
        if (const int k = nth(kFalls, f); k >= 0) {
            nodes_[std::size_t(k)].state &= std::uint8_t(~kWalked);
        }
    }
    history_.erase(history_.begin());
    c.result = Value::ofInt(1);
}

bool BridgePuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return c.integer(first + i);
    };
    auto hashOf = [&](std::size_t i) {
        const Value& v = c.at(first + i);
        return v.isText() ? xq::hash(v.text) : v.toUint();
    };
    auto store = [&](std::size_t i, Value v) {
        if (i < count) {
            if (Value* r = c.reg(first + i)) {
                *r = std::move(v);
            }
        }
    };
    auto result = [&](bool b) {
        c.result = Value::ofInt(b ? 1 : 0);
        return true;
    };
    switch (number) {
    case 10: // the node room, origin x, y, z, cell size
        if (count < 5) {
            return false;
        }
        nodes_.clear();
        history_.clear();
        capacity_ = std::size_t(std::max(0, integer(0)));
        origin_ = {c.real(first + 1), c.real(first + 2), c.real(first + 3)};
        size_ = c.real(first + 4);
        start_ = goal_ = at_ = -1;
        return result(true);
    case 11: { // a node: name, x, height, z [, stops [, switch]]
        if (count < 4) {
            return false;
        }
        const std::uint32_t name = hashOf(0);
        if (name != 0 && byName(name) < 0 && nodes_.size() < capacity_) {
            Node n;
            n.flags = kNode;
            n.name = name;
            n.at = {float(integer(1)), float(integer(2)), float(integer(3))};
            if (count > 4) {
                n.flags |= c.at(first + 4).toInt() != 0 ? kStop : kPass;
            }
            if (count > 5 && c.at(first + 5).toInt() != 0) {
                n.flags |= kSwitch;
            }
            nodes_.push_back(n);
        }
        return result(true);
    }
    case 12: { // a link between two nodes in line
        if (count < 2) {
            return false;
        }
        const int a = byName(hashOf(0)), b = byName(hashOf(1));
        if (a >= 0 && b >= 0 && a != b) {
            const int way = wayBetween(nodes_[std::size_t(a)].at, nodes_[std::size_t(b)].at);
            const int back = (way + 2) % 4;
            if (way >= 0 && nodes_[std::size_t(a)].link[std::size_t(way)] < 0 &&
                nodes_[std::size_t(b)].link[std::size_t(back)] < 0) {
                nodes_[std::size_t(a)].link[std::size_t(way)] = b;
                nodes_[std::size_t(b)].link[std::size_t(back)] = a;
            }
        }
        return result(true);
    }
    case 13: { // the start and the goal
        if (count < 2) {
            return false;
        }
        const int a = byName(hashOf(0)), b = byName(hashOf(1));
        if (a >= 0 && b >= 0 && a != b && start_ < 0) {
            start_ = a;
            goal_ = b;
            nodes_[std::size_t(b)].flags |= kGoal;
        }
        return result(true);
    }
    case 14: { // a node's kind: name, solid, kind, turn [, more]
        if (count < 4) {
            return false;
        }
        const int n = byName(hashOf(0));
        if (n < 0) {
            return result(true);
        }
        Node& k = nodes_[std::size_t(n)];
        k.flags |= c.at(first + 1).toInt() != 0 ? kSolid : kFalls;
        k.kind = std::uint8_t((k.kind & 0xf8) | (c.at(first + 2).toUint() & 7));
        k.state = std::uint8_t((k.state & 0xf9) | ((c.at(first + 3).toUint() & 3) << 1));
        if (count > 4) {
            k.kind = std::uint8_t((k.kind & 0xe7) | ((c.at(first + 4).toUint() & 3) << 3));
        }
        return result(true);
    }
    case 15: // the bridge cubes, from the start
        if (start_ < 0) {
            return false;
        }
        for (Node& k : nodes_) {
            k.state &= std::uint8_t(~kFalling);
            k.cubed = {};
        }
        makeCubes(start_);
        return result(true);
    case 20: // back to the start
        history_.clear();
        at_ = start_;
        for (Node& k : nodes_) {
            k.state &= std::uint8_t(~kWalked);
        }
        return result(true);
    case 21: { // the judge: on the goal with every switch walked
        bool solved = at_ >= 0 && at_ == goal_;
        for (const Node& k : nodes_) {
            if ((k.flags & kSwitch) && !(k.state & kWalked)) {
                solved = false;
            }
        }
        return result(solved);
    }
    case 22:
        undo(c, first, count);
        return true;
    case 23: { // the newest step's falls (0) or switches (1) -> count
        if (count < 2) {
            return false;
        }
        if (history_.empty()) {
            return result(true);
        }
        const Record& r = history_.front();
        store(1, Value::ofInt(int(integer(0) == 1 ? r.switches.size() : r.falls.size())));
        return result(true);
    }
    case 24: { // ... and its k-th entry
        if (count < 3) {
            return false;
        }
        if (history_.empty()) {
            return false;
        }
        const Record& r = history_.front();
        const auto& list = integer(0) == 1 ? r.switches : r.falls;
        const int k = integer(1);
        if (k < 0 || k >= int(list.size())) {
            return false;
        }
        store(2, Value::ofInt(list[std::size_t(k)]));
        return result(true);
    }
    case 30: { // a step: dx, dz, the player's turn, frames per cell
        if (count < 4) {
            return false;
        }
        const int dx = integer(0), dz = integer(1);
        const int way = dz == 0 && dx > 0   ? 2
                        : dz == 0 && dx < 0 ? 0
                        : dx == 0 && dz < 0 ? 1
                        : dx == 0 && dz > 0 ? 3
                                            : -1;
        if (way < 0) {
            return false;
        }
        step(way, c.real(first + 2), c.integer(first + 3));
        return result(true);
    }
    case 31: { // waypoint k -> x, y, z, frames
        if (count < 5) {
            return false;
        }
        const int k = integer(0);
        if (k < 0 || k >= int(waypoints_.size())) {
            return result(false);
        }
        const Waypoint& w = waypoints_[std::size_t(k)];
        store(1, Value::ofFloat(w.at.x));
        store(2, Value::ofFloat(w.at.y));
        store(3, Value::ofFloat(w.at.z));
        store(4, Value::ofFloat(float(w.frames)));
        return result(true);
    }
    case 32: { // whether a step the way can be taken
        if (count == 0) {
            return result(false);
        }
        const int way = integer(0);
        const int to = way >= 0 && way <= 3 ? stepEnd(at_, way) : -1;
        return result(to >= 0 && open(at_, to, way));
    }
    case 33: { // cell k walked -> its falls' count, its switch
        if (count < 3) {
            return false;
        }
        const int k = integer(0);
        if (k < 0 || k >= int(cells_.size())) {
            store(1, Value::ofInt(-1));
            store(2, Value::ofInt(-1));
            return result(true);
        }
        store(1, Value::ofInt(int(cells_[std::size_t(k)].falls.size())));
        store(2, Value::ofInt(cells_[std::size_t(k)].switchIndex));
        return result(true);
    }
    case 40: { // the nodes of the flags -> count
        if (count < 2) {
            return false;
        }
        const std::uint32_t mask = c.at(first).toUint();
        int n = 0;
        for (const Node& k : nodes_) {
            n += (k.flags & mask) != 0;
        }
        store(1, Value::ofInt(n));
        return result(true);
    }
    case 41: { // the k-th node of the flags -> turn [, kind [, more]]
        if (count < 2) {
            return false;
        }
        const int n = nth(std::uint16_t(c.at(first).toUint()), integer(1));
        if (n < 0) {
            return false;
        }
        const Node& k = nodes_[std::size_t(n)];
        store(2, Value::ofInt((k.state & 7) >> 1));
        if (k.flags & kNode) {
            store(3, Value::ofInt(k.kind & 7));
            store(4, Value::ofUint((k.kind & 0x1f) >> 3));
        }
        return result(true);
    }
    case 42: { // the k-th node of the flags -> world x, y, z
        if (count < 5) {
            return false;
        }
        const int n = nth(std::uint16_t(c.at(first).toUint()), integer(1));
        if (n < 0) {
            return false;
        }
        const Vec w = world(nodes_[std::size_t(n)].at);
        store(2, Value::ofFloat(w.x));
        store(3, Value::ofFloat(w.y));
        store(4, Value::ofFloat(w.z));
        return result(true);
    }
    case 43: { // the node of the flags on a cell -> its index
        if (count < 5) {
            return false;
        }
        const std::uint16_t mask = std::uint16_t(c.at(first).toUint());
        const Vec cell{float(integer(2)), float(integer(3)), float(integer(4))};
        int k = 0, found = -1;
        for (const Node& n : nodes_) {
            if (n.flags & mask) {
                if (n.at == cell) {
                    found = k;
                    break;
                }
                ++k;
            }
        }
        store(1, Value::ofInt(found));
        return true;
    }
    case 44: { // the player's node -> world x, y, z
        if (count < 3 || at_ < 0) {
            return false;
        }
        const Vec w = world(nodes_[std::size_t(at_)].at);
        store(0, Value::ofFloat(w.x));
        store(1, Value::ofFloat(w.y));
        store(2, Value::ofFloat(w.z));
        return result(true);
    }
    case 45: { // the player's node -> cell x, height, z
        if (count < 3 || at_ < 0) {
            return false;
        }
        const Vec v = nodes_[std::size_t(at_)].at;
        store(0, Value::ofInt(int(v.x)));
        store(1, Value::ofInt(int(v.y)));
        store(2, Value::ofInt(int(v.z)));
        return result(true);
    }
    case 46: { // cell k walked: its j-th falling cube
        if (count < 3) {
            return false;
        }
        const int k = integer(0), j = integer(1);
        if (k < 0 || k >= int(cells_.size())) {
            return false;
        }
        const auto& falls = cells_[std::size_t(k)].falls;
        if (j < 0 || j >= int(falls.size())) {
            return false;
        }
        store(2, Value::ofInt(falls[std::size_t(j)]));
        return result(true);
    }
    case 50:
    case 51: // the player's shadow (not drawn)
        return result(true);
    default:
        return false;
    }
}

std::string BridgePuzzle::describe() const {
    std::string out = "bridge at " + std::to_string(at_) + " start " + std::to_string(start_) +
                      " goal " + std::to_string(goal_) + "\n";
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const Node& n = nodes_[i];
        char line[160];
        std::snprintf(line, sizeof line,
                      "node %zu flags %x at %g %g %g state %x kind %x links %d %d %d %d\n", i,
                      n.flags, double(n.at.x), double(n.at.y), double(n.at.z), n.state, n.kind,
                      n.link[0], n.link[1], n.link[2], n.link[3]);
        out += line;
    }
    return out;
}

} // namespace

std::unique_ptr<SpecialPuzzle> makeBridgePuzzle(Game&) {
    return std::make_unique<BridgePuzzle>();
}

} // namespace picarat::adventure5
