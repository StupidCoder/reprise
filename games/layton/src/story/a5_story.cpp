// The fifth adventure's loop stack and its story loops. See a5_story.hpp.
#include "story/a5_story.hpp"
#include "story/a5_horse.hpp"
#include "story/a5_ruins.hpp"
#include "story/a5_rabbit.hpp"
#include "story/a5_robot.hpp"
#include "story/a5_secrets.hpp"
#include "story/a5_shop.hpp"
#include "graphics/a56_font.hpp"
#include "graphics/a56_text.hpp"
#include "puzzles/a5_puzzle.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>

namespace picarat::adventure5 {

using xq::Value;

namespace {

// Names the loops use (Shift-JIS).
constexpr const char* kRunningEvent =
    "\x89\xd2\x93\xad\x92\x86\x82\xcc\x83\x43\x83\x78\x83\x93\x83\x67"; // 稼働中のイベント
constexpr const char* kDxFlag = "\x82\x63\x82\x77\x94\xc5";             // ＤＸ版
// 0x1a73b0 / 0x1a73c4: the talk window's bubble and name plate switches,
// cleared when an event comes on top.
constexpr const char* kNoBubble =
    "\x89\xef\x98\x62\x90\x81\x82\xab\x8f\x6f\x82\xb5\x82\xc8\x82\xb5"; // 会話吹き出しなし
constexpr const char* kNoPlate = "\x89\xef\x98\x62\x96\xbc\x8e\x44\x82\xc8\x82\xb5"; // 会話名札なし
constexpr const char* kPlaceVariable =
    "\x8c\xbb\x8d\xdd\x82\xcc\x83\x76\x83\x8c\x83\x43\x83\x58"; // 現在のプレイス
constexpr const char* kFirstPlace = "\x93\xec\x92\xca\x82\xe8"; // 南通り

int valueInt(const Value& v, int fallback = -1) {
    return v.isNumber() ? v.toInt() : fallback;
}

void clearFlag(Game& game, const char* name) {
    const int id = game.flagId(name);
    if (id >= 0) {
        game.setFlag(id, false);
    }
}

// ------------------------------------------------------------ game loop

// lt5CLoopGame (creator 0x379880): the bottom of the stack.
class GameLoop final : public Loop {
public:
    using Loop::Loop;

    void start() override {
        // 0x182788: the loop's scope, one display frame per game frame
        // (0x2d3a84), the resident scripts, the fonts. Not modelled: the
        // resident sky "sky/s000" (0x32b8b8) and the system effect set
        // (0x2bcefc): the port's scene and sound load them where used.
        Loop::start();
        game_.displayFramesPerFrame = 1;
        for (const char* s :
             {"seq/prog_common", "seq/prog_saveload", "seq/prog_memo", "seq/prog_event"}) {
            game_.loadScript(s);
        }
        for (const auto& [name, path] :
             {std::pair<const char*, const char*>{"_FNT_NRM", "fnt/[eu]/nrm"},
              {"_FNT_SUB", "fnt/[eu]/sml"},
              {"_FNT_SML", "fnt/[eu]/sml"}}) {
            if (!game_.scene.resource(name)) {
                const auto folder = game_.pack.find(path, ".xf");
                if (!folder.empty()) {
                    adventure56::Resource r;
                    r.kind = adventure56::Resource::Kind::Font;
                    r.font = adventure56::loadFont(game_.pack, folder);
                    game_.scene.addResource(name, std::move(r));
                }
            }
        }
    }

    void resume() override {
        // 0x1829d8: the first time, the logos (when value 3 is negative);
        // the second, the save data is checked, "ＤＸ版" set when the
        // language index (0x2cdd08: byte 0x411d79, an index into the code
        // table 0x411df4 "jp", "en", "uk", "fr", "ge", ...) is 0 - the
        // Japanese game only, so the "《ＤＸ》" events (spin puzzles, absent
        // from the other languages' puzzle table) stay off elsewhere - and
        // the title asked for; after that the loop ends.
        // (The save checks and their error loops 7000..7020 are the port's
        // own: see the saves.)
        Loop::resume();
        if (stage_ == 0) {
            stage_ = 1;
            if (valueInt(values[3]) < 0 && !story_.startAtTitle) {
                story_.push(LogoLoopId, {});
                return;
            }
        } else if (stage_ != 1) {
            story_.pop();
            return;
        }
        stage_ = 2;
        if (const int dx = game_.flagId(kDxFlag); dx >= 0 && game_.language == 0) {
            game_.setFlag(dx, true);
        }
        story_.push(TitleLoopId, {});
        if (!story_.pending()) {
            story_.pop();
        }
    }

    void afterFrame() override {}

private:
    int stage_ = 0; // +0x160
};

// ------------------------------------------------------------ name entry

// lt5CLoopMenuNmae (creator 0x37b554, commands 0x1c4a88): the name
// keyboard. A script loop of menu_name with the keyboard table in the
// game's data (0x3e9638: 9 pages of 5 rows {y, keys of 16 bytes {x, kind,
// text}}, a key 24 pixels square; kind 1 types its text, 2..4 the Japanese
// sound marks, 5 / 6 shift and caps lock - the script's). The name is kept
// at +0x15d; +0x1a4 counts the keys typed, at most +0x1a0 (10; 8 in modes 4
// and 6: the passwords); the mode is the first value.
class NameLoop final : public ScriptLoop {
public:
    NameLoop(Story& story, int id, std::vector<Value> values)
        : ScriptLoop(story, id, std::move(values), "menu_name") {
        mode_ = std::max(0, valueInt(this->values[0], 0));
        // 0x37b554: the language (2930) picks single-byte keys (+0x1ac 0,
        // +0x1ae 1) unless it is 0 or 1 (Japanese: two bytes).
        japanese_ = game_.language <= 1 && game_.language == 0;
        charBytes_ = japanese_ ? 2 : 1;
        limit_ = mode_ == 4 || mode_ == 6 ? 8 : mode_ == 1 && japanese_ ? 5 : 10;
    }

    void keyboard(const NameInput& key) override { typed_.push_back(key); }

    bool command(int number, xq::Call& c, std::size_t first) override {
        auto put = [&](std::size_t i, Value v) {
            if (Value* r = c.reg(first + i)) {
                *r = std::move(v);
            }
        };
        switch (number) {
        case 1000: // {-> pages, key width, key height, mode}
            if (c.count() < first + 4) {
                return false;
            }
            put(0, Value::ofInt(9));
            put(1, Value::ofInt(kKeySize));
            put(2, Value::ofInt(kKeySize));
            put(3, Value::ofInt(mode_));
            c.result = Value::ofInt(1);
            return true;
        case 1001: // 0x1c4e20 {page, x, y, -> key x, row y, kind, cursor}
            return keyAt(c, first);
        case 1002: { // 0x1c5160 {1 space | 2 delete, -> cursor}
            if (c.count() < first + 2) {
                return false;
            }
            const int what = c.integer(first);
            if (what == 1) {
                append(japanese_ ? "\x81\x40" : " ");
            } else if (what == 2) {
                if (count_ > 0) {
                    dropLast();
                    if (auto* o = game_.scene.object(boxName(count_ - 1))) {
                        o->text.clear();
                    }
                    --count_;
                }
            } else {
                return false;
            }
            put(1, Value::ofInt(cursor()));
            c.result = Value::ofText(name_);
            return true;
        }
        case 1003: // a text as the name box shows it (padded in Japanese)
            if (c.count() <= first) {
                return false;
            }
            c.result = c.at(first).isText() ? Value::ofText(c.at(first).text) : Value{};
            return true;
        case 1004: { // {x}: the name's character boxes "nb%d", 25 pixels apart
            if (c.count() <= first) {
                return false;
            }
            const int x = c.integer(first);
            adventure56::Resource* font = game_.scene.resource("_FNT_NRM");
            for (int i = 0; i < limit_; ++i) {
                auto& o = game_.createObject(boxName(i), 1, 0x84, 0, {});
                auto at = o.position.value();
                at[0] = float(x + i * 25);
                at[1] = 5.0f;
                o.position.set(at);
                if (font && font->font) {
                    o.font = font->font;
                    o.content = 15;
                }
                o.centreH = true; // 0x2bfc68
                o.textColour = 1; // 0x172a60
            }
            c.result = Value::ofInt(1);
            return true;
        }
        case 1006: { // {text}: "\\" replaced by the string at *0x412818
            c.result = Value::ofInt(0);
            if (c.count() < first + 2 || !c.at(first + 1).isText()) {
                return false;
            }
            const std::string with = game_.codeText(game_.codeWord(0x412818));
            std::string out;
            for (const char ch : c.at(first + 1).text) {
                out += ch == '\\' ? with : std::string(1, ch);
            }
            if (Value* r = c.reg(first)) {
                *r = Value::ofText(out);
            }
            c.result = Value::ofInt(1);
            return true;
        }
        default:
            return false;
        }
    }

    void update() override {
        typeNext();
        ScriptLoop::update();
    }

private:
    static constexpr std::uint32_t kTable = 0x3e9638; // pages of 0x11d words
    static constexpr std::uint32_t kPage = 0x11d * 4, kRow = 0xe4, kKey = 16;
    static constexpr int kKeySize = 24;

    static std::string boxName(int i) { return "nb" + std::to_string(i); }

    int cursor() const { return std::min(count_, limit_ - 1); }

    // 0x294568: a key's text joins the name (when full, it replaces the
    // last character) and shows in its box.
    void append(const std::string& text) {
        if (count_ < limit_) {
            name_ += text;
            ++count_;
        } else {
            dropLast();
            name_ += text;
        }
        if (auto* o = game_.scene.object(boxName(count_ - 1))) {
            o->text = text;
        }
    }

    // The last character goes: an escape "{xy}" (4 bytes) or one character.
    void dropLast() {
        const std::size_t n = name_.size();
        if (n < std::size_t(charBytes_)) {
            return;
        }
        if (name_[n - std::size_t(charBytes_)] == '}' && n >= 4) {
            name_.resize(n - 4);
        } else {
            name_.resize(n - std::size_t(charBytes_));
        }
    }

    std::uint32_t row(int page, int r) const {
        return kTable + std::uint32_t(page - 1) * kPage + std::uint32_t(r) * kRow;
    }

    bool keyAt(xq::Call& c, std::size_t first) {
        if (c.count() < first + 7) {
            return false;
        }
        const int page = c.integer(first), x = c.integer(first + 1), y = c.integer(first + 2);
        // The row: -1 above the first; else the first band [y, y + 24) that
        // holds the pen, 5 past the last (0x1c4e20 reads on into memory).
        int r = -1;
        if (y >= std::int32_t(game_.codeWord(row(page, 0)))) {
            for (r = 0; r < 5; ++r) {
                const int top = std::int32_t(game_.codeWord(row(page, r)));
                if (y >= top && y < top + kKeySize) {
                    break;
                }
            }
        }
        int kind = 0, k = 0;
        std::uint32_t key = 0;
        if (r >= 0) {
            for (;; ++k) {
                key = row(page, r) + 4 + std::uint32_t(k) * kKey;
                const int kx = std::int32_t(game_.codeWord(key));
                if (kx < 0 || (kx <= x && x < kx + kKeySize)) {
                    break;
                }
            }
            kind = std::int8_t(game_.codeWord(key + 4) & 0xff);
        }
        if (kind == 1) {
            append(game_.codeText(key + 5, 11));
        } else if (kind >= 2 && kind <= 4 && !name_.empty() && name_.size() >= 2) {
            soundMark(kind);
        }
        auto put = [&](std::size_t i, int v) {
            if (Value* reg = c.reg(first + i)) {
                *reg = Value::ofInt(v);
            }
        };
        put(3, r >= 0 ? std::int32_t(game_.codeWord(key)) : 0);
        put(4, r >= 0 ? std::int32_t(game_.codeWord(row(page, r))) : 0);
        put(5, kind);
        put(6, cursor());
        c.result = Value::ofText(name_);
        return true;
    }

    // Kinds 2..4: the last kana changes by a pair table (0x3ebe4c dakuten,
    // 0x3ebf25 handakuten, 0x3ebf76 small): found at a pair start, the next
    // pair; else the one before.
    void soundMark(int kind) {
        static constexpr std::uint32_t kTables[3] = {0x3ebe4c, 0x3ebf25, 0x3ebf76};
        const std::uint32_t table = kTables[kind - 2];
        const std::string list = game_.codeText(table, 512);
        const std::string last = name_.substr(name_.size() - 2);
        const std::size_t at = list.find(last);
        if (at == std::string::npos) {
            return;
        }
        std::string with;
        if (at % 4 == 0 && at + 3 < list.size()) {
            with = list.substr(at + 2, 2);
        } else if (at >= 2) {
            with = list.substr(at - 2, 2);
        } else {
            return;
        }
        name_.replace(name_.size() - 2, 2, with);
    }

    // A key typed on the computer's keyboard: when the script waits for
    // keys (state r4000 0, no key cooling down r4013), it goes through the
    // script's own blocks: the press placed on the key and InputKey (after
    // ChangeKey to the key's page when it is on another), InputSpecialKey 1
    // / 2 for space / delete, ChangeStat 2 for OK.
    void typeNext() {
        if (typed_.empty() || game_.input.touching) {
            return;
        }
        const xq::Program* script = game_.loadScript("seq/menu_name");
        if (!script || script->statics.size() < 14) {
            return;
        }
        const auto& s = script->statics;
        if (s[0].toInt() != 0 || s[13].toInt() > 0) {
            return;
        }
        const NameInput key = typed_.front();
        auto call = [&](const char* block, std::vector<Value> args) {
            threads[0]->callback(block, args);
        };
        switch (key.action) {
        case NameInput::Action::Insert: {
            if (key.character == ' ') {
                call("InputSpecialKey", {Value::ofInt(1)});
                break;
            }
            const auto at = findKey(key.character, s[1].toInt());
            if (!at) {
                break;
            }
            if (at->page != s[1].toInt()) {
                call("ChangeKey", {Value::ofInt(at->page), Value::ofInt(0), Value::ofInt(0)});
                return; // types next frame, on its page
            }
            game_.pressX = at->x + kKeySize / 2;
            game_.pressY = at->y + kKeySize / 2;
            call("InputKey", {});
            break;
        }
        case NameInput::Action::Erase:
            call("InputSpecialKey", {Value::ofInt(2)});
            break;
        case NameInput::Action::Confirm:
            call("ChangeStat", {Value::ofInt(2)});
            break;
        default:
            break;
        }
        typed_.pop_front();
    }

    struct KeyPlace {
        int page = 0, x = 0, y = 0;
    };

    // The key that types a character: the European pages 6..9 (Latin-1
    // letters with marks are the accent page's escapes "{:a}" ...).
    std::optional<KeyPlace> findKey(std::uint8_t ch, int current) {
        std::string want(1, char(ch));
        static const std::map<std::uint8_t, const char*> kMarks = {
            {0xe4, "{:a}"}, {0xf6, "{:o}"}, {0xfc, "{:u}"}, {0xc4, "{:A}"}, {0xd6, "{:O}"},
            {0xdc, "{:U}"}, {0xdf, "{ss}"}, {0xe0, "{`a}"}, {0xe1, "{'a}"}, {0xe2, "{^a}"},
            {0xe8, "{`e}"}, {0xe9, "{'e}"}, {0xea, "{^e}"}, {0xeb, "{:e}"}, {0xec, "{`i}"},
            {0xed, "{'i}"}, {0xee, "{^i}"}, {0xef, "{:i}"}, {0xf2, "{`o}"}, {0xf3, "{'o}"},
            {0xf4, "{^o}"}, {0xf9, "{`u}"}, {0xfa, "{'u}"}, {0xfb, "{^u}"}, {0xe7, "{,c}"},
            {0xf1, "{-n}"}, {0xc7, "{,C}"}, {0xd1, "{-N}"}, {0xc0, "{`A}"}, {0xc1, "{'A}"},
            {0xc2, "{^A}"}, {0xc8, "{`E}"}, {0xc9, "{'E}"}, {0xca, "{^E}"}, {0xcb, "{:E}"},
            {0xcc, "{`I}"}, {0xcd, "{'I}"}, {0xce, "{^I}"}, {0xcf, "{:I}"}, {0xd2, "{`O}"},
            {0xd3, "{'O}"}, {0xd4, "{^O}"}, {0xd9, "{`U}"}, {0xda, "{'U}"}, {0xdb, "{^U}"},
            {0xa1, "{^!}"}, {0xbf, "{^?}"}};
        if (const auto it = kMarks.find(ch); it != kMarks.end()) {
            want = it->second;
        }
        std::vector<int> pages = {current, 6, 7, 8, 9};
        for (const int page : pages) {
            if (page < 6 || page > 9) {
                continue;
            }
            for (int r = 0; r < 5; ++r) {
                const int y = std::int32_t(game_.codeWord(row(page, r)));
                for (int k = 0; k < 14; ++k) {
                    const std::uint32_t key = row(page, r) + 4 + std::uint32_t(k) * kKey;
                    const int x = std::int32_t(game_.codeWord(key));
                    if (x < 0) {
                        break;
                    }
                    if ((game_.codeWord(key + 4) & 0xff) == 1 &&
                        game_.codeText(key + 5, 11) == want) {
                        return KeyPlace{page, x, y};
                    }
                }
            }
        }
        return std::nullopt;
    }

    std::string name_;
    int count_ = 0, limit_ = 10, mode_ = 0, charBytes_ = 1;
    bool japanese_ = false;
    std::deque<NameInput> typed_;
};

// ------------------------------------------------------------ event loop

// lt5CLoopEvent (creator 0x379be4): values {event number, branch (-1: by
// the kind's conditions)}.
class EventLoop final : public Loop {
public:
    EventLoop(Story& story, int id, std::vector<Value> values)
        : Loop(story, id, std::move(values)), playback_(game_) {}

    void start() override {
        // 0x2d4c44 -> 0x339478: the event (else the loop ends at once).
        Loop::start();
        const EventRecord* event = game_.story.event(valueInt(values[0]));
        if (!event) {
            if (game_.log) {
                game_.log("event " + std::to_string(valueInt(values[0])) + " not found");
            }
            story_.pop();
            return;
        }
        playback_.open(event, valueInt(values[1]));
    }

    void resume() override {
        // 0x1a72e4: the loop takes no input until its script lets it; the
        // talk window's switches are cleared; then 0x3394f0.
        Loop::resume();
        if (!playback_.event()) {
            return;
        }
        game_.objectTouchLocked = true;
        clearFlag(game_, kNoBubble);
        clearFlag(game_, kNoPlate);
        playback_.enter();
    }

    void begin() override {
        // 0x3393ac -> 0x3393d4 on thread 0, a running Main kept (the event
        // loop's +0x15c bit 0 is clear, 0x379c84).
        Loop::begin();
        if (playback_.event()) {
            playback_.run(*this, 0, true);
        }
    }

    void suspend() override {
        playback_.leave();
        Loop::suspend();
    }

    void end() override {
        // 0x1a72b8 -> 0x29e3f4.
        playback_.close();
        Loop::end();
    }

    void update() override {
        playback_.keepCurrent();
        Loop::update();
    }

private:
    EventPlayback playback_;
};

// ------------------------------------------------------------ place loop

// lt5CLoopPlace (creator 0x379cf4, vtable 0x3fff74): values {mode, event}.
// A script loop of prog_place ("Init" once, "Main" on begin).
class PlaceLoop final : public ScriptLoop {
public:
    PlaceLoop(Story& story, int id, std::vector<Value> values)
        : ScriptLoop(story, id, std::move(values), "prog_place") {}

    void start() override {
        // 0x1a7418: the scope, prog_place, "南通り" when no place is
        // current, two display frames per game frame (0x2d3af8).
        ScriptLoop::start();
        if (game_.variable(kPlaceVariable) < 0) {
            if (const PlaceRecord* p = game_.story.place(kFirstPlace)) {
                game_.setVariable(kPlaceVariable, p->id);
            }
        }
        game_.displayFramesPerFrame = 2;
    }

    void resume() override {
        // 0x1a753c: the place; without one the loop ends. The named
        // conditions are computed anew; in mode 0 a ruins place (110..200)
        // hands over to the ruins loop 6400 {floor - 1, 0}; else the
        // place's script (res/plc/p<id>) loads and the loop takes no input
        // until its script lets it.
        Loop::resume();
        const PlaceRecord* place = game_.story.place(game_.variable(kPlaceVariable));
        if (!place) {
            story_.pop();
            return;
        }
        refreshConditions(game_);
        if (valueInt(values[0], 0) == 0 && unsigned(place->id - 110) < 0x5b) {
            story_.push(6400, {Value::ofInt(place->id / 10 - 10), Value::ofInt(0)});
            return;
        }
        char path[32];
        std::snprintf(path, sizeof path, "res/plc/p%04d", place->id);
        game_.loadScript(path);
        game_.objectTouchLocked = true;
    }

    void end() override {
        // 0x1a749c: the level-0 things the place made go: the finder, the
        // environment effect, every screen's room (0x333bd4), the lower
        // screen's area map (kinds 5 and 11, both slots), the shown area.
        game_.scene.remove(xq::hash("map_finder"));
        game_.scene.removeResource(xq::hash("lt6_plc_chr_env_eff"));
        for (int s = 0; s < 5; ++s) {
            game_.removeRoom(s);
        }
        game_.releaseAreaMap(3);
        ScriptLoop::end();
    }
};

// ------------------------------------------------------------ puzzle loop

// lt5CLoopNazo (creator 0x379a8c): values {puzzle id, number, kind}. The
// session (puzzles/a5_puzzle.hpp) runs its play loop inside.
class PuzzleLoop final : public Loop {
public:
    using Loop::Loop;

    void start() override {
        Loop::start();
        const int id = valueInt(values[0]);
        if (!game_.defs.puzzle(id)) {
            story_.pop();
            return;
        }
        // 0x18394c: a puzzle not solved yet is seen now and ナゾヒント
        // holds the hints it has had (0x33f450); a solved one starts with
        // none. 0x18320c: the verdict (ナゾ判定) starts at -1.
        auto& p = game_.progress[id];
        if ((p.state & 3) == 2) {
            game_.setVariable(kHintVariable, 0);
        } else {
            game_.setPuzzleState(id, 1);
            game_.setVariable(kHintVariable, p.hints);
        }
        game_.setVariable(kVerdictVariable, -1);
        // 0x18394c: one display frame per game frame (0x2d3af8).
        game_.displayFramesPerFrame = 1;
        id_ = id;
        session_ = std::make_unique<PuzzleSession>(game_, id, false);
    }

    void update() override {
        if (!session_) {
            return;
        }
        session_->update();
    }

    void afterFrame() override {
        if (session_ && session_->ended() && !story_.pending() && !ending_) {
            ending_ = true;
            story_.pop();
        }
    }

    void end() override {
        if (session_) {
            book();
        }
        session_.reset();
        game_.special.reset();
        game_.puzzle = nullptr;
        game_.loopThreads[0] = game_.loopThreads[1] = nullptr;
        Loop::end();
    }

private:
    // 0x18320c after the session: unless already solved, a correct verdict
    // (ナゾ判定 1) solves the puzzle (0x2d24b8 -> 0x2d1fcc) and counts
    // ノーヒント正解数 (no hint used) and 連続ナゾ解き中, raising
    // れんぞく正解数 to it; a verdict of 3 (put off) counts あとまわし回数
    // up to 999. The hints used are kept (0x185428, at most 4).
    void book() {
        auto& p = game_.progress[id_];
        if ((p.state & 3) == 2) {
            return;
        }
        const int verdict = game_.variable(kVerdictVariable);
        const int hints = game_.variable(kHintVariable);
        auto bump = [this](const char* name) {
            game_.setVariable(name, game_.variable(name) + 1);
        };
        if (verdict == 1) {
            game_.setPuzzleState(id_, 2);
            if (hints == 0) {
                bump(kNoHintVariable);
            }
            bump(kStreakVariable);
            if (game_.variable(kBestStreakVariable) < game_.variable(kStreakVariable)) {
                game_.setVariable(kBestStreakVariable, game_.variable(kStreakVariable));
            }
        } else if (verdict == 3 && game_.variable(kPutOffVariable) < 999) {
            bump(kPutOffVariable);
        }
        p.hints = std::clamp(hints, 0, 4);
    }

    static constexpr const char* kVerdictVariable = "\x83\x69\x83\x5d\x94\xbb\x92\xe8"; // ナゾ判定
    static constexpr const char* kHintVariable =
        "\x83\x69\x83\x5d\x83\x71\x83\x93\x83\x67"; // ナゾヒント
    static constexpr const char* kNoHintVariable =
        "\x83\x6d\x81\x5b\x83\x71\x83\x93\x83\x67\x90\xb3\x89\xf0\x90\x94"; // ノーヒント正解数
    static constexpr const char* kStreakVariable =
        "\x98\x41\x91\xb1\x83\x69\x83\x5d\x89\xf0\x82\xab\x92\x86"; // 連続ナゾ解き中
    static constexpr const char* kBestStreakVariable =
        "\x82\xea\x82\xf1\x82\xbc\x82\xad\x90\xb3\x89\xf0\x90\x94"; // れんぞく正解数
    static constexpr const char* kPutOffVariable =
        "\x82\xa0\x82\xc6\x82\xdc\x82\xed\x82\xb5\x89\xf1\x90\x94"; // あとまわし回数

    std::unique_ptr<PuzzleSession> session_;
    int id_ = 0;
    bool ending_ = false;
};

// The script names of the factory's script loops (0x2c592c).
const char* scriptOf(int id) {
    switch (id) {
    case 2000:
        return "menu_logo";
    case 2010:
        return "menu_title";
    case 2020:
        return "menu_trial";
    case 2030:
        return "menu_name";
    case 2040:
        return "menu_finish";
    case 2050:
        return "menu_v2010";
    case 2100:
        return "menu_bag";
    case 2110:
        return "menu_invest";
    case 2120:
        return "menu_fukamaru";
    case 2130:
        return "menu_jiten";
    case 2140:
        return "menu_episode";
    case 2150:
        return "menu_staffroll";
    case 2160:
        return "menu_collection";
    case 2170:
        return "menu_rabit_select";
    case 2200:
        return "menu_profile_edit";
    case 2210:
        return "menu_profile_view";
    case 2300:
        return "menu_game_coin_shop";
    case 2500:
        return "vhcl_train";
    case 6005: // lt5CLoopMenu (the factory's case at 0x2c7950)
        return "mini_rabbit_select";
    // The wireless menus and the secrets (the factory 0x2c592c: 4000 / 4100
    // as 3610 + 390 / 490, the secrets as 5000 + n; classes by loop_ids.py:
    // lt5CLoopMenu, 4100 and 5000 lt5CLoopMenuNkl, 5210 lt5CLoopScrtChara).
    case 4000:
        return "wifi_menu";
    case 4100:
        return "wifi_select";
    case 5000:
        return "scrt_mode";
    case 5100:
        return "scrt_chlg";
    case 5200:
        return "scrt_menu";
    case 5210:
        return "scrt_chara";
    case 5220:
        return "scrt_image";
    case 5230:
        return "scrt_sound";
    case 5240:
        return "scrt_voice";
    case 5250:
        return "scrt_movie";
    case 5260:
        return "scrt_door";
    case 3010:
        return "nazo_intro";
    case 3020:
        return "nazo_judge";
    case 7000:
        return "error_data";
    case 7010:
        return "error_save";
    case 7020:
        return "error_sram";
    default:
        return nullptr;
    }
}

} // namespace

// ------------------------------------------------------------ Loop

Loop::Loop(Story& story, int id_, std::vector<Value> v)
    : id(id_), story_(story), game_(story.game) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = i < v.size() ? v[i] : Value::ofInt(-1);
    }
    for (auto& t : threads) {
        t = std::make_unique<xq::Thread>(game_.machine, 16);
        for (std::size_t i = 0; i < values.size(); ++i) {
            t->values[i] = values[i];
        }
    }
}

Loop::~Loop() = default;

// ------------------------------------------------------------ events

bool EventPlayback::open(const EventRecord* event, int branch) {
    event_ = event;
    branch_ = nullptr;
    if (!event_) {
        return false;
    }
    makeCurrent();
    const auto kind = game_.story.kinds.find(event_->kind);
    if (kind != game_.story.kinds.end()) {
        if (branch < 0) {
            branch_ = pickBranch(game_, *event_);
        } else if (std::size_t(branch) < kind->second.branches.size()) {
            branch_ = &kind->second.branches[std::size_t(branch)];
        }
    }
    char path[64];
    std::snprintf(path, sizeof path, "evt/%02d/%02d_%06d", event_->group, event_->group, event_->number);
    game_.loadScript(path);
    return true;
}

void EventPlayback::enter() {
    // 0x3394f0: the event is current again, "稼働中のイベント" names it, its
    // sound set and its texts ("lt5_evt": txt/@/GG/GG_NNNNNN.xs).
    if (!event_) {
        return;
    }
    makeCurrent();
    game_.setVariable(kRunningEvent, event_->number);
    if (event_->sound >= 0) {
        game_.selectSoundScene(event_->sound);
    }
    char path[64];
    std::snprintf(path, sizeof path, "txt/@/%02d/%02d_%06d.xs", event_->group, event_->group, event_->number);
    if (const auto texts = game_.pack.find(path, ""); !texts.empty()) {
        adventure56::Resource r;
        r.kind = adventure56::Resource::Kind::Text;
        r.text = adventure56::loadTextList(game_.pack, texts);
        game_.scene.addResource("lt5_evt", std::move(r));
    }
}

void EventPlayback::run(Loop& loop, int thread, bool keep) {
    // 0x3393d4: unless a running Main is kept, the branch's opening
    // effects (0x33cb3c) and Main at the branch's label.
    if (!event_ || (keep && loop.threads[std::size_t(thread)]->running())) {
        return;
    }
    if (branch_) {
        for (const auto& r : branch_->begin) {
            apply(game_, r);
        }
        advance(game_);
        advance(game_);
    }
    loop.runBlock(thread, "Main", branch_ ? branch_->label : std::string());
}

void EventPlayback::leave() {
    // 0x29e378: "稼働中のイベント" is cleared.
    game_.setVariable(kRunningEvent, -1);
}

void EventPlayback::close() {
    // 0x29e3f4: the branch's closing effects (0x33cacc); the current event
    // is forgotten.
    if (event_ && branch_) {
        for (const auto& r : branch_->end) {
            apply(game_, r);
        }
        advance(game_);
        advance(game_);
    }
    if (game_.currentEvent == event_) {
        game_.currentEvent = nullptr;
    }
    event_ = nullptr;
    branch_ = nullptr;
}

void EventPlayback::keepCurrent() {
    if (event_) {
        game_.currentEvent = event_;
    }
}

void EventPlayback::makeCurrent() {
    // 0x184bcc: the event and its puzzle become current.
    game_.currentEvent = event_;
    if (!event_->puzzle.empty()) {
        game_.currentPuzzleName = event_->puzzle;
    }
}

void Loop::start() {
    // 0x2adf3c: the depth below the loop's own scope, then the scope.
    level_ = game_.scene.scopeDepth();
    game_.openScope(true);
}

void Loop::end() {
    // 0x2add74: the threads stop, the input switch clears, the loop's
    // scopes close.
    for (auto& t : threads) {
        t->stop();
    }
    game_.objectTouchLocked = false;
    if (topLevel_ >= 0) {
        game_.closeScopesTo(topLevel_);
        topLevel_ = -1;
    }
    if (level_ >= 0) {
        game_.closeScopesTo(level_);
        level_ = -1;
    }
}

void Loop::resume() {
    // 0x2add1c: a scope for what the loop shows while on top; its input
    // switch clears.
    ++resumes;
    topLevel_ = game_.scene.scopeDepth();
    game_.openScope(true);
    game_.objectTouchLocked = false;
}

void Loop::suspend() {
    // 0x2ad308: the on-top scope closes.
    if (topLevel_ >= 0) {
        game_.closeScopesTo(topLevel_);
        topLevel_ = -1;
    }
}

void Loop::begin() {
    begun = true;
}

void Loop::update() {
    game_.loopThreads[0] = threads[0].get();
    game_.loopThreads[1] = threads[1].get();
    threads[0]->run();
    threads[1]->run();
}

void Loop::afterFrame() {
    if (begun && !story_.pending() && !threadsRunning()) {
        story_.pop();
    }
}

bool Loop::command(int, xq::Call&, std::size_t) {
    return false;
}

bool Loop::threadsRunning() const {
    return threads[0]->running() || threads[1]->running();
}

bool Loop::startBlock(int thread, const std::string& block, const std::string& label) {
    const xq::Program* p = game_.newestScript();
    if (!p) {
        return false;
    }
    game_.loopThreads[0] = threads[0].get();
    game_.loopThreads[1] = threads[1].get();
    if (!threads[std::size_t(thread)]->start(*p, block, label)) {
        return false;
    }
    threads[std::size_t(thread)]->run();
    return threads[std::size_t(thread)]->running();
}

// ------------------------------------------------------------ Story

Story::Story(Game& g) : game(g) {
    game.loops = this;
}

Story::~Story() {
    while (!stack_.empty()) {
        stack_.back()->end();
        stack_.pop_back();
    }
    if (game.loops == this) {
        game.loops = nullptr;
    }
}

std::unique_ptr<Loop> Story::make(int id, std::vector<Value> values) {
    switch (id) {
    case 0:
    case GameLoopId:
        return std::make_unique<GameLoop>(*this, GameLoopId, std::move(values));
    case EventLoopId:
        return std::make_unique<EventLoop>(*this, id, std::move(values));
    case PlaceLoopId:
        return std::make_unique<PlaceLoop>(*this, id, std::move(values));
    case PuzzleLoopId:
    case PuzzleLoopId + 1:
        return std::make_unique<PuzzleLoop>(*this, id, std::move(values));
    case NameLoopId:
        return std::make_unique<NameLoop>(*this, id, std::move(values));
    case RuinsLoopId:
        return makeRuinsLoop(*this, id, std::move(values));
    case HorseLoopId:
        return makeHorseLoop(*this, id, std::move(values));
    case ShopLoopId:
        return makeShopLoop(*this, id, std::move(values));
    case RobotLoopId:
        return makeRobotLoop(*this, id, std::move(values));
    case RabbitLoopId:
    case RabbitCommuLoopId:
    case RabbitNazoLoopId:
    case RabbitShowLoopId:
    case RabbitNoteLoopId:
        return makeRabbitLoop(*this, id, std::move(values));
    case SecretProfilesLoopId:
        return makeSecretProfilesLoop(*this, id, std::move(values));
    default:
        break;
    }
    if (const char* script = scriptOf(id)) {
        return std::make_unique<ScriptLoop>(*this, id, std::move(values), script);
    }
    return nullptr;
}

bool Story::push(int id, const std::vector<Value>& given) {
    std::vector<Value> values = given;
    if (remapLoop) {
        id = remapLoop(id, values);
    }
    if (id != 0 && id != GameLoopId && id != EventLoopId && id != PlaceLoopId &&
        id != PuzzleLoopId && id != PuzzleLoopId + 1 && id != RuinsLoopId && id != HorseLoopId && id != ShopLoopId && id != RobotLoopId && !isRabbitLoop(id) && !scriptOf(id)) {
        if (unknownLoop) {
            unknownLoop(id);
        }
        return false;
    }
    requests_.push_back({3, id, values});
    return true;
}

void Story::pop() {
    requests_.push_back({0, 0, {}});
}

void Story::restart() {
    requests_.push_back({1, 0, {}});
}

bool Story::requestEvent(const Value& event, int branch) {
    const EventRecord* e =
        event.isText() ? game.story.event(event.text) : game.story.event(event.toInt());
    if (!e) {
        return false;
    }
    return push(EventLoopId, {Value::ofInt(e->number), Value::ofInt(branch)});
}

Value Story::loopValue(int i) const {
    const Loop* t = top();
    return t && i >= 0 && i < 8 ? t->values[std::size_t(i)] : Value{};
}

void Story::keyboard(const NameInput& key) {
    if (Loop* t = top()) {
        t->keyboard(key);
    }
}

bool Story::loopCommand(int number, xq::Call& c, std::size_t first) {
    Loop* t = top();
    return t && t->command(number, c, first);
}

std::string Story::stackText() const {
    std::string s;
    for (const auto& l : stack_) {
        s += (s.empty() ? "" : " ") + std::to_string(l->id);
    }
    return s;
}

void Story::carryOut() {
    // 0x257230's requests in order: a pop suspends (+0x38, when resumed)
    // and ends (+0x2c) the top loop; a restart only suspends it; a push
    // suspends it and starts (+0x28) the new one. The loop then on top is
    // resumed by Story::frame once its Init allows.
    while (!requests_.empty()) {
        Request r = std::move(requests_.front());
        requests_.pop_front();
        Loop* t = top();
        if (debugLog) {
            debugLog("loop request " + std::to_string(r.kind) + " id " + std::to_string(r.id) +
                     " on " + (t ? std::to_string(t->id) : std::string("-")));
        }
        auto cover = [](Loop* l) {
            if (l && l->resumed) {
                l->suspend();
                l->resumed = false;
            }
        };
        switch (r.kind) {
        case 0: // pop
            if (t) {
                cover(t);
                t->end();
                stack_.pop_back();
            }
            break;
        case 1: // restart
            cover(t);
            break;
        default: { // push
            auto loop = make(r.id, std::move(r.values));
            if (!loop) {
                break;
            }
            cover(t);
            Loop* l = loop.get();
            stack_.push_back(std::move(loop));
            l->start();
            break;
        }
        }
    }
}

void Story::frame(const Input& input) {
    if (stack_.empty()) {
        // An empty stack asks for the game loop.
        push(GameLoopId, {Value::ofInt(0), Value::ofInt(0), Value::ofInt(-1), Value::ofInt(-1)});
    }
    carryOut();
    if (Loop* t = top()) {
        // The end of the manager's pass: the loop on top resumes once its
        // Init has run (a resume clears "begun", 0x3369cc). Then the frame
        // driver 0x37a0e0: Init once, begin when resumed and allowed.
        if (!t->resumed && !t->resumeWaits()) {
            t->resumed = true;
            t->begun = false;
            t->resume();
        }
        if (!t->initStarted) {
            t->init();
        }
    }
    game.beginFrame(input);
    if (Loop* t = top()) {
        if (t->resumed && !t->begun && !pending() && !t->beginWaits()) {
            t->begin();
        }
        t->update();
        t->afterFrame();
    }
    game.endFrame();
}

Value valueOf(const import::adventure56::DataList& list, const import::adventure56::Operand& o) {
    switch (o.type & 15) {
    case 1:
        return Value::ofInt(std::int32_t(o.value));
    case 2:
        return Value::ofUint(o.value);
    case 3: {
        float f;
        std::memcpy(&f, &o.value, sizeof f);
        return Value::ofFloat(f);
    }
    case 8: {
        if (o.value >= list.strings.size()) {
            return Value::ofText({});
        }
        const auto* s = reinterpret_cast<const char*>(list.strings.data() + o.value);
        return Value::ofText(std::string(s, strnlen(s, list.strings.size() - o.value)));
    }
    default:
        return {};
    }
}

std::vector<DataCommand> readDataList(Game& game, const std::string& path) {
    std::vector<DataCommand> out;
    const auto file = game.pack.find(path, ".xs");
    if (file.empty()) {
        return out;
    }
    const auto list = import::adventure56::parseXscr(game.pack.read(file));
    for (const auto& c : list.commands) {
        DataCommand d;
        d.opcode = c.opcode;
        for (unsigned i = 0; i < c.count; ++i) {
            d.operands.push_back(valueOf(list, list.operands[c.first + i]));
        }
        out.push_back(std::move(d));
    }
    return out;
}

} // namespace picarat::adventure5
