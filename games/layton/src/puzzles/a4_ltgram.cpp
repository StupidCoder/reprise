// The shape-fitting loop (overlay 20). See a4_ltgram.hpp.
#include "puzzles/a4_ltgram.hpp"
#include "puzzles/a4_loop_kit.hpp"

namespace picarat::adventure4 {

using namespace kit;

namespace {
// Overlay 20's load address and its strings (read from the game's code).
constexpr std::uint32_t kOverlay20 = 0x02106260;
constexpr std::uint32_t kMoveIcon = 0x02107ca4;   // "lt4_icon_move"
constexpr std::uint32_t kMenuAnim = 0x02107cb4;   // the icons' animation
constexpr std::uint32_t kMoveLook = 0x02107cc0;   // "move"
constexpr std::uint32_t kTurnIcon = 0x02107cc8;   // "lt4_icon_rot"
constexpr std::uint32_t kTurnLook = 0x02107cd8;   // "turn"
constexpr std::uint32_t kFlipButton = 0x02107ce0; // "lt4_ltgrm_turn"
constexpr std::uint32_t kFlipBlock = 0x02107cf0;  // its touch block
constexpr std::uint32_t kFlipCorner[4] = {0x02107d10, 0x02107d20, 0x02107d08, 0x02107d18};
constexpr std::uint32_t kLockedFlag = 0x02107d28;  // the "puzzle operation off" flag
constexpr std::uint32_t kVerdictName = 0x02107d3c; // the verdict variable
constexpr int kPieceKind = 20, kFixedKind = 19, kButtonKind = 15;
constexpr int kWallArea = 3;
constexpr std::int32_t kTurnFrames = 0x5000; // 5 frames a quarter turn / a snap

#define OP(i) (i + 1)
} // namespace

LtgramLoop::LtgramLoop(Scene& scene, Services& services, std::span<const Value> arguments)
    : Loop(scene, services, arguments) {
    reset();
}

void LtgramLoop::reset() {
    // 0210641c.
    origin_ = count_ = {};
    size_ = 1;
    targets_.clear();
    least_ = {};
    solved_ = false;
    before_.clear();
    beforeDone_ = true;
    looks_.clear();
    targetArea_ = -1;
}

bool LtgramLoop::command(gss::Call& c, int id) {
    switch (id) {
    case 0:
        return board(c);
    case 1:
        return restart(c);
    case 2:
        return addTarget(c);
    case 3:
        return grid(c);
    case 4:
        return moved(c);
    case 5:
        return flip(c);
    case 6:
        return extent(c);
    case 7:
        return addBefore(c);
    case 8:
        return addLook(c);
    default:
        return Loop::command(c, id);
    }
}

bool LtgramLoop::board(gss::Call& c) {
    // 021066a4 {x, y, across, down, size, flip button, its animation,
    // [target area id]}.
    const std::size_t n = c.count() - 1;
    if (n <= 5) {
        return false;
    }
    origin_ = {s16(c.integer(OP(0))), s16(c.integer(OP(1)))};
    count_ = {s16(c.integer(OP(2))), s16(c.integer(OP(3)))};
    size_ = c.integer(OP(4));
    const auto code = [&](std::uint32_t a) {
        return scene_.assets.codeText(20, kOverlay20, a);
    };
    if (Chara* icon = scene_.create(code(kMoveIcon), -1, {})) {
        attach(scene_, *icon, code(kMenuAnim), 6);
        play(*icon, code(kMoveLook));
        icon->alpha.set(0);
    }
    if (Chara* icon = scene_.create(code(kTurnIcon), -1, {})) {
        attach(scene_, *icon, code(kMenuAnim), 6);
        play(*icon, code(kTurnLook));
        icon->alpha.set(0);
        icon->setSteps(4, kTurnFrames);
        icon->setStep(0);
    }
    if (c.integer(OP(5)) != 0) {
        // The flip button: a touch button calling the flip block, hidden
        // until a piece is chosen (5112 with only a touch block, then off).
        const std::string name = code(kFlipButton);
        const Value values[] = {Value::ofText(name), Value::ofInt(kButtonKind), Value{}};
        if (Chara* b = scene_.create(name, kButtonKind, values)) {
            attach(scene_, *b, text(c, OP(6)), 6);
            b->touchKind = 1;
            b->callbacks = {};
            b->callbacks[3] = code(kFlipBlock);
            b->enterState(0);
            b->setSwitch(Switches::Master, false);
        }
    }
    if (n > 7) {
        targetArea_ = c.integer(OP(7));
    }
    c.result = Value::ofInt(1);
    return true;
}

bool LtgramLoop::restart(gss::Call& c) {
    // 02106954: every piece (and fixed piece) back to its look, the place
    // its sequence names, its first turn, full alpha and no mirroring.
    for (Chara* o : scene_.objects()) {
        if (!active(*o) || (o->kind != kPieceKind && o->kind != kFixedKind)) {
            continue;
        }
        scene_.place(*o, o->screen, 5);
        play(*o, valueText(*o, 2));
        const auto* seq = o->animation.sequence();
        if (!seq) {
            return false;
        }
        o->position.set({seq->x * kOne, seq->y * kOne});
        o->setStep(valueInt(*o, 6));
        o->alpha.set(kOne);
        o->scale.set({kOne, kOne});
        o->kind = kPieceKind;
        if (o->thread.values.size() > 1) {
            o->thread.values[1] = Value::ofInt(kPieceKind);
        }
    }
    if (!before_.empty()) {
        beforeDone_ = false;
    }
    c.result = Value::ofInt(1);
    return true;
}

bool LtgramLoop::addTarget(gss::Call& c) {
    // 02106b40 {x, y, across, down, [least]}.
    const std::size_t n = c.count() - 1;
    if (n < 4 || targets_.size() >= 16) {
        return false;
    }
    targets_.push_back({s16(c.integer(OP(0))), s16(c.integer(OP(1))), s16(c.integer(OP(2))),
                        s16(c.integer(OP(3)))});
    if (n == 5) {
        least_[targets_.size() - 1] = c.integer(OP(4));
    }
    c.result = Value::ofInt(1);
    return true;
}

bool LtgramLoop::addBefore(gss::Call& c) {
    // 02106c24 {x, y, across, down}.
    if (c.count() - 1 < 4 || before_.size() >= 16) {
        return false;
    }
    before_.push_back({s16(c.integer(OP(0))), s16(c.integer(OP(1))), s16(c.integer(OP(2))),
                       s16(c.integer(OP(3)))});
    beforeDone_ = false;
    c.result = Value::ofInt(1);
    return true;
}

bool LtgramLoop::addLook(gss::Call& c) {
    // 02107404 {piece, sequence}.
    if (c.count() - 1 <= 1 || looks_.size() >= 16) {
        return false;
    }
    looks_.push_back({nameHash(text(c, OP(0))), nameHash(text(c, OP(1)))});
    c.result = Value::ofInt(1);
    return true;
}

bool LtgramLoop::grid(gss::Call& c) {
    // 02106cec {piece}: the board's cells, snaps of 5 frames.
    if (c.count() - 1 == 0) {
        return false;
    }
    Chara* o = scene_.find(text(c, OP(0)));
    if (!o) {
        return false;
    }
    o->position.setGrid({{}, origin_, count_, size_, kTurnFrames});
    c.result = Value::ofInt(1);
    return true;
}

bool LtgramLoop::moved(gss::Call& c) {
    // 02106d74 {piece or none}: judges, then puts the flip button at the
    // piece's corner that faces the board's middle.
    if (c.count() - 1 == 0) {
        return false;
    }
    Chara* piece = scene_.find(text(c, OP(0)));
    if (piece) {
        judge();
    }
    if (solved_) {
        return true;
    }
    if (Chara* button = scene_.find(scene_.assets.codeText(20, kOverlay20, kFlipButton))) {
        if (button->thread.values.size() < 2) {
            return false;
        }
        button->thread.values[1] = c.at(OP(0));
        if (!piece) {
            button->setSwitch(Switches::Master, false);
        } else {
            button->setSwitch(Switches::Master, true);
            const auto cell = piece->position.grid().cell;
            const int cx = s16(s16(cell[0] * size_) + origin_[0]);
            const int cy = s16(s16(cell[1] * size_) + origin_[1]);
            const int midX = s16(s16(s16(count_[0] / 2) * size_) + origin_[0]);
            const int midY = s16(s16(s16(count_[1] / 2) * size_) + origin_[1]);
            const auto b0 = piece->bounds();
            const Box b =
                turnBox({b0.x1, b0.y1, b0.x2, b0.y2}, mirrorOf(*piece), quarterOf(*piece));
            int corner, x, y;
            if (cx < midX) {
                corner = cy < midY ? 2 : 0;
                x = b.x2 + cx;
                y = cy < midY ? b.y2 + cy : b.y1 + cy;
            } else {
                corner = cy < midY ? 3 : 1;
                x = b.x1 + cx;
                y = cy < midY ? b.y2 + cy : b.y1 + cy;
            }
            play(*button, scene_.assets.codeText(20, kOverlay20, kFlipCorner[corner]));
            button->position.set({s16(x) * kOne, s16(y) * kOne});
        }
    }
    c.result = Value::ofInt(1);
    return true;
}

bool LtgramLoop::flip(gss::Call& c) {
    // 0210717c {piece}: mirrored across the axis that is horizontal in its
    // current turn; the judge's answer.
    if (c.count() - 1 == 0) {
        return false;
    }
    Chara* o = scene_.find(text(c, OP(0)));
    if (!o) {
        return false;
    }
    unsigned m = mirrorOf(*o);
    m ^= (quarterOf(*o) & 1) == 0 ? 1u : 2u;
    setMirror(*o, m);
    c.result = Value::ofInt(judge() ? 1 : 0);
    return true;
}

bool LtgramLoop::extent(gss::Call& c) {
    // 02107218 {piece, left, top, right, bottom}: how far the piece's box
    // reaches from its position in each direction, after turning and
    // mirroring.
    if (c.count() - 1 < 5) {
        return false;
    }
    Chara* o = scene_.find(text(c, OP(0)));
    if (!o) {
        return false;
    }
    unsigned q = unsigned(quarterOf(*o));
    const auto s = o->scale.value();
    const int sx = s[0] / kOne;
    int sy = s[1] / kOne;
    const auto b0 = o->bounds();
    const int x1 = b0.x1, y1 = b0.y1;
    const int w = s16(b0.x2 - x1), h = s16(b0.y2 - y1);
    if (sx < 0) {
        q = (q + 2) & 3;
        sy = -sy;
    }
    std::array<int, 4> e{};
    if (sy < 1) {
        switch (q) {
        case 0:
            e = {-x1, y1 + h, x1 + w, -y1};
            break;
        case 1:
            e = {-y1, -x1, y1 + h, x1 + w};
            break;
        case 2:
            e = {x1 + w, -y1, -x1, y1 + h};
            break;
        case 3:
            e = {y1 + h, x1 + w, -y1, -x1};
            break;
        }
    } else {
        switch (q) {
        case 0:
            e = {-x1, -y1, x1 + w, y1 + h};
            break;
        case 1:
            e = {y1 + h, -x1, -y1, x1 + w};
            break;
        case 2:
            e = {x1 + w, y1 + h, -x1, -y1};
            break;
        case 3:
            e = {-y1, x1 + w, y1 + h, -x1};
            break;
        }
    }
    for (std::size_t i = 0; i < 4; ++i) {
        if (Value* r = c.reg(OP(1 + i))) {
            *r = Value::ofInt(e[i]);
        }
    }
    c.result = Value::ofInt(1);
    return true;
}

bool LtgramLoop::judge() {
    // 02107480.
    if (solved_) {
        return true;
    }
    auto anyMoving = [&] {
        // 020d1c08: a piece still on its way.
        for (const Chara* o : scene_.objects()) {
            if (o->kind == kPieceKind && active(*o) && moving(*o)) {
                return true;
            }
        }
        return false;
    };
    if (!beforeDone_ && !anyMoving() && covered(before_, kWallArea)) {
        for (const Look& l : looks_) {
            Chara* o = scene_.find(l.object);
            if (!o) {
                continue;
            }
            scene_.place(*o, o->screen, 4);
            play(*o, l.sequence);
            o->kind = kFixedKind;
            if (o->thread.values.size() > 1) {
                o->thread.values[1] = Value::ofInt(kFixedKind);
            }
            o->setSwitch(Switches::Touch, false);
        }
        beforeDone_ = true;
    }
    if (beforeDone_ && !targets_.empty() && !anyMoving() && covered(targets_, targetArea_)) {
        solved_ = true;
        services_.data.flags[scene_.assets.codeText(20, kOverlay20, kLockedFlag) + "#0"] = 1;
        if (Chara* b = scene_.find(scene_.assets.codeText(20, kOverlay20, kFlipButton))) {
            b->setSwitch(Switches::Master, false);
        }
        services_.data.variables[scene_.assets.codeText(20, kOverlay20, kVerdictName) + "#0"] = 1;
    }
    return solved_;
}

bool LtgramLoop::covered(const std::vector<Region>& regions, int areaId) {
    // 02107640. Every cell of every region must lie in a piece box (cell
    // corners, boxes inclusive on their top left); while "before" regions
    // are done, a region with a count only needs that many, and a region
    // without one that fails ends the check. When judging the "before"
    // regions, no piece box may lie outside them either.
    const auto boxes = obstacles(scene_, 0, kPieceKind, areaId, nullptr);
    auto inside = [](const Box& b, int x, int y) {
        return b.x1 <= x && b.y1 <= y && x < b.x2 && y < b.y2;
    };
    auto cellX = [&](const Region& r, int i) {
        return s16(s16((i + r.x) * size_) + origin_[0]);
    };
    auto cellY = [&](const Region& r, int j) {
        return s16(s16((j + r.y) * size_) + origin_[1]);
    };
    bool ok = true;
    for (std::size_t k = 0; k < regions.size(); ++k) {
        const Region& r = regions[k];
        int hits = 0;
        bool stop = false;
        for (int j = 0; j < r.down && !stop; ++j) {
            for (int i = 0; i < r.across; ++i) {
                const int x = cellX(r, i), y = cellY(r, j);
                bool hit = false;
                for (const Box& b : boxes) {
                    if (inside(b, x, y)) {
                        hit = true;
                        break;
                    }
                }
                if (hit) {
                    ++hits;
                } else {
                    ok = false;
                }
            }
            if (beforeDone_ && !ok && least_[k] == 0) {
                stop = true;
            }
        }
        if (stop) {
            break;
        }
        if (beforeDone_ && least_[k] > 0 && least_[k] <= hits) {
            ok = true;
        }
    }
    if (!beforeDone_ && !before_.empty()) {
        return ok;
    }
    if (areaId == kWallArea && ok) {
        const auto all = obstacles(scene_, 0, kPieceKind, -1, nullptr);
        for (const Box& b : all) {
            bool onRegion = false;
            for (const Region& r : regions) {
                for (int j = 0; j < r.down && !onRegion; ++j) {
                    for (int i = 0; i < r.across; ++i) {
                        if (inside(b, cellX(r, i), cellY(r, j))) {
                            onRegion = true;
                            break;
                        }
                    }
                }
                if (onRegion) {
                    break;
                }
            }
            if (!onRegion) {
                return false;
            }
        }
    }
    return ok;
}

} // namespace picarat::adventure4
