// The model railway. See a4_train.hpp.
#include "minigames/a4_train.hpp"
#include "puzzles/a4_loop_kit.hpp"
#include "system/a4_definitions.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace picarat::adventure4 {

namespace {
// Cell kinds (5200 operand 1).
enum Kind : int {
    Empty = 0,      // 1..7 printed rails (like the pieces), 11..17 roads
    Lot = 20,       // an empty lot
    StartCell = 31, // a train's start
    Goal = 32,      // the goal (colour 99: the fixed trains')
    Obstacle = 33,  // house, mountain, tree
    Station = 34,   // object -4 vertical, 5 horizontal
};

constexpr int kFuelCan = 6;
constexpr int kCellPixels = 16;
// The board's table (ov39 0210b9e0, set by the static initialiser
// 0210b958): the run's board origin (24, 24) in pixels, half a cell (8.0),
// the camera's centre (128, 100) and the isometric projection: x' = 1.25 x
// - 1.25 y + 128, y' = 0.625 x + 0.625 y.
constexpr int kOrigin = 24;
constexpr std::int32_t kHalfCell = 0x8000;
constexpr std::int32_t kCameraX = 0x80000, kCameraY = 0x64000;
constexpr std::int32_t kProjectX = 0x80000, kProjectY = 0;
constexpr std::int32_t kXx = 0x1400, kXy = 0xa00, kYx = -0x1400, kYy = 0xa00;
// Sound effects (020da688): a piece laid, the run's rumble, the whistle,
// a fuel can, a station.
constexpr int kLaySound = 0x280, kRunSound = 0x283, kWhistleSound = 0x282;
constexpr int kFuelSound = 0x287, kStationSound = 0x288;

std::int32_t toFixed(float f) {
    return std::int32_t(f * 4096.0f); // 0202973c then 02029318
}

std::int32_t divFx(std::int64_t a) {
    return std::int32_t(a / 4096); // rounded toward zero (the +0xfff of the code)
}

std::string format(const char* f, int a, int b = 0) {
    char out[32];
    std::snprintf(out, sizeof out, f, a, b);
    return out;
}
} // namespace

TrainLoop::TrainLoop(Game& game, int id, std::span<const Value> values)
    : ScriptLoop(game, id, values) {
    // 021059a0 (start): the courses, then the select screen.
    loadCourses();
    mode_ = 1;
}

void TrainLoop::resume() {
    ScriptLoop::resume();
}

// ------------------------------------------------------------- the courses

void TrainLoop::loadCourses() {
    // 0210b168: res/sub/lt4_sub_train.cb read with handlers for 5200..5230
    // (0210a9a0 the cell kinds by name, 0210aca8 a course's size and fuel,
    // 0210ad6c the number of trains, 0210ada8/0210ae30/0210aecc a player's
    // train, a fixed train, a road car, 0210af64 a row of cells,
    // 0210b124 the course's end). At most ten courses.
    const auto list = game_.assets.data("res/sub/lt4_sub_train.cb");

    struct KindDef {
        int kind = 0, colour = 0, object = 0;
    };

    std::map<std::string, KindDef> kinds;
    Course c;
    auto integer = [&](const import::adventure4::DataCommand& cmd, unsigned i) {
        return i < cmd.count ? std::int32_t(list.operands[cmd.first + i].value) : 0;
    };
    auto text = [&](const import::adventure4::DataCommand& cmd, unsigned i) {
        return i < cmd.count ? list.text(list.operands[cmd.first + i]) : std::string();
    };
    int row = 0;
    for (const auto& cmd : list.commands) {
        switch (cmd.opcode) {
        case 5200: // {name, kind, colour, object}; at most 48 kinds
            if (kinds.size() < 48) {
                kinds[text(cmd, 0)] = {integer(cmd, 1), integer(cmd, 2), integer(cmd, 3)};
            }
            break;
        case 5210: // {width, height, pictures..., fuel (operand 7)}
            c = Course{};
            c.width = integer(cmd, 0);
            c.height = integer(cmd, 1);
            c.fuel = integer(cmd, 7);
            c.cells.assign(std::size_t(c.width * c.height), Cell{});
            row = 0;
            break;
        case 5211: // {player trains, fixed trains}
            c.players = integer(cmd, 0);
            c.fixed = integer(cmd, 1);
            break;
        case 5212: // a player's train {x, y, cars}
            if (int(c.starts.size()) < c.players) {
                c.starts.push_back(
                    {integer(cmd, 0) + c.width * integer(cmd, 1), integer(cmd, 2), 0});
            }
            break;
        case 5213: // a fixed train {x, y, cars, direction}
            if (int(c.starts.size()) < c.players + c.fixed) {
                c.starts.push_back({integer(cmd, 0) + c.width * integer(cmd, 1), integer(cmd, 2),
                                    integer(cmd, 3)});
            }
            break;
        case 5214: // a road car {x, y, direction}: one car
            c.starts.push_back({integer(cmd, 0) + c.width * integer(cmd, 1), 1, integer(cmd, 2)});
            ++c.roads;
            break;
        case 5220: // a row of cells by kind name
            if (int(cmd.count) == c.width && row < c.height) {
                for (int x = 0; x < c.width; ++x) {
                    Cell& cell = c.cells[std::size_t(row * c.width + x)];
                    const auto it = kinds.find(text(cmd, unsigned(x)));
                    if (it != kinds.end()) {
                        cell.kind = it->second.kind;
                        cell.colour = it->second.colour;
                        cell.object = it->second.object;
                    }
                    if (cell.kind == Station) {
                        ++c.stations;
                    }
                }
                ++row;
            }
            break;
        case 5230: // the course is complete
            if (courses_.size() < 10) {
                courses_.push_back(c);
            }
            break;
        default:
            break;
        }
    }
}

// --------------------------------------------------------------- commands

bool TrainLoop::command(gss::Call& c, int op) {
    auto arg = [&](std::size_t i) {
        return c.count() > i ? c.integer(i) : 0;
    };
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    switch (op) {
    case 0: // 021095b0: the mode
        return result(mode_);
    case 1: // 021095c8: a new mode (the select, editor or run setup follows)
        if (c.count() < 2) {
            return false;
        }
        mode_ = arg(1);
        changing_ = true;
        return result(1);
    case 2: // 021095fc: the course
        return result(stage_);
    case 3: // 02109614
        if (c.count() != 2) {
            return false;
        }
        stage_ = std::int8_t(arg(1));
        return result(1);
    case 4: // 02109644
        forgetPictures();
        return result(1);
    case 5: // 021097b0: the run's state (6 without an operand: 0, paused)
    case 6:
        runState_ = arg(1);
        return result(1);
    case 7: // 021097d0
        viewStep_ = 0;
        return result(1);
    case 8: { // 021097e0: the camera shakes, 24 frames
        // One pixel each way, the sign changing every three frames.
        const std::int32_t d = (viewStep_ / 3) % 2 == 0 ? kOne : -kOne;
        view_[0] += d;
        view_[1] += d;
        ++viewStep_;
        return result(viewStep_ != 24 ? 1 : 0);
    }
    case 9: { // 021098d8: the camera's glide to the crashed vehicle
        const Vehicle& v = vehicles_[std::size_t(camera_)];
        viewStep_ = 0;
        viewFrom_ = view_;
        viewTo_ = {kCameraX - v.car[0].screen[0], kCameraY - v.car[0].screen[1]};
        return result(camera_ != 0 ? 1 : 0);
    }
    case 10: { // 02109984: one of its 16 frames
        const std::int32_t t = toFixed(float(viewStep_) / 16.0f);
        view_ = {viewFrom_[0] + kit::mulFx(viewTo_[0] - viewFrom_[0], t),
                 viewFrom_[1] + kit::mulFx(viewTo_[1] - viewFrom_[1], t)};
        ++viewStep_;
        return result(viewStep_ != 16 ? 1 : 0);
    }
    case 100: // 02109ac4: the select screen is set up
        changing_ = false;
        return result(1);
    case 200: // 02109ad4
        setUpEditor();
        return result(1);
    case 201: // 0210a0fc
        resetBoard();
        return result(1);
    case 300: // 0210a10c
        setUpRun();
        return result(1);
    default:
        return ScriptLoop::command(c, op);
    }
}

// ------------------------------------------------------------- geometry

TrainLoop::Point TrainLoop::centre(int originX, int originY, int cell) const {
    // 02107744: the cell's centre, half a pixel up and left of the grid
    // point (single precision).
    const int w = stageShown_ > 0 ? courses_[std::size_t(stageShown_ - 1)].width : 1;
    const int row = cell / w, col = cell % w;
    return {toFixed(float(originX + col * kCellPixels) - 0.5f),
            toFixed(float(originY + row * kCellPixels) - 0.5f)};
}

TrainLoop::Point TrainLoop::edge(int cell, int direction) const {
    // 02107fb0: the middle of the cell's side in a direction (the run's
    // board origin).
    Point p = centre(kOrigin, kOrigin, cell);
    switch (direction) {
    case 0:
        p[0] += kHalfCell;
        break;
    case 1:
        p[0] -= kHalfCell;
        break;
    case 2:
        p[1] += kHalfCell;
        break;
    case 3:
        p[1] -= kHalfCell;
        break;
    default:
        break;
    }
    return p;
}

TrainLoop::Point TrainLoop::project(Point b) {
    // 0210825c.
    return {divFx(std::int64_t(b[0]) * kXx + std::int64_t(kYx) * b[1]) + kProjectX,
            divFx(std::int64_t(b[0]) * kXy + std::int64_t(kYy) * b[1]) + kProjectY};
}

TrainLoop::Point TrainLoop::curve(Point a, Point c, Point b, std::int32_t t) {
    // 02108064: the quadratic curve from a to b about c at t (0..1).
    const std::int32_t u = kOne - t;
    const std::int32_t uu = kit::mulFx(u, u), tt = kit::mulFx(t, t);
    const std::int32_t ut = kit::mulFx(divFx(std::int64_t(t) * 0x2000), u);
    Point p;
    for (int i = 0; i < 2; ++i) {
        p[std::size_t(i)] = kit::mulFx(uu, a[std::size_t(i)]) + kit::mulFx(ut, c[std::size_t(i)]) +
                            kit::mulFx(tt, b[std::size_t(i)]);
    }
    return p;
}

bool TrainLoop::penOver(int cell) const {
    // 021077d0: the pen within half a cell of the cell's centre.
    const Point p = centre(originX_, originY_, cell);
    const std::int32_t px = std::int32_t(services_.touch.x) << 12,
                       py = std::int32_t(services_.touch.y) << 12;
    return p[0] - kHalfCell < px && px < p[0] + kHalfCell && p[1] - kHalfCell < py &&
           py < p[1] + kHalfCell;
}

const Animation::Sequence* TrainLoop::sequence(const AnimationResource* a,
                                               const std::string& name) const {
    return a && a->animation ? a->animation->sequence(name) : nullptr;
}

Chara* TrainLoop::object(const std::string& name) const {
    return scene_.find(name);
}

// -------------------------------------------------------------- the board

void TrainLoop::refreshCell(int index, bool run) {
    // 02108300: a cell's picture by its kind and piece ("fix%d" printed
    // rails in the editor, "rail%d" laid pieces and the run's rails,
    // "load%d" roads, 8/9 level crossings, "start%d", "goal", "map%d").
    const Cell& c = course().cells[std::size_t(index)];
    Look& look = tiles_[std::size_t(index)];
    std::string name;
    if (c.kind < 8) {
        if (c.state == 0) {
            name = format(run ? "rail%d" : "fix%d", c.kind);
        } else if (run) {
            name = "rail7";
        } else {
            name = c.state == 1 ? "fix8" : c.state == 2 ? "fix9" : "fix7";
        }
    } else if (c.kind < 18) {
        if (c.state == 0) {
            name = format("load%d", c.kind - 10);
        } else if (c.state == 2) {
            name = "load8";
        } else if (c.state == 1) {
            name = "load9";
        } else {
            return;
        }
    } else if (c.state > 0) {
        name = format("rail%d", c.state);
    } else if (c.kind == StartCell) {
        name = format("start%d", c.colour);
    } else if (c.kind == Goal) {
        name = "goal";
    } else {
        name = format("map%d", c.colour);
    }
    look.sequence = sequence(look.animation, name);
}

void TrainLoop::resetBoard() {
    // 021088ac: every piece taken up, the heads back at the starts, the
    // fuel full.
    Course& co = course();
    for (int i = 0; i < int(co.cells.size()); ++i) {
        if (co.cells[std::size_t(i)].state != 0) {
            co.cells[std::size_t(i)].state = 0;
            refreshCell(i, false);
        }
    }
    for (int i = 0; i < co.players; ++i) {
        heads_[std::size_t(i)] = vehicles_[std::size_t(i)].start;
        if (Chara* o = object(format("actv%d", i))) {
            o->position.set(centre(originX_, originY_, heads_[std::size_t(i)]));
        }
        vehicles_[std::size_t(i)].length = 0;
    }
    fuel_ = co.fuel;
    showFuel();
}

void TrainLoop::showFuel() {
    // 02105ebc / 021088ac: the bar moves up two pixels a unit (at most 50).
    if (Chara* bar = object("anm_fuel_bar")) {
        const float v = -2.0f * float(std::min(fuel_, 50));
        bar->offset.set({0, toFixed(v)});
    }
}

void TrainLoop::forgetPictures() {
    // 02109644.
    tiles_ = {};
    objects_ = {};
    mapTiles_ = {};
    mapObjects_ = {};
    for (Vehicle& v : vehicles_) {
        for (Car& car : v.car) {
            car.look = car.map = nullptr;
        }
    }
    scene_.loopSprites.clear();
}

// -------------------------------------------------------------- the editor

void TrainLoop::setUpEditor() {
    // 02109ad4.
    const AnimationResource* panel = scene_.animation("anm_pnl");
    pen_ = 0;
    drag_ = -1;
    if (stage_ != stageShown_ && stage_ >= 1 && stage_ <= int(courses_.size())) {
        stageShown_ = stage_;
        const Course& co = course();
        vehicles_.assign(co.starts.size(), Vehicle{});
        for (std::size_t i = 0; i < co.starts.size(); ++i) {
            vehicles_[i].start = co.starts[i].cell;
            vehicles_[i].cars = co.starts[i].cars;
            vehicles_[i].direction = co.starts[i].direction;
            vehicles_[i].road = int(i) >= co.players + co.fixed;
        }
        resetBoard();
    }
    if (stageShown_ < 1) {
        changing_ = false;
        return;
    }
    Course& co = course();
    // The board in the middle of a 12 x 9 area from (24, 24).
    originX_ = kOrigin + ((12 - co.width) >> 1) * kCellPixels;
    originY_ = kOrigin + ((9 - co.height) >> 1) * kCellPixels;
    for (int i = 0; i < co.players; ++i) {
        Vehicle& v = vehicles_[std::size_t(i)];
        if (v.length == 0) {
            heads_[std::size_t(i)] = v.start;
            v.path[0] = v.start;
        }
        // The head's marker "actv%d", blinking.
        const std::string name = format("actv%d", i);
        if (Chara* o = scene_.create(name, 14, {})) {
            kit::attach(scene_, *o, "anm_pnl", -1);
            kit::play(*o, "actv");
            o->position.set(centre(originX_, originY_, heads_[std::size_t(i)]));
            o->alpha.move(0x666, 0x3c000, 0x20);
        }
    }
    showFuel();
    const int roadStart = co.roads > 0 ? vehicles_[std::size_t(co.players + co.fixed)].start : -1;
    for (int i = 0; i < int(co.cells.size()); ++i) {
        Cell& cell = co.cells[std::size_t(i)];
        tiles_[std::size_t(i)] = {panel, nullptr, false, {}};
        if (cell.kind == StartCell) {
            cell.state = 0;
        }
        refreshCell(i, false);
        Look& obj = objects_[std::size_t(i)];
        obj = {panel, nullptr, false, {}};
        if (cell.object == 0) {
            if (i == roadStart) {
                obj.sequence = sequence(panel, "car");
            }
        } else {
            obj.sequence = sequence(panel, format("obj%d", std::abs(cell.object)));
        }
    }
    if (Chara* max = object("anm_fuel_max")) {
        max->animation.showText(format("%02d", co.fuel), 6, 0, 0);
    }
    // Each train's number of cars at its start.
    for (int i = 0; i < co.players + co.fixed; ++i) {
        const std::string name = format("vehicle_num%d", i);
        if (Chara* o = scene_.create(name, 14, {})) {
            kit::attach(scene_, *o, "anm_pnl", -1);
            kit::play(*o, format("%1d", vehicles_[std::size_t(i)].cars));
            o->position.set(centre(originX_, originY_, vehicles_[std::size_t(i)].start));
        }
    }
    changing_ = false;
}

void TrainLoop::penIdle() {
    // 02105a64: a press on a train's head picks that train.
    if (services_.touch.state != 1) {
        return;
    }
    for (int i = 0; i < course().players; ++i) {
        if (penOver(heads_[std::size_t(i)])) {
            if (Chara* o = object(format("actv%d", i))) {
                o->alpha.set(kOne);
            }
            selected_ = i;
            pen_ = 1;
        }
    }
}

void TrainLoop::penDrag() {
    // 02105b24: the pen more than half a cell away from the head's centre
    // asks for the neighbouring cell on that side (the longer axis wins;
    // a tie is horizontal). Lifting the pen lets go of the train.
    if (services_.touch.state == 3) {
        if (Chara* o = object(format("actv%d", selected_))) {
            o->alpha.move(0x666, 0x3c000, 0x20);
        }
        pen_ = 0;
        return;
    }
    const int head = heads_[std::size_t(selected_)];
    const Point c = centre(originX_, originY_, head);
    const std::int32_t px = std::int32_t(services_.touch.x) << 12,
                       py = std::int32_t(services_.touch.y) << 12;
    const std::int32_t dx = std::abs(px - c[0]), dy = std::abs(py - c[1]);
    if (std::max({dx, dy, kHalfCell}) == kHalfCell) {
        drag_ = -1;
        return;
    }
    const int w = course().width;
    if (dx < dy) {
        drag_ = py < c[1] ? 3 : 2;
        target_ = py < c[1] ? head - w : head + w;
    } else {
        drag_ = px < c[0] ? 1 : 0;
        target_ = px < c[0] ? head - 1 : head + 1;
    }
}

void TrainLoop::applyDrag() {
    // 02105ebc: a step taken moves the head (and its marker).
    if (drag_ == -1) {
        return;
    }
    if (extend()) {
        services_.effect(kLaySound);
        heads_[std::size_t(selected_)] = target_;
        if (Chara* o = object(format("actv%d", selected_))) {
            o->position.set(centre(originX_, originY_, target_));
        }
        showFuel();
    }
    drag_ = -1;
}

int TrainLoop::pieceFor(int from, int at) const {
    // 02107f08: the piece for cell `at`, entered from cell `from` and left
    // in the drag's direction (1 vertical, 2 horizontal, 3 up-right,
    // 4 down-right, 5 down-left, 6 up-left).
    switch (drag_) {
    case 0:
        return at < from ? 4 : from == at - 1 ? 2 : 3;
    case 1:
        return from < at ? 6 : from == at + 1 ? 2 : 5;
    case 2:
        return at < from ? 4 : from == at - 1 ? 5 : 1;
    case 3:
        return from < at ? 6 : from == at + 1 ? 3 : 1;
    default:
        return 0;
    }
}

bool TrainLoop::extend() {
    // 02107904: one step of the selected train's track toward target_.
    Course& co = course();
    Vehicle& v = vehicles_[std::size_t(selected_)];
    const int head = heads_[std::size_t(selected_)];
    Cell& from = co.cells[std::size_t(head)];
    const bool horizontal = (drag_ & 2) == 0;
    for (int i = 0; i < co.players; ++i) {
        if (i != selected_ && heads_[std::size_t(i)] == target_) {
            return false; // another train's head
        }
    }
    const int w = co.width;
    switch (drag_) {
    case 0:
        if (target_ % w == 0) {
            return false;
        }
        break;
    case 1:
        if (head % w == 0) {
            return false;
        }
        break;
    case 2:
        if (target_ >= int(co.cells.size())) {
            return false;
        }
        break;
    case 3:
        if (target_ < 0) {
            return false;
        }
        break;
    default:
        break;
    }
    Cell& to = co.cells[std::size_t(target_)];
    // Back onto the previous cell: its piece comes up again.
    const int previous = from.kind == Goal ? v.length - 2 : v.length - 1;
    if (previous >= 0 && target_ == v.path[std::size_t(previous)]) {
        if (from.kind == Goal) {
            --v.length;
        }
        to.state = to.state == 7 ? (horizontal ? 1 : 2) : 0;
        refreshCell(target_, false);
        if (to.object == kFuelCan && to.state == 0) {
            fuel_ -= 5;
        }
        ++fuel_;
        --v.length;
        return true;
    }
    if (from.kind == Goal || fuel_ < 1) {
        return false;
    }
    for (int i = 0; i < co.players + co.fixed; ++i) {
        if (target_ == vehicles_[std::size_t(i)].start) {
            return false;
        }
    }
    if (to.kind == Obstacle) {
        return false;
    }
    if (to.kind == Station &&
        ((horizontal && to.object == -4) || (!horizontal && to.object == 5))) {
        return false;
    }
    if ((to.kind == Goal || to.kind == Station) && to.colour == 99) {
        return false; // the fixed trains' goal
    }
    // What lies on the cell already: only a straight piece crossed at right
    // angles (a crossing, or a level crossing on a road).
    int piece = 0;
    if (to.kind < 8) {
        if (to.state != 0) {
            return false;
        }
        piece = to.kind;
    } else {
        piece = to.state;
        if (to.kind < 18) {
            if (to.state != 0) {
                return false;
            }
            piece = to.kind - 10;
        }
    }
    if (piece != 0 && !(piece == 1 && horizontal) && !(piece == 2 && !horizontal)) {
        return false;
    }
    bool laid = false;
    if (from.kind == StartCell) {
        v.points[0] = edge(head, drag_ ^ 1);
        v.length = 1;
        v.points[1] = edge(head, drag_);
        v.path[1] = target_;
        laid = true;
    } else if (from.kind == Lot || from.kind < 18) {
        // The head's own piece is laid as the track leaves it (a can on
        // it gives five units the first time).
        if (from.object == kFuelCan && from.state == 0) {
            fuel_ += 5;
        }
        from.state = from.state == 0 ? pieceFor(v.path[std::size_t(v.length - 1)], head) : 7;
        ++v.length;
        v.points[std::size_t(v.length)] = edge(head, drag_);
        v.path[std::size_t(v.length)] = target_;
        refreshCell(head, false);
        laid = true;
    } else if (from.kind == Station) {
        if ((horizontal && from.object == -4) || (!horizontal && from.object == 5)) {
            return false;
        }
        from.state = -1;
        ++v.length;
        v.points[std::size_t(v.length)] = edge(head, drag_);
        v.path[std::size_t(v.length)] = target_;
        laid = true;
    }
    if (to.kind == Goal) {
        // The goal holds the whole train: its far side ends the track.
        ++v.length;
        v.points[std::size_t(v.length)] = edge(target_, drag_);
    }
    --fuel_;
    return laid;
}

// ----------------------------------------------------------------- the run

void TrainLoop::buildFixedPaths() {
    // 02108cf0: a fixed train follows its printed rails to the goal.
    Course& co = course();
    for (int i = co.players; i < co.players + co.fixed; ++i) {
        Vehicle& v = vehicles_[std::size_t(i)];
        int cell = v.start, dir = v.direction;
        v.points[0] = edge(cell, dir ^ 1);
        v.length = 1;
        for (;;) {
            int next = cell;
            switch (dir) {
            case 0:
                next = cell + 1;
                break;
            case 1:
                next = cell - 1;
                break;
            case 2:
                next = cell + co.width;
                break;
            case 3:
                next = cell - co.width;
                break;
            default:
                break;
            }
            v.points[std::size_t(v.length)] = edge(cell, dir);
            v.path[std::size_t(v.length)] = next;
            const int kind = co.cells[std::size_t(next)].kind;
            if (kind == Goal) {
                ++v.length;
                v.points[std::size_t(v.length)] = edge(next, dir);
                break;
            }
            if (kind > 7) {
                break;
            }
            // The rail turns the train (3 up-right, 4 down-right, 5
            // down-left, 6 up-left).
            int turned = dir;
            switch (kind) {
            case 3:
                turned = dir == 2 ? 0 : 3;
                break;
            case 4:
                turned = dir == 3 ? 0 : 2;
                break;
            case 5:
                turned = dir == 3 ? 1 : 2;
                break;
            case 6:
                turned = dir == 2 ? 1 : 3;
                break;
            default:
                break;
            }
            ++v.length;
            cell = next;
            dir = turned;
        }
    }
}

void TrainLoop::buildRoadPaths() {
    // 02108efc: the road car follows its road until it comes back to its
    // start (it loops) or leaves the board (it starts again).
    Course& co = course();
    if (co.roads == 0) {
        return;
    }
    Vehicle& v = vehicles_[std::size_t(co.players + co.fixed)];
    int cell = v.start, dir = v.direction;
    v.points[0] = edge(cell, dir ^ 1);
    v.length = 1;
    for (;;) {
        int next = cell;
        bool off = false;
        switch (dir) {
        case 0:
            next = std::int8_t(cell + 1);
            off = (cell + 1) % co.width == 0;
            break;
        case 1:
            next = std::int8_t(cell - 1);
            off = cell % co.width == 0;
            break;
        case 2:
            next = std::int8_t(cell + co.width);
            off = co.width * co.height <= next;
            break;
        case 3:
            next = std::int8_t(cell - co.width);
            off = next < 0;
            break;
        default:
            break;
        }
        v.points[std::size_t(v.length)] = edge(cell, dir);
        if (next == v.start) {
            v.flags |= 8;
            v.points[0] = v.points[std::size_t(v.length)];
            return;
        }
        if (off) {
            v.flags &= ~8u;
            return;
        }
        v.path[std::size_t(v.length)] = next;
        int turned = dir;
        switch (co.cells[std::size_t(next)].kind) {
        case 13:
            turned = dir == 2 ? 0 : 3;
            break;
        case 14:
            turned = dir == 3 ? 0 : 2;
            break;
        case 15:
            turned = dir == 3 ? 1 : 2;
            break;
        case 16:
            turned = dir == 2 ? 1 : 3;
            break;
        default:
            break;
        }
        ++v.length;
        cell = next;
        dir = turned;
    }
}

void TrainLoop::setUpRun() {
    // 0210a10c.
    Course& co = course();
    const AnimationResource* lower = scene_.animation("anm_a_pnl");
    const AnimationResource* upper = scene_.animation("anm_b_pnl");
    originX_ = originY_ = kOrigin;
    for (int i = 0; i < int(co.cells.size()); ++i) {
        Cell& cell = co.cells[std::size_t(i)];
        cell.occupant = -1;
        Look& tile = tiles_[std::size_t(i)];
        tile = {lower, nullptr, false, {}};
        const Point board{toFixed(float(originX_ + (i % co.width) * kCellPixels)),
                          toFixed(float(originY_ + (i / co.width) * kCellPixels))};
        tile.screen = project(board);
        tile.mirror = cell.kind == 11 && cell.state == 2;
        refreshCell(i, true);
        // The map's laid pieces (upper screen), in the editor's look.
        mapTiles_[std::size_t(i)] = {};
        if (cell.kind < 21 && cell.state != 0) {
            Look keep = tile;
            tiles_[std::size_t(i)] = {upper, nullptr, false, {}};
            refreshCell(i, false);
            mapTiles_[std::size_t(i)] = tiles_[std::size_t(i)];
            tiles_[std::size_t(i)] = keep;
        }
        objects_[std::size_t(i)] = {};
        mapObjects_[std::size_t(i)] = {};
        if (cell.object != 0) {
            objects_[std::size_t(i)] = {lower,
                                        sequence(lower, format("obj%d", std::abs(cell.object))),
                                        cell.object < 1, tile.screen};
            if (cell.object == kFuelCan) {
                mapObjects_[std::size_t(i)] = {upper, sequence(upper, "obj6"), false, {}};
            }
        }
    }
    co.blocked = -1;
    buildFixedPaths();
    buildRoadPaths();
    for (int vi = 0; vi < int(vehicles_.size()); ++vi) {
        Vehicle& v = vehicles_[std::size_t(vi)];
        v.path[0] = v.start;
        v.index = 0;
        v.step = 0;
        v.flags = (v.flags & 0xbc) | 0x20;
        if (v.road) {
            v.flags |= 4;
        }
        Cell& start = co.cells[std::size_t(v.start)];
        start.occupant = vi;
        start.occupantCar = 0;
        // Which way the train leaves its start.
        int back = 0, piece = 2;
        bool mirror = false;
        if (v.length != 0) {
            back = v.path[1] <= v.path[0] ? 1 : 0;
            mirror = back != 0;
            if (std::abs(v.path[0] - v.path[1]) != 1) {
                piece = 1;
                mirror = !mirror;
            }
        }
        if (!v.road) {
            start.state = piece;
            refreshCell(v.start, true);
        }
        for (int k = 0; k < v.cars; ++k) {
            Car& car = v.car[std::size_t(k)];
            car = Car{};
            if (!v.road) {
                car.map = sequence(upper, k == 0 ? "btrain" : "bvehicle");
                car.mirror = mirror;
                car.board = v.length == 0 ? centre(kOrigin, kOrigin, v.start)
                                          : curve(v.points[0], centre(kOrigin, kOrigin, v.start),
                                                  v.points[1], 0x800);
                car.look = sequence(lower, format(k == 0 ? "train%d%d" : "vehicle%d%d", vi, back));
            } else {
                car.map = sequence(upper, "car");
                car.board = centre(originX_, originY_, v.path[0]);
                // The axis: whether the first point sits on a column line.
                if (((v.points[0][0] + 0x800) & 0xffff) != 0) {
                    v.flags |= 0x10;
                } else {
                    v.flags &= ~0x10u;
                }
                switch (v.direction) {
                case 0:
                    car.look = sequence(lower, "car1");
                    car.mirror = true;
                    break;
                case 1:
                    car.look = sequence(lower, "car0");
                    car.mirror = true;
                    break;
                case 2:
                    car.look = sequence(lower, "car1");
                    break;
                case 3:
                    car.look = sequence(lower, "car0");
                    break;
                default:
                    break;
                }
            }
            car.screen = project(car.board);
        }
    }
    // The cells by diagonals from the top-left corner (drawing order).
    order_.clear();
    for (int d = 0; d < co.width + co.height - 1; ++d) {
        int k = d * co.width;
        while (k >= 0) {
            if (k < int(co.cells.size())) {
                order_.push_back(k);
            }
            k -= co.width - 1;
            if (k % co.width == 0) {
                break;
            }
        }
    }
    runState_ = 0;
    stations_ = 0;
    camera_ = 0;
    follow();
    changing_ = false;
}

bool TrainLoop::moving(const Vehicle& v) {
    // 0210573c.
    if (v.flags & 2) {
        return false;
    }
    if (!(v.flags & 1)) {
        return v.index < v.length;
    }
    return v.step != v.stopStep;
}

void TrainLoop::stopAt(Vehicle& v, int step) {
    // 021057ac.
    if (!(v.flags & 1)) {
        v.stopStep = step;
        v.flags |= 1;
    }
}

void TrainLoop::advance(Vehicle& v) {
    // 02105464: 32 steps a cell; a train stops in the middle of its last
    // cell.
    if (++v.step > 31) {
        v.step = 0;
        ++v.index;
    }
    if (!(v.flags & 4) && v.length - 1 == v.index) {
        stopAt(v, 16);
    }
}

void TrainLoop::follow() {
    // 02105808: the followed vehicle's engine in the camera's centre.
    const Car& c = vehicles_[std::size_t(camera_)].car[0];
    view_ = {kCameraX - c.screen[0], kCameraY - c.screen[1]};
}

void TrainLoop::trainLook(Car& car, Point delta, int vehicle, int carIndex) {
    // 02108a80: the picture by the way the car moves (0 down, 1 up
    // vertically on the board; mirrored for the other axis; 2..4 the
    // turns).
    const float x = float(delta[0]) / 4096.0f, y = float(delta[1]) / 4096.0f;
    int look = 0;
    if (std::fabs(x) < 0.13f) {
        look = 0 >= y ? 1 : 0;
        car.mirror = look == 0;
    } else if (std::fabs(y) < 0.13f) {
        look = 0 >= x ? 1 : 0;
        car.mirror = look != 0;
    } else if (0 < x && 0 < y) {
        look = 2;
    } else if (0 < x && y < 0) {
        car.mirror = false;
        look = 3;
    } else if (x < 0 && 0 < y) {
        car.mirror = true;
        look = 3;
    } else {
        look = 4;
    }
    car.look = sequence(scene_.animation("anm_a_pnl"),
                        format(carIndex == 0 ? "train%d%d" : "vehicle%d%d", vehicle, look));
}

void TrainLoop::carLook(Car& car, Point delta) {
    // 02108c1c: a road car faces along the axis it moves on.
    const float x = float(delta[0]) / 4096.0f, y = float(delta[1]) / 4096.0f;
    int look = 0;
    if (std::fabs(x) < std::fabs(y)) {
        look = y >= 0 ? 1 : 0;
        car.mirror = false;
    } else {
        look = x >= 0 ? 1 : 0;
        car.mirror = true;
    }
    car.look = sequence(scene_.animation("anm_a_pnl"), format("car%d", look));
}

void TrainLoop::restartRoadCar(int vi) {
    // 02109178: a road car at the end of its road starts again from its
    // start - unless something stands there (then both stop: an accident).
    Course& co = course();
    Vehicle& v = vehicles_[std::size_t(vi)];
    Cell& start = co.cells[std::size_t(v.path[0])];
    const bool taken = start.occupant != -1;
    if (taken) {
        stopAt(v, 0);
        stopAt(vehicles_[std::size_t(start.occupant)], 0);
    }
    co.cells[std::size_t(v.path[std::size_t(v.index - 1)])].occupant = -1;
    if (taken) {
        co.blocked = vi;
    } else {
        start.occupant = vi;
        start.occupantCar = 0;
        const Cell& next = co.cells[std::size_t(v.path[1])];
        if (next.occupant != -1) {
            Vehicle& o = vehicles_[std::size_t(next.occupant)];
            if (next.occupantCar + 1 < o.cars || !moving(o)) {
                stopAt(v, 16);
                stopAt(o, 16);
            }
        }
    }
    if (((v.points[0][0] + 0x800) & 0xffff) == 0) {
        v.flags |= 0x10;
    } else {
        v.flags &= ~0x10u;
    }
    v.index = 0;
    Car& car = v.car[0];
    car.board = v.points[0];
    car.screen = project(car.board);
    if (!(v.flags & 8)) {
        const AnimationResource* lower = scene_.animation("anm_a_pnl");
        car.mirror = v.direction < 2;
        car.look = sequence(lower, v.direction == 0 || v.direction == 2 ? "car1" : "car0");
    }
}

void TrainLoop::runStep() {
    // 02106044: every vehicle one step on.
    Course& co = course();
    for (int vi = 0; vi < int(vehicles_.size()); ++vi) {
        Vehicle& v = vehicles_[std::size_t(vi)];
        const bool road = (v.flags & 4) != 0;
        if (!moving(v)) {
            continue;
        }
        advance(v);
        const int index = v.index;
        const int step = v.step;
        const int cars = std::min(v.cars, index + 1);
        for (int k = 0; k < cars; ++k) {
            Car& car = v.car[std::size_t(k)];
            const int seg = index - k;
            // A car waits until the half of its start cell is behind it.
            if (road ? (v.flags & 0x20) && step < 17 : seg == 0 && step < 17) {
                continue;
            }
            if (seg == 0 || step != 0) {
                const Point a = v.points[std::size_t(seg)], b = v.points[std::size_t(seg + 1)];
                Point at{}, delta{};
                if (road) {
                    v.flags &= ~0x20u;
                    if (a[0] == b[0] || a[1] == b[1]) {
                        const std::int32_t t = toFixed(float(step) / 32.0f);
                        at = {a[0] + kit::mulFx(t, b[0] - a[0]), a[1] + kit::mulFx(t, b[1] - a[1])};
                        delta = {at[0] - car.board[0], at[1] - car.board[1]};
                    } else {
                        // Round a corner: one axis, then the other.
                        if (step == 16) {
                            v.flags ^= 0x10;
                        }
                        const int u = step >= 16 ? step - 16 : step;
                        const std::int32_t t = toFixed(float(u) / 16.0f);
                        if (!(v.flags & 0x10)) {
                            at = {car.board[0], a[1] + kit::mulFx(t, b[1] - a[1])};
                            delta = step == 16 ? Point{0, b[1] - a[1]}
                                               : Point{at[0] - car.board[0], at[1] - car.board[1]};
                        } else {
                            at = {a[0] + kit::mulFx(t, b[0] - a[0]), car.board[1]};
                            delta = step == 16 ? Point{b[0] - a[0], 0}
                                               : Point{at[0] - car.board[0], at[1] - car.board[1]};
                        }
                    }
                    carLook(car, delta);
                } else {
                    const std::int32_t t = toFixed(float(step) / 32.0f);
                    at = curve(a, centre(kOrigin, kOrigin, v.path[std::size_t(seg)]), b, t);
                    delta = {at[0] - car.board[0], at[1] - car.board[1]};
                    trainLook(car, delta, vi, k);
                }
                car.board = at;
            } else {
                // The car enters cell path[seg].
                if (road) {
                    if (seg == v.length) {
                        restartRoadCar(vi);
                        return;
                    }
                    if (((v.points[std::size_t(seg)][0] + 0x800) & 0xffff) == 0) {
                        v.flags |= 0x10;
                    } else {
                        v.flags &= ~0x10u;
                    }
                }
                car.board = v.points[std::size_t(seg)];
                const int at = v.path[std::size_t(seg)];
                Cell& cell = co.cells[std::size_t(at)];
                if (cell.object == kFuelCan && objects_[std::size_t(at)].sequence) {
                    // The can is taken.
                    objects_[std::size_t(at)] = {};
                    mapObjects_[std::size_t(at)] = {};
                    if (vi == camera_) {
                        services_.effect(kFuelSound);
                    }
                }
                if (k == 0 && cell.kind == Station) {
                    ++stations_;
                    if (vi == camera_) {
                        services_.effect(kStationSound);
                    }
                }
                if (k == 0 && cell.occupant != -1) {
                    // Into an occupied cell: both crash.
                    v.flags |= 2;
                    vehicles_[std::size_t(cell.occupant)].flags |= 2;
                    break;
                }
                cell.occupant = vi;
                cell.occupantCar = k;
                if (seg == 1 && k + 1 < v.cars) {
                    co.cells[std::size_t(v.path[0])].occupantCar = k + 1;
                } else {
                    co.cells[std::size_t(v.path[std::size_t(seg - 1)])].occupant = -1;
                }
                auto blockedBy = [&](const Cell& next) {
                    if (next.occupant == -1) {
                        return false;
                    }
                    const Vehicle& o = vehicles_[std::size_t(next.occupant)];
                    return next.occupantCar + 1 < o.cars || !moving(o);
                };
                if (road && (v.flags & 8) && seg == v.length - 1) {
                    const Cell& next = co.cells[std::size_t(v.path[0])];
                    if (blockedBy(next)) {
                        stopAt(v, 16);
                        stopAt(vehicles_[std::size_t(next.occupant)], 16);
                    }
                } else if (k == 0 && seg < v.length - 1) {
                    const Cell& next = co.cells[std::size_t(v.path[std::size_t(seg + 1)])];
                    if (blockedBy(next)) {
                        stopAt(v, 16);
                        stopAt(vehicles_[std::size_t(next.occupant)], 16);
                    }
                }
            }
            car.screen = project(car.board);
        }
    }
    if (!services_.effectPlaying(kRunSound)) {
        services_.effect(kRunSound);
    }
    if (vehicles_[0].index == 3) {
        services_.stopEffect(kWhistleSound);
    }
    follow();
    checkResult();
}

void TrainLoop::checkResult() {
    // 02108590: a player's train standing short of the goal, or a road
    // car standing still, is an accident; with every player's train in the
    // goal the course is won if every station was passed.
    Course& co = course();
    int arrived = 0;
    for (int vi = 0; vi < int(vehicles_.size()); ++vi) {
        Vehicle& v = vehicles_[std::size_t(vi)];
        if (vi < co.players) {
            if (moving(v)) {
                continue;
            }
            if (co.cells[std::size_t(v.path[std::size_t(v.index)])].kind != Goal) {
                callback(0, "ResultAccident", {});
                camera_ = vi;
                runState_ = 2;
                return;
            }
            camera_ = vi ^ 1;
            ++arrived;
            if (!(v.flags & 0x40)) {
                // The cars stand straight: on a straight piece in the
                // middle of their cell, elsewhere on a whole pixel.
                for (int k = 0; k < v.cars; ++k) {
                    const int seg = v.length - 1 - k;
                    if (seg < 0) {
                        continue;
                    }
                    const Point a = v.points[std::size_t(seg)], b = v.points[std::size_t(seg + 1)];
                    Car& car = v.car[std::size_t(k)];
                    if (a[0] == b[0] || a[1] == b[1]) {
                        car.screen = tiles_[std::size_t(v.path[std::size_t(seg)])].screen;
                    } else {
                        car.screen = {(car.screen[0] + 0x800) & ~0xfff,
                                      (car.screen[1] + 0x800) & ~0xfff};
                    }
                }
                v.flags |= 0x40;
            }
        } else if ((v.flags & 4) && !moving(v)) {
            callback(0, "ResultAccident", {});
            camera_ = vi;
            runState_ = 2;
            return;
        }
    }
    if (arrived == co.players) {
        callback(0, co.stations == stations_ ? "ResultGoal" : "ResultMissGoal", {});
        runState_ = 2;
    }
}

// ------------------------------------------------------------------ hooks

void TrainLoop::afterPen() {
    // 021059d8 (vtable +0x54): the editor's pen.
    ScriptLoop::afterPen();
    if (changing_ || mode_ != 2) {
        return;
    }
    if (pen_ == 0) {
        penIdle();
    } else {
        penDrag();
    }
}

void TrainLoop::afterUpdate() {
    // 02105d8c (vtable +0x5c): the editor applies the drag and shows the
    // fuel; the run steps while it runs. Nothing moves while the lower
    // screen fades (0209f148).
    ScriptLoop::afterUpdate();
    if (changing_) {
        scene_.loopSprites.clear();
        return;
    }
    if (!scene_.screens[0].brightness.moving()) {
        if (mode_ == 2) {
            if (pen_ == 1) {
                applyDrag();
            }
            if (Chara* cur = object("anm_fuel_cur")) {
                cur->animation.showText(format("%02d", fuel_), 6, 0, 0);
            }
        } else if (mode_ == 3 && runState_ == 1) {
            runStep();
        }
    }
    // The draw hooks (+0x64, +0x6c), drawn under the objects.
    scene_.loopSprites.clear();
    if (mode_ == 2) {
        drawEditor();
    } else if (mode_ == 3) {
        drawRun();
        drawMap();
    }
}

// ---------------------------------------------------------------- drawing

void TrainLoop::sprite(const Look& look, Point at) {
    // 020a2cd0: the picture's first step at a pixel (rounded), opaque.
    if (!look.animation || !look.sequence) {
        return;
    }
    Scene::LoopSprite s;
    s.animation = look.animation;
    s.sequence = look.sequence;
    s.position = {(at[0] + 0x800) & ~0xfff, (at[1] + 0x800) & ~0xfff};
    s.mirror = look.mirror ? 1u : 0u;
    scene_.loopSprites.push_back(s);
}

void TrainLoop::drawEditor() {
    // 02106c60: the board's cells, then what stands on them.
    const Course& co = course();
    for (int i = 0; i < int(co.cells.size()); ++i) {
        const Point at{toFixed(float(originX_ + (i % co.width) * kCellPixels)),
                       toFixed(float(originY_ + (i / co.width) * kCellPixels))};
        sprite(tiles_[std::size_t(i)], at);
        Look obj = objects_[std::size_t(i)];
        obj.mirror = co.cells[std::size_t(i)].object <= 0;
        sprite(obj, at);
    }
}

void TrainLoop::drawRun() {
    // 02106dfc: the cells, then by diagonals what stands on them and the
    // cars, so that nearer things cover farther ones.
    const Course& co = course();
    for (int i = 0; i < int(co.cells.size()); ++i) {
        const Look& t = tiles_[std::size_t(i)];
        sprite(t, {t.screen[0] + view_[0], t.screen[1] + view_[1]});
    }
    if (co.blocked != -1) {
        drawCars(vehicles_[std::size_t(co.blocked)], 0);
    }
    drawn_.fill(false);
    for (Vehicle& v : vehicles_) {
        for (Car& car : v.car) {
            car.drawn = false;
        }
    }
    for (int k = 0; k < int(order_.size()); ++k) {
        drawCellAt(k);
    }
}

void TrainLoop::drawCellAt(int k) {
    // 02107014: the cell's object, then the car in it (with the object of
    // the cell it comes from or goes to drawn first when that one is the
    // next in the order).
    if (k >= int(order_.size())) {
        return;
    }
    const Course& co = course();
    const int cell = order_[std::size_t(k)];
    auto object = [&](int at) {
        const Look& o = objects_[std::size_t(at)];
        if (o.sequence && !drawn_[std::size_t(at)]) {
            drawn_[std::size_t(at)] = true;
            sprite(o, {o.screen[0] + view_[0], o.screen[1] + view_[1]});
        }
    };
    object(cell);
    const Cell& c = co.cells[std::size_t(cell)];
    if (c.occupant < 0) {
        return;
    }
    Vehicle& v = vehicles_[std::size_t(c.occupant)];
    const int carIndex = c.occupantCar;
    if (v.car[std::size_t(carIndex)].drawn) {
        return;
    }
    const int seg = v.index - carIndex;
    const int w = co.width;
    // The next cell in the order is drawn first when it lies on the same
    // diagonal line as this one.
    auto nextFirst = [&] {
        if (k + 1 < int(order_.size()) &&
            std::abs(order_[std::size_t(k)] - order_[std::size_t(k + 1)]) == w - 1) {
            drawCellAt(k + 1);
        }
    };
    if (v.step <= 16 && seg - 1 >= 0) {
        // The first half of the cell: the cell the car comes from.
        const int from = v.path[std::size_t(seg - 1)];
        if (from - cell == 1) {
            nextFirst();
        }
        object(from);
    } else if (v.step > 16 && seg + 1 < v.length) {
        // The second half: the cell it goes to.
        const int to = v.path[std::size_t(seg + 1)];
        if (to - cell == w || to - cell == 1) {
            nextFirst();
        }
        object(to);
    }
    drawCars(v, carIndex);
}

void TrainLoop::drawCars(Vehicle& v, int k) {
    // 021054d4: a car (and the one behind it while it still leaves the
    // start).
    const AnimationResource* lower = scene_.animation("anm_a_pnl");
    auto draw = [&](int i) {
        Car& car = v.car[std::size_t(i)];
        sprite({lower, car.look, car.mirror, {}},
               {car.screen[0] + view_[0], car.screen[1] + view_[1]});
        car.drawn = true;
    };
    if (v.index == k && k + 1 < v.cars && v.step > 25) {
        draw(k + 1);
    }
    draw(k);
}

void TrainLoop::drawMap() {
    // 02107410 (vtable +0x6c): the upper screen's map: the laid pieces,
    // the cans and the cars on the course's picture.
    const Course& co = course();
    const AnimationResource* upper = scene_.animation("anm_b_pnl");
    const std::int32_t ox = toFixed(16.0f + float(((12 - co.width) >> 1) << 4)),
                       oy = toFixed(24.0f + float(((9 - co.height) >> 1) << 4));
    for (int i = 0; i < int(co.cells.size()); ++i) {
        const Point at{toFixed(float(originX_ + (i % co.width) * kCellPixels)) + ox,
                       toFixed(float(originY_ + (i / co.width) * kCellPixels)) + oy};
        Look t = mapTiles_[std::size_t(i)];
        t.mirror = false;
        sprite(t, at);
        sprite(mapObjects_[std::size_t(i)], at);
    }
    for (const Vehicle& v : vehicles_) {
        for (int k = v.cars - 1; k >= 0; --k) {
            const Car& car = v.car[std::size_t(k)];
            sprite({upper, car.map, false, {}}, {car.board[0] + ox, car.board[1] + oy});
        }
    }
}

} // namespace picarat::adventure4
