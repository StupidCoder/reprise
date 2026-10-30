// The third adventure's puzzle index and secret pages. See a3_menus.hpp.
#include "story/a3_menus.hpp"
#include "sound/a3_sound.hpp"
#include <cmath>
#include <cstdio>

namespace picarat::adventure3 {

namespace {
using lscr::Object;
using lscr::Value;
namespace fx = lscr::fx;

constexpr std::uint32_t kOverlayLoad = 0x0211df00; // overlays 18 and 27..32

std::string formatted(const std::string& format, int n) {
    char out[64];
    std::snprintf(out, sizeof out, format.c_str(), n);
    return out;
}

void put(lscr::Call& c, std::size_t i, Value v) {
    if (Value* slot = c.variable(i)) {
        slot->get() = std::move(v);
    }
}

// 020653f0 / 02065420: alpha at once / over `frames`.
void fade(Object& o, int alpha, int frames) {
    if (frames == 0) {
        o.alpha.set({alpha});
    } else {
        o.alpha.to(1, {alpha}, frames * fx::kOne);
    }
}

// 0206582c (curve 1, 20 frames): the buttons of a page slide.
void slide(Object& o, int dx) {
    const auto& v = o.offset.value;
    o.offset.to(1, {v[0] + dx, v[1]}, 20 * fx::kOne);
}

// 0206ad94 (at once).
void setText(lscr::Scene& sc, const std::string& message, const std::string& text) {
    if (Object* m = sc.message(message)) {
        m->text.set(text, sc.text, true);
    }
}

// The row {id} of a text table (3310; 0203be74..0203bf8c's binary search):
// its two texts (+4, +8).
const std::array<std::string, 2>* tableRow(lscr::Scene& sc, const std::string& table, int id) {
    auto t = sc.textTables.find(table);
    if (t == sc.textTables.end()) {
        return nullptr;
    }
    auto e = t->second.find(id);
    return e == t->second.end() || e->second.empty() ? nullptr : &e->second.front();
}

// 020f2384 -> 020b0b8c: an object of the loop still fading.
bool anyFading(lscr::Scene& sc) {
    for (Object* o : sc.creationOrder()) {
        if (o->shown() && o->alpha.active()) {
            return true;
        }
    }
    return false;
}

// 020f2390 -> 020b0bf8: an object of the loop still moving (hypothesis:
// position or offset tweens, as 4015's shake test reads its own).
bool anyMoving(lscr::Scene& sc) {
    for (Object* o : sc.creationOrder()) {
        if (o->shown() && (o->position.active() || o->offset.active())) {
            return true;
        }
    }
    return false;
}
} // namespace

// --- MENU_JITEN (overlay 18) ---------------------------------------------------------

namespace jiten {
constexpr int kOverlay = 18;
constexpr std::uint32_t kRowCheck = 0x0211ed3c, kRowSolved = 0x0211ed4c, kRowIcon = 0x0211ed5c,
                        kRowNumber = 0x0211ed6c, kCursor = 0x0211ed78, kKnob = 0x0211ed84,
                        kKnobTouch = 0x0211ed90, kKnobTravel = 0x0211ed98,
                        kKnobHeight = 0x0211eda4, kListText = 0x0211edb0,
                        kListMessage = 0x0211edc4, kMarked = 0x0211edd0, kUnmarked = 0x0211edd8,
                        kSolved = 0x0211ede4, kUnsolved = 0x0211ede8,
                        kSelectedAll = 0x0211edec, kSelectedMarked = 0x0211ee10,
                        kBusy = 0x0211eba8, kList = 0x0211eb9c;
constexpr int kRows = 6, kRowPitch = 21;
} // namespace jiten

MenuJitenLoop::MenuJitenLoop(Story& story, LoopArgs args)
    : ScriptLoop(story, "MENU_JITEN", args, "evt/prg/lt3_menu_jiten.cbin") {}

std::string MenuJitenLoop::name(std::uint32_t address) const {
    return story_.codeName(address, jiten::kOverlay).toText();
}

std::string MenuJitenLoop::rowObject(std::uint32_t format, int row) const {
    return formatted(name(format), row);
}

void MenuJitenLoop::activate() {
    ScriptLoop::activate(); // 0211e378
    listPending_ = false;
    listCount_ = 0;
    listShift_ = 0;
}

int MenuJitenLoop::entry(int index) const {
    // 0211e51c: by the list shown (ワーク[4]): 0 every puzzle found
    // (020e68f4), 1 the marked ones (020e691c); a negative index asks for
    // the count.
    std::vector<int> list;
    switch (story_.state.variable(Value::ofText(name(jiten::kList)))) {
    case 0:
        list = story_.foundSlots();
        break;
    case 1:
        list = story_.markedSlots();
        break;
    default:
        return -1;
    }
    if (index < 0) {
        return int(list.size());
    }
    return index < int(list.size()) ? list[std::size_t(index)] : -1;
}

void MenuJitenLoop::showRow(int row, int slot) {
    // 0211e5ac: a row's four objects, hidden without a puzzle; with one, its
    // favourite mark, solved or not, and number (the slot + 1).
    lscr::Scene& sc = *scene_;
    Object* check = sc.find(rowObject(jiten::kRowCheck, row));
    Object* solved = sc.find(rowObject(jiten::kRowSolved, row));
    Object* icon = sc.find(rowObject(jiten::kRowIcon, row));
    Object* number = sc.find(rowObject(jiten::kRowNumber, row));
    for (Object* o : {check, solved, icon, number}) {
        if (o) {
            fade(*o, slot == -1 ? 0 : fx::kOne, 0);
        }
    }
    if (slot == -1) {
        return;
    }
    const auto& slots = story_.data.puzzleSlots();
    if (slot < 0 || slot >= int(slots.size())) {
        return;
    }
    const auto record = story_.puzzles.find(slots[std::size_t(slot)].number);
    const bool marked = record != story_.puzzles.end() && record->second.marked;
    const bool done = record != story_.puzzles.end() && record->second.state == 2;
    if (check) {
        lscr::play(*check,
                   lscr::sequenceOf(*check, Value::ofText(name(marked ? jiten::kMarked
                                                                      : jiten::kUnmarked))),
                   0);
    }
    if (solved) {
        lscr::play(*solved,
                   lscr::sequenceOf(*solved, Value::ofText(name(done ? jiten::kSolved
                                                                     : jiten::kUnsolved))),
                   0);
    }
    if (number) {
        number->user[0] = Value::of(slot + 1);
    }
}

void MenuJitenLoop::fillRows(int top) {
    // 0211e74c: the cursor, then rows 1..5 from `top` (row 0 is the one
    // that scrolls in).
    showCursor(top);
    for (int k = 0; k < 5; ++k) {
        showRow(k + 1, entry(top + k));
    }
}

void MenuJitenLoop::layRows() {
    // 0211ea08: the rows' places, 21 pixels apart from y 40.
    lscr::Scene& sc = *scene_;
    for (int row = 0; row < jiten::kRows; ++row) {
        const int y = 40 + jiten::kRowPitch * row;
        const std::array<std::pair<std::uint32_t, std::array<int, 2>>, 4> places{
            {{jiten::kRowCheck, {21, y}},
             {jiten::kRowSolved, {38, y + 1}},
             {jiten::kRowIcon, {52, y}},
             {jiten::kRowNumber, {92, y}}}};
        for (const auto& [format, at] : places) {
            if (Object* o = sc.find(rowObject(format, row))) {
                o->position.set({at[0] * fx::kOne, at[1] * fx::kOne});
            }
        }
    }
}

void MenuJitenLoop::showCursor(int top) {
    // 0211e858: the bar on the row of the puzzle picked last in this list
    // (none: hidden).
    lscr::Scene& sc = *scene_;
    Object* cursor = sc.find(name(jiten::kCursor));
    if (!cursor) {
        return;
    }
    int picked = -1;
    switch (story_.state.variable(Value::ofText(name(jiten::kList)))) {
    case 0:
        picked = story_.state.variable(Value::ofText(name(jiten::kSelectedAll)));
        break;
    case 1:
        picked = story_.state.variable(Value::ofText(name(jiten::kSelectedMarked)));
        break;
    default:
        break;
    }
    for (int k = 0; k < 5; ++k) {
        if (picked == entry(top + k)) {
            cursor->position.set({16 * fx::kOne, (56 + jiten::kRowPitch * k) * fx::kOne});
            cursor->switches[0].on = true;
            return;
        }
    }
    cursor->switches[0].on = false;
}

void MenuJitenLoop::frame(const lscr::SceneInput& input) {
    ScriptLoop::frame(input);
    if (!listPending_ || !scene_) {
        return;
    }
    // 0211e3d4: the list's titles, a line each, as one message moved up by
    // the rows above the top; then ワーク[2] says it is done.
    lscr::Scene& sc = *scene_;
    std::string list;
    for (int i = 0; i < listCount_; ++i) {
        const int slot = entry(i);
        const auto& slots = story_.data.puzzleSlots();
        if (slot >= 0 && slot < int(slots.size())) {
            if (const PuzzleInfo* p = story_.data.puzzleInfo(slots[std::size_t(slot)].number)) {
                list += p->title;
            }
        }
        list += '\n';
    }
    sc.strings[name(jiten::kListText)] = list;
    if (Object* m = sc.message(name(jiten::kListMessage))) {
        m->offset.set({0, fx::fromFloat(listShift_)});
        m->text.set(list, sc.text, true);
    }
    story_.state.setVariable(Value::ofText(name(jiten::kBusy)), 0);
    listPending_ = false;
}

lscr::Result MenuJitenLoop::command(int index, lscr::Call& call) {
    lscr::Scene& sc = *scene_;
    switch (index) {
    case 0: // 1280
        if (call.count() >= 2) {
            put(call, 1, Value::of(entry(call.integer(0))));
        }
        break;
    case 1: // 1281: the objects are found by name when used
        break;
    case 2: // 1282
        if (call.count() >= 2) {
            showRow(call.integer(0), call.integer(1));
        }
        break;
    case 3: // 1283
        if (call.count() >= 1) {
            fillRows(call.integer(0));
        }
        break;
    case 4: // 1284
        if (call.count() >= 2) {
            listCount_ = call.integer(0);
            listShift_ = float(call.integer(1)) * -21.0f;
            listPending_ = true;
            story_.state.setVariable(Value::ofText(name(jiten::kBusy)), 1);
        }
        break;
    case 5: // 1285
        if (call.count() >= 1) {
            fillRows(call.integer(0));
            layRows();
        }
        break;
    case 6: // 1286
        if (call.count() >= 1) {
            showCursor(call.integer(0));
        }
        break;
    case 7: { // 1287
        if (call.count() < 2) {
            break;
        }
        // The knob moves down the bar's travel (ワーク[13]) as the top row
        // goes from the first to the last five; its touch box (ワーク[14]
        // high) follows it.
        const int count = call.integer(0), top = call.integer(1);
        const float travel =
            float(story_.state.variable(Value::ofText(name(jiten::kKnobTravel))));
        float y = 0;
        if (top != 0) {
            y = top == count - 5 ? travel : float(top) * (travel / (float(count) - 4.0f));
        }
        Object* knob = sc.find(name(jiten::kKnob));
        if (!knob) {
            break;
        }
        knob->offset.set({0, fx::fromFloat(y)});
        const int at = knob->position.value[1] + knob->offset.value[1];
        const int boxY = int(std::floor(float(at) / 4096.0f + 0.5f));
        if (Object* touch = sc.find(name(jiten::kKnobTouch))) {
            touch->areas.clear();
            touch->areas.push_back(
                {227, boxY, 16, story_.state.variable(Value::ofText(name(jiten::kKnobHeight))), 3});
        }
        break;
    }
    default:
        break;
    }
    return lscr::Result::Next;
}

// --- SCRT_CHARA (overlay 27) ------------------------------------------------------------

namespace chara {
constexpr int kOverlay = 27;
constexpr std::uint32_t kButton = 0x0211eec8, kStartPicture = 0x0211eed4, kNumber = 0x0211eee0,
                        kNumberFormat = 0x0211eee8, kNamePlate = 0x0211eef0, kText = 0x0211eefc,
                        kProfile = 0x0211ef04, kPlace = 0x0211ef10, kPicked = 0x0211e954,
                        kPage = 0x0211e960, kNames = 0x0211ed4c;
constexpr int kPerPage = 20;
} // namespace chara

SecretCharaLoop::SecretCharaLoop(Story& story, LoopArgs args)
    : ScriptLoop(story, "SCRT_CHARA", args, "evt/prg/lt3_scrt_chara.cbin") {}

std::string SecretCharaLoop::name(std::uint32_t address) const {
    return story_.codeName(address, chara::kOverlay).toText();
}

std::string SecretCharaLoop::characterName(int index) const {
    // The list of names at 0211ed4c (pointers into the overlay).
    const std::uint32_t at = chara::kNames + 4u * std::uint32_t(index);
    const std::uint32_t p = story_.assets.codeWord(chara::kOverlay, kOverlayLoad, at);
    return story_.assets.codeText(chara::kOverlay, kOverlayLoad, p);
}

void SecretCharaLoop::activate() {
    ScriptLoop::activate(); // 0211e300
    shown_.clear();
    story_.state.setVariable(Value::ofText(name(chara::kPicked)), 0);
    character_ = index_ = step_ = previous_ = 0;
    previousName_.clear();
    startPicture_ = true;
}

void SecretCharaLoop::deactivate() {
    shown_.clear(); // 0211e35c
    ScriptLoop::deactivate();
}

lscr::Result SecretCharaLoop::command(int index, lscr::Call& call) {
    lscr::Scene& sc = *scene_;
    if (index == 0 && call.count() >= 3) {
        // 1280: two pages' twenty buttons slide by dx (one leaving, one
        // coming).
        const int dx = fx::fromFloat(call.real(2));
        for (int k = 0; k < chara::kPerPage; ++k) {
            for (int first : {call.integer(0), call.integer(1)}) {
                if (Object* b = sc.find(formatted(name(chara::kButton), first + k))) {
                    slide(*b, dx);
                }
            }
        }
    } else if (index == 1 && call.count() >= 3) {
        // 1281: column {column} of page {page}: four buttons five apart.
        const int first = (call.integer(0) - 1) * chara::kPerPage + call.integer(1);
        for (int k = 0; k < 4; ++k) {
            if (Object* b = sc.find(formatted(name(chara::kButton), first + 5 * k))) {
                b->switches[0].on = call.integer(2) != 0;
            }
        }
    } else if (index == 2 && call.count() >= 1) {
        // 1282: button {n} of the page shown (ワーク[1]).
        index_ = (story_.state.variable(Value::ofText(name(chara::kPage))) - 1) * chara::kPerPage +
                 call.integer(0);
        const std::string who = characterName(index_);
        const CharacterInfo* info = story_.data.character(who);
        const int id = info ? info->id : -1;
        if (!startPicture_ && id == character_) {
            // The one shown: another of its animations, at random (its
            // name up to a '*'), unless it is the one playing.
            Object* o = sc.find(shown_);
            if (o && o->animation && !o->animation->sequences.empty()) {
                const auto& all = o->animation->sequences;
                std::string pick = all[std::size_t(story_.state.random()) % all.size()].name;
                if (!o->sequence || o->sequence->name != pick) {
                    if (const auto star = pick.find('*'); star != std::string::npos) {
                        pick.resize(star);
                    }
                    lscr::play(*o, lscr::sequenceOf(*o, Value::ofText(pick)), 0);
                }
            }
            sc.setAllInput(true);
            return lscr::Result::Next;
        }
        if (Object* o = sc.find(shown_)) {
            fade(*o, 0, 5);
        }
        previous_ = character_;
        previousName_ = shown_;
        character_ = id;
        shown_ = who;
        step_ = 1;
    }
    return lscr::Result::Next;
}

void SecretCharaLoop::frame(const lscr::SceneInput& input) {
    ScriptLoop::frame(input);
    if (!scene_) {
        return;
    }
    lscr::Scene& sc = *scene_;
    switch (step_) {
    case 1:
        // The page's first picture goes; the number and name plate.
        if (startPicture_) {
            sc.remove(name(chara::kStartPicture));
            sc.freeResource(name(chara::kStartPicture));
            startPicture_ = false;
        }
        if (Object* n = sc.find(name(chara::kNumber))) {
            n->user[0] = Value::of(index_ + 1);
        }
        if (Object* plate = sc.find(name(chara::kNamePlate))) {
            const std::string sequence = formatted(name(chara::kNumberFormat), index_ + 1);
            lscr::play(*plate, lscr::sequenceOf(*plate, Value::ofText(sequence)), 0);
        }
        step_ = 2;
        break;
    case 2:
        // The profile (text table row index + 1).
        if (const auto* row = tableRow(sc, name(chara::kText), index_ + 1)) {
            setText(sc, name(chara::kProfile), (*row)[1]);
        }
        step_ = 3;
        break;
    case 3:
        // Once the old one has faded: it goes, the new one comes (020f2520,
        // 020f25d8: as 4012 does).
        if (anyFading(sc)) {
            break;
        }
        if (!previousName_.empty() && previousName_ != shown_) {
            sc.remove(previousName_);
            std::erase(sc.cast, previousName_);
        }
        if (!sc.find(shown_)) {
            if (const CharacterInfo* ch = story_.data.character(shown_)) {
                sc.loadAnimation(shown_, 0, "chr/" + ch->animation + ".cani");
                if (sc.create(Object::Kind::Sprite, shown_, shown_)) {
                    sc.cast.push_back(shown_);
                }
            }
        }
        step_ = 4;
        break;
    case 4:
        // Placed on the upper screen's right (020f254c, lt3_pos), layer 2,
        // its first animation, fading in over 9 frames.
        if (Object* o = sc.find(shown_)) {
            if (auto p = sc.assets.position(name(chara::kPlace))) {
                sc.setLayer(*o, p->layer);
                o->mirror = p->mirror;
                o->position.set({p->x * fx::kOne, p->y * fx::kOne});
            }
            sc.setLayer(*o, 2);
            lscr::play(*o, lscr::sequenceOf(*o, Value::of(0)), 0);
            o->switches[0].on = true;
            fade(*o, 0, 0);
            fade(*o, fx::kOne, 9);
        }
        step_ = 6;
        break;
    case 6:
        if (!anyFading(sc)) {
            step_ = 0;
            sc.setAllInput(true);
        }
        break;
    default:
        break;
    }
}

// --- SCRT_VOICE (overlay 28) ------------------------------------------------------------

namespace voice {
constexpr int kOverlay = 28;
constexpr std::uint32_t kBalloon = 0x0211e7e8, kBalloonFormat = 0x0211e7f4,
                        kPortraitOff = 0x0211e7fc, kPortraitOn = 0x0211e808, kText = 0x0211e814,
                        kMessage = 0x0211e81c, kPortrait = 0x0211e7dc, kPicked = 0x0211e638,
                        kLines = 0x0211e3d4, kSpeakers = 0x0211e348;
constexpr int kPortraits = 13;
} // namespace voice

SecretVoiceLoop::SecretVoiceLoop(Story& story, LoopArgs args)
    : ScriptLoop(story, "SCRT_VOICE", args, "evt/prg/lt3_scrt_voice.cbin") {}

std::string SecretVoiceLoop::name(std::uint32_t address) const {
    return story_.codeName(address, voice::kOverlay).toText();
}

void SecretVoiceLoop::activate() {
    ScriptLoop::activate(); // 0211dfcc
    story_.state.setVariable(Value::ofText(name(voice::kPicked)), 0);
}

void SecretVoiceLoop::frame(const lscr::SceneInput& input) {
    ScriptLoop::frame(input);
    if (!scene_) {
        return;
    }
    // 0211e078: the button picked (ワーク[0], 1..35; 255 none).
    const Value picked = Value::ofText(name(voice::kPicked));
    const int n = story_.state.variable(picked);
    if (n == 0 || n == 255) {
        return;
    }
    lscr::Scene& sc = *scene_;
    auto word = [&](std::uint32_t table, int i) {
        const std::uint32_t at = table + 4u * std::uint32_t(i);
        return int(story_.assets.codeWord(voice::kOverlay, kOverlayLoad, at));
    };
    // Its line (16-byte rows {kind, a, b, c}): kind 0 an ordinary stream
    // (str/nrm), 1 an event's voice {group, event, line}.
    const std::uint32_t row = voice::kLines + 16u * std::uint32_t(n - 1);
    if (Adventure3Sound* snd = story_.sound) {
        snd->stopStream(0); // 020d0820(0)
        const int kind = word(row, 0);
        if (kind == 0) {
            snd->openStream(Adventure3Sound::Ordinary, word(row, 1));
        } else if (kind == 1) {
            const int event = word(row, 1) << 16 | word(row, 2);
            snd->openStream(Adventure3Sound::Event, event, word(row, 3));
        }
        snd->startStream(1.0f, 0, 0);
    }
    // The speaker's balloon and portrait.
    const int speaker = word(voice::kSpeakers, n - 1);
    if (Object* balloon = sc.find(name(voice::kBalloon))) {
        lscr::play(*balloon,
                   lscr::sequenceOf(*balloon,
                                    Value::ofText(formatted(name(voice::kBalloonFormat), speaker))),
                   0);
    }
    for (int i = 1; i <= voice::kPortraits; ++i) {
        if (Object* p = sc.find(formatted(name(voice::kPortrait), i))) {
            const bool on = i == speaker;
            const std::string sequence =
                formatted(name(on ? voice::kPortraitOn : voice::kPortraitOff), i);
            lscr::play(*p, lscr::sequenceOf(*p, Value::ofText(sequence)), 0);
        }
    }
    if (const auto* line = tableRow(sc, name(voice::kText), n)) {
        setText(sc, name(voice::kMessage), (*line)[1]);
    }
    story_.state.setVariable(picked, 0);
    sc.setAllInput(true);
}

// --- SCRT_MOVIE (overlay 29) ------------------------------------------------------------

namespace movie {
constexpr int kOverlay = 29;
constexpr std::uint32_t kButton = 0x0211e380, kPage = 0x0211e38c, kMovies = 0x0211e130;
constexpr int kButtons = 37;
} // namespace movie

SecretMovieLoop::SecretMovieLoop(Story& story, LoopArgs args)
    : ScriptLoop(story, "SCRT_MOVIE", args, "evt/prg/lt3_scrt_movie.cbin") {}

std::string SecretMovieLoop::name(std::uint32_t address) const {
    return story_.codeName(address, movie::kOverlay).toText();
}

void SecretMovieLoop::start() {
    // 0211e084: the first page (ワーク[15]); it stays across the movies.
    story_.state.setVariable(Value::ofText(name(movie::kPage)), 1);
}

lscr::Result SecretMovieLoop::command(int index, lscr::Call& call) {
    lscr::Scene& sc = *scene_;
    if (index == 0 && call.count() >= 1) {
        // 1280: every button slides by dx.
        const int dx = fx::fromFloat(call.real(0));
        for (int i = 1; i <= movie::kButtons; ++i) {
            if (Object* b = sc.find(formatted(name(movie::kButton), i))) {
                slide(*b, dx);
            }
        }
    } else if (index == 2 && call.count() >= 2) {
        // 1282: button n's movie (the list at 0211e130).
        const int n = call.integer(0);
        if (n >= 1 && n <= movie::kButtons) {
            put(call, 1,
                Value::of(int(story_.assets.codeWord(movie::kOverlay, kOverlayLoad,
                                                     movie::kMovies + 4u * std::uint32_t(n - 1)))));
        }
    }
    return lscr::Result::Next; // 1281: the buttons are found by name when used
}

// --- SCRT_IMAGE (overlay 30) ------------------------------------------------------------

namespace image {
constexpr int kOverlay = 30;
constexpr std::uint32_t kResource = 0x0211e5d0, kObject = 0x0211e5dc, kNumber = 0x0211e5e4,
                        kText = 0x0211e5ec, kTitle = 0x0211e5f4, kMessage = 0x0211e600,
                        kPictures = 0x0211e474;
constexpr int kCount = 20;
} // namespace image

SecretImageLoop::SecretImageLoop(Story& story, LoopArgs args)
    : ScriptLoop(story, "SCRT_IMAGE", args, "evt/prg/lt3_scrt_image.cbin") {}

std::string SecretImageLoop::name(std::uint32_t address) const {
    return story_.codeName(address, image::kOverlay).toText();
}

void SecretImageLoop::activate() {
    ScriptLoop::activate(); // 0211dfb4
    step_ = 0;
    picture_ = 1;
    first_ = false;
    resource_.clear();
    object_.clear();
    oldResource_.clear();
    oldObject_.clear();
}

lscr::Result SecretImageLoop::command(int index, lscr::Call& call) {
    if (index != 0 || call.count() < 1) {
        return lscr::Result::Next;
    }
    // 1280 {dx}: the picture dx away (wrapping in 1..20) starts loading to
    // the upper screen.
    const int dx = call.integer(0);
    first_ = dx == 0;
    picture_ += dx;
    if (picture_ < 1) {
        picture_ = image::kCount;
    } else if (picture_ > image::kCount) {
        picture_ = 1;
    }
    resource_ = formatted(name(image::kResource), picture_);
    const std::uint32_t path = story_.assets.codeWord(
        image::kOverlay, kOverlayLoad, image::kPictures + 4u * std::uint32_t(picture_ - 1));
    scene_->loadPicture(resource_, 2, story_.assets.codeText(image::kOverlay, kOverlayLoad, path));
    step_ = 1;
    return lscr::Result::Next;
}

void SecretImageLoop::frame(const lscr::SceneInput& input) {
    ScriptLoop::frame(input);
    if (!scene_) {
        return;
    }
    lscr::Scene& sc = *scene_;
    if (step_ == 1) {
        // Loaded (02063334; at once here): the picture, number, title and
        // text.
        step_ = 2;
        object_ = formatted(name(image::kObject), picture_);
        sc.create(Object::Kind::Picture, object_, resource_);
        if (Object* n = sc.find(name(image::kNumber))) {
            n->user[0] = Value::of(picture_);
        }
        if (const auto* row = tableRow(sc, name(image::kText), picture_)) {
            setText(sc, name(image::kTitle), (*row)[0]);
            setText(sc, name(image::kMessage), (*row)[1]);
        }
    } else if (step_ == 2) {
        // The frame after: the picture before goes, and input comes back
        // (not for the first picture: the program turns it on itself).
        if (!oldObject_.empty() && oldObject_ != object_) {
            sc.remove(oldObject_);
        }
        oldObject_ = object_;
        if (!oldResource_.empty() && oldResource_ != resource_) {
            sc.freeResource(oldResource_);
        }
        oldResource_ = resource_;
        step_ = 3;
        if (!first_) {
            sc.setAllInput(true);
        }
    }
}

// --- SCRT_SOUND (overlay 31) ------------------------------------------------------------

namespace sound {
constexpr int kOverlay = 31;
constexpr std::uint32_t kButton = 0x0211ea90, kAnimation = 0x0211ea9c, kSectionN = 0x0211eaa4,
                        kSectionT = 0x0211eaac, kSectionC = 0x0211eab4, kStartPicture = 0x0211eac0,
                        kText = 0x0211eacc, kTitle = 0x0211ead4, kNumber = 0x0211eae0,
                        kIcons = 0x0211e894, kPieceTable = 0x0211e54c;
constexpr int kPieces = 26, kPerPage = 15;
} // namespace sound

SecretSoundLoop::SecretSoundLoop(Story& story, LoopArgs args)
    : ScriptLoop(story, "SCRT_SOUND", args, "evt/prg/lt3_scrt_sound.cbin") {}

std::string SecretSoundLoop::name(std::uint32_t address) const {
    return story_.codeName(address, sound::kOverlay).toText();
}

std::array<int, 2> SecretSoundLoop::piece(int index) const {
    const std::uint32_t at = sound::kPieceTable + 8u * std::uint32_t(index);
    return {int(story_.assets.codeWord(sound::kOverlay, kOverlayLoad, at)),
            int(story_.assets.codeWord(sound::kOverlay, kOverlayLoad, at + 4))};
}

void SecretSoundLoop::activate() {
    ScriptLoop::activate(); // 0211e400
    page_ = 1;
    startPicture_ = true;
    step_ = current_ = wait_ = 0;
}

lscr::Result SecretSoundLoop::command(int index, lscr::Call& call) {
    lscr::Scene& sc = *scene_;
    switch (index) {
    case 0: { // 1280 {dx}: the other page slides in, this one out.
        if (call.count() < 1) {
            break;
        }
        const int dx = call.integer(0);
        const float shift = float(dx) * 256.0f;
        page_ += dx;
        if (page_ < 1) {
            page_ = 2;
        } else if (page_ > 2) {
            page_ = 1;
        }
        for (int i = 0; i < sound::kPieces; ++i) {
            Object* b = sc.find(formatted(name(sound::kButton), i));
            if (!b) {
                continue;
            }
            if (page_ - 1 == i / sound::kPerPage) {
                b->offset.set({fx::fromFloat(shift), 0});
            }
            slide(*b, fx::fromFloat(-shift));
        }
        step_ = 1;
        break;
    }
    case 1: // 1281: the buttons, the second page's off screen to the right.
        for (int i = 0; i < sound::kPieces; ++i) {
            auto text = [&](std::uint32_t at) {
                const std::uint32_t p = story_.assets.codeWord(sound::kOverlay, kOverlayLoad, at);
                return Value::ofText(story_.assets.codeText(sound::kOverlay, kOverlayLoad, p));
            };
            const std::uint32_t icons = sound::kIcons + 8u * std::uint32_t(i);
            const std::vector<Value> values{text(icons), text(icons + 4), Value::of(i)};
            // 020f2900: a button as 4018 makes it.
            Object* b = sc.create(Object::Kind::Button, formatted(name(sound::kButton), i),
                                  name(sound::kAnimation));
            if (!b) {
                continue;
            }
            sc.objectThread(*b, values);
            b->releasedSection = name(sound::kSectionN);
            b->touchedSection = name(sound::kSectionT);
            b->clickedSection = name(sound::kSectionC);
            b->touch = 0;
            sc.callOn(*b, b->releasedSection);
            if (i >= sound::kPerPage) {
                b->offset.set({256 * fx::kOne, 0});
            }
        }
        break;
    case 2: { // 1282 {piece}: what plays stops, a click, then the piece.
        if (call.count() < 1) {
            break;
        }
        const int n = call.integer(0);
        Adventure3Sound* snd = story_.sound;
        if (startPicture_) {
            sc.remove(name(sound::kStartPicture));
            sc.freeResource(name(sound::kStartPicture));
            startPicture_ = false;
        } else if (snd) {
            if (piece(current_)[0] == 1) {
                snd->stopStream(0);
            } else if (piece(current_)[0] == 0) {
                snd->stopMusic(0);
            }
        }
        if (snd) {
            snd->playEffect(680);
        }
        if (const auto* row = tableRow(sc, name(sound::kText), n + 1)) {
            setText(sc, name(sound::kTitle), (*row)[1]);
        }
        if (Object* number = sc.find(name(sound::kNumber))) {
            number->user[0] = Value::of(n + 1);
        }
        if (snd) {
            const auto [kind, id] = piece(n);
            if (kind == 1) {
                snd->openStream(Adventure3Sound::Music, id); // 020d0618
            } else if (kind == 0) {
                snd->loadMusic(id, 30); // 020d04fc
            }
        }
        wait_ = 30;
        current_ = n;
        step_ = 2;
        break;
    }
    case 3: // 1283: stop (15 frames).
        if (Adventure3Sound* snd = story_.sound) {
            if (piece(current_)[0] == 1) {
                snd->stopStream(15);
            } else if (piece(current_)[0] == 0) {
                snd->stopMusic(15);
            }
        }
        break;
    default:
        break;
    }
    return lscr::Result::Next;
}

void SecretSoundLoop::frame(const lscr::SceneInput& input) {
    ScriptLoop::frame(input);
    if (!scene_) {
        return;
    }
    if (step_ == 1) {
        if (anyMoving(*scene_)) {
            return;
        }
        step_ = 0;
        scene_->setAllInput(true);
    }
    if (step_ != 2) {
        return;
    }
    if (wait_ != 0) {
        --wait_;
        return;
    }
    if (Adventure3Sound* snd = story_.sound) {
        if (piece(current_)[0] == 1) {
            snd->startStream(1.0f, 0, 0); // 020d07a8
        } else if (piece(current_)[0] == 0) {
            snd->playMusic(1.0f, 0, 1); // 020d05b8
        }
    }
    step_ = 0;
    scene_->setAllInput(true);
}

// --- SCRT_ROOM (overlay 32) -------------------------------------------------------------

namespace room {
constexpr int kOverlay = 32;
constexpr std::uint32_t kMessage = 0x0211e320, kCharacter = 0x0211e330;
constexpr int kSalt = 0x4b55;
// The console's Wi-Fi address (firmware, 027ffcf4): the port has none, so a
// fixed one of its own stands in (a host choice; the password differs from a
// real console's).
constexpr std::array<std::uint8_t, 6> kAddress{0x00, 0x09, 0xbf, 0x4c, 0x33, 0x4a};
} // namespace room

SecretRoomLoop::SecretRoomLoop(Story& story, LoopArgs args)
    : ScriptLoop(story, "SCRT_ROOM", args, "evt/prg/lt3_scrt_room.cbin") {}

std::string SecretRoomLoop::password(const std::array<std::uint8_t, 6>& a, int salt) {
    // Eight bytes mixed from the address and the salt; even places become a
    // digit (the byte mod 9), odd ones a letter A..G (mod 7).
    const int b0 = a[0], b1 = a[1], b2 = a[2], b3 = a[3], b4 = a[4], b5 = a[5];
    const int sum02 = (b0 + b3) & 0xff;
    const int mix13 = (b1 + b3 - b4) & 0xff;
    const int all = (b2 + b3 + b4 + b5) & 0xff;
    const std::array<int, 8> bytes{sum02 * salt,
                                   mix13 + salt,
                                   all * salt,
                                   b3 - salt,
                                   b4 * salt,
                                   b5 + salt,
                                   ((all + sum02 + mix13) & 0xff) * salt,
                                   ((b3 - b4 - b5) & 0xff) - salt};
    std::string out;
    for (int i = 0; i < 8; ++i) {
        const int byte = bytes[std::size_t(i)] & 0xff;
        out += i % 2 == 0 ? char('0' + byte % 9) : char('A' + byte % 7);
    }
    return out;
}

void SecretRoomLoop::activate() {
    ScriptLoop::activate(); // 0211df00
    // A centred message per character on the upper screen, 16 pixels apart
    // from x 68, y 170.
    const std::string pass = password(room::kAddress, room::kSalt);
    const std::string format = story_.codeName(room::kMessage, room::kOverlay).toText();
    for (int i = 0; i < 8; ++i) {
        if (Object* m = scene_->createMessage(formatted(format, i), lscr::Screen::Upper, 16, 0)) {
            const std::string character =
                story_.codeName(room::kCharacter, room::kOverlay).toText(); // "%c"
            m->text.set(formatted(character, pass[std::size_t(i)]), scene_->text, true);
            m->position.set({fx::fromFloat(68.0f + 16.0f * float(i)), 170 * fx::kOne});
            m->text.style.centerX = true;
        }
    }
}

// --- Factory ----------------------------------------------------------------------------

std::unique_ptr<Loop> makeMenuLoop(Story& story, const std::string& name, const LoopArgs& args) {
    // 020eb5a4: these set the last argument (SCRT_MOVIE 1, the others 0).
    LoopArgs a = args;
    a[3] = name == "SCRT_MOVIE" ? 1 : 0;
    if (name == "MENU_JITEN") {
        return std::make_unique<MenuJitenLoop>(story, a);
    }
    if (name == "SCRT_CHARA") {
        return std::make_unique<SecretCharaLoop>(story, a);
    }
    if (name == "SCRT_VOICE") {
        return std::make_unique<SecretVoiceLoop>(story, a);
    }
    if (name == "SCRT_MOVIE") {
        return std::make_unique<SecretMovieLoop>(story, a);
    }
    if (name == "SCRT_IMAGE") {
        return std::make_unique<SecretImageLoop>(story, a);
    }
    if (name == "SCRT_SOUND") {
        return std::make_unique<SecretSoundLoop>(story, a);
    }
    if (name == "SCRT_ROOM") {
        return std::make_unique<SecretRoomLoop>(story, a);
    }
    return nullptr;
}

} // namespace picarat::adventure3
