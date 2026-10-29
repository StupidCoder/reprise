// The fourth adventure's scene: resources, objects, animation players and
// screens. See a4_scene.hpp.
#include "system/a4_scene.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace picarat::adventure4 {
namespace {

// ITCM 01ff86a0: CRC-32 (reflected 0xedb88320, initial and final XOR all
// ones). An empty name hashes to 0.
std::uint32_t crc32(std::string_view s) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            t[i] = c;
        }
        return t;
    }();
    if (s.empty()) {
        return 0;
    }
    std::uint32_t crc = 0xffffffffu;
    for (unsigned char ch : s) {
        crc = table[(crc ^ ch) & 0xff] ^ (crc >> 8);
    }
    return ~crc;
}

// Round a 20.12 coordinate to a pixel (ov6 020abac4 adds 0x800).
int pixel(std::int32_t v) {
    return (v + 0x800) >> 12;
}

// Blends `c` over `d` with alpha 0..31 (the hardware's translucency).
std::uint16_t blend(std::uint16_t c, std::uint16_t d, unsigned a) {
    if (a >= 31 || !(d & 0x8000)) {
        return std::uint16_t(c | 0x8000);
    }
    auto channel = [&](int shift) {
        const unsigned s = (c >> shift) & 31, t = (d >> shift) & 31;
        return ((s * (a + 1) + t * (31 - a)) / 32) << shift;
    };
    return std::uint16_t(0x8000 | channel(0) | channel(5) | channel(10));
}

// The 3D engine's texture modulation by a polygon colour (white keeps the
// texel).
std::uint16_t modulate(std::uint16_t c, std::uint16_t colour) {
    if ((colour & 0x7fff) == 0x7fff) {
        return c;
    }
    auto channel = [&](int shift) {
        const unsigned t = (c >> shift) & 31, v = (colour >> shift) & 31;
        return (((t + 1) * (v + 1) - 1) / 32) << shift;
    };
    return std::uint16_t((c & 0x8000) | channel(0) | channel(5) | channel(10));
}

// The colour treatment of what is being drawn (an object's pack), or null.
const ColourMatrix* partColour = nullptr;

std::uint16_t treat(std::uint16_t p, const ColourMatrix& cm) {
    const std::int32_t c[3] = {p & 31, (p >> 5) & 31, (p >> 10) & 31};
    std::uint16_t q = std::uint16_t(p & 0x8000);
    for (int r = 0; r < 3; ++r) {
        const std::int64_t v = std::int64_t(cm.m[std::size_t(r)]) * c[0] +
                               std::int64_t(cm.m[std::size_t(3 + r)]) * c[1] +
                               std::int64_t(cm.m[std::size_t(6 + r)]) * c[2] +
                               std::int64_t(cm.k[std::size_t(r)]) * 31;
        q = std::uint16_t(q | (std::clamp<std::int64_t>(v >> 12, 0, 31) << (5 * r)));
    }
    return q;
}

void drawPart(Surface& out, int top, const Picture& p, const Rect& part, int x, int y,
              unsigned alpha, std::uint16_t colour = 0x7fff) {
    for (int sy = 0; sy < part.height; ++sy) {
        const int py = part.y + sy, dy = y + sy;
        if (py >= int(p.height) || dy < 0 || dy >= 192) {
            continue;
        }
        for (int sx = 0; sx < part.width; ++sx) {
            const int px = part.x + sx, dx = x + sx;
            if (px >= int(p.width) || dx < 0 || dx >= 256) {
                continue;
            }
            const std::size_t at = std::size_t(py) * p.width + std::size_t(px);
            std::uint16_t c = modulate(p.pixels[at], colour);
            if (!(c & 0x8000)) {
                continue;
            }
            if (partColour) {
                c = treat(c, *partColour);
            }
            unsigned a = p.alpha.empty() ? 31 : p.alpha[at];
            a = a * (alpha + 1) / 32;
            std::uint16_t& d = out.pixels[std::size_t(top + dy) * out.width + std::size_t(dx)];
            d = blend(c, d, a);
        }
    }
}

} // namespace

std::uint32_t nameHash(std::string_view name) {
    return crc32(name);
}

// ------------------------------------------------------ animation player

void AnimationPlayer::attach(const AnimationResource* resource) {
    resource_ = resource;
    sequence_ = next_ = nullptr;
    glyphs_.clear();
    textShown_ = false;
    time_ = 0;
    loops_ = 0;
    flags_ = 0;
}

bool AnimationPlayer::play(const Animation::Sequence* sequence, unsigned flags) {
    if (!resource_ || !sequence) {
        return false;
    }
    glyphs_.clear();
    textShown_ = false;
    if (sequence_ != sequence || (flags & Restart)) {
        sequence_ = sequence;
        time_ = 0;
        loops_ = 0;
    }
    flags_ = flags & ~unsigned(Restart);
    starred_ = sequence->name.find('*') != std::string::npos;
    next_ = nullptr;
    nextFlags_ = 0;
    if (!(flags & NoChain) && sequence->next >= 0 &&
        std::size_t(sequence->next) < resource_->animation->sequences.size()) {
        next_ = &resource_->animation->sequences[std::size_t(sequence->next)];
        nextFlags_ = 1;
    }
    return true;
}

bool AnimationPlayer::showText(const std::string& text, int step, int line, unsigned flags) {
    if (!resource_ || text.empty()) {
        return false;
    }
    glyphs_.clear();
    int x = 0, y = 0;
    for (std::size_t i = 0; i < text.size() && glyphs_.size() < 20;) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\n') {
            ++i;
            ++x; // the game moves one pixel on at a new line (02095088)
            y += line;
            continue;
        }
        const bool wide = (c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc);
        const std::string name = text.substr(i, wide ? 2 : 1);
        i += wide ? 2 : 1;
        if (const auto* image = resource_->animation->image(name)) {
            glyphs_.push_back({image, x, y});
        }
        x += step;
    }
    sequence_ = nullptr;
    flags_ = flags;
    time_ = 0;
    loops_ = 1;
    next_ = nullptr;
    textShown_ = true;
    return true;
}

const Animation::Step* AnimationPlayer::step() const {
    if (!sequence_ || sequence_->steps.empty()) {
        return nullptr;
    }
    // 02091e44: the last step starting at or before the time.
    const unsigned t = unsigned(time_ >> 12);
    const Animation::Step* found = &sequence_->steps.front();
    for (const auto& s : sequence_->steps) {
        if (s.start <= t) {
            found = &s;
        }
    }
    return found;
}

bool AnimationPlayer::finished() const {
    return (flags_ & Hold) && loops_ > 0;
}

void AnimationPlayer::update(std::int32_t elapsed) {
    if (!sequence_) {
        return;
    }
    // 02095340
    if (!(flags_ & Paused)) {
        time_ += std::int32_t((std::int64_t(elapsed) * speed) / kOne);
    }
    const std::int32_t length = std::int32_t(sequence_->length) << 12;
    if (time_ < length) {
        return;
    }
    if (!(flags_ & Hold)) {
        if (length > 0) {
            loops_ = (loops_ + unsigned(time_ / length)) & 0xffffff;
            time_ %= length;
        } else {
            time_ = 0;
            loops_ = (loops_ + 1) & 0xffffff;
        }
    } else {
        time_ = length;
        loops_ = 1;
    }
    if (next_) {
        const std::int32_t keep = time_;
        if (play(next_, nextFlags_)) {
            time_ = keep;
        }
    }
}

// ------------------------------------------------------------- objects

Chara::Chara(Scene& owner, std::string objectName, int objectKind, const gss::Program* script,
             std::span<const gss::Value> values)
    : name(std::move(objectName)), hash(nameHash(name)), kind(objectKind),
      thread(owner.machine, std::max<std::size_t>(16, values.size())), program(script),
      scene(owner) {
    std::copy(values.begin(), values.end(), thread.values.begin());
    scale.set({kOne, kOne});
    alpha.set(kOne);
    rotation.angle = true;
}

std::array<int, 2> Chara::pixelPosition() const {
    return {pixel(world[0]), pixel(world[1])};
}

void Chara::updateWorld() {
    // 020acfa4: position + offset through the place's matrix (products
    // rounded toward zero, 020ad1b4), then its translation.
    const auto p = position.value(), o = offset.value();
    const std::int64_t x = p[0] + o[0] + shaking[0], y = p[1] + o[1] + shaking[1];
    const auto& m = parentMatrix;
    world = {std::int32_t((m[0] * x + m[2] * y) / kOne) + parentTranslation[0],
             std::int32_t((m[1] * x + m[3] * y) / kOne) + parentTranslation[1]};
}

namespace {
// The sine table's angle index for a radian angle (020ad220: 65536ths of a
// turn, top 12 bits).
std::size_t angleIndex(std::int32_t angle) {
    const std::int64_t turn = (std::int64_t(angle) << 16) / kTwoPi;
    return std::size_t((std::uint16_t(turn) >> 4) & 0xfff);
}

std::int32_t tableSine(std::size_t i) {
    return std::int32_t(std::lround(4096.0 * std::sin(2 * M_PI * double(i) / 4096.0)));
}

std::int32_t mul(std::int64_t a, std::int64_t b) {
    return std::int32_t((a * b) / kOne);
}

// A step's angle: step * 2 pi / count (020836e4, the divider in 20.12).
std::int32_t stepAngle(int step, int count) {
    const std::int32_t turns = std::int32_t((std::int64_t(step) * kOne * kTwoPi) / kOne);
    return wrapAngle(std::int32_t((std::int64_t(turns) << 12) / (std::int64_t(count) << 12)));
}
} // namespace

std::array<std::int32_t, 4> Chara::matrix() const {
    const std::size_t i = angleIndex(rotation.value());
    const std::int32_t sn = tableSine(i), cs = tableSine((i + 1024) & 0xfff);
    const auto sc = scale.value();
    const std::array<std::int32_t, 4> own{mul(cs, sc[0]), mul(sn, sc[0]), mul(-sn, sc[1]),
                                          mul(cs, sc[1])};
    const auto& m = parentMatrix;
    return {mul(m[0], own[0]) + mul(m[2], own[1]), mul(m[1], own[0]) + mul(m[3], own[1]),
            mul(m[0], own[2]) + mul(m[2], own[3]), mul(m[1], own[2]) + mul(m[3], own[3])};
}

void Chara::setSteps(int count, std::int32_t frames) {
    // 020838d0: a step is 2 pi / count (single precision, truncated).
    steps = {};
    if (count == 0) {
        return;
    }
    steps.count = count;
    steps.frames = frames;
    steps.angle = std::int32_t(6.2831855f / float(count) * 4096.0f);
}

bool Chara::setStep(int step) {
    // 020836e4
    if (steps.count == 0) {
        steps.step = 0;
        return false;
    }
    steps.step = int(unsigned(step) % unsigned(steps.count));
    rotation.set(stepAngle(steps.step, steps.count));
    return true;
}

int Chara::stepAt(std::int32_t angle) const {
    // 02083948: half a step toward the angle's side, then whole steps.
    if (steps.count == 0) {
        return 0;
    }
    const std::int32_t half = std::int32_t((std::int64_t(steps.angle) * 0x800) / kOne);
    const std::int32_t a = angle < 0 ? angle - half : angle + half;
    const int q = int((float(a) / 4096.0f) / (float(steps.angle) / 4096.0f));
    const int r = q % steps.count;
    return r < 0 ? r + steps.count : r;
}

bool Chara::turnTo(std::int32_t angle, int curve, bool lock) {
    // 02083a50
    if (steps.count == 0) {
        return false;
    }
    steps.step = stepAt(angle);
    return rotation.turn(stepAngle(steps.step, steps.count), steps.frames, curve, lock);
}

void Chara::setSwitch(Switches::Kind k, bool on) {
    // lt4CChara 020db38c / 020db3e4 (Master, Touch); the other two are the
    // plain gnCChara setters (020a9508, 020a952c).
    if ((k == Switches::Master || k == Switches::Touch) && on && state != 0) {
        const bool could = touchable();
        switches.set(k, true);
        if (!could) {
            enterState(0);
        }
        return;
    }
    switches.set(k, on);
}

bool Chara::touchable() const {
    // 020db43c
    return switches.active(Switches::Touch) && touchKind != -1 && state != -1 && state != 3 &&
           state != 6;
}

Animation::Box Chara::bounds() const {
    if (!touchAreas.empty()) {
        // 020ac340: the union of the touch areas.
        Animation::Box b{touchAreas[0][0], touchAreas[0][1], touchAreas[0][0] + touchAreas[0][2],
                         touchAreas[0][1] + touchAreas[0][3]};
        for (const auto& a : touchAreas) {
            b.x1 = std::min(b.x1, a[0]);
            b.y1 = std::min(b.y1, a[1]);
            b.x2 = std::max(b.x2, a[0] + a[2]);
            b.y2 = std::max(b.y2, a[1] + a[3]);
        }
        return b;
    }
    if (visual == Visual::Animation && animation.sequence()) {
        return animation.sequence()->box;
    }
    if (visual == Visual::Image && image && image->picture) {
        return {0, 0, int(image->picture->width), int(image->picture->height)};
    }
    return {};
}

namespace {
// ov6 0208eb84: a pixel of a picture part that is drawn.
bool covers(const Picture& p, const Rect& part, int x, int y) {
    if (x < 0 || y < 0 || x >= part.width || y >= part.height) {
        return false;
    }
    const int px = part.x + x, py = part.y + y;
    if (px >= int(p.width) || py >= int(p.height)) {
        return false;
    }
    return (p.pixels[std::size_t(py) * p.width + std::size_t(px)] & 0x8000) != 0;
}
} // namespace

int Chara::hit(int x, int y) const {
    if (visual == Visual::None || !touchable()) {
        return -1;
    }
    // Into the object's space: minus where it is, through the inverse of
    // its matrix (+0x140, 020ad4ec), rounded.
    const auto m = matrix();
    const std::int64_t det = (std::int64_t(m[0]) * m[3] - std::int64_t(m[2]) * m[1]) / kOne;
    if (det == 0) {
        return -1;
    }
    const std::int64_t dx = (std::int64_t(x) << 12) - world[0],
                       dy = (std::int64_t(y) << 12) - world[1];
    const std::int64_t ux = (m[3] * dx - m[2] * dy) / det, uy = (m[0] * dy - m[1] * dx) / det;
    const int lx = int((ux + 0x800) >> 12), ly = int((uy + 0x800) >> 12);
    if (!touchAreas.empty()) {
        for (auto a = touchAreas.rbegin(); a != touchAreas.rend(); ++a) {
            const int ax = lx - (*a)[0], ay = ly - (*a)[1];
            if (ax >= 0 && ay >= 0 && ax < (*a)[2] && ay < (*a)[3]) {
                return int(std::uint16_t((*a)[4]));
            }
        }
        return -1;
    }
    // ov6 020952fc / 02093f48 / 02093de4: the step's composite, its box,
    // then every image's parts. (Pictures, 0208eaec, are hit only with a
    // resource flag not reconstructed: treated as misses.)
    if (visual != Visual::Animation || !animation.resource() || !animation.sequence()) {
        return -1;
    }
    const Animation& anim = *animation.resource()->animation;
    const Animation::Step* s = animation.step();
    if (!s || s->index >= anim.composites.size()) {
        return -1;
    }
    const auto& composite = anim.composites[s->index];
    const int px = lx - s->dx, py = ly - s->dy;
    if (unsigned(px - composite.box.x1) >=
            unsigned(std::int16_t(composite.box.x2 - composite.box.x1)) ||
        unsigned(py - composite.box.y1) >=
            unsigned(std::int16_t(composite.box.y2 - composite.box.y1))) {
        return -1;
    }
    for (const auto& cc : composite.cells) {
        if (cc.index >= anim.images.size()) {
            continue;
        }
        const auto& image = anim.images[cc.index];
        if (image.picture < 0 || std::size_t(image.picture) >= anim.pictures.size()) {
            continue;
        }
        const Picture& picture = anim.pictures[std::size_t(image.picture)];
        for (const auto& ic : image.cells) {
            if (ic.index < picture.parts.size() &&
                covers(picture, picture.parts[ic.index], px - cc.x - ic.x, py - cc.y - ic.y)) {
                return 0;
            }
        }
    }
    return -1;
}

bool Chara::enterState(int s) {
    // 020db5ac: the block of the state's slot (ov7 020e28e8: states 0..6
    // use slots 0, 1, 2, 3, 4, 0, 5), else state 0's.
    constexpr std::array<std::size_t, 7> kSlot{0, 1, 2, 3, 4, 0, 5};
    if (s < 0 || s > 6 || !program) {
        return false;
    }
    const std::string& block = callbacks[kSlot[std::size_t(s)]];
    if (!block.empty() && thread.start(*program, block)) {
        state = s;
        return true;
    }
    if (s != 0) {
        return enterState(0);
    }
    state = 0;
    return false;
}

void Chara::update(std::int32_t elapsed) {
    if (visual == Visual::None) {
        return;
    }
    // lt4CChara 020db8e4: the object's own thread first (while it
    // updates); after a click (states 3 and 6) the object returns to its
    // normal state.
    const bool updating = switches.active(Switches::Update);
    if (updating) {
        thread.run();
    }
    if ((state == 3 || state == 6) && !thread.running()) {
        enterState(0);
    }
    // gnCChara 020accf4: the tweens, then the animation; where it is in
    // the screen is worked out either way.
    if (updating) {
        alpha.update(elapsed);
        position.update(elapsed);
        offset.update(elapsed);
        rotation.update(elapsed);
        scale.update(elapsed);
        shake.update(elapsed);
        shaking = {};
        const auto amount = shake.value();
        if (amount[0] || amount[1]) {
            for (std::size_t i = 0; i < 2; ++i) {
                const float r = 2.0f * scene.machine.randomFloat(1.0f) - 1.0f;
                shaking[i] = std::int32_t(float(amount[i]) * r);
            }
        }
        if (visual == Visual::Animation) {
            animation.update(elapsed);
        }
    }
    updateWorld();
}

// --------------------------------------------------------------- scene

ColourMatrix ColourMatrix::of(std::array<std::int32_t, 3> v, std::array<std::int32_t, 3> t,
                              std::int32_t s) {
    // The game's RGB -> YIQ (ITCM 01ff86e0) and YIQ -> RGB (01ff871c)
    // matrices, 4.12, as its start-up code fills them (short constants of
    // that routine).
    static constexpr std::int32_t kToYiq[9] = {1224,  2441, 868,   2404, -1122,
                                               -2142, 466,  -1318, 1273};
    static constexpr std::int32_t kToRgb[9] = {4096,  4096, 4096,  3915, -1114,
                                               -4526, 2543, -2650, 6971};
    auto fx = [](std::int32_t a, std::int32_t b) {
        return std::int32_t((std::int64_t(a) * b) >> 12);
    };
    // 01ff8730: out[3c + r] = sum_j A[3j + r] * (B[3c + j] * v_j).
    std::array<std::int32_t, 9> o{};
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            std::int32_t sum = 0;
            for (int j = 0; j < 3; ++j) {
                sum += kToRgb[3 * j + r] * fx(kToYiq[3 * c + j], v[std::size_t(j)]);
            }
            o[std::size_t(3 * c + r)] = sum >> 12;
        }
    }
    // 01ff8e4c: (1 - s) of that, s of the target colour.
    ColourMatrix out;
    for (std::size_t i = 0; i < 9; ++i) {
        out.m[i] = fx(o[i], kOne - s);
    }
    for (std::size_t i = 0; i < 3; ++i) {
        out.k[i] = fx(t[i], s);
    }
    return out;
}

void Screen::setColour(const ColourMatrix& c) {
    colour = colourFrom = colourTo = c;
    colourTime = colourFrames = 0;
}

void Screen::moveColour(const ColourMatrix& c, std::int32_t frames) {
    if (frames <= 0) {
        setColour(c);
        return;
    }
    colourFrom = colour;
    colourTo = c;
    colourTime = 0;
    colourFrames = frames;
}

Scene::Scene(Assets& a) : assets(a) {
    for (auto& s : screens) {
        s.brightness.set(0);
        s.blend.set(kOne);
    }
    // The tweens' random curves draw on this scene's generator (the game
    // has one).
    Tween::random = [](void* scene) {
        return static_cast<Scene*>(scene)->machine.randomFloat(1.0f);
    };
    Tween::randomContext = this;
}

Scene::~Scene() {
    if (Tween::randomContext == this) {
        Tween::random = nullptr;
        Tween::randomContext = nullptr;
    }
}

const gss::Program& Scene::loadScript(const std::string& path) {
    scriptFiles_.push_back(assets.scriptBytes(path));
    const auto parsed = import::adventure4::parseGss1(scriptFiles_.back());
    auto program = std::make_unique<gss::Program>(path, parsed);
    // A new script links to the one loaded before it (ov6 0208c7ac).
    program->next = newestScript();
    scripts_.push_back(std::move(program));
    scriptLevels_.push_back(level);
    return *scripts_.back();
}

bool Scene::loadImage(const std::string& name, int screen, const std::string& path) {
    if (screen < 0 || screen > 1 || !assets.exists(path)) {
        return false;
    }
    images_[nameHash(name)] = ImageResource{name, screen, &assets.picture(path), level};
    return true;
}

bool Scene::loadAnimation(const std::string& name, int screen, const std::string& path) {
    if (screen < 0 || screen > 1 || !assets.exists(path)) {
        return false;
    }
    animations_[nameHash(name)] =
        AnimationResource{name, screen, &assets.animation(path), level, {}, {}, {}, 0, 0};
    return true;
}

const ImageResource* Scene::image(std::string_view name) const {
    const auto it = images_.find(nameHash(name));
    return it == images_.end() ? nullptr : &it->second;
}

AnimationResource* Scene::animation(std::string_view name) {
    const auto it = animations_.find(nameHash(name));
    return it == animations_.end() ? nullptr : &it->second;
}

const AnimationResource* Scene::animation(std::string_view name) const {
    const auto it = animations_.find(nameHash(name));
    return it == animations_.end() ? nullptr : &it->second;
}

bool Scene::unloadImage(std::string_view name) {
    return images_.erase(nameHash(name)) > 0;
}

bool Scene::unloadAnimation(std::string_view name) {
    return animations_.erase(nameHash(name)) > 0;
}

Chara* Scene::create(const std::string& name, int kind, std::span<const gss::Value> values) {
    const std::uint32_t h = nameHash(name);
    if (const auto it = objects_.find(h); it != objects_.end()) {
        if (!it->second->deleted) {
            return nullptr;
        }
        // A removed object of the same name (a closed level's, purged at
        // the next update) makes way; it is kept until then in case its own
        // thread is the one running.
        removed_.push_back(std::move(it->second));
        objects_.erase(it);
    }
    auto object = std::make_unique<Chara>(*this, name, kind, newestScript(), values);
    Chara* c = object.get();
    c->level = level;
    objects_.emplace(h, std::move(object));
    return c;
}

Chara* Scene::find(std::string_view name) const {
    const auto it = objects_.find(nameHash(name));
    return it == objects_.end() || it->second->deleted ? nullptr : it->second.get();
}

Chara* Scene::find(std::uint32_t h) const {
    const auto it = objects_.find(h);
    return it == objects_.end() || it->second->deleted ? nullptr : it->second.get();
}

Chara* Scene::objectAt(int screen, int x, int y, int& area) const {
    if (screen < 0 || screen > 1) {
        return nullptr;
    }
    const auto& order = drawOrder_[std::size_t(screen)];
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        if ((*it)->deleted) {
            continue;
        }
        const int a = (*it)->hit(x, y);
        if (a >= 0) {
            area = a;
            return *it;
        }
    }
    return nullptr;
}

bool Scene::remove(std::string_view name) {
    Chara* c = find(name);
    if (!c) {
        return false;
    }
    c->deleted = true;
    for (auto& order : drawOrder_) {
        order.erase(std::remove(order.begin(), order.end(), c), order.end());
    }
    return true;
}

void Scene::removeAllImages() {
    images_.clear();
}

void Scene::removeAllAnimations() {
    animations_.clear();
}

void Scene::removeAllObjects() {
    for (Chara* c : objects()) {
        remove(c->name);
    }
}

void Scene::removeAllMessages() {
    messages_.clear();
}

void Scene::removeAllPaints() {
    for (auto& [h, p] : paints_) {
        clearPaint(*p);
    }
    paints_.clear();
    paintOrder_.clear();
}

namespace {
constexpr const char* kRoomPack = "lt4_map"; // ov7 020e3c48
} // namespace

bool Scene::showRoom(int id, int map, char variant, bool withScript) {
    // 020cf940: the room already shown stays as it is.
    if (id == room.shown) {
        return true;
    }
    resetRoom();
    char picture[32], pack[32], script[32];
    std::snprintf(picture, sizeof picture, "map/m%04d%c.ci", map, variant);
    std::snprintf(pack, sizeof pack, "map/m%04d%c.ca", map, variant);
    std::snprintf(script, sizeof script, "res/map/m%04d.cq", map);
    if (withScript) {
        room.script = assets.exists(script) ? script : "";
        if (assets.exists(pack)) {
            // Level 0: the pack outlives the loops (020d132c with level 0).
            const int saved = level;
            level = 0;
            loadAnimation(kRoomPack, 0, pack);
            level = saved;
        }
    }
    screens[0].background = assets.exists(picture) ? &assets.picture(picture) : nullptr;
    room.shown = id;
    return true;
}

void Scene::resetRoom() {
    // 020cefe8.
    room.parts.clear();
    room.shown = -1;
    room.ready = false;
    room.script.clear();
    animations_.erase(nameHash(kRoomPack));
}

void Scene::updateRoom() {
    // 020cf080: once (the files are in at once in the port).
    if (room.ready || room.shown < 0) {
        return;
    }
    if (const AnimationResource* r = animation(kRoomPack)) {
        const auto& sequences = r->animation->sequences;
        for (std::size_t i = 0; i < sequences.size() && i < 16; ++i) {
            auto part = std::make_unique<Room::Part>();
            part->player.attach(r);
            part->player.play(&sequences[i], AnimationPlayer::Restart);
            part->alpha.set(kOne);
            room.parts.push_back(std::move(part));
        }
    }
    if (!room.script.empty()) {
        // A program of its own, linked in front of the loaded scripts,
        // run through on a thread with 16 values and then dropped.
        const auto bytes = assets.scriptBytes(room.script);
        const auto parsed = import::adventure4::parseGss1(bytes);
        gss::Program program(room.script, parsed);
        program.next = newestScript();
        gss::Thread thread(machine, 16);
        if (thread.start(program, "MapMain")) {
            for (int guard = 0; guard < 100000 && thread.running(); ++guard) {
                thread.run();
            }
        }
    }
    room.ready = true;
}

void Scene::resetArea() {
    // 020cfd64: the area's pack, its objects (kind 1) and its two text
    // boxes go.
    areaShown = -1;
    for (Chara* c : objects()) {
        if (c->kind == 1) {
            remove(c->name);
        }
    }
    animations_.erase(nameHash("lt4_gmp")); // ov7 020e3c58
    removeMessage("lt4_plc");               // 020e3c60
    removeMessage("lt4_aim");               // 020e3c68
}

bool Scene::removeMessage(std::string_view name) {
    return messages_.erase(nameHash(name)) != 0;
}

Scene::Room::Part* Scene::roomPart(std::string_view sequence) {
    const AnimationResource* r = animation(kRoomPack);
    if (!r) {
        return nullptr;
    }
    const auto& sequences = r->animation->sequences;
    for (std::size_t i = 0; i < sequences.size() && i < room.parts.size(); ++i) {
        if (sequences[i].name == sequence) {
            return room.parts[i].get();
        }
    }
    return nullptr;
}

// 020ef760..020ef77c: frame * 60.0 / rate (+0x380: the header's rate /
// 2^24, 020ef804), in floats, truncated.
static int filmTime(const Scene::Film& film) {
    if (!film.frames || film.frame < 0) {
        return -1;
    }
    const float rate = float(film.frames->numerator) / 16777216.0f;
    return int(float(film.frame) * 60.0f / rate);
}

void Scene::updateFilm() {
    // 020ccdac: a film asked for opens when no process runs.
    if (film.state < 0 && film.request >= 0) {
        char path[24];
        std::snprintf(path, sizeof path, "mov/m%02d.mods", film.request);
        film.number = film.request;
        film.request = -1;
        film.frame = -1;
        film.shown = 0;
        film.clock = 0;
        try {
            static std::map<std::string, std::shared_ptr<const mobiclip::Tables>> tables;
            auto& t = tables[assets.root.string()];
            if (!t) {
                t = mobiclip::Tables::fromOverlay(assets.code(1),
                                                  mobiclip::Tables::kAdventure4Layout);
            }
            film.frames = std::make_shared<MovieFrames>(assets.root / path, t);
            film.state = 0;
        } catch (const std::exception&) {
            film.frames.reset();
        }
        return;
    }
    switch (film.state) {
    case 0: // opened: ready to start
        film.state = 1;
        break;
    case 2: // started: the first frame shows
        film.state = 3;
        film.clock = 0;
        film.frame = 0;
        film.shown = 1;
        film.time = 0;
        break;
    case 3: {
        // The film's rate (8.24 frames a second) against 60 frames a
        // second; frames the game falls behind on are skipped.
        film.clock += film.frames->numerator;
        unsigned due = unsigned(film.clock / (std::uint64_t(60) << 24));
        if (due >= film.frames->count && film.flag) {
            // ov10 020efa74: a looping film (9100's flag) opens again and
            // plays on from its first frame.
            film.clock = 0;
            film.frame = 0;
            film.shown = 1;
        } else if (due >= film.frames->count) {
            film.state = 5;
        } else if (int(due) != film.frame) {
            film.frame = int(due);
            ++film.shown;
        }
        film.time = filmTime(film);
        break;
    }
    case 6: // stopped: the process ends
        film = {};
        break;
    default:
        break;
    }
}

void Scene::place(Chara& c, int screen, int priority) {
    // 020dbc88: out of its old screen's order, into the new one after every
    // object of the same or a lower priority.
    if (priority < 0) {
        priority = c.priority;
    }
    for (auto& order : drawOrder_) {
        order.erase(std::remove(order.begin(), order.end(), &c), order.end());
    }
    c.screen = screen;
    c.priority = priority;
    if (screen < 0 || screen > 1 || priority >= 14) {
        return;
    }
    auto& order = drawOrder_[std::size_t(screen)];
    auto at = order.begin();
    while (at != order.end() && (*at)->priority <= priority) {
        ++at;
    }
    order.insert(at, &c);
}

std::vector<Chara*> Scene::objects(int screen) const {
    std::vector<Chara*> out;
    for (const auto& [h, c] : objects_) {
        if (!c->deleted && (screen < 0 || c->screen == screen)) {
            out.push_back(c.get());
        }
    }
    return out;
}

Chara* Scene::objectOfKind(int kind, int index) const {
    // 020dcfd8: counting the live objects of the kind in the manager's
    // order.
    if (index < 0) {
        return nullptr;
    }
    for (const auto& [h, c] : objects_) {
        if (!c->deleted && c->kind == kind && index-- == 0) {
            return c.get();
        }
    }
    return nullptr;
}

Chara* Scene::objectOnPlace(std::uint32_t placeHash, int index) const {
    // 020dd054
    if (placeHash == 0 || index < 0) {
        return nullptr;
    }
    for (const auto& [h, c] : objects_) {
        if (!c->deleted && c->placeHash == placeHash && index-- == 0) {
            return c.get();
        }
    }
    return nullptr;
}

Place* Scene::createPlace(const std::string& name, std::array<std::int32_t, 2> at,
                          const std::string& parent) {
    // 020d6ef4 / 020d63ac: identity matrix, the position as given.
    const std::uint32_t h = nameHash(name);
    if (places_.contains(h)) {
        return nullptr;
    }
    auto p = std::make_unique<Place>();
    p->name = name;
    p->hash = h;
    p->parentHash = nameHash(parent);
    p->local = at;
    p->position = at;
    p->level = level;
    Place* raw = p.get();
    places_.emplace(h, std::move(p));
    return raw;
}

Place* Scene::findPlace(std::uint32_t h) {
    const auto it = places_.find(h);
    return it == places_.end() ? nullptr : it->second.get();
}

Place* Scene::findPlace(std::string_view name) {
    return findPlace(nameHash(name));
}

const Place* Scene::nearestPlace(std::array<std::int32_t, 2> at, std::int32_t radius) const {
    // 020d782c: the shown place closest to the point, nearer than the
    // radius; squared distances in 20.12 (products >> 12), the first of
    // equals wins.
    auto square = [](std::int64_t v) {
        return std::int32_t((v * v) >> 12);
    };
    std::int32_t best = square(radius);
    const Place* found = nullptr;
    for (const auto& [h, p] : places_) {
        if (!p->switches.on(Switches::Master)) {
            continue;
        }
        const std::int64_t dx = p->position[0] - at[0], dy = p->position[1] - at[1];
        const std::int32_t d = std::int32_t((dx * dx + dy * dy) >> 12);
        if (d < best) {
            best = d;
            found = p.get();
        }
    }
    return found;
}

int Scene::placesInside(std::array<std::int32_t, 2> at, std::array<std::int32_t, 2> size) const {
    int n = 0;
    for (const auto& [h, p] : places_) {
        if (p->switches.on(Switches::Master) && p->position[0] >= at[0] &&
            p->position[1] >= at[1] && p->position[0] < at[0] + size[0] &&
            p->position[1] < at[1] + size[1]) {
            ++n;
        }
    }
    return n;
}

const Place* Scene::placeInside(std::array<std::int32_t, 2> at, std::array<std::int32_t, 2> size,
                                int n) const {
    if (n < 0) {
        return nullptr;
    }
    const Place* found = nullptr;
    int i = 0;
    for (const auto& [h, p] : places_) {
        if (p->switches.on(Switches::Master) && p->position[0] >= at[0] &&
            p->position[1] >= at[1] && p->position[0] < at[0] + size[0] &&
            p->position[1] < at[1] + size[1]) {
            found = p.get();
            if (i++ == n) {
                break;
            }
        }
    }
    return found;
}

std::vector<Message*> Scene::messages(int screen) {
    std::vector<Message*> out;
    for (auto& [h, m] : messages_) {
        if (screen < 0 || m->screen == screen) {
            out.push_back(m.get());
        }
    }
    return out;
}

void Scene::freeLevel() {
    for (auto it = places_.begin(); it != places_.end();) {
        it = it->second->level == level ? places_.erase(it) : std::next(it);
    }
    for (auto it = paints_.begin(); it != paints_.end();) {
        if (it->second->level == level) {
            // The drawing goes with the paint (its layer is released).
            clearPaint(*it->second);
            std::erase(paintOrder_, it->second.get());
            it = paints_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = messages_.begin(); it != messages_.end();) {
        it = it->second->level == level ? messages_.erase(it) : std::next(it);
    }
    for (Chara* c : objects()) {
        if (c->level == level) {
            remove(c->name);
        }
    }
    std::erase_if(texts_, [&](const auto& e) { return e.second.level == level; });
    std::erase_if(animations_, [&](const auto& e) { return e.second.level == level; });
    std::erase_if(images_, [&](const auto& e) { return e.second.level == level; });
    // The script cache (ov6 0208c96c): the level's scripts leave the
    // newest-first chain.
    for (std::size_t i = scripts_.size(); i-- > 0;) {
        if (scriptLevels_[i] == level) {
            freedScripts_.push_back(std::move(scripts_[i]));
            scripts_.erase(scripts_.begin() + std::ptrdiff_t(i));
            scriptLevels_.erase(scriptLevels_.begin() + std::ptrdiff_t(i));
        }
    }
    for (std::size_t i = 0; i < scripts_.size(); ++i) {
        scripts_[i]->next = i ? scripts_[i - 1].get() : nullptr;
    }
    --level;
}

// ------------------------------------------------------------- paints

bool Paint::hit(int x, int y) const {
    // 020b285c: inside the area (unsigned compares), when the paint can be
    // touched.
    if (mode == 0 || !touchable()) {
        return false;
    }
    return unsigned(std::uint16_t(x - area[0])) < unsigned(std::uint16_t(area[2] - area[0])) &&
           unsigned(std::uint16_t(y - area[1])) < unsigned(std::uint16_t(area[3] - area[1]));
}

void Paint::press(int x, int y) {
    // 020b2144 (020b2144's clear-on-press runs through Scene::clearPaint
    // from the loop).
    if (pressed) {
        return;
    }
    start = current = previous = {x - area[0], y - area[1]};
    if (mode == 1 || mode == 5 || mode == 8 || (mode & 0x10)) {
        dirty = true;
    }
    if (mode & 0x10) {
        strokes.emplace_back();
    }
    pressed = true;
    move(x, y);
}

void Paint::move(int x, int y) {
    // 020b2250
    if (!pressed) {
        return;
    }
    previous = current;
    current = {x - area[0], y - area[1]};
    if (mode == 4 || mode == 8) {
        dirty = true;
    } else if (mode & 0x10) {
        dirty = true;
        if (unsigned(std::uint16_t(current[0])) < unsigned(std::uint16_t(area[2] - area[0])) &&
            unsigned(std::uint16_t(current[1])) < unsigned(std::uint16_t(area[3] - area[1]))) {
            strokes.back().push_back(current);
        }
    }
}

void Paint::release() {
    // 020b2380
    if (!pressed) {
        return;
    }
    if (mode == 2 || mode == 3) {
        dirty = true;
    }
    pressed = false;
}

namespace {
// ov6 02096b98: a pen `width` wide from a to b (pixels, 20.12) into a
// screen's drawing layer, row by row inside `clip` (x1, y1, x2, y2): the
// points nearer to the segment than half the width (rounded inward to
// whole pixels).
void drawPen(std::vector<std::uint16_t>& layer, const std::array<int, 4>& clip,
             std::array<double, 2> a, std::array<double, 2> b, double width, std::uint16_t ink) {
    const double r = width / 2;
    const double top = std::max<double>(clip[1], std::ceil(std::min(a[1], b[1]) - r));
    const double bottom = std::min<double>(clip[3] - 1, std::floor(std::max(a[1], b[1]) + r));
    const double dx = b[0] - a[0], dy = b[1] - a[1], length2 = dx * dx + dy * dy;
    for (double y = top; y <= bottom; ++y) {
        double lo = 1e9, hi = -1e9;
        auto disc = [&](const std::array<double, 2>& c) {
            const double h = r * r - (y - c[1]) * (y - c[1]);
            if (h >= 0) {
                const double w = std::sqrt(h);
                lo = std::min(lo, c[0] - w), hi = std::max(hi, c[0] + w);
            }
        };
        disc(a);
        disc(b);
        if (length2 > 0 && dy != 0) {
            // The band around the segment on this row: within r of the line
            // and between the ends' perpendiculars.
            const double length = std::sqrt(length2);
            const double xLine = a[0] + dx * (y - a[1]) / dy;
            const double spread = std::abs(r * length / dy);
            double l = xLine - spread, h = xLine + spread;
            // Projection 0..length2 of (p - a) on (dx, dy): dx (x - ax) + dy (y - ay).
            const double base = dy * (y - a[1]);
            const double p0 = (0 - base) / dx, p1 = (length2 - base) / dx;
            if (dx != 0) {
                l = std::max(l, std::min(p0, p1) + a[0]);
                h = std::min(h, std::max(p0, p1) + a[0]);
            } else if (base < 0 || base > length2) {
                l = 1, h = 0; // beyond the ends of an upright segment
            }
            if (l <= h) {
                lo = std::min(lo, l), hi = std::max(hi, h);
            }
        } else if (length2 > 0 && std::abs(y - a[1]) <= r) {
            lo = std::min(lo, std::min(a[0], b[0])), hi = std::max(hi, std::max(a[0], b[0]));
        }
        const int x1 = std::max(clip[0], int(std::ceil(lo))),
                  x2 = std::min(clip[2] - 1, int(std::floor(hi)));
        for (int x = x1; x <= x2; ++x) {
            if (x >= 0 && x < 256 && y >= 0 && y < 192) {
                layer[std::size_t(y) * 256 + std::size_t(x)] = std::uint16_t(0x8000 | ink);
            }
        }
    }
}
} // namespace

Paint* Scene::createPaint(const std::string& name, int screen) {
    const std::uint32_t h = nameHash(name);
    if (paints_.contains(h) || screen < 0 || screen > 1) {
        return nullptr;
    }
    auto p = std::make_unique<Paint>();
    p->name = name;
    p->hash = h;
    p->screen = screen;
    p->level = level;
    Paint* raw = p.get();
    paints_.emplace(h, std::move(p));
    paintOrder_.push_back(raw);
    return raw;
}

Paint* Scene::paint(std::uint32_t h) {
    const auto it = paints_.find(h);
    return it == paints_.end() ? nullptr : it->second.get();
}

Paint* Scene::paint(std::string_view name) {
    return paint(nameHash(name));
}

Paint* Scene::paintAt(int screen, int x, int y) {
    for (auto it = paintOrder_.rbegin(); it != paintOrder_.rend(); ++it) {
        if ((*it)->screen == screen && (*it)->hit(x, y)) {
            return *it;
        }
    }
    return nullptr;
}

void Scene::clearPaint(Paint& p) {
    // 020b23c8: no drawn box; 020b242c clears the area in the layer.
    p.drawn = {};
    auto& layer = screens[std::size_t(p.screen)].drawing;
    for (int y = std::max(0, p.area[1]); y < std::min(192, p.area[3]); ++y) {
        for (int x = std::max(0, p.area[0]); x < std::min(256, p.area[2]); ++x) {
            layer[std::size_t(y) * 256 + std::size_t(x)] = 0;
        }
    }
    p.strokes.clear();
}

void Scene::readHandwriting(Paint& p) {
    if (!recognizer_) {
        // 020cd174: the recogniser's pack dcm/dcm_en.cb (scrdb.dat, the
        // database; conv_list.gu, "GCSU" + u16 offset + u16 count, then
        // {from, to} pairs sorted by `from`).
        const auto db = assets.packMember("dcm/dcm_en.cb", "scrdb.dat");
        recognizer_ = std::make_unique<handwriting::Recognizer>(db);
        const auto list = assets.packMember("dcm/dcm_en.cb", "conv_list.gu");
        if (list.size() < 8 || std::string(list.begin(), list.begin() + 4) != "GCSU") {
            throw std::runtime_error("unexpected conv_list.gu");
        }
        const unsigned at = list[4] | list[5] << 8, count = list[6] | list[7] << 8;
        for (unsigned i = 0; i < count && at + 4 * i + 4 <= list.size(); ++i) {
            const std::size_t k = at + 4 * i;
            characterMap_.push_back({std::uint16_t(list[k] | list[k + 1] << 8),
                                     std::uint16_t(list[k + 2] | list[k + 3] << 8)});
        }
    }
    // ov9 020ef1e8: settings zeroed, then the character classes by mode
    // (0x10: 0x10, 0x11: 4, 0x12: 8, 0x13: 0xc), the second classes by
    // language (en 1, sp 8, fr 2, it 0x10, ge 4), slant 0x400; four results.
    handwriting::Settings r;
    const int m = p.mode - 0x10;
    r.categories = m == 0 ? 0x10 : m == 1 ? 4 : m == 2 ? 8 : m == 3 ? 0xc : 0;
    static constexpr std::array<int, 6> kByLanguage{0, 1, 8, 2, 0x10, 4};
    r.categories2 = language >= 1 && language < int(kByLanguage.size())
                        ? kByLanguage[std::size_t(language)]
                        : 0;
    r.slant = 0x400;
    r.baseline = 0;
    r.helpline = 0;
    std::vector<handwriting::Stroke> strokes;
    for (const auto& s : p.strokes) {
        handwriting::Stroke stroke;
        for (const auto& pt : s) {
            stroke.push_back({std::int16_t(pt[0]), std::int16_t(pt[1])});
        }
        strokes.push_back(std::move(stroke));
    }
    std::vector<handwriting::Result> results;
    if (strokes.empty() || recognizer_->recognize(strokes, r, 4, results) != 0) {
        results.clear();
    }
    // 020ef3a0: each result's first character mapped to a byte (0207e96c);
    // 020dfd1c keeps the first.
    p.answer.clear();
    for (const auto& result : results) {
        if (result.text.empty() || result.text[0] == 0) {
            break;
        }
        const auto it = std::lower_bound(
            characterMap_.begin(), characterMap_.end(), result.text[0],
            [](const std::array<std::uint16_t, 2>& e, char16_t c) { return e[0] < c; });
        const std::uint8_t b =
            it != characterMap_.end() && (*it)[0] == result.text[0] ? std::uint8_t((*it)[1]) : 0;
        if (b != 0) {
            p.answer = std::string(1, char(b));
            break;
        }
    }
    if (results.empty()) {
        p.answer = "?";
    }
}

void Scene::updatePaints() {
    // 020b2948 for the pen modes (4, 8 and the handwriting ones): a pen
    // stroke from the previous point to the current one; the drawn box
    // grows by the current point.
    for (Paint* p : paintOrder_) {
        if (p->mode == 0 || !p->switches.active(Switches::Update) || !p->dirty) {
            continue;
        }
        p->dirty = false;
        auto& layer = screens[std::size_t(p->screen)].drawing;
        const std::array<int, 4> clip = p->area;
        const double ox = p->area[0], oy = p->area[1];
        std::array<int, 4> box{};
        switch (p->mode) {
        case 1: // a dot
            drawPen(layer, clip, {ox + p->current[0], oy + p->current[1]},
                    {ox + p->current[0], oy + p->current[1]}, 1, p->ink);
            box = {p->current[0], p->current[1], p->current[0] + 1, p->current[1] + 1};
            break;
        case 2: // straight lines, boxes and fills are not used by the
        case 3: // scripts reconstructed so far
        case 5:
            throw std::runtime_error("paint mode " + std::to_string(p->mode) +
                                     " not reconstructed");
        default:
            drawPen(layer, clip, {ox + p->previous[0], oy + p->previous[1]},
                    {ox + p->current[0], oy + p->current[1]}, p->width / 4096.0, p->ink);
            box = {p->current[0], p->current[1], p->current[0] + 1, p->current[1] + 1};
            break;
        }
        // Into screen coordinates, the area's bounds, and the union with
        // what was drawn before (an empty box starts it).
        box = {std::max(box[0] + p->area[0], p->area[0]), std::max(box[1] + p->area[1], p->area[1]),
               std::min(box[2] + p->area[0], p->area[2]),
               std::min(box[3] + p->area[1], p->area[3])};
        if (p->drawn[0] < p->drawn[2] || p->drawn[1] < p->drawn[3]) {
            p->drawn = {std::min(p->drawn[0], box[0]), std::min(p->drawn[1], box[1]),
                        std::max(p->drawn[2], box[2]), std::max(p->drawn[3], box[3])};
        } else {
            p->drawn = box;
        }
    }
}

void Scene::pushSwitches() {
    // ov7 020cdd34: objects, messages, then the places (020d6e38).
    for (Chara* c : objects()) {
        c->switches.push();
    }
    for (auto& [h, m] : messages_) {
        m->switches.push();
    }
    for (auto& [h, p] : places_) {
        p->switches.push();
    }
    for (auto& [h, p] : paints_) {
        p->switches.push();
    }
}

void Scene::popSwitches() {
    // ov7 020cde20: each switch gets its saved state back through its
    // setter (020a95a8).
    for (Chara* c : objects()) {
        const auto saved = c->switches.popped();
        for (std::size_t k = 0; k < 4; ++k) {
            c->setSwitch(Switches::Kind(k), saved[k]);
        }
    }
    for (auto& [h, m] : messages_) {
        const auto saved = m->switches.popped();
        for (std::size_t k = 0; k < 4; ++k) {
            m->switches.set(Switches::Kind(k), saved[k]);
        }
    }
    for (auto& [h, p] : places_) {
        const auto saved = p->switches.popped();
        for (std::size_t k = 0; k < 4; ++k) {
            p->switches.set(Switches::Kind(k), saved[k]);
        }
    }
    for (auto& [h, p] : paints_) {
        const auto saved = p->switches.popped();
        for (std::size_t k = 0; k < 4; ++k) {
            p->switches.set(Switches::Kind(k), saved[k]);
        }
    }
}

Message* Scene::createMessage(const std::string& name, int screen, int width, int font) {
    const std::uint32_t h = nameHash(name);
    if (messages_.contains(h) || screen < 0 || screen > 1) {
        return nullptr;
    }
    auto m = std::make_unique<Message>();
    m->name = name;
    m->hash = h;
    m->screen = screen;
    m->width = width;
    m->font = font;
    m->level = level;
    Message* raw = m.get();
    messages_.emplace(h, std::move(m));
    return raw;
}

void Scene::eraseTexts(int screen, std::array<int, 4> box) {
    for (auto& [h, m] : messages_) {
        if (m->screen == screen && !m->fresh) {
            m->erased.push_back(box);
        }
    }
}

Message* Scene::message(std::string_view name) {
    const auto it = messages_.find(nameHash(name));
    return it == messages_.end() ? nullptr : it->second.get();
}

bool Scene::removeTexts(std::string_view name) {
    return texts_.erase(nameHash(name)) > 0;
}

bool Scene::loadTexts(const std::string& name, int group, int number) {
    // ov7 020d00e0: "txt/@/%02d/%02d_%06d.cb"
    char path[64];
    std::snprintf(path, sizeof path, "txt/@/%02d/%02d_%06d.cb", group, group, number);
    if (!assets.exists(path)) {
        return false;
    }
    const auto list = assets.data(path);
    TextList t;
    for (const auto& c : list.commands) {
        if (c.opcode != 1201 || c.count < 3) {
            continue;
        }
        const int id = int(std::int32_t(list.operands[c.first].value));
        t.entries[id].push_back(
            {list.text(list.operands[c.first + 1]), list.text(list.operands[c.first + 2])});
    }
    t.level = level;
    texts_[nameHash(name)] = std::move(t);
    return true;
}

int Scene::textCount(std::string_view name, int id) const {
    const auto it = texts_.find(nameHash(name));
    if (it == texts_.end()) {
        return 0;
    }
    const auto e = it->second.entries.find(id);
    return e == it->second.entries.end() ? 0 : int(e->second.size());
}

int Scene::textIdAt(std::string_view name, int index) const {
    const auto it = texts_.find(nameHash(name));
    if (it == texts_.end() || index < 0 || std::size_t(index) >= it->second.entries.size()) {
        return -1;
    }
    return std::next(it->second.entries.begin(), index)->first;
}

const std::string* Scene::text(std::string_view name, int id, int index, int field) const {
    // ov6 020a4844: the id's strings, then one of two fields.
    const auto it = texts_.find(nameHash(name));
    if (it == texts_.end() || field < 0 || field > 1 || index < 0) {
        return nullptr;
    }
    const auto entry = it->second.entries.find(id);
    if (entry == it->second.entries.end() || std::size_t(index) >= entry->second.size()) {
        return nullptr;
    }
    return &entry->second[std::size_t(index)][std::size_t(field)];
}

void Scene::updateObjects(std::int32_t elapsed) {
    // ov7 020dd994: the objects in the screens' draw orders (manager +0x80,
    // +0x90: by priority, then as placed; the lower screen's first), so an
    // object updates after the one its place hangs on when that one is
    // drawn below it; then the deleted ones go. Objects the port keeps out
    // of the draw orders (not on a screen yet, or priority 14 and up)
    // follow in name order (*hypothesis* for their place in the game's
    // lists).
    std::vector<Chara*> live;
    for (const auto& order : drawOrder_) {
        for (Chara* c : order) {
            if (!c->deleted) {
                live.push_back(c);
            }
        }
    }
    for (auto& [h, c] : objects_) {
        if (!c->deleted && std::find(live.begin(), live.end(), c.get()) == live.end()) {
            live.push_back(c.get());
        }
    }
    for (Chara* c : live) {
        if (c->deleted) {
            continue;
        }
        // The place the object sits on lends it its current transform.
        if (c->placeHash) {
            if (const Place* p = findPlace(c->placeHash)) {
                c->parentMatrix = p->matrix;
                c->parentTranslation = p->position;
            }
        }
        c->update(elapsed);
        // 020d77cc / 020d6504: places made with this object as their parent
        // follow it: its matrix, and its position plus the place's own
        // offset through that matrix (products rounded toward zero).
        const auto m = c->matrix();
        for (auto& [h, p] : places_) {
            if (p->parentHash != c->hash) {
                continue;
            }
            const std::int64_t x =
                std::int64_t(p->local[0]) * m[0] + std::int64_t(p->local[1]) * m[2];
            const std::int64_t y =
                std::int64_t(p->local[0]) * m[1] + std::int64_t(p->local[1]) * m[3];
            p->matrix = m;
            p->position = {std::int32_t(x / kOne) + c->world[0],
                           std::int32_t(y / kOne) + c->world[1]};
        }
    }
    std::erase_if(objects_, [](const auto& entry) { return entry.second->deleted; });
    removed_.clear();
}

void Scene::updateScreens(std::int32_t elapsed) {
    for (auto& s : screens) {
        s.brightness.update(elapsed);
        s.blend.update(elapsed);
        s.shake.update(elapsed);
        s.shaking = {};
        const auto amount = s.shake.value();
        for (std::size_t i = 0; i < 2 && (amount[0] || amount[1]); ++i) {
            const float r = 2.0f * machine.randomFloat(1.0f) - 1.0f;
            s.shaking[i] = int(float(amount[i]) * r) / kOne;
        }
        if (s.colourMoving()) {
            s.colourTime = std::min(s.colourFrames, s.colourTime + elapsed);
            auto lerp = [&](std::int32_t a, std::int32_t b) {
                return a + std::int32_t((std::int64_t(b - a) * s.colourTime) / s.colourFrames);
            };
            for (std::size_t i = 0; i < 9; ++i) {
                s.colour.m[i] = lerp(s.colourFrom.m[i], s.colourTo.m[i]);
            }
            for (std::size_t i = 0; i < 3; ++i) {
                s.colour.k[i] = lerp(s.colourFrom.k[i], s.colourTo.k[i]);
            }
        }
    }
    // The packs' colour treatments.
    for (auto& [h, a] : animations_) {
        if (a.colourTime < a.colourFrames) {
            a.colourTime = std::min(a.colourFrames, a.colourTime + elapsed);
            auto lerp = [&](std::int32_t x, std::int32_t y) {
                return x + std::int32_t((std::int64_t(y - x) * a.colourTime) / a.colourFrames);
            };
            for (std::size_t i = 0; i < 9; ++i) {
                a.colour.m[i] = lerp(a.colourFrom.m[i], a.colourTo.m[i]);
            }
            for (std::size_t i = 0; i < 3; ++i) {
                a.colour.k[i] = lerp(a.colourFrom.k[i], a.colourTo.k[i]);
            }
        }
    }
    // 020cf478: the room's parts.
    for (auto& part : room.parts) {
        part->player.update(elapsed);
        part->alpha.update(elapsed);
    }
    for (auto& [h, m] : messages_) {
        m->position.update(elapsed);
        m->offset.update(elapsed);
    }
}

void Scene::updateMessages(std::int32_t elapsed) {
    // ov7 020df748, after the objects.
    for (auto& [h, m] : messages_) {
        if (m->switches.active(Switches::Update)) {
            if (const int sound = m->update(elapsed); sound >= 0 && playEffect) {
                playEffect(sound);
            }
        }
        m->fresh = false; // its text is in the layer now
    }
}

namespace {
// The text layer's colours: fnt/color.lp (16 entries).
const std::vector<std::uint16_t>& textPalette(Assets& assets) {
    static std::vector<std::uint16_t> palette;
    if (palette.empty()) {
        std::ifstream in(assets.file("fnt/color.cp"), std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        palette = import::adventure4::parseGplt(bytes);
    }
    return palette;
}

// Escapes {xy} (table ov6 020b4790, read from the game's code).
const std::map<std::string, unsigned>& textEscapes(Assets& assets) {
    static std::map<std::string, unsigned> escapes;
    if (escapes.empty()) {
        for (std::uint32_t at = 0x020b4790;; at += 3) {
            const std::string bytes = assets.codeText(6, 0x0207b4c0, at);
            if (bytes.size() < 3) {
                break;
            }
            escapes[bytes.substr(0, 2)] = std::uint8_t(bytes[2]);
        }
    }
    return escapes;
}

// The digits of a tag after its letter ("<W30>" -> 30).
int tagNumber(const std::string& tag, std::size_t from) {
    int n = 0;
    for (std::size_t k = from; k < tag.size(); ++k) {
        n = n * 10 + (std::isdigit(std::uint8_t(tag[k])) ? tag[k] - '0' : 0);
    }
    return n;
}
} // namespace

const std::vector<std::uint16_t>& textColours(Assets& assets) {
    return textPalette(assets);
}

std::string plainText(Assets& assets, std::string_view t) {
    const auto& escapes = textEscapes(assets);
    std::string out;
    for (std::size_t i = 0; i < t.size();) {
        if (t[i] == '<' && t.find('>', i) != std::string_view::npos) {
            i = t.find('>', i) + 1;
        } else if (t[i] == '{' && t.find('}', i) != std::string_view::npos && i + 2 < t.size()) {
            const auto e = escapes.find(std::string(t.substr(i + 1, 2)));
            if (e != escapes.end()) {
                out += char(e->second);
            }
            i = t.find('}', i) + 1;
        } else {
            out += t[i++];
        }
    }
    return out;
}

// The game's font slot the message was made with: the boot loads
// fnt/font_en1.cf into slot 0 and fnt/font_en2.cf into slot 1 (ov8
// 020ef968, 020efa08); the cartridge's code names no other font.
static const Font& messageFont(Assets& assets, const Message& m) {
    return assets.font(m.font == 1 ? "fnt/font_en2.cf" : "fnt/font_en1.cf");
}

bool Scene::setText(Message& m, const std::string& t, bool instant, bool waitAtEnd) {
    m.clear();
    m.fresh = true;
    m.text = t;
    const Font& font = messageFont(assets, m);
    const auto& escapes = textEscapes(assets);
    using Record = Message::Record;
    // Lay the text out in lines (ov6 020ae868: glyphs advance by their
    // advance plus the letter spacing; a new line moves down by the font
    // height plus the line spacing and the room for ruby).
    // 020ae5f4: room for ruby above each line, none for a negative offset,
    // else the offset plus the ruby font's height (font slot 1,
    // hypothesis); glyphs sit below it.
    const int ruby =
        m.rubyOffset < 0 ? 0 : m.rubyOffset + int(assets.font("fnt/font_en2.cf").cellHeight);
    const int lineHeight = int(font.cellHeight) + ruby + m.lineSpacing;
    std::vector<std::pair<std::size_t, int>> lines{{0, 0}}; // first record, width
    int x = 0, y = 0, colour = m.textColour;
    std::size_t bracket = SIZE_MAX;
    auto closeBracket = [&] {
        if (bracket != SIZE_MAX) {
            m.records[bracket].value = int(m.records.size() - bracket);
            m.records.push_back({Record::Close});
            bracket = SIZE_MAX;
        }
    };
    for (std::size_t i = 0; i < t.size();) {
        const auto c = std::uint8_t(t[i]);
        unsigned code = c;
        if (c == '\n') {
            lines.back().second = x - m.letterSpacing;
            lines.push_back({m.records.size(), 0});
            x = 0;
            y += lineHeight;
            ++i;
            continue;
        }
        if (c >= 1 && c <= 0x1f) {
            ++i;
            continue;
        }
        if (c == '<') {
            const auto close = t.find('>', i);
            if (close != std::string::npos) {
                std::string tag = t.substr(i + 1, close - i - 1);
                const bool end = !tag.empty() && tag[0] == '/';
                if (end) {
                    tag.erase(0, 1);
                }
                const char letter = tag.empty() ? 0 : char(std::toupper(std::uint8_t(tag[0])));
                if (letter == 'C' && !end && tag.size() > 1) {
                    // <Cx>: colour codes B C G N O R S T W (020e28f0).
                    static const std::map<char, int> codes{{'B', 1}, {'C', 0}, {'G', 5},
                                                           {'N', 4}, {'O', 7}, {'R', 3},
                                                           {'S', 6}, {'T', 8}, {'W', 2}};
                    const auto it = codes.find(char(std::toupper(std::uint8_t(tag[1]))));
                    colour = it == codes.end() ? 0 : it->second;
                } else if (letter == 'C') {
                    colour = m.textColour;
                } else if (letter == 'T') {
                    // <T>..</T>: a stretch a tap shows at once.
                    if (end) {
                        closeBracket();
                    } else {
                        bracket = m.records.size();
                        m.records.push_back({Record::Open});
                    }
                } else if (letter == 'W' && !end) {
                    // <W>, <W n>, <WT>: a wait (ends a stretch first).
                    if (bracket != SIZE_MAX) {
                        closeBracket();
                        bracket = m.records.size();
                        m.records.push_back({Record::Open});
                    }
                    Record w{Record::Wait};
                    if (tag.size() == 1) {
                        w.value = 30;
                    } else if (std::isdigit(std::uint8_t(tag[1]))) {
                        w.value = tagNumber(tag, 1);
                    } else if (std::toupper(std::uint8_t(tag[1])) == 'T') {
                        w.value = -1;
                    }
                    m.records.push_back(w);
                } else if (letter == 'L' && !end) {
                    // <L s>: a pause until s seconds (60 frames each).
                    Record l{Record::Pause};
                    const auto dot = tag.find('.');
                    l.value = tagNumber(tag.substr(0, dot), 1) * 60;
                    m.records.push_back(l);
                }
                i = close + 1;
                continue;
            }
        }
        if (c == '{') {
            const auto close = t.find('}', i + 1);
            if (close != std::string::npos && i + 2 < t.size()) {
                const auto e = escapes.find(t.substr(i + 1, 2));
                i = close + 1;
                if (e == escapes.end()) {
                    continue;
                }
                code = e->second;
            } else {
                ++i;
            }
        } else if ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc)) {
            code = unsigned(c) << 8 | std::uint8_t(i + 1 < t.size() ? t[i + 1] : 0);
            i += 2;
        } else {
            ++i;
        }
        const FontGlyph* g = font.glyph(code);
        const int advance = g ? int(g->advance) : int(font.cellWidth);
        if (g) {
            m.records.push_back({Record::Glyph, g, x, y + ruby, colour});
        }
        x += advance + m.letterSpacing;
    }
    closeBracket();
    lines.back().second = x - m.letterSpacing;
    // 020afef0: flag 0 centres each line on the position, flag 1 the whole
    // text vertically.
    for (std::size_t l = 0; l < lines.size(); ++l) {
        const std::size_t end = l + 1 < lines.size() ? lines[l + 1].first : m.records.size();
        const int shift = (m.flags & 1) ? lines[l].second / 2 : 0;
        for (std::size_t r = lines[l].first; r < end; ++r) {
            m.records[r].x -= shift;
        }
    }
    if (m.flags & 2) {
        const int height = y + int(font.cellHeight) + ruby;
        for (auto& r : m.records) {
            r.y -= height - height / 2;
        }
    }
    m.instant = instant;
    m.waitAtEnd = waitAtEnd;
    return true;
}

void Message::clear() {
    // 020afe20
    records.clear();
    erased.clear();
    text.clear();
    cursor = 0;
    revealed = 0;
    clock = 0;
    waitFrames = pauseUntil = 0;
    bracketEnd = 0;
    soundCounter = 10000;
    instant = waitAtEnd = waiting = skip = inBracket = skipping = false;
}

int Message::update(std::int32_t elapsed) {
    // 020b03d0.
    if (records.empty()) {
        return -1;
    }
    clock += elapsed;
    ++soundCounter;
    bool tapped = skip;
    skip = false;
    bool resumed = false;
    if (waiting) {
        if (!tapped) {
            return -1;
        }
        tapped = false;
        resumed = true;
        waiting = false;
    }
    if (tapped && inBracket && revealed < bracketEnd) {
        skipping = true;
        waitFrames = 0;
        pauseUntil = 0;
        cursor = std::int32_t(bracketEnd) << 12;
        inBracket = false;
        resumed = true;
    }
    if (waitFrames != 0) {
        --waitFrames;
        return -1;
    }
    if (pauseUntil != 0) {
        if (clock < pauseUntil * kOne) {
            return -1;
        }
        pauseUntil = 0;
    }
    const std::int32_t all = std::int32_t(records.size()) << 12;
    if (!instant) {
        cursor = std::min(all, cursor + std::int32_t((std::int64_t(speed) * elapsed) / kOne));
    } else {
        cursor = all;
        instant = false;
    }
    std::size_t i = revealed, until = std::size_t(cursor >> 12);
    for (; i < until && i < records.size(); ++i) {
        const Record& r = records[i];
        bool stop = false;
        switch (r.type) {
        case Record::Glyph:
            break;
        case Record::Wait:
            waitFrames = 0;
            if (r.value < 0) {
                waiting = true;
            } else {
                waitFrames = r.value;
            }
            stop = true;
            break;
        case Record::Pause:
            pauseUntil = 0;
            if (!skipping) {
                pauseUntil = r.value;
                stop = true;
            }
            break;
        case Record::Open:
            bracketEnd = i + std::size_t(r.value);
            inBracket = true;
            ++until;
            break;
        case Record::Close:
            inBracket = false;
            ++until;
            break;
        }
        if (stop) {
            ++i;
            break;
        }
    }
    revealed = std::min(i, records.size());
    cursor = (cursor & 0xfff) + (std::int32_t(revealed) << 12);
    (void)resumed;
    // The typing sound every typeSoundFrames frames while text appears.
    int sound = -1;
    if (revealed != 0 && !waiting && !skipping && !done() && typeSoundFrames != 0 &&
        soundCounter >= typeSoundFrames) {
        sound = typeSound;
        soundCounter = 0;
    }
    // At the end, a text that asks for a tap waits once for it.
    if (waitAtEnd && revealed >= records.size()) {
        waiting = true;
        waitAtEnd = false;
    }
    if (done()) {
        soundCounter = 10000;
        waitAtEnd = false;
        skipping = false;
    }
    return sound;
}

void Scene::drawMessage(Surface& out, int top, const Message& m) const {
    if (!m.switches.active(Switches::Draw) || m.records.empty()) {
        return;
    }
    const Font& font = messageFont(assets, m);
    const auto& palette = textPalette(assets);
    const auto p = m.position.value(), o = m.offset.value();
    const int x0 = pixel(p[0] + o[0]), y0 = pixel(p[1] + o[1]);
    for (std::size_t r = 0; r < m.revealed && r < m.records.size(); ++r) {
        const auto& placed = m.records[r];
        if (placed.type != Message::Record::Glyph || !placed.glyph) {
            continue;
        }
        const FontGlyph& g = *placed.glyph;
        const std::uint16_t ink =
            std::uint16_t(placed.colour >= 0 && std::size_t(placed.colour) < palette.size()
                              ? palette[std::size_t(placed.colour)] & 0x7fff
                              : 0);
        for (unsigned gy = 0; gy < font.cellHeight; ++gy) {
            for (unsigned gx = 0; gx < g.width; ++gx) {
                if (!g.mask[gy * g.width + gx]) {
                    continue;
                }
                const int px = x0 + placed.x + int(g.left) + int(gx);
                const int py = y0 + placed.y + int(gy);
                const bool cleared =
                    std::any_of(m.erased.begin(), m.erased.end(), [&](const auto& e) {
                        return px >= e[0] && px < e[2] && py >= e[1] && py < e[3];
                    });
                if (px >= 0 && px < 256 && py >= 0 && py < 192 && !cleared) {
                    out.pixels[std::size_t(top + py) * 256 + std::size_t(px)] =
                        std::uint16_t(0x8000 | ink);
                }
            }
        }
    }
}

namespace {
// One picture part an object draws, at a pixel offset from its position.
struct PartAt {
    const Picture* picture;
    const Rect* part;
    int x, y;
};

// The parts of an image (ov6 020a1df4: translucent images draw their cells
// last to first).
void imageParts(std::vector<PartAt>& out, const Animation& a, const Animation::Image& image, int x,
                int y, bool translucent) {
    if (image.picture < 0 || std::size_t(image.picture) >= a.pictures.size()) {
        return;
    }
    const Picture& p = a.pictures[std::size_t(image.picture)];
    auto cell = [&](const Animation::Cell& c) {
        if (c.index < p.parts.size()) {
            out.push_back({&p, &p.parts[c.index], x + c.x, y + c.y});
        }
    };
    if (translucent) {
        for (auto it = image.cells.rbegin(); it != image.cells.rend(); ++it) {
            cell(*it);
        }
    } else {
        for (const auto& c : image.cells) {
            cell(c);
        }
    }
}

// Draws a part through an object's matrix around (ox, oy) (pixels), by
// mapping each screen pixel's centre back into the part (nearest pixel).
void drawTransformed(Surface& out, int top, const PartAt& pa, const std::array<std::int32_t, 4>& m,
                     std::int32_t ox, std::int32_t oy, unsigned alpha) {
    const double a = m[0] / 4096.0, b = m[1] / 4096.0, c = m[2] / 4096.0, d = m[3] / 4096.0;
    const double det = a * d - b * c;
    if (det == 0) {
        return;
    }
    const double wx = ox / 4096.0, wy = oy / 4096.0;
    const Rect& r = *pa.part;
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (int k = 0; k < 4; ++k) {
        const double lx = pa.x + ((k & 1) ? r.width : 0), ly = pa.y + ((k & 2) ? r.height : 0);
        const double sx = wx + a * lx + c * ly, sy = wy + b * lx + d * ly;
        minX = std::min(minX, sx), maxX = std::max(maxX, sx);
        minY = std::min(minY, sy), maxY = std::max(maxY, sy);
    }
    const Picture& p = *pa.picture;
    for (int y = std::max(0, int(std::floor(minY))); y < std::min(192, int(std::ceil(maxY)) + 1);
         ++y) {
        for (int x = std::max(0, int(std::floor(minX)));
             x < std::min(256, int(std::ceil(maxX)) + 1); ++x) {
            const double dx = x + 0.5 - wx, dy = y + 0.5 - wy;
            const double lx = (d * dx - c * dy) / det - pa.x, ly = (a * dy - b * dx) / det - pa.y;
            const int u = int(std::floor(lx)), v = int(std::floor(ly));
            if (u < 0 || v < 0 || u >= r.width || v >= r.height || r.x + u >= int(p.width) ||
                r.y + v >= int(p.height)) {
                continue;
            }
            const std::size_t at = std::size_t(r.y + v) * p.width + std::size_t(r.x + u);
            std::uint16_t colour = p.pixels[at];
            if (!(colour & 0x8000)) {
                continue;
            }
            if (partColour) {
                colour = treat(colour, *partColour);
            }
            unsigned aa = p.alpha.empty() ? 31 : p.alpha[at];
            aa = aa * (alpha + 1) / 32;
            std::uint16_t& dst = out.pixels[std::size_t(top + y) * out.width + std::size_t(x)];
            dst = blend(colour, dst, aa);
        }
    }
}
} // namespace

void Scene::drawRoomPart(Surface& out, int top, const Room::Part& part) const {
    if (part.hidden || part.held || !part.player.resource()) {
        return;
    }
    const std::int32_t a = std::clamp(part.alpha.value(), 0, kOne);
    const unsigned alpha = unsigned((std::int64_t(a) * 0x1ffff) >> 24) & 0xff;
    if (alpha == 0) {
        return;
    }
    const Animation& anim = *part.player.resource()->animation;
    const Animation::Step* step = part.player.step();
    if (!step || step->index >= anim.composites.size()) {
        return;
    }
    std::vector<PartAt> parts;
    for (const auto& cc : anim.composites[step->index].cells) {
        if (cc.index < anim.images.size()) {
            imageParts(parts, anim, anim.images[cc.index], step->dx + cc.x, step->dy + cc.y,
                       alpha < 31);
        }
    }
    for (const auto& pa : parts) {
        drawPart(out, top, *pa.picture, *pa.part, pa.x, pa.y, alpha);
    }
}

void Scene::drawObject(Surface& out, int top, const Chara& c) const {
    if (!c.switches.active(Switches::Draw)) {
        return;
    }
    const std::int32_t a = std::clamp(c.alpha.value(), 0, kOne);
    unsigned alpha = unsigned((std::int64_t(a) * 0x1ffff) >> 24) & 0xff; // 020ad880
    // The screen's blend of its objects over the background (020997f4).
    if (c.screen >= 0 && c.screen < 2) {
        const int eva = (screens[std::size_t(c.screen)].blend.value() * 16 + 0x800) >> 12;
        if (eva < 16) {
            alpha = alpha * unsigned(std::max(eva, 0)) / 16;
        }
    }
    if (alpha == 0) {
        return;
    }
    const bool translucent = alpha < 31;
    std::vector<PartAt> parts;
    Rect whole;
    if (c.visual == Chara::Visual::Image && c.image && c.image->picture) {
        const Picture& p = *c.image->picture;
        whole = Rect{0, 0, int(p.width), int(p.height)};
        parts.push_back({&p, &whole, 0, 0});
    } else if (c.visual == Chara::Visual::Animation && c.animation.resource()) {
        const Animation& anim = *c.animation.resource()->animation;
        if (c.animation.textMode()) {
            for (const auto& g : c.animation.glyphs()) {
                imageParts(parts, anim, *g.image, g.x, g.y, translucent);
            }
        } else if (const Animation::Step* s = c.animation.step();
                   s && s->index < anim.composites.size()) {
            const auto& composite = anim.composites[s->index];
            auto cell = [&](const Animation::Cell& cc) {
                if (cc.index < anim.images.size()) {
                    imageParts(parts, anim, anim.images[cc.index], s->dx + cc.x, s->dy + cc.y,
                               translucent);
                }
            };
            // ov6 020a2ec0: translucent composites draw their cells last to
            // first.
            if (translucent) {
                for (auto it = composite.cells.rbegin(); it != composite.cells.rend(); ++it) {
                    cell(*it);
                }
            } else {
                for (const auto& cc : composite.cells) {
                    cell(cc);
                }
            }
        }
    }
    const auto* res = c.visual == Chara::Visual::Animation ? c.animation.resource() : nullptr;
    partColour = res && !res->colour.identity() ? &res->colour : nullptr;

    struct Reset {
        ~Reset() { partColour = nullptr; }
    } reset;

    // The film part (020a1a24): a box of the film's frame on the texture.
    auto drawFilmPart = [&] {
        const auto& f = c.filmPart;
        if (!f.on || !film.frames || film.frame < 0) {
            return;
        }
        const Surface& frame = film.frames->frame(unsigned(film.frame));
        const auto [x0, y0] = c.pixelPosition();
        for (int y = 0; y < f.h; ++y) {
            for (int x = 0; x < f.w; ++x) {
                const int sx = f.sx + x, sy = f.sy + y, dx = x0 + f.x + x, dy = y0 + f.y + y;
                if (sx < 0 || sy < 0 || sx >= int(frame.width) || sy >= int(frame.height) ||
                    dx < 0 || dy < 0 || dx >= 256 || dy >= 192) {
                    continue;
                }
                out.pixels[std::size_t(top + dy) * 256 + std::size_t(dx)] = std::uint16_t(
                    0x8000 | frame.pixels[std::size_t(sy) * frame.width + std::size_t(sx)]);
            }
        }
    };
    const auto m = c.matrix();
    if (m == std::array<std::int32_t, 4>{kOne, 0, 0, kOne}) {
        const auto [x, y] = c.pixelPosition();
        for (const auto& pa : parts) {
            drawPart(out, top, *pa.picture, *pa.part, x + pa.x, y + pa.y, alpha);
        }
        drawFilmPart();
        return;
    }
    for (const auto& pa : parts) {
        drawTransformed(out, top, pa, m, c.world[0], c.world[1], alpha);
    }
}

namespace {
// The DS 3D engine's scanline fill of an opaque polygon with whole-pixel
// corners (general hardware behaviour; matched pixel for pixel against the
// ROM's lines and dots). Each side of a scanline follows an edge with an
// 18-bit fraction step; flat ("x-major") edges cover a run of pixels per
// scanline. Which edge pixels belong to the polygon depends on the side,
// the slope's direction and whether the edge is flat.
struct EdgeSlope {
    bool right = false;
    int x0 = 0, xmin = 0, xmax = 0;
    bool negative = false, xMajor = false;
    std::int32_t increment = 0, dx = 0;

    int setup(int ax, int bx, int ay, int by, int y) {
        x0 = ax;
        if (bx > ax) {
            xmin = ax;
            xmax = bx - 1;
            negative = false;
        } else if (bx < ax) {
            xmin = bx;
            xmax = ax - 1;
            negative = true;
        } else {
            xmin = ax - (right ? 1 : 0);
            xmax = xmin;
            negative = false;
        }
        const int xlen = xmax + 1 - xmin, ylen = by - ay;
        if (ylen == 0) {
            increment = 0;
        } else if (ylen == xlen) {
            increment = 0x40000;
        } else {
            increment = std::abs((bx - ax) * ((1 << 18) / ylen));
        }
        xMajor = increment > 0x40000;
        if (xMajor) {
            dx = right ? (negative ? 0x60000 : increment - 0x20000)
                       : (negative ? increment - 0x20000 + 0x40000 : 0x20000);
        } else {
            dx = increment != 0 && negative ? 0x40000 : 0;
        }
        dx += (y - ay) * increment;
        return value();
    }

    int value() const {
        const int x = negative ? x0 - (dx >> 18) : x0 + (dx >> 18);
        return std::clamp(x, xmin, xmax);
    }

    int step() {
        dx += increment;
        return value();
    }

    // The pixels the edge covers on this scanline.
    int length() const {
        if (!xMajor) {
            return 1;
        }
        if (right != negative) {
            return (dx >> 18) - ((dx - increment) >> 18);
        }
        return ((dx + increment) >> 18) - (dx >> 18);
    }
};

void fillPolygon(Surface& out, int top, std::span<const std::array<int, 2>> v,
                 std::uint16_t colour) {
    const int n = int(v.size());
    if (n < 3) {
        return;
    }
    int vtop = 0, vbottom = 0;
    long area = 0;
    for (int i = 0; i < n; ++i) {
        const auto& p = v[std::size_t(i)];
        const auto& t = v[std::size_t(vtop)];
        const auto& b = v[std::size_t(vbottom)];
        if (p[1] < t[1] || (p[1] == t[1] && p[0] < t[0])) {
            vtop = i;
        }
        if (p[1] > b[1] || (p[1] == b[1] && p[0] > b[0])) {
            vbottom = i;
        }
        const auto& q = v[std::size_t((i + 1) % n)];
        area += long(p[0]) * q[1] - long(q[0]) * p[1];
    }
    const int ytop = v[std::size_t(vtop)][1], ybottom = v[std::size_t(vbottom)][1];
    if (ytop == ybottom) {
        return;
    }
    // The left side walks the corners forward when they wind one way.
    const int forward = area < 0 ? 1 : -1;
    EdgeSlope left, right;
    right.right = true;

    struct Chain {
        int current, next, step;
        EdgeSlope* slope;
    };

    Chain chains[2] = {{vtop, vtop, forward, &left}, {vtop, vtop, -forward, &right}};
    auto setup = [&](Chain& c, int y) {
        int from = c.next;
        for (;;) {
            const int to = (from + c.step + n) % n;
            if (v[std::size_t(to)][1] > y || from == vbottom) {
                break;
            }
            from = to;
        }
        c.current = from;
        c.next = (from + c.step + n) % n;
        const auto& a = v[std::size_t(c.current)];
        const auto& b = v[std::size_t(c.next)];
        return c.slope->setup(a[0], b[0], a[1], b[1], y);
    };
    int xl = setup(chains[0], ytop), xr = setup(chains[1], ytop);
    for (int y = ytop; y < ybottom; ++y) {
        if (y >= v[std::size_t(chains[0].next)][1] && chains[0].current != vbottom) {
            xl = setup(chains[0], y);
        }
        if (y >= v[std::size_t(chains[1].next)][1] && chains[1].current != vbottom) {
            xr = setup(chains[1], y);
        }
        int xs = xl, xe = xr;
        const EdgeSlope* sl = &left;
        const EdgeSlope* sr = &right;
        const Chain* cl = &chains[0];
        const Chain* cr = &chains[1];
        if (xs > xe) {
            std::swap(xs, xe);
            std::swap(sl, sr);
            std::swap(cl, cr);
        }
        const bool last = y == ybottom - 1;
        const bool split = v[std::size_t(cl->next)][0] != v[std::size_t(cr->next)][0];
        const int leftLength = sl->length(), rightLength = sr->length();
        const bool fillLeft = sl->negative || !sl->xMajor || (last && sl->xMajor && split);
        const bool fillRight = (!sr->negative && sr->xMajor) ||
                               (!(sr->negative && sr->xMajor) && sr->increment == 0) ||
                               (last && sr->xMajor && split);
        auto plot = [&](int x) {
            if (x >= 0 && x < 256 && y >= 0 && y < 192) {
                out.pixels[std::size_t(top + y) * 256 + std::size_t(x)] =
                    std::uint16_t(colour | 0x8000);
            }
        };
        int x = xs;
        int limit = std::min(xs + leftLength, xe + 1);
        if (!fillLeft) {
            x = std::min(limit, xe - rightLength + 1);
        } else {
            for (; x < limit; ++x) {
                plot(x);
            }
        }
        for (limit = std::min(xe - rightLength + 1, xe + 1); x < limit; ++x) {
            plot(x);
        }
        if (fillRight) {
            for (; x <= xe; ++x) {
                plot(x);
            }
        }
        xl = left.step();
        xr = right.step();
    }
}

// 20.12 to whole pixels, rounded down (the 3D engine's screen positions).
int floorPixel(std::int64_t v) {
    return int(v >= 0 ? v / kOne : -((-v + kOne - 1) / kOne));
}

// a * b in 20.12, toward zero (fx multiplies).
std::int32_t mulFx(std::int64_t a, std::int64_t b) {
    return std::int32_t((a * b) / kOne);
}

// 0200a768: 1 / sqrt(v) in 20.12 through the divider and square root units.
std::int32_t inverseSqrtFx(std::int32_t v) {
    if (v <= 0) {
        return 0;
    }
    const std::uint64_t q = (std::uint64_t(1) << 44) / std::uint64_t(v);
    const std::uint64_t n = std::uint64_t(v) << 32;
    auto s = std::uint64_t(std::sqrt(double(n)));
    while (s * s > n) {
        --s;
    }
    while ((s + 1) * (s + 1) <= n) {
        ++s;
    }
    const auto high = std::uint32_t((q * s) >> 32);
    return std::int32_t(high + 0x200) >> 10;
}
} // namespace

void Scene::drawLoopShape(Surface& out, int top, const LoopShape& shape) const {
    using Corner = std::array<int, 2>;
    if (shape.dot) {
        // 020a0460: a square from the corner, the size across and down.
        const int x = floorPixel(shape.a[0]), y = floorPixel(shape.a[1]);
        const int x2 = floorPixel(std::int64_t(shape.a[0]) + shape.b[0]);
        const int y2 = floorPixel(std::int64_t(shape.a[1]) + shape.b[1]);
        const std::array<Corner, 4> v{{{x, y}, {x2, y}, {x2, y2}, {x, y2}}};
        fillPolygon(out, top, v, shape.colour);
        return;
    }
    // 0209fb20: a quad reaching half the width past both ends and half the
    // width to each side, shifted half a pixel; a line of no length is a
    // square of the width.
    const std::int32_t half = mulFx(0x800, std::abs(shape.width));
    const auto& a = shape.a;
    const auto& b = shape.b;
    if (a == b) {
        const std::array<Corner, 4> v{{
            {floorPixel(a[0] - half + 0x800), floorPixel(a[1] - half + 0x800)},
            {floorPixel(a[0] + half + 0x800), floorPixel(a[1] - half + 0x800)},
            {floorPixel(a[0] + half + 0x800), floorPixel(a[1] + half + 0x800)},
            {floorPixel(a[0] - half + 0x800), floorPixel(a[1] + half + 0x800)},
        }};
        fillPolygon(out, top, v, shape.colour);
        return;
    }
    const std::int64_t dx = std::int64_t(b[0]) - a[0], dy = std::int64_t(b[1]) - a[1];
    const auto squared = std::int32_t((dx * dx + dy * dy) / kOne);
    const std::int32_t inverse = inverseSqrtFx(squared);
    const std::int32_t ux = mulFx(mulFx(dx, inverse), half), uy = mulFx(mulFx(dy, inverse), half);
    const std::int32_t vx = -uy, vy = ux;
    const std::array<Corner, 4> v{{
        {floorPixel(std::int64_t(a[0]) + 0x800 - ux - vx),
         floorPixel(std::int64_t(a[1]) + 0x800 - uy - vy)},
        {floorPixel(std::int64_t(a[0]) + 0x800 - ux + vx),
         floorPixel(std::int64_t(a[1]) + 0x800 - uy + vy)},
        {floorPixel(std::int64_t(b[0]) + 0x800 + ux + vx),
         floorPixel(std::int64_t(b[1]) + 0x800 + uy + vy)},
        {floorPixel(std::int64_t(b[0]) + 0x800 + ux - vx),
         floorPixel(std::int64_t(b[1]) + 0x800 + uy - vy)},
    }};
    fillPolygon(out, top, v, shape.colour);
}

void Scene::drawLoopSprite(Surface& out, int top, const LoopSprite& sprite) const {
    // ov6 020a2be8: the sequence's first step, its composite at the position
    // plus the step's offset.
    if (!sprite.sequence || sprite.sequence->steps.empty() || sprite.alpha == 0) {
        return;
    }
    const Animation& anim = *sprite.animation->animation;
    const Animation::Step& step = sprite.step ? *sprite.step : sprite.sequence->steps.front();
    if (step.index >= anim.composites.size()) {
        return;
    }
    const bool translucent = sprite.alpha < 31;
    std::vector<PartAt> parts;
    const auto& composite = anim.composites[step.index];
    auto cell = [&](const Animation::Cell& cc) {
        if (cc.index < anim.images.size()) {
            imageParts(parts, anim, anim.images[cc.index], step.dx + cc.x, step.dy + cc.y,
                       translucent);
        }
    };
    if (translucent) {
        for (auto it = composite.cells.rbegin(); it != composite.cells.rend(); ++it) {
            cell(*it);
        }
    } else {
        for (const auto& cc : composite.cells) {
            cell(cc);
        }
    }
    if (sprite.mirror & 3) {
        const std::array<std::int32_t, 4> m{(sprite.mirror & 1) ? -kOne : kOne, 0, 0,
                                            (sprite.mirror & 2) ? -kOne : kOne};
        for (const auto& pa : parts) {
            drawTransformed(out, top, pa, m, sprite.position[0], sprite.position[1], sprite.alpha);
        }
        return;
    }
    const int x = pixel(sprite.position[0]), y = pixel(sprite.position[1]);
    for (const auto& pa : parts) {
        drawPart(out, top, *pa.picture, *pa.part, x + pa.x, y + pa.y, sprite.alpha, sprite.colour);
    }
}

void Scene::drawLoopMesh(Surface& out, int top, const LoopMesh& mesh) const {
    // Each quad of the strips is filled like the hardware's polygons: the
    // corners on whole pixels, the texture coordinates interpolated along
    // the left and right edges and across each row, the texel blended with
    // the polygon's alpha.
    const Picture* p = mesh.image ? mesh.image->picture : nullptr;
    if (!p || p->width == 0 || mesh.columns < 2 || mesh.alpha == 0) {
        return;
    }

    struct Corner {
        int x, y;
        float u, v;
    };

    auto corner = [&](std::size_t i) {
        const auto& c = mesh.corners[i];
        return Corner{floorPixel(c[0]), floorPixel(c[1]), float(c[2]) / kOne, float(c[3]) / kOne};
    };
    const std::size_t across = std::size_t(mesh.columns);
    const std::size_t rows = mesh.corners.size() / across;
    for (std::size_t r = 0; r + 1 < rows; ++r) {
        for (std::size_t c = 0; c + 1 < across; ++c) {
            const std::array<Corner, 4> q{corner(r * across + c), corner(r * across + c + 1),
                                          corner((r + 1) * across + c + 1),
                                          corner((r + 1) * across + c)};
            int ytop = q[0].y, ybottom = q[0].y;
            for (const Corner& k : q) {
                ytop = std::min(ytop, k.y);
                ybottom = std::max(ybottom, k.y);
            }
            for (int y = std::max(ytop, 0); y < std::min(ybottom, 192); ++y) {
                // Where the row crosses the edges: the leftmost and the
                // rightmost crossing.
                bool any = false;
                Corner left{}, right{};
                for (std::size_t e = 0; e < 4; ++e) {
                    const Corner& a = q[e];
                    const Corner& b = q[(e + 1) % 4];
                    if (a.y == b.y || y < std::min(a.y, b.y) || y >= std::max(a.y, b.y)) {
                        continue;
                    }
                    const float t = float(y - a.y) / float(b.y - a.y);
                    const Corner at{int(std::lround(a.x + t * float(b.x - a.x))), y,
                                    a.u + t * (b.u - a.u), a.v + t * (b.v - a.v)};
                    if (!any || at.x < left.x) {
                        left = at;
                    }
                    if (!any || at.x > right.x) {
                        right = at;
                    }
                    any = true;
                }
                if (!any || right.x <= left.x) {
                    continue;
                }
                for (int x = std::max(left.x, 0); x < std::min(right.x, 256); ++x) {
                    const float t = float(x - left.x) / float(right.x - left.x);
                    const int u =
                        std::clamp(int(left.u + t * (right.u - left.u)), 0, int(p->width) - 1);
                    const int v =
                        std::clamp(int(left.v + t * (right.v - left.v)), 0, int(p->height) - 1);
                    const std::size_t at = std::size_t(v) * p->width + std::size_t(u);
                    const std::uint16_t texel = modulate(p->pixels[at], mesh.colour);
                    if (!(texel & 0x8000)) {
                        continue;
                    }
                    unsigned a = p->alpha.empty() ? 31 : p->alpha[at];
                    a = a * (mesh.alpha + 1) / 32;
                    std::uint16_t& d = out.pixels[std::size_t(top + y) * 256 + std::size_t(x)];
                    d = blend(texel, d, a);
                }
            }
        }
    }
}

void Scene::render(Surface& out) const {
    out.width = 256;
    out.height = 384;
    out.pixels.assign(256u * 384u, 0x8000);
    for (int s = 0; s < 2; ++s) {
        const int top = s == upper ? 0 : 192;
        const Screen& screen = screens[std::size_t(s)];
        for (int y = 0; y < 192; ++y) {
            for (int x = 0; x < 256; ++x) {
                out.pixels[std::size_t(top + y) * 256 + std::size_t(x)] =
                    std::uint16_t(0x8000 | screen.backdrop);
            }
        }
        if (screen.background) {
            drawPart(out, top, *screen.background,
                     Rect{0, 0, int(screen.background->width), int(screen.background->height)}, 0,
                     0, 31);
        }
        // The room's parts on its screen (after the picture).
        if (s == 0) {
            for (const auto& part : room.parts) {
                drawRoomPart(out, top, *part);
            }
        }
        // The film on the main engine's screen (ov10 draws it in 3D over
        // the background).
        if (s == 0 && film.frames && film.frame >= 0 && film.state >= 3 && !film.flag) {
            const Surface& picture = film.frames->frame(unsigned(film.frame));
            for (int y = 0; y < 192 && y < int(picture.height); ++y) {
                for (int x = 0; x < 256 && x < int(picture.width); ++x) {
                    out.pixels[std::size_t(top + y) * 256 + std::size_t(x)] = std::uint16_t(
                        0x8000 | picture.pixels[std::size_t(y) * picture.width + std::size_t(x)]);
                }
            }
        }
        for (const LoopMesh& mesh : loopMeshes) {
            if (mesh.image && mesh.image->screen == s) {
                drawLoopMesh(out, top, mesh);
            }
        }
        for (const LoopSprite& sprite : loopSprites) {
            if (sprite.animation && sprite.animation->screen == s) {
                drawLoopSprite(out, top, sprite);
            }
        }
        if (s == 0) {
            for (const LoopShape& shape : loopShapes) {
                drawLoopShape(out, top, shape);
            }
        }
        for (const Chara* c : drawOrder_[std::size_t(s)]) {
            drawObject(out, top, *c);
        }
        // The paints' drawing layer (order against the objects:
        // hypothesis).
        for (std::size_t i = 0; i < screen.drawing.size(); ++i) {
            if (screen.drawing[i] & 0x8000) {
                out.pixels[std::size_t(top) * 256 + i] = screen.drawing[i];
            }
        }
        for (const auto& [h, m] : messages_) {
            if (m->screen == s) {
                drawMessage(out, top, *m);
            }
        }
        // The shake moves the whole picture.
        if (screen.shaking[0] || screen.shaking[1]) {
            std::vector<std::uint16_t> copy(out.pixels.begin() + std::ptrdiff_t(top) * 256,
                                            out.pixels.begin() + std::ptrdiff_t(top + 192) * 256);
            for (int y = 0; y < 192; ++y) {
                for (int x = 0; x < 256; ++x) {
                    const int sx = x - screen.shaking[0], sy = y - screen.shaking[1];
                    out.pixels[std::size_t(top + y) * 256 + std::size_t(x)] =
                        sx >= 0 && sx < 256 && sy >= 0 && sy < 192
                            ? copy[std::size_t(sy) * 256 + std::size_t(sx)]
                            : std::uint16_t(0x8000 | screen.backdrop);
                }
            }
        }
        // The colour treatment (*hypothesis*: on everything the screen
        // shows; the game applies its matrix to the screen's palettes).
        if (!screen.colour.identity()) {
            for (int y = 0; y < 192; ++y) {
                for (int x = 0; x < 256; ++x) {
                    std::uint16_t& p = out.pixels[std::size_t(top + y) * 256 + std::size_t(x)];
                    p = treat(std::uint16_t(p | 0x8000), screen.colour);
                }
            }
        }
        // Master brightness: 16 steps toward white or black.
        const std::int32_t b = screen.brightness.value();
        const int level = (b * 16) / kOne;
        if (level != 0) {
            for (int y = 0; y < 192; ++y) {
                for (int x = 0; x < 256; ++x) {
                    std::uint16_t& p = out.pixels[std::size_t(top + y) * 256 + std::size_t(x)];
                    auto channel = [&](int shift) -> unsigned {
                        const int v = (p >> shift) & 31;
                        const int n =
                            level > 0 ? v + ((31 - v) * level) / 16 : v - (v * -level) / 16;
                        return unsigned(n) << shift;
                    };
                    p = std::uint16_t(0x8000 | channel(0) | channel(5) | channel(10));
                }
            }
        }
    }
}

} // namespace picarat::adventure4
