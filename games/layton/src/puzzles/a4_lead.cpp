// The "lead the cat" loop (overlay 25). See a4_lead.hpp.
#include "puzzles/a4_lead.hpp"
#include "puzzles/a4_loop_kit.hpp"
#include <cstdio>

namespace picarat::adventure4 {

using namespace kit;

namespace {
// Overlay 25's load address and its strings (read from the game's code).
constexpr std::uint32_t kOverlay25 = 0x02106260;
constexpr std::uint32_t kLockedFlag = 0x02107ee0;  // the "puzzle operation off" flag
constexpr std::uint32_t kCounter = 0x02107ef4;     // "counter"
constexpr std::uint32_t kCountFormat = 0x02107efc; // "%d"
constexpr std::uint32_t kVerdictName = 0x02107f00; // the verdict variable
constexpr std::uint32_t kMessage = 0x02107f0c;     // "f_msg"
constexpr std::uint32_t kFishFormat = 0x02107f14;  // "fish_%d"
constexpr int kStepSound = 0x303, kEatSound = 0x304, kNoMoveSound = 0x90;
constexpr int kStepFrames = 11; // frames a cell, and between footsteps
constexpr int kRestartFrames = 90;

#define OP(i) (i + 1)
} // namespace

LeadLoop::LeadLoop(Scene& scene, Services& services, std::span<const Value> arguments)
    : Loop(scene, services, arguments) {}

std::string LeadLoop::text(std::uint32_t address) const {
    return scene_.assets.codeText(25, kOverlay25, address);
}

bool LeadLoop::command(gss::Call& c, int id) {
    // 021069c8.
    switch (id) {
    case 0:
        return setup(c);
    case 1:
        return addWalls(c);
    case 2:
        // 02107a30.
        restart();
        c.result = Value::ofInt(1);
        return true;
    case 3:
        return addAllowed(c);
    default:
        return Loop::command(c, id);
    }
}

bool LeadLoop::setup(gss::Call& c) {
    // 0210723c {x, y, across, down, cell width, cell height, cat, start x,
    // start y, goal x, goal y, fish animation, fish look, eaten look, fish,
    // sound}.
    if (c.count() - 1 < 16) {
        c.result = Value::ofInt(0);
        return false;
    }
    origin_ = {s16(c.integer(OP(0))), s16(c.integer(OP(1)))};
    count_ = {s16(c.integer(OP(2))), s16(c.integer(OP(3)))};
    cell_ = {s16(c.integer(OP(4))), s16(c.integer(OP(5)))};
    start_ = {s16(c.integer(OP(7))), s16(c.integer(OP(8)))};
    goal_ = {s16(c.integer(OP(9))), s16(c.integer(OP(10)))};
    fishCount_ = c.integer(OP(14));
    placeSound_ = c.integer(OP(15));
    cells_.assign(std::size_t(count_[0] * count_[1]), {});
    wallsAcross_.assign(std::size_t((count_[0] - 1) * count_[1]), false);
    wallsDown_.assign(std::size_t(count_[0] * (count_[1] - 1)), false);
    fish_.clear();
    cat_ = nameHash(kit::text(c, OP(6)));
    at_ = start_;
    moveCat(start_[0], start_[1], false);
    if (const Chara* cat = scene_.find(cat_)) {
        for (std::size_t i = 0; i < looks_.size(); ++i) {
            looks_[i] = valueText(*cat, 4 + i);
        }
    }
    for (int y = 0; y < count_[1]; ++y) {
        for (int x = 0; x < count_[0]; ++x) {
            const std::string name = std::string(1, char('0' + x)) + "_" + char('0' + y);
            cells_[std::size_t(y * count_[0] + x)] = {
                nameHash(name), x * cell_[0] + origin_[0] + cell_[0] / 2,
                y * cell_[1] + origin_[1] + cell_[1] / 2, false, true};
        }
    }
    for (int i = 0; i < fishCount_; ++i) {
        char name[32];
        std::snprintf(name, sizeof name, text(kFishFormat).c_str(), i);
        if (Chara* f = scene_.create(name, 15, {})) {
            attach(scene_, *f, kit::text(c, OP(11)), 5);
        }
        fish_.push_back({nameHash(name), kit::text(c, OP(12)), kit::text(c, OP(13))});
    }
    return true;
}

bool LeadLoop::addWalls(gss::Call& c) {
    // 02107928 {wall..., kind}: a row of walls; kind 0 between columns,
    // else between rows.
    const std::size_t n = c.count() - 1;
    if (n == 0) {
        c.result = Value::ofInt(0);
        return false;
    }
    const std::size_t per = n - 1;
    const bool across = c.integer(OP(per)) == 0;
    auto& list = across ? wallsAcross_ : wallsDown_;
    int& row = across ? acrossRows_ : downRows_;
    for (std::size_t i = 0; i < per; ++i) {
        const std::size_t at = std::size_t(row) * per + i;
        if (at < list.size()) {
            list[at] = c.integer(OP(i)) != 0;
        }
    }
    ++row;
    return true;
}

bool LeadLoop::addAllowed(gss::Call& c) {
    // 02107b60 {allowed...}: a row of cells where fish may go.
    const std::size_t n = c.count() - 1;
    if (n == 0) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t at = std::size_t(allowedRows_) * n + i;
        if (at < cells_.size()) {
            cells_[at].allowed = c.integer(OP(i)) != 0;
        }
    }
    ++allowedRows_;
    c.result = Value::ofInt(1);
    return true;
}

void LeadLoop::showCount() {
    if (Chara* counter = scene_.find(text(kCounter))) {
        char digits[16];
        std::snprintf(digits, sizeof digits, text(kCountFormat).c_str(), fishCount_ - used_);
        counter->animation.showText(digits, 6, 0, 0);
    }
}

void LeadLoop::restart() {
    // 02107a54.
    used_ = 0;
    showCount();
    if (Chara* m = scene_.find(text(kMessage))) {
        m->alpha.set(0);
    }
    frames_ = 0;
    for (Cell& cell : cells_) {
        cell.fish = false;
    }
    for (const Fish& f : fish_) {
        if (Chara* o = scene_.find(f.name)) {
            o->alpha.set(0);
        }
    }
    moveCat(start_[0], start_[1], false);
    if (Chara* cat = scene_.find(cat_)) {
        play(*cat, looks_[0]);
        setMirror(*cat, 0);
    }
    arrived_ = false;
    busy_ = false;
    at_ = start_;
}

void LeadLoop::moveCat(int x, int y, bool walking) {
    // 02106b40: to the cell's middle; walking takes 11 frames a cell.
    Chara* cat = scene_.find(cat_);
    const int px = s16(s16(x * cell_[0]) + origin_[0] + cell_[0] / 2);
    const int py = s16(s16(y * cell_[1]) + origin_[1] + cell_[1] / 2);
    if (!cat) {
        return;
    }
    if (!walking) {
        cat->position.set({px * kOne, py * kOne});
        return;
    }
    const int dx = std::abs(at_[0] - x), dy = std::abs(at_[1] - y);
    int cells = 0;
    if (unsigned(direction_) < 2) {
        cells = dy;
    }
    if (direction_ == 2 || direction_ == 3) {
        cells = dx;
    }
    walk_ = cells * kStepFrames;
    const auto frames = std::int32_t(4096.0f * (float(kStepFrames) * float(cells)));
    cat->position.move({px * kOne, py * kOne}, frames, 0);
}

bool LeadLoop::clear(int direction, int x, int y, int tx, int ty) const {
    // 02106e98: no wall between the cat and the cell (stepping one cell at a
    // time until it is reached).
    for (int i = 0;; ++i) {
        int cx = x, cy = y;
        bool wall = false;
        switch (direction) {
        case 0:
            cy = y - i - 1;
            wall = wallsDown_[std::size_t(cy * count_[0] + x)];
            break;
        case 1:
            wall = wallsDown_[std::size_t((y + i) * count_[0] + x)];
            cy = y + i + 1;
            break;
        case 2:
            cx = x - i - 1;
            wall = wallsAcross_[std::size_t(y * (count_[0] - 1) + cx)];
            break;
        default:
            wall = wallsAcross_[std::size_t(y * (count_[0] - 1) + x + i)];
            cx = x + i + 1;
            break;
        }
        if (wall) {
            return false;
        }
        if (cx == tx && cy == ty) {
            return true;
        }
    }
}

bool LeadLoop::turnToward(int x, int y) {
    // 02106cd4.
    Chara* cat = scene_.find(cat_);
    if (x != at_[0] && y != at_[1]) {
        return false;
    }
    if (!cat) {
        return false;
    }
    if (x == at_[0] && y - at_[1] < 0) {
        if (!clear(0, at_[0], at_[1], x, y)) {
            return false;
        }
        play(*cat, looks_[1]);
        direction_ = 0;
    }
    if (x == at_[0] && y - at_[1] > 0) {
        if (!clear(1, at_[0], at_[1], x, y)) {
            return false;
        }
        play(*cat, looks_[2]);
        direction_ = 1;
    }
    if (x - at_[0] < 0 && y == at_[1]) {
        if (!clear(2, at_[0], at_[1], x, y)) {
            return false;
        }
        play(*cat, looks_[3]);
        setMirror(*cat, 0);
        direction_ = 2;
    }
    if (x - at_[0] > 0 && y == at_[1]) {
        if (!clear(3, at_[0], at_[1], x, y)) {
            return false;
        }
        play(*cat, looks_[3]);
        setMirror(*cat, 1);
        direction_ = 3;
    }
    return true;
}

bool LeadLoop::place(float px, float py) {
    // 02107028.
    const int x = int(std::int32_t(px - float(origin_[0]))) / cell_[0];
    const int y = int(std::int32_t(py - float(origin_[1]))) / cell_[1];
    if (!(x < count_[0] && y < count_[1])) {
        return false;
    }
    if (at_[0] == x && at_[1] == y) {
        return false;
    }
    if (fishCount_ <= used_ || x < 0 || y < 0) {
        return false;
    }
    Fish& f = fish_[std::size_t(used_)];
    Cell& cell = cells_[std::size_t(y * count_[0] + x)];
    if (!cell.allowed) {
        return false;
    }
    Chara* o = scene_.find(f.name);
    services_.effect(placeSound_);
    if (cell.fish || !o) {
        return false;
    }
    play(*o, f.placed);
    o->position.set({cell.px * kOne, cell.py * kOne});
    o->alpha.set(kOne);
    if (!turnToward(x, y)) {
        return false;
    }
    f.x = x;
    f.y = y;
    cell.fish = true;
    moveCat(x, y, true);
    at_ = {x, y};
    walking_ = true;
    ++used_;
    return true;
}

void LeadLoop::afterUpdate() {
    // 02106974: the walk counts down a frame at a time.
    if (walk_ > 0) {
        walkBefore_ = walk_;
        walk_ -= 1;
    }
}

void LeadLoop::afterPen() {
    // 02106504.
    if (services_.data.flags[text(kLockedFlag) + "#0"] != 0) {
        return;
    }
    Chara* cat = scene_.find(cat_);
    if (!cat) {
        return;
    }
    if (sliding(*cat)) {
        // A footstep every 11 frames of the walk.
        if (walk_ / kStepFrames != walkBefore_ / kStepFrames || walk_ <= 0) {
            services_.effect(kStepSound);
        }
        return;
    }
    ++frames_;
    if (!busy_ && services_.touch.state == 1) {
        if (arrived_ && walking_) {
            return;
        }
        if (fishCount_ == used_ && frames_ > kRestartFrames) {
            restart();
            return;
        }
        const auto& t = services_.touch;
        const float x = float(t.startX), y = float(t.startY);
        const bool inside = x >= float(origin_[0]) && y >= float(origin_[1]) &&
                            x < float(cell_[0] * count_[0] + origin_[0]) &&
                            y < float(cell_[1] * count_[1] + origin_[1]);
        if (!inside || !place(x, y)) {
            return;
        }
        busy_ = true;
        if (used_ >= 2) {
            if (Chara* previous = scene_.find(fish_[std::size_t(used_ - 2)].name)) {
                previous->alpha.set(kOne);
            }
        }
        showCount();
    }
    if (used_ < 1) {
        return;
    }
    const Fish& f = fish_[std::size_t(used_ - 1)];
    if (walking_ && !sliding(*cat) && f.x == at_[0] && f.y == at_[1]) {
        eaten_ = scene_.find(f.name);
        if (eaten_) {
            play(*eaten_, f.eaten);
        }
        arrived_ = true;
    }
    if (!arrived_) {
        return;
    }
    // The cat eats facing the way it walked (the eating sound on the frame
    // it still shows a walking look), then stands again.
    if (const auto* seq = cat->animation.sequence();
        seq && (seq->name == looks_[1] || seq->name == looks_[2] || seq->name == looks_[3])) {
        services_.effect(kEatSound);
    }
    if (direction_ == 0) {
        cat->animation.play(cat->animation.resource()
                                ? cat->animation.resource()->animation->sequence(looks_[4])
                                : nullptr,
                            8);
    } else if (direction_ == 1) {
        cat->animation.play(cat->animation.resource()
                                ? cat->animation.resource()->animation->sequence(looks_[5])
                                : nullptr,
                            8);
    } else {
        cat->animation.play(cat->animation.resource()
                                ? cat->animation.resource()->animation->sequence(looks_[6])
                                : nullptr,
                            8);
    }
    if (!cat->animation.done()) {
        return;
    }
    play(*cat, looks_[0]);
    setMirror(*cat, 0);
    arrived_ = false;
    walking_ = false;
    if (eaten_) {
        eaten_->alpha.set(0);
    }
    busy_ = false;
    if (used_ == fishCount_ && at_ == goal_) {
        services_.data.variables[text(kVerdictName) + "#0"] = 1;
        return;
    }
    if (Chara* m = scene_.find(text(kMessage))) {
        if (fishCount_ != used_) {
            m->alpha.set(0);
            frames_ = 0;
            return;
        }
        services_.effect(kNoMoveSound);
        m->alpha.set(kOne);
    }
}

} // namespace picarat::adventure4
