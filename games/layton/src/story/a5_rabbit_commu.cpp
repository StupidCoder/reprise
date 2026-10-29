// The rabbit's room (loop 6001, lt5CLoopMiniRabbitCommu; creator 0x37f6a4,
// constructor 0x388290, vtable 0x4034d4: start 0x305e90, commands
// 0x305f5c, frame end 0x305a00, input 0x306d00, reset 0x3089ac).
//
// The rabbit lives on a graph of points in its room (res/sub/
// lt5_sub_rabbit_commu.xs): at each point the script draws one of the
// point's motions by weight (500) and plays it (510); a moving motion walks
// it to a neighbouring point or carries it to a fixed one (climbing,
// sliding, the stepping stones). Talking (lt5_sub_rabbit_talk.xs) picks a
// talk of a group by its conditions - flags, the rabbit's flags and
// tricks, the story's rabbit lessons - at random unless one has priority
// (502), and its outcome teaches tricks and sets flags (503). In training
// the player touches and strokes the rabbit or presses the turn buttons;
// the touches are gathered as gesture codes (body parts, directions,
// buttons) and the rabbit reacts with the first reaction whose gestures
// match for its posture (frame end), possibly a trick it learns.
// Docs: .analysis/adventure56/docs/rabbit.md.
#include "story/a5_rabbit_parts.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace picarat::adventure5::rabbit {

namespace {

// A point of the room (8331, 0x306b78; 0x34 bytes at +0x1250): its id,
// name, place, its neighbours (8351; kept sorted by their names' hashes,
// 0x3078cc) and the motions drawn there by weight (8361, 0x3069f0: the
// weights' sum at +0x18).
struct Point {
    int id = 0;
    std::string name;
    std::uint32_t hash = 0;
    std::array<float, 3> at{};
    std::vector<int> neighbours; // point indices
    std::vector<std::pair<int, float>> motions; // motion index, weight
    float weights = 0;
};

// A motion of the room (8341, 0x308184; 0x18 bytes at +0x1268): its id,
// name, whether it moves the rabbit, the point it ends on (-1: a
// neighbour) and the heading it takes there (degrees).
struct RoomMotion {
    int id = 0;
    std::string name;
    bool moves = false;
    int to = -1;
    float angle = 0;
};

// A scripted visit (8391; 0x18 bytes at +0x1280): two points, the rabbit
// flag it sets once done, the flag that opens it and the lesson flag
// index it needs (0: none).
struct Visit {
    int id = 0, from = -1, to = -1, done = -1;
    std::string flag;
    int lesson = 0;
};

// A reaction (8321, 0x308da0; 0x50 bytes at +0x1238): a name, the motion
// played, a trick it teaches (520), the posture it needs and leaves
// (0 front, 1 right, 2 left, 3 back, 4 on two feet, 5 handstand), the
// touch kind (1 tap, 2 double tap, 3 held), up to four gesture codes with
// the counts that must follow them (held touches), a talk group (525), the
// trick that must be known (-1 none), and whether it applies in each of the
// three modes (522).
struct Reaction {
    std::string name, motion;
    int teaches = 0, from = 0, to = 0, kind = 0;
    std::array<std::uint32_t, 4> gestures{};
    std::array<int, 4> runs{};
    int talk = -1, needs = -1;
    std::array<bool, 3> modes{};
};

// A talk (8252, 0x1272fc; 0x2c bytes): its kind (99: any), priority, the
// flag it needs, the lesson flag index it needs, the rabbit flag it needs,
// the trick it needs known, the rabbit flag that rules it out (and that it
// sets), its text row, a trick whose words' variant it sets and the
// variant, up to four tricks it teaches and four it masters.
struct Talk {
    int kind = 0;
    bool priority = false;
    std::string flag;
    int lesson = -1, needsFlag = -1, needsTrick = -1, doneFlag = -1;
    int text = 0;
    int trick = -1, variant = 0;
    std::array<int, 4> learns{}, masters{};
};

class RabbitCommu final : public RabbitPart {
public:
    RabbitCommu(Story& story, int id, std::vector<Value> values)
        : RabbitPart(story, id, std::move(values), "mini_rabbit_commu") {
        talk_.learns.fill(-1);
        talk_.masters.fill(-1);
    }

    void start() override;
    void afterFrame() override;
    void update() override;
    bool command(int number, xq::Call& c, std::size_t first) override;

private:
    void loadTables(const std::string& path);
    const Point* point(int i) const {
        return i >= 0 && i < int(points_.size()) ? &points_[std::size_t(i)] : nullptr;
    }
    const RoomMotion* motion(int i) const {
        return i >= 0 && i < int(motions_.size()) ? &motions_[std::size_t(i)] : nullptr;
    }
    bool flagSet(const std::string& name, int index = 0) const {
        const int id = game_.flagId(name);
        return id >= 0 && game_.flag(id + index);
    }
    int chooseTalk(int group, int kind, bool masteredOnly); // 502, 0x307c60
    bool applyTalk();                                       // 503, 0x307b30
    const Reaction* matchGesture() const;                   // 0x305868
    static std::uint32_t directionCode(float x, float y); // 0x307768
    void tapDetector(bool touching, bool newPress);      // 0x308024
    void pushGesture(std::uint32_t code) {
        if (gestures_.size() < 0x400) {
            gestures_.push_back(code);
        }
    }

    std::vector<Point> points_;
    std::vector<RoomMotion> motions_;
    std::vector<Visit> visits_;
    std::vector<Reaction> reactions_;
    std::map<int, std::vector<Talk>> talks_;
    // +0x1c0: the gesture codes 1..7 stand for the body's parts (the
    // picked mesh's name hash: DefaultLib_head, _side_L, _side_R, _back,
    // _chest, _nose, _etc), 8..11 directions and 12 / 13 the turn buttons.
    std::array<std::uint32_t, 14> codes_{};

    Talk talk_;                  // +0x194: the talk chosen
    int posture_ = 0;            // +0x1f8
    int event_ = 0;              // +0x210: 1 tap, 2 double tap, 3 held
    int held_ = 0;               // +0x214
    bool touchOn_ = false;       // +0x218
    int taught_ = -1;            // +0x220 (520)
    int reactionTalk_ = -1;      // +0x224 (525)
    int mode_ = 0;               // +0x228 (522)
    bool turning_ = false;       // +0x22c: a turning reaction plays
    bool turnReaction_ = false;  // +0x230
    std::vector<std::uint32_t> gestures_; // +0x234 (up to 0x400)
    // The tap detector (+0x1fc .. +0x214).
    bool secondTap_ = false, tapWaiting_ = false, pressed_ = false;
    int tapFrames_ = 0, sinceRelease_ = 0x100;
    // The story's rabbit lessons: <ウサギクリア> (0x3ed0e8; its hash at RAM
    // 0x413058), read with the lesson's number as the flag index.
    std::string lessonFlag_;
};

void RabbitCommu::loadTables(const std::string& path) {
    // 0x3089c4 over the list with the parsers at RAM 0x44406c.
    int group = -1;
    for (const auto& c : readDataList(game_, path)) {
        switch (c.opcode) {
        case 8251: // {group, count}
            group = c.number(0);
            talks_[group];
            break;
        case 8252: { // a talk of the group (18 operands)
            if (group < 0 || c.operands.size() < 18) {
                break;
            }
            Talk t;
            t.kind = c.number(0);
            t.priority = c.number(1) != 0;
            t.flag = c.text(2);
            t.lesson = c.number(3);
            t.needsFlag = c.number(4);
            t.needsTrick = c.number(5);
            t.doneFlag = c.number(6);
            t.text = c.number(7);
            t.trick = c.number(8);
            t.variant = c.number(9);
            for (std::size_t i = 0; i < 4; ++i) {
                t.learns[i] = c.number(10 + i);
                t.masters[i] = c.number(14 + i);
            }
            talks_[group].push_back(std::move(t));
            break;
        }
        case 8321: { // a reaction (at least 12 operands)
            if (c.operands.size() < 12) {
                break;
            }
            Reaction r;
            r.name = c.text(0);
            r.motion = c.text(1);
            r.teaches = c.number(2);
            r.from = c.number(3);
            r.to = c.number(4);
            r.kind = c.number(5);
            for (std::size_t i = 0; i < 4; ++i) {
                const int g = c.number(6 + i);
                r.gestures[i] = g >= 0 && g < int(codes_.size()) ? codes_[std::size_t(g)] : 0xffffffffu;
            }
            r.runs = {c.number(10), c.number(11), c.number(12), 0};
            r.talk = c.number(13);
            r.needs = c.number(14);
            for (std::size_t i = 0; i < 3; ++i) {
                r.modes[i] = c.number(15 + i) != 0;
            }
            reactions_.push_back(std::move(r));
            break;
        }
        case 8331: { // {id, name, x, y, z}
            Point p;
            p.id = c.number(0);
            p.name = c.text(1);
            p.hash = xq::hash(p.name);
            p.at = {c.real(2), c.real(3), c.real(4)};
            points_.push_back(std::move(p));
            break;
        }
        case 8341: { // {id, name, moves, to, angle}
            RoomMotion m;
            m.id = c.number(0);
            m.name = c.text(1);
            m.moves = c.number(2) != 0;
            m.to = c.number(3);
            m.angle = c.real(4);
            motions_.push_back(std::move(m));
            break;
        }
        case 8351: { // {point, neighbours...}: inserted by name hash (after equals)
            const int p = c.number(0);
            if (p < 0 || p >= int(points_.size()) || !points_[std::size_t(p)].neighbours.empty()) {
                break;
            }
            auto& list = points_[std::size_t(p)].neighbours;
            for (std::size_t i = 1; i < c.operands.size(); ++i) {
                const int q = c.number(i);
                if (q < 0 || q >= int(points_.size())) {
                    continue;
                }
                const std::uint32_t h = points_[std::size_t(q)].hash;
                auto at = std::upper_bound(list.begin(), list.end(), h, [this](std::uint32_t key, int e) {
                    return key < points_[std::size_t(e)].hash;
                });
                list.insert(at, q);
            }
            break;
        }
        case 8361: { // {point, (motion, weight)...}
            const int p = c.number(0);
            if (p < 0 || p >= int(points_.size()) || !points_[std::size_t(p)].motions.empty()) {
                break;
            }
            auto& pt = points_[std::size_t(p)];
            for (std::size_t i = 1; i + 1 < c.operands.size(); i += 2) {
                pt.motions.emplace_back(c.number(i), c.real(i + 1));
                pt.weights += c.real(i + 1);
            }
            break;
        }
        case 8391: // {id, point, point, rabbit flag, flag, lesson}
            visits_.push_back({c.number(0), c.number(1), c.number(2), c.number(3), c.text(4), c.number(5)});
            break;
        default: // counts and ends
            break;
        }
    }
}

void RabbitCommu::start() {
    RabbitPart::start(); // seq/mini_rabbit_commu
    // 0x388290: the gesture codes.
    codes_[0] = 0;
    const char* parts[7] = {"DefaultLib_head",  "DefaultLib_side_L", "DefaultLib_side_R",
                            "DefaultLib_back",  "DefaultLib_chest",  "DefaultLib_nose",
                            "DefaultLib_etc"};
    for (std::size_t i = 0; i < 7; ++i) {
        codes_[1 + i] = xq::hash(parts[i]);
    }
    for (std::uint32_t i = 8; i < 14; ++i) {
        codes_[i] = i;
    }
    loadTables("res/sub/lt5_sub_rabbit_talk");
    loadTables("res/sub/lt5_sub_rabbit_commu");
    lessonFlag_ = game_.codeText(0x3ed0e8);
    if (RabbitHub* h = hub()) {
        h->resetState(); // +0x22d0 = 1
    }
}

void RabbitCommu::update() {
    // 0x306d00 before the scripts. The lower screen is a touch pad (1222)
    // moving a point over the top screen; while touching is on and no
    // turning reaction plays, the protagonist's hand shows there. A press on
    // the pad casts a ray through the point (clamped to 15..388 x 10..200)
    // into the rabbit's collision meshes (0x22c05c): the hit part's
    // material becomes the gesture code and the hand lies on the fur there;
    // past the clamp the code is the side of the screen's diagonals
    // (0x307768). Each frame of a touch adds its code; a new press clears
    // them. *Simplified*: the hand at the hit point (no lower than 0.3),
    // turned by the hit's normal; the original also slides it while the
    // point leaves the rabbit (+0x128c states 2..4) and drags a trail over
    // the fur (0x22c998).
    RabbitHub* h = hub();
    if (h) {
        h->showHand(touchOn_ && !turning_);
    }
    const bool padTouch = game_.input.touching && game_.touchSourceId == 1;
    const bool newPress = padTouch && !game_.previousInput.touching;
    std::uint32_t code = 0;
    Object* usa = game_.scene.object3D(xq::hash("usa"));
    if (h && usa && touchOn_ && padTouch) {
        const float cx = game_.padCursor.x, cy = game_.padCursor.y;
        const float x = std::clamp(cx, 15.0f, 388.0f), y = std::clamp(cy, 10.0f, 200.0f);
        const bool outside = x != cx || y != cy;
        const auto hit = game_.scene.meshPick(*usa, 0, x, y);
        if (hit.hit) {
            std::string part = hit.material;
            std::replace(part.begin(), part.end(), '.', '_');
            code = xq::hash(part);
            auto at = hit.point;
            at[1] = std::max(at[1], 0.3f);
            h->moveHand(at);
            h->turnHand(hit.normal);
        }
        if (outside) {
            code = directionCode(x, y);
        }
    }
    tapDetector(padTouch, newPress);
    if (newPress) {
        gestures_.clear();
    }
    if (padTouch) {
        pushGesture(code);
    }
    RabbitPart::update();
}

std::uint32_t RabbitCommu::directionCode(float x, float y) {
    // 0x307768: the side of the diagonals of (100, 0)..(300, 240).
    const float c1 = 200 * y - 240 * (x - 100);
    const float c2 = 200 * (y - 240) + 240 * (x - 100);
    if (c1 <= 0) {
        return c2 <= 0 ? 8 : 10;
    }
    return c2 <= 0 ? 11 : 9;
}

void RabbitCommu::tapDetector(bool touching, bool newPress) {
    // 0x308024 on the pad's touch: a tap (released soon and near, the
    // input word's 0x40) waits a frame to become a tap (1) unless a second
    // press came quickly (the word's 0x4), which makes it a double tap (2);
    // a held touch is 3 while it lasts, counted in frames.
    const bool released = !touching && game_.previousInput.touching && game_.touchSourceId == 1;
    if (released) {
        if (game_.tapped) {
            if (!secondTap_) {
                tapWaiting_ = true;
            } else {
                event_ = 2;
            }
        }
    } else if (newPress) {
        secondTap_ = tapWaiting_ = false;
        tapFrames_ = 0;
        event_ = 0;
        held_ = 0;
        pressed_ = true;
        if (sinceRelease_ < 0x14) {
            secondTap_ = true; // a press soon after the last (0x3a61f0: 0x1800)
        }
    } else if (!touching && tapWaiting_) {
        if (++tapFrames_ > 1 && !secondTap_) {
            event_ = 1;
            tapWaiting_ = false;
        }
    }
    if (touching) {
        if (event_ == 0) {
            event_ = 3;
        }
        ++held_;
        sinceRelease_ = 0;
    } else {
        if (event_ == 3) {
            event_ = 0;
        }
        sinceRelease_ += std::max(1, game_.displayFramesPerFrame); // display frames (0x3a61f0)
    }
}

const Reaction* RabbitCommu::matchGesture() const {
    // 0x305868: a tap or double tap matches a reaction of that kind for the
    // posture whose first gesture is the last one gathered; a touch held
    // over 14 frames one whose gestures follow in order, each followed by
    // more gestures than its count.
    if (event_ == 1 || event_ == 2) {
        if (gestures_.empty()) {
            return nullptr;
        }
        for (const auto& r : reactions_) {
            if (r.kind == event_ && r.from == posture_ && r.gestures[0] == gestures_.back()) {
                return &r;
            }
        }
    } else if (event_ == 3 && held_ > 14) {
        for (const auto& r : reactions_) {
            if (r.kind != 3 || r.from != posture_) {
                continue;
            }
            bool all = true;
            std::size_t at = 0;
            for (std::size_t i = 0; i < 4; ++i) {
                if (r.gestures[i] == 0xffffffffu) {
                    continue;
                }
                bool found = false;
                for (; at < gestures_.size(); ++at) {
                    if (gestures_[at] == r.gestures[i]) {
                        const int left = int(gestures_.size() - at);
                        found = r.runs[i] < left;
                        if (found) {
                            at += std::size_t(r.runs[i]);
                        }
                        break;
                    }
                }
                all = all && found;
            }
            if (all) {
                return &r;
            }
        }
    }
    return nullptr;
}

void RabbitCommu::afterFrame() {
    // 0x305a00: the base frame end, the rabbit's motion machine, then (with
    // touching on) a matching reaction: the rabbit plays its motion unless
    // busy, a trick it needs is unknown or the mode rules it out.
    ScriptLoop::afterFrame();
    RabbitHub* h = hub();
    if (!h) {
        return;
    }
    h->stepMotion();
    turning_ = h->busy() && turnReaction_;
    if (!touchOn_) {
        return;
    }
    const bool wasTap = event_ == 1 || event_ == 2;
    const Reaction* r = matchGesture();
    if (wasTap) {
        event_ = 0; // 0x305868 clears the touch state for taps
        held_ = 0;
    }
    if (!r) {
        return;
    }
    event_ = 0;
    held_ = 0;
    const int state = trickState(game_, r->needs); // (trick ids are their slots)
    const bool modeOk = mode_ < 1 || mode_ > 3 || r->modes[std::size_t(mode_ - 1)];
    if (!((state == 0 && r->needs != -1) || h->busy() || !modeOk)) {
        h->playOnce(r->motion, 100); // (angle 100: keeps its heading)
        posture_ = r->to;
        if (r->to >= 0 && r->to <= 3) {
            h->setStandMotion(kStandMotion);
        } else if (r->to == 4) {
            h->setStandMotion(game_.codeText(0x305c0c)); // 058_二本足で立つ_LOOP
        } else if (r->to == 5) {
            h->setStandMotion(game_.codeText(0x305c24)); // 069_逆立ち_LOOP
        }
        taught_ = r->teaches;
        reactionTalk_ = r->talk;
        turnReaction_ = r->motion == game_.codeText(0x305c34) || r->motion == game_.codeText(0x305c44);
    }
    gestures_.clear();
}

int RabbitCommu::chooseTalk(int group, int kind, bool masteredOnly) {
    // 0x307c60: from the first talk of the kind (or of kind 99 if that comes
    // first), the talks of that kind whose conditions hold; one with
    // priority is taken at once, else one at random. Result: its text row
    // (0: none).
    talk_ = Talk{};
    talk_.learns.fill(-1);
    talk_.masters.fill(-1);
    const auto it = talks_.find(group);
    if (it == talks_.end()) {
        return 0;
    }
    const auto& list = it->second;
    std::size_t u = 0;
    int want = kind;
    for (; u < list.size(); ++u) {
        if (list[u].kind == kind) {
            break;
        }
        if (list[u].kind == 99) {
            want = 99;
            break;
        }
    }
    std::vector<std::size_t> candidates;
    for (; u < list.size() && list[u].kind == want; ++u) {
        const Talk& t = list[u];
        bool ok = t.flag.empty() || game_.flagId(t.flag) < 0 || flagSet(t.flag);
        if (ok && t.doneFlag >= 0 && rabbitFlag(game_, t.doneFlag)) {
            ok = false;
        }
        if (ok && t.lesson > 0 && !flagSet(lessonFlag_, t.lesson)) {
            ok = false;
        }
        if (ok && t.needsFlag >= 0 && !rabbitFlag(game_, t.needsFlag)) {
            ok = false;
        }
        if (ok && t.needsTrick > 0 && trickState(game_, t.needsTrick) == 0) {
            ok = false;
        }
        if (masteredOnly) {
            bool skip = false;
            for (int m : t.masters) {
                if (m >= 0 && trickState(game_, m) == 0) {
                    skip = true;
                }
            }
            if (skip) {
                continue;
            }
        }
        if (!ok) {
            continue;
        }
        if (t.priority) {
            talk_ = t;
            break;
        }
        if (candidates.size() < 32) {
            candidates.push_back(u);
        }
    }
    if (talk_.text != 0) {
        return talk_.text;
    }
    if (!candidates.empty()) {
        talk_ = list[candidates[game_.machine.random(std::uint32_t(candidates.size()))]];
        return talk_.text;
    }
    return 0;
}

bool RabbitCommu::applyTalk() {
    // 0x307b30: the talk's rabbit flag, its trick's words variant, the
    // tricks it teaches (learned and marked new) and masters (marked new
    // when unknown before).
    RabbitHub* h = hub();
    if (!h) {
        return false;
    }
    if (talk_.doneFlag >= 0) {
        setRabbitFlag(game_, talk_.doneFlag, true);
    }
    if (talk_.trick >= 0) {
        const Trick* t = h->trick(talk_.trick);
        if (!t) {
            return false;
        }
        setVariant(game_, t->slot, talk_.variant & 0xff);
    }
    for (std::size_t i = 0; i < 4; ++i) {
        if (const Trick* t = talk_.learns[i] >= 0 ? h->trick(talk_.learns[i]) : nullptr) {
            addTrickState(game_, t->slot, 1);
            setFresh(game_, t->slot, true);
        }
        if (const Trick* t = talk_.masters[i] >= 0 ? h->trick(talk_.masters[i]) : nullptr) {
            if (trickState(game_, t->slot) == 0) {
                setFresh(game_, t->slot, true);
            }
            addTrickState(game_, t->slot, 2);
        }
    }
    return true;
}

bool RabbitCommu::command(int number, xq::Call& c, std::size_t first) {
    const std::size_t n = operands(c, first);
    RabbitHub* h = hub();
    switch (number) {
    case 500: { // {point}: a motion drawn by the point's weights (-1: none)
        const Point* p = n ? point(c.integer(first)) : nullptr;
        if (!p || p->motions.empty()) {
            return n ? result(c, -1) : false;
        }
        // (A draw past the last sum reads beyond the list in the
        // original; the port takes the last motion.)
        const float r = game_.machine.randomFloat(p->weights);
        float sum = 0;
        std::size_t i = 0;
        for (; i + 1 < p->motions.size(); ++i) {
            sum += p->motions[i].second;
            if (r < sum) {
                break;
            }
        }
        const RoomMotion* m = motion(p->motions[i].first);
        return result(c, m ? m->id : -1);
    }
    case 501: { // {point}: a neighbour at random
        const Point* p = n ? point(c.integer(first)) : nullptr;
        if (!p) {
            return n ? result(c, -1) : false;
        }
        const std::uint32_t k = game_.machine.random(std::uint16_t(p->neighbours.size()));
        const Point* q = k < p->neighbours.size() ? point(p->neighbours[k]) : nullptr;
        return result(c, q ? q->id : -1);
    }
    case 502: // {group, kind [, mastered only]}: a talk's text row
        if (n < 2) {
            return false;
        }
        return result(c, chooseTalk(c.integer(first), c.integer(first + 1),
                                    n > 2 && c.at(first + 2).truthy()));
    case 503: // the talk's outcome
        return result(c, applyTalk() ? 1 : 0);
    case 505: { // {point, -> x, y, z}
        const Point* p = n >= 4 ? point(c.integer(first)) : nullptr;
        if (!p) {
            return n >= 4 ? result(c, 0) : false;
        }
        for (std::size_t i = 0; i < 3; ++i) {
            put(c, first + 1 + i, Value::ofFloat(p->at[i]));
        }
        return result(c, 1);
    }
    case 510: { // {motion, point}: the rabbit plays it; result: where it ends (-1: here)
        if (n < 2 || !h) {
            return false;
        }
        const RoomMotion* m = motion(c.integer(first));
        if (!m) {
            return result(c, -1);
        }
        if (!m->moves) {
            h->playOnce(m->name, 100);
            return result(c, -1);
        }
        if (m->to >= 0) {
            h->playOnce(m->name, m->angle * 0.0174533f);
            const Point* to = point(m->to);
            return result(c, to ? to->id : -1);
        }
        const Point* p = point(c.integer(first + 1));
        if (!p) {
            return result(c, -1);
        }
        h->walkTo(m->name, p->at);
        return result(c, p->id);
    }
    case 520: // the trick the last reaction teaches (then -1)
        c.result = Value::ofInt(taught_);
        taught_ = -1;
        return true;
    case 521: // the posture back to the front
        posture_ = 0;
        return result(c, 1);
    case 522: // {mode}
        if (n == 0) {
            return false;
        }
        mode_ = c.integer(first);
        return result(c, 1);
    case 523: // the posture
        return result(c, posture_);
    case 524: // a learned trick's text row among the posture's reactions (-1: none)
        if (h) {
            for (const auto& r : reactions_) {
                const Trick* t = h->trick(r.needs);
                if (r.from == posture_ && trickState(game_, r.needs) == 1 && t) {
                    return result(c, t->text);
                }
            }
        }
        return result(c, -1);
    case 525: // the last reaction's talk group (then -1)
        c.result = Value::ofInt(reactionTalk_);
        reactionTalk_ = -1;
        return true;
    case 530: // {on}: touching the rabbit
        if (n == 0) {
            return false;
        }
        touchOn_ = c.at(first).truthy();
        return result(c, 1);
    case 531: // {1 / 2}: a turn button pressed (gesture 12 / 13, a tap)
        if (n == 0) {
            return false;
        }
        if (c.integer(first) == 1) {
            pushGesture(12);
        } else if (c.integer(first) == 2) {
            pushGesture(13);
        }
        event_ = 1;
        return result(c, 1);
    case 540: { // {-> point, -> point}: a scripted visit that is open
        if (n < 2) {
            return false;
        }
        for (const auto& v : visits_) {
            if (!flagSet(v.flag) || (v.lesson != 0 && !flagSet(lessonFlag_, v.lesson)) ||
                rabbitFlag(game_, v.done)) {
                continue;
            }
            const Point* a = point(v.from);
            const Point* b = point(v.to);
            if (a && b) {
                put(c, first, Value::ofInt(a->id));
                put(c, first + 1, Value::ofInt(b->id));
                return result(c, 1);
            }
        }
        return result(c, 0);
    }
    case 541: { // {motion}: it moves the rabbit
        if (n == 0) {
            return false;
        }
        const RoomMotion* m = motion(c.integer(first));
        return result(c, m && m->moves ? 1 : 0);
    }
    case 542: { // a mastered trick's motion at random (049_その場で困る without)
        int count = 0;
        for (int s = 0; s < 32; ++s) {
            count += trickState(game_, s) == 2 ? 1 : 0;
        }
        const int pick = int(game_.machine.random(std::uint32_t(count)));
        int k = 0;
        for (int s = 0; s < 32 && h; ++s) {
            if (trickState(game_, s) != 2) {
                continue;
            }
            if (k == pick) {
                const Trick* t = h->trick(s);
                const TrickMotion* m = t ? h->trickMotion(t->description) : nullptr;
                if (m) {
                    c.result = Value::ofText(m->motion);
                    return true;
                }
            }
            ++k;
        }
        c.result = Value::ofText(game_.codeText(0x3069b4));
        return true;
    }
    case 550: // a trick marked new
        for (int s = 0; s < 32; ++s) {
            if (fresh(game_, s)) {
                return result(c, 1);
            }
        }
        return result(c, 0);
    case 590: // {i}: the talk's i-th trick taught
    case 591: // {i}: the talk's i-th trick mastered
        if (n == 0 || c.unsignedInt(first) >= 4) {
            return false;
        }
        return result(c, number == 590 ? talk_.learns[c.unsignedInt(first)]
                                       : talk_.masters[c.unsignedInt(first)]);
    case 592: // the talk's trick (its words' variant)
        return result(c, talk_.trick);
    case 901: { // {point, motion}: the point's next motion after it (debug)
        const Point* p = n >= 2 ? point(c.integer(first)) : nullptr;
        if (!p || p->motions.empty()) {
            c.result = Value::ofInt(n >= 2 ? c.integer(first + 1) : 0);
            return n >= 2;
        }
        std::size_t k = 0;
        for (std::size_t i = 0; i < p->motions.size(); ++i) {
            const RoomMotion* m = motion(p->motions[i].first);
            if (m && m->id == c.integer(first + 1)) {
                k = i + 1;
                break;
            }
        }
        if (k >= p->motions.size()) {
            k = 0;
        }
        const RoomMotion* m = motion(p->motions[k].first);
        c.result = Value::ofInt(m ? m->id : c.integer(first + 1));
        return true;
    }
    default:
        return RabbitPart::command(number, c, first);
    }
}

} // namespace

std::unique_ptr<Loop> makeCommu(Story& story, int id, std::vector<Value> values) {
    return std::make_unique<RabbitCommu>(story, id, std::move(values));
}

} // namespace picarat::adventure5::rabbit
