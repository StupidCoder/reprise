// The third adventure's scene: objects, tweens, messages, screens. See
// a3_lscr_scene.hpp; the commands are in a3_lscr_scene_commands.cpp.
#include "system/a3_lscr_scene.hpp"
#include "sound/a3_sound.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <numbers>
#include <stdexcept>

namespace picarat::lscr {

// --- Fixed point ----------------------------------------------------------------

namespace fx {
int mul(int a, int b) {
    return int((std::int64_t(a) * b + 0x800) >> 12);
}

int div(int a, int b) {
    // 02005df4/02005d1c: the divider's 32.32 quotient, rounded to 20.12.
    if (b == 0) {
        return 0;
    }
    const std::int64_t q = (std::int64_t(a) << 32) / b;
    return int((q + 0x80000) >> 20);
}

int fromFloat(float v) {
    const float f = v * 4096.0f;
    if (std::isnan(f)) {
        return 0;
    }
    if (f >= 2147483648.0f) {
        return 0x7fffffff;
    }
    if (f <= -2147483648.0f) {
        return int(0x80000000u);
    }
    return int(f);
}

float toFloat(int v) {
    return float(v) / 4096.0f;
}

// The game's table equals round(sin(2 pi i / 4096) * 4096) in every entry
// (checked against the ROM's table at 0202753c).
int sine(int i) {
    return int(std::lround(std::sin(2 * std::numbers::pi * (i & 0xfff) / 4096.0) * 4096.0));
}

int cosine(int i) {
    return int(std::lround(std::cos(2 * std::numbers::pi * (i & 0xfff) / 4096.0) * 4096.0));
}
} // namespace fx

// --- Tweens ---------------------------------------------------------------------

namespace {
// 0203b930: the curve applied to f = remaining / total (1 at the start).
int shape(int curve, int f) {
    switch (curve) {
    case 2:
        return fx::kOne - fx::mul(f - fx::kOne, f - fx::kOne);
    case 3:
        return fx::mul(f, f);
    case 4:
        if (f < 0x800) {
            return 2 * fx::mul(f, f);
        }
        return fx::kOne - 2 * fx::mul(fx::kOne - f, fx::kOne - f);
    case 5:
        return fx::sine((f >> 1) & 0xfff);
    case 6:
        return 4 * fx::mul(f, fx::kOne - f);
    default:
        return f;
    }
}
} // namespace

template <int N>
void Tween<N>::set(const V& v) {
    curve = 0;
    locked = false;
    remaining = total = 0;
    value = target = start = v;
}

template <int N>
bool Tween<N>::to(int curveKind, const V& to, int duration, bool lock) {
    if (locked) {
        return false;
    }
    if (to != value && curveKind != 0 && duration > 0) {
        curve = curveKind;
        locked = lock;
        remaining = total = duration;
        start = value;
        target = to;
        return true;
    }
    set(to);
    return false;
}

template <int N>
void Tween<N>::step(int elapsed) {
    if (!active()) {
        return;
    }
    remaining -= elapsed;
    if (curve == 7) {
        // Endless: wrap the remaining time into the period, then follow
        // (1 + cos) / 2 from the target (f = 1) through the start and back.
        const int whole = fx::div(remaining, total) & ~0xfff;
        remaining -= fx::mul(whole, total);
        const int f = fx::div(remaining, total);
        const int g = ((fx::cosine(-f) + fx::kOne) * 0x800 + 0x800) >> 12;
        for (int i = 0; i < N; ++i) {
            value[i] = start[i] + fx::mul(target[i] - start[i], g);
        }
        return;
    }
    if (remaining < 1) {
        const V end = curve == 5 || curve == 6 ? start : target;
        set(end);
        return;
    }
    const int f = shape(curve, fx::div(remaining, total));
    for (int i = 0; i < N; ++i) {
        const int d = fx::mul(target[i] - start[i], f);
        value[i] = curve == 5 || curve == 6 ? start[i] + d : target[i] - d;
    }
}

template struct Tween<1>;
template struct Tween<2>;
template struct Tween<3>;
template struct Tween<10>;

namespace {
constexpr int kPi = 0x3244, kTwoPi = 0x6488;

// 02005e58 and neighbours: an angle into (-pi, pi].
int normalise(int a) {
    if (std::abs(a) <= kPi) {
        return a;
    }
    int r = (a + kPi) % kTwoPi;
    if (r < 0) {
        r += kTwoPi;
    }
    return r - kPi;
}

// A point turned by `angle` (fixed-point radians, the game's sine table).
std::array<int, 2> turn(std::array<int, 2> p, int angle) {
    const int i = int(std::lround(double(angle) * 4096.0 / double(kTwoPi))) & 0xfff;
    const int c = fx::cosine(i), s = fx::sine(i);
    return {fx::mul(p[0], c) - fx::mul(p[1], s), fx::mul(p[0], s) + fx::mul(p[1], c)};
}
} // namespace

// --- Objects --------------------------------------------------------------------

namespace {
int sequenceLength(const adventure3::Animation::Sequence* s) {
    int n = 0;
    if (s) {
        for (const auto& st : s->steps) {
            n += int(st.duration);
        }
    }
    return n;
}

const adventure3::Animation::Step* currentStep(const Object& o) {
    if (!o.sequence || o.sequence->steps.empty()) {
        return nullptr;
    }
    int t = o.time >> 12, acc = 0;
    for (const auto& st : o.sequence->steps) {
        acc += int(st.duration);
        if (t < acc) {
            return &st;
        }
    }
    return &o.sequence->steps.back();
}

// 020667d4: one frame of a sprite's playback.
void advance(Object& o, int elapsed) {
    if (!o.sequence || o.paused) {
        return;
    }
    const int length = sequenceLength(o.sequence) << 12;
    bool ended = false;
    o.time += fx::mul(o.speed, elapsed);
    if (!o.once && length > 0) {
        while (o.time >= length) {
            o.time -= length;
            ++o.loops;
            ended = true;
        }
    } else if (o.time >= length) {
        o.time = length;
        o.finished = true;
        o.loops = 1;
        ended = true;
    }
    if (ended && o.queued) {
        o.sequence = o.queued;
        o.once = o.queuedOnce;
        o.time = 0;
        o.loops = 0;
        o.finished = false;
        o.queued = nullptr;
    }
}

// A pen line of a canvas (02045a0c): every pixel within half the pen width
// (4721) of the segment between two points - a band with round ends.
void inkLine(Surface& s, int x0, int y0, int x1, int y1, std::uint16_t color, int width = 3,
             const Rect* clip = nullptr) {
    const double w = width / 2.0, dx = x1 - x0, dy = y1 - y0;
    const double length2 = dx * dx + dy * dy;
    const int reach = int(std::ceil(w));
    for (int y = std::min(y0, y1) - reach; y <= std::max(y0, y1) + reach; ++y) {
        for (int x = std::min(x0, x1) - reach; x <= std::max(x0, x1) + reach; ++x) {
            if (x < 0 || y < 0 || x >= int(s.width) || y >= int(s.height)) {
                continue;
            }
            if (clip && (x < clip->x || y < clip->y || x >= clip->x + clip->width ||
                         y >= clip->y + clip->height)) {
                continue;
            }
            double t = length2 > 0 ? ((x - x0) * dx + (y - y0) * dy) / length2 : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            const double ex = x0 + t * dx - x, ey = y0 + t * dy - y;
            if (ex * ex + ey * ey <= w * w + 1e-9) {
                s.pixels[size_t(y) * s.width + size_t(x)] = color;
            }
        }
    }
}

// Draws `src` (its centre the anchor) turned by `angle` and scaled about
// (x, y), sampling the nearest source pixel.
void drawTransformed(Surface& dst, const Surface& src, int x, int y, int angle,
                     const std::array<int, 2>& scale) {
    const int sx = scale[0] ? scale[0] : fx::kOne, sy = scale[1] ? scale[1] : fx::kOne;
    const int radius = int(std::hypot(double(src.width), double(src.height)) / 2.0 *
                           std::max(std::abs(sx), std::abs(sy)) / 4096.0) +
                       1;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int px = x + dx, py = y + dy;
            if (px < 0 || py < 0 || px >= int(dst.width) || py >= int(dst.height)) {
                continue;
            }
            // Pixel centres, as the hardware samples (ROM captures of
            // puzzle 19's turned keys).
            auto l = turn({(dx << 12) + 0x800, (dy << 12) + 0x800}, -angle);
            const int u = (fx::div(l[0], sx) >> 12) + int(src.width / 2);
            const int v = (fx::div(l[1], sy) >> 12) + int(src.height / 2);
            if (u < 0 || v < 0 || u >= int(src.width) || v >= int(src.height)) {
                continue;
            }
            const auto c = src.pixels[size_t(v) * src.width + size_t(u)];
            if (c & 0x8000) {
                dst.pixels[size_t(py) * dst.width + size_t(px)] = c;
            }
        }
    }
}

int alpha5(int alpha) {
    return std::clamp((alpha * 31 + 0x800) >> 12, 0, 31);
}

std::array<int, 4> turnRect(std::array<int, 4> r, int mirror, int step);
} // namespace

// --- Scene ----------------------------------------------------------------------

Scene::Scene(adventure3::Assets& a, const GameText& t, Host h, GameState* state)
    : machine_(state ? std::make_unique<Machine>(*state) : std::make_unique<Machine>()),
      machine(*machine_), assets(a), text(t), host(std::move(h)) {
    installCore(machine);
    machine.ownerCall = [](Call& c, const std::string& section, std::span<const Value> args) {
        // 1100: the owner's thread starts over on the section.
        c.thread.restart(section, args);
    };
    for (auto& b : brightness) {
        b.set({0});
    }
    for (auto& screen : layerAlpha) {
        for (auto& l : screen) {
            l.set({fx::kOne});
        }
    }
    messagePalette = {0x0000, 0x0000, 0x7fff, 0x001f, 0x0000,
                      0x0000, 0x0000, 0x0000, 0x0000, 0x0000};
    installScene(*this);
}

Scene::~Scene() = default;

void Scene::load(const std::string& path) {
    machine.load(assets.script(path), path);
}

Thread& Scene::start(const std::string& section, std::span<const Value> variables,
                     const std::string& label) {
    main_ = std::make_unique<Thread>(machine);
    for (size_t i = 0; i < variables.size() && i < 16; ++i) {
        main_->variables[i] = variables[i];
    }
    if (!main_->call(section, {}, label)) {
        throw std::runtime_error("scene section " + section + " is missing");
    }
    return *main_;
}

Thread& Scene::restart(const std::string& section, const std::string& label) {
    if (!main_) {
        return start(section, {}, label);
    }
    main_->stop();
    if (!main_->call(section, {}, label)) {
        throw std::runtime_error("scene section " + section + " is missing");
    }
    return *main_;
}

bool Scene::running() const {
    return main_ && main_->running();
}

void Scene::frame(const Input& input) {
    // The input record the objects see (020ae5e4, 020afc04, 020b0350:
    // +0xec) filters jitter: per axis it keeps its place while the pen is
    // within a pixel of it; otherwise it follows, a pixel short when the pen
    // moves to larger coordinates and exactly when it moves to smaller ones.
    // A press starts afresh. (Fits every frame probed on puzzle 19 - one
    // pixel a frame - and puzzle 32 - four and six pixels, both ways; the
    // routine is not identified, a hypothesis.)
    if (input.pressed || !input_.held) {
        lastPen_ = {input.x, input.y};
    } else {
        const std::array<int, 2> now{input.x, input.y};
        for (std::size_t k = 0; k < 2; ++k) {
            const int d = now[k] - lastPen_[k];
            if (d > 1) {
                lastPen_[k] = now[k] - 1;
            } else if (d < -1) {
                lastPen_[k] = now[k];
            }
        }
    }
    // The scene's own record (+0x1e4) keeps where the pen came down (+0x44)
    // and a smoothed place (+0x5c), per axis: after a move of two pixels or
    // more three quarters of the new place and a quarter of the last; a
    // one-pixel difference is held until it has lasted four frames (fits
    // every frame probed on puzzles 18 and 67; the routine is not
    // identified). Both stay when the pen is lifted.
    if (input.pressed) {
        pressPen_ = smoothPen_ = {input.x, input.y};
        smoothHeld_ = {0, 0};
    } else if (input.held) {
        const std::array<int, 2> now{input.x, input.y}, last{input_.x, input_.y};
        for (std::size_t k = 0; k < 2; ++k) {
            const int d = std::abs(now[k] - smoothPen_[k]);
            if (d > 1) {
                smoothPen_[k] = (3 * now[k] + last[k]) / 4;
                smoothHeld_[k] = 0;
            } else if (d == 1) {
                if (++smoothHeld_[k] >= 4) {
                    smoothPen_[k] = now[k];
                    smoothHeld_[k] = 0;
                }
            } else {
                smoothHeld_[k] = 0;
            }
        }
    }
    input_ = input;
    loading = false;
    // The puzzle engine's step (vtable 0xc0) comes first; the common step
    // (020f3788) then dispatches the pen with input allowed as it decided.
    if (engine) {
        engine->frame(*this);
    }
    dispatchInput();
    if (engine) {
        engine->afterInput(*this);
    }
    if (main_) {
        main_->run();
    }
    for (auto& b : brightness) {
        b.step(fx::kOne);
    }
    for (auto& screen : layerAlpha) {
        for (auto& l : screen) {
            l.step(fx::kOne);
        }
    }
    for (auto& f : backgroundFilter) {
        f.record.step(fx::kOne);
    }
    for (auto& [animation, f] : tints) {
        f.record.step(fx::kOne);
    }
    stage.scroll.step(fx::kOne);
    for (auto& part : stage.parts) {
        advance(part, fx::kOne);
    }
    // 020adc20 / 020667d4: each object, in creation order, runs its thread
    // and then moves - unless it is hidden or stopped.
    // 020b1ff4: the physics step first, then every object in draw-list
    // order runs its thread and moves - unless it is hidden or stopped.
    if (physics) {
        physicsStep();
    }
    updating_ = true;
    const auto order = order_;
    for (Object* op : order) {
        Object& o = *op;
        if (o.dead || !o.moves()) {
            continue;
        }
        if (o.thread) {
            o.thread->run();
        }
        if (o.memo) {
            memoStep(o);
        }
        o.alpha.step(fx::kOne);
        o.position.step(fx::kOne);
        o.offset.step(fx::kOne);
        o.rotation.step(fx::kOne);
        if (!o.rotation.active()) {
            o.rotation.set({normalise(o.rotation.value[0])}); // 02042738
        }
        o.scale.step(fx::kOne);
        o.shake.step(fx::kOne);
        if (o.shake.value[0] != 0 || o.shake.value[1] != 0) {
            // 020667d4: a random place within the shake, each frame (the
            // game's own random floats; here the game state's sequence).
            for (int k = 0; k < 2; ++k) {
                const int amp = std::abs(o.shake.value[size_t(k)]);
                const int r = machine.state.random() % 4097; // 0..1
                o.jitter[size_t(k)] = int((std::int64_t(2 * r - 4096) * amp) >> 12);
            }
        } else {
            o.jitter = {0, 0};
        }
        if (o.animation) {
            // 020667d4: a speaking object shows its sequence's "*" variant
            // (the mouth moving) and goes back to the plain one when it
            // stops; either switch starts the sequence over.
            if (o.talking && o.sequence && o.sequence->name.find('*') == std::string::npos) {
                if (const auto* star = o.animation->sequence(o.sequence->name + "*")) {
                    play(o, star, o.playFlags);
                }
            }
            advance(o, fx::kOne);
            if (!o.talking && o.sequence && !o.sequence->name.empty() &&
                o.sequence->name.back() == '*') {
                const std::string plain = o.sequence->name.substr(0, o.sequence->name.size() - 1);
                if (const auto* p = o.animation->sequence(plain)) {
                    play(o, p, o.playFlags);
                }
            }
        }
        if (o.kind == Object::Kind::Message) {
            // 0206b5b4: the message reveals at its speed; a tap or A
            // (0206b508) skips a <T> part or ends a wait for a tap.
            if (o.behaviour) {
                o.behaviour->update(*this, o);
            } else {
                o.text.update(input_.pressed || (input_.keysPressed & 1u));
            }
        }
    }
    updating_ = false;
    // Then the engine's own step after the common update (vtable 0xe4).
    if (engine) {
        engine->update(*this);
    }
    objects_.remove_if([](const Object& o) { return o.dead; });
    std::erase_if(threads_, [](const std::unique_ptr<Thread>& t) { return t->owner < 0; });
}

void Scene::erase(Object* o) {
    // Out of the draw list and every link at once; the storage goes after
    // the frame's update (objects delete each other from their threads).
    std::erase(order_, o);
    if (o->thread) {
        o->thread->stop();
        o->thread->owner = -1;
        o->thread = nullptr;
    }
    o->dead = true;
    if (!updating_) {
        objects_.remove_if([](const Object& x) { return x.dead; });
    }
}

int Scene::hit(const Object& o, int x, int y) const {
    // 020665a4: the pen in the object's own frame (its turn and scale
    // undone); the touch areas (4121) give their id, else the shown frame
    // gives 3 where it has pixels.
    const auto [px, py] = pixel(o);
    auto local = turn({(x - px) << 12, (y - py) << 12}, -totalRotation(o));
    int sx = o.scale.value[0] ? o.scale.value[0] : fx::kOne;
    int sy = o.scale.value[1] ? o.scale.value[1] : fx::kOne;
    sx = o.mirror & 1 ? -sx : sx;
    sy = o.mirror & 2 ? -sy : sy;
    const int lx = (fx::div(local[0], sx) + 0x800) >> 12,
              ly = (fx::div(local[1], sy) + 0x800) >> 12;
    if (!o.areas.empty()) {
        for (const auto& a : o.areas) {
            if (lx >= a.x && ly >= a.y && lx < a.x + a.w && ly < a.y + a.h) {
                return a.id;
            }
        }
        return -1;
    }
    // 0204dd14 / 02049c34: else the shown frame (or picture) where it has
    // pixels, by 8x8 tiles.
    if (o.animation) {
        if (const auto* st = currentStep(o)) {
            return o.animation->touches(st->frame, lx - st->x + o.sequence->originX,
                                        ly - st->y + o.sequence->originY)
                       ? 3
                       : -1;
        }
        return -1;
    }
    if (o.picture && o.picture->tileUsed(lx, ly)) {
        return 3;
    }
    return -1;
}

namespace {
// 0200606c: the angle of a vector (fixed-point radians).
int angleOf(int x, int y) {
    return int(std::atan2(double(y), double(x)) * 4096.0);
}
} // namespace

void Scene::grabFor(Object& o, bool turning) {
    // Where the pen took hold: for moving, relative to the position;
    // for turning, relative to the drawn place, with the angle then.
    const auto& in = lastPen_;
    if (turning) {
        const auto [px, py] = pixel(o);
        o.grab = {in[0] - px, in[1] - py};
    } else {
        o.grab = {in[0] - ((o.position.value[0] + 0x800) >> 12),
                  in[1] - ((o.position.value[1] + 0x800) >> 12)};
    }
    o.grabAngle = o.rotation.value[0];
}

void Scene::dragged(Object& o) {
    // The position follows the pen; on a grid it snaps to the cell under
    // it whenever that changes (02065974 / 02065994).
    const std::array<int, 2> pen = lastPen_;
    dragTo(o, {(pen[0] - o.grab[0]) << 12, (pen[1] - o.grab[1]) << 12});
}

void Scene::dragTo(Object& o, std::array<int, 2> p) {
    if (o.grid.size <= 0) {
        o.position.set(p);
        return;
    }
    const auto before = o.cell;
    Object probe = o;
    snap(probe, p);
    if (probe.cell != before) {
        snap(o, p);
    }
}

std::vector<std::array<int, 4>> Scene::wallsAround(const Object& self) const {
    return areasWith(4, &self);
}

std::vector<std::array<int, 4>> Scene::areasWith(int id, const Object* except) const {
    std::vector<std::array<int, 4>> out;
    for (const Object& o : objects_) {
        if (&o == except || o.dead || !o.switches[0].on || o.kind == Object::Kind::Message ||
            o.kind == Object::Kind::Canvas) {
            continue;
        }
        const auto [px, py] = pixel(o);
        for (const auto& a : o.areas) {
            if (a.id == id) {
                auto r = turnRect({a.x, a.y, a.x + a.w, a.y + a.h}, o.mirror, o.step);
                out.push_back({r[0] + px, r[1] + py, r[2] + px, r[3] + py});
            }
        }
    }
    return out;
}

std::array<int, 4> Scene::turnedTouchBox(const Object& o) const {
    return turnRect(touchBox(o), o.mirror, o.step);
}

bool Scene::hits(const Object& o, std::array<int, 2> at,
                 const std::vector<std::array<int, 4>>& walls) const {
    // 020b0048: the object's own id-4 areas at the place, their bounding
    // box against each wall first (overlaps are strict).
    std::vector<std::array<int, 4>> own;
    for (const auto& a : o.areas) {
        if (a.id == 4) {
            own.push_back({a.x + at[0], a.y + at[1], a.x + a.w + at[0], a.y + a.h + at[1]});
        }
    }
    if (own.empty()) {
        return false;
    }
    std::array<int, 4> box = own[0];
    for (const auto& r : own) {
        box = {std::min(box[0], r[0]), std::min(box[1], r[1]), std::max(box[2], r[2]),
               std::max(box[3], r[3])};
    }
    auto overlap = [](const std::array<int, 4>& a, const std::array<int, 4>& b) {
        return a[0] < b[2] && a[1] < b[3] && a[2] > b[0] && a[3] > b[1];
    };
    for (const auto& w : walls) {
        if (!overlap(box, w)) {
            continue;
        }
        if (own.size() < 2) {
            return true;
        }
        for (const auto& r : own) {
            if (overlap(r, w)) {
                return true;
            }
        }
    }
    return false;
}

void Scene::slid(Object& o) {
    // 020b0350: the piece walks towards the pen a pixel at a time - one
    // pixel along the longer axis, the rounded share along the other - for
    // at most eight steps a frame; a blocked diagonal step tries the longer
    // axis alone, then the shorter.
    const auto walls = wallsAround(o);
    const std::array<int, 2> target{lastPen_[0] - o.grab[0], lastPen_[1] - o.grab[1]};
    auto cur = pixel(o);
    const auto start = cur;
    const std::array<std::array<int, 2>, 4> unit{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
    auto major = [](int dx, int dy) {
        if (dx == 0 && dy == 0) {
            return -1;
        }
        return std::abs(dx) < std::abs(dy) ? (dy < 0 ? 3 : 1) : (dx < 0 ? 2 : 0);
    };
    auto minor = [](int dx, int dy) {
        if (std::abs(dx) < std::abs(dy)) {
            return dx == 0 ? -1 : dx < 1 ? 2 : 0;
        }
        return dy == 0 ? -1 : dy < 1 ? 3 : 1;
    };
    int budget = 8;
    for (;;) {
        if (cur == target) {
            break;
        }
        const int dx = target[0] - cur[0], dy = target[1] - cur[1];
        std::array<int, 2> next = cur;
        if (std::abs(dx) < std::abs(dy)) {
            const float q = float(dx) / float(dy);
            if (dy < 0) {
                next[1] -= 1;
                next[0] -= int(q - 0.5f);
            } else {
                next[1] += 1;
                next[0] += int(0.5f + q);
            }
        } else {
            const float q = float(dy) / float(dx);
            if (dx < 0) {
                next[0] -= 1;
                next[1] -= int(q - 0.5f);
            } else {
                next[0] += 1;
                next[1] += int(0.5f + q);
            }
        }
        if (!hits(o, next, walls)) {
            cur = next;
            if (--budget < 1) {
                break;
            }
            continue;
        }
        const int m = major(dx, dy);
        if (m != -1) {
            const std::array<int, 2> a{cur[0] + unit[size_t(m)][0], cur[1] + unit[size_t(m)][1]};
            if (!hits(o, a, walls)) {
                cur = a;
                if (--budget > 0) {
                    continue;
                }
                break;
            }
        }
        const int n = minor(dx, dy);
        if (n == -1) {
            break;
        }
        const std::array<int, 2> b{cur[0] + unit[size_t(n)][0], cur[1] + unit[size_t(n)][1]};
        if (hits(o, b, walls)) {
            break;
        }
        cur = b;
        if (--budget < 0) {
            break;
        }
    }
    // Its move sound (flags[0]) plays while it moves and stops after a still
    // frame (020601a0 / 020601ec / 02060210).
    if (host.sound) {
        const bool moved = cur != start;
        if (!host.sound->effectPlaying(o.flags[0])) {
            if (moved) {
                host.sound->playEffect(o.flags[0]);
            }
        } else if (!moved) {
            host.sound->stopEffect(o.flags[0], 1);
        }
    }
    o.position.set({cur[0] << 12, cur[1] << 12});
}

void Scene::turned(Object& o) {
    // The angle turns with the pen around the drawn place; with steps it
    // jumps to the step under the pen when that changes (020ae0xx).
    const std::array<int, 2> pen = lastPen_;
    const auto [px, py] = pixel(o);
    const int a =
        normalise(angleOf(pen[0] - px, pen[1] - py) - angleOf(o.grab[0], o.grab[1]) + o.grabAngle);
    if (o.steps <= 0) {
        o.rotation.set({a});
    } else if (stepFor(o, a) != o.step) {
        turnToward(o, a);
    }
}

bool Scene::buttonInput(Object& o, bool available) {
    // Buttons (020ae5e4), switches (020add78) and pieces (020afc04) take the
    // pen when it touches them; the touch area hit decides between pressing
    // (3), moving (1) and turning (2).
    const Input& in = input_;
    const bool active = available && o.takesInput();
    auto idle = [&](const std::string& section) {
        o.touch = 0;
        o.grab = {0, 0};
        callOn(o, section);
        return false;
    };
    if (o.touch == 0) {
        if (!active || !in.pressed) {
            return available;
        }
        const int h = hit(o, lastPen_[0], lastPen_[1]);
        if (h != 1 && h != 2 && h != 3) {
            return available;
        }
        switch (o.style) {
        case Object::Style::Button:
            o.touch = h == 1 ? 2 : h == 2 ? 3 : 1;
            grabFor(o, h == 2);
            callOn(o, o.touchedSection);
            break;
        case Object::Style::Switch:
            o.touch = h == 1 ? 2 : h == 2 ? 3 : 1;
            grabFor(o, h == 2);
            if (h == 3) {
                callOn(o, o.touchedSection);
            }
            break;
        case Object::Style::Piece:
            o.touch = h == 2 ? 3 : 2;
            grabFor(o, h == 2);
            callOn(o, h == 2 ? o.clickedSection : o.touchedSection);
            break;
        case Object::Style::Script:
            return available;
        case Object::Style::Slider:
            if (h != 1) {
                return available;
            }
            o.touch = 1;
            grabFor(o, false);
            callOn(o, o.touchedSection);
            break;
        }
        return false;
    }
    switch (o.style) {
    case Object::Style::Script:
        return available;
    case Object::Style::Slider:
        // 020b0350: lifted, it snaps to its grid cell; its move sound stops.
        if (!active || !in.held) {
            if (o.grid.size > 0) {
                snap(o, o.position.value);
            }
            if (host.sound) {
                host.sound->stopEffect(o.flags[0], 0);
            }
            return idle(o.releasedSection);
        }
        slid(o);
        return false;
    case Object::Style::Button:
        if (o.touch == 4) {
            return o.thread && o.thread->running() ? false : idle(o.releasedSection);
        }
        if (!active) {
            return idle(o.releasedSection);
        }
        if (!in.held) {
            if (o.touch == 1) {
                o.touch = 4;
                o.grab = {0, 0};
                callOn(o, o.clickedSection);
                return false;
            }
            return idle(o.releasedSection);
        }
        if (o.touch == 1) {
            return hit(o, lastPen_[0], lastPen_[1]) > 0 ? false : idle(o.releasedSection);
        }
        break;
    case Object::Style::Switch:
        if (o.touch == 1) {
            if (!(o.thread && o.thread->running())) {
                o.touch = 0;
            }
            return false;
        }
        if (!active || !in.held) {
            o.touch = 0;
            o.grab = {0, 0};
            return false;
        }
        break;
    case Object::Style::Piece:
        if (!active || !in.held) {
            return idle(o.releasedSection);
        }
        break;
    }
    if (o.touch == 2) {
        dragged(o);
    } else if (o.touch == 3) {
        turned(o);
    }
    return false;
}

void Scene::dispatchInput() {
    // The pen goes to the objects front to back until one takes it (the
    // input handler of each object, vtable entry 7). Sprites take a held
    // pen that touches them (02066760); buttons follow their state machine.
    bool available = !inputBlocked;
    // Memo pads take the pen before the objects: their ink shows over the
    // memo's board (a sprite at layer 0), and presses in their area draw
    // (ROM captures of the puzzle memo; the pad's own layer order is not
    // recovered, hypothesis).
    for (Object* o : order_) {
        if (o->kind == Object::Kind::Canvas && o->memo && o->screen == touchScreen() &&
            o->moves()) {
            memoInput(*o, available);
        }
    }
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
        Object& o = **it;
        if (o.screen != touchScreen() || !o.moves()) {
            continue;
        }
        if (o.kind == Object::Kind::Button && o.style != Object::Style::Script) {
            available = buttonInput(o, available);
        } else if (o.kind == Object::Kind::Canvas && o.circling) {
            circleInput(o, available);
        } else if (o.kind == Object::Kind::Canvas && o.memo) {
            continue;
        } else if (o.kind == Object::Kind::Canvas && o.writing) {
            // The paint layer's input (ov0 0206c9d8): a stroke starts on a
            // press inside the box and keeps the pen until it is lifted.
            const bool drawing = o.writing->box(0).drawing;
            if ((available && o.takesInput()) || drawing) {
                Touch t{input_.held, input_.pressed, input_.released, input_.x, input_.y};
                if (o.writing->write(0, t)) {
                    showReading(o);
                }
                for (const auto& c : o.writing->ink) {
                    inkLine(o.ink, c.x0, c.y0, c.x1, c.y1, o.erasing ? 0 : o.inkColour, o.penWidth);
                }
                o.writing->ink.clear();
                if (drawing || o.writing->box(0).drawing) {
                    available = false;
                }
            }
        } else if (o.kind == Object::Kind::Sprite && available && o.takesInput() && input_.held &&
                   hit(o, input_.x, input_.y) >= 0) {
            available = false;
        }
    }
    penTaken_ = !available && !inputBlocked;
}

Thread& Scene::objectThread(Object& o, std::span<const Value> variables) {
    threads_.push_back(std::make_unique<Thread>(machine));
    Thread& t = *threads_.back();
    t.owner = o.id;
    for (size_t i = 0; i < variables.size() && i < 16; ++i) {
        t.variables[i] = variables[i];
    }
    o.thread = &t;
    return t;
}

void Scene::callOn(Object& o, const std::string& section) {
    if (o.thread && !section.empty()) {
        o.thread->replace(section, {});
    }
}

void Scene::showReading(Object& c) {
    // The writing box shows what it read with the sprite of its own name
    // (DECUMA_ADD creates it from nazo/input_abc): the sequence named by the
    // character, nothing when the box is empty (hypothesis from the ROM's
    // answer screen; the routine is not identified).
    Object* o = find(c.name);
    if (!o || !o->animation || !c.writing) {
        return;
    }
    const std::string shown = c.writing->shown(0);
    if (const auto* s = o->animation->sequence(shown)) {
        o->sequence = s;
        o->time = 0;
        o->loops = 0;
        o->finished = false;
    } else {
        o->sequence = nullptr;
    }
}

void Scene::setAllInput(bool on) {
    for (Object& o : objects_) {
        o.switches[1].on = on;
    }
}

Object* Scene::find(const std::string& name) {
    for (Object& o : objects_) {
        if (o.name == name && !o.dead && o.kind != Object::Kind::Message &&
            o.kind != Object::Kind::Canvas) {
            return &o;
        }
    }
    return nullptr;
}

Object* Scene::findById(int id) {
    for (Object& o : objects_) {
        if (o.id == id && !o.dead) {
            return &o;
        }
    }
    return nullptr;
}

Object* Scene::owner(const Thread& thread) {
    return thread.owner ? findById(thread.owner) : nullptr;
}

void Scene::setLayer(Object& o, int layer) {
    // 020b1cac / 020b0d40: out of the draw list and back in after every
    // object of the same or a higher layer.
    std::erase(order_, &o);
    o.layer = layer;
    auto at = std::find_if(order_.begin(), order_.end(),
                           [&](const Object* p) { return p->layer < layer; });
    order_.insert(at, &o);
}

std::array<int, 2> Scene::absolute(const Object& o) const {
    std::array<int, 2> p{o.position.value[0] + o.offset.value[0],
                         o.position.value[1] + o.offset.value[1]};
    if (!o.link.empty()) {
        if (const auto base = pointPosition(o.link)) {
            p[0] += (*base)[0];
            p[1] += (*base)[1];
        }
    }
    return p;
}

std::array<int, 2> Scene::pixel(const Object& o) const {
    const auto p = absolute(o);
    return {(p[0] + 0x800) >> 12, (p[1] + 0x800) >> 12};
}

std::optional<std::array<int, 2>> Scene::pointPosition(const std::string& name) const {
    // 020d3e68: world = the attached object's transform applied to the
    // point's own place (free points: the place itself).
    auto it = points.find(name);
    if (it == points.end()) {
        return std::nullopt;
    }
    const Point& p = it->second;
    if (p.attachedTo.empty()) {
        return p.local;
    }
    for (const Object& o : objects_) {
        if (!o.dead && o.name == p.attachedTo && o.kind != Object::Kind::Message &&
            o.kind != Object::Kind::Canvas) {
            const auto origin = absolute(o);
            auto q =
                turn({fx::mul(p.local[0], o.scale.value[0]), fx::mul(p.local[1], o.scale.value[1])},
                     o.rotation.value[0]);
            return std::array<int, 2>{origin[0] + q[0], origin[1] + q[1]};
        }
    }
    return p.local;
}

int Scene::totalRotation(const Object& o, int depth) const {
    int a = o.rotation.value[0];
    if (o.link.empty() || depth > 32) {
        return a;
    }
    auto it = points.find(o.link);
    if (it == points.end() || it->second.attachedTo.empty()) {
        return a;
    }
    for (const Object& c : objects_) {
        if (!c.dead && c.name == it->second.attachedTo && c.kind != Object::Kind::Message &&
            c.kind != Object::Kind::Canvas) {
            return normalise(a + totalRotation(c, depth + 1));
        }
    }
    return a;
}

namespace {
// 02064bd4: a rectangle mirrored by the object's flags (bit 0 across, bit 1
// up and down) and turned by its step (quarter turns), normalised.
std::array<int, 4> turnRect(std::array<int, 4> r, int mirror, int step) {
    const int fx_ = (mirror & 1) ? -1 : 1, fy = (mirror & 2) ? -1 : 1;
    int x0 = fx_ * r[0], y0 = fy * r[1], x1 = fx_ * r[2], y1 = fy * r[3];
    int a = x0, b = y0, c = x1, d = y1;
    switch (step) {
    case 1:
        a = -y0, b = x0, c = -y1, d = x1;
        break;
    case 2:
        a = -x0, b = -y0, c = -x1, d = -y1;
        break;
    case 3:
        a = y0, b = -x0, c = y1, d = -x1;
        break;
    default:
        break;
    }
    return {std::min(a, c), std::min(b, d), std::max(a, c), std::max(b, d)};
}

} // namespace

// 02065ec0: an object's touch box - all its touch areas together, else its
// sequence's box.
std::array<int, 4> touchBox(const Object& o) {
    if (!o.areas.empty()) {
        std::array<int, 4> r{o.areas[0].x, o.areas[0].y, o.areas[0].x + o.areas[0].w,
                             o.areas[0].y + o.areas[0].h};
        for (const auto& a : o.areas) {
            r = {std::min(r[0], a.x), std::min(r[1], a.y), std::max(r[2], a.x + a.w),
                 std::max(r[3], a.y + a.h)};
        }
        return r;
    }
    if (o.sequence) {
        const auto& b = o.sequence->box;
        const int x = b.x - o.sequence->originX, y = b.y - o.sequence->originY;
        return {x, y, x + b.width, y + b.height};
    }
    return {0, 0, 0, 0};
}

namespace {

int valueFixed(const Object& o, int i) {
    auto it = o.user.find(i);
    return it == o.user.end() ? 0 : fx::fromFloat(it->second.toFloat());
}
} // namespace

void Scene::physicsStep() {
    // 020b1ff4: walls are the touch areas with id 4 of every shown object,
    // mirrored and turned with it; falling objects move straight down (or
    // up) by their speed plus gravity until a wall stops them, then bounce
    // back by their bounce factor.
    struct Segment {
        const Object* owner;
        std::array<int, 4> r; // x0, y0, x1, y1 (fixed point, world)
    };

    struct Faller {
        Object* o;
        std::array<int, 4> box;
        size_t first = 0, count = 0; // its own walls
    };

    std::vector<Segment> walls;
    std::vector<Faller> fallers;
    for (Object& o : objects_) {
        if (o.dead || !o.switches[0].on || o.kind == Object::Kind::Message ||
            o.kind == Object::Kind::Canvas) {
            continue;
        }
        const auto at = absolute(o);
        const size_t first = walls.size();
        for (const auto& a : o.areas) {
            if (a.id != 4) {
                continue;
            }
            auto r = turnRect({a.x, a.y, a.x + a.w, a.y + a.h}, o.mirror, o.step);
            walls.push_back({&o,
                             {(r[0] << 12) + at[0], (r[1] << 12) + at[1], (r[2] << 12) + at[0],
                              (r[3] << 12) + at[1]}});
        }
        if (o.dynamic) {
            auto b = turnRect(touchBox(o), o.mirror, o.step);
            fallers.push_back({&o,
                               {(b[0] << 12) + at[0], (b[1] << 12) + at[1], (b[2] << 12) + at[0],
                                (b[3] << 12) + at[1]},
                               first,
                               walls.size() - first});
        }
    }
    // The fallers by height, lowest first (the game's exchange sort).
    for (size_t i = 1; i < fallers.size(); ++i) {
        for (size_t j = 0; j < i; ++j) {
            if (fallers[j].o->position.value[1] < fallers[i].o->position.value[1]) {
                std::swap(fallers[j], fallers[i]);
            }
        }
    }
    for (auto& f : fallers) {
        Object& o = *f.o;
        int speed = valueFixed(o, 2) + valueFixed(o, 3);
        const int bounce = valueFixed(o, 4);
        int t = fx::kOne, move = speed;
        if (speed != 0) {
            for (const auto& w : walls) {
                if (w.owner == &o || f.box[0] >= w.r[2] || f.box[2] <= w.r[0]) {
                    continue;
                }
                for (size_t k = f.first; k < f.first + f.count; ++k) {
                    const auto& own = walls[k].r;
                    if (own[0] >= w.r[2] || own[2] <= w.r[0]) {
                        continue;
                    }
                    if (speed >= 0) {
                        const int gap = w.r[1] - own[3];
                        if (gap < 0) {
                            if (own[3] < w.r[3]) { // already into it
                                move = std::min(move, gap);
                                t = 0;
                            }
                        } else {
                            const int part = fx::div(gap, speed);
                            if (part >= 0 && part < t) {
                                move = gap;
                                t = part;
                            }
                        }
                    } else {
                        const int gap = w.r[3] - own[1];
                        const int part = fx::div(gap, speed);
                        if (part >= 0 && part < t) {
                            move = gap;
                            t = part;
                        }
                    }
                }
            }
            if (t < fx::kOne) {
                if (std::abs(move) >= fx::kOne) {
                    // A landing: value 5 counts them (the first plays sound
                    // value 6, not played yet).
                    auto& landings = o.user[5];
                    landings = Value::of(landings.toInt() + 1);
                }
                speed = fx::mul(speed, -bounce);
            }
        }
        for (size_t k = f.first; k < f.first + f.count; ++k) {
            walls[k].r[1] += move;
            walls[k].r[3] += move;
        }
        f.box[1] += move;
        f.box[3] += move;
        o.user[2] = Value::ofFloat(fx::toFloat(speed));
        o.position.set({o.position.value[0], o.position.value[1] + move});
    }
}

Scene::Point& Scene::point(const std::string& name) {
    auto [it, added] = points.try_emplace(name);
    if (added) {
        it->second.serial = int(points.size());
    }
    return it->second;
}

std::string Scene::nearestPoint(std::array<int, 2> at, unsigned mask, const std::string& object,
                                bool freeOnly) {
    int best = 0x200000;
    std::string found;
    for (const auto& [name, p] : pointList()) {
        const unsigned kind = p->attachedTo.empty() ? 0 : 1;
        if (!(mask & (1u << kind)) || carries(name, object)) {
            continue;
        }
        if (freeOnly) {
            bool used = false;
            for (Object* o : drawOrder()) {
                used = used || o->link == name;
            }
            if (used) {
                continue;
            }
        }
        const auto w = *pointPosition(name);
        const int d = std::abs(w[0] - at[0]) + std::abs(w[1] - at[1]);
        if (d < best) {
            best = d;
            found = name;
        }
    }
    return found;
}

std::vector<std::pair<std::string, const Scene::Point*>> Scene::pointList() const {
    std::vector<std::pair<std::string, const Point*>> out;
    for (const auto& [name, p] : points) {
        out.emplace_back(name, &p);
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.second->serial < b.second->serial; });
    return out;
}

void Scene::linkTo(Object& o, const std::string& point) {
    // 020ad7b4: unlinking keeps the place; 020ad884: linking makes the
    // position relative to the point.
    const auto here = absolute(o);
    o.link.clear();
    o.position.set({here[0] - o.offset.value[0], here[1] - o.offset.value[1]});
    if (point.empty() || !points.contains(point)) {
        return;
    }
    const auto base = *pointPosition(point);
    o.link = point;
    o.position.set({o.position.value[0] - base[0], o.position.value[1] - base[1]});
}

bool Scene::carries(const std::string& point, const std::string& object, int depth) const {
    auto it = points.find(point);
    if (it == points.end() || it->second.attachedTo.empty() || object.empty() || depth > 32) {
        return false;
    }
    if (it->second.attachedTo == object) {
        return true;
    }
    for (const Object& o : objects_) {
        if (!o.dead && o.name == it->second.attachedTo && !o.link.empty()) {
            return carries(o.link, object, depth + 1);
        }
    }
    return false;
}

int Scene::stepFor(const Object& o, int angle) const {
    // 0204240c: the step whose sector holds the angle.
    if (o.steps <= 0) {
        return 0;
    }
    const int stepAngle = fx::fromFloat(6.2831855f / float(o.steps));
    const int half = fx::mul(stepAngle, 0x800);
    const int shifted = angle < 0 ? angle - half : angle + half;
    int q = int((float(shifted) / 4096.0f) / (float(stepAngle) / 4096.0f)) % o.steps;
    return q < 0 ? q + o.steps : q;
}

namespace {
int stepAngle(int step, int steps) {
    return normalise(fx::div(fx::mul(step << 12, kTwoPi), steps << 12));
}
} // namespace

void Scene::setStep(Object& o, int step) {
    // 020422c8.
    if (o.steps <= 0) {
        o.step = 0;
        return;
    }
    o.step = step % o.steps;
    o.rotation.set({stepAngle(o.step, o.steps)});
}

void Scene::turnToward(Object& o, int angle, bool lock) {
    // 020424d0 / 020425a8: to the nearest step, the short way round
    // (curve 3, `turnTime`).
    if (o.steps <= 0) {
        return;
    }
    o.step = stepFor(o, angle);
    const int target = stepAngle(o.step, o.steps);
    int current = normalise(o.rotation.value[0]);
    const int d = target - current;
    if (std::abs(d) > kPi) {
        current += d < 0 ? -kTwoPi : kTwoPi;
    }
    o.rotation.set({current});
    o.rotation.to(3, {target}, o.turnTime, lock);
}

void Scene::snap(Object& o, std::array<int, 2> p) {
    // 02042068 / 02042148: the nearest cell, clamped to the grid, reached
    // with curve 3 over the grid's snap time.
    const auto& g = o.grid;
    if (g.size <= 0) {
        return;
    }
    for (int k = 0; k < 2; ++k) {
        int c = ((p[size_t(k)] + 0x800) >> 12) + (g.size >> 1) - g.origin[size_t(k)];
        c = c >= 0 ? c / g.size : -((-c) / g.size);
        o.cell[size_t(k)] = std::clamp(c, 0, g.last[size_t(k)]);
    }
    o.position.to(
        3, {(o.cell[0] * g.size + g.origin[0]) << 12, (o.cell[1] * g.size + g.origin[1]) << 12},
        g.time);
}

std::vector<Object*> Scene::creationOrder() {
    std::vector<Object*> out;
    for (Object& o : objects_) {
        if (!o.dead && o.kind != Object::Kind::Message) {
            out.push_back(&o);
        }
    }
    return out;
}

std::vector<Object*> Scene::drawOrder() const {
    return order_;
}

Object* Scene::create(Object::Kind kind, const std::string& name, const std::string& res,
                      bool needsResource) {
    // 020b64b8: an existing name makes the creation fail; so does a missing
    // resource - except for the touchable objects of 020b8568, which are
    // made without one when it is missing (a place's invisible hotspots,
    // resource "", touched through their touch areas).
    if (find(name)) {
        return nullptr;
    }
    const Resource* r = resource(res);
    if (!r && needsResource) {
        return nullptr;
    }
    Object o;
    o.kind = kind;
    o.name = name;
    o.id = nextId_++;
    o.screen = r ? r->screen : Screen::Lower;
    o.picture = r ? r->picture : nullptr;
    o.animation = r ? r->animation : nullptr;
    o.alpha.set({fx::kOne});
    o.scale.set({fx::kOne, fx::kOne});
    o.level = level_;
    objects_.push_back(std::move(o));
    Object& added = objects_.back();
    setLayer(added, defaultLayer);
    return &added;
}

void Scene::remove(const std::string& name) {
    if (Object* o = find(name)) {
        erase(o);
    }
}

Object* Scene::message(const std::string& name) {
    for (Object& o : objects_) {
        if (o.name == name && !o.dead && o.kind == Object::Kind::Message) {
            return &o;
        }
    }
    return nullptr;
}

Object* Scene::createMessage(const std::string& name, Screen screen, int capacity, int font) {
    if (message(name)) {
        return nullptr;
    }
    Object o;
    o.kind = Object::Kind::Message;
    o.name = name;
    o.id = nextId_++;
    o.screen = screen;
    o.capacity = capacity;
    o.alpha.set({fx::kOne});
    // The game's two message fonts (020c7534: index 1 the second one).
    o.level = level_;
    o.text.style.font = &assets.font(font == 1 ? "font_en2" : "font_en1");
    o.text.style.defaultColour = 1; // 02069c54 (1, 9)
    o.text.hooks.typing = [this](int id) {
        if (host.sound) {
            host.sound->playEffect(id);
        }
    };
    objects_.push_back(std::move(o));
    Object& added = objects_.back();
    setLayer(added, defaultLayer);
    return &added;
}

Object* Scene::canvas(const std::string& name) {
    for (Object& o : objects_) {
        if (o.name == name && !o.dead && o.kind == Object::Kind::Canvas) {
            return &o;
        }
    }
    return nullptr;
}

Object* Scene::createCanvas(const std::string& name, bool circling) {
    if (canvas(name)) {
        return nullptr;
    }
    Object o;
    o.kind = Object::Kind::Canvas;
    o.name = name;
    o.id = nextId_++;
    o.level = level_;
    o.alpha.set({fx::kOne});
    o.ink = {256, 192, std::vector<std::uint16_t>(256 * 192, 0)};
    o.circling = circling;
    objects_.push_back(std::move(o));
    Object& added = objects_.back();
    setLayer(added, defaultLayer);
    return &added;
}

namespace {
constexpr std::size_t kInkDelay = 3;
// ov6 0x020f65c8: the flag a circling canvas sets while its circle does not
// count (read from the game's code).
constexpr std::uint32_t kOverlay6 = 0x020a7980, kNoVerdictFlag = 0x020f65c8;
} // namespace

void Scene::wipeCircle(Object& c) {
    // 020cb7f8: ink and box gone, both sprites hidden, no verdict.
    std::fill(c.ink.pixels.begin(), c.ink.pixels.end(), 0);
    c.inked = {0, 0, 0, 0};
    for (int k : {1, 2}) {
        if (Object* o = find(c.canvasValues[size_t(k)].toText())) {
            o->switches[0].on = false;
        }
    }
    machine.state.setFlag(Value::ofText(assets.codeText(6, kOverlay6, kNoVerdictFlag)), true);
}

void Scene::circleInput(Object& c, bool& available) {
    const Input& in = input_;
    const Rect& r = c.area;
    if (!c.inking) {
        // 0206c6c8 + 020cb5a8: a press inside the area starts a new circle.
        if (!available || !c.takesInput() || !in.pressed) {
            return;
        }
        const unsigned dx = unsigned(in.x - r.x), dy = unsigned(in.y - r.y);
        if (dx >= unsigned(r.width) || dy >= unsigned(r.height)) {
            return;
        }
        std::fill(c.ink.pixels.begin(), c.ink.pixels.end(), 0); // flag 0x10: 0206c83c
        c.inked = {0, 0, 0, 0};
        c.inking = true;
        c.pendingInk.clear();
        c.pen = {int(dx), int(dy)};
        c.canvasValues[4] = Value::ofText("");
        c.canvasValues[3] = Value::of(0);
        for (int k : {1, 2}) {
            if (Object* o = find(c.canvasValues[size_t(k)].toText())) {
                o->switches[0].on = false;
            }
        }
        available = false;
        return;
    }
    available = false;
    // 0206c774 / 0206caec: the pen draws from its last place; the box grows
    // by each segment's extent, clamped to the area.
    // Like the writing boxes' ink, a segment is drawn three frames late and
    // whatever is still waiting when the pen lifts is never drawn - nor
    // counted in the box (ROM captures of puzzle 2).
    const std::array<int, 2> now = in.held ? std::array<int, 2>{in.x - r.x, in.y - r.y} : c.pen;
    c.pendingInk.push_back({c.pen[0], c.pen[1], now[0], now[1]});
    c.pen = now;
    if (!in.held) {
        c.pendingInk.clear();
    }
    if (c.pendingInk.size() > kInkDelay) {
        const auto d = c.pendingInk.front();
        c.pendingInk.erase(c.pendingInk.begin());
        drawCircleSegment(c, d);
    }
    if (in.held) {
        return;
    }
    circleEnded(c);
}

void Scene::drawCircleSegment(Object& c, const std::array<int, 4>& d) {
    const Rect& r = c.area;
    inkLine(c.ink, d[0] + r.x, d[1] + r.y, d[2] + r.x, d[3] + r.y, c.inkColour, c.penWidth);
    std::array<int, 4> seg{std::min(d[0], d[2]) + r.x, std::min(d[1], d[3]) + r.y,
                           std::max(d[0], d[2]) + 1 + r.x, std::max(d[1], d[3]) + 1 + r.y};
    auto clampTo = [&](std::array<int, 4> b) {
        return std::array<int, 4>{
            std::clamp(b[0], r.x, r.x + r.width), std::clamp(b[1], r.y, r.y + r.height),
            std::clamp(b[2], r.x, r.x + r.width), std::clamp(b[3], r.y, r.y + r.height)};
    };
    const bool empty = !(c.inked[0] < c.inked[2] && c.inked[1] < c.inked[3]);
    c.inked = clampTo(
        empty ? seg
              : std::array<int, 4>{std::min(seg[0], c.inked[0]), std::min(seg[1], c.inked[1]),
                                   std::max(seg[2], c.inked[2]), std::max(seg[3], c.inked[3])});
}

void Scene::circleEnded(Object& c) {
    // 020cb650: the circle ends. The points inside the box (020d3d30,
    // first in creation order) decide.
    c.inking = false;
    for (int k : {1, 2}) {
        if (Object* o = find(c.canvasValues[size_t(k)].toText())) {
            o->switches[0].on = false;
        }
    }
    int count = 0;
    std::string first;
    for (const auto& [name, p] : pointList()) {
        const auto w = *pointPosition(name);
        const int px = w[0] >> 12, py = w[1] >> 12;
        if (px >= c.inked[0] && py >= c.inked[1] && px < c.inked[2] && py < c.inked[3]) {
            if (first.empty()) {
                first = name;
            }
            ++count;
        }
    }
    c.canvasValues[3] = Value::of(count);
    const Value flag = Value::ofText(assets.codeText(6, kOverlay6, kNoVerdictFlag));
    if (!first.empty() && count <= c.canvasValues[0].toInt()) {
        c.canvasValues[4] = Value::ofText(first);
        if (Object* marker = find(c.canvasValues[2].toText())) {
            marker->switches[0].on = true;
            marker->position.set(points.at(first).local);
        }
        machine.state.setFlag(flag, false);
        return;
    }
    if (Object* error = find(c.canvasValues[1].toText())) {
        error->switches[0].on = true;
    }
    machine.state.setFlag(flag, true);
}

void Scene::setWritingMode(Object& c, int mode) {
    // ov6 020eac4c: the recogniser's character classes by mode (0: digits
    // and symbols 0x2010, 1: 4, 2: 8, 3: 0xc, otherwise none), secondary
    // mask 1, slant 0x400, no writing guides; dcm/code maps the
    // recogniser's characters to the game's (020ea6c8).
    if (!recognizer_) {
        recognizer_ =
            std::make_unique<handwriting::Recognizer>(readFile(assets.root / "dcm/scrdb_eu.ldcm"));
        const auto codes = assets.script("dcm/code.lbin");
        for (const auto& k : codes.commands) {
            if (k.opcode == 901 && k.operands.size() >= 2) {
                characterCodes_.try_emplace(std::uint16_t(k.operands[1].value),
                                            std::uint8_t(k.operands[0].value));
            }
        }
    }
    handwriting::Settings r;
    r.categories = mode == 0 ? 0x2010 : mode == 1 ? 4 : mode == 2 ? 8 : mode == 3 ? 0xc : 0;
    r.categories2 = 1;
    r.slant = 0x400;
    r.baseline = 0;
    r.helpline = 0;
    WrittenAnswer::Setup setup;
    setup.rules = WrittenAnswer::Rules::Adventure3;
    setup.characterCodes = characterCodes_;
    WrittenAnswer::BoxSetup box;
    box.area = c.area;
    box.recognition = r;
    box.reading = WrittenAnswer::Reading::Character;
    setup.boxes.push_back(box);
    c.writingMode = mode;
    c.writing = std::make_shared<WrittenAnswer>(setup, *recognizer_);
}

namespace {
// The memo pad's script and its sections (ov12 0x0211fe30; ov6 0x020f65e8,
// 0x020f65f4, 0x020f6600), read from the game's code.
constexpr std::uint32_t kOverlay12 = 0x0211f6e0, kMemoScript = 0x0211fe30;
constexpr std::array<std::uint32_t, 3> kMemoSections{0x020f65e8, 0x020f65f4, 0x020f6600};
} // namespace

void Scene::startMemo(Object& pad, std::span<const Value> arguments) {
    // 020cc264: the script loaded, the arguments set as the thread's
    // variables (02051970) and MEMO_BEGIN started; a new start replaces a
    // run still going.
    const std::string script = assets.codeText(12, kOverlay12, kMemoScript);
    if (!loadedScripts_.contains(script)) {
        load(script);
        loadedScripts_.insert(script);
    }
    if (pad.thread) {
        pad.thread->stop();
        pad.thread->owner = -1;
        pad.thread = nullptr;
    }
    Thread& t = objectThread(pad, arguments);
    pad.memoStage = 1;
    t.call(assets.codeText(6, kOverlay6, kMemoSections[0]), {});
}

void Scene::memoStep(Object& pad) {
    // 020cc464: after the thread's step, the next section once the last
    // ended; after MEMO_END the pad lets its thread and script go.
    if (pad.memoStage == 0 || (pad.thread && pad.thread->running())) {
        return;
    }
    if (pad.thread && pad.memoStage < 3) {
        pad.thread->call(assets.codeText(6, kOverlay6, kMemoSections[size_t(pad.memoStage)]), {});
        ++pad.memoStage;
        return;
    }
    if (pad.thread) {
        pad.thread->owner = -1;
        pad.thread = nullptr;
    }
    pad.memoStage = 0;
    pad.pendingInk.clear();
}

void Scene::memoInput(Object& c, bool& available) {
    // The paint layer's pen (0206c9d8) while MEMO_MAIN runs: a stroke
    // starts on a press inside the area (when nothing in front took the
    // pen) and keeps the pen until it is lifted. Like the other canvases
    // the ink trails the pen by three frames and what is still waiting at
    // the release is dropped (the shared paint rule; not measured on the
    // memo itself).
    const SceneInput& in = input_;
    const Rect& r = c.area;
    if (!c.inking) {
        if (c.memoStage != 2 || !available || !c.takesInput() || !in.pressed || in.x < r.x ||
            in.y < r.y || in.x >= r.x + r.width || in.y >= r.y + r.height) {
            return;
        }
        c.inking = true;
        c.pen = {in.x, in.y};
        c.pendingInk.clear();
    }
    available = false;
    const std::array<int, 2> now = in.held ? std::array<int, 2>{in.x, in.y} : c.pen;
    c.pendingInk.push_back({c.pen[0], c.pen[1], now[0], now[1]});
    c.pen = now;
    if (!in.held) {
        c.pendingInk.clear();
        c.inking = false;
        return;
    }
    if (c.pendingInk.size() > kInkDelay) {
        const auto d = c.pendingInk.front();
        c.pendingInk.erase(c.pendingInk.begin());
        inkLine(c.ink, d[0], d[1], d[2], d[3], c.erasing ? 0 : c.inkColour, c.penWidth, &r);
    }
}

void Scene::removeMessage(const std::string& name) {
    if (Object* o = message(name)) {
        erase(o);
    }
}

namespace {
// Resource banks: 0 and 1 the lower screen, 2 and 3 the upper one
// (hypothesis from the puzzle scenes: "anm_b_*" and "txt_b" are bank 2 and
// shown above).
Screen bankScreen(int bank) {
    return bank >= 2 ? Screen::Upper : Screen::Lower;
}
} // namespace

void Scene::loadPicture(const std::string& name, int bank, const std::string& path) {
    // Pictures (3510) and animations (3610) are separate lists in the game:
    // one name can be both (the chapter card's "obj_chapt").
    if (!assets.exists(path)) {
        return;
    }
    if (auto it = resources_.find(name); it != resources_.end()) {
        if (!it->second.picture) {
            it->second.picture = &assets.picture(path);
        }
        return;
    }
    resources_[name] = {bankScreen(bank), &assets.picture(path), nullptr};
    if (!scopes_.empty()) {
        scopes_.back().push_back(name);
    }
}

void Scene::loadAnimation(const std::string& name, int bank, const std::string& path) {
    if (!assets.exists(path)) {
        return;
    }
    if (auto it = resources_.find(name); it != resources_.end()) {
        if (!it->second.animation) {
            it->second.animation = &assets.animation(path);
        }
        return;
    }
    resources_[name] = {bankScreen(bank), nullptr, &assets.animation(path)};
    if (!scopes_.empty()) {
        scopes_.back().push_back(name);
    }
}

const Scene::Resource* Scene::resource(const std::string& name) const {
    auto it = resources_.find(name);
    return it == resources_.end() ? nullptr : &it->second;
}

void Scene::freeResource(const std::string& name) {
    resources_.erase(name);
}

void Scene::pushScope() {
    // 3010 (020d9134): one level deeper; every object keeps its switches
    // for the end of the scope (02051d80).
    ++level_;
    scopes_.emplace_back();
    for (Object& o : objects_) {
        for (auto& sw : o.switches) {
            sw.saved = std::uint16_t(sw.saved << 1 | (sw.on ? 0 : 1));
        }
    }
}

void Scene::clearLevel() {
    if (!scopes_.empty()) {
        for (const auto& name : scopes_.back()) {
            resources_.erase(name);
        }
        scopes_.back().clear();
    }
    std::vector<Object*> doomed;
    for (Object& o : objects_) {
        if (!o.dead && o.level >= level_) {
            doomed.push_back(&o);
        }
    }
    for (Object* o : doomed) {
        erase(o);
    }
}

void Scene::saveSwitches() {
    for (Object& o : objects_) {
        for (auto& sw : o.switches) {
            sw.saved = std::uint16_t(sw.saved << 1 | (sw.on ? 0 : 1));
        }
    }
}

void Scene::restoreSwitches() {
    for (Object& o : objects_) {
        for (auto& sw : o.switches) {
            sw.on = !(sw.saved & 1);
            sw.saved = std::uint16_t(sw.saved >> 1);
        }
    }
}

void Scene::freeAll(bool pictures) {
    std::erase_if(resources_, [pictures](const auto& r) {
        return pictures ? r.second.picture != nullptr : r.second.animation != nullptr;
    });
}

void Scene::popScope() {
    // 3011 (020d914c): what the level created goes (020d947c), then the
    // remaining objects get their switches back (02051e68).
    if (scopes_.empty()) {
        return;
    }
    for (const auto& name : scopes_.back()) {
        resources_.erase(name);
    }
    scopes_.pop_back();
    std::vector<Object*> doomed;
    for (Object& o : objects_) {
        if (!o.dead && o.level >= level_) {
            doomed.push_back(&o);
        }
    }
    for (Object* o : doomed) {
        erase(o);
    }
    --level_;
    for (Object& o : objects_) {
        for (auto& sw : o.switches) {
            sw.on = !(sw.saved & 1);
            sw.saved = std::uint16_t(sw.saved >> 1);
        }
    }
}

void Scene::resetBackground(int screen) {
    if (screen == 0) {
        stage = {};
    } else {
        guide.id = -1;
        guide.picture = nullptr;
        guide.shown = false;
    }
    backgroundFilter[size_t(screen)] = {};
}

void Scene::setScreenPicture(int screen, const adventure3::Picture* picture) {
    resetBackground(screen);
    screenPicture[size_t(screen)] = picture;
}

void Scene::setStage(int map) {
    // 020c2398: rc/lt3_map/m%04d.lbin - 5220 the picture (map/<name>.cimg),
    // 5230 the animation of its parts (map/<name>.cani), 5232 a part {x, y,
    // sequence}; unchanged when it is already the stage, else the screen
    // starts afresh (020c2130).
    if (map == stage.id) {
        stage.shown = true;
        return;
    }
    resetBackground(0);
    stage.id = map;
    stage.shown = true;
    char path[48];
    std::snprintf(path, sizeof path, "rc/lt3_map/m%04d.lbin", map);
    if (!assets.exists(path)) {
        return;
    }
    const auto s = assets.script(path);
    const adventure3::Animation* animation = nullptr;
    for (const auto& c : s.commands) {
        if (c.opcode == 5220 && !c.operands.empty()) {
            const std::string p = "map/" + s.text(c.operands[0]) + ".cimg";
            stage.picture = assets.exists(p) ? &assets.picture(p) : nullptr;
        } else if (c.opcode == 5230 && !c.operands.empty()) {
            const std::string p = "map/" + s.text(c.operands[0]) + ".cani";
            animation = assets.exists(p) ? &assets.animation(p) : nullptr;
        } else if (c.opcode == 5232 && c.operands.size() >= 3 && animation) {
            Object part;
            part.screen = Screen::Lower;
            part.animation = animation;
            part.position.set({int(std::int32_t(c.operands[0].value)) << 12,
                               int(std::int32_t(c.operands[1].value)) << 12});
            play(part, animation->sequence(s.text(c.operands[2])), 0);
            stage.parts.push_back(std::move(part));
        }
    }
}

void Scene::showStagePart(const std::string& sequence, bool shown) {
    for (Object& part : stage.parts) {
        if (part.sequence && part.sequence->name == sequence) {
            part.switches[0].on = shown;
        }
    }
}

std::uint16_t Scene::BackgroundFilter::apply(std::uint16_t colour) const {
    // 02048ea4 per colour: the channels as 0..1 (/31), times the matrix
    // (020496b0: out[i] = sum over j of in[j] * m[j][i]), then moved by the
    // level towards white (level > 0) or black (< 0) - out + (end - out) *
    // |level| - and back to five bits (rounding: hypothesis).
    const auto& m = record.value;
    const std::array<int, 3> in{(colour & 0x1f) * fx::kOne / 31,
                                (colour >> 5 & 0x1f) * fx::kOne / 31,
                                (colour >> 10 & 0x1f) * fx::kOne / 31};
    const int level = m[kLevel];
    const int end = level > 0 ? fx::kOne : 0;
    const int weight = std::abs(level);
    std::uint16_t out = 0;
    for (int i = 0; i < 3; ++i) {
        int v = (in[0] * m[size_t(i)] + in[1] * m[size_t(3 + i)] + in[2] * m[size_t(6 + i)]) >> 12;
        v += fx::mul(end - v, weight);
        const int c = std::clamp((v * 31 + 0x800) >> 12, 0, 31);
        out = std::uint16_t(out | c << (5 * i));
    }
    return out;
}

void Scene::loadTextTable(const std::string& name, int file, int part) {
    // 020f1ed4: "txt/?/%02d.cpck", one part "%02d_%06d.lbin" or all of them.
    char folder[32];
    std::snprintf(folder, sizeof folder, "txt/?/%02d", file);
    auto& table = textTables[name];
    auto read = [&](const std::string& path) {
        const auto s = assets.script(path);
        for (const auto& c : s.commands) {
            if (c.opcode == 6001 && c.operands.size() >= 3) {
                table[int(std::int32_t(c.operands[0].value))].push_back(
                    {s.text(c.operands[1]), s.text(c.operands[2])});
            }
        }
    };
    if (part >= 0) {
        char file_[48];
        std::snprintf(file_, sizeof file_, "%s/%02d_%06d.lbin", folder, file, part);
        read(file_);
        return;
    }
    const auto dir = assets.file(folder);
    std::vector<std::string> parts;
    for (const auto& e : std::filesystem::directory_iterator(resolvePath(dir))) {
        parts.push_back(std::string(folder) + "/" + e.path().filename().string());
    }
    std::sort(parts.begin(), parts.end());
    for (const auto& p : parts) {
        read(p);
    }
}

// --- Rendering ------------------------------------------------------------------

Surface Scene::render() const {
    Surface out{256, 384, std::vector<std::uint16_t>(256 * 384, 0x8000)};
    for (int s = 0; s < 2; ++s) {
        Surface screen{256, 192,
                       std::vector<std::uint16_t>(256 * 192, std::uint16_t(backdrop[s] | 0x8000))};
        // The background: the stage (the background manager's picture and
        // its animated parts), the guide or the screen picture - all through
        // the screen's filter (ROM: an event's dimmed stage dims its parts
        // too).
        const bool staged = s == int(Screen::Lower) && stage.shown && stage.picture;
        if (staged) {
            const int scrollX = stage.scroll.value[0] >> 12, scrollY = stage.scroll.value[1] >> 12;
            blit(screen, stage.picture->image, scrollX, scrollY);
            for (const Object& part : stage.parts) {
                if (!part.shown()) {
                    continue;
                }
                if (const auto* st = currentStep(part)) {
                    part.animation->drawFrame(
                        screen, st->frame,
                        scrollX + (part.position.value[0] >> 12) + st->x - part.sequence->originX,
                        scrollY + (part.position.value[1] >> 12) + st->y - part.sequence->originY);
                }
            }
        }
        const bool guided = s == int(Screen::Upper) && guide.shown;
        if (guided && guide.picture) {
            blit(screen, guide.picture->image, 0, 0);
        }
        if (screenPicture[s] && !(s == int(Screen::Lower) && stage.shown) && !guided) {
            blit(screen, screenPicture[s]->image, 0, 0);
        }
        if (!backgroundFilter[s].plain()) {
            for (auto& px : screen.pixels) {
                px = std::uint16_t(backgroundFilter[s].apply(px) | 0x8000);
            }
        }
        // The puzzle engine's own drawing (vtable 0xf8, after 020f3a78) is
        // 3D: above the pictures, beneath the sprites (puzzle 18's "no more
        // lines" sign covers the lines).
        bool engineDrawn = !engine;
        // Messages are the message manager's (020c7534), not the draw
        // list's: they show above the screen's sprites (ROM: the talk window
        // covers characters standing at layer 1) - in their own order.
        std::vector<const Object*> drawn;
        drawn.reserve(order_.size());
        for (const Object* o : order_) {
            if (o->kind != Object::Kind::Message) {
                drawn.push_back(o);
            }
        }
        for (const Object* o : order_) {
            if (o->kind == Object::Kind::Message) {
                drawn.push_back(o);
            }
        }
        // Memo pads last, over the objects (see dispatchInput).
        std::stable_partition(drawn.begin(), drawn.end(), [](const Object* o) {
            return o->kind != Object::Kind::Canvas || !o->memo;
        });
        for (const Object* o : drawn) {
            if (int(o->screen) != s || !o->shown()) {
                continue;
            }
            if (!engineDrawn && o->kind != Object::Kind::Picture) {
                engine->draw(*this, screen, Screen(s));
                engineDrawn = true;
            }
            const int a = alpha5(fx::mul(o->alpha.value[0], layerAlpha[size_t(s)][4].value[0]));
            if (a <= 0) {
                continue;
            }
            auto [x, y] = pixel(*o);
            x += o->jitter[0] >> 12;
            y += o->jitter[1] >> 12;
            // Blend weights out of 16. Objects on the upper screen show at
            // 15/16 of their colour even at full alpha (blend weights 15 and
            // 0; measured on puzzle 6's title card and puzzle screen - the
            // screen picture of 4501 is not affected; the register setting
            // behind it is not identified, hypothesis).
            const bool upper = s == int(Screen::Upper);
            const int fore = a >= 31 ? (upper ? 15 : 16) : a / 2;
            const int back = a >= 31 ? 0 : 16 - fore;
            const BackgroundFilter* tint = nullptr;
            if (o->animation) {
                if (auto t = tints.find(o->animation); t != tints.end() && !t->second.plain()) {
                    tint = &t->second;
                }
            }
            const bool blended = fore != 16 || back != 0 || tint;
            Surface layer;
            if (blended) {
                layer = {256, 192, std::vector<std::uint16_t>(256 * 192, 0)};
            }
            Surface& target = blended ? layer : screen;
            switch (o->kind) {
            case Object::Kind::Picture:
                if (o->picture) {
                    blit(target, o->picture->image, x, y);
                }
                break;
            case Object::Kind::Sprite:
            case Object::Kind::Button:
            case Object::Kind::Touch:
                if (const auto* st = currentStep(*o)) {
                    const int turnBy = totalRotation(*o);
                    const bool transformed = turnBy != 0 || o->scale.value[0] != fx::kOne ||
                                             o->scale.value[1] != fx::kOne || o->mirror != 0;
                    if (!transformed) {
                        o->animation->drawFrame(target, st->frame, x + st->x - o->sequence->originX,
                                                y + st->y - o->sequence->originY);
                        break;
                    }
                    // Turned or scaled about the object's place (the
                    // hardware's affine sprites; the sense of the turn is a
                    // hypothesis: positive turns clockwise on screen).
                    Surface frame{256, 256, std::vector<std::uint16_t>(256 * 256, 0)};
                    o->animation->drawFrame(frame, st->frame, 128 + st->x - o->sequence->originX,
                                            128 + st->y - o->sequence->originY);
                    // Mirroring is a negative scale (before the turn).
                    const std::array<int, 2> scale{
                        o->mirror & 1 ? -o->scale.value[0] : o->scale.value[0],
                        o->mirror & 2 ? -o->scale.value[1] : o->scale.value[1]};
                    drawTransformed(target, frame, x, y, turnBy, scale);
                }
                break;
            case Object::Kind::Number: {
                if (!o->animation) {
                    break;
                }
                char digits[16];
                std::snprintf(digits, sizeof digits, o->format.c_str(),
                              o->user.contains(0) ? o->user.at(0).toInt() : 0);
                const int n = int(std::strlen(digits));
                for (int i = 0; i < n; ++i) {
                    const auto* seq = o->animation->sequence(o->prefix + std::string(1, digits[i]));
                    if (!seq || seq->steps.empty()) {
                        continue;
                    }
                    const auto& st = seq->steps[0];
                    o->animation->drawFrame(target, st.frame,
                                            x + st.x - seq->originX - (n - i) * o->spacing,
                                            y + st.y - seq->originY);
                }
                break;
            }
            case Object::Kind::Message:
                if (!o->switches[3].on) {
                    break; // a message's fourth switch hides it (0206bc88)
                }
                if (o->behaviour) {
                    o->behaviour->draw(*this, target, Screen(s), *o);
                } else if (o->clip[2] > 0) {
                    // 4640: only inside its rectangle.
                    Surface text{256, 192, std::vector<std::uint16_t>(256 * 192, 0)};
                    o->text.draw(text, x, y, messagePalette);
                    for (int py = std::max(0, o->clip[1]);
                         py < std::min(192, o->clip[1] + o->clip[3]); ++py) {
                        for (int px = std::max(0, o->clip[0]);
                             px < std::min(256, o->clip[0] + o->clip[2]); ++px) {
                            const auto v = text.pixels[size_t(py * 256 + px)];
                            if (v & 0x8000) {
                                target.pixels[size_t(py * 256 + px)] = v;
                            }
                        }
                    }
                } else {
                    o->text.draw(target, x, y, messagePalette);
                }
                break;
            case Object::Kind::Canvas:
                blit(target, o->ink, 0, 0);
                break;
            }
            if (blended) {
                if (tint) {
                    for (auto& px : layer.pixels) {
                        if (px & 0x8000) {
                            px = std::uint16_t(tint->apply(px) | 0x8000);
                        }
                    }
                }
                blit(screen, layer, 0, 0, fore, back);
            }
        }
        if (!engineDrawn) {
            engine->draw(*this, screen, Screen(s));
        }
        // The master brightness: -16 (black) .. 16 (white), from -1 .. 1,
        // rounded down (ROM captures of puzzle 6's title card: one step into
        // a 15-frame fade from black shows level -15).
        const int level = (brightness[s].value[0] * 16) >> 12;
        applyBrightness(screen, level, level);
        const int top = (s == int(Screen::Upper)) != swapped ? 0 : 192;
        std::copy(screen.pixels.begin(), screen.pixels.end(), out.pixels.begin() + top * 256);
    }
    return out;
}

} // namespace picarat::lscr
