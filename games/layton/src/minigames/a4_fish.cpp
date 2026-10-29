// The fish tank. See a4_fish.hpp.
#include "minigames/a4_fish.hpp"
#include "puzzles/a4_loop_kit.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace picarat::adventure4 {

namespace {
constexpr const char* kTanks = "res/sub/lt4_sub_fish_intr.cb"; // 0210b524
constexpr const char* kPieces = "anm_a_obj";                   // 0210b7c4
constexpr const char* kFish = "obj_fish";                      // 0210b7d0

// The kinds of piece the code tests (the rest only matter to the script):
// 10..15 the fish's start, 20 a coin, 30..39 what the fish bounces off,
// 33 a bubble and 34 a hyper bubble among them (the rush breaks through
// both).
constexpr int kCoin = 20, kBubble = 33, kHyperBubble = 34;

bool bouncesOff(int kind) {
    return kind / 10 == 3; // 02105bdc
}

bool breaksInRush(int kind) {
    return kind == kBubble || kind == kHyperBubble; // 02105bb0
}

// The ARM9's sine and cosine table (0202c904: 4096 pairs of 4.12 values),
// which the water's shimmer reads.
constexpr std::uint32_t kSinCos = 0x0202c904;

// A cell's centre in 20.12 pixels: 18 pixels a cell from (2, 2)
// (02107818, 02108518, 02108e94).
std::int32_t centre(int c) {
    return std::int32_t(4096.0f * (float(c) * 18.0f + 2.0f + 9.0f));
}

// The cell holding a pixel (20.12; 02109ec8, 0210a1a0).
int cellOf(std::int32_t p) {
    return int((((float(p) / 4096.0f - 2.0f) - 9.0f) + 9.0f) / 18.0f);
}

std::int32_t toFixed(float f) {
    return std::int32_t(4096.0f * f); // 0202973c then 02029318
}

// 02106b68 (clamp to 0..1), then the 3D engine's 5-bit alpha.
unsigned alpha5(std::int32_t a) {
    a = std::clamp(a, 0, kOne);
    return unsigned((a * 0x1ffff) >> 24) & 0xff;
}

std::string format(const char* f, int a) {
    char out[32];
    std::snprintf(out, sizeof out, f, a);
    return out;
}

// 020db6b8: an object's normal block (not a button), started at once.
void setNormal(Chara& o, const std::string& block) {
    o.touchKind = 0;
    o.callbacks[0] = block;
    o.callbacks[1].clear();
    o.callbacks[2].clear();
    o.enterState(0);
}
} // namespace

// ------------------------------------------------------------ the menu

bool FishMenuLoop::command(gss::Call& c, int op) {
    switch (op) {
    case 1: { // 021056b4: the tanks' numbers (5410 how many, 5411 a tank)
        tanks_.clear();
        const auto list = game_.assets.data(kTanks);
        for (const auto& cmd : list.commands) {
            auto integer = [&](unsigned i) {
                return i < cmd.count ? std::int32_t(list.operands[cmd.first + i].value) : 0;
            };
            if (cmd.opcode == 5411 && cmd.count > 2) { // 0210544c
                tanks_.push_back({integer(3), integer(2), integer(4)});
            }
        }
        c.result = Value::ofInt(1);
        return true;
    }
    case 2: { // 021058d8 {tank, -> bubbles, -> coins, -> seconds}
        if (c.count() < 5) {
            c.result = Value::ofInt(0);
            return true;
        }
        const int i = c.integer(1);
        if (i < 0 || i >= int(tanks_.size())) {
            c.result = Value::ofInt(0);
            return true;
        }
        const Numbers& n = tanks_[std::size_t(i)];
        const int values[3] = {n.bubbles, n.coins, n.seconds};
        for (std::size_t k = 0; k < 3; ++k) {
            if (Value* r = c.reg(2 + k)) {
                *r = Value::ofInt(values[k]);
            }
        }
        c.result = Value::ofInt(1);
        return true;
    }
    default:
        return ScriptLoop::command(c, op);
    }
}

// ------------------------------------------------------------ the tank

FishLoop::FishLoop(Game& game, int id, std::span<const Value> values)
    : ScriptLoop(game, id, values) {
    // 02106098: the tank is the loop's first value.
    stage_ = std::int16_t(values.empty() ? 0 : values[0].toInt());
}

FishLoop::~FishLoop() {
    scene_.loopSprites.clear();
    scene_.loopMeshes.clear();
}

const FishLoop::Tank* FishLoop::tank() const {
    return stage_ >= 0 && stage_ < int(tanks_.size()) ? &tanks_[std::size_t(stage_)] : nullptr;
}

Chara* FishLoop::fish() const {
    return scene_.find(kFish);
}

const FishLoop::Kind* FishLoop::kindOf(int kind) const {
    const auto it = kinds_.find(kind);
    return it == kinds_.end() ? nullptr : &it->second;
}

bool FishLoop::inside(Cell c) const {
    const Tank* t = tank();
    return t && c[0] >= 0 && c[0] < t->width && c[1] >= 0 && c[1] < t->height;
}

const FishLoop::Piece* FishLoop::pieceAt(Cell c) const {
    if (c[0] < 0 || c[0] >= kColumns || c[1] < 0 || c[1] >= kRows) {
        return nullptr;
    }
    const int i = grid_[std::size_t(c[0])][std::size_t(c[1])];
    return i >= 0 && i < int(pieces_.size()) ? &pieces_[std::size_t(i)] : nullptr;
}

FishLoop::Piece* FishLoop::pieceAt(Cell c) {
    return const_cast<Piece*>(std::as_const(*this).pieceAt(c));
}

int FishLoop::rushLeft() const {
    return rush_ ? std::max(3 - rushBounces_, 0) : 0;
}

std::vector<Value> FishLoop::hitArguments() const {
    // 021091c8: the rush's bounces left, and whether it began just now
    // (the speed is the rush's and was not before).
    const bool begun = speed_ == numbers_[4] && lastSpeed_ != numbers_[4];
    return {Value::ofInt(rushLeft()), Value::ofInt(begun ? 1 : 0)};
}

void FishLoop::loadTanks() {
    // 021094d0: res/sub/lt4_sub_fish_intr.cb with handlers 5410 (how many),
    // 5411 a tank's numbers {width, height, coins, bubbles, seconds}
    // (02105eb0), 5413 a row of 14 kinds (02105f64), 5412 the tank's end
    // (02105f4c).
    tanks_.clear();
    const auto list = game_.assets.data(kTanks);
    Tank t;
    int row = 0;
    bool open = false;
    for (const auto& cmd : list.commands) {
        auto integer = [&](unsigned i) {
            return i < cmd.count ? std::int32_t(list.operands[cmd.first + i].value) : 0;
        };
        switch (cmd.opcode) {
        case 5411:
            if (cmd.count > 2) {
                t = Tank{};
                t.width = std::min(integer(0), kColumns);
                t.height = std::min(integer(1), kRows);
                t.coins = integer(2);
                t.bubbles = integer(3);
                t.seconds = integer(4);
                row = 0;
                open = true;
            }
            break;
        case 5413:
            if (open && cmd.count > 3 && row < kRows) {
                for (unsigned x = 0; x < unsigned(kColumns); ++x) {
                    t.cells[x][std::size_t(row)] = integer(x);
                }
                ++row;
            }
            break;
        case 5412:
            if (open) {
                tanks_.push_back(t);
                open = false;
            }
            break;
        default:
            break;
        }
    }
}

// ------------------------------------------------------------ setting up

void FishLoop::clearTank() {
    // 02107528: the fish forgotten, the grids emptied, the pieces gone.
    fish_ = -1;
    for (auto& column : grid_) {
        column.fill(-1);
    }
    initial_ = saved_ = grid_;
    pieces_.clear();
    shimmer_ = nullptr;
}

bool FishLoop::addPiece(int kind, Cell cell) {
    // 02107604: a piece of a kind on a cell, showing the kind's sequence.
    const Kind* k = kindOf(kind);
    if (!k) {
        return false;
    }
    Piece p;
    p.index = int(pieces_.size());
    p.cell = cell;
    p.kind = k;
    if (const AnimationResource* a = scene_.animation(kPieces)) {
        p.player.attach(a);
        p.player.play(a->animation->sequence(k->sequence), 0);
    }
    p.shown = !k->sequence.empty();
    grid_[std::size_t(cell[0])][std::size_t(cell[1])] = p.index;
    pieces_.push_back(std::move(p));
    return true;
}

bool FishLoop::setUp(const std::string& picture) {
    // 02109804 (2400 10): a piece for every cell of the tank, the fish, the
    // water's picture, then the path plotted.
    const Tank* t = tank();
    if (!t) {
        return false;
    }
    clearTank();
    for (int y = 0; y < t->height; ++y) {
        for (int x = 0; x < t->width; ++x) {
            const int kind = t->cells[std::size_t(x)][std::size_t(y)];
            if (kind != 0 && !addPiece(kind, {x, y})) {
                clearTank();
                return false;
            }
        }
    }
    initial_ = grid_;
    if (!placeFish()) {
        clearTank();
        return false;
    }
    shimmer_ = scene_.image(picture);
    state_ = 1;
    plot();
    // Five players of "dot" from the pieces' pack are set up here too
    // (+0x43e0); nothing in the overlay draws them.
    return true;
}

bool FishLoop::placeFish() {
    // 02107818: the fish at its start, setting off in the start's direction.
    if (!tank()) {
        return false;
    }
    auto start = std::find_if(pieces_.begin(), pieces_.end(),
                              [](const Piece& p) { return p.kind && p.kind->kind / 10 == 1; });
    if (start == pieces_.end()) {
        return false;
    }
    Piece& p = *start;
    switch (p.kind->kind) {
    case 10:
        direction_ = {1, 0};
        break;
    case 11:
        direction_ = {1, -1};
        break;
    case 12:
        direction_ = {1, 1};
        break;
    case 13:
        direction_ = {-1, 0};
        break;
    case 14:
        direction_ = {-1, -1};
        break;
    case 15:
        direction_ = {-1, 1};
        break;
    default:
        break;
    }
    const Point at{centre(p.cell[0]), centre(p.cell[1])};
    Chara* f = fish();
    if (!f) {
        const std::vector<Value> values{Value::ofText(kFish), Value::ofInt(15)};
        f = scene_.create(kFish, 15, values);
    }
    const AnimationResource* pack = scene_.animation(kPieces);
    if (f) {
        setNormal(*f, "Obj_Nrm"); // 0210b7dc
        f->position.set(at);
        f->offset.set({0, 0});
        if (pack) {
            kit::attach(scene_, *f, kPieces, 5);
            kit::play(*f, p.kind->sequence);
        }
    }
    // The fish's own piece is not drawn (its player is emptied).
    p.player = AnimationPlayer{};
    fish_ = p.index;
    lastDirection_ = direction_;
    target_ = p.cell;
    from_ = to_ = at;
    progress_ = 0;
    speed_ = lastSpeed_ = 1.0f;
    burst_.set(0);
    bouncing_ = false;
    if (f) {
        kit::setMirror(*f, direction_[0] < 0 ? 1 : 0); // 020abe90
    }
    const Animation::Sequence* look = pack ? pack->animation->sequence(p.kind->sequence) : nullptr;
    for (Trail& tr : trail_) {
        tr = {at, direction_, look};
    }
    trailAlpha_.set(0);
    rushBegun_ = false;
    rushBounces_ = 0;
    bounceCount_ = 0;
    rush_ = false;
    return true;
}

// ------------------------------------------------------------ the path

bool FishLoop::plot() {
    // 021089c8: the fish's path from its start to its 63rd bounce, swum
    // on the grid and recorded; the grid and the pieces are put back after.
    framesPerCell_ = 0;
    elapsed_ = 0; // 02108bd8
    eaten_ = 0;
    warned_ = false;
    stepIndex_ = 0;
    hitIndex_ = 0;
    hits_.clear();
    steps_.clear();
    saved_ = grid_;
    if (fish_ < 0) {
        return false;
    }
    // A port safeguard: a fish shut in on every side would bounce for ever.
    int bounces = 1;
    for (int guard = 0; bounces < 64 && guard < 100000; ++guard) {
        if (plotStep()) {
            ++bounces;
        }
        enterTarget(false);
    }
    grid_ = saved_;
    if (const Tank* t = tank()) {
        for (int y = 0; y < t->height; ++y) {
            for (int x = 0; x < t->width; ++x) {
                const int i = grid_[std::size_t(x)][std::size_t(y)];
                if (i >= 0 && i < int(pieces_.size())) {
                    pieces_[std::size_t(i)].cell = {x, y};
                }
            }
        }
    }
    placeFish();
    return true;
}

bool FishLoop::plotStep() {
    // 02107d1c: the next cell. A wall or a piece the fish bounces off
    // beside it (across, then up or down) turns that half of the direction
    // round; with both sides free, such a piece on the cell itself turns
    // both. Each bounce is a hit; the rush breaks through bubbles instead.
    const Tank& t = *tank();
    lastDirection_ = direction_;
    const Cell at = fishPiece().cell;
    target_ = {at[0] + direction_[0], at[1] + direction_[1]};
    bool bounced = false;
    int bounces = 0;
    for (int guard = 0; guard < 16; ++guard) {
        const Piece* o = nullptr;
        const bool outX = target_[0] < 0 || target_[0] >= t.width;
        o = outX ? nullptr : pieceAt(Cell{target_[0], at[1]});
        if (outX || (o && bouncesOff(o->kind->kind))) {
            if (!(o && breaksInRush(o->kind->kind) && rush_)) {
                ++bounces;
                bounced = true;
                target_[0] = at[0] - direction_[0];
                direction_[0] = -direction_[0];
            }
        } else {
            const bool outY = target_[1] < 0 || target_[1] >= t.height;
            o = outY ? nullptr : pieceAt(Cell{at[0], target_[1]});
            if (outY || (o && bouncesOff(o->kind->kind))) {
                if (!(o && breaksInRush(o->kind->kind) && rush_)) {
                    ++bounces;
                    bounced = true;
                    target_[1] = at[1] - direction_[1];
                    direction_[1] = -direction_[1];
                }
            } else {
                o = pieceAt(target_);
                if (!o || !bouncesOff(o->kind->kind)) {
                    break;
                }
                if (!(breaksInRush(o->kind->kind) && rush_)) {
                    target_ = {at[0] - direction_[0], at[1] - direction_[1]};
                    direction_ = {-direction_[0], -direction_[1]};
                    bounced = true;
                    ++bounces;
                }
            }
        }
        hit(o ? o->index : -1, false);
    }
    if (bounced) {
        startBounce();
    }
    // At most 512 steps are kept in the original (+0x13e0); later steps of
    // a run would read past them. The port keeps them all.
    steps_.push_back({bounced, bounces, at, target_, direction_});
    ++stepIndex_;
    return bounced;
}

void FishLoop::startBounce() {
    // The bounce (02107d1c, 02108294): the cell starts over with the fish
    // on its centre; outside the rush a burst of speed that dies away,
    // in it the next slower speed.
    progress_ = 0;
    if (Chara* f = fish()) {
        f->position.set(to_);
    }
    if (!rush_) {
        burst_.set(toFixed(numbers_[0]));
        burst_.move(0, toFixed(numbers_[1]), 0);
    } else {
        burst_.set(0);
        speed_ = numbers_[std::size_t(2 + std::max(2 - rushBounces_, 0))];
    }
    ++bounceCount_;
}

bool FishLoop::replayStep() {
    // 02108294: the next plotted step, the hits met on the way first.
    while (hitIndex_ < hits_.size() && hits_[hitIndex_].step <= stepIndex_) {
        hit(hits_[hitIndex_].piece, true);
        ++hitIndex_;
    }
    Step s;
    if (stepIndex_ < int(steps_.size())) {
        s = steps_[std::size_t(stepIndex_)];
    } else {
        // Past the plot (a port safeguard): straight on.
        const Cell at = fishPiece().cell;
        s = {false, 0, at, {at[0] + direction_[0], at[1] + direction_[1]}, direction_};
    }
    target_ = s.to;
    lastDirection_ = direction_;
    direction_ = s.direction;
    if (s.bounced) {
        if (Chara* f = fish()) {
            // 0210b7e4: told whether it was a corner and the rush's
            // bounces left.
            const std::vector<Value> args{Value::ofInt(s.bounces > 1 ? 1 : 0),
                                          Value::ofInt(rushLeft())};
            f->thread.callback("Fish_Reflect", args);
        }
        startBounce();
    }
    ++stepIndex_;
    return s.bounced;
}

void FishLoop::enterTarget(bool live) {
    // 02108518: the fish moves onto its target; a coin there is eaten.
    if (Piece* o = pieceAt(target_)) {
        if (o->kind && o->kind->kind == kCoin) {
            ++eaten_;
        }
        if (live) {
            hitShow(*o);
        }
    }
    from_ = to_;
    to_ = {centre(target_[0]), centre(target_[1])};
    const Cell at = fishPiece().cell;
    grid_[std::size_t(target_[0])][std::size_t(target_[1])] =
        grid_[std::size_t(at[0])][std::size_t(at[1])];
    grid_[std::size_t(at[0])][std::size_t(at[1])] = -1;
    fishPiece().cell = target_;
}

bool FishLoop::hit(int piece, bool live) {
    // 02108bf8: a bounce off a piece or a wall (-1), or a bubble the rush
    // breaks. While plotting, the hit is recorded for the run.
    const Piece* o =
        piece >= 0 && piece < int(pieces_.size()) ? &pieces_[std::size_t(piece)] : nullptr;
    if (o && !o->kind) {
        return false;
    }
    if (!live) {
        hits_.push_back({piece, stepIndex_});
    }
    lastSpeed_ = speed_;
    const int kind = o ? o->kind->kind : 0;
    if (!(o && breaksInRush(kind) && rush_)) {
        rushBounces_ = (rushBounces_ + 1) & 0xff;
    }
    if (rush_ && rushBounces_ > 2) {
        // The third bounce ends the rush.
        speed_ = 1.0f;
        rush_ = false;
        trailAlpha_.move(0, 0xc000, 0);
        if (live && kind != kHyperBubble) {
            if (Chara* f = fish()) {
                f->thread.callback("Fish_StrongFinish"); // 0210b7f4
            }
        }
    }
    if (live && !o) {
        callback(0, "Main_OutHit", hitArguments()); // 0210b808
        return false;
    }
    if (breaksInRush(kind)) {
        grid_[std::size_t(o->cell[0])][std::size_t(o->cell[1])] = -1;
    }
    if (kind == kHyperBubble && !rush_) {
        speed_ = numbers_[4];
        rush_ = true;
        rushBounces_ = 0;
        rushBegun_ = true;
    }
    if (live) {
        if (Chara* f = fish(); f && !o->kind->fishBlock.empty()) {
            f->thread.callback(o->kind->fishBlock);
        }
        hitShow(*o);
    }
    return true;
}

void FishLoop::hitShow(const Piece& p) {
    // 02108e94: an object "obj_NN" on the piece's cell runs the kind's hit
    // block (the coin's, a bounce's sound, the bubble bursting) and then
    // deletes itself ("AutoDeleteObj_Nrm" as its normal block).
    if (!p.kind || p.kind->hitBlock.empty()) {
        return;
    }
    const std::string name = format("obj_%02d", p.index); // 0210b814
    const std::vector<Value> values{Value::ofText(name), Value::ofInt(15), Value::ofInt(p.index)};
    Chara* o = scene_.create(name, 15, values);
    if (!o) {
        return;
    }
    o->setSwitch(Switches::Update, true);
    o->setSwitch(Switches::Touch, false);
    o->setSwitch(Switches::Draw, false);
    o->position.set({centre(p.cell[0]), centre(p.cell[1])});
    if (kit::attach(scene_, *o, kPieces, 4)) {
        kit::play(*o, p.kind->sequence);
    }
    setNormal(*o, "AutoDeleteObj_Nrm"); // 0210b820
    o->thread.callback(p.kind->hitBlock, hitArguments());
}

// ------------------------------------------------------------ the run

void FishLoop::swim(std::int32_t elapsed) {
    // 02106c0c: a frame of the run. The fish crosses a cell in the run's
    // frames per cell times the step's length over its speed, plus what
    // the burst adds; on each cell it takes the next plotted step. The run
    // ends with the coins eaten (Main_GameClear) or the time up
    // (Main_GameOver).
    const float t = float(elapsed) / 4096.0f;
    elapsed_ += t;
    const Tank* tk = tank();
    Chara* f = fish();
    if (!tk || !f || fish_ < 0) {
        return;
    }
    if (speed_ <= 0.0f) {
        return;
    }
    const float dx = float(direction_[0]), dy = float(direction_[1]);
    const float length = std::sqrt(dx * dx + dy * dy) / speed_;
    if (!bouncing_) {
        // The burst adds its value each frame (the trapezoid of this and
        // the last frame's value over the frame's time).
        const float before = float(burst_.value()) / 4096.0f;
        burst_.update(elapsed);
        if (before == 0.0f && float(burst_.value()) / 4096.0f == 0.0f) {
            burst_.set(kOne);
            burst_.set(0);
        }
        const float a = float(burst_.value()) / 4096.0f;
        const float b = float(burst_.previous()) / 4096.0f;
        progress_ = progress_ + ((t + a) + (t - 1.0f) * (0.5f * (b + a)));
    }
    // An after-image of the fish every four frames.
    if (trailTime_ >= 4.0f) {
        trailTime_ = trailTime_ - 4.0f;
        for (std::size_t i = trail_.size() - 1; i > 0; --i) {
            trail_[i] = trail_[i - 1];
        }
        const auto p = f->position.value(), o = f->offset.value();
        trail_[0] = {{p[0] + o[0], p[1] + o[1]}, direction_, f->animation.sequence()};
    }
    trailTime_ = trailTime_ + t;
    if (progress_ >= framesPerCell_ * length) {
        if (eaten_ >= tk->coins) {
            callback(0, "Main_GameClear", {}); // 0210b794
            state_ = 3;
            trailAlpha_.move(0, 0xc000, 0);
            return;
        }
        float e = elapsed_;
        if (!(t <= 1.0f)) {
            e = e - 1.0f;
        }
        if (e / 60.0f > float(tk->seconds)) {
            callback(0, "Main_GameOver", {}); // 0210b7a4
            state_ = 3;
            trailAlpha_.move(0, 0xc000, 0);
            return;
        }
        progress_ = progress_ - framesPerCell_ * length;
        bouncing_ = replayStep();
        if (bouncing_ && float(lastElapsed_) / 4096.0f > 1.0f) {
            elapsed_ = elapsed_ + (1.0f - t);
        }
    }
    if (elapsed_ / 60.0f > float(tk->seconds) - 0.1f && !warned_) {
        callback(0, "Main_PreSmlEnd", {}); // 0210b7b4
        warned_ = true;
    }
    if (target_ != fishPiece().cell && !(bouncing_ && f->thread.running())) {
        // Into the next cell (after a bounce, once the fish's bounce
        // animation is over).
        enterTarget(true);
        if (bouncing_) {
            progress_ = progress_ + (t - 1.0f);
        }
        if (rushBegun_ && rush_) {
            trailAlpha_.move(kOne, kOne, 0);
            const auto p = f->position.value(), o = f->offset.value();
            for (Trail& tr : trail_) {
                tr = {{p[0] + o[0], p[1] + o[1]}, direction_, f->animation.sequence()};
            }
            rushBegun_ = false;
        }
        if (eaten_ >= tk->coins) {
            callback(0, "Main_PreSmlEnd", {});
            warned_ = true;
        }
        bouncing_ = false;
    }
    if (bouncing_) {
        return;
    }
    // Along the cell.
    const float k = progress_ / (framesPerCell_ * length);
    const std::int32_t x =
        std::int32_t(float(from_[0]) * (1.0f - k)) + std::int32_t(float(to_[0]) * k);
    const std::int32_t y =
        std::int32_t(float(from_[1]) * (1.0f - k)) + std::int32_t(float(to_[1]) * k);
    f->position.set({x, y});
}

void FishLoop::afterUpdate() {
    // 021064a4 (vtable +0x5c): the after-images' fade, the run, the water's
    // shimmer (02107438), the pieces' animations.
    ScriptLoop::afterUpdate();
    const std::int32_t elapsed = kOne;
    trailAlpha_.update(elapsed);
    if (state_ == 2) {
        swim(elapsed);
    }
    shimmerAlpha_ = 0x800;
    shimmerPhase_ += (elapsed / kOne) * 0x40;
    for (Piece& p : pieces_) {
        p.player.update(elapsed);
    }
    lastElapsed_ = elapsed;
    draw();
}

void FishLoop::draw() {
    // 02106538 (vtable +0x64): the water's picture as a grid of 9 x 7
    // corners whose texture shimmers by up to a texel (the sine table at a
    // phase per corner), half translucent; the pieces; in the rush, the
    // fish's after-images, the oldest faintest, fading out with the rush.
    scene_.loopMeshes.clear();
    scene_.loopSprites.clear();
    if (shimmerAlpha_ != 0 && shimmer_) {
        Scene::LoopMesh mesh;
        mesh.image = shimmer_;
        mesh.columns = 9;
        mesh.alpha = alpha5(shimmerAlpha_);
        for (int r = 0; r < 7; ++r) {
            const std::int32_t y = r * 0x1b000 + 0x2000;
            for (int c = 0; c < 9; ++c) {
                const std::int32_t x = c * 0x1f800 + 0x2000;
                const int k = (c & 1) * 5 + (c >> 1);
                const int i = int(std::uint32_t((shimmerPhase_ + k * 0x140) *
                                                (k + ((r + 1) & 1) * 4 + (r >> 1))) &
                                  0xffff) >>
                              4;
                const std::uint32_t w =
                    game_.assets.codeWord(-1, 0x02000000, kSinCos + 4u * unsigned(i));
                const auto sine = std::int16_t(w & 0xffff), cosine = std::int16_t(w >> 16);
                mesh.corners.push_back({x, y, x + sine, y + cosine});
            }
        }
        scene_.loopMeshes.push_back(std::move(mesh));
    }
    for (const Piece& p : pieces_) {
        if (!p.player.sequence() || !p.shown) {
            continue;
        }
        Scene::LoopSprite s;
        s.animation = p.player.resource();
        s.sequence = p.player.sequence();
        s.step = p.player.step();
        s.position = {centre(p.cell[0]), centre(p.cell[1])};
        s.alpha = alpha5(kOne);
        scene_.loopSprites.push_back(s);
    }
    const float fade = float(trailAlpha_.value()) / 4096.0f;
    const AnimationResource* pack = scene_.animation(kPieces);
    if (fade > 0.0f && pack) {
        const int count = rushBounces_ == 0 ? 8 : rushBounces_ <= 1 ? 5 : 2;
        for (int i = 0; i < count; ++i) {
            const Trail& tr = trail_[std::size_t(count - 1 - i)];
            if (!tr.sequence) {
                continue;
            }
            const float share = float(unsigned(i + 1)) / float(unsigned(count + 1));
            const std::int32_t a = std::int32_t(float(std::int32_t(4096.0f * share)) * fade);
            Scene::LoopSprite s;
            s.animation = pack;
            s.sequence = tr.sequence;
            s.position = {(tr.at[0] + 0x800) & ~0xfff, (tr.at[1] + 0x800) & ~0xfff};
            s.alpha = alpha5(a);
            s.mirror = tr.direction[0] <= 0 ? 1u : 0u;
            scene_.loopSprites.push_back(s);
        }
    }
}

// ------------------------------------------------------------ commands

bool FishLoop::command(gss::Call& c, int op) {
    const std::size_t n = c.count() > 0 ? c.count() - 1 : 0; // operands after the sub-command
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    auto out = [&](std::size_t i, Value v) {
        if (Value* r = c.reg(i)) {
            *r = std::move(v);
        }
    };
    switch (op) {
    case 1: // 021094d0
        loadTanks();
        return result(1);
    case 2: // 02109710: eight numbers
        if (n < 8) {
            return result(0);
        }
        for (std::size_t i = 0; i < 8; ++i) {
            numbers_[i] = c.real(1 + i);
        }
        // Two of them are scaled by sqrt(648) (the diagonal of 18 x 18):
        // +0x174 and +0x17c, neither read again.
        numbers_[5] *= std::sqrt(648.0f);
        numbers_[7] *= std::sqrt(648.0f);
        return result(1);
    case 10: // 02109804 {picture}
        if (n == 0) {
            return result(0);
        }
        return result(setUp(kit::text(c, 1)) ? 1 : 0);
    case 11: { // 02109b34 {-> tank, -> bubbles, -> index, -> seconds}
        const Tank* t = tank();
        if (n < 4 || !t) {
            return result(0);
        }
        out(1, Value::ofInt(stage_));
        out(2, Value::ofInt(t->bubbles));
        out(3, Value::ofInt(stage_)); // the tank's number in the list ([3])
        out(4, Value::ofInt(t->seconds));
        return result(1);
    }
    case 12: { // 02109be0: back to before the run
        grid_ = saved_;
        if (const Tank* t = tank()) {
            for (int y = 0; y < t->height; ++y) {
                for (int x = 0; x < t->width; ++x) {
                    const int i = grid_[std::size_t(x)][std::size_t(y)];
                    if (i < 0 || i >= int(pieces_.size())) {
                        continue;
                    }
                    Piece& p = pieces_[std::size_t(i)];
                    // 02105b78: the kind's sequence again.
                    if (p.player.resource() && p.kind) {
                        p.player.play(p.player.resource()->animation->sequence(p.kind->sequence),
                                      0);
                    }
                    p.shown = p.kind && !p.kind->sequence.empty();
                    p.cell = {x, y};
                }
            }
        }
        // The pieces' hit objects end themselves.
        for (const Piece& p : pieces_) {
            if (Chara* o = scene_.find(format("obj_%02d", p.index))) {
                setNormal(*o, "AutoDeleteObj_Nrm");
            }
        }
        placeFish();
        state_ = 1;
        return result(1);
    }
    case 20: // 02109d60 {frames per cell}: the run starts
        if (n == 0) {
            return result(0);
        }
        framesPerCell_ = c.real(1);
        state_ = 2;
        elapsed_ = 0;
        eaten_ = 0;
        warned_ = false;
        stepIndex_ = 0;
        hitIndex_ = 0;
        saved_ = grid_;
        if (Chara* f = fish(); f && f->program) {
            f->thread.start(*f->program, "Fish_Move"); // 0210b838 (020db62c)
        }
        return result(1);
    case 21: { // 02109e0c {-> seconds left}
        const Tank* t = tank();
        if (n == 0 || !t) {
            return result(0);
        }
        const int left = state_ == 1 ? t->seconds : std::max(t->seconds - int(elapsed_ / 60.0f), 0);
        out(1, Value::ofInt(left));
        return result(1);
    }
    case 30: { // 02109ec8 {object, kind, -> piece}: a bubble the player places
        if (n < 2) {
            return result(0);
        }
        Chara* o = scene_.find(kit::text(c, 1));
        const Kind* k = kindOf(c.integer(2));
        if (!o || !k) {
            return result(0);
        }
        Piece p;
        p.index = int(pieces_.size());
        p.kind = k;
        const auto at = o->position.value(), off = o->offset.value();
        p.cell = {cellOf(at[0] + off[0]), cellOf(at[1] + off[1])};
        if (const AnimationResource* a = scene_.animation(kPieces)) {
            p.player.attach(a);
            p.player.play(a->animation->sequence(k->sequence), 0);
        }
        pieces_.push_back(std::move(p));
        out(3, Value::ofInt(pieces_.back().index));
        return result(1);
    }
    case 31: { // 0210a1a0 {piece, x, y, -> x, -> y}: onto the nearest free spot
        if (n < 3) {
            return result(0);
        }
        const int index = c.integer(1);
        const std::int32_t px = toFixed(c.real(2)), py = toFixed(c.real(3));
        const Cell near{cellOf(px), cellOf(py)};
        // The spots (kind 50) among the cells around, nearest first, within
        // a cell's width.
        Cell best{-1, -1};
        float bestDistance = 1000000.0f;
        for (int i = -1; i < 2; ++i) {
            for (int j = -1; j < 2; ++j) {
                const Cell cell{near[0] + i, near[1] + j};
                if (!inside(cell)) {
                    continue;
                }
                const Piece* spot = pieceAt(cell);
                if (!spot || !spot->kind || spot->kind->kind != 50) {
                    continue;
                }
                const std::int32_t cx =
                    std::int32_t(4096.0f * ((float(cell[0]) * 18.0f + 2.0f) + 9.0f)) - px;
                const std::int32_t cy =
                    std::int32_t(4096.0f * ((float(cell[1]) * 18.0f + 2.0f) + 9.0f)) - py;
                std::int64_t d2 = std::int64_t(cx) * cx + std::int64_t(cy) * cy;
                d2 = (d2 + ((d2 >> 63) & 0xfff)) >> 12;
                const float d = float(std::int32_t(d2)) / 4096.0f;
                if (d < bestDistance) {
                    best = cell;
                    bestDistance = d;
                }
            }
        }
        if ((best[0] == -1 && best[1] == -1) || bestDistance > 324.0f) {
            return result(0);
        }
        Piece* spot = pieceAt(best);
        if (!spot) {
            return result(0);
        }
        spot->shown = false;
        grid_[std::size_t(best[0])][std::size_t(best[1])] = index;
        if (n > 3) {
            out(4, Value::ofFloat(float(centre(best[0])) / 4096.0f));
            out(5, Value::ofFloat(float(centre(best[1])) / 4096.0f));
        }
        if (index >= 0 && index < int(pieces_.size())) {
            pieces_[std::size_t(index)].cell = best;
        }
        plot();
        return result(1);
    }
    case 32: { // 0210a72c {piece}: off its spot again
        if (n == 0 || !tank()) {
            return result(0);
        }
        const int index = c.integer(1);
        for (int y = 0; y < tank()->height; ++y) {
            for (int x = 0; x < tank()->width; ++x) {
                int& g = grid_[std::size_t(x)][std::size_t(y)];
                if (g != index) {
                    continue;
                }
                g = initial_[std::size_t(x)][std::size_t(y)];
                if (Piece* spot = pieceAt({x, y})) {
                    spot->shown = true;
                }
                plot();
                return result(1);
            }
        }
        return result(0);
    }
    case 33: { // 0210a838 {piece, shown}
        if (n < 2) {
            return result(0);
        }
        const int index = c.integer(1);
        if (index < 0 || index >= int(pieces_.size())) {
            return result(0);
        }
        pieces_[std::size_t(index)].shown = c.at(2).truthy();
        return result(1);
    }
    case 40: // 0210a8d4 {-> dx, -> dy, [-> last dx, -> last dy]}
        if (n < 2) {
            return result(0);
        }
        out(1, Value::ofInt(direction_[0]));
        out(2, Value::ofInt(direction_[1]));
        if (n > 3) {
            out(3, Value::ofInt(lastDirection_[0]));
            out(4, Value::ofInt(lastDirection_[1]));
        }
        return result(1);
    case 41: // 0210a97c {speed}: at most 10
        if (n == 0) {
            return result(0);
        }
        lastSpeed_ = speed_;
        speed_ = std::min(10.0f, c.real(1)); // 0210a9f0
        return result(1);
    case 42: // 0210aa8c {-> speed, [-> last speed]}
        if (n == 0) {
            return result(0);
        }
        out(1, Value::ofFloat(speed_));
        if (n > 1) {
            out(2, Value::ofFloat(lastSpeed_));
        }
        return result(1);
    case 43: // 0210aa0c {-> the rush's bounces left}
        if (n == 0) {
            return result(0);
        }
        out(1, Value::ofInt(rushLeft()));
        return result(1);
    case 50: // 0210ab04 {how many kinds}
        kinds_.clear();
        return result(1);
    case 51: { // 0210ad14 {kind, sequence, fish block, hit block}
        if (n < 4) {
            return result(0);
        }
        const int kind = c.integer(1);
        kinds_[kind] = {kind, kit::text(c, 2), kit::text(c, 3), kit::text(c, 4)};
        return result(1);
    }
    default:
        return ScriptLoop::command(c, op);
    }
}

} // namespace picarat::adventure4
