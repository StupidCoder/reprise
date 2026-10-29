// The shop game (loop 6200, lt5CLoopMiniShop; creator 0x37b7d4, vtable
// 0x401094: start 0x1d4df8, commands 0x1d4e44, reset 0x1d6f88).
//
// A script loop (seq/mini_shop) with a shelf the script cannot hold itself:
// ten stages from res/sub/lt5_sub_shop.xs (read at the start, 0x1d6ffc), a
// grid of 15 x 7 cells of 19 pixels, the goods (up to ten per stage, each a
// 3 x 3 cell mask) dragged between a rack of four visible slots at the top
// of the lower screen and the shelf, and the customers' walk from one good
// to a neighbour that shares its colour or its kind. The commands (2110,
// vtable +0x4c) are below; the script draws, plays and judges the rest.
// Docs: .analysis/adventure56/docs/story.md ("The shop game").
#include "story/a5_shop.hpp"
#include "import/a56_formats.hpp"
#include "system/a5_game.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace picarat::adventure5 {

namespace {

using xq::Value;

constexpr int kCols = 15, kRows = 7, kCells = kCols * kRows;
constexpr int kCell = 19;          // pixels per cell
constexpr int kStages = 10;
constexpr int kGoods = 10;         // per stage
constexpr int kRackSlots = 10;     // the rack's list (+0x16c)
constexpr int kRackShown = 4;      // slots on the screen
// Static values the code reads (0x412ff8 +0x0c / +0x0e): the shelf's
// pixel origin before a stage's offset (set at boot; console memory: 17,
// 69).
constexpr std::int16_t kShelfX = 17, kShelfY = 69;
// 0x443fe4 (set at boot; console memory): the shade over places a good
// cannot go.
constexpr std::array<float, 4> kShade{0.3451f, 0.1882f, 0.0941f, 0.5f};

// A good of a stage (0x18 bytes; 8110).
struct Good {
    std::uint32_t colour = 0; // +0x00: a tag (8101) by name hash
    std::uint32_t kind = 0;   // +0x04
    std::string name;         // +0x08: also its object's name
    std::array<std::int8_t, 9> cells{}; // +0x0c: the 3 x 3 mask (bit 1: not a bottom)
    std::int8_t width = 0, height = 0;  // +0x15 / +0x16: pixels (cells x 19)
};

// A stage (400 bytes; 8100 .. 8120).
struct Stage {
    std::array<Good, kGoods> goods{};
    int count = 0;                                   // +0x18d
    std::vector<std::pair<std::string, std::uint32_t>> tags; // +0xf0 (8101)
    std::array<std::uint32_t, kGoods> order{};      // +0xf4: goods by their order number
    std::int16_t startX = 0, startY = 0;            // +0x11c: the first good's cell
    int offset = 0;                                  // +0x120: the shelf's x offset
    std::array<std::uint8_t, kCells> grid{};         // +0x124: 0xff wall, bit 1 a board

    int goodIndex(std::uint32_t nameHash) const {
        for (int i = 0; i < kGoods; ++i) {
            if (xq::hash(goods[std::size_t(i)].name) == nameHash) {
                return i;
            }
        }
        return -1;
    }
    // 0x2923fc: a tag's text by its hash ("" when unknown).
    std::string tagText(std::uint32_t hash) const {
        for (const auto& t : tags) {
            if (t.second == hash) {
                return t.first;
            }
        }
        return {};
    }
};

std::vector<Stage> loadStages(const Game& game) {
    std::vector<Stage> stages(kStages);
    const auto file = game.pack.find("res/sub/lt5_sub_shop", ".xs");
    if (file.empty()) {
        return stages;
    }
    const auto list = import::adventure56::parseXscr(game.pack.read(file));
    auto text = [&list](const import::adventure56::Operand& o) {
        if ((o.type & 15) != 8 || o.value >= list.strings.size()) {
            return std::string();
        }
        const auto* s = reinterpret_cast<const char*>(list.strings.data() + o.value);
        return std::string(s, strnlen(s, list.strings.size() - o.value));
    };
    auto integer = [](const import::adventure56::Operand& o) {
        if ((o.type & 15) == 3) {
            float f;
            std::memcpy(&f, &o.value, sizeof f);
            return int(f);
        }
        return std::int32_t(o.value);
    };
    std::size_t at = 0;
    for (const auto& c : list.commands) {
        if (at >= stages.size()) {
            break;
        }
        Stage& s = stages[at];
        const auto* op = &list.operands[c.first];
        switch (c.opcode) {
        case 8100: // 0x130dd0 {offset, x, y}
            if (c.count >= 3) {
                s.offset = integer(op[0]);
                s.startX = std::int16_t(integer(op[1]));
                s.startY = std::int16_t(integer(op[2]));
            }
            break;
        case 8101: // 0x14bd38: the tags (kept once per stage)
            if (s.tags.empty()) {
                for (unsigned i = 0; i < c.count; ++i) {
                    const std::string t = text(op[i]);
                    s.tags.emplace_back(t, xq::hash(t));
                }
            }
            break;
        case 8110: { // 0x122a28 {name, w, h, 9 cells, colour, kind, order}
            if (c.count < 14 || s.count >= kGoods) {
                break;
            }
            Good& g = s.goods[std::size_t(s.count)];
            g.name = text(op[0]);
            g.width = std::int8_t(integer(op[1]) * kCell);
            g.height = std::int8_t(integer(op[2]) * kCell);
            for (int i = 0; i < 9; ++i) {
                g.cells[std::size_t(i)] = std::int8_t(integer(op[3 + i]));
            }
            g.colour = xq::hash(text(op[12]));
            g.kind = xq::hash(text(op[13]));
            if (c.count > 14) {
                const int k = integer(op[14]);
                if (k >= 0 && k < kGoods) {
                    s.order[std::size_t(k)] = xq::hash(g.name);
                }
            }
            ++s.count;
            break;
        }
        case 8111: // 0x122b50: the grid's bytes
            for (unsigned i = 0; i < c.count && i < unsigned(kCells); ++i) {
                s.grid[i] = std::uint8_t(integer(op[i]));
            }
            break;
        case 8120: // 0x12b750: the next stage
            ++at;
            break;
        default:
            break;
        }
    }
    return stages;
}

// A cell next to a good (6 bytes, 0x2606c0's list): where, from which side
// (1 above, 2 below, 3 left, 4 right) and the good there (its number, 0
// none).
struct Neighbour {
    std::int16_t x = 0, y = 0;
    std::uint8_t side = 0;
    std::uint8_t good = 0;
};
using Neighbours = std::array<Neighbour, 12>;

struct Cell {
    std::int16_t x = 0, y = 0;
};

class ShopLoop final : public ScriptLoop {
public:
    ShopLoop(Story& story, int id, std::vector<Value> values)
        : ScriptLoop(story, id, std::move(values), "mini_shop") {}

    void start() override {
        // 0x1d4df8: the scope, the stages (0x1d6ffc), seq/mini_shop.
        Loop::start();
        stages_ = loadStages(game_);
        game_.loadScript("seq/mini_shop");
    }

    bool command(int number, xq::Call& c, std::size_t first) override;

private:
    std::size_t operands(const xq::Call& c, std::size_t first) const {
        return c.count() > first ? c.count() - first : 0;
    }
    void put(xq::Call& c, std::size_t i, Value v) {
        if (Value* r = c.reg(i)) {
            *r = std::move(v);
        }
    }
    void putInt(xq::Call& c, std::size_t i, int v) { put(c, i, Value::ofInt(v)); }
    std::uint32_t nameOf(const xq::Call& c, std::size_t i) const {
        const std::string* t = c.text(i);
        return xq::hash(t ? *t : std::string());
    }

    Stage* stage() { return stage_ >= 0 && stage_ < int(stages_.size()) ? &stages_[std::size_t(stage_)] : nullptr; }
    adventure56::Object* object(std::uint32_t hash) { return game_.scene.object(hash); }
    adventure56::Object* goodObject(const Good& g) { return object(xq::hash(g.name)); }

    // The bytes after the grid in the loop (+0x194): the grid's 105, the
    // stage number (+0x1fd), the 105 marks (+0x1fe). The code reads past a
    // row's end into the next row and past the grid into the stage number
    // and the marks (goods overhanging the right edge); these reads keep
    // that.
    std::uint8_t cellByte(int i) const {
        if (i >= 0 && i < kCells) {
            return grid_[std::size_t(i)];
        }
        if (i == kCells) {
            return std::uint8_t(stage_);
        }
        if (i > kCells && i - kCells - 1 < kCells) {
            return marks_[std::size_t(i - kCells - 1)];
        }
        return 0;
    }
    void setCellByte(int i, std::uint8_t v) {
        if (i >= 0 && i < kCells) {
            grid_[std::size_t(i)] = v;
        } else if (i == kCells) {
            stage_ = std::int8_t(v);
        } else if (i > kCells && i - kCells - 1 < kCells) {
            marks_[std::size_t(i - kCells - 1)] = v;
        }
    }
    static int cellIndex(int x, int y) { return y * kCols + x; }
    static int maskCell(int k) { return (k / 3) * kCols + k % 3; } // a mask cell's grid step

    // A good object's cell: its position plus 9, less the shelf origin,
    // over 19 (the code's packed 16-bit arithmetic, truncating).
    Cell cellOf(const adventure56::Object& o) const {
        const auto& p = o.position.value();
        const std::int16_t x = std::int16_t(std::int16_t(int(p[0])) + 9 - originX_);
        const std::int16_t y = std::int16_t(std::int16_t(int(p[1])) + 9 - originY_);
        return {std::int16_t(x / kCell), std::int16_t(y / kCell)};
    }
    std::array<float, 2> pixelOf(Cell c) const {
        return {float(std::int16_t(originX_ + c.x * kCell)), float(std::int16_t(originY_ + c.y * kCell))};
    }
    static void moveTo(adventure56::Object& o, std::array<float, 2> p, float frames, int curve) {
        const float z = o.position.value()[2];
        if (frames > 0) {
            o.position.start({p[0], p[1], z}, frames, curve);
        } else {
            o.position.set({p[0], p[1], z});
        }
    }

    void markGood(const Good& g, int number, Cell at);
    void placeInRack(std::uint32_t hash, int index);
    void insertInRack(std::uint32_t hash, int slot);
    int neighbours(int good, Neighbours& list, int shift);
    bool restsOnlyOn(int number, int self);
    int dependants(int number, std::array<std::uint8_t, 12>& out);
    bool findAnchors(std::uint32_t hash);
    bool nearestAnchor(const Good& g, Cell& at) const;
    int moveWithDependants(std::uint32_t hash, const std::array<std::uint8_t, 12>& dependants);
    void stamp(adventure56::Object& canvas, float x, float y);

    std::vector<Stage> stages_;
    std::array<std::uint32_t, kRackSlots> rack_{}; // +0x16c
    std::array<std::uint8_t, kCells> grid_{};     // +0x194: the stage's grid, a good's number << 4
    std::int8_t stage_ = -1;                      // +0x1fd
    std::array<std::uint8_t, kCells> marks_{};    // +0x1fe: 1 covered, 2 an anchor
    std::int8_t scroll_ = 0;                      // +0x267
    std::int16_t lastX_ = 0, lastY_ = 0;          // +0x268: where a lifted good was (y -1: the rack)
    std::int16_t originX_ = 0, originY_ = 0;      // +0x26c
    std::array<std::int8_t, 10> walked_{};        // +0x270: goods a customer passed
    std::int8_t walkedCount_ = 0;                 // +0x284
    std::array<std::int8_t, 10> found_{};         // +0x27a: the last walk's next goods
};

void ShopLoop::markGood(const Good& g, int number, Cell at) {
    // 0x292094: the good's number in the high nibble of its mask's cells.
    for (int k = 0; k < 9; ++k) {
        if (g.cells[std::size_t(k)] != 0) {
            const int i = cellIndex(at.x, at.y) + maskCell(k);
            setCellByte(i, std::uint8_t(cellByte(i) | std::uint8_t(number << 4)));
        }
    }
}

void ShopLoop::placeInRack(std::uint32_t hash, int index) {
    // 0x292244: a good in rack slot `index` (scrolled): centred in its 60
    // pixel slot from x 40, standing on y 61; its shadow "a_<name>" at
    // 107 + 27 index, 211 (at once when drawn, else over 8 frames). Both
    // move over 8 frames on curve 2.
    Stage* s = stage();
    adventure56::Object* o = object(hash);
    if (!s || !o) {
        return;
    }
    const int k = s->goodIndex(hash);
    if (k < 0) {
        return;
    }
    const Good& g = s->goods[std::size_t(k)];
    if (auto* shadow = object(xq::hash("a_" + g.name))) {
        const std::array<float, 2> p{107.0f + float(index) * 27.0f, 211.0f};
        moveTo(*shadow, p, shadow->drawn ? 0.0f : 8.0f, 2);
    }
    const float x = 40.0f + float(index - scroll_) * 60.0f + (30.0f - float(g.width >> 1));
    const float y = (60.0f - float(g.height)) + 1.0f;
    moveTo(*o, {x, y}, 8.0f, 2);
}

void ShopLoop::insertInRack(std::uint32_t hash, int slot) {
    // 0x1d5634: into a visible slot (0..3): onto an empty place it goes
    // after the last good; onto another good the goods from there move one
    // on.
    slot = std::clamp(slot, 0, kRackShown - 1);
    int at = slot + scroll_;
    if (at < 0 || at >= kRackSlots) {
        return;
    }
    if (rack_[std::size_t(at)] == 0) {
        while (at > 0 && rack_[std::size_t(at - 1)] == 0) {
            --at;
        }
    } else if (rack_[std::size_t(at)] != hash) {
        for (int i = kRackSlots - 2; i >= at; --i) {
            if (rack_[std::size_t(i)] != 0) {
                rack_[std::size_t(i + 1)] = rack_[std::size_t(i)];
                placeInRack(rack_[std::size_t(i)], i + 1);
            }
        }
    }
    rack_[std::size_t(at)] = hash;
    placeInRack(hash, at);
}

int ShopLoop::neighbours(int good, Neighbours& list, int shift) {
    // 0x2926ec: the cells around a good on the shelf (shifted `shift`
    // columns): below, above, right and left of each of its cells unless
    // the good itself is there; each good once (0x2606c0: an empty cell
    // matches the list's empty entries, so empty cells are not added; a
    // good seen above again marks its entry's side 1).
    list = {};
    Stage* s = stage();
    if (!s || good < 0 || good >= kGoods) {
        return 0;
    }
    const Good& g = s->goods[std::size_t(good)];
    adventure56::Object* o = goodObject(g);
    if (!o) {
        return 0;
    }
    const Cell at = cellOf(*o);
    if (at.y < 0) {
        return 0;
    }
    const int self = good + 1;
    int count = 0;
    auto add = [&](int x, int y, int side) {
        const std::uint8_t v = cellByte(cellIndex(x, y));
        if (v == 0xff) {
            return;
        }
        const int id = v >> 4;
        for (auto& e : list) {
            if (e.good == id) {
                if (side == 1) {
                    e.side = 1;
                }
                return;
            }
        }
        if (count >= int(list.size())) {
            return; // port safeguard
        }
        list[std::size_t(count)] = {std::int16_t(x), std::int16_t(y), std::uint8_t(side), std::uint8_t(id)};
        ++count;
    };
    for (int k = 0; k < 3; ++k) {
        const int row = at.y + k;
        for (int j = 0; j < 3; ++j) {
            const int col = std::int16_t(at.x + shift) + j;
            if (g.cells[std::size_t(k * 3 + j)] == 0) {
                continue;
            }
            if (row < 6 && self != cellByte(cellIndex(col, row + 1)) >> 4) {
                add(col, row + 1, 2);
            }
            if (row > 0 && self != cellByte(cellIndex(col, row - 1)) >> 4) {
                add(col, row - 1, 1);
            }
            if (col < 14 && self != cellByte(cellIndex(col + 1, row)) >> 4) {
                add(col + 1, row, 4);
            }
            if (col > 0 && self != cellByte(cellIndex(col - 1, row)) >> 4) {
                add(col - 1, row, 3);
            }
        }
    }
    return count;
}

bool ShopLoop::restsOnlyOn(int number, int self) {
    // 0x2924a4: good `number` (1-based) stands on nothing but good `self`:
    // none of its bottom cells (mask without bit 1) is on a board or on
    // another good.
    Stage* s = stage();
    if (!s || number < 1 || number > kGoods) {
        return false;
    }
    const Good& g = s->goods[std::size_t(number - 1)];
    adventure56::Object* o = goodObject(g);
    if (!o) {
        return false;
    }
    const Cell at = cellOf(*o);
    for (int k = 0; k < 3; ++k) {
        for (int j = 0; j < 3; ++j) {
            const std::int8_t m = g.cells[std::size_t(k * 3 + j)];
            if (m == 0 || (m & 2) != 0) {
                continue;
            }
            const int i = cellIndex(at.x + j, at.y + k);
            const int below = cellByte(i + kCols) >> 4;
            if ((cellByte(i) & 2) != 0 || (below != 0 && below != self)) {
                return false;
            }
        }
    }
    return true;
}

int ShopLoop::dependants(int number, std::array<std::uint8_t, 12>& out) {
    // 0x1d5ee0: the goods right above good `number` that stand on it alone.
    Neighbours list;
    const int n = neighbours(number - 1, list, 0);
    int found = 0;
    for (int i = 0; i < n; ++i) {
        const auto& e = list[std::size_t(i)];
        if (e.side == 1 && restsOnlyOn(e.good, number) && found < int(out.size())) {
            out[std::size_t(found++)] = e.good;
        }
    }
    return found;
}

bool ShopLoop::findAnchors(std::uint32_t hash) {
    // 0x1d5f94: every cell the good may stand at (mark bit 1, its mask's
    // cells bit 0 = 1, written, not added): inside the shelf, on no wall
    // and no good, and resting: one of its bottom cells on a board or on a
    // good below.
    Stage* s = stage();
    const int k = s ? s->goodIndex(hash) : -1;
    if (k < 0) {
        return false;
    }
    const Good& g = s->goods[std::size_t(k)];
    marks_ = {};
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            bool blocked = false, floating = true;
            for (int a = 0; a < 3; ++a) {
                const int y = r + a;
                for (int b = 0; b < 3; ++b) {
                    const std::int8_t m = g.cells[std::size_t(a * 3 + b)];
                    if (m == 0) {
                        continue;
                    }
                    const int x = c + b;
                    const std::uint8_t v = cellByte(y * kCols + x);
                    if (v == 0xff) {
                        continue;
                    }
                    if (!(y < kRows && x < kCols) || blocked) {
                        blocked = true;
                        continue;
                    }
                    if (floating && (m & 2) == 0 &&
                        ((v & 2) != 0 || (y < kRows - 1 && (cellByte((y + 1) * kCols + x) & 0xf0) != 0))) {
                        floating = false;
                    }
                    if ((std::uint8_t(m) & v) != 0) {
                        blocked = true;
                    }
                }
            }
            if (blocked || floating) {
                continue;
            }
            const int base = r * kCols + c;
            for (int a = 0; a < 9; ++a) {
                if (g.cells[std::size_t(a)] != 0) {
                    const int i = base + maskCell(a);
                    if (i >= 0 && i < kCells) {
                        marks_[std::size_t(i)] = 1;
                    }
                }
            }
            marks_[std::size_t(base)] |= 2;
        }
    }
    return true;
}

bool ShopLoop::nearestAnchor(const Good& g, Cell& at) const {
    // 0x34745c: the anchor nearest to `at` in pixels (later ones win
    // ties, from 1000 down); none within the good's width or height: false.
    const auto p = pixelOf(at);
    float best = 1000.0f;
    for (int y = 0; y < kRows; ++y) {
        for (int x = 0; x < kCols; ++x) {
            if ((marks_[std::size_t(y * kCols + x)] & 2) == 0) {
                continue;
            }
            const auto q = pixelOf({std::int16_t(x), std::int16_t(y)});
            const float dx = q[0] - p[0], dy = q[1] - p[1];
            float d = dx * dx + dy * dy;
            d = d > 0.0f ? std::sqrt(d) : 0.0f;
            if (d <= best) {
                at = {std::int16_t(x), std::int16_t(y)};
                best = d;
            }
        }
    }
    return !(best >= float(g.width) && float(g.height) <= best);
}

int ShopLoop::moveWithDependants(std::uint32_t hash, const std::array<std::uint8_t, 12>& carried) {
    // 0x1d45c4: the anchors in the good's row 0..4 cells right and 1..4
    // left (up to the first that is none) at which the goods it carries
    // would still stand on it alone; those anchors (and their cells) are
    // kept, the others go. True when one besides its own place is left.
    Stage* s = stage();
    const int k = s ? s->goodIndex(hash) : -1;
    adventure56::Object* o = k >= 0 ? goodObject(s->goods[std::size_t(k)]) : nullptr;
    if (!o) {
        return 0;
    }
    const Good& g = s->goods[std::size_t(k)];
    const Cell at = cellOf(*o);
    std::array<std::uint8_t, kCells> keep{};
    bool moved = false;
    auto tryShift = [&](int d) {
        const int anchor = at.y * kCols + d + at.x;
        if ((cellByte(anchor + kCells + 1) & 2) == 0) { // the mark (+0x1fe)
            return false;
        }
        Neighbours list;
        const int n = neighbours(k, list, d);
        std::array<std::uint8_t, 4> above{};
        int count = 0;
        for (int i = 0; i < n && count < 4; ++i) {
            const auto& e = list[std::size_t(i)];
            if (e.side == 1 && restsOnlyOn(e.good, k + 1)) {
                above[std::size_t(count++)] = e.good;
            }
        }
        int wanted = 0, kept = 0;
        for (int i = 0; i < 3; ++i) {
            const std::uint8_t v = carried[std::size_t(i)];
            if (v != 0) {
                ++wanted;
                if (v == above[0] || v == above[1] || v == above[2]) {
                    ++kept;
                }
            }
        }
        if (wanted == kept) {
            for (int a = 0; a < 9; ++a) {
                if (g.cells[std::size_t(a)] != 0) {
                    const int i = anchor + maskCell(a);
                    if (i >= 0 && i < kCells) {
                        keep[std::size_t(i)] |= 1;
                    }
                }
            }
            if (anchor >= 0 && anchor < kCells) {
                keep[std::size_t(anchor)] |= 2;
            }
            if (d != 0) {
                moved = true;
            }
        }
        return true;
    };
    for (int d = 0; d < 5 && tryShift(d); ++d) {
    }
    for (int d = -1; d > -5 && tryShift(d); --d) {
    }
    if (moved) {
        for (int i = 0; i < kCells; ++i) {
            marks_[std::size_t(i)] &= keep[std::size_t(i)];
        }
    }
    return moved;
}

void ShopLoop::stamp(adventure56::Object& canvas, float x, float y) {
    // 0x391a04 in pen mode 3 (0x39f8b8): a square of the pen width at the
    // point (canvas pixels), in the pen colour.
    if (canvas.content != 8 || !canvas.picture) {
        return;
    }
    auto& pic = *canvas.picture;
    const float w = canvas.pen.width;
    const int x0 = std::max(0, int(std::floor(std::min(x, x + w))));
    const int y0 = std::max(0, int(std::floor(std::min(y, y + w))));
    const int x1 = std::min(pic.width, int(std::ceil(std::max(x, x + w))));
    const int y1 = std::min(pic.height, int(std::ceil(std::max(y, y + w))));
    const platform::Color colour{
        std::uint8_t(std::clamp(canvas.pen.colour[0], 0.0f, 1.0f) * 255.0f + 0.5f),
        std::uint8_t(std::clamp(canvas.pen.colour[1], 0.0f, 1.0f) * 255.0f + 0.5f),
        std::uint8_t(std::clamp(canvas.pen.colour[2], 0.0f, 1.0f) * 255.0f + 0.5f),
        std::uint8_t(std::clamp(canvas.pen.colour[3], 0.0f, 1.0f) * 255.0f + 0.5f)};
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            pic.pixels[std::size_t(yy) * std::size_t(pic.width) + std::size_t(xx)] = colour;
        }
    }
}

bool ShopLoop::command(int number, xq::Call& c, std::size_t first) {
    const std::size_t n = operands(c, first);
    switch (number) {
    case 0: { // {global good number, -> stage (1-based), -> good in it}
        if (n < 2) {
            return false;
        }
        const int want = c.integer(first);
        int sum = 0, s = 0;
        for (int k = 0; k < kStages; ++k) {
            sum += stages_[std::size_t(s)].count;
            if (want <= sum) {
                break;
            }
            s = (s + 1) & 0xff;
            if (s >= kStages) {
                s = kStages - 1; // port safeguard
            }
        }
        const int count = stages_[std::size_t(s)].count;
        putInt(c, first + 1, s + 1);
        putInt(c, first + 2, count + (want - sum) - 1);
        return true;
    }
    case 10: { // {stage (1-based)}: the rack, the grid, the shelf's origin
        if (n != 1) {
            return false;
        }
        stage_ = std::int8_t(c.integer(first) - 1);
        Stage* s = stage();
        if (!s) {
            return true;
        }
        std::copy(s->order.begin() + 1, s->order.end(), rack_.begin());
        rack_[kRackSlots - 1] = 0;
        grid_ = s->grid;
        scroll_ = 0;
        originX_ = std::int16_t(kShelfX + s->offset);
        originY_ = kShelfY;
        return true;
    }
    case 100: { // {object}: the stage's first good at its cell (and the star there)
        Stage* s = stage();
        if (n == 0 || !s) {
            return false;
        }
        const std::uint32_t hash = nameOf(c, first);
        adventure56::Object* o = object(hash);
        if (!o) {
            return false;
        }
        const Cell start{s->startX, s->startY};
        const auto p = pixelOf(start);
        moveTo(*o, p, 0, 0);
        const int k = s->goodIndex(hash);
        if (k >= 0) {
            markGood(s->goods[std::size_t(k)], k + 1, start);
        }
        if (auto* star = object(xq::hash("star_ef"))) {
            moveTo(*star, p, 0, 0);
        }
        return true;
    }
    case 101: { // {object, kind}: touch rectangles over the good's cells (rows merged)
        Stage* s = stage();
        adventure56::Object* o = n >= 2 ? object(nameOf(c, first)) : nullptr;
        const int k = s && n >= 2 ? s->goodIndex(nameOf(c, first)) : -1;
        if (!o || k < 0) {
            return false;
        }
        const Good& g = s->goods[std::size_t(k)];
        for (int row = 0; row < 3; ++row) {
            float x = 0;
            for (int j = 0; j < 3; ++j) {
                if (g.cells[std::size_t(row * 3 + j)] != 0) {
                    float w = float(kCell);
                    while (j + 1 < 3 && g.cells[std::size_t(row * 3 + j + 1)] != 0) {
                        w += float(kCell);
                        ++j;
                    }
                    if (o->areas.size() < 16) {
                        o->areas.push_back({c.integer(first + 1) & 0xff, x, float(row * kCell), w, float(kCell)});
                    }
                }
                x += float(kCell);
            }
        }
        return true;
    }
    case 102: { // {-> the stage's number of goods}
        Stage* s = stage();
        if (n == 0 || !s) {
            return false;
        }
        putInt(c, first, s->count);
        return true;
    }
    case 103: { // {good, -> name, -> width, -> height, -> colour, -> kind, -> order - 1}
        Stage* s = stage();
        const int k = n >= 2 ? c.integer(first) : -1;
        if (!s || k < 0 || k >= kGoods) {
            return false;
        }
        const Good& g = s->goods[std::size_t(k)];
        put(c, first + 1, Value::ofText(g.name));
        if (n < 3) {
            return true;
        }
        putInt(c, first + 2, g.width);
        if (n > 3) {
            putInt(c, first + 3, g.height);
        }
        if (n > 4) {
            put(c, first + 4, Value::ofText(s->tagText(g.colour)));
        }
        if (n > 5) {
            put(c, first + 5, Value::ofText(s->tagText(g.kind)));
        }
        if (n >= 7) {
            const std::uint32_t h = xq::hash(g.name);
            for (int i = 0; i < kGoods; ++i) {
                if (s->order[std::size_t(i)] == h) {
                    putInt(c, first + 6, i - 1);
                }
            }
        }
        return true;
    }
    case 104: { // {object, -> slot}: a good let go over the rack goes into the nearest visible slot
        Stage* s = stage();
        const std::uint32_t hash = n ? nameOf(c, first) : 0;
        adventure56::Object* o = n ? object(hash) : nullptr;
        const int k = s ? s->goodIndex(hash) : -1;
        if (!o || k < 0) {
            return false;
        }
        const Good& g = s->goods[std::size_t(k)];
        const auto& p = o->position.value();
        const float cx = p[0] + float(std::int8_t(g.width / 2));
        const float cy = p[1] + float(std::int8_t(g.height / 2));
        const float dy = 0.0f - cy;
        float best = 100000.0f;
        int pick = 0;
        for (int u = 0; u < kRackShown; ++u) {
            const float dx = (70.0f + float(u) * 60.0f) - cx;
            float d = dx * dx;
            d = d + dy * dy;
            if (best >= d) {
                best = d;
                pick = u;
            }
        }
        if (n >= 2) {
            putInt(c, first + 1, pick);
        }
        insertInRack(hash, pick);
        return true;
    }
    case 105: { // {object}: a good taken out of the rack (the others close up)
        if (n == 0) {
            return false;
        }
        const std::uint32_t hash = nameOf(c, first);
        for (int i = 0; i < kRackSlots; ++i) {
            if (rack_[std::size_t(i)] == hash) {
                for (int j = i; j < kRackSlots - 1; ++j) {
                    rack_[std::size_t(j)] = rack_[std::size_t(j + 1)];
                    placeInRack(rack_[std::size_t(j)], j);
                }
                lastX_ = std::int16_t(i);
                lastY_ = -1;
                break;
            }
        }
        rack_[kRackSlots - 1] = 0;
        return true;
    }
    case 106: { // {object}: a good lifted off the shelf
        Stage* s = stage();
        const std::uint32_t hash = n ? nameOf(c, first) : 0;
        adventure56::Object* o = n ? object(hash) : nullptr;
        const int k = s ? s->goodIndex(hash) : -1;
        if (!o || k < 0) {
            return false;
        }
        const Good& g = s->goods[std::size_t(k)];
        const Cell at = cellOf(*o);
        for (int a = 0; a < 9; ++a) {
            if (g.cells[std::size_t(a)] != 0) {
                const int i = cellIndex(at.x, at.y) + maskCell(a);
                setCellByte(i, std::uint8_t(cellByte(i) & 0x0f));
            }
        }
        lastX_ = at.x;
        lastY_ = at.y;
        return true;
    }
    case 107: { // {object, -> on the shelf}: a good let go over the shelf
        Stage* s = stage();
        const std::uint32_t hash = n ? nameOf(c, first) : 0;
        adventure56::Object* o = n ? object(hash) : nullptr;
        const int k = s ? s->goodIndex(hash) : -1;
        if (!o || k < 0) {
            return false;
        }
        const Good& g = s->goods[std::size_t(k)];
        Cell at = cellOf(*o);
        int onShelf = 1;
        if (nearestAnchor(g, at)) {
            markGood(g, k + 1, at);
            moveTo(*o, pixelOf(at), 5.0f, 2);
        } else if (lastY_ < 0) {
            insertInRack(hash, lastX_);
            onShelf = 0;
        } else {
            Cell back{lastX_, lastY_};
            nearestAnchor(g, back);
            lastX_ = back.x;
            lastY_ = back.y;
            markGood(g, k + 1, back);
            moveTo(*o, pixelOf(back), 5.0f, 2);
        }
        putInt(c, first + 1, onShelf);
        return true;
    }
    case 108: { // {by, -> at an end}: the rack scrolls
        Stage* s = stage();
        if (n < 2 || !s) {
            return false;
        }
        int want = std::int8_t(c.integer(first) + scroll_);
        scroll_ = std::int8_t(want);
        int filled = 0;
        for (int i = 0; i < s->count && i < kRackSlots; ++i) {
            if (rack_[std::size_t(i)] != 0) {
                ++filled;
            }
        }
        int atEnd = 0;
        if (want < 1 || filled - 4 <= want) {
            want = std::max(want, 0);
            want = std::min(want, int(std::int8_t(s->count - 4)));
            atEnd = 1;
            scroll_ = std::int8_t(want);
        }
        putInt(c, first + 1, atEnd);
        for (int i = 0; i < s->count && i < kRackSlots; ++i) {
            if (rack_[std::size_t(i)] != 0) {
                placeInRack(rack_[std::size_t(i)], i);
            }
        }
        return true;
    }
    case 109: { // {object, -> can move}: a good taken up; the places it cannot go are shaded
        if (n == 0) {
            return false;
        }
        Stage* s = stage();
        const std::uint32_t hash = nameOf(c, first);
        const int k = s ? s->goodIndex(hash) : -1;
        adventure56::Object* canvas = object(xq::hash("disp_info"));
        if (!canvas || k < 0) {
            return false;
        }
        int movable = 0;
        int count = 0;
        std::array<std::uint8_t, 12> carried{};
        if (s->order[0] != hash) {
            count = dependants(k + 1, carried);
            if (count != 3) {
                movable = 1;
            }
        }
        findAnchors(hash);
        if (movable != 0) {
            if (count != 0) {
                movable = moveWithDependants(hash, carried) ? 2 : 0;
            }
            canvas->pen.colour = kShade; // 0x1776a8
            const auto& origin = canvas->position.value();
            for (int r = 0; r < kRows; ++r) {
                for (int col = 0; col < kCols; ++col) {
                    const int i = r * kCols + col;
                    if ((marks_[std::size_t(i)] & 1) != 0 || (grid_[std::size_t(i)] & 0xf0) != 0) {
                        continue;
                    }
                    const auto p = pixelOf({std::int16_t(col), std::int16_t(r)});
                    stamp(*canvas, p[0] - origin[0], p[1] - origin[1]);
                }
            }
            if (movable == 2) {
                canvas->pen.width = 60.0f;
                for (int u = 0; u < kRackShown; ++u) {
                    stamp(*canvas, 40.0f + float(u) * 60.0f - origin[0], 1.0f - origin[1]);
                }
                canvas->pen.width = 19.0f;
            }
        }
        if (movable == 0) {
            // The goods it carries shake (offset 1 -> -1 over 5 frames, curve 0x21).
            for (int i = 0; i < count; ++i) {
                const int g = carried[std::size_t(i)];
                if (g < 1 || g > kGoods) {
                    continue;
                }
                if (auto* o = goodObject(s->goods[std::size_t(g - 1)])) {
                    const float z = o->offset.value()[2];
                    o->offset.set({1.0f, 0.0f, z});
                    o->offset.start({-1.0f, 0.0f, z}, 5.0f, 0x21);
                }
            }
        }
        if (n >= 2) {
            putInt(c, first + 1, movable);
        }
        return true;
    }
    case 110: { // {tag}: its number (-1)
        Stage* s = stage();
        if (n == 0 || !s) {
            return false;
        }
        const std::uint32_t h = nameOf(c, first);
        int found = -1;
        for (std::size_t i = 0; i < s->tags.size(); ++i) {
            if (s->tags[i].second == h) {
                found = int(i);
                break;
            }
        }
        c.result = Value::ofInt(found);
        return true;
    }
    case 200: { // the customers' walk: {good (10+: the first), -> next count, -> next, -> tag, -> side [, -> x, -> y [, -> link, -> link kind]]}
        Stage* s = stage();
        if (n < 5 || !s) {
            return false;
        }
        int from = c.integer(first) & 0xff;
        if (from >= 10) {
            walked_ = {};
            walkedCount_ = 0;
            from = s->goodIndex(s->order[0]) & 0xff;
            putInt(c, first, from);
        }
        Neighbours list;
        const int count = neighbours(from, list, 0);
        found_ = {};
        int next = 0, last = 0;
        std::uint32_t shared = 0;
        const int self = (from + 1) & 0xff;
        for (int i = 0; i < count; ++i) {
            const auto& e = list[std::size_t(i)];
            const int id = e.good;
            std::uint32_t match = 0;
            bool skip = self == 0 || id == 0 || self == id;
            for (int w = 0; !skip && w < walkedCount_; ++w) {
                skip = walked_[std::size_t(w)] == id;
            }
            if (!skip && self <= kGoods && id <= kGoods) {
                const Good& a = s->goods[std::size_t(self - 1)];
                const Good& b = s->goods[std::size_t(id - 1)];
                match = a.colour == b.colour ? a.colour : a.kind == b.kind ? a.kind : 0;
            }
            if (match != 0) {
                found_[std::size_t(next++)] = std::int8_t(id - 1);
                shared = match;
                last = i;
            }
        }
        putInt(c, first + 1, next);
        const auto& e = list[std::size_t(last)];
        putInt(c, first + 2, std::int8_t(e.good) - 1);
        if (shared != 0) {
            put(c, first + 3, Value::ofText(s->tagText(shared)));
        }
        putInt(c, first + 4, e.side);
        const auto p = pixelOf({e.x, e.y});
        if (n > 5) {
            put(c, first + 5, Value::ofFloat(p[0]));
        }
        if (n > 6) {
            put(c, first + 6, Value::ofFloat(p[1]));
        }
        if (next == 1) {
            walked_[std::size_t(walkedCount_ % 10)] = std::int8_t(from + 1);
            ++walkedCount_;
        } else {
            walked_ = {};
            walkedCount_ = 0;
        }
        if (n > 7) {
            // The link: a good (all stages counted) whose colour / kind is
            // the shared tag.
            int before = 0;
            for (int t = 0; t < kStages && t != stage_; ++t) {
                before = (before + stages_[std::size_t(t)].count) & 0xff;
            }
            for (int g = 0; g < s->count; ++g) {
                const Good& good = s->goods[std::size_t(g)];
                if (good.colour == shared) {
                    putInt(c, first + 7, before + g);
                    putInt(c, first + 8, 2);
                    break;
                }
                if (good.kind == shared) {
                    putInt(c, first + 7, before + g);
                    putInt(c, first + 8, 3);
                    break;
                }
            }
        }
        return true;
    }
    case 201: { // {animation format, priority}: the next goods show it, in front
        Stage* s = stage();
        if (n < 2 || !s) {
            return false;
        }
        const std::string* format = c.text(first);
        for (int i = 0; i < 10; ++i) {
            const int g = found_[std::size_t(i)];
            if (g == 0 || g < 0 || g >= kGoods) {
                break; // a found good 0 ends the list too (0x1d5428)
            }
            adventure56::Object* o = goodObject(s->goods[std::size_t(g)]);
            if (!o) {
                continue;
            }
            char name[64];
            std::snprintf(name, sizeof name, format ? format->c_str() : "", g);
            game_.playAnimation(*o, Value::ofText(name), 0);
            game_.scene.setPriority(*o, std::int16_t(c.integer(first + 1)));
        }
        return true;
    }
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<Loop> makeShopLoop(Story& story, int id, std::vector<Value> values) {
    return std::make_unique<ShopLoop>(story, id, std::move(values));
}

} // namespace picarat::adventure5
