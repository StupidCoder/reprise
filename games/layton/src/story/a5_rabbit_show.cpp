// The rabbit show (loop 6003, lt5CLoopMiniRabbitShow; creator 0x37f5a0,
// constructor 0x388034, vtable 0x403444: start 0x302f38, commands
// 0x303260, frame end 0x302a28, reset 0x3050c4).
//
// A show plays on a stage ("map", rs001) behind a curtain: a timeline of
// up to 128 events (res/sub/lt5_sub_rabbit_show<n>.xs, 8311) whose frames
// the script counts - the narrator's lines, the director's comments, the
// rabbit's tricks in the show's slots and what happens on stage: the
// stage's background and its props changing (sets from
// lt5_sub_rabbit_show0.xs, 8371 / 8381), the light dimming and a spot
// following the rabbit. Each trick slot plays the trick the player chose in
// the show puzzle (or, on a replay, the right one); the director comments
// by the hub's judgement (right, wrong, free). The script drives the
// timeline; the loop starts an event (710) and finishes it (711 waits).
// Docs: .analysis/adventure56/docs/rabbit.md.
#include "story/a5_rabbit_parts.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace picarat::adventure5::rabbit {

namespace {

// A stage set (8371 backgrounds, 8381 props; 0x303090 / 0x304210, 0x18
// bytes): an id, a name, the stage mesh it shows (backgrounds) and the
// motions that bring it on and take it off.
struct StageSet {
    int id = 0;
    std::string name, mesh, on, off;
};

// An event (8311, 0x303d6c; 0x50 bytes from +0x17c): the trick slot it
// plays (-1 none), its kind (1 the rabbit's trick, 50 wait for the lights,
// 51 the light's level, 52 a spot on an object, 53 the spot off, 100 the
// background, 101 a prop, 3 nothing), the frames at which the narrator's
// line, the event, the director's comment and a sound start, a name, four
// values, the director's lines for a right, wrong or free slot, a value
// (721), the rabbit's motion (replays) and its place on the stage
// (x = column x 5 - 10, z = 7.5 - row x 5) and heading.
struct Event {
    int step = -1, kind = 0;
    int line = 0, start = 0, comment = 0, sound = 0;
    std::string name;
    std::array<float, 4> values{};
    std::array<int, 3> lines{}; // right, wrong, free
    int extra = 0;
    std::string motion;
    std::array<float, 3> at{};
    float angle = 0;
};

constexpr int kEvents = 128; // +0x178

class RabbitShow final : public RabbitPart {
public:
    RabbitShow(Story& story, int id, std::vector<Value> values)
        : RabbitPart(story, id, std::move(values), "mini_rabbit_show") {}

    void start() override;
    void end() override;
    void afterFrame() override;
    bool command(int number, xq::Call& c, std::size_t first) override;

private:
    void loadTables(const std::string& path);
    const StageSet* findSet(const std::vector<StageSet>& list, const std::string& name) const {
        for (const auto& s : list) {
            if (s.name == name) {
                return &s;
            }
        }
        return nullptr;
    }
    const Event* event(std::uint32_t i) const { return i < events_.size() ? &events_[i] : nullptr; }
    const RabbitHub::Mark* mark() const {
        RabbitHub* h = hub();
        if (!h || slot_ < 0 || slot_ >= int(h->marks().size())) {
            return nullptr;
        }
        return &h->marks()[std::size_t(slot_)];
    }
    // A motion on one of the stage's layers (0x2936c0 with flags 8: once).
    static void playLayer(Object& o, const std::string& motion, int layer);
    static void clearLayer(Object& o, int layer); // 0x18f6cc
    static void layerToEnd(Object& o, int layer); // 0x18fb30 with 1
    static int motionLength(const Object& o, const std::string& motion);

    void beginEvent(const Event& e, Object& usa, Object& map);
    void rabbitTrick(const Event& e, Object& usa); // 0x305250
    void background(const Event& e, Object& map);  // 0x303bd8
    void prop(const Event& e, Object& map);        // 0x3043e0
    void spotOn(const std::string& name, float height, float level, float frames);
    bool prepareStage(); // 730, 0x304628

    std::vector<StageSet> backgrounds_, props_; // +0x29b8 / +0x29d0
    std::vector<Event> events_;                 // +0x17c (count +0x178)
    std::string headerBackground_;              // 8312
    std::array<std::string, 2> headerProps_;
    int number_ = 1;                // +0x174: the show (1..10; past 99 a replay)
    std::uint32_t current_ = 0;     // +0x297c
    std::uint32_t last_ = 0xffffffff; // +0x2980
    bool done_ = false;             // +0x2984
    int counter_ = 0;               // +0x2988
    int slot_ = 0;                  // +0x298c: the trick slot reached
    std::string pending_;           // +0x2990: a trick waiting for the walk back
    int layer_ = 0;                 // +0x2994: the background's layer (0 / 1)
    const StageSet* background_ = nullptr; // +0x2998
    std::array<const StageSet*, 2> shown_{}; // +0x29a0: props on layers 2 / 3
    bool replayMotions_ = false;    // +0x29b4 (798): the events' own motions
    // The lights (0x304f74 / 0x3050dc): levels faded by tweens; the spot
    // over an object. Kept as state only: the light objects these drive
    // (lgt_dir / lgt_spot, their LGT files) are not applied yet (open).
    adventure56::Tween<1> directional_, spot_;
    std::string spotObject_;
    float spotHeight_ = 0;
    bool lightsOn_ = false;
};

void RabbitShow::loadTables(const std::string& path) {
    // 0x30537c over the list (parsers at RAM 0x44411c).
    for (const auto& c : readDataList(game_, path)) {
        switch (c.opcode) {
        case 8371: // {id, name, mesh, on, off}
            backgrounds_.push_back({c.number(0), c.text(1), c.text(2), c.text(3), c.text(4)});
            break;
        case 8381:
            props_.push_back({c.number(0), c.text(1), c.text(2), c.text(3), c.text(4)});
            break;
        case 8312: // {background, prop, prop}
            headerBackground_ = c.text(0);
            headerProps_ = {c.text(1), c.text(2)};
            break;
        case 8311: { // an event (19 operands)
            if (c.operands.size() < 19 || events_.size() >= kEvents) {
                break;
            }
            Event e;
            e.step = c.number(0);
            e.kind = c.number(1);
            e.line = c.number(2);
            e.start = c.number(3);
            e.name = c.text(4);
            for (std::size_t i = 0; i < 4; ++i) {
                e.values[i] = c.real(5 + i);
            }
            e.motion = c.text(9);
            e.at = {c.real(10) * 5 - 10, 0, 7.5f - c.real(11) * 5};
            // degrees x 1/180 x pi, wrapped (0x303d6c).
            e.angle = adventure56::wrapAngle(c.real(12) * 0.0055555557f * 3.14159274f);
            e.lines = {c.number(13), c.number(14), c.number(15)};
            e.comment = c.number(16);
            e.extra = c.number(17);
            e.sound = c.number(18);
            events_.push_back(std::move(e));
            break;
        }
        default:
            break;
        }
    }
}

void RabbitShow::start() {
    // 0x302f38: value 1 is the show (-1: the first; from 100 a replay of
    // show value - 99).
    const int v = values[1].toInt();
    int file = 0;
    if (v < 100) {
        number_ = v == -1 ? 1 : v + 1;
        file = number_;
    } else {
        number_ = v;
        file = v - 99;
    }
    RabbitPart::start(); // seq/mini_rabbit_show
    loadTables("res/sub/lt5_sub_rabbit_show0");
    loadTables(format("res/sub/lt5_sub_rabbit_show%u", unsigned(file)));
}

void RabbitShow::end() {
    RabbitPart::end();
}

void RabbitShow::playLayer(Object& o, const std::string& motion, int layer) {
    if (motion.empty()) {
        return;
    }
    if (layer == 0) {
        o.motion = motion;
        o.motionTime = 0;
        o.motionOnce = true;
        o.motionDone = false;
        o.motionStops = false;
        return;
    }
    if (layer >= 1 && layer <= int(o.upperMotions.size())) {
        auto& l = o.upperMotions[std::size_t(layer - 1)];
        l.motion = motion;
        l.time = 0;
        l.once = true;
        l.done = false;
        l.stops = false;
    }
}

void RabbitShow::clearLayer(Object& o, int layer) {
    if (layer == 0) {
        o.motion.clear();
        o.motionTime = 0;
    } else if (layer >= 1 && layer <= int(o.upperMotions.size())) {
        o.upperMotions[std::size_t(layer - 1)] = {};
    }
}

void RabbitShow::layerToEnd(Object& o, int layer) {
    // 0x18fb30 with 1: the layer's time at its motion's end.
    if (layer < 1 || layer > int(o.upperMotions.size())) {
        return;
    }
    auto& l = o.upperMotions[std::size_t(layer - 1)];
    const int length = motionLength(o, l.motion);
    if (length > 0) {
        l.time = float(length - 1);
    }
}

int RabbitShow::motionLength(const Object& o, const std::string& motion) {
    const adventure56::Motion* m = o.model && !motion.empty() ? o.model->motion(motion) : nullptr;
    return m ? std::max(1, m->last - m->first + 1) : 0;
}

void RabbitShow::spotOn(const std::string& name, float height, float level, float frames) {
    // 0x304930 / 0x304c14: the spot follows the object from `height`
    // above it (0x3713c4 / 0x2cfd48) and fades to `level`.
    spotObject_ = name;
    spotHeight_ = height;
    spot_.start({level}, frames, 0);
}

bool RabbitShow::prepareStage() {
    // 0x304628: the header's background (its mesh shown, its "on" motion
    // played once on layer 0, cleared when one frame long) and props
    // (played on layer 2 and shown at their end).
    Object* map = game_.scene.object3D(xq::hash(game_.codeText(0x3ed1c0)));
    if (!map) {
        return false;
    }
    if (const StageSet* b = findSet(backgrounds_, headerBackground_)) {
        map->hiddenMeshes.erase(xq::hash(b->mesh));
        playLayer(*map, b->on, 0);
        if (motionLength(*map, b->on) == 1) {
            clearLayer(*map, 0);
        }
        background_ = b;
    }
    if (const StageSet* p = findSet(props_, headerProps_[0])) {
        playLayer(*map, p->on, 2);
        layerToEnd(*map, 2);
        shown_[0] = p;
    }
    if (const StageSet* p = findSet(props_, headerProps_[1])) {
        // (The original plays the second prop on layer 2 as well and moves
        // layer 3 to its end; no show has a second prop.)
        playLayer(*map, p->on, 2);
        layerToEnd(*map, 3);
        shown_[1] = p;
    }
    return true;
}

void RabbitShow::background(const Event& e, Object& map) {
    // 0x303bd8: the old background goes on its layer, the new one comes
    // on the other.
    if (background_) {
        playLayer(map, background_->off, layer_);
        layer_ = layer_ <= 1 ? 1 - layer_ : 0;
    }
    const StageSet* b = findSet(backgrounds_, e.name);
    background_ = b;
    if (b) {
        map.hiddenMeshes.erase(xq::hash(b->mesh));
        playLayer(map, b->on, layer_);
        if (motionLength(map, b->on) == 1) {
            clearLayer(map, layer_);
        }
    }
    done_ = true;
}

void RabbitShow::prop(const Event& e, Object& map) {
    // 0x3043e0: a prop comes on (value 0 is 1) or goes off on its layer
    // (2 + its slot); a new one takes a free slot (none free: nothing).
    const bool on = e.values[0] == 1.0f;
    int k = -1;
    for (int i = 0; i < 2; ++i) {
        if (shown_[std::size_t(i)] && shown_[std::size_t(i)]->name == e.name) {
            k = i;
            break;
        }
    }
    if (k >= 0) {
        const StageSet* p = shown_[std::size_t(k)];
        if (on) {
            playLayer(map, p->on, k + 2);
        } else {
            playLayer(map, p->off, k + 2);
            shown_[std::size_t(k)] = nullptr;
        }
        done_ = true;
        return;
    }
    if (!shown_[0]) {
        k = 0;
    } else if (!shown_[1]) {
        k = 1;
    } else {
        done_ = true;
        return;
    }
    if (const StageSet* p = findSet(props_, e.name)) {
        if (on) {
            playLayer(map, p->on, k + 2);
            shown_[std::size_t(k)] = p;
        } else {
            playLayer(map, p->off, k + 2);
        }
    }
    done_ = true;
}

void RabbitShow::rabbitTrick(const Event& e, Object& usa) {
    // 0x305250: the slot's trick (the one chosen in the puzzle; with 798 on
    // the event's own motion; none: one of the two failure motions at
    // random, 0x291738). If it would carry the rabbit past z = 5 it first
    // dashes back to (0, 0, -9) and plays the trick after.
    RabbitHub* h = hub();
    if (!h) {
        return;
    }
    std::string motion;
    if (const RabbitHub::Mark* m = mark(); m && m->trick > 0) {
        const Trick* t = h->trick(m->trick);
        const TrickMotion* tm = t ? h->trickMotion(t->description) : nullptr;
        if (tm) {
            motion = tm->motion;
        }
    }
    if (replayMotions_) {
        motion = e.motion;
    }
    if (motion.empty()) {
        motion = game_.codeText(game_.machine.random(2) == 0 ? 0x3ed178 : 0x3ed15c);
    }
    const auto move = h->motionMove(motion);
    if (usa.position.value()[2] + move[2] > 5.0f) {
        pending_ = motion;
        h->walkTo(game_.codeText(0x3ed190), {0, 0, -9}); // 014_ダッシュ
        return;
    }
    h->playOnce(motion, 0);
}

void RabbitShow::beginEvent(const Event& e, Object& usa, Object& map) {
    done_ = false;
    counter_ = 0;
    switch (e.kind) {
    case 53: // the spot leaves the object (0x2cd138)
        if (spotObject_ == e.name) {
            spotObject_.clear();
        }
        done_ = true;
        counter_ = -1;
        break;
    case 1:
        rabbitTrick(e, usa);
        break;
    case 51: // the light's level over frames
        directional_.start({e.values[0]}, e.values[1], 0);
        done_ = true;
        counter_ = -1;
        break;
    case 52: // a spot on an object
        spotOn(e.name, e.values[2], e.values[0], e.values[1]);
        done_ = true;
        counter_ = -1;
        break;
    case 100:
        background(e, map);
        break;
    case 101:
        prop(e, map);
        break;
    default:
        break;
    }
}

void RabbitShow::afterFrame() {
    // 0x302a28: the base frame end, the lights' frame, the rabbit's motion
    // machine, then the current event: begun when it changed, and kept on
    // (the rabbit's trick until it is done, waiting for the lights) or done.
    ScriptLoop::afterFrame();
    RabbitHub* h = hub();
    if (!h) {
        return;
    }
    directional_.step();
    spot_.step();
    h->stepMotion();
    Object* usa = game_.scene.object3D(xq::hash("usa"));
    Object* map = game_.scene.object3D(xq::hash(game_.codeText(0x3ed1c0)));
    if (!usa || !map) {
        return;
    }
    if (const Event* e = event(current_)) {
        if (current_ != last_) {
            beginEvent(*e, *usa, *map);
        }
        if (e->kind == 1) {
            if (counter_ == 0) {
                if (!h->busy()) {
                    if (pending_.empty()) {
                        done_ = true;
                        counter_ = -1;
                    } else {
                        h->turnTo(0); // *hypothesis*: s0 is 0 here
                        ++counter_;
                    }
                }
            } else if (counter_ == 1 && !h->busy()) {
                if (pending_.empty()) {
                    done_ = true;
                    counter_ = -1;
                } else {
                    h->playOnce(pending_, 0);
                    pending_.clear();
                    counter_ = 0;
                }
            }
        } else if (e->kind == 50) {
            if (!directional_.moving() && !spot_.moving()) {
                done_ = true;
                counter_ = -1;
            }
        } else {
            done_ = true;
        }
        last_ = current_;
    }
}

bool RabbitShow::command(int number, xq::Call& c, std::size_t first) {
    const std::size_t n = operands(c, first);
    auto index = [&c, first]() { return c.unsignedInt(first); };
    auto outI = [&c, first](std::size_t i, int v) { put(c, first + i, Value::ofInt(v)); };
    auto outF = [&c, first](std::size_t i, float v) { put(c, first + i, Value::ofFloat(v)); };
    switch (number) {
    case 701: // {event, -> its trick slot}
    case 723: // {event, -> its kind}
    case 721: { // {event, -> its value}
        const Event* e = n >= 2 ? event(index()) : nullptr;
        if (n < 2) {
            return false;
        }
        if (!e) {
            return result(c, 0);
        }
        outI(1, number == 701 ? e->step : number == 723 ? e->kind : e->extra);
        return result(c, 1);
    }
    case 702: { // {event, -> the rabbit's motion}
        const Event* e = n >= 2 ? event(index()) : nullptr;
        if (n < 2) {
            return false;
        }
        if (!e) {
            return result(c, 0);
        }
        put(c, first + 1, Value::ofText(e->motion));
        return result(c, 1);
    }
    case 703: { // {event, -> x, y, z}
        const Event* e = n >= 4 ? event(index()) : nullptr;
        if (n < 4) {
            return false;
        }
        if (!e) {
            return result(c, 0);
        }
        for (std::size_t i = 0; i < 3; ++i) {
            outF(1 + i, e->at[i]);
        }
        return result(c, 1);
    }
    case 704:   // {event, -> heading}
    case 705:   // {event, -> the line's frame}
    case 706:   // {event, -> the event's frame}
    case 707:   // {event, -> the director's frame}
    case 708: { // {event, -> the sound's frame}
        const Event* e = n >= 2 ? event(index()) : nullptr;
        if (n < 2) {
            return false;
        }
        if (!e) {
            return result(c, 0);
        }
        const float v = number == 704   ? e->angle
                        : number == 705 ? float(e->line)
                        : number == 706 ? float(e->start)
                        : number == 707 ? float(e->comment)
                                        : float(e->sound);
        outF(1, v);
        return result(c, 1);
    }
    case 710: // {event}: it starts
        if (n == 0) {
            return false;
        }
        current_ = index();
        return result(c, 1);
    case 711: // the event still runs
        return result(c, done_ ? 0 : 1);
    case 712: // {event}: past the last
        if (n == 0) {
            return false;
        }
        return result(c, index() <= events_.size() ? 0 : 1);
    case 713: // the next trick slot
        ++slot_;
        return result(c, 1);
    case 714: { // {from, kind}: an event of the kind from there on
        if (n < 2) {
            return false;
        }
        for (std::uint32_t i = index(); i < kEvents; ++i) {
            const int kind = i < events_.size() ? events_[i].kind : 0;
            if (kind == c.integer(first + 1)) {
                return result(c, 1);
            }
        }
        return result(c, 0);
    }
    case 720: { // {event, -> the director's line for the slot's mark}
        const Event* e = n >= 2 ? event(index()) : nullptr;
        if (n < 2) {
            return false;
        }
        if (!e) {
            return result(c, 0);
        }
        const RabbitHub::Mark* m = mark();
        outI(1, !m ? e->lines[2] : m->mark == -1 ? e->lines[1] : m->mark == 1 ? e->lines[0] : e->lines[2]);
        return result(c, 1);
    }
    case 722: { // {event, -> the slot's trick}
        if (n < 2) {
            return false;
        }
        if (!event(index())) {
            return result(c, 0);
        }
        const RabbitHub::Mark* m = mark();
        outI(1, m ? m->trick : 0);
        return result(c, 1);
    }
    case 725: { // the slot's mark
        const RabbitHub::Mark* m = mark();
        return result(c, m ? m->mark : 0);
    }
    case 730: // the stage's opening sets
        return result(c, prepareStage() ? 1 : 0);
    case 740: // {level, frames}: the light
        if (n < 2) {
            return false;
        }
        directional_.start({c.real(first)}, c.real(first + 1), 0);
        return result(c, 1);
    case 741: { // {object, level, frames, height}: the spot on it
        if (n < 4) {
            return false;
        }
        const std::string* name = c.text(first);
        if (!name || !game_.scene.object3D(xq::hash(*name))) {
            return result(c, 0);
        }
        spotOn(*name, c.real(first + 3), c.real(first + 1), c.real(first + 2));
        return result(c, 1);
    }
    case 742: { // {show}: the right answer as the judgement (replays)
        RabbitHub* h = hub();
        const Show* s = h && n ? h->show(c.integer(first)) : nullptr;
        if (!s) {
            return false;
        }
        std::vector<RabbitHub::Mark> marks(32);
        for (const auto& st : s->steps) {
            if (st.trick > 0 && st.slot >= 0 && st.slot < 32) {
                marks[std::size_t(st.slot)].mark = 1;
            }
        }
        for (const auto& e : events_) {
            if (e.motion.empty() || e.step < 0 || e.step >= 32) {
                continue;
            }
            const TrickMotion* tm = h->trickMotionNamed(e.motion);
            for (int i = 0; tm && i < h->trickCount(); ++i) {
                if (h->trickAt(i)->description == tm->description) {
                    marks[std::size_t(e.step)].trick = h->trickAt(i)->id;
                    break;
                }
            }
        }
        marks.resize(std::size_t(std::max(0, s->slots)));
        h->setJudgement(true, std::move(marks));
        return true;
    }
    case 743: // {-> replay}: the show's number (a replay is marked)
        if (n == 0) {
            return false;
        }
        if (number_ > 99) {
            number_ -= 99;
            put(c, first, Value::ofInt(1));
        }
        return result(c, number_);
    case 798: // {on}: the events' own motions (replays)
        if (n == 0) {
            return false;
        }
        replayMotions_ = c.at(first).truthy();
        return result(c, 1);
    case 799: // the show's sky and lights take over the top screen (0x303a50)
        lightsOn_ = true;
        directional_.set({1});
        spot_.set({0});
        return true;
    default:
        return RabbitPart::command(number, c, first);
    }
}

} // namespace

std::unique_ptr<Loop> makeShow(Story& story, int id, std::vector<Value> values) {
    return std::make_unique<RabbitShow>(story, id, std::move(values));
}

} // namespace picarat::adventure5::rabbit
