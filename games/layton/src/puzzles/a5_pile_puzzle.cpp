// The fifth adventure's pile puzzles (catalogue type 15, loop 3220;
// puzzles 34, 97, 113, 114, 133): pieces wait in piles beside a grid; the
// player drags them onto grid cells (some may be turned), and the puzzle is
// solved when every cell holds the kind of piece it asks for.
//
// Reconstructed from the loop's command handler 0x1f936c (vtable 0x4015a4,
// constructor 0x37c174; no own input or update) and its helpers: grid
// 0x1f9b78, pieces 0x1fa04c / 0x1fa550, pile counters 0x1fa330 / 0x28abdc,
// the drop 0x1fa918.
//
// How it plays: a piece in its pile is drawn smaller (its pile scale is
// added to the scale, 5207's tween at 0x200); grabbing it (41) makes it
// full size over 5 frames. Dropped (42), it goes to the nearest cell that
// is not blocked (11) - or back to its pile when that is nearer - and the
// script glides it there. The judge (22) marks the cells each placed piece
// covers (its size from the setup, across and down swapped when turned a
// quarter) with its animation's name; solved when every cell's mark is the
// name the script asked for (21; none for the others). Counter objects
// (50) show how many pieces of their kind are left in the piles.
#include "puzzles/a5_special.hpp"
#include <cstdio>

namespace picarat::adventure5 {
namespace {

using adventure56::Object;
using xq::Call;
using xq::Value;

class PilePuzzle final : public SpecialPuzzle {
public:
    explicit PilePuzzle(Game& game) : game_(game) {}

    bool command(int number, Call& c, std::size_t first) override;
    std::string describe() const override;

private:
    struct Piece {
        float scaleX = 0, scaleY = 0; // the pile scale (added to the scale)
        std::uint32_t kind = 0;       // its animation's name
        std::uint32_t object = 0;     // "pile_NN"
        int homeX = 0, homeY = 0;     // its place in the pile
        int toX = 0, toY = 0;         // where it was put
        bool inPile = true;           // +0x18
    };

    struct Cell {
        std::uint32_t mark = 0, wanted = 0;
        int x = 0, y = 0;
        bool blocked = false;
    };

    struct Counter {
        std::uint32_t object = 0;
        std::string name;
    };

    Piece* find(std::uint32_t object, int* index = nullptr) {
        for (std::size_t i = 0; i < pieces_.size(); ++i) {
            if (pieces_[i].object == object) {
                if (index) {
                    *index = int(i);
                }
                return &pieces_[i];
            }
        }
        return nullptr;
    }

    Cell* cell(int x, int y) {
        if (unsigned(x) >= unsigned(width_) || unsigned(y) >= unsigned(height_)) {
            return nullptr;
        }
        return &cells_[std::size_t(y * width_ + x)];
    }

    Object& addPiece(Call& c, std::size_t first, std::size_t cellArg, std::size_t scaleArg);
    void updateCounters();
    Piece* drop(std::uint32_t object);
    bool judge();

    Game& game_;
    int grabbed_ = -1;              // +0x160
    std::vector<Piece> pieces_;     // +0x16c
    std::vector<Cell> cells_;       // +0x178
    std::vector<Counter> counters_; // +0x184
    int originX_ = 0, originY_ = 0; // +0x190 / +0x192
    int width_ = 0, height_ = 0;    // +0x194 / +0x196
    int size_ = 1;                  // +0x198
    int pieceW_ = 3, pieceH_ = 3;   // +0x1a0 / +0x1a4: cells a piece covers
};

// 0x1fa04c / 0x1fa550: "pile_NN" (priority 110, kind 7, values name, 7,
// cell) at its pile place, pile-sized, playing its animation.
Object& PilePuzzle::addPiece(Call& c, std::size_t first, std::size_t cellArg,
                             std::size_t scaleArg) {
    char name[16];
    std::snprintf(name, sizeof name, "pile_%02x", unsigned(pieces_.size()));
    const int cellSize = c.integer(first + cellArg);
    const Value values[] = {Value::ofText(name), Value::ofInt(7), Value::ofInt(cellSize)};
    Object& o = game_.createObject(name, 1, 110, 7, values);
    Piece p;
    const std::string* kind = c.text(first + 3);
    p.kind = kind ? xq::hash(*kind) : 0;
    p.object = o.hash;
    p.homeX = p.toX = std::int16_t(c.integer(first));
    p.homeY = p.toY = std::int16_t(c.integer(first + 1));
    p.scaleX = c.real(first + scaleArg);
    p.scaleY = c.real(first + scaleArg + 1);
    p.inPile = true;
    pieces_.push_back(p);
    o.scaleOffset.set({p.scaleX, p.scaleY});
    o.position.set({float(p.homeX), float(p.homeY), o.position[2]});
    const std::string* resource = c.text(first + 2);
    if (resource) {
        game_.setAnimationSet(o, xq::hash(*resource));
    }
    if (kind) {
        game_.playAnimation(o, Value::ofText(*kind), 0);
    }
    return o;
}

// 0x28abdc: each counter shows how many pieces of its kind are in the
// piles ("%d").
void PilePuzzle::updateCounters() {
    for (const Counter& counter : counters_) {
        Object* o = game_.scene.object(counter.object);
        if (!o) {
            continue;
        }
        const std::uint32_t name = xq::hash(counter.name);
        int n = 0;
        for (const Piece& p : pieces_) {
            const Object* po = game_.scene.object(p.object);
            if (po && p.inPile && po->animation && po->playing >= 0 &&
                xq::hash(po->animation->animations[std::size_t(po->playing)].name) == name) {
                ++n;
            }
        }
        char text[16];
        std::snprintf(text, sizeof text, "%d", n);
        game_.playAnimation(*o, Value::ofText(text), 0);
    }
}

// 0x1fa918: the grabbed piece goes to the nearest free cell, or home when
// home is nearer (then pile-sized again over 5 frames).
PilePuzzle::Piece* PilePuzzle::drop(std::uint32_t object) {
    Piece* p =
        grabbed_ >= 0 && grabbed_ < int(pieces_.size()) ? &pieces_[std::size_t(grabbed_)] : nullptr;
    Object* o = game_.scene.object(object);
    if (!p || p->object != object || !o) {
        return nullptr;
    }
    const int x = std::int16_t(int(o->position[0] + o->offset[0]));
    const int y = std::int16_t(int(o->position[1] + o->offset[1]));
    int best = 1000000;
    // The sixth adventure (0x2d03a4) takes a cell only when the piece's
    // footprint from it (as the top-left; across and down unturned) has no
    // blocked cell: cell index + i + j x width, indexes past the last cell
    // not checked (so a footprint may wrap into the next row); the last cell
    // is never a candidate.
    const bool sixth = game_.edition == Edition::Sixth;
    const int candidates = sixth ? int(cells_.size()) - 1 : int(cells_.size());
    for (int k = 0; k < candidates; ++k) {
        const Cell& c = cells_[std::size_t(k)];
        if (sixth) {
            bool free = true;
            for (int i = 0; i < pieceW_ && free; ++i) {
                for (int j = 0; j < pieceH_ && free; ++j) {
                    const int idx = k + i + j * width_;
                    free = idx >= int(cells_.size()) || !cells_[std::size_t(idx)].blocked;
                }
            }
            if (!free) {
                continue;
            }
        } else if (c.blocked) {
            continue;
        }
        const int dx = std::int16_t(x - c.x), dy = std::int16_t(y - c.y);
        const int d = dy * dy + dx * dx;
        if (d < best) {
            p->toX = c.x;
            p->toY = c.y;
            best = d;
        }
    }
    const int dx = std::int16_t(x - p->homeX), dy = std::int16_t(y - p->homeY);
    if (dy * dy + dx * dx < best) {
        p->toX = p->homeX;
        p->toY = p->homeY;
        p->inPile = true;
        o->scaleOffset.start({p->scaleX, p->scaleY}, 5, 2);
    }
    return p;
}

// Command 22: the cells marked by the placed pieces; solved when every
// mark is the wanted one.
bool PilePuzzle::judge() {
    for (Cell& c : cells_) {
        c.mark = 0;
    }
    for (Object* o : game_.scene.screenList(1)) {
        if (o->kind != 7) {
            continue;
        }
        const Piece* p = find(o->hash);
        if (!p || p->inPile) {
            continue;
        }
        const int half = size_ >> 1;
        const int cx = std::int16_t((std::int16_t(p->toX - originX_) + half) / size_);
        const int cy = std::int16_t((std::int16_t(p->toY - originY_) + half) / size_);
        int across = pieceW_, down = pieceH_;
        if (o->stepIndex == 1 || o->stepIndex == 3) {
            std::swap(across, down);
        }
        // The sixth adventure's judge (0x2cf2c0) marks from the piece's cell
        // rightward and downward (the cell is the footprint's top-left); the
        // fifth's centres the footprint on it.
        const bool sixth = game_.edition == Edition::Sixth;
        const int startX = sixth ? 0 : (across & 1) == 0 ? 1 - (across >> 1) : -(across >> 1);
        const int startY = sixth ? 0 : (down & 1) == 0 ? 1 - (down >> 1) : -(down >> 1);
        for (int j = 0; j < down; ++j) {
            for (int i = 0; i < across; ++i) {
                if (Cell* c = cell(std::int16_t(startX + i + cx), std::int16_t(startY + j + cy))) {
                    c->mark = p->kind;
                }
            }
        }
    }
    for (const Cell& c : cells_) {
        if (c.mark != c.wanted) {
            return false;
        }
    }
    return true;
}

std::string PilePuzzle::describe() const {
    std::string out;
    char line[128];
    std::snprintf(line, sizeof line, "grid %d %d origin %d %d size %d piece %d %d\n", width_,
                  height_, originX_, originY_, size_, pieceW_, pieceH_);
    out += line;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const Cell& cl = cells_[std::size_t(y * width_ + x)];
            std::snprintf(line, sizeof line, "cell %d %d wanted %u blocked %d\n", x, y, cl.wanted,
                          int(cl.blocked));
            out += line;
        }
    }
    for (const Piece& p : pieces_) {
        const Object* o = game_.scene.object(p.object);
        std::snprintf(line, sizeof line,
                      "piece %s kind %u home %d %d steps %d scale %g %g pilescale %g %g\n",
                      o ? o->name.c_str() : "?", p.kind, p.homeX, p.homeY, o ? o->steps : 0,
                      o ? o->scale[0] : 0.0f, o ? o->scale[1] : 0.0f, p.scaleX, p.scaleY);
        out += line;
        if (o) {
            for (const auto& a : o->areas) {
                std::snprintf(line, sizeof line, "  area %d %g %g %g %g\n", a.kind, a.x, a.y, a.w,
                              a.h);
                out += line;
            }
        }
    }
    return out;
}

bool PilePuzzle::command(int number, Call& c, std::size_t first) {
    const std::size_t count = c.count() - first;
    auto integer = [&](std::size_t i) {
        return c.integer(first + i);
    };
    auto name = [&](std::size_t i) {
        const Value& v = c.at(first + i);
        return v.isText() ? xq::hash(v.text) : v.toUint();
    };
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    auto rects = [&](auto apply) {
        const int x0 = std::int16_t(integer(0)), y0 = std::int16_t(integer(1));
        const int w = integer(2), h = integer(3);
        for (int j = 0; j < h; ++j) {
            for (int i = 0; i < w; ++i) {
                if (Cell* cl = cell(std::int16_t(x0 + i), std::int16_t(y0 + j))) {
                    apply(*cl);
                }
            }
        }
    };
    switch (number) {
    case 0:
        // Every piece back in its pile.
        for (Piece& p : pieces_) {
            p.inPile = true;
            if (Object* o = game_.scene.object(p.object)) {
                o->position.set({float(p.homeX), float(p.homeY), o->position[2]});
                o->scaleOffset.set({p.scaleX, p.scaleY});
                o->stepIndex = 0;
                if (o->steps > 0) {
                    o->stepAngle.set({0});
                }
            }
        }
        updateCounters();
        grabbed_ = -1;
        return result(1);
    case 10: {
        // 0x1f9b78: cells across, down; first cell x, y; cell size [;
        // the cells a piece covers across, down].
        if (count < 5) {
            return false;
        }
        width_ = std::int16_t(integer(0));
        height_ = std::int16_t(integer(1));
        originX_ = std::int16_t(integer(2));
        originY_ = std::int16_t(integer(3));
        size_ = integer(4);
        if (count > 5) {
            pieceW_ = integer(5);
        }
        if (count > 6) {
            pieceH_ = integer(6);
        }
        cells_.assign(std::size_t(std::max(0, width_ * height_)), Cell{});
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                Cell* cl = cell(x, y);
                cl->x = std::int16_t(originX_ + x * size_);
                cl->y = std::int16_t(originY_ + y * size_);
            }
        }
        return result(1);
    }
    case 11:
        // Blocked cells (x, y, across, down).
        if (count < 4) {
            return false;
        }
        rects([](Cell& cl) { cl.blocked = true; });
        return result(1);
    case 20:
        game_.setVariable(names::verdict, 1);
        return result(1);
    case 21: {
        // The kind wanted in cells (x, y, across, down, name).
        if (count < 5) {
            return false;
        }
        const std::uint32_t wanted = name(4);
        rects([wanted](Cell& cl) { cl.wanted = wanted; });
        return result(1);
    }
    case 22:
        return result(judge() ? 1 : 0);
    case 40: {
        // A piece: x, y, resource, animation, normal / slide blocks, cell
        // size, pile scale x, y.
        if (count < 9) {
            return false;
        }
        Object& o = addPiece(c, first, 6, 7);
        const float half = float(integer(6) >> 1), full = float(integer(6));
        o.areas.push_back({2, -half, -half, full, full});
        const std::string* normal = c.text(first + 4);
        const std::string* slide = c.text(first + 5);
        const std::string handlers[] = {normal ? *normal : std::string(),
                                        slide ? *slide : std::string(), std::string()};
        game_.setHandlers(o, 3, handlers);
        c.result = Value::ofInt(1);
        return true;
    }
    case 41: {
        // Grab: full size over 5 frames, out of the pile.
        if (count == 0) {
            return false;
        }
        bool ok = false;
        int index = -1;
        Piece* p = grabbed_ < 0 ? find(name(0), &index) : nullptr;
        if (p) {
            if (Object* o = game_.scene.object(p->object)) {
                o->scaleOffset.start({0, 0}, 5, 2);
                p->inPile = false;
                grabbed_ = index;
                ok = true;
            }
        }
        updateCounters();
        return result(ok ? 1 : 0);
    }
    case 42: {
        // Drop: where it goes into two registers.
        if (count < 3) {
            return false;
        }
        const bool had = grabbed_ >= 0;
        if (had) {
            if (Piece* p = drop(name(0))) {
                if (Value* r = c.reg(first + 1)) {
                    *r = Value::ofInt(p->toX);
                }
                if (Value* r = c.reg(first + 2)) {
                    *r = Value::ofInt(p->toY);
                }
            }
        }
        grabbed_ = -1;
        updateCounters();
        return result(had ? 1 : 0);
    }
    case 43: {
        // A turnable piece: x, y, resource, animation, normal / slide /
        // turn blocks, its outline (x, y, width, height), steps, pile
        // scale x, y. The whole outline turns it (kind 3), its middle
        // half (at least as wide as it is thin) slides it (kind 2).
        if (count < 14) {
            return false;
        }
        Object& o = addPiece(c, first, 11, 12);
        o.steps = std::max(0, integer(11));
        o.stepFrames = o.steps ? 5.0f : 0.0f;
        o.stepIndex = 0;
        const float rx = c.real(first + 7), ry = c.real(first + 8);
        const float rw = c.real(first + 9), rh = c.real(first + 10);
        float x2 = rx, y2 = ry, w2 = rw, h2 = rh;
        if (rw < rh) {
            h2 = rh * 0.5f;
            if (h2 < rw) {
                h2 = rw;
            }
            y2 += h2 * 0.5f;
        }
        if (rw > rh) {
            w2 = rw * 0.5f;
            if (w2 < rh) {
                w2 = rh;
            }
            x2 += w2 * 0.5f;
        }
        o.areas.push_back({3, rx, ry, rw, rh});
        o.areas.push_back({2, x2, y2, w2, h2});
        const std::string* normal = c.text(first + 4);
        const std::string* slide = c.text(first + 5);
        const std::string* turn = c.text(first + 6);
        const std::string handlers[] = {normal ? *normal : std::string(),
                                        slide ? *slide : std::string(),
                                        turn ? *turn : std::string()};
        game_.setHandlers(o, 3, handlers);
        c.result = Value::ofInt(1);
        return true;
    }
    case 50: {
        // A counter (x, y, resource, name; kind 6).
        if (count < 4) {
            return false;
        }
        const std::string* resource = c.text(first + 2);
        const std::string* label = c.text(first + 3);
        if (!label) {
            return false;
        }
        Object& o = game_.createObject(*label, 1, 110, 6, {});
        o.position.set(
            {float(std::int16_t(integer(0))), float(std::int16_t(integer(1))), o.position[2]});
        counters_.push_back({o.hash, *label});
        if (resource) {
            game_.setAnimationSet(o, xq::hash(*resource));
        }
        updateCounters();
        return result(1);
    }
    default:
        return false;
    }
}

} // namespace

std::unique_ptr<SpecialPuzzle> makePilePuzzle(Game& game) {
    return std::make_unique<PilePuzzle>(game);
}

} // namespace picarat::adventure5
