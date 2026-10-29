// The rabbit's hub (loop 6000, lt5CLoopMiniRabbit; creator 0x37db48,
// vtable 0x402488: start 0x22ccd4, end 0x22d500, commands 0x22d544, frame
// end 0x22ca18). It holds what the other rabbit loops share: the tricks,
// their motions and the shows (res/sub/lt5_sub_rabbit_common.xs, _nazo.xs),
// the rabbit "usa" with its motion machine, and the protagonist's hand. Its
// script (seq/mini_rabbit_main) is empty: once the rabbit is made, its frame
// end puts the loop its value 0 names on top (6002..6005, else 6001
// Commu), handing it the hub (command 200; the original's value is the
// hub's address). The loops on top forward the commands they do not take
// to the hub. The rabbit stands, walks to a point, plays a motion once or
// turns (0x22dc9c); motions that move it carry their movement in the 8301
// table: between two points of the motion's progress the rabbit is moved
// and turned by tweens lasting that stretch.
// Docs: .analysis/adventure56/docs/rabbit.md.
#include "story/a5_rabbit_parts.hpp"
#include "graphics/a56_model.hpp"
#include "graphics/a56_text.hpp"
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace picarat::adventure5::rabbit {

const std::string kChosenRabbit =
    "\x81\x83\x83\x45\x83\x54\x83\x4d\x81\x84\x91\x49\x82\xf1\x82\xbe\x83\x45\x83\x54\x83\x4d";
const std::string kStandMotion = "001_\x97\xa7\x82\xbf";
// The port's object for the rabbit's shadow instance (not a game object).
const char* const kShadowObject = "#usa_shadow";

namespace {

constexpr float kTwoPi = 6.28318548f; // 0x40c90fdb
constexpr float kDegree = 0.0174533f; // 0x3c8efa36
// The motions that end a played motion when they show (0x22e918 ..):
// 058_二本足で立つ_LOOP, 069_逆立ち_LOOP (with the standing one).
const std::string kTwoFeetLoop =
    "058_\x93\xf1\x96\x7b\x91\xab\x82\xc5\x97\xa7\x82\xc2_LOOP";
const std::string kHandstandLoop = "069_\x8b\x74\x97\xa7\x82\xbf_LOOP";
// The turning motions of state 4 (0x22df64 / 0x22df74).
const std::string kTurnRight = "041_\x90\x55\x82\xe8\x8c\xfc\x82\xad\x82\x71";
const std::string kTurnLeft = "042_\x90\x55\x82\xe8\x8c\xfc\x82\xad\x82\x6b";

// 0x28b23c + 0x2b8394: a vector turned about the vertical axis.
std::array<float, 3> turned(float yaw, const std::array<float, 3>& v) {
    const float c = std::cos(yaw), s = std::sin(yaw);
    return {v[0] * c + v[2] * s, v[1], -v[0] * s + v[2] * c};
}

// 0x2c00dc + 0x2be1d0: the heading of a direction across the floor (0 when
// it has no length there).
float heading(float x, float z) {
    if (x == 0 && z == 0) {
        return 0;
    }
    return adventure56::wrapAngle(std::atan2(x, z));
}

} // namespace

std::string format(const char* f, ...) {
    char s[64];
    va_list a;
    va_start(a, f);
    std::vsnprintf(s, sizeof s, f, a);
    va_end(a);
    return s;
}

// ------------------------------------------------------------ the record

int trickState(const Game& g, int slot) {
    if (slot < 0 || slot >= 32) {
        return 0;
    }
    if (g.rabbit.mastered >> slot & 1) {
        return 2;
    }
    return g.rabbit.learned >> slot & 1 ? 1 : 0;
}

void addTrickState(Game& g, int slot, int state) {
    if (slot < 0 || slot >= 32) {
        return;
    }
    if (state == 2) {
        g.rabbit.mastered |= 1u << slot;
    } else if (state == 1) {
        g.rabbit.learned |= 1u << slot;
    }
}

void setFresh(Game& g, int slot, bool on) {
    if (slot < 0 || slot >= 32) {
        return;
    }
    g.rabbit.fresh = on ? g.rabbit.fresh | 1u << slot : g.rabbit.fresh & ~(1u << slot);
}

bool fresh(const Game& g, int slot) { return slot >= 0 && slot < 32 && (g.rabbit.fresh >> slot & 1); }

int variant(const Game& g, int slot) {
    return slot >= 0 && slot < 32 ? g.rabbit.variants[std::size_t(slot)] : 0;
}

void setVariant(Game& g, int slot, int v) {
    if (slot >= 0 && slot < 32) {
        g.rabbit.variants[std::size_t(slot)] = std::uint8_t(v);
    }
}

bool rabbitFlag(const Game& g, int n) {
    return n >= 0 && n < 128 && (g.rabbit.flags[std::size_t(n >> 3)] >> (n & 7) & 1);
}

bool setRabbitFlag(Game& g, int n, bool on) {
    if (n < 0 || n >= 128) {
        return false;
    }
    auto& b = g.rabbit.flags[std::size_t(n >> 3)];
    b = std::uint8_t(on ? b | 1 << (n & 7) : b & ~(1 << (n & 7)));
    return true;
}

// ------------------------------------------------------------------ 6000

const Trick* RabbitHub::trick(int id) const {
    for (const auto& t : tricks_) {
        if (t.id == id) {
            return &t;
        }
    }
    return nullptr;
}

const Show* RabbitHub::show(int index) const {
    for (const auto& s : shows_) {
        if (s.index == index) {
            return &s;
        }
    }
    return nullptr;
}

const TrickMotion* RabbitHub::trickMotion(const std::string& description) const {
    for (const auto& m : trickMotions_) {
        if (m.description == description) {
            return &m;
        }
    }
    return nullptr;
}

const TrickMotion* RabbitHub::trickMotionNamed(const std::string& motion) const {
    for (const auto& m : trickMotions_) {
        if (m.motion == motion) {
            return &m;
        }
    }
    return nullptr;
}

std::array<float, 3> RabbitHub::motionMove(const std::string& motion) const {
    std::array<float, 3> sum{};
    const auto it = stretches_.find(motion);
    if (it != stretches_.end()) {
        for (const auto& s : it->second) {
            for (std::size_t k = 0; k < 3; ++k) {
                sum[k] += s.move[k];
            }
        }
    }
    return sum;
}

void RabbitHub::loadTables(const std::string& path) {
    // 0x22f324 over the list (the parsers by opcode, RAM 0x443ff4).
    Show* open = nullptr;
    for (const auto& c : readDataList(game_, path)) {
        switch (c.opcode) {
        case 8201: { // {id, name, learned, mastered, slot, kind, description, text}
            Trick t;
            t.id = c.number(0);
            t.name = c.text(1);
            t.learned = c.number(2) != 0;
            t.mastered = c.number(3) != 0;
            t.slot = c.number(4);
            t.kind = c.number(5);
            t.description = c.text(6);
            t.text = c.number(7);
            tricks_.push_back(std::move(t));
            break;
        }
        case 8221: // {id, description, motion}
            trickMotions_.push_back({c.number(0), c.text(1), c.text(2)});
            break;
        case 8301: // {motion, from, to, x, y, z, angle}
            stretches_[c.text(0)].push_back(
                {c.real(1), c.real(2), {c.real(3), c.real(4), c.real(5)}, c.real(6)});
            break;
        case 8231: { // {index, name, slots, needed, steps}
            Show s;
            s.index = c.number(0);
            s.name = c.text(1);
            s.slots = c.number(2);
            s.needed = c.number(3);
            shows_.push_back(std::move(s));
            open = &shows_.back();
            break;
        }
        case 8232: // {slot (from 1; 0x1271bc keeps it from 0), second, trick}
            if (open) {
                open->steps.push_back({c.number(0) - 1, c.number(1), c.number(2)});
            }
            break;
        case 8233:
            open = nullptr;
            break;
        default: // 8200 / 8220 / 8230 / 8300 counts, 8202 / 8222 / 8302 ends
            break;
        }
    }
}

std::shared_ptr<adventure56::Model> RabbitHub::model(const std::string& path) {
    const auto folder = game_.pack.find(path, ".xc");
    return folder.empty() ? nullptr : adventure56::loadModel(game_.pack, folder);
}

void RabbitHub::start() {
    ScriptLoop::start(); // seq/mini_rabbit_main
    loadTables("res/sub/lt5_sub_rabbit_common");
    loadTables("res/sub/lt5_sub_rabbit_nazo");
    // Two display frames per game frame (0x22cd08 -> 0x2d3af8 with 2): the
    // rabbit's loops run at 30 Hz, so a held touch gathers one gesture per
    // two display frames (the head's sleep needs over 60 of them).
    game_.displayFramesPerFrame = 2;
    standMotion_ = kStandMotion; // +0x2320
    // The chosen rabbit's model and the protagonist's hand: ra102 / ra302
    // for rabbit 2, else ra101 / ra301; the motions (ra101mot) and the
    // shadow (ra201).
    const bool second = game_.variable(kChosenRabbit) == 2;
    rabbit_ = model(second ? "mini/ra102" : "mini/ra101");
    hand_ = model(second ? "mini/ra302" : "mini/ra301");
    motions_ = model("mini/ra101mot");
    shadow_ = model("mini/ra201");
    if (rabbit_ && motions_) {
        rabbit_->attachMotions(motions_, 0); // 0x2c9c98
    }
    // "usa" (3D, top screen, priority 0x82) shows the rabbit standing,
    // drawn by its own model component (0x2ae750, vtable 0x40401c). Its
    // draw (+0x40, 0x22c8e0) first draws the shadow instance (+0x1a1c,
    // ra201, linked at +0x86c) at the object's matrix times the posed
    // matrix of the bone "kage" (+0x868, 0x2b8458), then the rabbit. The
    // port gives the shadow its own object under "usa" at that bone, made
    // first so it draws first. *Not modelled*: the fur look in two
    // colourings by the rabbit (0x22bf54) and the stylus trail over the fur
    // (0x22c998).
    if (rabbit_ && shadow_) {
        Object& kage = game_.scene.create3D(kShadowObject, 0, 0x82, 0);
        kage.model = shadow_;
        kage.parent = xq::hash("usa");
        kage.parentBone = xq::hash(game_.codeText(0x22d4e0)); // "kage"
    }
    if (rabbit_) {
        Object& usa = game_.scene.create3D("usa", 0, 0x82, 0);
        usa.model = rabbit_;
        usa.motion = standMotion_;
        rabbitMade_ = true;
    }
    // The protagonist's hand (priority 0x82), hidden (vtable +0xc).
    handName_ = game_.codeText(0x22d4e8);
    if (hand_) {
        Object& hand = game_.scene.create3D(handName_, 0, 0x82, 0);
        hand.model = hand_;
        hand.visible = false;
    }
    // The tricks known from the start: a trick with no state yet is
    // learned (and marked new) or mastered as its record says.
    for (const auto& t : tricks_) {
        if (trickState(game_, t.slot) != 0) {
            continue;
        }
        if (t.learned) {
            setFresh(game_, t.slot, true);
            addTrickState(game_, t.slot, 1);
        }
        if (t.mastered) {
            addTrickState(game_, t.slot, 2);
        }
    }
}

void RabbitHub::end() {
    // 0x22d500: "usa" goes, then the base end.
    game_.scene.remove3D(xq::hash("usa"));
    game_.scene.remove3D(xq::hash(kShadowObject));
    ScriptLoop::end();
}

void RabbitHub::afterFrame() {
    // 0x22ca18: the base frame end, then (the rabbit made, no request
    // waiting) the loop of value 0 on top.
    ScriptLoop::afterFrame();
    frameEnded_ = true;
    // The shadow is drawn with the rabbit (the component's draw).
    if (Object* kage = game_.scene.object3D(xq::hash(kShadowObject))) {
        const Object* usa = game_.scene.object3D(xq::hash("usa"));
        kage->visible = usa && usa->visible;
        kage->drawn = usa && usa->drawn;
    }
    if (!rabbitMade_ || story_.pending()) {
        return;
    }
    const int v = values[0].toInt();
    const int next = v >= RabbitNazoLoopId && v <= RabbitNoteLoopId + 1 ? v : RabbitCommuLoopId;
    story_.push(next, {Value::ofInt(kHandle)});
}

// ---- the motion machine

const MotionStretch* RabbitHub::stretchAt(const std::string& motion, float t) const {
    const auto it = stretches_.find(motion);
    if (it == stretches_.end()) {
        return nullptr;
    }
    for (const auto& s : it->second) {
        if (s.from <= t && t < s.to) {
            return &s;
        }
    }
    return nullptr;
}

void RabbitHub::playMotion(Object& usa, const std::string& motion, unsigned flags) {
    // 0x2936c0 (the blend frames it takes are not modelled). *Hypothesis*:
    // asking for the motion already playing keeps it going unless flag 4
    // restarts it.
    if (usa.motion != motion || (flags & 4)) {
        usa.motionTime = 0;
        usa.motionDone = false;
    }
    usa.motion = motion;
    usa.motionStops = false;
    usa.motionOnce = (flags & 8) != 0;
}

float RabbitHub::progress(const Object& usa) const {
    // 0x340958: (time - start) / length of the layer's motion.
    const adventure56::Motion* m = usa.model ? usa.model->motion(usa.motion) : nullptr;
    if (!m) {
        return 0;
    }
    return usa.motionTime / float(std::max(1, m->last - m->first + 1));
}

float RabbitHub::stretchFrames(const Object& usa, const MotionStretch& s) const {
    // (to - from) x length (0x340a70) / the layer's rate (0x280958).
    const adventure56::Motion* m = usa.model ? usa.model->motion(usa.motion) : nullptr;
    if (!m) {
        return 0;
    }
    const float rate = usa.motionSpeed * m->rate;
    return rate > 0 ? (s.to - s.from) * float(std::max(1, m->last - m->first + 1)) / rate : 0;
}

void RabbitHub::settleRotation(Object& usa) {
    auto r = usa.rotation3.value();
    for (float& a : r) {
        a = adventure56::wrapAngle(a);
    }
    usa.rotation3.set(r);
    usa.rotation3Offset.set({0, 0, 0});
}

void RabbitHub::walkTo(const std::string& motion, const std::array<float, 3>& to) {
    if (motion.empty()) {
        return;
    }
    motion_ = motion;
    target_ = to;
    state_ = 2;
    restart_ = true;
}

void RabbitHub::playOnce(const std::string& motion, float angle) {
    if (motion.empty()) {
        return;
    }
    motion_ = motion;
    angle_ = angle;
    state_ = 3;
}

void RabbitHub::enterWalk(Object& usa) {
    // 0x22e2c4: the walking motion, facing the target over 15 frames, and
    // a loop's stride and rise fitted so that whole loops end on the target
    // (the motion's first stretch's forward move as the stride).
    playMotion(usa, motion_);
    settleRotation(usa);
    const auto pos = usa.position.value();
    const std::array<float, 3> d{target_[0] - pos[0], target_[1] - pos[1], target_[2] - pos[2]};
    usa.rotation3.start({0, heading(d[0], d[2]), 0}, 15, 2);
    const auto it = stretches_.find(motion_);
    if (it == stretches_.end() || it->second.empty()) {
        return;
    }
    const float distance = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const float loops = distance / it->second.front().move[2];
    const auto whole = loops > 0 ? std::uint32_t(loops) : 0u;
    if (loops - float(whole) < 0.5f) {
        if (whole == 0) {
            step_[1] = d[1];
            step_[2] = distance;
        } else {
            step_[1] = d[1] / float(whole);
            step_[2] = distance / float(whole);
        }
    } else {
        step_[1] = d[1] / (float(whole) + 1);
        step_[2] = distance / (float(whole) + 1);
    }
}

void RabbitHub::stepWalk(Object& usa) {
    // 0x22e95c: within one unit of the target it glides there over 15
    // frames and stands; otherwise each stretch of the walking motion moves
    // it a loop's step towards the target, over the stretch's frames.
    const std::string current = usa.motion;
    const float t = progress(usa);
    if (usa.position.moving() || usa.offset.moving()) {
        return;
    }
    const auto pos = usa.position.value();
    const std::array<float, 3> d{target_[0] - pos[0], target_[1] - pos[1], target_[2] - pos[2]};
    const float distance = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (distance < 1) {
        usa.position.start(target_, 15, 2);
        playMotion(usa, standMotion_);
        state_ = 1;
        return;
    }
    if (const MotionStretch* s = stretchAt(current, t)) {
        const float frames = stretchFrames(usa, *s);
        const auto move = turned(heading(d[0], d[2]), step_);
        usa.position.start({pos[0] + move[0], pos[1] + move[1], pos[2] + move[2]}, frames, 0);
    }
    lastProgress_ = t;
}

void RabbitHub::stepPlay(Object& usa) {
    // 0x22e6b8: done when the motion has run to its end or a standing
    // motion shows; each of its stretches moves and turns the rabbit over
    // the stretch's frames (+1).
    const std::string current = usa.motion;
    const float t = usa.motion == motion_ ? progress(usa) : 0.0f;
    if (usa.motionDone || current == kStandMotion || current == kTwoFeetLoop ||
        current == kHandstandLoop) {
        state_ = 1;
        return;
    }
    const MotionStretch* s = stretchAt(motion_, t);
    if (!s) {
        return;
    }
    const auto pos = usa.position.value();
    const float turn = usa.rotation3Offset.value()[1];
    const float frames = stretchFrames(usa, *s) + 1;
    const auto move = turned(usa.rotation3.value()[1], s->move);
    if (!usa.position.moving() && !usa.offset.moving()) {
        usa.position.start({pos[0] + move[0], pos[1] + move[1], pos[2] + move[2]}, frames, 0);
    }
    if (!usa.rotation3.moving() && !usa.rotation3Offset.moving()) {
        usa.rotation3Offset.start({0, turn + s->angle * kDegree, 0}, frames, 2);
    }
}

void RabbitHub::stepTurn(Object& usa) {
    // 0x22e584: done at the motion's end or when the standing motion
    // shows; the turning motion's stretch turns the rabbit to the angle.
    if (usa.motionDone || usa.motion == kStandMotion) {
        state_ = 1;
        return;
    }
    if (usa.rotation3.moving()) {
        return;
    }
    if (const MotionStretch* s = stretchAt(usa.motion, progress(usa))) {
        usa.rotation3.start({0, angle_, 0}, stretchFrames(usa, *s), 2);
    }
}

void RabbitHub::stepMotion() {
    Object* usa = game_.scene.object3D(xq::hash("usa"));
    if (!usa) {
        return;
    }
    if (state_ != previous_ || restart_) {
        switch (state_) {
        case 1:
            settleRotation(*usa);
            break;
        case 2:
            enterWalk(*usa);
            break;
        case 3:
            playMotion(*usa, motion_);
            settleRotation(*usa);
            if (angle_ < kTwoPi) {
                usa->rotation3.start({0, angle_, 0}, 15, 2);
            }
            break;
        case 4: {
            // The way to turn, wrapped into [-pi, pi); within 0.1 it stands.
            float d = angle_ - usa->rotation3.value()[1];
            d = adventure56::wrapAngle(d);
            if (std::fabs(d) < 0.1f) {
                state_ = 1;
            } else {
                playMotion(*usa, d < 0 ? kTurnRight : kTurnLeft);
            }
            break;
        }
        default:
            break;
        }
    }
    const int before = state_;
    switch (state_) {
    case 1:
        playMotion(*usa, standMotion_);
        break;
    case 2:
        stepWalk(*usa);
        break;
    case 3:
        stepPlay(*usa);
        break;
    case 4:
        stepTurn(*usa);
        break;
    default:
        break;
    }
    restart_ = state_ != before;
    previous_ = state_;
}

void RabbitHub::showHand(bool on) {
    if (Object* hand = game_.scene.object3D(xq::hash(handName_))) {
        hand->visible = on;
    }
}

void RabbitHub::moveHand(const std::array<float, 3>& at) {
    if (Object* hand = game_.scene.object3D(xq::hash(handName_))) {
        hand->position.set(at);
    }
}

void RabbitHub::turnHand(const std::array<float, 3>& n) {
    // 0x22ec00: the hand's axes from the surface normal - x = up x n
    // (normalized), y = n, z = -(n x x) - read as angles: y = asin(z.x),
    // x = -atan2(z.y, z.z), z = -atan2(n.x, x.x) (with z.y and z.z both
    // near 0: x = 0, z = atan2(x.y, n.y)); set at once.
    Object* hand = game_.scene.object3D(xq::hash(handName_));
    if (!hand) {
        return;
    }
    std::array<float, 3> x{n[2], 0, -n[0]}; // (0, 1, 0) x n
    const float xl = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
    if (xl > 0) {
        for (float& c : x) {
            c /= xl;
        }
    }
    const std::array<float, 3> z{-(n[1] * x[2] - n[2] * x[1]), -(n[2] * x[0] - n[0] * x[2]),
                                 -(n[0] * x[1] - n[1] * x[0])};
    const float ry = std::asin(std::clamp(z[0], -1.0f, 1.0f));
    std::array<float, 3> r{};
    if (std::fabs(z[1]) < 1e-5f && std::fabs(z[2]) < 1e-5f) {
        r = {0, ry, (x[1] == 0 && n[1] == 0) ? 0.0f : std::atan2(x[1], n[1])};
    } else {
        r = {-std::atan2(z[1], z[2]), ry, -std::atan2(n[0], x[0])};
    }
    hand->rotation3.set(r);
}

bool RabbitHub::command(int number, xq::Call& c, std::size_t first) {
    const std::size_t n = c.count() > first ? c.count() - first : 0;
    auto res = [&c](std::int32_t v) {
        c.result = Value::ofInt(v);
        return true;
    };
    auto put = [&c](std::size_t i, Value v) {
        if (Value* r = i < c.count() ? c.reg(i) : nullptr) {
            *r = std::move(v);
        }
    };
    auto text = [&c](std::size_t i) {
        const std::string* t = c.text(i);
        return t ? *t : std::string();
    };
    Object* usa = game_.scene.object3D(xq::hash("usa"));
    switch (number) {
    case 100: { // {motion, progress, -> from, to, x, y, z, angle}: its stretch there
        if (n < 8) {
            return false;
        }
        const auto it = stretches_.find(text(first));
        const float t = c.real(first + 1);
        if (it != stretches_.end()) {
            for (const auto& s : it->second) {
                if (s.from <= t && t <= s.to) {
                    const float v[6] = {s.from, s.to, s.move[0], s.move[1], s.move[2], s.angle};
                    for (std::size_t i = 0; i < 6; ++i) {
                        put(first + 2 + i, Value::ofFloat(v[i]));
                    }
                    return res(1);
                }
            }
        }
        return res(0);
    }
    case 110: { // {motion, x, y, z}: walk there
        if (n < 4) {
            return false;
        }
        const std::string m = text(first);
        if (m.empty()) {
            return res(0);
        }
        walkTo(m, {c.real(first + 1), c.real(first + 2), c.real(first + 3)});
        return res(1);
    }
    case 111: { // {motion, angle}: play it once (no restart of the state)
        if (n < 2) {
            return false;
        }
        const std::string m = text(first);
        if (m.empty()) {
            return res(0);
        }
        motion_ = m;
        angle_ = c.real(first + 1);
        state_ = 3;
        return res(1);
    }
    case 112: // {angle}: turn to it
        if (n == 0) {
            return false;
        }
        angle_ = c.real(first);
        state_ = 4;
        return res(1);
    case 113: // stand
        state_ = 1;
        return res(1);
    case 114: // stand at once: the standing motion restarted, place and turns cleared
        state_ = previous_ = 1;
        restart_ = false;
        if (usa) {
            playMotion(*usa, kStandMotion, 4);
            usa->offset.set({0, 0, 0});
            usa->position.set({0, 0, 0});
            usa->rotation3Offset.set({0, 0, 0});
            usa->rotation3.set({0, 0, 0});
        }
        return res(1);
    case 115: // the standing motion back to 001_立ち
        setStandMotion(kStandMotion);
        return res(1);
    case 117: // walking
        return res(state_ == 2 ? 1 : 0);
    case 118: // the target's distance across x (0x2d0fa8 then 0x2d0b08: its first part)
        if (!usa) {
            return false;
        }
        c.result = Value::ofFloat(target_[0] - usa->position.value()[0]);
        return true;
    case 119: // busy
        return res(state_ != 1 ? 1 : 0);
    case 120: // the rabbit's move stops where it is
        if (usa) {
            usa->position.set(usa->position.value());
        }
        return res(1);
    case 160: { // {trick id}: its first text row
        if (n == 0) {
            return false;
        }
        const Trick* t = trick(c.integer(first));
        return res(t ? t->text : 0);
    }
    case 170: // Nazo's last judgement passed
        return res(passed_ ? 1 : 0);
    case 200: // the hub's handle for the loops on top
        c.result = Value::ofInt(kHandle);
        return true;
    case 210: // the second name (progress +0x8694 + 0x30)
        c.result = Value::ofText(game_.names[1]);
        return true;
    case 220: // {flag}
        if (n == 0) {
            return false;
        }
        return res(rabbitFlag(game_, c.integer(first)) ? 1 : 0);
    case 221: // {flag, on}
        if (n < 2) {
            return false;
        }
        return res(setRabbitFlag(game_, c.integer(first), c.at(first + 1).truthy()) ? 1 : 0);
    case 300: // "usa" exists (the result is left as it was)
        return usa != nullptr;
    case 950:   // {3D object, -> its motions' count}
    case 951:   // {3D object, -> its motion's name}
    case 952: { // {3D object, motion index}: the hub plays that motion once
        Object* o = n >= 2 ? game_.scene.object3D(xq::hash(text(first))) : nullptr;
        if (!o) {
            return res(0);
        }
        if (number == 950) {
            put(first + 1, Value::ofInt(o->model ? int(o->model->motions.size()) : 0));
            return res(1);
        }
        if (number == 951) {
            put(first + 1, Value::ofText(o->motion));
            return res(1);
        }
        const int i = c.integer(first + 1);
        if (o->model && i >= 0 && i < int(o->model->motions.size())) {
            o->motion = o->model->motions[std::size_t(i)].name;
            o->motionTime = 0;
            o->motionDone = false;
        }
        motion_ = o->motion;
        state_ = 3;
        return res(1);
    }
    default: // 201 (the motion model) and the debug commands: not used
        return false;
    }
}

// ------------------------------------------------------------ the parts

RabbitHub* RabbitPart::hub() const {
    return dynamic_cast<RabbitHub*>(story_.findLoop(RabbitLoopId));
}

bool RabbitPart::command(int number, xq::Call& c, std::size_t first) {
    RabbitHub* h = hub();
    return h && h->command(number, c, first);
}

const std::string* RabbitPart::row(std::uint32_t list, int r, int page) {
    auto* res = game_.scene.resource(list);
    return res && res->text ? res->text->field(r, unsigned(std::max(0, page)), 1) : nullptr;
}

void RabbitPart::addCell(Object& o, const std::string& cell, float x, float y) {
    if (!o.animation) {
        return;
    }
    const std::uint32_t h = xq::hash(cell);
    for (std::size_t i = 0; i < o.animation->cells.size(); ++i) {
        if (o.animation->cells[i].hash == h) {
            o.pieces.push_back({int(i), x, y});
            return;
        }
    }
}

} // namespace picarat::adventure5::rabbit

namespace picarat::adventure5 {

std::unique_ptr<Loop> makeRabbitLoop(Story& story, int id, std::vector<xq::Value> values) {
    switch (id) {
    case RabbitLoopId:
        return std::make_unique<rabbit::RabbitHub>(story, id, std::move(values));
    case RabbitNoteLoopId:
        return rabbit::makeNote(story, id, std::move(values));
    case RabbitNazoLoopId:
        return rabbit::makeNazo(story, id, std::move(values));
    case RabbitCommuLoopId:
        return rabbit::makeCommu(story, id, std::move(values));
    case RabbitShowLoopId:
        return rabbit::makeShow(story, id, std::move(values));
    default:
        return nullptr;
    }
}

} // namespace picarat::adventure5
