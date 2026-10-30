// The third adventure's GAME and script loops. See a3_loops.hpp.
#include "story/a3_loops.hpp"
#include "minigames/a3_book.hpp"
#include "minigames/a3_car.hpp"
#include "minigames/a3_parrot.hpp"
#include "sound/a3_sound.hpp"
#include "story/a3_menus.hpp"
#include "story/a3_name_entry.hpp"
#include "story/a3_talk.hpp"
#include "video/movie.hpp"
#include <cstdio>
#include <map>

namespace picarat::adventure3 {

// --- GAME (overlay 12) -------------------------------------------------------------

void GameLoop::start() {
    // 0211f740 (the parts that concern the game): a new game's data
    // (020dda70: the saved state cleared, ten hint coins), then the title
    // with the logos on top - requests are handled in order, so the logos
    // end up on top and run first.
    //
    // Not reproduced: the save memory check (ERROR_SRAM / ERROR_DATA: the
    // port's saves are files, checked when they are read) and the secret
    // mode start with arguments (the secret menu is not part of the story).
    story_.resetGame();
    if (story_.continueCheckpoint) {
        // The app's checkpoint (a host convenience): its game, then its
        // place, as the title's Continue does - without the recap.
        story_.continueCheckpoint = false;
        if (story_.loadCheckpoint()) {
            const int place = story_.state.variable(story_.codeName(0x020f6c88));
            story_.request(Story::Request::Push, "PLACE", {place, 0, -1, 0});
            return;
        }
    }
    story_.request(Story::Request::Push, "MENU_TITLE");
    if (!story_.startAtTitle) {
        story_.request(Story::Request::Push, "MENU_LOGO");
    }
}

void GameLoop::frame(const lscr::SceneInput&) {
    // 0211fc00: uncovered with nothing waiting - the music and streams stop
    // (15 frames), the game ends and, as its third argument asks, starts
    // over.
    if (story_.requestsPending()) {
        return;
    }
    if (story_.sound) {
        story_.sound->stopMusic(15);
        story_.sound->stopStream(15);
    }
    story_.request(Story::Request::Pop);
    if (args_[2] != 0) {
        story_.request(Story::Request::Push, "GAME", args_);
    }
}

// --- Script loops (overlay 14) ---------------------------------------------------

void ScriptLoop::activate() {
    // 0211db80: the program loads; MAIN starts once it has (0211dccc). The
    // scene is new: what the loop showed before it was covered went when it
    // was (+0x44). Its own sections come before the resident ones.
    scene_ = makeScene();
    scene_->load(file_);
    for (const auto& f : Story::residentScripts()) {
        scene_->load(f);
    }
    state_ = State::Loading;
}

void ScriptLoop::deactivate() {
    scene_.reset();
    state_ = State::Idle;
}

bool ScriptLoop::busy() const {
    // 0211db28: busy while loading (and, in the game, while 020eb3e8 finds
    // the screens or the sound still changing; not reproduced).
    return state_ == State::Loading || Loop::busy();
}

void ScriptLoop::frame(const lscr::SceneInput& input) {
    if (!scene_) {
        return;
    }
    if (state_ == State::Loading) {
        // The loop's arguments are the thread's variables 200..203
        // (020f3290).
        const std::vector<lscr::Value> vars{lscr::Value::of(args_[0]), lscr::Value::of(args_[1]),
                                            lscr::Value::of(args_[2]), lscr::Value::of(args_[3])};
        scene_->start("MAIN", vars);
        state_ = State::Running;
    }
    scene_->frame(input);
    if (state_ == State::Running && !scene_->running()) {
        // 0211dc84: MAIN has ended - pop, unless MAIN asked for something
        // itself (then those requests decide).
        state_ = State::Finished;
        if (!story_.requestsPending()) {
            story_.request(Story::Request::Pop);
        }
    }
}

// --- Events (overlay 13) ----------------------------------------------------------

namespace {
constexpr std::uint32_t kEventSoundOff = 0x0211e050; // ov13: イベントサウンドセット無効
constexpr std::uint32_t kTalkMessage = 0x0211e06c;   // ov13: "lt3_msg_talk"
} // namespace

void EventLoop::start() {
    // 0211dbbc.
    event_ = story_.data.event(args_[0]);
    if (!event_) {
        throw std::runtime_error("event " + std::to_string(args_[0]) + " is not in lt3_evt");
    }
    story_.setEvent(event_);
    if (const auto* variants = story_.data.program(event_->program)) {
        if (args_[1] < 0) {
            for (const auto& v : *variants) {
                if (story_.holds(v.conditions)) {
                    variant_ = &v;
                    break;
                }
            }
        } else if (std::size_t(args_[1]) < variants->size()) {
            variant_ = &(*variants)[std::size_t(args_[1])];
        }
    }
    scene_ = makeScene();
    char path[48];
    std::snprintf(path, sizeof path, "evt/%02d/%02d_%06d.cbin", event_->group, event_->group,
                  event_->id);
    scene_->load(path);
    for (const auto& f : Story::residentScripts()) {
        scene_->load(f);
    }
    loading_ = true;
}

void EventLoop::activate() {
    // 0211dd28: the event is current again, its sound set (60 frames), its
    // talk window, and a scope for what it shows from now on.
    story_.setEvent(event_);
    if (!story_.state.flag(story_.codeName(kEventSoundOff, 13)) && story_.sound &&
        event_->soundSet >= 0) {
        story_.sound->selectSet(event_->soundSet, 0, 60);
    }
    const std::string talk = story_.codeName(kTalkMessage, 13).text;
    if (!scene_->message(talk)) {
        if (lscr::Object* m = scene_->createMessage(talk, lscr::Screen::Lower, 0x100, 0)) {
            m->text.style.characterSpacing = 1; // 02069c74 (1, 5)
            m->text.style.lineSpacing = 5;
            auto window = std::make_shared<TalkWindow>(story_, event_->group, event_->id);
            m->behaviour = window;
        }
    }
    scene_->pushScope();
    for (int s = 0; s < 2; ++s) {
        scene_->brightness[size_t(s)].set({story_.brightness[size_t(s)]});
    }
}

void EventLoop::deactivate() {
    // 0211ddd4 and the base (020f339c): no current event; what it showed
    // since it was uncovered goes.
    scene_->popScope();
    story_.setEvent(nullptr);
}

void EventLoop::end() {
    // 0211dd00: the variant's closing actions, then the phases move on.
    if (variant_) {
        story_.apply(variant_->after);
    }
    story_.advancePhases();
    story_.setEvent(nullptr);
}

bool EventLoop::busy() const {
    return loading_ || Loop::busy();
}

void EventLoop::frame(const lscr::SceneInput& input) {
    if (loading_) {
        // 0211de20: the variant's opening actions, the phases, then MAIN
        // (at the variant's label) with the request's arguments.
        if (variant_) {
            story_.apply(variant_->before);
        }
        story_.advancePhases();
        const std::vector<lscr::Value> vars{lscr::Value::of(args_[0]), lscr::Value::of(args_[1]),
                                            lscr::Value::of(args_[2]), lscr::Value::of(args_[3])};
        scene_->start("MAIN", vars, variant_ ? variant_->label : std::string());
        loading_ = false;
    }
    // 0211dde4 looks before the scripts run: MAIN that ended last frame
    // pops the event - unless the event already asked for something else,
    // which the scheduler did at the start of this frame (EVENT_NAZO pops
    // and pushes the event again in its last statements).
    if (!scene_->running()) {
        if (!popped_) {
            popped_ = true;
            story_.request(Story::Request::Pop);
        }
        return;
    }
    scene_->frame(input);
}

// --- Places (overlay 15) ----------------------------------------------------------

namespace {
constexpr std::uint32_t kRecapFlag = 0x0211e078;      // ov15: あらすじ起動
constexpr std::uint32_t kPlaceVariable = 0x0211e0a0;  // ov15: 現在のプレイス
constexpr std::uint32_t kExamineMessage = 0x0211e0e0; // ov15: "lt3_msg_tobj"
} // namespace

void PlaceLoop::start() {
    // 0211db8c: the place becomes the current one.
    if (const PlaceInfo* p = story_.data.place(args_[0])) {
        story_.setPlace(*p);
    }
}

void PlaceLoop::activate() {
    // 0211dbb0.
    bool recap = false;
    if (story_.state.flag(story_.codeName(kRecapFlag, 15))) {
        story_.state.setFlag(story_.codeName(kRecapFlag, 15), false);
        // The current phase's recap: the event named by its save label.
        const Phase* phase = story_.data.phase(story_.state.variable(story_.codeName(0x020f6d6c)));
        if (const EventInfo* e = phase ? story_.data.event(phase->save) : nullptr) {
            story_.request(Story::Request::Push, "EVENT", {e->id, 0, 0, 0});
            recap = true;
        }
    }
    const int place = story_.state.variable(story_.codeName(kPlaceVariable, 15));
    if (const PlaceInfo* p = story_.data.place(place)) {
        story_.setPlace(*p);
    }
    scene_ = makeScene();
    char path[48];
    std::snprintf(path, sizeof path, "rc/lt3_plc/p%04d.lbin", place);
    if (story_.assets.exists(path)) {
        scene_->load(path);
    }
    std::snprintf(path, sizeof path, "rc/obj/p%04d.cbin", place);
    if (story_.assets.exists(path)) {
        scene_->load(path);
    }
    for (const auto& f : Story::residentScripts()) {
        scene_->load(f);
    }
    const std::string examine = story_.codeName(kExamineMessage, 15).text;
    if (lscr::Object* m = scene_->createMessage(examine, lscr::Screen::Lower, 0x100, 0)) {
        auto window = std::make_shared<TalkWindow>(story_, 30, place); // txt/?/30
        m->behaviour = window;
    }
    state_ = recap ? 5 : 1;
}

void PlaceLoop::deactivate() {
    scene_.reset();
    state_ = 0;
}

bool PlaceLoop::busy() const {
    return state_ != 5 || Loop::busy();
}

void PlaceLoop::frame(const lscr::SceneInput& input) {
    if (!scene_) {
        return;
    }
    // 0211ddbc: each part starts when the one before has ended (the
    // first once the room has loaded); the loop part also ends when a
    // request is waiting.
    const std::vector<lscr::Value> vars{lscr::Value::of(args_[0]), lscr::Value::of(args_[1]),
                                        lscr::Value::of(args_[2]), lscr::Value::of(args_[3])};
    switch (state_) {
    case 1:
        scene_->start("PLACE_BEGIN", vars);
        state_ = 2;
        break;
    case 2:
        if (!scene_->running()) {
            // The room is ready for the player: the app's checkpoint.
            story_.saveCheckpoint();
            scene_->restart("PLACE_LOOP");
            state_ = 3;
        }
        break;
    case 3:
        if (!scene_->running() || story_.requestsPending()) {
            scene_->restart("PLACE_END");
            state_ = 4;
        }
        break;
    case 4:
        if (!scene_->running()) {
            state_ = 5;
        }
        break;
    default:
        break;
    }
    scene_->frame(input);
}

std::vector<TouchArea> PlaceLoop::touchAreas() const {
    // The room's objects are made by evt/lt3_place's ADD_OBJ_* functions:
    // touchable objects (4017) tagged with their kind (4117: 1 hint coin,
    // 3 event, 4 way out, 5 remark, 6 menu, 7 view, 2 effect) whose touch
    // runs CHR_OBJ_<kind> with the object's values. ADD_OBJ_COIN adds no
    // object for a coin already found (condition 1832). CHR_OBJ_EVNT tells
    // events with a puzzle apart as the help does: the event's puzzle (1801,
    // 1902) none, or solved (1890 2), shows the event effect, else the
    // puzzle effect. The objects take the pen only while the loop waits for
    // it (the touched function first turns input off, 3023 0).
    std::vector<TouchArea> out;
    if (state_ != 3 || !scene_ || scene_->inputBlocked) {
        return out;
    }
    for (const lscr::Object* o : scene_->drawOrder()) {
        if (o->dead || o->kind != lscr::Object::Kind::Button || o->screen != lscr::Screen::Lower ||
            !o->moves() || !o->takesInput()) {
            continue;
        }
        TouchArea area;
        switch (o->kindTag) {
        case 1:
            area.kind = TouchArea::Kind::HintCoin;
            break;
        case 5:
            area.kind = TouchArea::Kind::Text;
            break;
        case 3: {
            // The object's values are its thread's variables 200..: name,
            // event (ADD_OBJ_EVNT -> 4017).
            area.kind = TouchArea::Kind::Dialog;
            if (o->thread) {
                const lscr::Value event = o->thread->variables[1];
                const EventInfo* info = event.isText() ? story_.data.event(event.toText())
                                                       : story_.data.event(event.toInt());
                if (info && !info->puzzle.empty()) {
                    const int puzzle = story_.data.puzzle(lscr::Value::ofText(info->puzzle));
                    const auto it = story_.puzzles.find(puzzle);
                    if (puzzle >= 0 && (it == story_.puzzles.end() || it->second.state != 2)) {
                        area.kind = TouchArea::Kind::Puzzle;
                    }
                }
            }
            break;
        }
        default:
            continue;
        }
        const auto [px, py] = scene_->pixel(*o);
        const auto box = scene_->turnedTouchBox(*o);
        area.x = px + box[0];
        area.y = py + box[1];
        area.w = box[2] - box[0];
        area.h = box[3] - box[1];
        if (area.w > 0 && area.h > 0) {
            out.push_back(area);
        }
    }
    return out;
}

// --- Movies (overlay 16) ----------------------------------------------------------

namespace {
constexpr std::uint32_t kMoviePlaying = 0x0211eeb0; // ov16: ムービー中
// The thread variables the movie's script and the loop share (020f1a9c /
// 020f1ab8): 204 state (1 opened, 2 start, 3 playing, 4 over, 5 done, 6
// failed), 205 keep the sound, 206 skippable.
constexpr int kState = 4, kKeepSound = 5, kSkippable = 6;
} // namespace

MovieLoop::MovieLoop(Story& story, LoopArgs args) : Loop(story, "MOVIE", args) {}

MovieLoop::~MovieLoop() = default;

void MovieLoop::start() {
    // 0211e374.
    scene_ = makeScene();
    scene_->backdrop = {0, 0};
    scene_->load("evt/lt3_movie.cbin");
    char path[48];
    std::snprintf(path, sizeof path, "evt/mov/%02d.cbin", args_[0]);
    if (story_.assets.exists(path)) {
        scene_->load(path);
    }
    for (const auto& f : Story::residentScripts()) {
        scene_->load(f);
    }
    // The captions: txt/?/50 part <movie>, rows {frame, -, text}.
    std::snprintf(path, sizeof path, "txt/?/50/50_%06d.lbin", args_[0]);
    if (story_.assets.exists(path)) {
        const auto t = story_.assets.script(path);
        for (const auto& c : t.commands) {
            if (c.opcode == 6001 && c.operands.size() >= 3) {
                captions_.push_back(
                    {int(std::int32_t(c.operands[0].value)), t.text(c.operands[2])});
            }
        }
    }
    if (lscr::Object* m = scene_->createMessage("caption", lscr::Screen::Lower, 0x80, 0)) {
        m->position.set({128 * lscr::fx::kOne, 175 * lscr::fx::kOne});
        m->text.style.centerX = m->text.style.centerY = true;
        m->text.style.characterSpacing = 1; // 02069c74 (1, 1)
        m->text.style.lineSpacing = 1;
        m->text.style.defaultColour = 2; // 02069c54 (2, 0): white
    }
    char mods[32];
    std::snprintf(mods, sizeof mods, "mov/m%03d.mods", args_[0]);
    const std::vector<lscr::Value> vars{lscr::Value::of(args_[0]), lscr::Value::of(args_[1]),
                                        lscr::Value::of(args_[2]), lscr::Value::of(args_[3]),
                                        lscr::Value::of(0)};
    scene_->start("MAIN", vars);
    lscr::Value* state = &scene_->mainThread()->variables[kState];
    try {
        movie_ = std::make_unique<MovieFrames>(
            story_.assets.file(mods),
            mobiclip::Tables::fromOverlay(readFile(story_.assets.file("code/overlay2.bin")),
                                          mobiclip::Tables::kAdventure3Origin));
        *state = lscr::Value::of(1);
    } catch (const std::exception&) {
        *state = lscr::Value::of(6);
    }
    story_.state.setFlag(story_.codeName(kMoviePlaying, 16), true);
}

void MovieLoop::end() {
    // 0211e618.
    story_.state.setFlag(story_.codeName(kMoviePlaying, 16), false);
}

void MovieLoop::frame(const lscr::SceneInput& input) {
    lscr::Thread* thread = scene_->mainThread();
    auto var = [&](int i) -> lscr::Value& {
        return thread->variables[size_t(i)];
    };
    // 0211e6b0: START (key 3, 0206078c) - or a waiting request - skips,
    // when the script allows it. Touching does not.
    if (var(kSkippable).toInt() != 0 && var(kState).toInt() == 3 &&
        ((input.keysPressed & (1u << 3)) || story_.requestsPending())) {
        var(kState) = lscr::Value::of(4);
    }
    scene_->frame(input);
    if (var(kState).toInt() == 2) {
        // Playing starts: the screens fade in over the first argument.
        var(kState) = lscr::Value::of(3);
        playing_ = true;
        for (auto& b : scene_->brightness) {
            b.to(1, {0}, args_[1] * lscr::fx::kOne);
        }
    }
    if (playing_ && movie_ && var(kState).toInt() == 3) {
        // The alarm at the film's rate (8.24 frames a second) moves the
        // frame to show on; frames the game falls behind on are skipped.
        clock_ += movie_->numerator;
        const unsigned due = unsigned(clock_ / (std::uint64_t(60) << 24));
        if (due >= movie_->count) {
            var(kState) = lscr::Value::of(4);
        } else {
            shown_ = due;
        }
        // 0211e234: captions due by now, at once.
        while (nextCaption_ < captions_.size() && int(shown_) >= captions_[nextCaption_].frame) {
            if (lscr::Object* m = scene_->message("caption")) {
                m->text.set(captions_[nextCaption_].text, story_.text, true);
            }
            ++nextCaption_;
        }
    }
    const int s = var(kState).toInt();
    if (s != 1 && s != 3 && s != 5) {
        // Over (or failed): the sound stops unless kept, the screens fade
        // out over the third argument, and the loop pops itself.
        if (var(kKeepSound).toInt() == 0 && story_.sound) {
            story_.sound->stopStream(args_[2]);
        }
        var(kState) = lscr::Value::of(5);
        playing_ = false;
        if (!story_.requestsPending()) {
            story_.request(Story::Request::Pop);
        }
        for (auto& b : scene_->brightness) {
            b.to(1, {-lscr::fx::kOne}, args_[2] * lscr::fx::kOne, true);
        }
    }
}

void MovieLoop::skip() {
    lscr::Value& state = scene_->mainThread()->variables[kState];
    if (state.toInt() == 3) {
        state = lscr::Value::of(4);
    }
}

Surface MovieLoop::render() const {
    Surface out = scene_->render();
    if (movie_ && (playing_ || shown_ > 0)) {
        // The film on the upper screen, under the screen's brightness.
        Surface picture = const_cast<MovieFrames&>(*movie_).frame(shown_);
        const int level = (scene_->brightness[1].value[0] * 16) >> 12;
        applyBrightness(picture, level, level);
        std::copy(picture.pixels.begin(), picture.pixels.end(), out.pixels.begin());
    }
    return out;
}

// --- The loop registry -------------------------------------------------------------

std::optional<std::string> scriptLoopFile(Assets& assets, const std::string& name) {
    // 020f4740 registers each script loop with its name and its program; the
    // literal pool 020f4b54..020f4c38 holds the pairs (each with a record
    // address between or after them). Read once from overlay 6.
    static std::map<std::string, std::string> table;
    if (table.empty()) {
        constexpr int kOverlay = 6;
        constexpr std::uint32_t kLoad = 0x020a7980, kFirst = 0x020f4b54, kLast = 0x020f4c38;
        std::string pendingName;
        for (std::uint32_t a = kFirst; a < kLast; a += 4) {
            const std::uint32_t p = assets.codeWord(kOverlay, kLoad, a);
            if (p < kLoad || p >= 0x020f8340) {
                continue;
            }
            const std::string s = assets.codeText(kOverlay, kLoad, p);
            if (s.ends_with(".cbin")) {
                if (!pendingName.empty()) {
                    table[pendingName] = s;
                    pendingName.clear();
                }
            } else if (!s.empty() &&
                       s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ_") == std::string::npos) {
                pendingName = s;
            }
        }
    }
    auto it = table.find(name);
    return it == table.end() ? std::nullopt : std::optional(it->second);
}

// --- Puzzles (overlay 35) -----------------------------------------------------------

NazoLoop::NazoLoop(Story& story, LoopArgs args) : Loop(story, "NAZO", args) {}

NazoLoop::~NazoLoop() = default;

void NazoLoop::start() {
    // 0211e100: the puzzle the request names becomes the current one.
    const int number = args_[0];
    story_.currentPuzzle = number;
    if (story_.autoSolve && story_.autoSolve(number)) {
        if (story_.puzzles[number].state == 0) {
            story_.setPuzzleState(number, 1);
        }
        story_.setPuzzleState(number, 2);
        popped_ = true;
        story_.request(Story::Request::Pop);
        return;
    }
    puzzle_ = std::make_unique<PuzzleLoop>(
        story_.assets, story_.text, story_.state, unsigned(number), story_.puzzles[number],
        story_.sound, [this](int n, int state) { story_.setPuzzleState(n, state); });
    puzzle_->drivesSound = false;
    puzzle_->setBrightness(story_.brightness[0]);
    puzzle_->onScene = [this](lscr::Scene& scene) {
        installStoryCommands(scene, story_, *this);
    };
    if (lscr::Scene* s = puzzle_->scene()) {
        installStoryCommands(*s, story_, *this);
    }
}

void NazoLoop::frame(const lscr::SceneInput& input) {
    if (!puzzle_ || popped_) {
        return;
    }
    puzzle_->frame(input);
    if (puzzle_->finished()) {
        // 0211e6c8: over - the loop ends itself.
        popped_ = true;
        story_.request(Story::Request::Pop);
    }
}

Surface NazoLoop::render() const {
    return puzzle_ ? puzzle_->render() : Loop::render();
}

bool NazoLoop::busy() const {
    const lscr::Scene* s = scene();
    return s && (s->brightness[0].active() || s->brightness[1].active());
}

lscr::Scene* NazoLoop::scene() {
    return puzzle_ ? puzzle_->scene() : nullptr;
}

const lscr::Scene* NazoLoop::scene() const {
    return puzzle_ ? puzzle_->scene() : nullptr;
}

std::unique_ptr<Loop> makeStoryLoop(Story& story, const std::string& name, const LoopArgs& args) {
    if (name == "NAZO") {
        return std::make_unique<NazoLoop>(story, args);
    }
    if (name == "MINI_PARROT_MENU") {
        return std::make_unique<ParrotMenuLoop>(story, args);
    }
    if (name == "MINI_PARROT_GAME") {
        return std::make_unique<ParrotGameLoop>(story, args);
    }
    if (name == "MINI_BOOK") {
        return std::make_unique<BookLoop>(story, args);
    }
    if (name == "MINI_CAR_SLCT") {
        return std::make_unique<CarSelectLoop>(story, args);
    }
    if (name == "MINI_CAR_EDIT") {
        return std::make_unique<CarEditLoop>(story, args);
    }
    if (name == "MINI_CAR_GAME") {
        return std::make_unique<CarGameLoop>(story, args);
    }
    if (name == "NAME_RESIST") {
        return std::make_unique<NameEntryLoop>(story, args);
    }
    if (name == "EVENT") {
        return std::make_unique<EventLoop>(story, args);
    }
    if (name == "PLACE") {
        return std::make_unique<PlaceLoop>(story, args);
    }
    if (name == "MOVIE") {
        return std::make_unique<MovieLoop>(story, args);
    }
    if (name == "MENU_NAZOBEE" || name == "MENU_NAZORINE") {
        // 020eb77c / 020eb7d8: Granny Riddleton's shack and her cat - one
        // script loop program; the factory overwrites the last two
        // arguments, (0, 0) for the shack and (1, 0) for the cat.
        LoopArgs a = args;
        a[2] = name == "MENU_NAZORINE" ? 1 : 0;
        a[3] = 0;
        return std::make_unique<ScriptLoop>(story, name, a, "evt/prg/lt3_menu_nazoba.cbin");
    }
    return makeMenuLoop(story, name, args);
}

} // namespace picarat::adventure3
