// The third adventure's toy car (see a3_car.hpp).
#include "minigames/a3_car.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

namespace picarat::adventure3 {

namespace {
using lscr::fx::kOne;
using Vec3 = std::array<int, 3>; // fixed point, in cells (z up)

// Overlay 20's names (lt3CLoopMiniCarBase).
constexpr int kBase = 20;
constexpr std::uint32_t kDataFile = 0x0211ef58;   // rc/mode/lt3_mini_car.cbin
constexpr std::uint32_t kTextTable = 0x0211ef74;  // lt3_car (txt/?/40 part 7200)
constexpr std::uint32_t kDirections = 0x0211ef48; // 右 下 左 上, four bytes apart
constexpr int kTextFile = 40, kTextPart = 7200;

// "mini/%s" + ".cani": a course's sprite bank (0203fee4 appends).
std::string bankPath(const Story& story, std::uint32_t format, std::uint32_t suffix, int overlay,
                     const std::string& bank) {
    char out[64];
    std::snprintf(out, sizeof out, story.codeName(format, overlay).text.c_str(), bank.c_str());
    return out + story.codeName(suffix, overlay).text;
}

// A sprite the loop draws itself (the overlay's 0211f060 / 0211f0b0 /
// 0211f108 / 0211f180): a sequence of one of the scene's animations, its
// time and length (fixed point frames), held at the end or looping.
struct Sprite {
    const Animation* animation = nullptr;
    const Animation::Sequence* sequence = nullptr;
    int time = 0, length = 0;
    bool hold = false;

    bool valid() const { return sequence != nullptr; }

    bool ended() const { return length <= time; } // 0211f08c

    // 0211f0b0: the sequence by name, from the start; none if it is missing.
    bool play(const Animation* a, const std::string& name, bool holdAtEnd = false) {
        *this = {};
        if (!a || name.empty()) {
            return false;
        }
        const auto* s = a->sequence(name);
        if (!s) {
            return false;
        }
        animation = a;
        sequence = s;
        for (const auto& st : s->steps) {
            length += int(st.duration) * kOne;
        }
        hold = holdAtEnd;
        return true;
    }

    // 0211f108.
    void step(int elapsed) {
        if (!valid() || length < 1) {
            return;
        }
        time += elapsed;
        if (hold) {
            time = std::min(time, length);
            return;
        }
        while (time >= length) {
            time -= length;
        }
    }

    // 0211f180 -> 02056acc: the frame due now with its anchor at (x, y).
    void draw(Surface& screen, int x, int y, int scale = kOne) const {
        if (!valid() || sequence->steps.empty()) {
            return;
        }
        const int t = time >> 12;
        const Animation::Step* st = &sequence->steps.back();
        int acc = 0;
        for (const auto& s : sequence->steps) {
            acc += int(s.duration);
            if (t < acc) {
                st = &s;
                break;
            }
        }
        const int fx = st->x - sequence->originX, fy = st->y - sequence->originY;
        if (scale == kOne) {
            animation->drawFrame(screen, st->frame, x + fx, y + fy);
            return;
        }
        // Scaled about the anchor (the car on the map sinking into water).
        Surface frame{128, 128, std::vector<std::uint16_t>(128 * 128, 0)};
        animation->drawFrame(frame, st->frame, 64 + fx, 64 + fy);
        for (int dy = -64; dy < 64; ++dy) {
            for (int dx = -64; dx < 64; ++dx) {
                const int sx = 64 + int((std::int64_t(dx) << 12) / scale),
                          sy = 64 + int((std::int64_t(dy) << 12) / scale);
                if (sx < 0 || sy < 0 || sx >= 128 || sy >= 128) {
                    continue;
                }
                const std::uint16_t px = frame.pixels[size_t(sy * 128 + sx)];
                const int ox = x + dx, oy = y + dy;
                if ((px & 0x8000) && ox >= 0 && oy >= 0 && ox < int(screen.width) &&
                    oy < int(screen.height)) {
                    screen.pixels[size_t(oy * int(screen.width) + ox)] = px;
                }
            }
        }
    }
};

const Animation* animationOf(const lscr::Scene& scene, const std::string& resource) {
    const auto* r = scene.resource(resource);
    return r ? r->animation : nullptr;
}

int round12(int v) {
    return (v + 0x800) >> 12;
}

// A value of the script as fixed point (0203e1b4 reads it as a float, then
// 02024098 / 02023f2c times 4096).
int fixedArg(const lscr::Call& c, std::size_t i) {
    return i < c.count() ? lscr::fx::fromFloat(c.real(i)) : 0;
}

void putFloat(lscr::Call& c, std::size_t i, int fixed) {
    if (i < c.count()) {
        if (lscr::Value* v = c.variable(i)) {
            v->get() = lscr::Value::ofFloat(float(fixed) / 4096.0f);
        }
    }
}

void putInt(lscr::Call& c, std::size_t i, int value) {
    if (i < c.count()) {
        if (lscr::Value* v = c.variable(i)) {
            v->get() = lscr::Value::of(value);
        }
    }
}
} // namespace

// --- The course data -----------------------------------------------------------------

const CarData::Tile* CarData::tile(const std::string& name) const {
    const int i = tileIndex(name);
    return i < 0 ? nullptr : &tiles[size_t(i)];
}

int CarData::tileIndex(const std::string& name) const {
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        if (tiles[i].name == name) {
            return int(i);
        }
    }
    return -1;
}

const CarData::Piece* CarData::pieceById(int id) const { // 0211e54c
    if (id < 0) {
        return nullptr;
    }
    for (const auto& p : pieces) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

const CarData& CarData::get(const Story& story) {
    static std::map<const Assets*, CarData> cache;
    if (auto it = cache.find(&story.assets); it != cache.end()) {
        return it->second;
    }
    CarData d;
    const auto file = story.assets.script(story.codeName(kDataFile, kBase).text);
    std::array<std::string, 4> directions;
    for (int i = 0; i < 4; ++i) {
        directions[size_t(i)] = story.codeName(kDirections + 4 * std::uint32_t(i), kBase).text;
    }
    Course* course = nullptr;
    int nextSlot = 0;
    for (const auto& c : file.commands) {
        auto text = [&](std::size_t i) {
            return i < c.operands.size() && c.operands[i].type == 2 ? file.text(c.operands[i])
                                                                    : std::string();
        };
        auto num = [&](std::size_t i) {
            if (i >= c.operands.size()) {
                return 0;
            }
            const auto& o = c.operands[i];
            if (o.type == 1) {
                float f;
                std::memcpy(&f, &o.value, 4);
                return int(f);
            }
            return int(std::int32_t(o.value));
        };
        switch (c.opcode) {
        case 4310: // 0211dd74
            if (c.operands.size() >= 4) {
                d.pieces.push_back({num(0), text(1), text(2), text(3)});
            }
            break;
        case 4311: // 0211de50
            if (c.operands.size() >= 9) {
                d.tiles.push_back({text(0), num(1) != 0, text(2), text(3), num(4), text(5), text(6),
                                   text(7), text(8)});
            }
            break;
        case 4320: { // 0211e014
            if (c.operands.size() < 10) {
                break;
            }
            Course k;
            k.name = text(0);
            k.width = num(1);
            k.height = num(2);
            k.startX = num(3);
            k.startY = num(4);
            if (c.operands[5].type == 2) {
                const std::string dir = text(5);
                for (int i = 0; i < 4; ++i) {
                    if (dir == directions[size_t(i)]) {
                        k.direction = i;
                    }
                }
            } else {
                k.direction = num(5);
            }
            k.extra = {num(6), num(7)};
            k.placeBank = text(8);
            k.runBank = text(9);
            d.courses.push_back(std::move(k));
            course = &d.courses.back();
            break;
        }
        case 4322: // 0211e334: the course's slots follow the earlier ones'
            if (course && !c.operands.empty()) {
                course->firstSlot = nextSlot;
                course->pieceTotal = num(0);
                nextSlot += course->pieceTotal + 1;
                for (std::size_t i = 1; i < c.operands.size() && i <= 5; ++i) {
                    course->pieceCounts.push_back(num(i));
                }
            }
            break;
        case 4323: // 0211e3d4: a row
            if (course) {
                for (std::size_t i = 0; i < c.operands.size(); ++i) {
                    course->tiles.push_back(d.tileIndex(text(i)));
                }
            }
            break;
        case 4321:
            course = nullptr;
            break;
        default:
            break;
        }
    }
    for (auto& k : d.courses) {
        k.tiles.resize(std::size_t(std::max(0, k.width * k.height)), -1);
    }
    return cache.emplace(&story.assets, std::move(d)).first->second;
}

namespace {
const CarData::Course* courseAt(const Story& story, int index) { // 0211e6a0
    const auto& d = CarData::get(story);
    return index >= 0 && index < int(d.courses.size()) ? &d.courses[size_t(index)] : nullptr;
}

// The scene and its program, as the minigame loops start them (the course
// text table lt3_car with them, 0211e6b4 -> 020f1ed4).
std::unique_ptr<lscr::Scene> carScene(Story& story, std::unique_ptr<lscr::Scene> scene,
                                      const std::string& program) {
    scene->load(program);
    for (const auto& f : Story::residentScripts()) {
        scene->load(f);
    }
    scene->loadTextTable(story.codeName(kTextTable, kBase).text, kTextFile, kTextPart);
    return scene;
}
} // namespace

// --- MINI_CAR_SLCT (overlay 21) ---------------------------------------------------

namespace {
constexpr int kSelect = 21;
constexpr std::uint32_t kSelectProgram = 0x0211fa44; // evt/prg/lt3_mini_car_slct.cbin
constexpr std::uint32_t kSelectMarks = 0x0211fa2c;   // anm_stt
constexpr std::uint32_t kSelectFormat = 0x0211fa34, kSelectSuffix = 0x0211fa3c;
} // namespace

class CarSelectLoop::Engine final : public lscr::SceneEngine {
public:
    explicit Engine(Story& story) : story_(story) {}

    void command(lscr::Scene& scene, int hook, lscr::Call&) override {
        if (hook != 0) {
            return;
        }
        // 0211f0f8 (1280): the picked course's sprites for the upper screen.
        const std::string marks = story_.codeName(kSelectMarks, kSelect).text;
        scene.freeResource(marks);
        if (const auto* c = picked(scene)) {
            scene.loadAnimation(
                marks, 2, bankPath(story_, kSelectFormat, kSelectSuffix, kSelect, c->placeBank));
        }
    }

    void draw(const lscr::Scene& scene, Surface& screen, lscr::Screen which) const override {
        // 0211f3d8 (vtable 0x100): the course's flags on its map, from
        // 0211f378's corner ((12 - width) * 8 + 40, (9 - height) * 8 + 44).
        if (which != lscr::Screen::Upper) {
            return;
        }
        const auto* c = picked(scene);
        const auto* a = animationOf(scene, story_.codeName(kSelectMarks, kSelect).text);
        if (!c || !a) {
            return;
        }
        const auto& data = CarData::get(story_);
        const int left = (12 - c->width) * 8 + 40, top = (9 - c->height) * 8 + 44;
        for (int y = 0; y < c->height; ++y) {
            for (int x = 0; x < c->width; ++x) {
                const int t = c->tiles[size_t(y * c->width + x)];
                if (t < 0) {
                    continue;
                }
                Sprite s;
                if (s.play(a, data.tiles[size_t(t)].mark)) {
                    s.draw(screen, left + x * 16, top + y * 16);
                }
            }
        }
    }

private:
    const CarData::Course* picked(const lscr::Scene& scene) const {
        // The course is the scene thread's variable 200 (020f1a9c(loop, 0)).
        auto& s = const_cast<lscr::Scene&>(scene);
        const lscr::Value* v = s.sceneVariable(0);
        return v ? courseAt(story_, v->toInt()) : nullptr;
    }

    Story& story_;
};

CarSelectLoop::CarSelectLoop(Story& story, LoopArgs args) : Loop(story, "MINI_CAR_SLCT", args) {}

CarSelectLoop::~CarSelectLoop() = default;

void CarSelectLoop::activate() {
    // 0211f1a8 (start) and 0211f290 (uncovered: MAIN again).
    scene_ = carScene(story_, makeScene(), story_.codeName(kSelectProgram, kSelect).text);
    engine_ = std::make_unique<Engine>(story_);
    scene_->engine = engine_.get();
    state_ = State::Loading;
}

void CarSelectLoop::deactivate() {
    if (scene_) {
        scene_->engine = nullptr;
    }
    scene_.reset();
    engine_.reset();
    state_ = State::Idle;
}

bool CarSelectLoop::busy() const {
    return state_ == State::Loading || Loop::busy();
}

void CarSelectLoop::frame(const lscr::SceneInput& input) {
    if (!scene_) {
        return;
    }
    if (state_ == State::Finished) {
        return;
    }
    if (state_ == State::Loading) {
        // 0211f308: MAIN, the course variable (200) -1 at first (020f1b08).
        const std::vector<lscr::Value> vars{lscr::Value::of(-1)};
        scene_->start("MAIN", started_ ? std::span<const lscr::Value>{} : vars);
        started_ = true;
        state_ = State::Running;
    }
    if (state_ == State::Running && !scene_->running()) {
        // 0211f2cc: MAIN over - the menu ends (unless the script asked).
        state_ = State::Finished;
        if (!story_.requestsPending()) {
            story_.request(Story::Request::Pop);
        }
        return;
    }
    scene_->frame(input);
}

lscr::Result CarSelectLoop::command(int index, lscr::Call& call) {
    if (scene_ && engine_) {
        engine_->command(*scene_, index, call);
    }
    return lscr::Result::Next;
}

// --- MINI_CAR_EDIT (overlay 22) ---------------------------------------------------

namespace {
constexpr int kEdit = 22;
constexpr std::uint32_t kEditProgram = 0x021202d8; // evt/prg/lt3_mini_car_edit.cbin
constexpr std::uint32_t kEditBank = 0x021202d0;    // anm_obj
constexpr std::uint32_t kEditFormat = 0x021202c0, kEditSuffix = 0x021202c8;
constexpr std::uint32_t kPointName = 0x02120264;     // p_%02d_%02d (x, y)
constexpr std::uint32_t kPieceName = 0x02120270;     // i_%02d_%02d (kind, number)
constexpr std::uint32_t kPieceResource = 0x0212027c; // anm_cmn
constexpr std::uint32_t kPieceIdle = 0x02120284, kPieceMove = 0x0212028c, kPieceDrop = 0x02120294;
constexpr std::uint32_t kPlacements = 0x0212029c; // ＜車＞アイテム配置
constexpr std::uint32_t kCarObject = 0x021202b0;  // obj_car
constexpr std::uint32_t kCarFacing = 0x021202b8;  // str%02d
// The pieces' home points and counters (0x021200d4 / 0x021200c0 tables).
constexpr std::array<std::uint32_t, 5> kPieceHomes{0x02120048, 0x02120040, 0x02120000, 0x02120030,
                                                   0x02120008};
constexpr std::array<std::uint32_t, 5> kPieceCounters{0x02120028, 0x02120018, 0x02120038,
                                                      0x02120020, 0x02120010};

std::string format2(const Story& story, std::uint32_t address, int overlay, int a, int b) {
    char out[32];
    std::snprintf(out, sizeof out, story.codeName(address, overlay).text.c_str(), a, b);
    return out;
}
} // namespace

class CarEditLoop::Engine final : public lscr::SceneEngine {
public:
    Engine(Story& story, CarEditLoop& loop, const CarData::Course& course)
        : story_(story), loop_(loop), course_(course) {
        left_ = (12 - course.width) * 8 + 24; // 0211f684
        top_ = (9 - course.height) * 8 + 24;
    }

    int left() const { return left_; }

    int top() const { return top_; }

    void command(lscr::Scene& scene, int hook, lscr::Call& c) override {
        switch (hook) {
        case 0:
            setup(scene);
            break;
        case 1: // 0211f53c (1281): the car's place and direction {[x], [y], [direction]}
            putInt(c, 0, left_ + course_.startX * 16);
            putInt(c, 1, top_ + course_.startY * 16);
            putInt(c, 2, loop_.direction_);
            break;
        case 2: // 0211f5ec (1282): turn the car {direction}
            if (c.count() > 0) {
                loop_.direction_ = c.integer(0);
                if (lscr::Object* car = scene.find(story_.codeName(kCarObject, kEdit).text)) {
                    char seq[16];
                    std::snprintf(seq, sizeof seq, story_.codeName(kCarFacing, kEdit).text.c_str(),
                                  loop_.direction_);
                    lscr::play(*car, lscr::sequenceOf(*car, lscr::Value::ofText(seq)), 0);
                }
            }
            break;
        default:
            break;
        }
    }

    void draw(const lscr::Scene& scene, Surface& screen, lscr::Screen which) const override {
        // 0211fb7c: every cell seen from above, then its flag.
        if (which != lscr::Screen::Lower) {
            return;
        }
        const auto* a = animationOf(scene, story_.codeName(kEditBank, kEdit).text);
        const auto& data = CarData::get(story_);
        for (int y = 0; y < course_.height; ++y) {
            for (int x = 0; x < course_.width; ++x) {
                const int t = course_.tiles[size_t(y * course_.width + x)];
                if (t < 0) {
                    continue;
                }
                for (const std::string* seq :
                     {&data.tiles[size_t(t)].flat, &data.tiles[size_t(t)].mark}) {
                    Sprite s;
                    if (s.play(a, *seq)) {
                        s.draw(screen, left_ + x * 16, top_ + y * 16);
                    }
                }
            }
        }
    }

private:
    void setup(lscr::Scene& scene) {
        // 0211f110 (1280): the car's direction as last left, a point on each
        // cell a piece may go on, and the pieces, each on its cell or at
        // its home.
        const auto& data = CarData::get(story_);
        const lscr::Value placements = story_.codeName(kPlacements, kEdit);
        loop_.direction_ = course_.direction;
        for (int y = 0; y < course_.height; ++y) {
            for (int x = 0; x < course_.width; ++x) {
                const int t = course_.tiles[size_t(y * course_.width + x)];
                if (t >= 0 && data.tiles[size_t(t)].placeable) {
                    scene.point(format2(story_, kPointName, kEdit, x, y)).local = {
                        (left_ + x * 16) * kOne, (top_ + y * 16) * kOne};
                }
            }
        }
        int slot = course_.firstSlot;
        for (std::size_t kind = 0; kind < course_.pieceCounts.size() && kind < kPieceHomes.size();
             ++kind) {
            const std::string home = story_.codeName(kPieceHomes[kind], kEdit).text;
            const std::string counter = story_.codeName(kPieceCounters[kind], kEdit).text;
            for (int n = 0; n < course_.pieceCounts[kind]; ++n, ++slot) {
                // 020f29d8: a piece (as 4021) with the script's sections
                // MOV_N / MOV_M / MOV_R and the values {home, counter}.
                const std::string name = format2(story_, kPieceName, kEdit, int(kind), n);
                lscr::Object* o = scene.create(lscr::Object::Kind::Button, name,
                                               story_.codeName(kPieceResource, kEdit).text);
                if (!o) {
                    continue;
                }
                const std::vector<lscr::Value> values{lscr::Value::ofText(home),
                                                      lscr::Value::ofText(counter)};
                scene.objectThread(*o, values);
                o->style = lscr::Object::Style::Piece;
                o->releasedSection = story_.codeName(kPieceIdle, kEdit).text;
                o->touchedSection = story_.codeName(kPieceMove, kEdit).text;
                o->clickedSection = story_.codeName(kPieceDrop, kEdit).text;
                lscr::play(*o, lscr::sequenceOf(*o, lscr::Value::ofText(home)), 0);
                o->touch = 0;
                scene.setLayer(*o,
                               0); // 020f2474 (as 4112; the store at +1000 is not reconstructed)
                // 020b43c4: on its point (offset 0, 0) - its cell, else its home.
                const int cell = story_.state.variable(placements, slot);
                scene.linkTo(*o, cell < 1 ? home
                                          : format2(story_, kPointName, kEdit,
                                                    (cell - 1) % course_.width,
                                                    (cell - 1) / course_.width));
                o->position.set({0, 0});
                scene.callOn(*o, o->releasedSection); // 020afb88: MOV_N counts it in
            }
        }
        const int car = story_.state.variable(placements, slot);
        if (car - 1 >= 0 && car - 1 < 4) {
            loop_.direction_ = car - 1;
        }
    }

    Story& story_;
    CarEditLoop& loop_;
    const CarData::Course& course_;
    int left_ = 0, top_ = 0;
};

CarEditLoop::CarEditLoop(Story& story, LoopArgs args) : Loop(story, "MINI_CAR_EDIT", args) {}

CarEditLoop::~CarEditLoop() = default;

void CarEditLoop::activate() {
    // 0211f684: the course (the request's first argument), its sprites, the program.
    const auto* course = courseAt(story_, args_[0]);
    if (!course) {
        story_.request(Story::Request::Pop);
        return;
    }
    scene_ = carScene(story_, makeScene(), story_.codeName(kEditProgram, kEdit).text);
    scene_->loadAnimation(story_.codeName(kEditBank, kEdit).text, 0,
                          bankPath(story_, kEditFormat, kEditSuffix, kEdit, course->placeBank));
    engine_ = std::make_unique<Engine>(story_, *this, *course);
    scene_->engine = engine_.get();
    state_ = State::Loading;
}

void CarEditLoop::keep() {
    // 0211f830: each piece's cell (+ 1) from the point it is linked to,
    // "p_XX_YY", 0 elsewhere; then the car's direction + 1.
    const auto* course = courseAt(story_, args_[0]);
    if (!scene_ || !course) {
        return;
    }
    const lscr::Value placements = story_.codeName(kPlacements, kEdit);
    int slot = course->firstSlot;
    auto digit = [](char c) {
        return c >= '0' && c <= '9' ? c - '0' : 0;
    };
    for (std::size_t kind = 0; kind < course->pieceCounts.size(); ++kind) {
        for (int n = 0; n < course->pieceCounts[kind]; ++n, ++slot) {
            int value = 0;
            if (const lscr::Object* o =
                    scene_->find(format2(story_, kPieceName, kEdit, int(kind), n))) {
                const std::string& p = o->link;
                if (p.size() >= 7 && p[0] == 'p') {
                    const int x = digit(p[2]) * 10 + digit(p[3]),
                              y = digit(p[5]) * 10 + digit(p[6]);
                    value = y * course->width + x + 1;
                }
            }
            story_.state.setVariable(placements, value, slot, true);
        }
    }
    story_.state.setVariable(placements, direction_ + 1, slot, true);
}

void CarEditLoop::deactivate() {
    keep();
    if (scene_) {
        scene_->engine = nullptr;
    }
    scene_.reset();
    engine_.reset();
    state_ = State::Idle;
}

void CarEditLoop::end() {
    keep();
}

bool CarEditLoop::busy() const {
    return state_ == State::Loading || Loop::busy();
}

void CarEditLoop::frame(const lscr::SceneInput& input) {
    if (!scene_) {
        return;
    }
    if (state_ == State::Finished) {
        return;
    }
    if (state_ == State::Loading) {
        // 0211fb00: MAIN once everything has loaded.
        const std::vector<lscr::Value> vars{lscr::Value::of(args_[0]), lscr::Value::of(args_[1]),
                                            lscr::Value::of(args_[2]), lscr::Value::of(args_[3])};
        scene_->start("MAIN", vars);
        state_ = State::Running;
    }
    if (state_ == State::Running && !scene_->running()) {
        // 0211fac4: MAIN over - the loop ends (unless the script asked).
        state_ = State::Finished;
        if (!story_.requestsPending()) {
            story_.request(Story::Request::Pop);
        }
        return;
    }
    scene_->frame(input);
}

lscr::Result CarEditLoop::command(int index, lscr::Call& call) {
    if (scene_ && engine_) {
        engine_->command(*scene_, index, call);
    }
    return lscr::Result::Next;
}

// --- MINI_CAR_GAME (overlay 23) ---------------------------------------------------

namespace {
constexpr int kGame = 23;
constexpr std::uint32_t kGameProgram = 0x0212338c; // evt/prg/lt3_mini_car_game.cbin
constexpr std::uint32_t kGameMarks = 0x0212337c;   // anm_stt (upper: the map's flags and car)
constexpr std::uint32_t kGameBank = 0x02123384;    // anm_obj (lower: the course)
constexpr std::uint32_t kGameFormat = 0x0212336c, kGameSuffix = 0x02123374;
constexpr std::uint32_t kGamePlacements = 0x021233b4; // ＜車＞アイテム配置
constexpr std::uint32_t kOffCourse = 0x021233c8;      // GAME_OVER_EMPTY
constexpr std::uint32_t kCarMark = 0x021233d8;        // car
constexpr std::uint32_t kCarFacingGame = 0x0212335c;  // car%02d
constexpr std::uint32_t kFlagObject = 0x02123364;     // obj01
constexpr int kCamera = -20, kCar = -10;              // the script's names for them
} // namespace

class CarGameLoop::Engine final : public lscr::SceneEngine {
public:
    Engine(Story& story, const CarData::Course& course)
        : story_(story), data_(CarData::get(story)), course_(course), cells_(course.tiles.size()) {
        left_ = (12 - course.width) * 8 + 40; // 02121928
        top_ = (9 - course.height) * 8 + 44;
    }

    // 02121b48: the course laid out once the resources are in.
    void layout(lscr::Scene& scene) {
        lower_ = animationOf(scene, story_.codeName(kGameBank, kGame).text);
        upper_ = animationOf(scene, story_.codeName(kGameMarks, kGame).text);
        for (int y = 0; y < course_.height; ++y) {
            for (int x = 0; x < course_.width; ++x) {
                Cell& c = cells_[size_t(y * course_.width + x)];
                setTile(c, course_.tiles[size_t(y * course_.width + x)]);
                c.position.set({x * kOne, y * kOne, 0});
            }
        }
        const Vec3 start{course_.startX * kOne, course_.startY * kOne, 0};
        camera_.set(start);
        car_.position.set(start);
        car_.offset.set({0, 0, 0});
        // The pieces where the placement left them.
        const lscr::Value placements = story_.codeName(kGamePlacements, kGame);
        int slot = course_.firstSlot;
        for (std::size_t kind = 0; kind < course_.pieceCounts.size() && kind < data_.pieces.size();
             ++kind) {
            for (int n = 0; n < course_.pieceCounts[kind]; ++n, ++slot) {
                const int v = story_.state.variable(placements, slot);
                if (v > 0 && v - 1 < int(cells_.size())) {
                    setPiece(cells_[size_t(v - 1)], &data_.pieces[kind]);
                }
            }
        }
        face(story_.state.variable(placements, slot) - 1);
    }

    void command(lscr::Scene& scene, int hook, lscr::Call& c) override {
        result = lscr::Result::Next;
        switch (hook) {
        case 0: // 0211fc88 (1280): where {id, [x], [y], [z], [direction]}
            if (c.count() > 0) {
                const int id = c.integer(0);
                Vec3 p{};
                int dir = 0;
                if (id == kCamera) {
                    p = camera_.value;
                } else if (Body* b = body(id)) {
                    p = b->position.value;
                    dir = b->direction;
                }
                putFloat(c, 1, p[0]);
                putFloat(c, 2, p[1]);
                putFloat(c, 3, p[2]);
                putInt(c, 4, dir);
            }
            break;
        case 1: // 0211fe48 (1281): place {id, x, y, z}
            if (c.count() >= 4) {
                const Vec3 p{fixedArg(c, 1), fixedArg(c, 2), fixedArg(c, 3)};
                if (c.integer(0) == kCamera) {
                    camera_.set(p);
                } else if (Body* b = body(c.integer(0))) {
                    b->position.set(p);
                }
            }
            break;
        case 2: // 0212008c (1282): move to {id, x, y, z, frames, curve}
        case 3: // 02120540 (1283): move by {id, x, y, z, frames, curve}
            if (c.count() >= 6) {
                Vec3 p{fixedArg(c, 1), fixedArg(c, 2), fixedArg(c, 3)};
                auto& tween = c.integer(0) == kCamera ? camera_ : bodyPosition(c.integer(0));
                if (&tween == &dummy_) {
                    break;
                }
                if (hook == 3) {
                    for (int i = 0; i < 3; ++i) {
                        p[size_t(i)] += tween.value[size_t(i)];
                    }
                }
                tween.to(c.integer(5), p, fixedArg(c, 4));
            }
            break;
        case 4: // 02120a80 (1284): offset {id, x, y, z}
            if (c.count() >= 4) {
                if (Body* b = body(c.integer(0))) {
                    b->offset.set({fixedArg(c, 1), fixedArg(c, 2), fixedArg(c, 3)});
                }
            }
            break;
        case 5: // 02120c14 (1285): offset to {id, x, y, z, frames, curve}
        case 6: // 02120f38 (1286): offset by {id, x, y, z, frames, curve}
            if (c.count() >= 6) {
                if (Body* b = body(c.integer(0))) {
                    Vec3 p{fixedArg(c, 1), fixedArg(c, 2), fixedArg(c, 3)};
                    if (hook == 6) {
                        for (int i = 0; i < 3; ++i) {
                            p[size_t(i)] += b->offset.value[size_t(i)];
                        }
                    }
                    b->offset.to(c.integer(5), p, fixedArg(c, 4));
                }
            }
            break;
        case 7: // 021212b4 (1287): the car faces {-10, direction}
            if (c.count() >= 2 && c.integer(0) == kCar) {
                face(c.integer(1));
            }
            break;
        case 8: // 0212130c (1288): sequence {id, name, [held at the end]}
            if (c.count() >= 2) {
                const bool hold = c.count() >= 3 && c.integer(2) != 0;
                if (c.integer(0) == kCar) {
                    car_.sprite.play(lower_, c.text(1), hold);
                } else if (Cell* cell = cellAt(c.integer(0))) {
                    cell->ground.play(lower_, c.text(1), hold);
                }
            }
            break;
        case 9: // 021213d8 (1289): what the car's cell does - its piece, else its kind
            if (Cell* cell = cellAt(carX_, carY_)) {
                if (cell->piece) {
                    const std::string section = cell->piece->section;
                    setPiece(*cell, nullptr);
                    call(scene, section);
                    result = lscr::Result::Called;
                } else if (cell->tile) {
                    call(scene, cell->tile->act);
                    result = lscr::Result::Called;
                }
            }
            break;
        case 10: // 02121470 (1290): a cell's kind {x, y, name}
            if (c.count() >= 3) {
                if (Cell* cell = cellAt(c.integer(0), c.integer(1))) {
                    setTile(*cell, data_.tileIndex(c.text(2)));
                }
            }
            break;
        case 11: // 021214e8 (1291): a cell's piece {x, y, id}
            if (c.count() >= 3) {
                if (Cell* cell = cellAt(c.integer(0), c.integer(1))) {
                    setPiece(*cell, data_.pieceById(c.integer(2)));
                }
            }
            break;
        case 12: // 02121568 (1292): a cell's number {variable, x, y} (-1 off the course)
            if (c.count() >= 3) {
                putInt(c, 0, index(c.integer(1), c.integer(2)));
            }
            break;
        case 13: { // 021215cc (1293): kinds turn into the next named {name, name...}
            if (c.count() < 2) {
                break;
            }
            for (Cell& cell : cells_) {
                if (!cell.tile) {
                    continue;
                }
                for (std::size_t i = 0; i < c.count(); ++i) {
                    if (cell.tile->name == c.text(i)) {
                        setTile(cell, data_.tileIndex(c.text((i + 1) % c.count())));
                        break;
                    }
                }
            }
            break;
        }
        default:
            break;
        }
    }

    bool condition(int index, std::span<lscr::Value> a) const {
        switch (index) {
        case 0: { // 021216b8 (2210): moving {[id]}
            if (a.empty()) {
                if (camera_.active() || car_.moving()) {
                    return true;
                }
                return std::any_of(cells_.begin(), cells_.end(),
                                   [](const Cell& c) { return c.moving(); });
            }
            const int id = a[0].toInt();
            if (id == kCamera) {
                return camera_.active();
            }
            const Body* b = const_cast<Engine*>(this)->body(id);
            return b && b->moving();
        }
        case 1: { // 021217b4 (2211): the sequence has ended {id}
            if (a.empty()) {
                return false;
            }
            const int id = a[0].toInt();
            if (id == kCar) {
                return car_.sprite.ended();
            }
            const Cell* cell = const_cast<Engine*>(this)->cellAt(id);
            return cell && cell->ground.ended();
        }
        case 2: { // 02121834 (2212): no flag is left on the course
            const std::string flag = story_.codeName(kFlagObject, kGame).text;
            return std::none_of(cells_.begin(), cells_.end(),
                                [&](const Cell& c) { return c.tile && c.tile->object == flag; });
        }
        case 3: { // 0212189c (2213): a cell's kind is {x, y, name}
            if (a.size() < 3) {
                return false;
            }
            const Cell* cell = const_cast<Engine*>(this)->cellAt(a[0].toInt(), a[1].toInt());
            return cell && cell->tile && cell->tile->name == a[2].toText();
        }
        default:
            return false;
        }
    }

    void update(lscr::Scene& scene) override {
        // 021220f4 (vtable 0xec): unless the script pauses (its variable 15),
        // everything moves on a frame; the car on the ground takes its
        // cell's height and, on a new cell, starts the cell's section; the
        // camera follows the car.
        if (const lscr::Value* paused = scene.sceneVariable(15); paused && paused->toInt() != 0) {
            return;
        }
        for (Cell& c : cells_) {
            c.step(kOne);
        }
        car_.step(kOne);
        const int beforeX = carX_, beforeY = carY_;
        carHeight_ = 0;
        if (car_.position.value[2] + car_.offset.value[2] < 1) {
            carX_ = round12(car_.position.value[0]);
            carY_ = round12(car_.position.value[1]);
            const bool moved = beforeX != carX_ || beforeY != carY_;
            Cell* cell = cellAt(carX_, carY_);
            if (!cell || !cell->tile) {
                if (moved) {
                    scene.restart(story_.codeName(kOffCourse, kGame).text); // 020f1988
                }
            } else {
                if (moved) {
                    call(scene, cell->tile->enter); // 020f1a10
                }
                if (cell->tile->height >= 0) {
                    carHeight_ = cell->tile->height;
                }
            }
        }
        camera_.set(car_.position.value);
    }

    void draw(const lscr::Scene&, Surface& screen, lscr::Screen which) const override {
        if (which == lscr::Screen::Lower) {
            drawCourse(screen);
        } else {
            drawMap(screen);
        }
    }

    lscr::Result result = lscr::Result::Next;

private:
    // An object of the course (0211f5fc): a sprite, where it is and how far
    // it is lifted off that (both tweened), the way it faces.
    struct Body {
        Sprite sprite;
        lscr::Tween<3> position, offset;
        int direction = 0;

        bool moving() const { return position.active() || offset.active(); } // 0211f67c

        void step(int elapsed) { // 0211f6b4
            sprite.step(elapsed);
            position.step(elapsed);
            offset.step(elapsed);
        }

        Vec3 at() const {
            return {position.value[0] + offset.value[0], position.value[1] + offset.value[1],
                    position.value[2] + offset.value[2]};
        }
    };

    // A cell (02121fe8, 0xb8 bytes): its kind's object as the body's sprite,
    // the ground and the piece on it.
    struct Cell : Body {
        const CarData::Tile* tile = nullptr;
        const CarData::Piece* piece = nullptr;
        Sprite ground, pieceSprite;

        void step(int elapsed) { // 0211f984
            Body::step(elapsed);
            ground.step(elapsed);
            pieceSprite.step(elapsed);
        }
    };

    void call(lscr::Scene& scene, const std::string& section) const { // 020f1a10 / 020f1a28
        if (section.empty()) {
            return;
        }
        if (lscr::Thread* t = scene.mainThread()) {
            t->call(section, {});
        }
    }

    void setTile(Cell& c, int tile) { // 0211fbb8
        c.tile = tile >= 0 && tile < int(data_.tiles.size()) ? &data_.tiles[size_t(tile)] : nullptr;
        c.sprite = {};
        c.ground = {};
        if (c.tile) {
            c.sprite.play(lower_, c.tile->object);
            c.ground.play(lower_, c.tile->ground);
        }
    }

    void setPiece(Cell& c, const CarData::Piece* p) { // 0211fc34
        c.piece = p;
        c.pieceSprite = {};
        if (p) {
            c.pieceSprite.play(lower_, p->sequence);
        }
    }

    void face(int direction) { // 0211f8d0
        if (direction < 0 || direction > 3) {
            direction = 0;
        }
        car_.direction = direction;
        char seq[16];
        std::snprintf(seq, sizeof seq, story_.codeName(kCarFacingGame, kGame).text.c_str(),
                      direction);
        car_.sprite.play(lower_, seq);
    }

    int index(int x, int y) const { // 0211fb54
        if (x < 0 || y < 0 || x >= course_.width || y >= course_.height) {
            return -1;
        }
        return y * course_.width + x;
    }

    Cell* cellAt(int x, int y) {
        const int i = index(x, y);
        return i < 0 ? nullptr : &cells_[size_t(i)];
    }

    Cell* cellAt(int i) { // 0211fb8c
        return i >= 0 && i < int(cells_.size()) ? &cells_[size_t(i)] : nullptr;
    }

    Body* body(int id) {
        if (id == kCar) {
            return &car_;
        }
        return cellAt(id);
    }

    lscr::Tween<3>& bodyPosition(int id) {
        Body* b = body(id);
        return b ? b->position : dummy_;
    }

    // 0212266c (vtable 0xf8): the oblique view. A cell is 22 pixels right
    // and 11 down along x, 17 left and 17 down along y, 24 up along z; depth
    // falls by 11, 17 and 12. The camera's place is at the screen's middle.
    struct Projected {
        int x = 0, y = 0, depth = 0;
    };

    Projected project(const Vec3& p) const {
        const Vec3 d{p[0] - camera_.value[0], p[1] - camera_.value[1], p[2] - camera_.value[2]};
        auto m = [](int a, int b) {
            return lscr::fx::mul(a, b);
        };
        return {m(0x16000, d[0]) + m(-0x11000, d[1]) + 128 * kOne,
                m(0xb000, d[0]) + m(0x11000, d[1]) + m(-0x18000, d[2]) + 96 * kOne,
                m(-0xb000, d[0]) + m(-0x11000, d[1]) + m(-0xc000, d[2])};
    }

    void drawBody(Surface& screen, const Body& b, int lift) const { // 0211f854
        const Projected p = project(b.at());
        b.sprite.draw(screen, round12(p.x), round12(p.y) - lift);
    }

    void drawCourse(Surface& screen) const {
        // The car off the course's near edges goes behind everything.
        const bool carFirst = car_.position.value[0] <= -kOne || car_.position.value[1] <= -kOne;
        if (carFirst) {
            drawBody(screen, car_, carHeight_);
        }
        // The ground, with the pieces standing on it (0211f9b0).
        for (const Cell& c : cells_) {
            const Projected p = project(c.at());
            c.ground.draw(screen, round12(p.x), round12(p.y));
            c.pieceSprite.draw(screen, round12(p.x), round12(p.y) - (c.tile ? c.tile->height : 0));
        }

        // The objects and the car, farthest first.
        struct Item {
            const Body* body;
            int depth, lift;
        };

        std::vector<Item> items;
        if (!carFirst) {
            items.push_back({&car_, project(car_.at()).depth, carHeight_});
        }
        for (const Cell& c : cells_) {
            if (!c.sprite.valid()) {
                continue;
            }
            const int depth = project(c.at()).depth;
            auto at = items.begin();
            while (at != items.end() && at->depth >= depth) {
                ++at;
            }
            items.insert(at, {&c, depth, 0});
        }
        for (const Item& i : items) {
            drawBody(screen, *i.body, i.lift);
        }
    }

    void drawMap(Surface& screen) const {
        // 021222d8 (vtable 0x100): the flags on the course's map, sixteen
        // pixels a cell, and the car: lifted twelve pixels a cell when it
        // jumps, smaller as it sinks below the ground.
        for (int y = 0; y < course_.height; ++y) {
            for (int x = 0; x < course_.width; ++x) {
                const Cell& c = cells_[size_t(y * course_.width + x)];
                if (c.tile && !c.tile->mark.empty()) {
                    Sprite s;
                    if (s.play(upper_, c.tile->mark)) {
                        s.draw(screen, left_ + x * 16, top_ + y * 16);
                    }
                }
            }
        }
        const Vec3 p = car_.at();
        int x = p[0] * 16 + left_ * kOne, y = p[1] * 16 + top_ * kOne;
        int scale = kOne;
        if (p[2] < 0) {
            scale = std::max((p[2] + 0x2000) / 2, 0);
        } else {
            y -= lscr::fx::mul(p[2], 0xc000);
        }
        if (scale * 4 < kOne) {
            return;
        }
        Sprite mark;
        if (mark.play(upper_, story_.codeName(kCarMark, kGame).text)) {
            mark.draw(screen, (x + 0x800) >> 12, (y + 0x800) >> 12, scale);
        }
    }

    Story& story_;
    const CarData& data_;
    const CarData::Course& course_;
    std::vector<Cell> cells_;
    Body car_;
    lscr::Tween<3> camera_, dummy_;
    const Animation *lower_ = nullptr, *upper_ = nullptr;
    int left_ = 0, top_ = 0;
    int carX_ = 0, carY_ = 0, carHeight_ = 0; // +0x8d8, +0x8dc, +0x8d4
};

CarGameLoop::CarGameLoop(Story& story, LoopArgs args) : Loop(story, "MINI_CAR_GAME", args) {}

CarGameLoop::~CarGameLoop() = default;

void CarGameLoop::activate() {
    // 02121928: the course, its two sprite banks, the program.
    const auto* course = courseAt(story_, args_[0]);
    if (!course) {
        story_.request(Story::Request::Pop);
        return;
    }
    scene_ = carScene(story_, makeScene(), story_.codeName(kGameProgram, kGame).text);
    scene_->loadAnimation(story_.codeName(kGameMarks, kGame).text, 2,
                          bankPath(story_, kGameFormat, kGameSuffix, kGame, course->placeBank));
    scene_->loadAnimation(story_.codeName(kGameBank, kGame).text, 0,
                          bankPath(story_, kGameFormat, kGameSuffix, kGame, course->runBank));
    engine_ = std::make_unique<Engine>(story_, *course);
    scene_->engine = engine_.get();
    state_ = State::Loading;
}

void CarGameLoop::deactivate() {
    if (scene_) {
        scene_->engine = nullptr;
    }
    scene_.reset();
    engine_.reset();
    state_ = State::Idle;
}

bool CarGameLoop::busy() const {
    return state_ == State::Loading || Loop::busy();
}

void CarGameLoop::frame(const lscr::SceneInput& input) {
    if (!scene_) {
        return;
    }
    if (state_ == State::Finished) {
        return;
    }
    if (state_ == State::Loading) {
        // 02121b48: the course laid out, then MAIN.
        engine_->layout(*scene_);
        const std::vector<lscr::Value> vars{lscr::Value::of(args_[0]), lscr::Value::of(args_[1]),
                                            lscr::Value::of(args_[2]), lscr::Value::of(args_[3])};
        scene_->start("MAIN", vars);
        state_ = State::Running;
    }
    if (state_ == State::Running && !scene_->running()) {
        // 02121b0c: MAIN over - the loop ends (unless the script asked).
        state_ = State::Finished;
        if (!story_.requestsPending()) {
            story_.request(Story::Request::Pop);
        }
        return;
    }
    scene_->frame(input);
}

lscr::Result CarGameLoop::command(int index, lscr::Call& call) {
    if (!scene_ || !engine_) {
        return lscr::Result::Next;
    }
    engine_->command(*scene_, index, call);
    return engine_->result;
}

bool CarGameLoop::condition(int index, lscr::Call&, std::span<lscr::Value> args) {
    return engine_ && engine_->condition(index, args);
}

// --- Free play ---------------------------------------------------------------------

namespace {
// The flag the first 1512 (set a flag) of a program's section names.
std::string flagIn(const Story& story, const std::string& program, const std::string& section) {
    const auto file = story.assets.script(program);
    bool in = false;
    for (const auto& c : file.commands) {
        if (c.opcode == 1100) {
            in = !c.operands.empty() && c.operands[0].type == 2 &&
                 file.text(c.operands[0]) == section;
        } else if (in && c.opcode == 1110 && c.operands.size() >= 2 &&
                   c.operands[0].value == 1512 && c.operands[1].type == 2) {
            return file.text(c.operands[1]);
        }
    }
    return {};
}

std::string courseFlag(const Story& story) {
    static const std::string debugSection = "DBG_ADD"; // a section name of the program
    return flagIn(story, story.codeName(kSelectProgram, kSelect).text, debugSection);
}

std::string clearFlag(const Story& story) {
    static const std::string success = "SUCCESS";
    return flagIn(story, story.codeName(kGameProgram, kGame).text, success);
}
} // namespace

void openCarCourses(Story& story) {
    const std::string flag = courseFlag(story);
    // The select asks for [course + 1]; [0] too, to be safe.
    for (int n = 0; n <= kCarCourses && !flag.empty(); ++n) {
        story.state.setFlag(lscr::Value::ofText(flag), true, n);
    }
}

std::vector<std::uint8_t> carCleared(const Story& story) {
    std::vector<std::uint8_t> cleared(kCarCourses);
    const std::string flag = clearFlag(story);
    for (int n = 0; n < kCarCourses && !flag.empty(); ++n) {
        cleared[std::size_t(n)] = story.state.flag(lscr::Value::ofText(flag), n + 1) ? 1 : 0;
    }
    return cleared;
}

void setCarCleared(Story& story, const std::vector<std::uint8_t>& cleared) {
    const std::string flag = clearFlag(story);
    for (int n = 0; n < kCarCourses && n < int(cleared.size()) && !flag.empty(); ++n) {
        story.state.setFlag(lscr::Value::ofText(flag), cleared[std::size_t(n)] != 0, n + 1);
    }
}

} // namespace picarat::adventure3
