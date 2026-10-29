// The hamster's walk: the 3D scene where the hamster walks the item field on
// its own (see a2_hamster_game.hpp for how the minigame plays).
//
// Hamster walk (overlay 15 020f4708 / 020f5e90 / 020f7f04 / 020f4df0) and the
// 3D actor layer it is built on (020efda0..020f3300). Floating point follows
// the overlay's binary32 soft-float calls (0203a290 add, 0203b2a4 sub,
// 0203afd4 mul, 0203aa94 div, 0203af44 int->float, 0203aed0 float->int
// truncating, 02038768/02038110 sin/cos on doubles).
#include "graphics/nds3d/fx.hpp"
#include "minigames/a2_hamster_game.hpp"
#include "puzzles/judgment.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace picarat {
using namespace nds3d;

namespace {
float i2f(int v) {
    return float(v);
}

int f2i(float v) {
    return int(v);
} // truncation toward zero (__fixsfsi)

float fbits(std::uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

fx32 mulRound(fx32 a, fx32 b) {
    return fx32((std::int64_t(a) * b + 0x800) >> 12);
}

fx32 mulTrunc(fx32 a, fx32 b) {
    return fx32((std::int64_t(a) * b) >> 12);
}

// sin / cos of degrees, through doubles like the overlay (float -> double -> float).
float sinDeg(int d) {
    return float(std::sin(double(i2f(d) * 3.14159274f / 180.0f)));
}

float cosDeg(int d) {
    return float(std::cos(double(i2f(d) * 3.14159274f / 180.0f)));
}

Bytes readBytes(const std::filesystem::path& p) {
    return readFile(p);
}

// 0206abe4: degrees -> quarter (0..3) centred on the axes.
int quarter(float degrees) {
    float v = 45.0f + degrees;
    if (v > 360.0f) {
        v -= 360.0f;
    }
    if (v < 0.0f) {
        v += 360.0f;
    }
    return f2i(v / 90.0f) & 3;
}

constexpr fx32 kAngle[4] = {0, 6433, 12867, -6433}; // 0206ac88 (02086f08)
constexpr std::array<std::pair<int, int>, 4> kDirection{
    {{0, 1}, {1, 0}, {0, -1}, {-1, 0}}}; // 020f5160 (020f8dbc)
} // namespace

// ---------------------------------------------------------------------------
// Resources (0x54-byte slots, 020eff1c / 020f023c / 020f049c / 020f0594 / 020f068c)

struct WalkResource {
    std::unique_ptr<ModelSet> model;
    std::unique_ptr<JointAnimation> joint;
    std::unique_ptr<MaterialAnimation> material;

    struct Motion {
        std::string name;
        int start = 0, end = 0, step = 0;
    };

    std::vector<Motion> motions;
    std::array<fx32, 3> boxMax{}, boxMin{}, base{};
    fx32 radius = 0, height = 0;

    bool loaded() const { return model != nullptr; }
};

// Easing value (020f072c set / 020f0794 update).
struct Ease {
    float current = 1.0f, target = 1.0f, step = 0.0f;

    void set(float t, float seconds) {
        target = t;
        if (!(seconds > 0.0f)) {
            current = target;
            step = 0.0f;
            return;
        }
        step = (target - current) / (60.0f * seconds);
    }

    void update() {
        if (step == 0.0f) {
            return;
        }
        current = current + step;
        if (step < 0.0f) {
            if (current > target) {
                return;
            }
        } else if (current < target) {
            return;
        }
        step = 0.0f;
        current = target;
    }
};

// 3D actor (020f0808): render object, motion, transform, speed and alpha eases.
struct Actor {
    WalkResource* res = nullptr;
    RenderObject object;
    std::string motion;
    int start = 0, end = 0, step = 0;
    fx32 frame = 0;
    float ratioPrev = 0, ratio = 0;
    int loops = 0;
    std::uint32_t flags = 0;
    Vec position{}, angles{}, scale{0x1000, 0x1000, 0x1000};
    std::array<fx32, 9> rotation{0x1000, 0, 0, 0, 0x1000, 0, 0, 0, 0x1000};
    Matrix world = Matrix::identity();
    Ease speed, alpha;
    int kind = -1; // +0x11c: resource slot

    bool bound() const { return res && res->loaded(); }

    void clear() { *this = Actor{}; }
};

struct HamsterGame::Walk {
    // Scene fields (0x4fc0 object).
    int state = 100, timer = 0; // +0x4e40 / +0x4e44
    int goal = 0;
    bool slim = false, cleared = false, finished = false; // +0x4e48 / +0x4e4c / +0x4e4d / +0x4e4e
    SceneKit::SpriteButton end;                           // +0x1c
    SceneKit::SpriteObject white, yellow, window, found,
        walkText; // +0x4e58 / +0x4e5c / +0x4e50 / +0x4e54 / +0x4e60
    SceneKit::SpriteObject levelNumber, levelText, bubble, icon,
        count;                              // +0x4e64 / +0x4e68 / +0x4e6c / +0x4e74 / +0x4e70
    std::array<WalkResource, 16> resources; // +0x20

    struct Camera { // +0x560
        int mode = 0;
        Matrix view = Matrix::identity(), projection = Matrix::identity(),
               combined = Matrix::identity();
        Vec eye{0, 0, 0x1000}, at{}, up{0, 0x1000, 0};
        fx32 near = 0, far = 0, top = 0, bottom = 0, left = 0, right = 0, scale = 0x1000,
             fovySin = 0, fovyCos = 0, aspect = 0;
        int distance = 4, pitch = 0x2d, yaw = 0, depth = 0x18, blendMode = 0;
        float blend1 = 1, blend2 = 1, progress = 1, zoom = 8, zoomStart = 8, zoomTarget = 0;
        Vec target{0, 0xa00, 0}, start{}, a{}, b{};
    } camera;

    Actor map; // +0x97c

    struct Hamster : Actor { // +0x71c
        fx32 baseSpeed = 0;
        int moveSpeed = 0, turnSpeed = 0, period = 0, stepSound = -1; // +0x120..+0x130
        std::array<int, 4> startVoices{-1, -1, -1, -1}, goalVoices{-1, -1, -1, -1};
        int startVoiceCount = 0, goalVoiceCount = 0; // +0x134 / +0x13c
        int state = 0, next = 0, timer = 0;
        std::uint32_t moveFlags = 0;                       // +0x148 / +0x14c / +0x154 / +0x158
        bool stop = false, smell = false, noCount = false; // +0x15c / +0x15d / +0x15e
        Vec pos{};
        fx32 facing = 0;
        int direction = 0;
        int col = 0, row = 0, steps = 0; // +0x160 / +0x16c / +0x170 / +0x174 / +0x176 / +0x178
        Vec velocity{};
        fx32 turn = 0; // +0x17c / +0x188
        Vec previous{};
        fx32 previousFacing = 0; // +0x18c
        Vec startPos{};
        fx32 startFacing = 0;
        int startDirection = 0; // +0x1a0
        int targetCol = 0, targetRow = 0;
        Vec targetPos{}; // +0x1b4 / +0x1b8
        std::array<Vec, 8> path{};
        int pathCount = 0, pathIndex = 0, previousIndex = 0; // +0x1c4 / +0x224 / +0x226 / +0x228
        int targetObject = -1, targetType = -1, collision = 0,
            radius = 0;    // +0x22a / +0x22c / +0x230 / +0x234
        int idleLimit = 0; // +0x240
    } hamster;

    struct Object : Actor {
        int type = 0, value = 0;
        int col = -1, row = -1;
        bool used = false;
        int timer = -1;
    }; // +0x120..+0x130

    std::array<Object, 48> objects;
    int objectCount = 0; // +0xa9c / +0x445c

    struct Effect : Actor {
        bool active = false;
    };

    std::array<Effect, 8> effects;             // +0x4460
    std::array<std::array<int, 8>, 6> grid{};  // +0x4d80 object index per cell
    std::array<std::array<int, 8>, 6> scent{}; // +0x4de0
    std::vector<std::string> messages;
    int messageCount = 0;                        // +0x4e78 / +0x4f18 / +0x4fb8
    int foundTimer = -1, foundX = 0, foundY = 0; // +0x4fba / +0x4fbc / +0x4fbe
    int flash = 0;         // ov2 02088a10: main-screen brightness of the 1200 flash
    bool skipTail = false; // end button: 020f5e90 returns before its common tail
    Gpu3D gpu;
    GlobalState glb;
    Renderer renderer;
    VramAllocator vram;
};

int HamsterGame::walkState() const {
    return walk_ ? walk_->state : 0;
}

namespace {
using Walk = HamsterGame::Walk;

// --- Resources -----------------------------------------------------------
void loadResource(WalkResource& r, const std::filesystem::path& root, const std::string& name,
                  Walk& w) {
    r = WalkResource{};
    const auto dir = root / "chara" / name;
    r.model = std::make_unique<ModelSet>(readBytes(dir / (name + ".nsbmd")));
    r.model->setup(w.gpu, w.vram);
    // 020f023c: bounding box from the model info (scaled by boxPosScale).
    const auto& d = r.model->data();
    (void)d;
    const auto box = r.model->box();
    r.boxMin = {box[0], box[1], box[2]};
    r.boxMax = {box[0] + box[3], box[1] + box[4], box[2] + box[5]};
    r.base = {box[0] + (box[3] >> 1), box[1], box[2] + (box[5] >> 1)};
    r.height = box[4];
    r.radius = vecMag({box[3], box[4], box[5]}) >> 1;
    r.model->setAlphaAll(0x1f); // 02012d08
    if (fileExists(dir / (name + ".nsbca"))) {
        r.joint = std::make_unique<JointAnimation>(readBytes(dir / (name + ".nsbca")));
    }
    if (fileExists(dir / (name + ".nsbma"))) {
        r.material = std::make_unique<MaterialAnimation>(readBytes(dir / (name + ".nsbma")));
    }
    if (fileExists(dir / (name + ".mtn"))) {
        const auto m = readBytes(dir / (name + ".mtn"));
        const unsigned n = m[0] | m[1] << 8;
        for (unsigned i = 0; i < n; ++i) {
            const std::size_t e = 8 + i * 24;
            WalkResource::Motion mo;
            mo.name = std::string(reinterpret_cast<const char*>(&m[e]),
                                  strnlen(reinterpret_cast<const char*>(&m[e]), 18));
            mo.start = m[e + 18] | m[e + 19] << 8;
            mo.end = m[e + 20] | m[e + 21] << 8;
            mo.step = m[e + 22] | m[e + 23] << 8;
            r.motions.push_back(mo);
        }
    }
}

// --- Actor (020f0944 bind, 020f0c24 motion, 020f0d84 update, 020f0f7c/0fe4 draw)
void setRotation(Actor& a, fx32 x, fx32 y, fx32 z) { // 020f0a78
    a.angles = {x, y, z};
    const unsigned index = unsigned(f2i(16.0f * i2f(y) / 6.28318548f) & 0xffff) >> 4;
    const auto [s, c] = sinCos(index);
    a.rotation = {c, 0, -s, 0, 0x1000, 0, s, 0, c};
}

void bind(Actor& a, WalkResource* r, int kind) { // 020f1fa0
    a.clear();
    a.speed.set(1.0f, 0);
    a.alpha.set(1.0f, 0);
    a.res = r;
    a.kind = kind;
    a.object.model = r->model.get();
    a.object.joint = r->joint.get();
    a.object.material = r->material.get();
    a.scale = {0x100, 0x100, 0x100};
}

bool selectMotion(Actor& a, const WalkResource::Motion* m, std::uint32_t flags,
                  std::uint32_t& random) { // 020f0c24
    if (!m) {
        return false;
    }
    a.motion = m->name;
    a.start = m->start;
    a.end = m->end;
    a.step = m->step;
    a.frame = fx32(m->start) << 12;
    a.ratio = 0;
    a.loops = 0;
    a.flags = flags & ~1u;
    if ((flags & 8) && m->end > m->start) {
        const int span = m->end - m->start;
        const int r = int(gameRandom(random)) % span;
        a.frame += r * 0x1000;
        a.ratio = i2f(r) / i2f(span);
    }
    a.ratioPrev = a.ratio;
    return true;
}

bool motionByName(Actor& a, const std::string& name, std::uint32_t flags,
                  std::uint32_t& random) { // 020f0d44
    if (!a.bound()) {
        return false;
    }
    for (const auto& m : a.res->motions) {
        if (m.name == name) {
            return selectMotion(a, &m, flags, random);
        }
    }
    return false;
}

bool motionByIndex(Actor& a, unsigned index, std::uint32_t flags,
                   std::uint32_t& random) { // 020f0d04
    if (!a.bound() || index >= a.res->motions.size()) {
        return false;
    }
    return selectMotion(a, &a.res->motions[index], flags, random);
}

void update(Actor& a) { // 020f0d84
    if (!a.bound()) {
        return;
    }
    a.speed.update();
    a.alpha.update();
    if (a.object.joint || a.object.material) {
        a.ratioPrev = a.ratio;
        if (a.step == 0 || a.end <= a.start) {
            a.frame = fx32(a.start) << 12;
            a.ratio = 1.0f;
        } else {
            a.frame += f2i(i2f(a.step) * a.speed.current);
            if (!(a.flags & 2)) {
                while (a.frame >= a.end * 0x1000) {
                    a.frame += (a.end - a.start) * -0x1000;
                    ++a.loops;
                }
            } else if (a.frame >= a.end * 0x1000) {
                a.frame = a.end << 12;
                a.flags |= 1;
                a.loops = 1;
            }
            a.ratio = i2f(a.frame - a.start * 0x1000) / i2f((a.end - a.start) * 0x1000);
        }
        a.object.jointFrame = a.frame;
        a.object.materialFrame = a.frame;
    }
    // World matrix: 3x3 rotation scaled by the actor scale, translated.
    a.world = Matrix::identity();
    for (int r = 0; r < 3; ++r) {
        const fx32 s = r == 0 ? a.scale.x : r == 1 ? a.scale.y : a.scale.z;
        for (int c = 0; c < 3; ++c) {
            a.world.m[std::size_t(r * 4 + c)] = mulTrunc(a.rotation[std::size_t(r * 3 + c)], s);
        }
    }
    a.world.m[12] = a.position.x;
    a.world.m[13] = a.position.y;
    a.world.m[14] = a.position.z;
}

void draw(Walk& w, Actor& a) { // 020f2000 = 020f0f7c + 020f0fe4
    if (!a.bound()) {
        return;
    }
    w.glb.baseScale = a.scale;
    w.glb.baseTranslation = a.position;
    w.glb.baseRotation = Matrix::identity();
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            w.glb.baseRotation.m[std::size_t(r * 4 + c)] = a.rotation[std::size_t(r * 3 + c)];
        }
    }
    w.glb.flush(w.gpu);
    if (!(a.flags & 4)) {
        a.res->model->setAlphaAll(f2i(31.0f * a.alpha.current));
    }
    w.renderer.draw(w.gpu, w.glb, a.object);
    w.glb.flush(w.gpu);
}

// Model box centre in world space (020efda0 + 0206b3b8).
Vec centre(const Actor& a) {
    const auto& r = *a.res;
    const Vec c{(r.boxMax[0] + r.boxMin[0]) >> 1, (r.boxMax[1] + r.boxMin[1]) >> 1,
                (r.boxMax[2] + r.boxMin[2]) >> 1};
    const auto& m = a.world.m;
    auto row = [&](int k) {
        return m[std::size_t(12 + k)] + mulRound(m[std::size_t(k)], c.x) +
               mulRound(m[std::size_t(4 + k)], c.y) + mulRound(m[std::size_t(8 + k)], c.z);
    };
    return {row(0), row(1), row(2)};
}

// --- Cells ---------------------------------------------------------------
Vec cellPosition(int col, int row) {
    return {(col - 4) * 0x1000 + 0x800, 0xcc, (row - 3) * 0x1000 + 0x800};
} // 020f50ec

int cellColumn(fx32 x) {
    const int v = x + 0x10000;
    return ((v + int(unsigned(v >> 11) >> 20)) >> 12) - 0xc;
} // 020f5130

int cellRow(fx32 z) {
    const int v = z + 0x10000;
    return ((v + int(unsigned(v >> 11) >> 20)) >> 12) - 0xd;
} // 020f5148
} // namespace

// ---------------------------------------------------------------------------
// Camera (020f1050 / 020f18a4 / 020f195c / 020f199c / 020f1598)

namespace {
void cameraProjection(Walk::Camera& c) { // 020f1490
    if (c.mode == 0) {
        c.projection = orthographic(mulRound(c.top, c.scale), mulRound(c.bottom, c.scale),
                                    mulRound(c.left, c.scale), mulRound(c.right, c.scale), c.near,
                                    c.far, 0x1000);
    } else {
        c.projection = perspective(c.fovySin, c.fovyCos, c.aspect, c.near, c.far, 0x1000);
    }
}

fx32 fx(float v) {
    return f2i(4096.0f * v);
}

void cameraOrtho(Walk::Camera& c, float t, float b, float l, float r, float n, float f,
                 float s) { // 020f1324
    c.mode = 0;
    c.top = fx(t);
    c.bottom = fx(b);
    c.left = fx(l);
    c.right = fx(r);
    c.scale = fx(s);
    c.near = fx(n);
    c.far = fx(f);
    cameraProjection(c);
}

void cameraLook(Walk::Camera& c, float ex, float ey, float ez, float ax, float ay, float az,
                float ux, float uy, float uz) { // 020f12c4
    c.eye = {fx(ex), fx(ey), fx(ez)};
    c.at = {fx(ax), fx(ay), fx(az)};
    c.up = {fx(ux), fx(uy), fx(uz)};
    c.view = lookAt(c.eye, c.up, c.at);
}

void cameraCombine(Walk::Camera& c) {
    c.combined = multiply(c.view, c.projection);
} // 020f157c

void cameraInit(Walk::Camera& c) { // 020f18a4 (020f1050 first)
    c = Walk::Camera{};
    cameraLook(c, 0, 0, 1, 0, 0, 0, 0, 1, 0);
    // 020f13d4: 30 degree perspective, 4:3 ... replaced right after by the
    // orthographic projection of 020f1324 (12/-12/-16/16, depth +-256).
    const unsigned index = unsigned(f2i(65536.0f * 0.523598790f / 6.28318548f) & 0xffff) >> 4;
    const auto [s, cs] = sinCos(index);
    c.fovySin = s;
    c.fovyCos = cs;
    c.aspect = fx(1.33333337f);
    cameraOrtho(c, 12, -12, -16, 16, -256, 256, 1);
    c.distance = 4;
    c.pitch = 0x2d;
    c.yaw = 0;
    c.depth = 0x18;
    c.blend1 = c.blend2 = c.progress = 1.0f;
    c.zoom = 8.0f;
    c.target = {0, 0xa00, 0};
    c.a = c.target;
    c.b = c.target;
    c.start = c.target;
    c.blendMode = 0;
    c.zoomStart = c.zoom;
    c.blend1 = 1;
    c.blend2 = 1;
    c.progress = 1;
}

void cameraMode(Walk::Camera& c, int mode, float b1, float b2) { // 020f195c
    c.zoomStart = c.zoom;
    c.start = c.target;
    c.blendMode = mode;
    c.blend1 = b1;
    c.blend2 = b2;
    c.progress = 1.0f;
}

void cameraUpdate(Walk::Camera& c) { // 020f199c
    float desiredZoom = c.zoomTarget;
    Vec desired = c.a;
    bool snap = false;
    switch (c.blendMode) {
    case 0:
        desired = {0, 0xa00, 0};
        snap = true;
        desiredZoom = fbits(0x40fd70a4);
        break;
    case 1:
        desired = c.a;
        snap = true;
        desiredZoom = 4.0f;
        break;
    case 2:
        desired = c.a;
        desiredZoom = 4.0f;
        break;
    case 3: {
        desired = vecAdd(c.a, c.b);
        desired.x >>= 1;
        desired.y >>= 1;
        desired.z >>= 1;
        const float s = float(std::sin(double(i2f(c.pitch) * 3.14159274f / 180.0f)));
        int dz = f2i(i2f(c.a.z - c.b.z) * s / 0.75f);
        if (dz < 0) {
            dz = -dz;
        }
        int dx = c.a.x - c.b.x;
        if (dx < 0) {
            dx = -dx;
        }
        const int m = std::max(dx, dz);
        desiredZoom = 2.0f + i2f(m) / 4096.0f;
        break;
    }
    default:
        break;
    }
    if (desiredZoom < 4.0f) {
        desiredZoom = 4.0f;
    }
    if (!snap) {
        const float k = 1.0f - c.blend1;
        c.zoom = desiredZoom + k * (c.zoom - desiredZoom);
        const Vec d = vecSub(c.target, desired);
        c.target = vecAdd(desired, {f2i(i2f(d.x) * k), f2i(i2f(d.y) * k), f2i(i2f(d.z) * k)});
    } else {
        const float k = (1.0f - c.blend1) * c.progress;
        c.progress = (c.progress - k <= c.blend2) ? k : c.progress - c.blend2;
        if (c.progress < 0.0f) {
            c.progress = 0.0f;
        }
        c.zoom = desiredZoom + c.progress * (c.zoomStart - desiredZoom);
        const Vec d = vecSub(c.start, desired);
        c.target = vecAdd(desired, {f2i(i2f(d.x) * c.progress), f2i(i2f(d.y) * c.progress),
                                    f2i(i2f(d.z) * c.progress)});
    }
    // Eye on a sphere around the target (yaw +0x118, pitch +0x114, distance +0x110).
    const float tx = i2f(c.target.x) / 4096.0f, ty = i2f(c.target.y) / 4096.0f,
                tz = i2f(c.target.z) / 4096.0f;
    const float sy = sinDeg(c.yaw), cp = cosDeg(c.pitch), sp = sinDeg(c.pitch), cy = cosDeg(c.yaw);
    const float dist = i2f(c.distance);
    const float ex = tx + dist * cp * sy, ey = ty + dist * sp, ez = tz + dist * cp * cy;
    const float h = 0.75f * c.zoom;
    cameraOrtho(c, 0.5f * h, -0.5f * h, -0.5f * c.zoom, 0.5f * c.zoom, 0.0f - i2f(c.depth),
                i2f(c.depth), 1.0f);
    cameraLook(c, ex, ey, ez, tx, ty, tz, 0, 1, 0);
    cameraCombine(c);
}

void cameraToGlobal(Walk& w) { // 020f1598
    w.glb.projection = w.camera.projection;
    w.glb.camera = lookAt(w.camera.eye, w.camera.up, w.camera.at);
}

// 020f1114: world position to screen pixels through the combined matrix.
void project(const Walk::Camera& c, const Vec& p, int& x, int& y) {
    const auto& m = c.combined.m;
    auto col = [&](int k) {
        return fx32(mulTrunc(m[std::size_t(k)], p.x)) + mulTrunc(m[std::size_t(4 + k)], p.y) +
               mulTrunc(m[std::size_t(8 + k)], p.z);
    };
    const fx32 w = col(3) + m[15];
    if (!w) {
        x = y = 0;
        return;
    }
    x = (col(0) + m[12]) * 0x80 / w + 0x80;
    y = 0x60 - (col(1) + m[13]) * 0x60 / w;
}
} // namespace

// ---------------------------------------------------------------------------
// Hamster (020f2018 / 020f2120 / 020f2264 / 020f2788), objects (020f311c..
// 020f3200), effects (020f3224..020f32b8) and the walk scene.

namespace {
struct Context {
    SceneKit& kit;
    GameState& state;
    std::uint32_t& random;
    Walk& w;
};

using Hamster = Walk::Hamster;

void resetPath(Hamster& h) { // 020f2120
    h.collision = 0;
    h.radius = 0;
    h.targetObject = -1;
    h.targetType = -1;
    h.targetPos = h.pos;
    h.targetCol = cellColumn(h.targetPos.x);
    h.targetRow = cellRow(h.targetPos.z);
    h.pathCount = h.pathIndex = h.previousIndex = 0;
    for (auto& p : h.path) {
        p = h.pos;
    }
}

bool setState(Context& c, int s); // 020f2264

void hamsterInit(Context& c) { // 020f2018 (after 020f1f74)
    auto& h = c.w.hamster;
    static_cast<Actor&>(h).clear();
    h = Hamster{};
    setState(c, 1); // no model yet: falls back to state 0
    h.baseSpeed = 0;
    h.moveSpeed = 0;
    h.turnSpeed = 0;
    h.period = 0x3c;
    h.stepSound = -1;
    resetPath(h);
    h.startVoices.fill(-1);
    h.goalVoices.fill(-1);
    h.startVoiceCount = h.goalVoiceCount = 0;
}

bool setState(Context& c, int s) {
    auto& h = c.w.hamster;
    if (s == 0) {
        h.state = h.next = 0;
        h.timer = 0;
        h.stop = false;
        h.moveFlags = 0;
        return false;
    }
    h.state = h.next = 0;
    h.timer = 0;
    h.stop = false;
    h.speed.set(1.0f, 0);
    std::string name;
    std::uint32_t animFlags = 0, move = 0;
    switch (s) {
    case 1:
        name = "stand";
        {
            const int r = int(gameRandom(c.random));
            h.idleLimit = (r % 5 + 5) * 0x3c;
        }
        break;
    case 2:
        name = "run";
        move = 0x212;
        break;
    case 3:
        name = "run";
        move = 0x413;
        break;
    case 4:
        name = "run";
        move = 0x413;
        h.speed.set(2.0f, 0);
        break;
    case 5:
        name = "roll";
        move = 0x403;
        h.speed.set(2.0f, 0);
        break;
    case 6:
        name = "run";
        move = 0x403;
        h.speed.set(1.5f, 0);
        break;
    case 7:
        name = "stand";
        move = 0x404;
        h.speed.set(1.5f, 0);
        break;
    case 8:
        name = "eat";
        animFlags = 2;
        move = 0x100;
        break;
    case 9: {
        char buf[16];
        std::snprintf(buf, sizeof buf, "play%d", int(gameRandom(c.random) & 1) + 1);
        name = buf;
        animFlags = 2;
        move = 0x100;
        break;
    }
    case 10:
        name = "tired";
        animFlags = 2;
        move = 0x100;
        break;
    case 11:
        name = "search";
        animFlags = 2;
        move = 0x100;
        break;
    case 12:
        name = "surprise";
        animFlags = 2;
        move = 0x100;
        break;
    case 13:
        name = "start";
        animFlags = 2;
        move = 0x100;
        break;
    case 14:
        name = "goal";
        animFlags = 2;
        move = 0x100;
        break;
    default:
        break;
    }
    if (!motionByName(h, name, animFlags, c.random)) {
        setState(c, 0);
        return false;
    }
    h.next = 1;
    h.state = s;
    h.moveFlags = move;
    if (h.state == 6) {
        c.kit.effect(h.stepSound);
    } else if (h.state != 7) {
        return true;
    }
    h.startPos = h.pos;
    h.startFacing = h.facing;
    h.startDirection = h.direction;
    return true;
}

// 020f163c / 020f172c / 020f1740: push-out against a circle or a square.
bool circleHit(const Vec& centre, fx32 radius, const Vec& point, Vec* out) {
    if (out) {
        *out = {};
    }
    Vec d = vecSub(point, centre);
    d.y = 0;
    const fx32 squared = vecDot(d, d);
    if (squared > fx32((std::int64_t(radius) * radius + 0x800) >> 12)) {
        return false;
    }
    if (out) {
        const fx32 push = radius - fxSqrt(squared);
        const Vec n = vecNormalize(d);
        *out = {mulRound(n.x, push), mulRound(n.y, push), mulRound(n.z, push)};
    }
    return true;
}

bool squareHit(const Vec& centre, fx32 half, const Vec& point, fx32 extra, Vec* out) {
    if (out) {
        *out = {};
    }
    const Vec d = vecSub(point, centre);
    const fx32 ax = d.x < 0 ? -d.x : d.x, az = d.z < 0 ? -d.z : d.z;
    const fx32 outer = half + extra;
    auto sign = [](fx32 v, fx32 a) {
        return a ? v / a : 0;
    };
    if ((half < ax ? az : ax) > half) {
        const Vec corner{centre.x + half * sign(d.x, ax), centre.y,
                         centre.z + half * sign(d.z, az)};
        if (circleHit(corner, extra, point, out)) {
            return true;
        }
    }
    if (az <= half && ax < outer) {
        if (out) {
            out->x = (outer - ax) * sign(d.x, ax);
        }
        return true;
    }
    if (ax <= half && az < outer) {
        if (out) {
            out->z = (outer - az) * sign(d.z, az);
        }
        return true;
    }
    return false;
}

fx32 wrapAngle(fx32 a) {
    if (a < -0x3243) {
        a += 0x6486;
    }
    if (a > 0x3243) {
        a -= 0x6486;
    }
    return a;
}

void hamsterUpdate(Context& c) { // 020f2788
    auto& h = c.w.hamster;
    for (;;) {
        h.previous = h.pos;
        h.previousFacing = h.facing;
        h.previousIndex = h.pathIndex;
        if (h.state == 1) {
            if (h.idleLimit < h.timer) {
                setState(c, 10);
            }
        } else if (h.state == 13 || h.state == 14) {
            const int count = h.state == 13 ? h.startVoiceCount : h.goalVoiceCount;
            if (h.timer == 30 && count) {
                const int v = int(gameRandom(c.random)) % count;
                if (auto s = c.kit.sound()) {
                    s->playTrackedStream(2,
                                         h.state == 13 ? h.startVoices[std::size_t(v)]
                                                       : h.goalVoices[std::size_t(v)],
                                         0, 1.0f, 0);
                }
            }
        }
        h.velocity = {};
        h.turn = 0;
        bool arrived = false;
        fx32 turnMax = 0;
        if (h.moveFlags & 4) {
            const float ratio = i2f(h.timer) / i2f(h.period);
            if (ratio >= 1.0f) {
                h.velocity = vecSub(h.path[std::size_t(h.pathIndex)], h.pos);
                ++h.pathIndex;
            } else {
                Vec d = vecSub(h.path[std::size_t(h.pathIndex)], h.startPos);
                d.x = f2i(i2f(d.x) * ratio);
                d.z = f2i(i2f(d.z) * ratio);
                h.velocity = vecSub(vecAdd(h.startPos, d), h.pos);
            }
        }
        if (h.moveFlags & 3) {
            const fx32 speed = f2i(i2f(h.moveSpeed) * h.speed.current);
            turnMax = f2i(i2f(h.turnSpeed) * h.speed.current);
            if (h.state == 5) {
                turnMax = 0x3243;
            }
            const bool onPath = h.pathCount > 0 && h.pathIndex < h.pathCount;
            const Vec target = onPath ? h.path[std::size_t(h.pathIndex)] : h.targetPos;
            Vec d = vecSub(target, h.pos);
            d.y = 0;
            if (d.x == 0 && d.z == 0) {
                const int next = h.pathIndex + 1;
                arrived = h.pathCount <= next;
                if (next < h.pathCount) {
                    h.pathIndex = next;
                }
            } else {
                if (h.moveFlags & 2) {
                    h.turn =
                        std::clamp(wrapAngle(atan2Index(d.x, d.z) - h.facing), -turnMax, turnMax);
                }
                if ((h.moveFlags & 1) && -turnMax < h.turn && h.turn < turnMax) {
                    const fx32 len = vecMag(d);
                    if (h.pathIndex + 1 < h.pathCount || h.radius < 1) {
                        if (speed < len) {
                            const Vec n = vecNormalize(d);
                            h.velocity = {mulRound(n.x, speed), mulRound(n.y, speed),
                                          mulRound(n.z, speed)};
                        } else {
                            h.velocity = {};
                            h.pos = target;
                            const int next = h.pathIndex + 1;
                            arrived = next == h.pathCount;
                            if (!arrived) {
                                h.pathIndex = next;
                            }
                        }
                    } else {
                        const Vec n = vecNormalize(d);
                        h.velocity = {mulRound(n.x, speed), mulRound(n.y, speed),
                                      mulRound(n.z, speed)};
                        const Vec next = vecAdd(h.pos, h.velocity);
                        Vec push{};
                        if (h.collision == 0) {
                            arrived = circleHit(target, h.radius + h.baseSpeed, next, &push);
                        } else if (h.collision == 1) {
                            arrived = squareHit(target, h.radius, next, h.baseSpeed, &push);
                        }
                        if (arrived) {
                            h.velocity = vecAdd(h.velocity, push);
                        }
                    }
                }
            }
            if (!(h.moveFlags & 1)) {
                h.velocity = {};
            }
            if (!(h.moveFlags & 2)) {
                h.turn = 0;
            }
            if (h.timer > 14) {
                if ((h.moveFlags & 0x400) && arrived) {
                    h.stop = true;
                }
                if ((h.moveFlags & 0x200) && -turnMax < h.turn && h.turn < turnMax) {
                    h.stop = true;
                }
            }
        }
        if (h.pathIndex != h.previousIndex && h.state == 7 && h.pathIndex == 1) {
            setState(c, 1);
        }
        if ((h.moveFlags & 0x100) && (h.flags & 1)) {
            h.stop = true;
        }
        if (h.stop) {
            setState(c, h.next);
            continue;
        }
        h.pos = vecAdd(h.pos, h.velocity);
        const int oldCol = h.col, oldRow = h.row;
        h.col = cellColumn(h.pos.x);
        h.row = cellRow(h.pos.z);
        if (!h.noCount && (oldCol != h.col || oldRow != h.row)) {
            ++h.steps;
        }
        h.facing = wrapAngle(h.facing + h.turn);
        h.direction = quarter(i2f(h.facing) * 360.0f / 25734.0f);
        Vec shown = h.pos;
        if (h.state == 6) {
            const fx32 total = vecDistance(h.startPos, h.targetPos),
                       done = vecDistance(h.startPos, shown);
            const float r = i2f(done) / i2f(total);
            const float k = 2.0f * r - 1.0f;
            const float s = 1.0f - k * k;
            shown.y += f2i(1024.0f * r);
            shown.y += f2i(1024.0f * s);
        } else if (h.state == 7 && h.pathIndex == 0) {
            const float r = i2f(h.timer) / i2f(h.period);
            const float k = 2.0f * r - 1.0f;
            const float s = 1.0f - k * k;
            if (h.targetType == 0xb && h.radius == 0) {
                shown.y += 0x400;
            } else {
                shown.y += f2i(1024.0f * r);
            }
            shown.y += f2i(6144.0f * s);
        }
        h.position = shown;
        setRotation(h, 0, h.facing, 0);
        update(h);
        if (h.moveFlags & 0x10) {
            if (h.ratioPrev <= 0.25f && 0.25f < h.ratio) {
                c.kit.effect(h.stepSound);
            }
            if (h.ratioPrev <= 0.75f && 0.75f < h.ratio) {
                c.kit.effect(h.stepSound);
            }
        }
        ++h.timer;
        return;
    }
}

// Objects and effects.
void objectReset(Walk::Object& o) {
    static_cast<Actor&>(o).clear();
    o.type = o.value = 0;
    o.col = o.row = -1;
    o.used = false;
    o.timer = -1;
}

void objectUpdate(Context& c, Walk::Object& o) { // 020f31b4
    if (!(o.alpha.current > 0.0f)) {
        return;
    }
    update(o);
    if ((o.flags & 2) && (o.flags & 1)) {
        motionByName(o, "stand", 0, c.random);
    }
}

int spawnEffect(Context& c, int slot, const Vec& at) { // 020f57ec
    for (int i = 0; i < 8; ++i) {
        auto& e = c.w.effects[std::size_t(i)];
        if (e.active) {
            continue;
        }
        bind(e, &c.w.resources[std::size_t(slot)], slot);
        e.active = true;
        motionByIndex(e, 0, 6, c.random);
        e.position = at;
        if (slot == 0xc) {
            c.kit.effect(0x1c4);
        }
        return i;
    }
    return -1;
}

void effectUpdate(Walk::Effect& e) { // 020f3284
    if (!e.active) {
        return;
    }
    if (e.flags & 1) {
        e.active = false;
        static_cast<Actor&>(e).clear();
        return;
    }
    update(e);
}

bool smellWave(Walk& w) { // 020f58cc
    for (auto& e : w.effects) {
        if (e.active && e.kind == 0xf && e.ratioPrev <= 0.3f && 0.3f < e.ratio) {
            return true;
        }
    }
    return false;
}

void placeObject(Context& c, int col, int row, int slot) { // 020f55d4
    auto& w = c.w;
    if (slot > 15 || w.objectCount >= 48) {
        return;
    }
    auto& o = w.objects[std::size_t(w.objectCount)];
    objectReset(o);
    bind(o, &w.resources[std::size_t(slot)], slot);
    motionByName(o, "stand", 8, c.random);
    o.position = cellPosition(col, row);
    o.col = col;
    o.row = row;
    static constexpr std::array<std::pair<int, int>, 10> kTypes{{{0, 0x700},
                                                                 {1, 0x700},
                                                                 {0, 0x700},
                                                                 {0, 0x700},
                                                                 {0, 0x700},
                                                                 {0, 0x700},
                                                                 {1, 0x500},
                                                                 {0, 0x700},
                                                                 {0, 0x700},
                                                                 {0, 0x700}}}; // 020f8dfc
    if (slot >= 2 && slot < 12) {
        o.type = kTypes[std::size_t(slot - 2)].first;
        o.value = kTypes[std::size_t(slot - 2)].second;
    }
    w.grid[std::size_t(row)][std::size_t(col)] = w.objectCount++;
}

// Messages in the lower window (020f5948 / 020f5978 / 020f5a44).
void clearMessages(Context& c) {
    c.kit.clearText(SceneKit::Lower);
    c.w.messages.clear();
    c.w.messageCount = 0;
}

void showMessages(Context& c, std::vector<std::string> m) {
    clearMessages(c);
    c.w.messages = std::move(m);
    c.w.messageCount = int(c.w.messages.size());
    if (!c.w.messages.empty()) {
        c.kit.textCentered(0x5c, c.w.messages[0], 3, 0x80, SceneKit::Lower);
    }
}

bool nextMessage(Context& c) {
    if (--c.w.messageCount < 1) {
        clearMessages(c);
        return false;
    }
    c.kit.clearText(SceneKit::Lower);
    c.kit.textCentered(0x5c, c.w.messages[1], 3, 0x80, SceneKit::Lower);
    return true;
}

// 020f5c14: depth-first search for the nearest unused object (4 steps).
int walkSearch(Walk& w, std::vector<std::pair<int, int>>& path, int depth, int maxDepth, int col,
               int row, int dir, int want) {
    if (maxDepth <= depth || unsigned(col) > 7 || unsigned(row) > 5) {
        return 0;
    }
    for (int i = 0; i < depth; ++i) {
        if (path[std::size_t(i)] == std::pair(col, row)) {
            return 0;
        }
    }
    path[std::size_t(depth)] = {col, row};
    const int index = w.grid[std::size_t(row)][std::size_t(col)];
    if (index >= 0 && !w.objects[std::size_t(index)].used) {
        const int kind = w.objects[std::size_t(index)].kind;
        if (kind == 3) {
            return 0;
        }
        if (want >= 0 && want != kind) {
            return 0;
        }
        return depth + 1;
    }
    static constexpr int order[4] = {0, 1, 3, 2}; // 020f8dcc
    bool found = false;
    std::vector<std::pair<int, int>> local = path;
    for (int k = 0; k < 4; ++k) {
        const int d = (dir + order[k]) & 3;
        local = path;
        local.resize(path.size());
        const int r =
            walkSearch(w, local, depth + 1, maxDepth, col + kDirection[std::size_t(d)].first,
                       row + kDirection[std::size_t(d)].second, d, want);
        if (r > 0 && r <= maxDepth - (found ? 1 : 0)) {
            for (int i = depth + 1; i < r; ++i) {
                path[std::size_t(i)] = local[std::size_t(i)];
            }
            found = true;
            maxDepth = r;
        }
    }
    return found ? maxDepth : 0;
}
} // namespace

// ---------------------------------------------------------------------------
// Walk scene (0x4fc0 object): 020f4708 create, 020f52d4 level setup,
// 020f5e90 logic, 020f7f04 draw, 020f4df0 destroy.

namespace {
// Level parameters (020f9110, 0x20 bytes per level; the voice counts are
// patched on first use by 020f52d4): base speed, move speed, turn speed,
// step sound, start voices, goal voices.
struct LevelParameters {
    fx32 baseSpeed;
    int moveSpeed, turnSpeed, stepSound;
    std::vector<int> start, goal;
};

const LevelParameters& levelParameters(int level) {
    static const std::array<LevelParameters, 5> table{{
        {0x666, 0x40, 0xda, 0x1b5, {366, 367, 368}, {370, 371}},
        {0x600, 0x60, 0x106, 0x1b5, {366, 367, 368}, {372, 374}},
        {0x599, 0x80, 0x147, 0x1b6, {367, 368, 369}, {375}},
        {0x533, 0xa0, 0x1b4, 0x1b6, {367, 368, 373}, {377}},
        {0x4cc, 0xc0, 0x28f, 0x1b7, {368, 373}, {378, 379}},
    }};
    return table[std::size_t(std::clamp(level, 1, 5) - 1)];
}

// 0206becc: %d / %s substitution of a message template.
std::string formatMessage(const std::string& pattern, std::initializer_list<std::string> args) {
    std::string out;
    auto it = args.begin();
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '%' && i + 1 < pattern.size() &&
            (pattern[i + 1] == 'd' || pattern[i + 1] == 's')) {
            if (it != args.end()) {
                out += *it++;
            }
            ++i;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

// 020f2000 for the model box centre: 020efda0 + 0206b3b8 (world transform).
Vec boxCentre(const Actor& a) {
    return centre(a);
}

void fadeObject(Context& c,
                Walk::Object& o) { // alpha 1 -> 0 over 0.5 s, sparkle effect 12 at the box centre
    o.alpha.set(0.0f, 0.5f);
    spawnEffect(c, 0xc, boxCentre(o));
}
} // namespace

void HamsterGame::walkCreate() {
    if (auto s = kit_.sound()) {
        s->roomSound(0xc, true);
    }
    kit_.clearText(SceneKit::Lower);
    kit_.clearText(SceneKit::Upper);
    kit_.resetDisplay(); // 0206dabc / 0206d9c0 / 0206decc (hypothesis: as the item screen)
    walk_ = std::make_unique<Walk>();
    auto& w = *walk_;
    Context c{kit_, state_, random_, w};
    // 0201c4ac: edge colours 020f8dac; DISP3DCNT as measured (texture, blend, AA, edge).
    w.gpu.edgeColors.fill(0x1086);
    w.gpu.disp3dcnt = 0x39;
    w.gpu.clearDepth = 0x7fff;
    w.gpu.clearAlpha = 0;
    const int shown = std::clamp(level(), 1, 5);
    kit_.lowerBackground = "subgame/ham/ham_bg";
    kit_.upperBackground = "subgame/ham/" + kit_.language + "/ham_sub" + std::to_string(shown);
    w.end = kit_.button("subgame/ham/%s/end_btn.spr", 0, 0, -1);
    w.white = kit_.load("subgame/ham/white_num.spr", 0, 0);
    w.yellow = kit_.load("subgame/ham/yerrow_num.spr", 0, 0);
    w.window = kit_.load("subgame/ham/window_ham.spr", 0, 0);
    w.found = kit_.load("subgame/ham/hakken_icon.spr", 0, 0);
    w.levelNumber = kit_.load("subgame/ham/level_num.sbj", 0, 0);
    w.bubble = kit_.load("subgame/ham/hukidashi.sbj", 0, 0);
    w.icon = kit_.load("subgame/ham/seticon.sbj", 0, 0);
    w.count = kit_.load("subgame/ham/count_num.sbj", 0, 0);
    w.walkText = kit_.load("subgame/ham/?/walk_mes.spr", 0xd4, 4);
    w.levelText = kit_.load("subgame/ham/?/level_mes.sbj", 0, 0);
    kit_.select(w.window, "gfx");
    w.window.x = 0x15;
    w.window.y = 0x39;
    // Resources 1..15 (020f8e4c): map1, obj1..obj10, eff1..eff4; slot 0 is the hamster.
    static constexpr const char* kNames[16] = {"",     "map1", "obj1", "obj2", "obj3", "obj4",
                                               "obj5", "obj6", "obj7", "obj8", "obj9", "obj10",
                                               "eff1", "eff2", "eff3", "eff4"};
    for (int i = 1; i < 16; ++i) {
        loadResource(w.resources[std::size_t(i)], kit_.assets.root, kNames[i], w);
    }
    cameraInit(w.camera);
    bind(w.map, &w.resources[1], 1);
    for (auto& o : w.objects) {
        objectReset(o);
    }
    for (auto& row : w.grid) {
        row.fill(-1);
    }
    for (auto& row : w.scent) {
        row.fill(0);
    }
    w.objectCount = 0;
    for (auto& e : w.effects) {
        static_cast<Actor&>(e).clear();
        e.active = false;
    }
    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 8; ++col) {
            const int i = row * 8 + col;
            int v = state_.progress.bytes[0x28e + std::size_t(i >> 1)];
            if (i & 1) {
                v >>= 4;
            }
            if (v & 15) {
                placeObject(c, col, row, (v & 15) + 1);
            }
        }
    }
    hamsterInit(c);
    // 020f52d4: level setup.
    for (auto& e : w.effects) {
        static_cast<Actor&>(e).clear();
        e.active = false;
    }
    int lv = level();
    w.slim = false;
    w.cleared = false;
    if (lv < 1) {
        lv = 1;
    }
    if (lv > 5) {
        w.slim = true;
        lv = 5;
    }
    hamsterInit(c);
    loadResource(w.resources[0], kit_.assets.root, "ham" + std::to_string(lv), w);
    w.goal = w.slim ? state_.progress.bytes[0x2be] : goalSteps(lv);
    auto& h = w.hamster;
    bind(h, &w.resources[0], 0);
    const auto& p = levelParameters(lv);
    h.baseSpeed = p.baseSpeed;
    h.moveSpeed = p.moveSpeed;
    h.turnSpeed = p.turnSpeed;
    h.stepSound = p.stepSound;
    h.period = f2i(8192.0f / (1.5f * i2f(p.moveSpeed)));
    h.direction = 0;
    h.col = 0;
    h.row = 0;
    h.steps = 0;
    h.pos = cellPosition(0, 0);
    for (int v : p.start) {
        if (h.startVoiceCount < 4) {
            h.startVoices[std::size_t(h.startVoiceCount++)] = v;
        }
    }
    for (int v : p.goal) {
        if (h.goalVoiceCount < 4) {
            h.goalVoices[std::size_t(h.goalVoiceCount++)] = v;
        }
    }
    w.camera.a = h.pos;
    w.camera.a.y = 0xa00;
    clearMessages(c);
    w.foundTimer = -1;
    w.foundX = w.foundY = 0;
    kit_.textTitle(0xd, kit_.text.expand(hamsterName()), 0, 0x57, SceneKit::Upper);
    if (auto s = kit_.sound()) {
        while (!s->bgmSettled()) {
            kit_.wait(1);
        }
    }
}

void HamsterGame::walkDestroy() {
    auto& w = *walk_;
    if (w.finished) {
        if (w.cleared && !w.slim) {
            state_.progress.bytes[0x283] = std::uint8_t(level() + 1);
        }
        if (state_.progress.bytes[0x2be] < unsigned(w.hamster.steps)) {
            state_.progress.bytes[0x2be] = std::uint8_t(w.hamster.steps);
        }
    }
    kit_.layer3D.clear();
    walk_.reset();
}

namespace {
// The walk is a state machine (020f5e90). State numbers are the original's,
// grouped by hundreds:
//   100..102   start: camera move, the opening message (how many steps the
//              next level needs), then wait for a tap or two seconds.
//   300        choose the next target. If the hamster smells food, it heads
//              for the nearest uneaten food item (kind 10), weighing the turn
//              it needs as well as the distance. Otherwise a depth-first
//              search finds the nearest unused item within four steps,
//              preferring kind 2. With no target the walk ends.
//   400, 500   walk to the target and meet the item there; the item's kind
//              leads to its own reaction states (1000..2001; 1800 is eating).
//   200..206   end of the walk: compare the steps walked with the level's
//              goal, show the result ("msg_nrm_*", or "msg_slm_*" in the
//              other mode) and raise the level when cleared.
//   600        the player pressed "end".
//
// Scene state helpers of 020f5e90: each state runs its first frame at timer 0;
// "go" switches state, clears the timer and runs the new state at once.
struct Logic {
    Context& c;
    Walk& w;
    Hamster& h;
    SceneKit& kit;
    std::function<std::string(const std::string&)> member;
    std::function<int()> level;
    std::string name;

    void sound(int id) { kit.effect(id); }

    Walk::Object* target() {
        return h.targetObject >= 0 ? &w.objects[std::size_t(h.targetObject)] : nullptr;
    }

    Walk::Object* at(int col, int row) {
        if (unsigned(col) > 7 || unsigned(row) > 5) {
            return nullptr;
        }
        const int i = w.grid[std::size_t(row)][std::size_t(col)];
        return i >= 0 ? &w.objects[std::size_t(i)] : nullptr;
    }

    void setTarget(int index) {
        auto& o = w.objects[std::size_t(index)];
        h.targetObject = index;
        h.targetType = o.kind;
        h.collision = o.type;
        h.radius = o.value;
    }

    void targetCell(int col, int row) {
        h.targetCol = col;
        h.targetRow = row;
        h.targetPos = cellPosition(col, row);
    }

    void message(const std::string& file, std::initializer_list<std::string> args,
                 const std::string& second = "", std::initializer_list<std::string> args2 = {}) {
        std::vector<std::string> m{kit.text.expand(formatMessage(member(file), args))};
        if (!second.empty()) {
            m.push_back(kit.text.expand(formatMessage(member(second), args2)));
        }
        showMessages(c, std::move(m));
    }

    void run();

    void go(int s) {
        w.state = s;
        w.timer = 0;
        run();
    }

    void search();                // state 300, first frame
    void forward(bool backwards); // states 1200 / 1300 after the surprise
};

void Logic::search() {
    setState(c, 11);
    h.noCount = false;
    resetPath(h);
    // 020f51c8: scent around the hamster's cell, 30 + 10 * distance.
    for (auto& row : w.scent) {
        row.fill(0);
    }
    for (int dy = -3; dy < 4; ++dy) {
        if (unsigned(h.row + dy) >= 6) {
            continue;
        }
        for (int dx = -3; dx < 4; ++dx) {
            if ((dx == 0 && dy == 0) || unsigned(h.col + dx) >= 8) {
                continue;
            }
            if (std::abs(dx) + std::abs(dy) >= 4) {
                continue;
            }
            w.scent[std::size_t(h.row + dy)][std::size_t(h.col + dx)] =
                std::int16_t(f2i(10.0f * std::sqrt(i2f(dx * dx + dy * dy))) + 0x1e);
        }
    }
    bool chosen = false;
    if (h.smell) {
        // Nearest unused food (kind 10) by turn angle plus squared distance.
        int best = -1, cost = 0x7fffffff;
        const fx32 facing = kAngle[h.direction & 3];
        for (int i = 0; i < w.objectCount; ++i) {
            const auto& o = w.objects[std::size_t(i)];
            if (o.used || o.kind != 10) {
                continue;
            }
            const int dc = h.col - o.col, dr = h.row - o.row;
            int d = atan2Index(dc, dr) - facing;
            if (d < -0x3243) {
                d += 0x6486;
            }
            if (d > 0x3243) {
                d -= 0x6486;
            }
            if (d < 0) {
                d = -d;
            }
            d += ((dc * dc + dr * dr) * 0x1000) >> 2;
            if (d < cost) {
                cost = d;
                best = i;
            }
        }
        if (best >= 0) {
            const auto& o = w.objects[std::size_t(best)];
            setTarget(best);
            h.targetPos = o.position;
            h.targetCol = o.col;
            h.targetRow = o.row;
            h.path[0] = cellPosition(o.col, o.row);
            h.pathCount = 1;
            chosen = true;
        }
    }
    if (chosen) {
        return;
    }
    // 020f5b6c: nearest unused item within four cells, preferring kind 2.
    std::vector<std::pair<int, int>> path(8);
    int count = walkSearch(w, path, 0, 4, h.col, h.row, h.direction, 2);
    if (count < 1) {
        count = walkSearch(w, path, 0, 4, h.col, h.row, h.direction, -1);
    }
    if (count < 1) {
        return;
    }
    const int index = w.grid[std::size_t(path[std::size_t(count - 1)].second)]
                            [std::size_t(path[std::size_t(count - 1)].first)];
    std::pair<int, int> last{0, 0};
    for (int i = 1; i < count; ++i) {
        const std::pair<int, int> d{path[std::size_t(i)].first - path[std::size_t(i - 1)].first,
                                    path[std::size_t(i)].second - path[std::size_t(i - 1)].second};
        const Vec p = cellPosition(path[std::size_t(i)].first, path[std::size_t(i)].second);
        if (d != last) {
            h.path[std::size_t(h.pathCount++)] = p;
            last = d;
        } else {
            h.path[std::size_t(h.pathCount - 1)] = p;
        }
    }
    const auto& o = w.objects[std::size_t(index)];
    setTarget(index);
    h.targetPos = o.position;
    h.targetCol = path[std::size_t(count - 1)].first;
    h.targetRow = path[std::size_t(count - 1)].second;
}

void Logic::forward(bool backwards) {
    if (auto* o = target(); o && o->alpha.current >= 1.0f) {
        fadeObject(c, *o);
    }
    auto d = kDirection[std::size_t(h.direction & 3)];
    if (backwards) {
        d = kDirection[std::size_t((h.direction + 2) & 3)];
    }
    int col = h.targetCol, row = h.targetRow;
    resetPath(h);
    h.targetCol = col;
    h.targetRow = row;
    h.targetType = 1;
    for (;;) {
        col += d.first;
        if (unsigned(col) > 7) {
            break;
        }
        row += d.second;
        if (unsigned(row) > 5) {
            break;
        }
        h.targetCol = col;
        h.targetRow = row;
        const int i = w.grid[std::size_t(row)][std::size_t(col)];
        if (i >= 0 && !w.objects[std::size_t(i)].used) {
            const auto& o = w.objects[std::size_t(i)];
            h.targetObject = i;
            h.targetType = o.kind;
            h.collision = o.type;
            h.radius = o.value;
            break;
        }
    }
    h.targetPos = cellPosition(h.targetCol, h.targetRow);
    h.path[std::size_t(h.pathCount)] = h.targetPos;
    ++h.pathCount;
    w.state += 1;
    w.timer = 0;
    setState(c, 4);
    run();
}

void Logic::run() {
    const int s = w.state;
    const int t = w.timer;
    // The end button works outside the start (1xx) and result (2xx) states.
    if (w.end.exists && s / 100 * 100 != 100 && s / 100 * 100 != 200) {
        kit.updateButton(w.end);
        if (kit.clicked(w.end)) {
            w.state = 600;
            sound(4);
            w.skipTail = true;
            return;
        }
    }
    auto waitHamster = [&](int first, int next, bool resetTimer = true) {
        if (t == 0) {
            setState(c, first);
            return;
        }
        if (h.state != first) {
            w.state = next;
            if (resetTimer) {
                w.timer = 0;
            }
            run();
        }
    };
    auto surprise = [&] {
        waitHamster(12, s + 1);
    }; // 020f7ab8
    auto tired = [&] {
        if (t == 0) {
            resetPath(h);
            setState(c, 10);
        } else if (h.state != 10) {
            go(2000);
        }
    }; // 020f7b0c
    auto play = [&] { // 020f6ff8: 1000 / 1400 / 1500
        auto* o = target();
        if (t == 0) {
            if (s == 1400) {
                if (o) {
                    spawnEffect(c, 0xe, o->position);
                }
                sound(441);
            } else if (s == 1500) {
                if (o) {
                    spawnEffect(c, 0xd, o->position);
                }
                sound(442);
            } else {
                sound(451);
            }
            setState(c, 9);
            return;
        }
        if (o && t == 0x2d) {
            if (s == 1400) {
                spawnEffect(c, 0xe, o->position);
            } else if (s == 1500) {
                spawnEffect(c, 0xd, o->position);
            }
        }
        if (h.state != 9) {
            if (s == 1400) {
                h.smell = true;
                spawnEffect(c, 0xf, boxCentre(h));
            } else if (s == 1500) {
                h.smell = false;
            }
            go(2000);
        }
    };
    auto afterRun = [&] { // 1201 / 1301 / 1701: wait for the dash
        if (h.state == 4) {
            return;
        }
        w.state = s + 1;
        w.timer = 0;
        if (h.targetType == 7 || h.targetType == 8 || h.targetType == 0xb) {
            w.state = 500;
            w.timer = 1;
        } else {
            sound(445);
        }
        run();
    };
    switch (s) {
    case 100:
        if (t == 0) {
            setState(c, 1);
            hamsterUpdate(c);
            cameraMode(w.camera, 1, 1.0f, 1.0f);
            cameraUpdate(w.camera);
            cameraMode(w.camera, 0, 0.1f, 0.015f);
            setState(c, 13);
            sound(0x1b3);
        } else if (t > 0x59) {
            go(101);
        }
        break;
    case 101:
        if (t == 0) {
            if (!w.slim) {
                message("msg_nrm_start.txt", {std::to_string(6 - level()), std::to_string(w.goal)});
            } else {
                message("msg_slm_start.txt", {std::to_string(int(c.state.progress.bytes[0x2be]))});
            }
        } else if (auto snd = kit.sound(); !snd || !snd->streamBusy()) {
            go(102);
        }
        break;
    case 102:
        if (kit.pressed() || t > 0x3b) {
            if (kit.pressed()) {
                sound(3);
            }
            clearMessages(c);
            go(300);
        }
        break;
    case 200:
        if (t > 0x1d) {
            go(201);
        }
        break;
    case 201:
        if (t == 0) {
            cameraMode(w.camera, 1, 0.1f, 0.015f);
            if (!w.slim) {
                if (unsigned(w.goal) <= unsigned(h.steps)) {
                    w.cleared = true;
                }
            } else if (unsigned(w.goal) < unsigned(h.steps)) {
                w.cleared = true;
            }
            resetPath(h);
            targetCell(h.targetCol, h.targetRow + 1);
            h.path[0] = h.targetPos;
            h.pathCount = 1;
            if (h.direction == 0) {
                go(s + 1);
                return;
            }
            setState(c, 2);
        } else if (h.state != 2) {
            go(s + 1);
        }
        break;
    case 202:
        if (t == 0) {
            if (auto snd = kit.sound()) {
                snd->bgmVolume(0.0f, snd->duration(1200));
            }
        } else if (t > 0x59) {
            go(s + 1);
        }
        break;
    case 203:
        if (t == 0) {
            if (!w.cleared) {
                if (auto snd = kit.sound()) {
                    snd->playTrackedEffect(0x1c0);
                }
                setState(c, 10);
            } else {
                if (auto snd = kit.sound()) {
                    snd->playTrackedEffect(0x1b4);
                }
                setState(c, 14);
            }
            const std::string steps = std::to_string(h.steps);
            if (!w.slim) {
                if (!w.cleared) {
                    message(h.steps == 1 ? "msg_nrm_fail1.txt" : "msg_nrm_fail.txt", {name, steps});
                } else if (level() < 5) {
                    message("msg_nrm_clear.txt", {name, steps, std::to_string(6 - level()),
                                                  std::to_string(5 - level())});
                } else {
                    message("msg_nrm_clear1.txt", {name, steps}, "msg_nrm_clear2.txt", {name});
                }
            } else if (!w.cleared) {
                if (h.steps == w.goal) {
                    message("msg_slm_even.txt", {name, steps});
                } else {
                    message(h.steps == 1 ? "msg_slm_fail1.txt" : "msg_slm_fail.txt", {name, steps});
                }
            } else {
                message("msg_slm_clear.txt", {name, steps});
            }
        } else if (t > 0x77) {
            auto snd = kit.sound();
            if (!snd || !snd->trackedBusy()) {
                w.state = s + 1;
                run();
            } // the timer keeps running
        }
        break;
    case 204:
        if (kit.pressed()) {
            sound(3);
            if (!nextMessage(c)) {
                go(s + 1);
            }
        }
        break;
    case 205:
        if (t > 0x3b) {
            if (auto snd = kit.sound()) {
                snd->bgmVolume(1.0f, snd->duration(1201));
            }
            go(s + 1);
        }
        break;
    case 206:
        if (t > 0x1d) {
            w.finished = true;
            go(600);
        }
        break;
    case 300:
        if (t == 0) {
            search();
        } else if (h.state != 0xb) {
            // 020f5ad8: "found" or "nothing" icon above the hamster for 30 frames.
            w.foundTimer = 0x1e;
            const bool found = h.targetObject >= 0;
            kit.select(w.found, found ? "gfx" : "gfx2");
            sound(found ? 0x1c1 : 0x1c2);
            go(400);
        }
        break;
    case 400:
        if (t > 0x1d) {
            if (h.targetObject < 0) {
                go(200);
            } else if (h.smell && h.targetType == 10) {
                go(1800);
            } else {
                go(500);
            }
        }
        break;
    case 500:
        if (t == 0) {
            setState(c, 3);
            break;
        }
        if (h.state == 3) {
            break;
        }
        {
            int next = 1000;
            switch (h.targetType) {
            case 2:
                next = 1100;
                break;
            case 3:
                next = 300;
                break;
            case 7:
                next = 1200;
                break;
            case 8:
                next = 1300;
                break;
            case 9:
                next = 1400;
                break;
            case 10:
                next = 1500;
                break;
            case 0xb:
                next = 1600;
                break;
            default:
                break;
            }
            if (auto* o = target()) {
                motionByName(*o, "action", 2, c.random);
                o->used = true;
            }
            go(next);
        }
        break;
    case 1000:
    case 1400:
    case 1500:
        play();
        break;
    case 1100:
        if (t == 0) {
            sound(447);
            setState(c, 8);
        } else if (h.state != 8) {
            go(2000);
        }
        break;
    case 1200:
    case 1300:
        if (t == 0) {
            if (s == 1200) {
                sound(446);
                w.flash = 16;
                kit.setLowerBrightness(w.flash); // 0201abc8 on 0x0400006c
                if (auto* o = target()) {
                    o->timer = 0x1e;
                }
            } else {
                sound(0x1bc);
            }
            setState(c, 12);
        } else {
            w.flash = std::max(w.flash - 1, 0);
            kit.setLowerBrightness(w.flash);
            if (h.state != 12) {
                forward(s == 1300);
            }
        }
        break;
    case 1201:
    case 1301:
    case 1701:
        afterRun();
        break;
    case 1202:
    case 1302:
    case 1702:
        surprise();
        break;
    case 1203:
    case 1303:
    case 1703:
        tired();
        break;
    case 1600:
        if (t == 0) {
            h.radius = 0;
            setState(c, 6);
            sound(h.stepSound);
        } else if (h.state != 6) {
            go(1700);
        }
        break;
    case 1700:
        if (t == 0) {
            if (auto* o = at(h.col, h.row)) {
                o->used = true;
                o->timer = 0x1e;
                sound(o->kind == 0xb ? 0x1bb : h.stepSound);
            }
            // Jump over the trampoline: two cells ahead, or the first item in reach.
            const auto d = kDirection[std::size_t(h.direction & 3)];
            int col = h.targetCol, row = h.targetRow;
            resetPath(h);
            h.targetCol = col;
            h.targetRow = row;
            h.targetType = 1;
            int steps = 0, landCol = col, landRow = row;
            for (;;) {
                col += d.first;
                if (col < 0 || col > 7) {
                    break; // unsigned short compare in the ROM
                }
                row += d.second;
                if (unsigned(row) > 5) {
                    break;
                }
                h.targetCol = col;
                h.targetRow = row;
                if (steps < 3) {
                    landCol = col;
                    landRow = row;
                    if (steps < 2) {
                        ++steps;
                        continue;
                    }
                }
                const int i = w.grid[std::size_t(row)][std::size_t(col)];
                if (i < 0 || w.objects[std::size_t(i)].used) {
                    ++steps;
                    continue;
                }
                const auto& o = w.objects[std::size_t(i)];
                if (steps != 2 || o.kind == 0xb) {
                    h.targetObject = i;
                    h.targetType = o.kind;
                    h.collision = o.type;
                    h.radius = steps == 2 ? 0 : o.value;
                    break;
                }
                steps = 3;
            }
            h.targetPos = cellPosition(h.targetCol, h.targetRow);
            const Vec land = cellPosition(landCol, landRow);
            h.path[std::size_t(h.pathCount++)] = land;
            if (land.x != h.targetPos.x || land.z != h.targetPos.z) {
                h.path[std::size_t(h.pathCount++)] = h.targetPos;
            }
            setState(c, 7);
        } else if (h.state != 7) {
            if (auto* o = at(h.col, h.row); o && !o->used) {
                if (o->kind == 0xb) {
                    go(1700);
                    return;
                } // 0x6a4: jump again
                o->used = true;
                fadeObject(c, *o);
                sound(h.stepSound);
            }
            setState(c, 4);
            go(s + 1);
            return;
        }
        break;
    case 1800:
        if (t == 0) {
            setState(c, 5);
            h.noCount = true;
        } else if (h.state != 5) {
            sound(445);
            go(s + 1);
        }
        break;
    case 1801:
        waitHamster(12, 1500);
        break;
    case 2000:
        if (t == 0) {
            if (auto* o = target(); o && o->alpha.current >= 1.0f) {
                o->used = true;
                fadeObject(c, *o);
            }
            h.radius = 0;
            if (h.pos.x == h.targetPos.x && h.pos.z == h.targetPos.z) {
                go(300);
                return;
            }
            setState(c, 3);
        } else if (h.state != 3) {
            go(s + 1);
        }
        break;
    case 2001:
        if (t > 0x1d) {
            go(300);
        }
        break;
    default:
        break;
    }
}
} // namespace

void HamsterGame::walkLogic() {
    auto& w = *walk_;
    auto& h = w.hamster;
    Context c{kit_, state_, random_, w};
    Logic logic{c,
                w,
                h,
                kit_,
                [this](const std::string& f) { return kit_.member("hm", f); },
                [this] { return level(); },
                hamsterName()};
    w.skipTail = false;
    if (researchTrace) {
        char line[200];
        std::snprintf(line, sizeof line,
                      "walk %d t %d ham %d pos %d %d face %d dir %d cell %d %d steps %d target %d "
                      "type %d path %d/%d anim %d %d %d %d",
                      w.state, w.timer, h.state, h.pos.x, h.pos.z, h.facing, h.direction, h.col,
                      h.row, h.steps, h.targetObject, h.targetType, h.pathIndex, h.pathCount,
                      h.start, h.end, h.step, h.frame);
        researchTrace(kit_.polls, line);
        if (w.state == 100 && w.timer == 0) {
            std::string objects = "objects random " + std::to_string(random_);
            for (int i = 0; i < w.objectCount; ++i) {
                objects += " " + std::to_string(w.objects[std::size_t(i)].frame);
            }
            researchTrace(kit_.polls, objects);
        }
    }
    logic.run();
    if (w.skipTail) {
        return;
    }
    // Common tail (020f7cac): camera targets, actors, effects, scent decay.
    w.camera.a = h.pos;
    w.camera.a.y = 0xa00;
    w.camera.b = h.targetPos;
    w.camera.b.y = 0xa00;
    cameraUpdate(w.camera);
    update(w.map);
    if (w.foundTimer >= 0 && --w.foundTimer < 0) {
        w.foundTimer = -1;
        w.foundX = w.foundY = 0;
    } // 020f5abc
    hamsterUpdate(c);
    if (h.smell && smellWave(w)) {
        spawnEffect(c, 0xf, centre(h));
    }
    for (int i = 0; i < w.objectCount; ++i) {
        auto& o = w.objects[std::size_t(i)];
        objectUpdate(c, o);
        if (o.timer >= 0 && --o.timer < 0) {
            fadeObject(c, o);
        }
    }
    for (auto& e : w.effects) {
        effectUpdate(e);
    }
    for (auto& row : w.scent) {
        for (auto& v : row) {
            if (v > 0) {
                --v;
            }
        }
    }
    ++w.timer;
}

void HamsterGame::walkDraw() {
    auto& w = *walk_;
    auto& h = w.hamster;
    // 3D: camera, map, visible items, hamster, effects; the frame is swapped
    // with the 2D buffers at the next present (0206ce04).
    cameraToGlobal(w);
    draw(w, w.map);
    for (int i = 0; i < w.objectCount; ++i) {
        auto& o = w.objects[std::size_t(i)];
        if (o.alpha.current > 0.0f) {
            draw(w, o);
        }
    }
    draw(w, h);
    for (auto& e : w.effects) {
        if (e.active) {
            draw(w, e);
        }
    }
    const std::uint32_t swap = 0;
    w.gpu.command(SWAP_BUFFERS, {&swap, 1});
    kit_.layer3D.resize(256 * 192);
    for (std::size_t i = 0; i < kit_.layer3D.size(); ++i) {
        const auto& p = w.gpu.frame()[i];
        kit_.layer3D[i] = std::uint32_t(p.r) | std::uint32_t(p.g) << 8 | std::uint32_t(p.b) << 16 |
                          std::uint32_t(p.a) << 24;
    }
    // Lower 3D sprites.
    if (w.found.loaded() && w.foundTimer > 0) {
        Vec p = centre(h);
        p.y += 0x800;
        int x = 0, y = 0;
        project(w.camera, p, x, y);
        w.foundX = std::int16_t(x - 0x10);
        w.foundY = std::int16_t(y - 0x20);
        float u = 3.0f * i2f(w.foundTimer);
        u = u / 30.0f;
        u = u - 2.0f;
        u = u * u;
        u = 1.0f - u;
        if (!(u >= 0.0f)) {
            u = 0.0f;
        }
        w.found.x = w.foundX;
        w.found.y = std::int16_t(f2i(i2f(w.foundY) - 24.0f * u));
        kit_.draw(w.found);
    }
    if (w.walkText.loaded()) {
        kit_.draw(w.walkText);
    }
    auto digits = [&](SceneKit::SpriteObject& s, unsigned value, int x, int step, int y) {
        if (!s.loaded()) {
            return;
        }
        for (int i = 0; i < 3; ++i) {
            kit_.select(s, std::to_string(value % 10));
            value /= 10;
            s.x = x;
            s.y = y;
            kit_.draw(s);
            x -= step;
        }
    };
    digits(w.yellow, unsigned(h.steps), 0xcb, 7, 4);
    digits(w.white, unsigned(w.goal), 0xea, 7, 4);
    if (w.end.exists) {
        const int band = w.state / 100 * 100;
        if (band != 100 && band != 200 && band != 600 && band != 700) {
            kit_.drawButton(w.end);
        }
    }
    if (w.window.loaded() && w.messageCount > 0) {
        kit_.draw(w.window);
    }
    // Upper 2D sprites.
    const int band = w.state / 100 * 100;
    if (h.targetObject >= 0 && (band == 400 || band == 500)) {
        if (w.bubble.loaded()) {
            kit_.select(w.bubble, "gfx");
            w.bubble.x = 0xc;
            w.bubble.y = 0x2c;
            kit_.draw(w.bubble);
        }
        if (w.icon.loaded()) {
            kit_.select(w.icon, std::to_string(w.objects[std::size_t(h.targetObject)].kind - 1));
            w.icon.x = 0x1c;
            w.icon.y = 0x3c;
            kit_.draw(w.icon);
        }
    }
    if (w.levelNumber.loaded()) {
        kit_.select(w.levelNumber, std::to_string(6 - level()));
        w.levelNumber.x = 0x9e;
        w.levelNumber.y = 0xc;
        kit_.draw(w.levelNumber);
    }
    if (w.levelText.loaded()) {
        kit_.select(w.levelText, std::to_string(level()));
        w.levelText.x = 0xae;
        w.levelText.y = 0xd;
        kit_.draw(w.levelText);
    }
    digits(w.count, unsigned(h.steps), 0xd9, 0x15, 0x45);
    digits(w.levelNumber, unsigned(w.goal), 0xea, 7, 0x6e);
    digits(w.levelNumber, unsigned(state_.progress.bytes[0x2be]), 0xea, 7, 0x87);
}
} // namespace picarat

namespace picarat {
HamsterGame::HamsterGame(GraphicsAssets& assets, PresentFrame present, AudioService* audio,
                         GameState& state, std::uint32_t& random)
    : kit_(assets, std::move(present), audio, SceneKind::Hamster), state_(state), random_(random) {}

HamsterGame::~HamsterGame() = default;
} // namespace picarat
