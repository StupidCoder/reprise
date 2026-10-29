// The lily-pad loop (overlay 24). See a4_jump.hpp.
#include "puzzles/a4_jump.hpp"
#include "puzzles/a4_loop_kit.hpp"

namespace picarat::adventure4 {

using namespace kit;

namespace {
// Overlay 24's load address and its strings (read from the game's code).
constexpr std::uint32_t kOverlay24 = 0x02106260;
constexpr std::uint32_t kMessage = 0x021079e0;  // "f_msg"
constexpr std::uint32_t kFrogName = 0x021079e8; // the frog's name for a restart
// Pad looks by size 1..3: sinking, chosen, plain.
constexpr std::uint32_t kSinking[3] = {0x021079f0, 0x021079fc, 0x02107a08};
constexpr std::uint32_t kChosen[3] = {0x02107a14, 0x02107a20, 0x02107a2c};
constexpr std::uint32_t kPlain[3] = {0x02107a38, 0x02107a44, 0x02107a50};
constexpr std::uint32_t kGoalLook = 0x02107a5c;    // the panel on the goal
constexpr std::uint32_t kVerdictName = 0x02107a64; // the verdict variable
constexpr std::uint32_t kPanelNames[4] = {0x02107a70, 0x02107a7c, 0x02107a88, 0x02107a94};
// The panels' offsets (ov24 021078f0, +8/+0xc).
constexpr std::array<std::array<int, 2>, 4> kPanelOffsets{{{0, -1}, {0, 1}, {-1, 0}, {1, 0}}};
constexpr int kNoMoveSound = 0x90;
constexpr std::int32_t kJumpFrames[4] = {0xc000, 0xc000, 0xf000, 0x12000}; // by pad size

#define OP(i) (i + 1)
} // namespace

JumpLoop::JumpLoop(Scene& scene, Services& services, std::span<const Value> arguments)
    : Loop(scene, services, arguments) {
    // 02106454 and the overlay's static initialiser (021077f0).
    for (std::size_t i = 0; i < 4; ++i) {
        panels_[i].name = nameHash(scene.assets.codeText(24, kOverlay24, kPanelNames[i]));
        panels_[i].dx = kPanelOffsets[i][0];
        panels_[i].dy = kPanelOffsets[i][1];
    }
}

bool JumpLoop::command(gss::Call& c, int id) {
    // 021065a8.
    switch (id) {
    case 0:
        return setup(c);
    case 1:
        return addRow(c);
    case 2:
        // 021075fc {frog}.
        if (c.count() - 1 == 0) {
            return false;
        }
        restart(text(c, OP(0)));
        c.result = Value::ofInt(1);
        return true;
    case 3:
        return jump(c);
    case 4:
        judge(c);
        return true;
    case 5:
        // 021074c8.
        placePanels();
        c.result = Value::ofInt(1);
        return true;
    default:
        return Loop::command(c, id);
    }
}

std::string JumpLoop::padName(int x, int y) const {
    // 02106a9c: "x_y" with one digit each (none outside 0..9).
    std::string s;
    if (x < 0 || x > 9) {
        return s;
    }
    s += char('0' + x);
    s += '_';
    if (y >= 0 && y <= 9) {
        s += char('0' + y);
    }
    return s;
}

JumpLoop::Pad* JumpLoop::pad(int x, int y) {
    // 02106698: by name; the original falls back to the last pad compared.
    const std::uint32_t h = nameHash(padName(x, y));
    for (Pad& p : pads_) {
        if (p.name == h) {
            return &p;
        }
    }
    return pads_.empty() ? nullptr : &pads_.back();
}

bool JumpLoop::onBoard(int x, int y) const {
    // 02106ae8.
    return x >= 0 && x < count_[0] && y >= 0 && y < count_[1];
}

int JumpLoop::moveTo(std::uint32_t object, int x, int y, bool jumping) {
    // 021066e4: to the middle of the cell.
    Chara* o = scene_.find(object);
    const int px = s16(x * cellW_ + origin_[0] + cellW_ / 2);
    const int py = s16(y * cellH_ + origin_[1] + cellH_ / 2);
    if (!o) {
        return 0;
    }
    if (!jumping) {
        o->position.set({px * kOne, py * kOne});
        return 0;
    }
    const Frog& f = frogs_.front();
    if (f.x == -1 || f.y == -1) {
        o->position.move({px * kOne, py * kOne}, kJumpFrames[0], 0);
        return 1;
    }
    const Pad* from = pad(f.x, f.y);
    const int size = from ? from->size : 0;
    if (size >= 0 && size <= 3) {
        o->position.move({px * kOne, py * kOne}, kJumpFrames[size], 0);
    }
    return size;
}

void JumpLoop::look(Chara* o, int size, bool sinking, bool chosen) {
    // 02106974.
    if (!o || size < 1 || size > 3) {
        return;
    }
    const auto i = std::size_t(size - 1);
    const std::uint32_t name = sinking ? kSinking[i] : chosen ? kChosen[i] : kPlain[i];
    const std::string n = scene_.assets.codeText(24, kOverlay24, name);
    if (o->animation.resource()) {
        o->animation.play(o->animation.resource()->animation->sequence(n), sinking ? 8u : 0u);
    }
}

bool JumpLoop::setup(gss::Call& c) {
    // 02106e30 {x, y, across, down, cell width, cell height, -, frog, frog
    // x, frog y, goal x, goal y}.
    if (c.count() - 1 <= 11) {
        return false;
    }
    origin_ = {s16(c.integer(OP(0))), s16(c.integer(OP(1)))};
    count_ = {s16(c.integer(OP(2))), s16(c.integer(OP(3)))};
    cellW_ = c.integer(OP(4));
    cellH_ = c.integer(OP(5));
    pads_.clear();
    frogs_.clear();
    const std::string frog = text(c, OP(7));
    Frog f{nameHash(frog), c.integer(OP(8)), c.integer(OP(9)), 0};
    if (const Chara* o = scene_.find(frog)) {
        f.step = valueInt(*o, 4);
    }
    frogs_.push_back(f);
    moveTo(f.name, f.x, f.y, false);
    start_ = {c.integer(OP(8)), c.integer(OP(9))};
    goal_ = {c.integer(OP(10)), c.integer(OP(11))};
    c.result = Value::ofInt(1);
    return true;
}

bool JumpLoop::addRow(gss::Call& c) {
    // 021070dc {size...}: one row of pads "0_row", "1_row", ...
    if (int(c.count() - 1) < count_[0]) {
        return false;
    }
    for (int i = 0; i < count_[0]; ++i) {
        const std::string name = padName(i, rows_);
        const int size = c.integer(OP(std::size_t(i)));
        look(scene_.find(name), size, false, false);
        pads_.push_back({nameHash(name), size, size == -1});
    }
    ++rows_;
    c.result = Value::ofInt(1);
    return true;
}

void JumpLoop::restart(const std::string& frog) {
    // 021074e4.
    if (frogs_.empty()) {
        return;
    }
    Frog& f = frogs_.front();
    if (Chara* o = scene_.find(f.name)) {
        o->setStep(f.step);
    }
    f.x = start_[0];
    f.y = start_[1];
    moveTo(nameHash(frog), f.x, f.y, false);
    for (Pad& p : pads_) {
        p.used = p.size == -1;
        look(scene_.find(p.name), p.size, false, false);
    }
    lastPad_ = false;
    messageFrames_ = 0;
    if (Chara* m = scene_.find(scene_.assets.codeText(24, kOverlay24, kMessage))) {
        m->alpha.set(0);
    }
    placePanels();
    updatePanels();
}

void JumpLoop::placePanels() {
    // 02106d34: the panels where the frog would land.
    if (frogs_.empty()) {
        return;
    }
    const Frog& f = frogs_.front();
    int size = 1;
    if (!padName(f.x, f.y).empty()) {
        const Pad* p = pad(f.x, f.y);
        size = p ? p->size : 0;
    }
    for (Panel& p : panels_) {
        if (Chara* o = scene_.find(p.name)) {
            o->alpha.set(0);
        }
        p.x = f.x + size * p.dx;
        p.y = f.y + size * p.dy;
        moveTo(p.name, p.x, p.y, false);
    }
}

void JumpLoop::updatePanels() {
    // 02106b1c: a panel shows on a pad still free (any pad once the last
    // is left), dimmed on the goal; with none, the "no move" message.
    if (frogs_.empty()) {
        return;
    }
    for (Panel& p : panels_) {
        Chara* o = scene_.find(p.name);
        if (o) {
            o->alpha.set(0);
        }
        if (!onBoard(p.x, p.y)) {
            continue;
        }
        const Pad* target = pad(p.x, p.y);
        if (!target || !(target->size != 0 || lastPad_) || target->used || !o) {
            continue;
        }
        if (p.x != goal_[0] || p.y != goal_[1]) {
            o->alpha.set(kOne);
            look(o, target->size, false, true);
        } else {
            o->alpha.set(kOne / 2);
            play(*o, scene_.assets.codeText(24, kOverlay24, kGoalLook));
        }
    }
    for (const Panel& p : panels_) {
        if (const Chara* o = scene_.find(p.name); o && o->alpha.value() > 0) {
            return;
        }
    }
    const Frog& f = frogs_.front();
    if (f.x == goal_[0] && f.y == goal_[1]) {
        return;
    }
    services_.effect(kNoMoveSound);
    if (Chara* m = scene_.find(scene_.assets.codeText(24, kOverlay24, kMessage))) {
        m->alpha.set(kOne);
    }
}

bool JumpLoop::jump(gss::Call& c) {
    // 02107210 {panel}: the frog jumps onto the panel's pad (if free); the
    // pad it leaves sinks. The result is the size it jumped from.
    if (c.count() - 1 == 0) {
        return false;
    }
    if (restarting_ || frogs_.empty()) {
        return true;
    }
    const std::uint32_t h = nameHash(text(c, OP(0)));
    const Panel* panel = nullptr;
    for (const Panel& p : panels_) {
        if (p.name == h) {
            panel = &p;
            break;
        }
    }
    if (!panel) {
        return true;
    }
    Frog& f = frogs_.front();
    Pad* target = pad(panel->x, panel->y);
    if (!target || target->used) {
        return true;
    }
    const Pad* from = pad(f.x, f.y);
    look(scene_.find(padName(f.x, f.y)), from ? from->size : 0, true, false);
    const int size = moveTo(f.name, panel->x, panel->y, true);
    f.x = panel->x;
    f.y = panel->y;
    if (Pad* now = pad(f.x, f.y)) {
        now->used = true;
    }
    c.result = Value::ofInt(size);
    return true;
}

bool JumpLoop::judge(gss::Call& c) {
    // 02107408: solved on the goal with at most one free pad left.
    if (frogs_.empty()) {
        return false;
    }
    updatePanels();
    int free = 0;
    for (const Pad& p : pads_) {
        if (p.size != 0 && p.size != -1 && !p.used) {
            ++free;
        }
    }
    if (free >= 2) {
        return false;
    }
    lastPad_ = true;
    const Frog& f = frogs_.front();
    if (f.x == goal_[0] && f.y == goal_[1]) {
        services_.data.variables[scene_.assets.codeText(24, kOverlay24, kVerdictName) + "#0"] = 1;
        c.result = Value::ofInt(1);
    }
    return true;
}

void JumpLoop::afterPen() {
    // 021064e4: while the "no move" message shows, a tap after a second puts
    // everything back.
    Chara* m = scene_.find(scene_.assets.codeText(24, kOverlay24, kMessage));
    if (!m) {
        return;
    }
    if (float(m->alpha.value()) / 4096.0f == 1.0f) {
        if (messageFrames_ > 60 && services_.touch.state == 1) {
            restart(scene_.assets.codeText(24, kOverlay24, kFrogName));
            restarting_ = true;
        }
        ++messageFrames_;
        return;
    }
    restarting_ = false;
}

} // namespace picarat::adventure4
