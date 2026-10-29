// The sixth adventure's Nut Roller: a squirrel on a track around a field
// throws the walnut, acorns and rocks into it; they roll until stopped and
// the walnut must come to rest in the tent. See a6_nut_roller.hpp for the
// rules; the routines cited are lt6CLoopMiniRisu's (sixth adventure code).
#include "minigames/a6_nut_roller.hpp"
#include "import/a56_formats.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace picarat::adventure6 {

using adventure5::Game;
using adventure56::Object3D;
using adventure56::Resource;
using xq::Value;

namespace {

constexpr int kDx[4] = {0, 0, -1, 1}; // 0x3da328
constexpr int kDy[4] = {-1, 1, 0, 0}; // 0x3da338
constexpr float kPi = 3.14159274f, kTwoPi = 6.28318548f;
// The squirrel's turn per direction (0x42498c: about y).
constexpr float kFacing[4] = {kPi, 0.0f, -1.57079637f, 1.57079637f};
// The class's object lists per grid object (+0x1c54 .. +0x1de0).
constexpr int kPool[NutRoller::ObjectKinds] = {0, 1, 1, 1, 8, 16, 32, 8, 16, 16};
constexpr const char* kResources[NutRoller::ObjectKinds] = {
    nullptr,   "res_risu",   "res_goal", "res_kurumi", "res_kinomi",
    "res_iwa", "res_bridge", "res_tree", "res_ueki",   "res_take"};
constexpr int kSplashes = 4;

// A cell's centre on the ground at a height (0x2c6a50 / 0x2c33a8: 20 per
// cell, 10 per height level).
std::array<float, 3> cellPoint(int x, int y, int h) {
    return {10.0f + float(x) * 20.0f, float(h) * 10.0f, 10.0f + float(y) * 20.0f};
}

std::array<float, 3> step(const std::array<float, 3>& p, int dir) {
    return {p[0] + float(kDx[dir]) * 20.0f, p[1], p[2] + float(kDy[dir]) * 20.0f};
}

bool isLowCurve(int t) {
    return t >= 3 && t <= 6;
}

bool isHighCurve(int t) {
    return t >= 10 && t <= 13;
}

// The directions a curve's ramp leads up from (0x2c70d4): types 3 / 10
// down and right, 4 / 11 down and left, 5 / 12 up and right, 6 / 13 up and
// left.
bool rampLeads(int curve, int dir) {
    switch (curve) {
    case 0:
        return dir == 1 || dir == 3;
    case 1:
        return dir == 1 || dir == 2;
    case 2:
        return dir == 0 || dir == 3;
    default:
        return dir == 0 || dir == 2;
    }
}

// Hamilton product a * b (0x28a7c0), quaternions as x, y, z, w.
std::array<float, 4> multiply(const std::array<float, 4>& a, const std::array<float, 4>& b) {
    return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
            a[3] * b[1] + a[1] * b[3] + a[2] * b[0] - a[0] * b[2],
            a[3] * b[2] + a[2] * b[3] + a[0] * b[1] - a[1] * b[0],
            a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}

// The rotation matrix of a quaternion (0x2827a0), rows of the usual
// column-vector matrix.
std::array<float, 9> rotationOf(const std::array<float, 4>& q) {
    const float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    const float s = n > 0.0f ? 2.0f / n : 0.0f;
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    return {1 - s * (y * y + z * z), s * (x * y - w * z),     s * (x * z + w * y),
            s * (x * y + w * z),     1 - s * (x * x + z * z), s * (y * z - w * x),
            s * (x * z - w * y),     s * (y * z + w * x),     1 - s * (x * x + y * y)};
}

// The turns about x, y and z that the port's objects compose (Rx Ry Rz)
// into the same matrix.
std::array<float, 3> anglesOf(const std::array<float, 9>& r) {
    const float sy = std::clamp(r[2], -1.0f, 1.0f);
    const float y = std::asin(sy);
    if (std::fabs(sy) > 0.99999f) {
        return {std::atan2(r[7], r[4]), y, 0.0f};
    }
    return {std::atan2(-r[5], r[8]), y, std::atan2(-r[1], r[0])};
}

int intOf(const import::adventure56::Operand& o) {
    switch (o.type & 15) {
    case 3: {
        float f;
        std::memcpy(&f, &o.value, sizeof f);
        return int(f);
    }
    default:
        return std::int32_t(o.value);
    }
}

float realOf(const import::adventure56::Operand& o) {
    if ((o.type & 15) == 3) {
        float f;
        std::memcpy(&f, &o.value, sizeof f);
        return f;
    }
    return float(std::int32_t(o.value));
}

} // namespace

NutRoller::NutRoller(Game& game) : game_(game) {}

// ---------------------------------------------------------------- objects

Object3D* NutRoller::object(std::uint32_t hash) const {
    return hash ? game_.scene.object3D(hash) : nullptr;
}

// 0x2c4b78: "obj_risu" for the squirrel, "obj_risu_<object>_<i>" for the
// others.
std::uint32_t NutRoller::poolName(Object type, int index, std::string* name) {
    char text[32];
    if (type == Squirrel) {
        std::snprintf(text, sizeof text, "obj_risu");
    } else {
        std::snprintf(text, sizeof text, "obj_risu_%d_%d", int(type), index);
    }
    if (name) {
        *name = text;
    }
    return xq::hash(text);
}

// 0x27e794: a model motion by name; looping unless played once (flag 8).
// A model without that motion keeps its own.
void NutRoller::playMotion(std::uint32_t hash, const char* motion, bool once) {
    Object3D* o = object(hash);
    if (!o || !o->model || !o->model->motion(motion)) {
        return;
    }
    if (o->motion != motion || o->motionDone) {
        o->motionTime = 0;
    }
    o->motion = motion;
    o->motionOnce = once;
    o->motionDone = false;
    o->motionStops = false;
}

// 0x257e60: the position tween (linear).
void NutRoller::moveTo(std::uint32_t hash, const std::array<float, 3>& to, float frames) {
    if (Object3D* o = object(hash)) {
        o->position.start(to, frames, 0);
    }
}

// 0x2c42c4: the last hidden splash shown with its "hit" played once.
void NutRoller::splash(int x, int y, int h) {
    std::uint32_t chosen = 0;
    for (int i = 0; i < kSplashes; ++i) {
        char name[24];
        std::snprintf(name, sizeof name, "obj_water_%d", i);
        if (const Object3D* o = object(xq::hash(name)); o && !o->visible) {
            chosen = xq::hash(name);
        }
    }
    if (Object3D* o = object(chosen)) {
        o->visible = true;
        playMotion(chosen, "hit", true);
        o->position.set(cellPoint(x, y, h));
    }
}

// 0x2c57d8 (2110 100): four splashes of "res_water", hidden.
void NutRoller::createSplashes() {
    const Resource* r = game_.scene.resource(xq::hash("res_water"), Resource::Kind::Model);
    if (!r || !r->model) {
        return;
    }
    for (int i = 0; i < kSplashes; ++i) {
        char name[24];
        std::snprintf(name, sizeof name, "obj_water_%d", i);
        game_.scene.remove3D(xq::hash(name));
        Object3D& o = game_.scene.create3D(name, 4, 0x6e, 2);
        o.model = r->model;
        o.visible = false;
    }
}

// 0x2c596c (2110 9): one merged object drawing a shadow mesh per piece,
// the unused ones far away.
void NutRoller::createShadows() {
    game_.scene.remove3D(xq::hash("obj_shadow"));
    const Resource* r = game_.scene.resource(xq::hash("res_shadow"), Resource::Kind::Model);
    if (r && r->model) {
        Object3D& o = game_.scene.create3D("obj_shadow", 4, 0x6e, 2);
        auto merged = std::make_shared<Object3D::Merged>();
        merged->model = r->model;
        merged->positionsOnly = true;
        merged->instances.assign(kMaxPieces, {});
        const auto& meshes = r->model->meshes;
        int mesh = 0;
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            if (meshes[i].name == "mg331a_output.skin1") {
                mesh = int(i);
            }
        }
        for (auto& in : merged->instances) {
            in.mesh = mesh;
            in.position[0] = 10000.0f;
        }
        merged->added = merged->instances.size();
        merged->built = true;
        o.merged = merged;
    }
    updateShadows();
}

// 0x228840: each piece's shadow under it, a frame's travel ahead while it
// moves (its own step speed), held at the cell's surface except on curves;
// none for the held ball and hidden pieces.
void NutRoller::updateShadows() {
    Object3D* shadow = object(xq::hash("obj_shadow"));
    if (!shadow || !shadow->merged) {
        return;
    }
    auto& instances = shadow->merged->instances;
    const Piece* s = squirrel();
    for (std::size_t i = 0; i < pieces_.size() && i < instances.size(); ++i) {
        const Piece& p = pieces_[i];
        const Object3D* o = object(p.object);
        float* at = instances[i].position;
        if (!o || !o->visible || (s && s->held == int(i))) {
            at[0] = 10000.0f, at[1] = 0.0f, at[2] = 0.0f;
            continue;
        }
        std::array<float, 3> pos = o->position.value();
        if (o->position.moving() || o->offset.moving()) {
            const float frames =
                float(p.isBall() && p.rolling ? timing_.rollFrames : timing_.walkFrames);
            const float speed = 20.0f / frames;
            if (p.dir >= 0 && p.dir < 4) {
                pos[0] += float(kDx[p.dir]) * speed;
                pos[2] += float(kDy[p.dir]) * speed;
            }
        }
        const int type = stage_.kindOf(p.x, p.y).type;
        const float top = float(stage_.at(p.x, p.y).height) * 10.0f;
        if (!isLowCurve(type) && !isHighCurve(type) && top <= pos[1]) {
            pos[1] = top;
        }
        std::copy(pos.begin(), pos.end(), at);
    }
}

// 0x228ce4 (each frame, 0x2c3070): a ball shows its rolling turn.
void NutRoller::showTurn(const Piece& p) {
    if (Object3D* o = object(p.object)) {
        o->rotation3.set(anglesOf(p.matrix));
    }
}

// ---------------------------------------------------------------- camera

// 0x228afc: a stage view over frames (curve 3).
void NutRoller::cameraView(int which, float frames) {
    const View& v = stage_.views[which ? 1 : 0];
    auto& cam = game_.scene.cameras[4];
    cam.eye.start(v.eye, frames, 3);
    cam.target.start(v.target, frames, 3);
    cam.fov.start({v.fov}, frames, 3);
    cam.stereoDistance.start({v.stereoDistance}, frames, 3);
    cam.stereoStrength.start({v.stereoStrength}, frames, 3);
    nearZ_.start({v.nearZ}, frames, 3);
    farZ_.start({v.farZ}, frames, 3);
    cameraMode_ = which ? 1 : 0;
}

// 0x2c4888: the camera starts following a ball, keeping the eye's
// direction from the target at the following distance.
void NutRoller::startFollowing(int piece) {
    if (followed_ < 0) {
        followStarts_ = true;
    }
    followed_ = piece;
    const auto& cam = game_.scene.cameras[4];
    std::array<float, 3> d;
    float length = 0;
    for (int i = 0; i < 3; ++i) {
        d[std::size_t(i)] = cam.eye[i] + cam.eyeOffset[i] - cam.target[i] - cam.targetOffset[i];
        length += d[std::size_t(i)] * d[std::size_t(i)];
    }
    length = std::sqrt(length);
    for (float& c : d) {
        c = length > 0.0f ? c / length * float(timing_.followDistance) : 0.0f;
    }
    followOffset_ = d;
}

// 0x2c43d0: each rolling frame the camera heads for the cell ahead of the
// ball, kept inside the grid's margins.
void NutRoller::follow() {
    if (followed_ < 0 || std::size_t(followed_) >= pieces_.size()) {
        return;
    }
    const Piece& p = pieces_[std::size_t(followed_)];
    if (!object(p.object)) {
        followStarts_ = false;
        return;
    }
    const int dir = std::clamp(p.dir, 0, 3);
    int cx = kDx[dir] + p.x;
    if (timing_.marginX > cx) {
        cx = timing_.marginX - 1;
    }
    cx = std::min(cx, stage_.width - timing_.marginX);
    int cy = kDy[dir] + p.y;
    if (cy <= timing_.marginY) {
        cy = timing_.marginY - 1;
    }
    cy = std::min(cy, stage_.height - timing_.marginY);
    const std::array<float, 3> target = cellPoint(cx, cy, p.h);
    const std::array<float, 3> eye{target[0] + followOffset_[0], target[1] + followOffset_[1],
                                   target[2] + followOffset_[2]};
    auto& cam = game_.scene.cameras[4];
    const float frames = float(timing_.followFrames);
    if (followStarts_) {
        cam.eye.start(eye, frames, 3);
        cam.target.start(target, frames, 3);
        cam.stereoDistance.start({stage_.followStereo}, frames, 0);
        nearZ_.start({stage_.followNear}, frames, 0);
        farZ_.start({stage_.followFar}, frames, 0);
    } else {
        cam.eye.start(eye, frames, 0);
        cam.target.start(target, frames, 0);
    }
    followStarts_ = false;
}

// The camera's near and far distances move as tweens in the game (view
// vtable +0x54 / +0x48); the port's camera keeps plain values.
void NutRoller::stepCameraClip() {
    const float frames = float(std::max(1, game_.displayFramesPerFrame));
    nearZ_.step(frames);
    farZ_.step(frames);
    auto& cam = game_.scene.cameras[4];
    if (nearZ_[0] > 0.0f) {
        cam.nearZ = nearZ_[0];
    }
    if (farZ_[0] > 0.0f) {
        cam.farZ = farZ_[0];
    }
}

// ---------------------------------------------------------------- stage

// 0x2c4614: res/sub/risu_stage_NN.xs read with the stage commands (table
// 0x424914: 8020..8026, handlers 0x173cf0 ...).
bool NutRoller::loadStage(int number) {
    char path[48];
    std::snprintf(path, sizeof path, "res/sub/risu_stage_%02d.xs", number);
    const auto file = game_.pack.find(path, "");
    if (file.empty()) {
        return false;
    }
    const auto list = import::adventure56::parseXscr(game_.pack.read(file));
    stage_ = {};
    int kind = 1, x = 0, y = 0;
    for (const auto& c : list.commands) {
        auto op = [&](int i) -> const import::adventure56::Operand& {
            return list.operands[std::size_t(c.first + i)];
        };
        switch (c.opcode) {
        case 8020:
            if (c.count >= 2) {
                stage_.width = std::clamp(intOf(op(0)) & 0xff, 0, kMaxColumns);
                stage_.height = std::clamp(intOf(op(1)) & 0xff, 0, kMaxRows);
            }
            break;
        case 8022:
            if (c.count >= 5 && kind < int(stage_.kinds.size())) {
                Kind& k = stage_.kinds[std::size_t(kind++)];
                k.type = std::uint8_t(intOf(op(0)));
                for (int d = 0; d < 4; ++d) {
                    k.blocked[d] = (intOf(op(1 + d)) & 0xff) != 0;
                }
            }
            break;
        case 8023: {
            if (c.count < 3 || y >= kMaxRows || stage_.width == 0) {
                break;
            }
            Cell& cell = stage_.at(x, y);
            const int object = intOf(op(1));
            cell.kind = std::uint8_t(intOf(op(0)));
            cell.height = std::uint8_t(intOf(op(2)));
            if (object == Squirrel || object == Walnut || object == Acorn || object == Rock) {
                cell.piece = std::uint8_t(object);
            } else {
                cell.fixture = std::uint8_t(object);
            }
            if (object >= 0 && object < int(stage_.counts.size())) {
                ++stage_.counts[std::size_t(object)];
            }
            if (++x >= stage_.width) {
                x = 0;
                ++y;
            }
            break;
        }
        case 8024:
        case 8025:
            if (c.count >= 11) {
                View& v = stage_.views[c.opcode - 8024];
                for (int i = 0; i < 3; ++i) {
                    v.eye[std::size_t(i)] = realOf(op(i));
                    v.target[std::size_t(i)] = realOf(op(3 + i));
                }
                if (c.opcode == 8025) {
                    v.eye[2] += 1.0f; // 0x176700
                }
                v.fov = realOf(op(6)) * 0.0174533f;
                v.stereoDistance = realOf(op(7));
                v.stereoStrength = realOf(op(8));
                v.nearZ = realOf(op(9));
                v.farZ = realOf(op(10));
            }
            break;
        case 8026:
            if (c.count >= 3) {
                stage_.followStereo = realOf(op(0));
                stage_.followNear = realOf(op(1));
                stage_.followFar = realOf(op(2));
            }
            break;
        default:
            break;
        }
    }
    return stage_.width > 0;
}

// 0x2c4b78: the course's objects - made fresh, except the tent, hedges and
// bamboo kept from before - then put where the grid has them.
void NutRoller::createObjects() {
    pieces_.clear();
    squirrel_ = -1;
    for (int t = Squirrel; t < ObjectKinds; ++t) {
        const Object type = Object(t);
        const Resource* r = game_.scene.resource(xq::hash(kResources[t]), Resource::Kind::Model);
        // The tree list is as long as there are acorns to grow trees.
        const int count = std::min(kPool[t], stage_.counts[std::size_t(type == Tree ? Acorn : t)]);
        if (type == Tree) {
            trees_ = count;
        }
        if (!r || !r->model) {
            continue;
        }
        for (int i = 0; i < count; ++i) {
            std::string name;
            const std::uint32_t hash = poolName(type, i, &name);
            if ((type == Tent || type == Hedge || type == Bamboo) && object(hash)) {
                continue;
            }
            game_.scene.remove3D(hash);
            Object3D& o = game_.scene.create3D(name, 4, 0x6e, 3);
            o.model = r->model;
            if (type == Tree) {
                o.drawn = false;
            } else if (type == Squirrel) {
                playMotion(hash, "wait1", false);
            }
        }
    }
    int used[ObjectKinds] = {};
    for (int y = 0; y < stage_.height; ++y) {
        for (int x = 0; x < stage_.width; ++x) {
            Cell& cell = stage_.at(x, y);
            if (cell.piece != None && used[cell.piece] < kPool[cell.piece]) {
                const Object type = Object(cell.piece);
                const std::uint32_t hash = poolName(type, used[cell.piece]++);
                if (object(hash)) {
                    addPiece(type, x, y, cell.height, hash);
                }
            }
            cell.fixtureObject = 0;
            if (cell.fixture != None && cell.fixture < ObjectKinds &&
                used[cell.fixture] < kPool[cell.fixture]) {
                const Object type = Object(cell.fixture);
                const std::uint32_t hash = poolName(type, used[cell.fixture]++);
                if (Object3D* o = object(hash)) {
                    cell.fixtureObject = hash;
                    auto at = cellPoint(x, y, cell.height);
                    if (type == Bridge) {
                        at[1] -= 10.0f; // +0x1e40
                    }
                    o->position.set(at);
                    if (type == Bridge && stage_.kindOf(x, y).type == BridgeAcross) {
                        o->rotation3.set({0.0f, 1.57079637f, 0.0f});
                    }
                }
            }
        }
    }
    updateShadows();
}

// 0x2c5528 with the pieces' set-ups: the squirrel (0x2c33a8) facing down;
// a ball (0x2c6a50) resting, its centre 10 above its place.
void NutRoller::addPiece(Object type, int x, int y, int h, std::uint32_t hash) {
    if (pieces_.size() >= std::size_t(kMaxPieces)) {
        return;
    }
    Piece p;
    p.type = type;
    p.object = hash;
    p.x = x, p.y = y, p.h = h;
    Object3D* o = object(hash);
    o->position.set(cellPoint(x, y, h));
    playMotion(hash, "wait", false);
    if (type != Squirrel) {
        o->offset.set({0.0f, 10.0f, 0.0f});
    } else {
        squirrel_ = int(pieces_.size());
    }
    pieces_.push_back(p);
}

// ---------------------------------------------------------------- rules

bool NutRoller::inGrid(int x, int y) const {
    return x >= 0 && y >= 0 && x < stage_.width && y < stage_.height;
}

NutRoller::Piece* NutRoller::squirrel() {
    return squirrel_ >= 0 ? &pieces_[std::size_t(squirrel_)] : nullptr;
}

const NutRoller::Piece* NutRoller::squirrel() const {
    return squirrel_ >= 0 ? &pieces_[std::size_t(squirrel_)] : nullptr;
}

// 0x250fd4: not fading (the alpha tween at the model's +0x94). The steps
// only ever ask for alpha 1, so this holds; kept as the game asks it.
bool NutRoller::atRest(const Piece& p) const {
    const Object3D* o = object(p.object);
    return o && !o->alpha.moving();
}

bool NutRoller::shown(const Piece& p) const {
    const Object3D* o = object(p.object);
    return o && o->visible;
}

// 0x2285e8: where a ball arriving at a cell comes to rest: the cell's
// height, or on top of a ball lying there.
int NutRoller::heightAt(int x, int y, const Piece* except) const {
    if (!inGrid(x, y)) {
        return 0x7fff;
    }
    int h = stage_.at(x, y).height;
    for (const Piece& q : pieces_) {
        if (&q != except && q.isBall() && shown(q) && atRest(q) && q.x == x && q.y == y) {
            h = std::max(h, q.h + 2);
        }
    }
    return h;
}

// 0x2c70d4: whether a ball can roll on in a direction (1), pushes the
// ball ahead instead (4), or is stopped (0). Fixtures hit play "hit".
unsigned NutRoller::ballCanMove(Piece& p, int dir, bool test) {
    if (dir < 0 || dir > 3) {
        return 0;
    }
    const int nx = p.x + kDx[dir], ny = p.y + kDy[dir];
    // Trees grown from acorns stand in the way.
    for (int i = 0; i < treesGrown_; ++i) {
        const std::uint32_t tree = poolName(Tree, i);
        if (const Object3D* o = object(tree);
            o && int(o->position[0] * 0.05f) == nx && int(o->position[2] * 0.05f) == ny) {
            if (!test) {
                playMotion(tree, "hit", false);
            }
            return 0;
        }
    }
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        Piece& q = pieces_[i];
        if (q.x != nx || q.y != ny || q.h != p.h) {
            continue;
        }
        if (object(q.object) && q.isBall() && shown(q)) {
            if (test) {
                return 0;
            }
            q.dir = p.dir;
            q.rolling = true;
            if (cameraMode_ == 0) {
                startFollowing(int(i));
            }
            return 4;
        }
        break;
    }
    if (!inGrid(nx, ny)) {
        return 0;
    }
    const int h = heightAt(nx, ny, &p);
    const int type = stage_.kindOf(nx, ny).type;
    if (h > p.h) {
        const int rise = h - p.h;
        const bool low = rise < 3 && isLowCurve(type) && rampLeads(type - 3, dir);
        const bool high = rise < 5 && isHighCurve(type) && rampLeads(type - 10, dir);
        if (!low && !high) {
            return 0;
        }
    }
    const Cell& next = stage_.at(nx, ny);
    if (next.fixture == None || next.fixture == Tent || next.fixture == Bridge) {
        if (type == BridgeAcross && (dir == 2 || dir == 3)) {
            return 0;
        }
        if (type == BridgeAlong && dir < 2) {
            return 0;
        }
        return stage_.kindOf(p.x, p.y).blocked[dir] ? 0 : 1;
    }
    if (next.fixtureObject && !test) {
        playMotion(next.fixtureObject, "hit", false);
    }
    return 0;
}

// 0x2c66a4: a ball's step: a curve it lies in turns it, then it rolls on
// if it can (a bridge it leaves collapses: event 8), tinted under the tent.
unsigned NutRoller::ballMove(Piece& p, int dir, bool test) {
    if (dir < 0 || dir > 3) {
        p.rolling = false;
        return 0;
    }
    Cell& here = stage_.at(p.x, p.y);
    if (here.height > p.h) {
        switch (stage_.kindOf(p.x, p.y).type) {
        case 3:
        case 10:
            dir = dir == 3 ? 0 : dir == 1 ? 2 : dir;
            break;
        case 4:
        case 11:
            dir = dir == 2 ? 0 : dir == 1 ? 3 : dir;
            break;
        case 5:
        case 12:
            dir = dir == 3 ? 1 : dir == 0 ? 2 : dir;
            break;
        case 6:
        case 13:
            dir = dir == 2 ? 1 : dir == 0 ? 3 : dir;
            break;
        default:
            break;
        }
    }
    p.dir = dir;
    unsigned r = ballCanMove(p, dir, test);
    if (!(r & 1)) {
        p.rolling = false;
        return r;
    }
    if (!test) {
        if (here.fixture == Bridge) {
            here.fixture = None;
            if (here.fixtureObject) {
                playMotion(here.fixtureObject, "break", true);
                splash(p.x, p.y, p.h - 1);
                r |= 8;
            }
            here.kind = 0;
            here.height = std::uint8_t(here.height - 1);
        }
        if (Object3D* o = object(p.object)) {
            o->position.start(step(o->position.value(), dir), float(timing_.rollFrames), 0);
            const bool tent = stage_.at(p.x + kDx[dir], p.y + kDy[dir]).fixture == Tent;
            const float shade = tent ? 0.7f : 1.0f;
            o->colour.start({shade, shade, shade}, float(timing_.rollFrames), 0);
            o->alpha.start({1.0f}, float(timing_.rollFrames), 0);
        }
    }
    p.x += kDx[dir];
    p.y += kDy[dir];
    return r;
}

// The ends a ball comes to (vtable +0x20 sinking into a hole, +0x24 crushed,
// +0x28 into water): a rock fills the hole (0x2c6494); an acorn grows a
// tree there (0x2c7c34); the walnut stuck, crushed or in water fails the
// course (0x2c7d6c, 0x2c7dd0, 0x2c7dac).
unsigned NutRoller::ballSinks(Piece& p) {
    Cell& cell = stage_.at(p.x, p.y);
    switch (p.type) {
    case Rock:
        cell.kind = 1;
        return 0;
    case Walnut:
        cell.kind = 1;
        failed_ = true;
        return 0;
    case Acorn: {
        if (Object3D* o = object(p.object)) {
            o->drawn = false;
        }
        if (treesGrown_ < trees_) {
            const std::uint32_t tree = poolName(Tree, treesGrown_);
            if (Object3D* t = object(tree)) {
                t->visible = true;
                t->position.set(cellPoint(p.x, p.y, cell.height));
                playMotion(tree, "open", false);
            }
            ++treesGrown_;
        }
        cell.kind = 1;
        return 0x10;
    }
    default:
        return 0;
    }
}

void NutRoller::ballInWater(Piece& p) {
    if (p.type == Walnut) {
        failed_ = true;
    }
}

void NutRoller::walnutCrushed(Piece& p) {
    playMotion(p.object, "break", true);
    failed_ = true;
}

// 0x2c6b14: a ball's frame: the end of a sink or stop, crushing the walnut
// under it, falling to its rest height, water and holes, and else (when
// rolling) the next step. Events: 2 fell, 4 crushed, 8 into a hole, 0x10
// into water, 0x40 a tree grew; from the step 1 moved, 0x20 pushed, 0x80
// a bridge broke.
unsigned NutRoller::ballSettle(Piece& p, bool resting, bool test) {
    unsigned events = 0;
    p.changed = false;
    if (p.sinking) {
        const unsigned r = ballSinks(p);
        p.rolling = false;
        p.sinking = false;
        return (r & 0x10) ? 0x40 : 0;
    }
    if (p.stopping) {
        p.rolling = false;
        p.stopping = false;
        return 0;
    }
    if (p.type == Rock || p.type == Acorn) {
        for (Piece& q : pieces_) {
            if (&q != &p && q.type == Walnut && shown(q) && atRest(q) && !q.rolling && q.x == p.x &&
                q.y == p.y && q.h < p.h) {
                p.changed = true;
                walnutCrushed(q);
                events |= 4;
                break;
            }
        }
    }
    int h = heightAt(p.x, p.y, &p);
    const int type = stage_.kindOf(p.x, p.y).type;
    if (isLowCurve(type) || isHighCurve(type)) {
        bool under = false;
        for (const Piece& q : pieces_) {
            if (&q != &p && shown(q) && atRest(q) && q.x == p.x && q.y == p.y && q.h < p.h) {
                under = true;
                break;
            }
        }
        if (!under) {
            h -= isLowCurve(type) ? 2 : 4;
        }
    }
    Cell& here = stage_.at(p.x, p.y);
    if (p.h > h) {
        p.changed = true;
        int drop = p.h - h;
        p.h = h;
        if (here.kind == 0) {
            ++drop;
            p.h = h - 1;
        }
        if (!test) {
            if (Object3D* o = object(p.object)) {
                auto to = o->position.value();
                to[1] = float(p.h) * 10.0f;
                const int frames = (int(float(drop) * 0.2f) + 1) * timing_.fallFrames;
                o->position.start(to, float(frames), 0);
            }
        }
        return events | 2;
    }
    if (here.kind == 0) {
        if (!test) {
            if (Object3D* o = object(p.object); o && o->visible) {
                ballInWater(p);
                o->visible = false;
                splash(p.x, p.y, p.h + 1);
            }
        }
        p.rolling = false;
        return events | 0x10;
    }
    if (type == Hole) {
        p.h -= 2;
        if (!test) {
            if (Object3D* o = object(p.object)) {
                auto to = o->position.value();
                to[1] -= 20.0f;
                o->position.start(to, float(timing_.fallFrames), 0);
            }
        }
        p.sinking = true;
        return events | 8;
    }
    if (!resting) {
        const unsigned r = ballMove(p, p.dir, test);
        events |= ((r & 4) ? 0x20u : 0u) | ((r & 8) ? 0x80u : 0u) | (r & 1);
    }
    return events;
}

// 0x2c64c4: while a ball's move runs it turns about the axis across its
// way; 2 when it changed this frame instead.
unsigned NutRoller::ballSpin(Piece& p) {
    if (p.changed) {
        return 2;
    }
    unsigned r = 0;
    const Object3D* o = object(p.object);
    if (!o) {
        return 0;
    }
    if (o->position.moving() || o->offset.moving()) {
        const float a = timing_.spin;
        std::array<float, 3> axis{1.0f, 0.0f, 0.0f};
        float angle = 0.0f;
        switch (p.dir) {
        case 0:
            angle = -a;
            break;
        case 1:
            angle = a;
            break;
        case 2:
            axis = {0.0f, 0.0f, 1.0f};
            angle = a;
            break;
        case 3:
            axis = {0.0f, 0.0f, 1.0f};
            angle = -a;
            break;
        default:
            break;
        }
        if (p.dir >= 0 && p.dir < 4) {
            // 0x2517c0: the turn as a quaternion.
            const float s = std::sin(angle * 0.5f), c = std::cos(angle * 0.5f);
            p.turn = multiply({axis[0] * s, axis[1] * s, axis[2] * s, c}, p.turn);
            p.matrix = rotationOf(p.turn);
        }
        r = 1;
    }
    showTurn(p);
    return r;
}

// 0x2c7b24: the squirrel walks only along the track, at its height, onto
// a cell nothing stands on.
bool NutRoller::squirrelCanMove(const Piece& s, int dir) const {
    if (dir < 0 || dir > 3) {
        return false;
    }
    const int tx = s.x + kDx[dir], ty = s.y + kDy[dir];
    if (!inGrid(tx, ty) || heightAt(tx, ty, &s) != s.h) {
        return false;
    }
    for (const Piece& q : pieces_) {
        if (q.x == tx && q.y == ty && q.h == s.h) {
            if (object(q.object) && shown(q)) {
                return false;
            }
            break;
        }
    }
    return stage_.kindOf(tx, ty).type == Track;
}

// 0x2c7564: a step the way it faces (1 when it walks), else a turn to face
// that way (2), the shorter way round.
unsigned NutRoller::squirrelStep(Piece& s, int dir) {
    if (dir < 0 || dir > 3) {
        return 0;
    }
    unsigned r = 0;
    Object3D* o = object(s.object);
    if (s.dir == dir) {
        r = squirrelCanMove(s, dir) ? 1 : 0;
        if (r & 1) {
            if (o) {
                o->position.start(step(o->position.value(), dir), float(timing_.walkFrames), 0);
            }
            s.x += kDx[dir];
            s.y += kDy[dir];
            if (s.held >= 0) {
                pieces_[std::size_t(s.held)].x += kDx[dir];
                pieces_[std::size_t(s.held)].y += kDy[dir];
            }
            playMotion(s.object, s.held >= 0 ? "walk2" : "walk1", false);
        }
    } else {
        r = 2;
        if (o) {
            // 0x209f8c: each angle brought within half a turn of the goal.
            std::array<float, 3> from = o->rotation3.value();
            const std::array<float, 3> to{0.0f, kFacing[dir], 0.0f};
            for (int i = 0; i < 3; ++i) {
                const float d = to[std::size_t(i)] - from[std::size_t(i)];
                if (std::fabs(d) > kPi) {
                    from[std::size_t(i)] += d < 0.0f ? -kTwoPi : kTwoPi;
                }
            }
            o->rotation3.set(from);
            o->rotation3.start(to, float(timing_.turnFrames), 0);
        }
    }
    s.dir = dir;
    return r;
}

NutRoller::Piece* NutRoller::pieceAt(int x, int y, int h) {
    for (Piece& q : pieces_) {
        if (q.x == x && q.y == y && q.h == h) {
            return &q;
        }
    }
    return nullptr;
}

// 0x2c793c: the ball ahead at the squirrel's height picked up (it rides
// on the bone "tgt01"); 3 walnut, 4 acorn, 5 rock, 0 nothing.
int NutRoller::pickUp(Piece& s) {
    if (s.dir < 0 || s.dir > 3) {
        return 0;
    }
    Piece* q = pieceAt(s.x + kDx[s.dir], s.y + kDy[s.dir], s.h);
    if (!q || !q->isBall()) {
        return 0;
    }
    q->x = s.x;
    q->y = s.y;
    s.held = int(q - pieces_.data());
    if (Object3D* o = object(q->object)) {
        o->position.set({0.0f, -10.0f, 0.0f});
        o->parent = s.object;
        o->parentBone = xq::hash("tgt01");
    }
    playMotion(s.object, "pickup", true);
    return q->type == Walnut ? 3 : q->type == Acorn ? 4 : 5;
}

// 0x2c76d8: the held ball put on the cell ahead at the same height (0), or
// thrown down into the field (1; from 16 above the cell ahead, the scripts
// fly it there); 2 the side is blocked, 4 the cell ahead is higher.
int NutRoller::putDown(Piece& s, std::array<float, 3>& throwFrom) {
    if (s.dir < 0 || s.dir > 3 || s.held < 0) {
        return 0;
    }
    const int tx = s.x + kDx[s.dir], ty = s.y + kDy[s.dir];
    if (!inGrid(tx, ty) || s.h < heightAt(tx, ty, &s)) {
        return 4;
    }
    if (stage_.kindOf(s.x, s.y).blocked[s.dir]) {
        return 2;
    }
    const Cell& target = stage_.at(tx, ty);
    if (target.height > s.h) {
        return 0;
    }
    Piece& ball = pieces_[std::size_t(s.held)];
    ball.x = tx;
    ball.y = ty;
    const Object3D* so = object(s.object);
    const std::array<float, 3> ahead =
        so ? step(so->position.value(), s.dir) : cellPoint(tx, ty, s.h);
    if (target.height == s.h) {
        if (Object3D* o = object(ball.object)) {
            o->position.set(ahead);
        }
        s.held = -1;
        playMotion(s.object, "wait1", false);
        return 0;
    }
    playMotion(s.object, "throw", true);
    throwFrom = {ahead[0], ahead[1] + 16.0f, ahead[2]};
    ball.h += 2;
    ball.dir = s.dir;
    ball.rolling = true;
    const int index = s.held;
    s.held = -1;
    if (cameraMode_ == 0) {
        startFollowing(index);
    }
    return 1;
}

// 0x2c6324 (2110 20): one frame of the balls; 1 once all rest, when the
// camera goes back to the chosen view over 30 frames.
int NutRoller::simulate(unsigned& events) {
    events = 0;
    bool settled = true;
    for (Piece& p : pieces_) {
        if (!p.isBall()) {
            continue;
        }
        if (!p.rolling) {
            const unsigned r = ballSettle(p, true, false);
            p.lastEvents = p.events;
            p.events = r;
            events |= r & ~p.lastEvents;
        } else {
            const unsigned spun = ballSpin(p);
            const unsigned r = ballSettle(p, false, false);
            p.lastEvents = p.events;
            p.events = r;
            settled = false;
            events |= spun | (r & ~p.lastEvents);
        }
    }
    if (!settled) {
        follow();
    } else {
        followed_ = -1;
        cameraView(cameraMode_, 30.0f);
    }
    return settled ? 1 : 0;
}

// 0x2c3fb4 (2110 21): the walnut lies in the tent (its "goal" plays).
bool NutRoller::goalReached() {
    for (const Piece& p : pieces_) {
        if (p.type != Walnut) {
            continue;
        }
        const Cell& cell = stage_.at(p.x, p.y);
        if (cell.fixture != Tent) {
            return false;
        }
        if (cell.fixtureObject) {
            playMotion(cell.fixtureObject, "goal", false);
        }
        return true;
    }
    return false;
}

// 0x2c4080 (2110 22): shadows; rolling balls turn; whether any piece still
// moves or turns (position or rotation tween).
bool NutRoller::anythingMoving(unsigned& events) {
    updateShadows();
    events = 0;
    for (Piece& p : pieces_) {
        if (p.isBall() && p.rolling) {
            events |= ballSpin(p);
        }
        if (const Object3D* o = object(p.object)) {
            if (o->position.moving() || o->offset.moving() || o->rotation3.moving() ||
                o->rotation3Offset.moving()) {
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------- undo

// 0x2c510c: the board, each piece's place, turn and visibility, the
// bridges standing and the trees grown.
NutRoller::Snapshot NutRoller::capture() const {
    Snapshot s;
    s.stage = stage_;
    for (const Piece& p : pieces_) {
        PieceState ps;
        ps.x = p.x, ps.y = p.y, ps.h = p.h, ps.dir = p.dir;
        ps.matrix = p.matrix;
        if (const Object3D* o = object(p.object)) {
            ps.position = o->position.value();
            ps.rotation = o->rotation3.value();
            ps.visible = o->visible;
        }
        s.pieces.push_back(ps);
    }
    for (int i = 0; i < kPool[Bridge]; ++i) {
        const Object3D* o = object(poolName(Bridge, i));
        // A bridge still breaking counts as gone.
        s.bridges[std::size_t(i)] = o && o->visible && !(o->motion == "break" && !o->motionDone);
    }
    s.trees = treesGrown_;
    return s;
}

// 0x2c5d30 with 0x2c3448 / 0x2c74f0: pieces back in place with their
// resting motion and plain colour, off the squirrel's hands; later trees
// gone, bridges back. (The rolling quaternion is not restored, only its
// matrix, as in the game.)
void NutRoller::restore(const Snapshot& s) {
    stage_ = s.stage;
    for (std::size_t i = 0; i < pieces_.size() && i < s.pieces.size(); ++i) {
        Piece& p = pieces_[i];
        const PieceState& ps = s.pieces[i];
        p.x = ps.x, p.y = ps.y, p.h = ps.h, p.dir = ps.dir;
        if (Object3D* o = object(p.object)) {
            o->visible = ps.visible;
            o->drawn = ps.visible;
            o->position.set(ps.position);
            o->rotation3.set(ps.rotation);
            o->colour.set({1.0f, 1.0f, 1.0f});
            o->alpha.set({1.0f});
            playMotion(p.object, "wait", false);
            o->parent = 0;
            o->parentBone = 0;
        }
        if (p.type == Squirrel) {
            p.held = -1;
        } else {
            p.matrix = ps.matrix;
            showTurn(p);
        }
    }
    for (int i = s.trees; i < treesGrown_; ++i) {
        if (Object3D* t = object(poolName(Tree, i))) {
            t->visible = false;
        }
    }
    treesGrown_ = s.trees;
    for (int i = 0; i < kPool[Bridge]; ++i) {
        const std::uint32_t bridge = poolName(Bridge, i);
        if (Object3D* o = object(bridge)) {
            o->visible = s.bridges[std::size_t(i)];
            playMotion(bridge, "wait", true);
        }
    }
}

// 0x2c510c (2110 7): the newest state goes into the ring, the present
// becomes the newest.
void NutRoller::pushUndo() {
    if (ring_.empty()) {
        ring_.assign(kUndoDepth, {});
    }
    ringCount_ = std::min(ringCount_ + 1, kUndoDepth);
    ringIndex_ = (ringIndex_ + 1) % kUndoDepth;
    ring_[std::size_t(ringIndex_)] = newest_;
    newest_ = capture();
}

// 0x2c5d30 (2110 8): back to the state before the newest.
void NutRoller::popUndo() {
    if (ringCount_ < 2 || ring_.empty()) {
        return;
    }
    const Snapshot& s = ring_[std::size_t(ringIndex_)];
    restore(s);
    newest_ = s;
    ringIndex_ = ringIndex_ == 0 ? kUndoDepth - 1 : ringIndex_ - 1;
    --ringCount_;
}

// ---------------------------------------------------------------- loop

// 0x2c3070: splashes, broken bridges and the crushed walnut vanish when
// their motion ends; the balls show their turns.
void NutRoller::update() {
    auto vanishAfter = [&](std::uint32_t hash, const char* motion) {
        Object3D* o = object(hash);
        if (o && o->visible && o->motion == motion && o->motionOnce && o->motionDone) {
            o->visible = false;
        }
    };
    for (int i = 0; i < kSplashes; ++i) {
        char name[24];
        std::snprintf(name, sizeof name, "obj_water_%d", i);
        vanishAfter(xq::hash(name), "hit");
    }
    for (int i = 0; i < kPool[Bridge]; ++i) {
        vanishAfter(poolName(Bridge, i), "break");
    }
    vanishAfter(poolName(Walnut, 0), "break");
    for (const Piece& p : pieces_) {
        if (p.isBall()) {
            showTurn(p);
        }
    }
    stepCameraClip();
}

bool NutRoller::command(int number, xq::Call& c, std::size_t first) {
    const std::size_t n = c.count() > first ? c.count() - first : 0;
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    auto out = [&](std::size_t i, const Value& v) {
        if (i < n) {
            if (Value* r = c.reg(first + i)) {
                *r = v;
            }
        }
    };
    Piece* s = squirrel();
    switch (number) {
    case 1: // the course
        if (n < 1) {
            return false;
        }
        if (loadStage(c.integer(first))) {
            createObjects();
            initial_ = stage_;
        }
        return result(1);
    case 2:
    case 102: // 0x2c5b40: the objects forgotten (the scripts' scope deletes them)
        pieces_.clear();
        squirrel_ = -1;
        followed_ = -1;
        cameraMode_ = 0;
        treesGrown_ = 0;
        trees_ = 0;
        return result(1);
    case 3:
    case 4:
        cameraView(number - 3, n > 0 ? c.real(first) : 0.0f);
        return result(1);
    case 5: // fixtures at rest; trees drawn but not shown
        for (int y = 0; y < stage_.height; ++y) {
            for (int x = 0; x < stage_.width; ++x) {
                if (const std::uint32_t f = stage_.at(x, y).fixtureObject) {
                    playMotion(f, "wait", false);
                }
            }
        }
        for (int i = 0; i < trees_; ++i) {
            if (Object3D* t = object(poolName(Tree, i))) {
                t->drawn = true;
                t->visible = false;
            }
        }
        return result(1);
    case 6: // restart
        stage_ = initial_;
        for (int i = 0; i < treesGrown_; ++i) {
            if (Object3D* t = object(poolName(Tree, i))) {
                t->visible = false;
            }
        }
        treesGrown_ = 0;
        createObjects();
        return result(1);
    case 7:
        pushUndo();
        return result(1);
    case 8:
        popUndo();
        return result(1);
    case 9:
        createShadows();
        return result(1);
    case 10: // the squirrel steps or turns
        if (n < 1) {
            return false;
        }
        return result(s ? int(squirrelStep(*s, c.integer(first))) : 0);
    case 11:
        return result(s ? pickUp(*s) : 0);
    case 12: { // put down or throw: the ball's object, where the throw starts
        std::uint32_t ball = 0;
        if (s && s->held >= 0) {
            ball = pieces_[std::size_t(s->held)].object;
        }
        std::array<float, 3> from{};
        const int r = s ? putDown(*s, from) : 0;
        out(0, Value::ofUint(ball));
        for (std::size_t i = 0; i < 3; ++i) {
            out(1 + i, Value::ofInt(int(from[i])));
        }
        return result(r);
    }
    case 13: { // what the squirrel holds
        int held = 0;
        if (s && s->held >= 0) {
            const Object t = pieces_[std::size_t(s->held)].type;
            held = t == Walnut ? 3 : t == Acorn ? 4 : 5;
        }
        return result(held);
    }
    case 14:
        if (s) {
            playMotion(s->object, s->held >= 0 ? "wait2" : "wait1", false);
        }
        return result(1);
    case 15: { // a ball ahead to pick up
        int ball = 0;
        if (s && s->dir >= 0 && s->dir < 4) {
            if (const Piece* q = pieceAt(s->x + kDx[s->dir], s->y + kDy[s->dir], s->h)) {
                ball = q->isBall() ? 1 : 0;
            }
        }
        return result(ball);
    }
    case 20: {
        unsigned events = 0;
        const int settled = simulate(events);
        out(0, Value::ofInt(int(events)));
        return result(settled);
    }
    case 21:
        return result(goalReached() ? 1 : 0);
    case 22: {
        unsigned events = 0;
        const bool moving = anythingMoving(events);
        out(0, Value::ofInt(int(events)));
        return result(moving ? 1 : 0);
    }
    case 23: { // the course failed (read once)
        const bool f = failed_;
        failed_ = false;
        return result(f ? 1 : 0);
    }
    case 30: // timing
        if (n < 8) {
            return false;
        }
        timing_.walkFrames = c.integer(first);
        timing_.turnFrames = c.integer(first + 1);
        timing_.rollFrames = c.integer(first + 2);
        timing_.fallFrames = c.integer(first + 3);
        timing_.followDistance = c.integer(first + 4);
        timing_.marginX = c.integer(first + 5);
        timing_.marginY = c.integer(first + 6);
        timing_.followFrames = c.integer(first + 7);
        if (n > 8) {
            timing_.spin = c.real(first + 8);
        }
        return result(1);
    case 100:
        createSplashes();
        return result(1);
    case 101: // 0x2c5f64: an empty undo ring
        ring_.assign(kUndoDepth, {});
        newest_ = {};
        ringCount_ = 0;
        ringIndex_ = 0;
        return result(1);
    default:
        return false;
    }
}

std::string NutRoller::describe() const {
    std::ostringstream o;
    o << "stage " << stage_.width << "x" << stage_.height << " fail " << failed_ << " camera "
      << cameraMode_ << " follow " << followed_ << " trees " << treesGrown_ << "\n";
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        const Piece& p = pieces_[i];
        o << " piece " << i << " type " << int(p.type) << " at " << p.x << "," << p.y << "," << p.h
          << " dir " << p.dir << (p.rolling ? " rolling" : "") << (p.sinking ? " sinking" : "")
          << " events " << p.events;
        if (p.held >= 0) {
            o << " holds " << p.held;
        }
        if (const Object3D* ob = object(p.object)) {
            o << " pos " << ob->position[0] << "," << ob->position[1] << "," << ob->position[2]
              << (ob->visible ? "" : " hidden");
        }
        o << "\n";
    }
    return o.str();
}

} // namespace picarat::adventure6
