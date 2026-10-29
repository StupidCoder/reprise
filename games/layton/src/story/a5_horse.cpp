// The horse ride (loop 6300, lt5CLoopMiniHorse; creator 0x37cf0c,
// constructor 0x3855e8, vtable 0x401be8).
//
// The ride is a script loop: its start (0x203d98) loads seq/mini_horse, and
// Init and Main run as in every script loop (0x2ad130 / 0x2ad1f8). The
// script draws everything and moves the horse; the loop keeps what the
// script cannot hold itself and answers its commands (2110, vtable +0x4c =
// 0x203de4):
//   * the course: the shapes of the map parts (100..103), the sections and
//     their pieces (104..116), three tables of Bezier bends per part shape
//     (200..204 the road, 300..304 the horse's way, 406..408 where the items
//     on a bend lie);
//   * the items on the road (400..405): barrels, coins and carrots, placed
//     on a drum (the course turns under the horse: parts_00's rotation),
//     copied to 48 live slots for the two pieces in view, drawn as the
//     instances of col_obj00 (3) and hit by the horse (405);
//   * the masked rider's way (500..506) and a three-node spring chain the
//     camera follows (601..606);
//   * the score (700..706).
// Docs: .analysis/adventure56/docs/story.md ("The horse ride").
#include "story/a5_horse.hpp"
#include "system/a5_game.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace picarat::adventure5 {

namespace {

using xq::Value;

constexpr float kDegree = 0.0174532924f;       // 0x204e60
constexpr float kHalfPi = 1.57079637f;          // 0x2064d4
constexpr std::size_t kItemsPerPiece = 16;      // (0x30c - 0xc) / 0x30
constexpr std::size_t kPiecesPerSection = 13;   // 0x27ac = 0x10 + 13 * 0x30c
constexpr std::size_t kLiveItems = 48;          // 0x1e0 .. 0xae0

struct Vec2 {
    float x = 0, y = 0;
};

// A list the script sizes first and then fills (0x2ca830 / 0x2ce56c); its
// "full" check (102, 202, 302, 408, 502) compares the two.
template <class T>
struct Table {
    std::size_t capacity = 0;
    std::vector<T> rows;

    void reset(int count) {
        capacity = std::size_t(std::max(0, count));
        rows.clear();
        rows.reserve(capacity);
    }
    bool add(T row) {
        if (rows.size() >= capacity) {
            return false;
        }
        rows.push_back(std::move(row));
        return true;
    }
    bool full() const { return capacity == rows.size(); }
    // 0x2d0408: a row by index, or null.
    const T* at(int i) const { return i >= 0 && std::size_t(i) < rows.size() ? &rows[std::size_t(i)] : nullptr; }
    T* at(int i) { return i >= 0 && std::size_t(i) < rows.size() ? &rows[std::size_t(i)] : nullptr; }
};

// 101: a map part's shape (0x24 bytes at +0x164). `kind` is the script's
// number for it; `bendFrom` .. `bendTo` (radians along the part) is where
// its bend lies (both 0 for the straight parts).
struct PartShape {
    std::uint32_t name = 0;
    int kind = 0;
    std::array<float, 7> values{}; // [5] bendFrom, [6] bendTo
};

// 201 / 301 / 407: a cubic Bezier from the origin (0x24 bytes), for a part
// shape and one of its ways through (variant).
struct Bend {
    std::uint32_t name = 0;
    int part = 0;
    int variant = 0;
    std::array<float, 6> controls{}; // P1, P2, P3 (x, y)

    // 0x287b2c: the point at t (P0 is the origin).
    Vec2 at(float t) const {
        float u = 1.0f - t;
        const float a = u * 3.0f;
        u = a * u;
        const float b3 = u * t;     // 3 (1 - t)^2 t
        const float b2 = a * t * t; // 3 (1 - t) t^2
        const float b1 = t * t * t;
        Vec2 p;
        p.x = controls[0] * b3;
        p.x += b2 * controls[2];
        p.x += b1 * controls[4];
        p.y = controls[1] * b3;
        p.y += b2 * controls[3];
        p.y += b1 * controls[5];
        return p;
    }
};

// 0x287a58 / 204 / 304 / 505: (a - from) / (to - from); above 1 is 1 (the
// bit compare lets negative values through).
float span(float a, float from, float to) {
    const float t = (a - from) / (to - from);
    return t > 1.0f || std::isnan(t) ? 1.0f : t;
}

// An item on the road (0x30 bytes: in a piece from +0xc, live at +0x1e0).
struct Item {
    int slot = 0;          // +0x00: the live copy's piece (0 current, 1 next)
    int lane = 0;          // +0x04: 0..5 across the road
    int kind = -1;         // +0x08: 0 barrel, 1 coin, 2 carrot
    bool active = false;   // +0x0c
    bool hit = false;      // +0x10
    float angle = 0;       // +0x14: radians along the drum
    std::array<float, 3> rotation{}; // +0x18
    std::array<float, 3> position{}; // +0x24
};

// A piece of a section (0x30c bytes).
struct Piece {
    int part = 0;    // 107: the part shape's kind
    int variant = 0; // 108: its way through
    std::size_t count = 0;
    std::array<Item, kItemsPerPiece> items{};
};

// 106: a section of the course (0x27ac bytes).
struct Section {
    std::uint32_t name = 0;
    int pieces = 0;
    float second = 0; // +0x08 (506)
    float first = 0;  // +0x0c (114)
    std::array<Piece, kPiecesPerSection> piece{};
};

// A piece the road is at: {section, piece}; {-1, -1} when there is none.
struct Cursor {
    std::int16_t section = 0, piece = 0;
    bool none() const { return section == -1 && piece == -1; }
};

// 501: a stretch of the masked rider's way (0x24 bytes).
struct MaskWay {
    int kind = 0;
    int id = 0;
    std::array<float, 6> controls{};

    Vec2 at(float t) const {
        Bend b;
        b.controls = controls;
        return b.at(t);
    }
};

// A node of the chain (0x48 bytes at +0xb0c, 287bd4).
struct Node {
    bool held = false;  // +0x08 (604)
    bool fixed = false; // +0x0c (nothing sets it)
    Vec2 position;      // +0x10
    Vec2 velocity;      // +0x18
    Vec2 step;          // +0x20
    Vec2 force;         // +0x28
    Vec2 push;          // +0x30 (nothing sets it)
    Vec2 back;          // +0x38: the pull of the node before
    Vec2 forward;       // +0x40: the pull of the node after
};

class HorseLoop final : public ScriptLoop {
public:
    HorseLoop(Story& story, int id, std::vector<Value> values)
        : ScriptLoop(story, id, std::move(values), "mini_horse") {}

    void afterFrame() override {
        // 0x203980: the base end of frame, then (once the camera and
        // obj_horse exist) their matrices are brought up to date
        // (0x2c9ebc, 0x2c9c3c, 0x2c9bb4) so the camera follows the horse
        // the same frame; the port computes views when it draws.
        ScriptLoop::afterFrame();
    }

    bool command(int number, xq::Call& c, std::size_t first) override;

private:
    std::size_t operands(const xq::Call& c, std::size_t first) const {
        return c.count() > first ? c.count() - first : 0;
    }
    bool result(xq::Call& c, Value v) {
        c.result = std::move(v);
        return true;
    }
    bool done(xq::Call& c) { return result(c, Value::ofInt(1)); }
    void put(xq::Call& c, std::size_t i, Value v) {
        if (Value* r = c.reg(i)) {
            *r = std::move(v);
        }
    }
    void putInt(xq::Call& c, std::size_t i, int v) { put(c, i, Value::ofInt(v)); }
    void putReal(xq::Call& c, std::size_t i, float v) { put(c, i, Value::ofFloat(v)); }
    std::uint32_t nameOf(const xq::Call& c, std::size_t i) const {
        const std::string* t = c.text(i);
        return xq::hash(t ? *t : std::string());
    }

    Section* section(std::uint32_t name) {
        for (auto& s : sections_.rows) {
            if (s.name == name) {
                return &s;
            }
        }
        return nullptr;
    }
    int partKind(std::uint32_t name) const {
        for (const auto& p : parts_.rows) {
            if (p.name == name) {
                return p.kind;
            }
        }
        return 0;
    }
    static const Bend* bend(const Table<Bend>& t, int part, int variant) {
        for (const auto& b : t.rows) {
            if (b.part == part && b.variant == variant) {
                return &b;
            }
        }
        return nullptr;
    }
    Piece* pieceAt(Cursor at) {
        Section* s = sections_.at(at.section);
        return s && at.piece >= 0 && std::size_t(at.piece) < kPiecesPerSection ? &s->piece[std::size_t(at.piece)] : nullptr;
    }
    float laneWidth(int lane) const {
        switch (lane) {
        case 2:
        case 3:
            return lanes_[0];
        case 1:
        case 4:
            return lanes_[1];
        case 0:
        case 5:
            return lanes_[2];
        default:
            return 0;
        }
    }
    float partValue(int i) const;

    bool placeItems(xq::Call& c, std::size_t first, std::size_t n);
    void resetItems();
    void shiftItems(int variant);
    void hitItems(float angle, int lane, int mode);
    void moveChain();
    bool bendCommand(Table<Bend>& t, int number, xq::Call& c, std::size_t first, std::size_t n);

    // Course.
    Table<PartShape> parts_; // +0x164
    Table<Section> sections_; // +0x174 (105 sets the size at once)
    Table<Bend> roads_;      // +0x184 (200..204)
    Table<Bend> ways_;       // +0x194 (300..304)
    Table<MaskWay> masks_;   // +0x1a4 (500..506)
    Table<Bend> itemBends_;  // +0x1b4 (406..408)
    float radius_ = 0;                // +0x1c0: the drum's
    float base_ = 0;                  // +0x1c4 (0's second value, not read here)
    std::array<float, 3> lanes_{};    // +0x1c8 .. +0x1d0: inner, middle, outer
    std::array<Cursor, 3> cursor_{};  // +0x1d4: current, next, after
    std::array<Item, kLiveItems> live_{}; // +0x1e0
    std::array<int, 3> firstSlot_{};  // +0xae0 barrels, +0xae4 coins, +0xae8 carrots
    float reach_ = 0;                 // +0xaec: half the angle an item is hit in
    float ahead_ = 0;                 // +0xaf0: where along the drum the horse is
    std::array<float, 3> flyMove_{};  // +0xaf4: a hit barrel's motion per frame
    std::array<float, 3> flyTurn_{};  // +0xb00
    std::array<Node, 3> chain_{};     // +0xb0c
    float length_ = 0;                // +0xbe4: the chain's rest length
    // Score.
    int coins_ = 0;                        // +0xbe8 (a byte)
    std::array<std::int16_t, 6> barrel_{}; // +0xbea (700), by mode
    std::array<std::int16_t, 6> coin_{};   // +0xbf6 (701)
    int score_ = 0;                        // +0xc04
    std::array<std::int8_t, 3> coinDigits_{}; // +0xc08: ones, tens, hundreds
};

float HorseLoop::partValue(int i) const {
    // 0x287afc on parts_00's values (+0x150 + 0x44): value i as a float.
    const auto* o = const_cast<adventure56::Scene&>(game_.scene).object3D(xq::hash("parts_00"));
    if (!o || i < 0 || std::size_t(i) >= o->values.size()) {
        return 0;
    }
    return game_.objectValue(*o, std::size_t(i)).toFloat();
}

bool HorseLoop::placeItems(xq::Call& c, std::size_t first, std::size_t n) {
    // 400 (0x206044) {kind, section, piece, variant, (degrees, lane)...}:
    // items of one kind on a piece. On a straight part the lane lies across
    // the road; on a bent one the item follows the bend's item way (406)
    // and stands across its tangent inside the bend.
    const int kind = c.integer(first);
    if (kind == -1) {
        return false;
    }
    Section* s = section(nameOf(c, first + 1));
    if (!s) {
        return false;
    }
    const int pieceIndex = c.integer(first + 2);
    if (pieceIndex < 0 || std::size_t(pieceIndex) >= kPiecesPerSection) {
        return false; // port safeguard: the game reads past the section
    }
    Piece& piece = s->piece[std::size_t(pieceIndex)];
    const int variant = c.integer(first + 3);
    // 0x206134: the shape by its index in the table (the kinds are 0..6 in
    // table order).
    const PartShape* shape = parts_.at(piece.part);
    const float from = shape ? shape->values[5] : 0.0f;
    const float to = shape ? shape->values[6] : 0.0f;
    const bool straight = piece.part == 0 || piece.part == 5 || piece.part == 6;
    float shift = 0; // s17: barrels stand between two lanes
    const std::size_t pairs = n >= 4 ? (n - 4) / 2 : 0;
    for (std::size_t k = 0; k < pairs; ++k) {
        const float a = c.real(first + 4 + 2 * k) * -kDegree;
        const int lane = c.integer(first + 5 + 2 * k);
        if (piece.count >= kItemsPerPiece) {
            break; // port safeguard: the game writes past the piece
        }
        Item& it = piece.items[piece.count];
        it.kind = kind;
        it.angle = -a;
        it.active = true;
        it.lane = lane;
        if (kind == 0) {
            switch (lane) {
            case 0:
                shift = (lanes_[2] - lanes_[1]) * 0.5f;
                break;
            case 1:
                shift = (lanes_[1] - lanes_[0]) * 0.5f;
                break;
            case 2:
                shift = lanes_[0];
                break;
            case 3:
                shift = (lanes_[0] - lanes_[1]) * 0.5f;
                break;
            case 4:
            case 5:
                shift = (lanes_[1] - lanes_[2]) * 0.5f;
                break;
            default:
                shift = 0;
                break;
            }
        }
        const unsigned u = unsigned(lane);
        float x = 0;
        if (straight) {
            float h = u < 3 ? -kHalfPi : 0.0f;
            if (u - 3 < 3) {
                h += kHalfPi;
            }
            h += -kHalfPi;
            x = std::cos(h) * (laneWidth(lane) - shift);
        } else {
            const Bend* b = bend(itemBends_, piece.part, variant);
            if (!b) {
                continue; // not counted (0x206578)
            }
            const Vec2 p = b->at(span(it.angle, from, to));
            if (it.angle < from || it.angle > to) {
                float h = u < 3 ? -kHalfPi : 0.0f;
                if (u - 3 < 3) {
                    h += kHalfPi;
                }
                h += -kHalfPi;
                x = (laneWidth(lane) - shift) * std::cos(h) - p.x;
            } else {
                const Vec2 q = b->at(span(it.angle - 0.01f, from, to));
                const float dx = p.x - q.x, dy = p.y - q.y;
                float h = dx == 0 && dy == 0 ? 0.0f : std::atan2(dy, dx); // 0x2be1d0
                if (u < 3) {
                    h -= kHalfPi;
                }
                if (u - 3 < 3) {
                    h += kHalfPi;
                }
                x = (laneWidth(lane) - shift) * std::cos(h) - p.x;
            }
        }
        it.position = {x, std::cos(a) * radius_, std::sin(a) * radius_};
        it.rotation = {a, 0, 0};
        ++piece.count;
    }
    return done(c);
}

void HorseLoop::resetItems() {
    // 404 {_, 0} (0x205298): the live slots empty, then the items of the
    // current and the next piece copied in, by kind from their first slots;
    // the next piece's lie further on by parts_00's value 10 (its angle)
    // and aside by its value 7.
    for (auto& l : live_) {
        l = Item{};
    }
    const float nextTurn = partValue(10);
    std::array<int, 3> slot = firstSlot_;
    for (int i = 0; i < 2; ++i) {
        if (cursor_[std::size_t(i)].none()) {
            continue;
        }
        const Piece* piece = pieceAt(cursor_[std::size_t(i)]);
        if (!piece) {
            continue;
        }
        const float turn = i == 1 ? nextTurn : 0.0f;
        const float aside = i == 1 ? partValue(7) : 0.0f;
        for (std::size_t k = 0; k < piece->count; ++k) {
            const Item& src = piece->items[k];
            if (!src.active) {
                continue;
            }
            Item it = src;
            it.slot = i;
            it.hit = false;
            const float a = -it.angle - turn;
            it.position[0] = src.position[0] + aside;
            it.position[1] = std::cos(a) * radius_;
            it.position[2] = std::sin(a) * radius_;
            it.rotation[0] = src.rotation[0] - turn;
            const int which = it.kind == 0 ? 0 : it.kind == 1 ? 1 : it.kind == 2 ? 2 : -1;
            if (which < 0) {
                continue;
            }
            int& s = slot[std::size_t(which)];
            if (s >= 0 && std::size_t(s) < kLiveItems) { // port safeguard
                live_[std::size_t(s)] = it;
            }
            ++s;
        }
    }
}

void HorseLoop::shiftItems(int variant) {
    // 404 {variant, 1} (0x2058c4): the next piece's live items move aside
    // as the road takes a way through (parts_00's value 9 for way 1, 8 for
    // way 2, else 7, less its value 7).
    const int index = variant == 1 ? 9 : variant == 2 ? 8 : 7;
    std::array<int, 3> slot = firstSlot_;
    for (int i = 0; i < 2; ++i) {
        if (cursor_[std::size_t(i)].none()) {
            continue;
        }
        const Piece* piece = pieceAt(cursor_[std::size_t(i)]);
        if (!piece) {
            continue;
        }
        const float by = i == 1 ? partValue(index) + -partValue(7) : 0.0f;
        for (std::size_t k = 0; k < piece->count; ++k) {
            const Item& src = piece->items[k];
            if (!src.active) {
                continue;
            }
            const int which = src.kind == 0 ? 0 : src.kind == 2 ? 2 : src.kind == 1 ? 1 : -1;
            if (which < 0) {
                continue;
            }
            int& s = slot[std::size_t(which)];
            if (s >= 0 && std::size_t(s) < kLiveItems) {
                live_[std::size_t(s)].position[0] += by;
            }
            ++s;
        }
    }
}

void HorseLoop::hitItems(float angle, int lane, int mode) {
    // 405 (0x20562c) {angle, lane [, mode]}: the current piece's live items
    // the horse reaches. A barrel spans its lane and the next; it costs
    // points (700's value for the mode, never below 0) and flies off. A
    // coin or a carrot in the horse's lane is taken: a coin adds 701's
    // value. Each calls its script block.
    auto call = [this](const char* block) { threads[0]->callback(block); };
    for (auto& it : live_) {
        if (!it.active || it.slot != 0) {
            continue;
        }
        if (it.hit) {
            for (int k = 0; k < 3; ++k) {
                it.position[std::size_t(k)] += flyMove_[std::size_t(k)];
            }
            for (int k = 0; k < 3; ++k) {
                it.rotation[std::size_t(k)] += flyTurn_[std::size_t(k)];
            }
            continue;
        }
        const float at = it.angle + ahead_;
        if (at - reach_ > angle || at + reach_ < angle) {
            continue;
        }
        if (it.kind == 0) {
            if (it.lane != lane && it.lane + 1 != lane) {
                continue;
            }
            score_ = std::max(0, score_ + barrel_[std::size_t(std::clamp(mode, 0, 5))]);
            it.hit = true;
            call("Mini_HitBarrel");
        } else if (it.lane == lane) {
            it.hit = true;
            it.active = false;
            if (it.kind == 2) {
                call("Mini_HitMachCarrot");
            } else if (it.kind == 1) {
                coins_ = (coins_ + 1) & 0xff;
                score_ += coin_[std::size_t(std::clamp(mode, 0, 5))];
                const unsigned n = unsigned(coins_);
                coinDigits_[0] = std::int8_t(n % 10);
                coinDigits_[1] = std::int8_t(((n * 0xcd) >> 11) % 10);
                coinDigits_[2] = std::int8_t((n * 41) >> 12);
                call("Mini_HitCoin");
            }
        }
    }
}

void HorseLoop::moveChain() {
    // 602 (0x205a8c): each node in turn is pulled toward its neighbours to
    // the rest length (a node with no neighbour ahead pulls along +y when
    // the two coincide), its velocity damped; a free node takes half the
    // force as a step.
    auto pull = [this](Vec2 to, Vec2 from, Vec2 none) {
        const Vec2 d{to.x - from.x, to.y - from.y};
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len == 0) {
            return none;
        }
        const float k = (len - length_) / len;
        return Vec2{d.x * k, d.y * k};
    };
    for (std::size_t i = 0; i < chain_.size(); ++i) {
        Node& n = chain_[i];
        n.back = i == 0 ? Vec2{} : pull(chain_[i - 1].position, n.position, Vec2{});
        n.forward = i == 2 ? Vec2{} : pull(chain_[i + 1].position, n.position, Vec2{0, 1.0f});
        const Vec2 damp{-n.velocity.x * 1.0f, -n.velocity.y * 1.0f};
        Vec2 f{n.push.x + n.back.x, n.push.y + n.back.y};
        f.x = f.x + n.forward.x;
        f.y = f.y + n.forward.y;
        f.x = f.x + damp.x;
        f.y = f.y + damp.y;
        n.force = f;
        if (!n.held && !n.fixed) {
            n.step = {f.x * 0.5f, f.y * 0.5f};
            n.velocity.x = n.velocity.x + n.step.x;
            n.velocity.y = n.velocity.y + n.step.y;
            n.position.x = n.position.x + n.velocity.x;
            n.position.y = n.position.y + n.velocity.y;
        }
    }
}

bool HorseLoop::bendCommand(Table<Bend>& t, int number, xq::Call& c, std::size_t first, std::size_t n) {
    switch (number) {
    case 0: // 200 / 300 / 406 (0x206e64 / 0x206a78 / 0x207778): the size
        if (n < 1) {
            return false;
        }
        t.reset(c.integer(first));
        if (&t != &itemBends_ && t.capacity == 0) {
            return false;
        }
        return done(c);
    case 1: { // 201 / 301 / 407 {part name, variant, P1, P2, P3}
        if (n < 8) {
            return false;
        }
        Bend b;
        b.name = nameOf(c, first);
        b.part = partKind(b.name);
        b.variant = c.integer(first + 1);
        for (std::size_t k = 0; k < 6; ++k) {
            b.controls[k] = c.real(first + 2 + k);
        }
        t.add(b);
        return done(c);
    }
    case 2: // 202 / 302 / 408: filled
        return t.full() ? done(c) : false;
    case 3: { // 203 / 303 {part, variant, -> P1, P2, P3}
        if (n < 8) {
            return false;
        }
        const Bend* b = bend(t, c.integer(first), c.integer(first + 1));
        if (!b) {
            return false;
        }
        for (std::size_t k = 0; k < 6; ++k) {
            putReal(c, first + 2 + k, b->controls[k]);
        }
        return done(c);
    }
    case 4: { // 204 / 304 {-> x, -> y, part, variant, a, from, to}
        if (n < 7) {
            return false;
        }
        const float tt = span(c.real(first + 4), c.real(first + 5), c.real(first + 6));
        const Bend* b = bend(t, c.integer(first + 2), c.integer(first + 3));
        const Vec2 p = b ? b->at(tt) : Vec2{};
        putReal(c, first, p.x);
        putReal(c, first + 1, p.y);
        return done(c);
    }
    default:
        return false;
    }
}

bool HorseLoop::command(int number, xq::Call& c, std::size_t first) {
    const std::size_t n = operands(c, first);
    if (number >= 200 && number <= 204) {
        return bendCommand(roads_, number - 200, c, first, n);
    }
    if (number >= 300 && number <= 304) {
        return bendCommand(ways_, number - 300, c, first, n);
    }
    switch (number) {
    case 0: // {drum radius, ?, lanes inner/middle/outer, first slots for barrels, carrots, coins, reach°, ahead°, chain length}
        if (n < 11) {
            return false;
        }
        radius_ = c.real(first);
        base_ = c.real(first + 1);
        lanes_ = {c.real(first + 2), c.real(first + 3), c.real(first + 4)};
        firstSlot_[0] = c.integer(first + 5);
        firstSlot_[2] = c.integer(first + 6);
        firstSlot_[1] = c.integer(first + 7);
        reach_ = c.real(first + 8) * kDegree;
        ahead_ = c.real(first + 9) * kDegree;
        length_ = c.real(first + 10);
        return done(c);
    case 3: { // col_obj00's instances where the live items are (hidden: at the origin)
        if (auto* o = game_.scene.object3D(xq::hash("col_obj00")); o && o->merged) {
            auto& g = *o->merged;
            for (std::size_t i = 0; i < kLiveItems && i < g.instances.size(); ++i) {
                const Item& it = live_[i];
                for (std::size_t k = 0; k < 3; ++k) {
                    g.instances[i].position[k] = it.active ? it.position[k] : 0.0f;
                    g.instances[i].rotation[k] = it.active ? it.rotation[k] : 0.0f;
                }
            }
        }
        return done(c);
    }
    case 4: // {-> the course is over (no current piece)}
        if (n < 1) {
            return false;
        }
        putInt(c, first, cursor_[0].none() ? 1 : 0);
        return done(c);
    case 5: // a hit barrel's motion and turn per frame
        if (n < 6) {
            return false;
        }
        flyMove_ = {c.real(first), c.real(first + 1), c.real(first + 2)};
        flyTurn_ = {c.real(first + 3), c.real(first + 4), c.real(first + 5)};
        return done(c);
    case 100: // 0x206fb4: the part shapes' number
        if (n < 1) {
            return false;
        }
        parts_.reset(c.integer(first));
        return parts_.capacity ? done(c) : false;
    case 101: { // {kind, name, seven values}
        if (n < 9) {
            return false;
        }
        PartShape p;
        p.kind = c.integer(first);
        p.name = nameOf(c, first + 1);
        for (std::size_t k = 0; k < 7; ++k) {
            p.values[k] = c.real(first + 2 + k);
        }
        parts_.add(p);
        return done(c);
    }
    case 102:
        return parts_.full() ? done(c) : false;
    case 103: { // {index, -> seven values}
        if (n < 8) {
            return false;
        }
        const PartShape* p = parts_.at(c.integer(first));
        if (!p) {
            return false;
        }
        for (std::size_t k = 0; k < 7; ++k) {
            putReal(c, first + 1 + k, p->values[k]);
        }
        return done(c);
    }
    case 104: { // 0x205ed4 {-> part, -> variant, _, _, _, stay in the section}: the road moves on a piece
        if (n < 6) {
            return false;
        }
        cursor_[0] = cursor_[1];
        cursor_[1] = cursor_[2];
        auto none = [&] {
            putInt(c, first, 0);
            putInt(c, first + 1, 0);
            cursor_[2] = {-1, -1};
            return true;
        };
        const Section* s = sections_.at(cursor_[2].section);
        if (!s) {
            return none();
        }
        // The piece after wraps around its section (0x2bf5b4's remainder).
        if (s->pieces > 0) {
            cursor_[2].piece = std::int16_t(std::int16_t(cursor_[2].piece + 1) % s->pieces);
        }
        if (std::size_t(cursor_[2].section) == sections_.capacity) {
            return none();
        }
        if (c.at(first + 5).truthy() && cursor_[0].section != cursor_[1].section) {
            return none();
        }
        const Piece* p = pieceAt(cursor_[2]);
        if (!p) {
            return none();
        }
        putInt(c, first, p->part);
        putInt(c, first + 1, p->variant);
        return done(c);
    }
    case 105: // 0x207104: the sections' number
        if (n < 1) {
            return false;
        }
        sections_.reset(c.integer(first));
        return done(c);
    case 106: { // 0x2074ec {name, pieces, first [, second]}
        if (n < 3) {
            return false;
        }
        Section s;
        s.name = nameOf(c, first);
        s.pieces = c.integer(first + 1);
        s.first = c.real(first + 2);
        if (n > 3) {
            s.second = c.real(first + 3);
        }
        sections_.add(s);
        return done(c);
    }
    case 107:   // {section, a part kind per piece}
    case 108: { // {section, a variant per piece}
        Section* s = n ? section(nameOf(c, first)) : nullptr;
        if (!s || s->pieces != int(n) - 1) {
            return false;
        }
        for (int k = 0; k < s->pieces && std::size_t(k) < kPiecesPerSection; ++k) {
            (number == 107 ? s->piece[std::size_t(k)].part : s->piece[std::size_t(k)].variant) =
                c.integer(first + 1 + std::size_t(k));
        }
        return done(c);
    }
    case 110: { // {-> three parts, -> three variants}: the road starts at section 0's first pieces
        if (n < 6) {
            return false;
        }
        const Section* s = sections_.at(0);
        if (!s) {
            return false;
        }
        for (std::size_t k = 0; k < 3; ++k) {
            putInt(c, first + k, s->piece[k].part);
            putInt(c, first + 3 + k, s->piece[k].variant);
        }
        cursor_ = {Cursor{0, 0}, Cursor{0, 1}, Cursor{0, 2}};
        return done(c);
    }
    case 111: // {-> the three cursors}
        if (n < 6) {
            return false;
        }
        for (std::size_t k = 0; k < 3; ++k) {
            putInt(c, first + 2 * k, cursor_[k].section);
            putInt(c, first + 2 * k + 1, cursor_[k].piece);
        }
        return done(c);
    case 112: { // {-> section, -> piece}: the piece after starts the next section
        if (n < 2) {
            return false;
        }
        if (cursor_[1].section >= std::int16_t(sections_.rows.size())) {
            putInt(c, first, cursor_[1].section);
            putInt(c, first + 1, int(sections_.rows.size()));
            return true;
        }
        if (!cursor_[2].none()) {
            cursor_[2] = {std::int16_t(cursor_[1].section + 1), 0};
        }
        putInt(c, first, cursor_[2].section);
        putInt(c, first + 1, cursor_[2].piece);
        return done(c);
    }
    case 113: { // {-> part, -> variant, section, piece}
        if (n < 4) {
            return false;
        }
        const Section* s = sections_.at(c.integer(first + 2));
        const int k = c.integer(first + 3);
        if (!s || s->pieces <= k || k < 0 || std::size_t(k) >= kPiecesPerSection) {
            putInt(c, first, 0);
            putInt(c, first + 1, 0);
            return result(c, Value::ofInt(0));
        }
        putInt(c, first, s->piece[std::size_t(k)].part);
        putInt(c, first + 1, s->piece[std::size_t(k)].variant);
        return done(c);
    }
    case 114: { // {-> value, section}: a section's first value (1 when there is none)
        if (n < 2) {
            return false;
        }
        const Section* s = sections_.at(c.integer(first + 1));
        putReal(c, first, s ? s->first : 1.0f);
        return done(c);
    }
    case 115: // {-> the piece after is in the last section}
        if (n < 1) {
            return false;
        }
        putInt(c, first, int(sections_.capacity) - 1 == int(std::uint16_t(cursor_[2].section)) ? 1 : 0);
        return done(c);
    case 116: // {the three cursors}
        if (n < 6) {
            return false;
        }
        for (std::size_t k = 0; k < 3; ++k) {
            cursor_[k] = {std::int16_t(c.integer(first + 2 * k)), std::int16_t(c.integer(first + 2 * k + 1))};
        }
        return done(c);
    case 400:
        return placeItems(c, first, n);
    case 404: // {variant, shift}: the live items afresh, or moved aside
        if (n < 2) {
            return false;
        }
        if (c.at(first + 1).truthy()) {
            shiftItems(c.integer(first));
        } else {
            resetItems();
        }
        return done(c);
    case 405: // {angle, lane [, mode]}
        if (n < 2) {
            return false;
        }
        hitItems(c.real(first), c.integer(first + 1), n < 3 ? 0 : c.integer(first + 2));
        return done(c);
    case 406:
    case 407:
    case 408:
        return bendCommand(itemBends_, number - 406, c, first, n);
    case 500: // 0x2078c4: the masked rider's ways' number
        if (n < 1) {
            return false;
        }
        masks_.reset(c.integer(first));
        return masks_.capacity ? done(c) : false;
    case 501: { // {kind, id, P1, P2, P3}
        if (n < 8) {
            return false;
        }
        MaskWay w;
        w.kind = c.integer(first);
        w.id = c.integer(first + 1);
        for (std::size_t k = 0; k < 6; ++k) {
            w.controls[k] = c.real(first + 2 + k);
        }
        masks_.add(w);
        return done(c);
    }
    case 502:
        return masks_.full() ? done(c) : false;
    case 503: { // {index, -> P1, P2, P3}
        if (n < 7) {
            return false;
        }
        const MaskWay* w = masks_.at(c.integer(first));
        if (!w) {
            return false; // port safeguard (the game reads through null)
        }
        for (std::size_t k = 0; k < 6; ++k) {
            putReal(c, first + 1 + k, w->controls[k]);
        }
        return done(c);
    }
    case 504: { // {kind, -> index, -> id}: one of the kind's ways at random
        if (n < 3) {
            return false;
        }
        const int kind = c.integer(first);
        std::vector<int> found;
        for (std::size_t k = 0; k < masks_.rows.size(); ++k) {
            if (masks_.rows[k].kind == kind) {
                found.push_back(int(k));
            }
        }
        const std::uint32_t r = game_.machine.random(std::uint32_t(found.size()));
        if (found.empty()) {
            putInt(c, first + 1, 0); // port safeguard
            putInt(c, first + 2, 0);
            return done(c);
        }
        const int pick = found[r % found.size()];
        putInt(c, first + 1, pick);
        putInt(c, first + 2, masks_.rows[std::size_t(pick)].id);
        return done(c);
    }
    case 505: { // {-> x, -> y, index, a, from, to}
        if (n < 6) {
            return false;
        }
        const float t = span(c.real(first + 3), c.real(first + 4), c.real(first + 5));
        const MaskWay* w = masks_.at(c.integer(first + 2));
        const Vec2 p = w ? w->at(t) : Vec2{};
        putReal(c, first, p.x);
        putReal(c, first + 1, p.y);
        return done(c);
    }
    case 506: { // {-> the second value of the after piece's section (else the last)}
        if (n < 1) {
            return false;
        }
        const int k = cursor_[2].section < 0 ? int(sections_.capacity) - 1 : cursor_[2].section;
        const Section* s = sections_.at(k);
        putReal(c, first, s ? s->second : 0.0f);
        return done(c);
    }
    case 601: // {x0, _, x1, _, x2, _}: the chain laid out along -y
        if (n < 6) {
            return false;
        }
        chain_[0].position = {c.real(first), 0.0f};
        chain_[1].position = {c.real(first + 2), -length_};
        chain_[2].position = {c.real(first + 4), length_ * -2.0f};
        return done(c);
    case 602:
        moveChain();
        return done(c);
    case 604: { // {node, x}: that node alone is held, at x
        if (n < 2) {
            return false;
        }
        for (auto& node : chain_) {
            node.held = false;
        }
        const int k = c.integer(first);
        if (k < 0 || k > 2) {
            return false; // port safeguard
        }
        chain_[std::size_t(k)].held = true;
        chain_[std::size_t(k)].position.x = c.real(first + 1);
        return done(c);
    }
    case 606: { // {-> x, -> y}: node 1 less node 2
        const Vec2 d{chain_[1].position.x - chain_[2].position.x, chain_[1].position.y - chain_[2].position.y};
        putReal(c, first, d.x);
        putReal(c, first + 1, d.y);
        return done(c);
    }
    case 700:   // a barrel's points by mode
    case 701: { // a coin's
        if (n < (number == 700 ? 6u : 2u)) {
            return false;
        }
        auto& to = number == 700 ? barrel_ : coin_;
        for (std::size_t k = 0; k < 6; ++k) {
            to[k] = std::int16_t(c.integer(first + k));
        }
        return done(c);
    }
    case 702:
        if (n < 1) {
            return false;
        }
        putInt(c, first, coins_);
        return done(c);
    case 703:
        if (n < 1) {
            return false;
        }
        putInt(c, first, score_);
        return done(c);
    case 704: { // {-> the score's five digits, highest first}
        if (n < 5) {
            return false;
        }
        const float s = float(score_);
        putInt(c, first, int(s * 9.99999975e-05f));
        putInt(c, first + 1, int(s * 0.00100000005f) % 10);
        putInt(c, first + 2, int(s * 0.00999999978f) % 10);
        putInt(c, first + 3, int(s * 0.100000001f) % 10);
        putInt(c, first + 4, score_ % 10);
        return done(c);
    }
    case 705: // {-> the coins' digits, highest first}
        if (n < 3) {
            return false;
        }
        putInt(c, first, coinDigits_[2]);
        putInt(c, first + 1, coinDigits_[1]);
        putInt(c, first + 2, coinDigits_[0]);
        return done(c);
    case 706: // the score afresh
        score_ = 0;
        coins_ = 0;
        coinDigits_ = {};
        return done(c);
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<Loop> makeHorseLoop(Story& story, int id, std::vector<Value> values) {
    return std::make_unique<HorseLoop>(story, id, std::move(values));
}

} // namespace picarat::adventure5
