// The fourth adventure's loop stack and its story loops. See a4_game.hpp.
#include "story/a4_game.hpp"
#include "minigames/a4_minigames.hpp"
#include "puzzles/a4_puzzle.hpp"
#include "sound/a4_sound.hpp"
#include "story/a4_script_loop.hpp"
#include "system/a4_definitions.hpp"
#include "system/a4_progress.hpp"
#include <algorithm>
#include <cstdio>
#include <deque>
#include <optional>

namespace picarat::adventure4 {

namespace {
// Overlay 13 (the game loop) loads at 021049e0; its file names.
constexpr std::uint32_t kOverlay13 = 0x021049e0;
constexpr std::uint32_t kCommonScript = 0x02106064;   // evt/prg/lt4_prog_common.cq
constexpr std::uint32_t kMemoScript = 0x02106080;     // evt/prg/lt4_prog_memo.cq
constexpr std::uint32_t kSaveLoadScript = 0x0210609c; // evt/prg/lt4_prog_saveload.cq
// Overlay 7: the script names of the script loops (020c971c).
constexpr std::uint32_t kOverlay7 = 0x020b6820;

std::array<Value, 8> requestValues(std::array<int, 6> ints) {
    std::array<Value, 8> v{};
    for (std::size_t i = 0; i < ints.size(); ++i) {
        v[i] = Value::ofInt(ints[i]);
    }
    v[6] = Value::ofInt(0);
    v[7] = Value::ofInt(0);
    return v;
}

// ------------------------------------------------------------ game loop

// lt4CLoopGame (ov13): the bottom of the stack.
class GameLoop final : public StoryLoop {
public:
    GameLoop(Game& game, std::span<const Value> values) : StoryLoop(game, GameLoopId, values) {
        // 02105370: the game's tables (res/@/lt4_def.cb, 020b8c68), the
        // SY effect bank 0, and the resident scripts in the loop's own
        // level. (The random seeds of the managers' name tables and the
        // check sums of four routines are left out: they guard the
        // original's memory.)
        game.loadResident();
    }

    void resume() override {
        // 02105c38: the second time on top it ends itself (a new game loop
        // follows); otherwise it asks for the title with its third value,
        // and for the logos in front of it when that is negative. (A
        // missing save memory, 8200, and damaged save slots, 8000, are not
        // reconstructed: the port's saves are its own.)
        Loop::resume();
        if (resumes > 1) {
            game_.pop();
            return;
        }
        if (game_.pending()) {
            return;
        }
        if (values_[0].toInt() != 0) {
            return;
        }
        const int title = values_[2].toInt();
        game_.request(TitleLoopId, {title, -1, -1, -1, -1, -1});
        if (title < 0 && !game_.startAtTitle) {
            game_.request(LogoLoopId, {-1, -1, -1, -1, -1, -1});
        }
    }
};

// ------------------------------------------------------------ name entry

// lt4CLoopMenuName (ov29): the name keyboard. A script loop whose script
// (lt4_menu_name) draws the keys and buttons; the loop keeps the name
// (+0x120, at most +0x160 characters: 10, or 8 in mode 4; the mode is the
// first value) and finds the key under the pen in the keyboard table of
// overlay 29 (02105b1c: pages of four rows {y, keys {x, kind, text[8]}},
// a key 19 pixels square; kind 1 types its text).
// The gallery of the secrets (4220, lt4CLoopSecretImage in ov32): the
// script loop plus 2400 0 {step}, which moves through ov32's list of
// pictures (21 once the flag named in ov32 is set, else 20; wrapping round)
// and shows the picture on the upper screen (020cee30); the result is its
// index. (The other secret pages, 4210/4230..4250, are the plain script
// loop under other class names: ov33..35 hold identical code.)
class SecretImageLoop final : public ScriptLoop {
public:
    using ScriptLoop::ScriptLoop;

    void resume() override {
        // 0210542c (on start): the list's length by the flag.
        ScriptLoop::resume();
        count_ = game_.data.flag(game_.assets.codeText(32, kOverlay32, kCompleteFlag)) ? 21 : 20;
    }

    bool command(gss::Call& c, int id) override {
        if (id != 0) {
            return ScriptLoop::command(c, id);
        }
        if (c.count() < 2) {
            return false;
        }
        // 021054b4
        index_ += c.integer(1);
        if (index_ < 0) {
            index_ = count_ - 1;
        } else if (index_ >= count_) {
            index_ = 0;
        }
        const std::string picture = game_.assets.codeText(
            32, kOverlay32,
            game_.assets.codeWord(32, kOverlay32, kPictures + 4 * std::uint32_t(index_)));
        game_.scene.resetArea();
        if (game_.assets.exists(picture)) {
            game_.scene.screens[1].background = &game_.assets.picture(picture);
        }
        c.result = Value::ofInt(index_);
        return true;
    }

private:
    static constexpr std::uint32_t kOverlay32 = 0x02105220;
    static constexpr std::uint32_t kPictures = 0x02105834;     // name pointers
    static constexpr std::uint32_t kCompleteFlag = 0x02105904; // "ゲームコンプリート"
    int index_ = 0, count_ = 20;                               // +0x120, +0x124
};

// The puzzle download (5100, lt4_wifi_load; native class 0x020cc048 with
// overlays 14 and 36): the script follows a job's steps through 2400 0..5
// {0 step: 0 waiting, 1 connecting, 2 connected, 3 downloading, 4 saving,
// 5 done, 10 cancelled, 99 failed; 1 cancelling allowed; 2 {order}: 1
// cancel; 3 the message's number (lt4 text list 40/8000); 4 the error code;
// 5 puzzles added} and starts it with 2411. The Nintendo Wi-Fi Connection is
// gone: the port's job fails the way the original fails without an access
// point (*hypothesis*, after the first adventure's measurement: error 52000
// about 1500 frames after connecting; message 25, "no access point").
class WifiLoadLoop final : public ScriptLoop {
public:
    using ScriptLoop::ScriptLoop;

    bool startJob() override {
        if (step_ != 0) {
            return false;
        }
        step_ = 1;
        frames_ = 0;
        return true;
    }

    bool jobRunning() const override { return step_ >= 1 && step_ < 5; }

    void afterUpdate() override {
        ScriptLoop::afterUpdate();
        if (step_ == 1 && ++frames_ >= kAttempt) {
            step_ = 99;
            message_ = 25;
            error_ = 52000;
        }
    }

    bool command(gss::Call& c, int id) override {
        switch (id) {
        case 0:
            c.result = Value::ofInt(step_);
            return true;
        case 1:
            c.result = Value::ofInt(step_ >= 1 && step_ < 5 ? 1 : 0);
            return true;
        case 2:
            if (c.count() > 1 && c.integer(1) == 1 && step_ >= 1 && step_ < 5) {
                step_ = 10; // cancelled
                message_ = 13;
            }
            c.result = Value::ofInt(1);
            return true;
        case 3:
            c.result = Value::ofInt(message_);
            return true;
        case 4:
            c.result = Value::ofInt(error_);
            return true;
        case 5:
            c.result = Value::ofInt(0);
            return true;
        default:
            return ScriptLoop::command(c, id);
        }
    }

private:
    static constexpr int kAttempt = 1500;
    int step_ = 0, frames_ = 0, message_ = 0, error_ = 0;
};

class NameEntryLoop final : public ScriptLoop {
public:
    NameEntryLoop(Game& game, int id, std::span<const Value> values)
        : ScriptLoop(game, id, values), assets_(game.assets) {
        // 021053f0.
        mode_ = std::max(0, values_[0].toInt());
        limit_ = mode_ == 0 || mode_ == 1 ? 10 : mode_ == 4 ? 8 : 0;
    }

    // Typed keys (as in the other adventures' name entries): a character
    // presses its key the way the pen does - the script's own InputKey
    // block runs with the touch at the key, after ChangeKey has brought up
    // the key's page when it is another - and space, delete and OK run the
    // buttons' blocks (InputSpecialKey 1 / 2, ChangeStat 2). One a frame,
    // while the keyboard is idle (lt4_menu_name's state 0; 1 a key held, 2 OK,
    // 3 back) and the key
    // lock (+14) is off.
    void keyboard(const NameInput& key) override { typed_.push_back(key); }

    bool command(gss::Call& c, int op) override {
        switch (op) {
        case 1000: // 0210551c: the keyboard's layout {rows, key size, ..., mode, limit}
            if (c.count() < 5) {
                return false;
            }
            put(c, 1, 4);
            put(c, 2, 19);
            put(c, 3, 19);
            put(c, 4, mode_);
            if (c.count() > 5) {
                put(c, 5, limit_);
            }
            c.result = Value::ofInt(1);
            return true;
        case 1001: // 021055ac {page, x, y, -> key x, row y, kind, length}: the key at the pen
            return keyAt(c);
        case 1002: // 02105784 {1 space | 2 delete, -> length}
            return special(c);
        default:
            return ScriptLoop::command(c, op);
        }
    }

protected:
    void afterPen() override {
        ScriptLoop::afterPen();
        if (typed_.empty() || services_.touch.down()) {
            return;
        }
        Value* state = scriptStatic(0);
        Value* page = scriptStatic(1);
        Value* lock = scriptStatic(14);
        if (!state || !page || !lock || state->toInt() != 0 || lock->toInt() > 0) {
            return;
        }
        const NameInput key = typed_.front();
        switch (key.action) {
        case NameInput::Action::Insert: {
            if (key.character == ' ') {
                const std::array<Value, 1> args{Value::ofInt(1)}; // the space button
                callback(0, "InputSpecialKey", args);
                break;
            }
            const auto at = findKey(char(key.character), page->toInt());
            if (!at) {
                break; // no such key
            }
            if (at->page != page->toInt()) {
                const std::array<Value, 3> args{Value::ofInt(at->page), Value::ofInt(0),
                                                Value::ofInt(0)};
                callback(0, "ChangeKey", args);
                return; // the key types next frame, on its page
            }
            services_.touch.startX = at->x + kKeySize / 2;
            services_.touch.startY = at->y + kKeySize / 2;
            callback(0, "InputKey", {});
            break;
        }
        case NameInput::Action::Erase:
        case NameInput::Action::Confirm: {
            // Delete is InputSpecialKey 2, OK ChangeStat 2.
            const std::array<Value, 1> args{Value::ofInt(2)};
            callback(0, key.action == NameInput::Action::Erase ? "InputSpecialKey" : "ChangeStat",
                     args);
            break;
        }
        default:
            break;
        }
        typed_.pop_front();
    }

private:
    static constexpr std::uint32_t kOverlay29 = 0x02105220;
    static constexpr std::uint32_t kTablePool = 0x02105774; // -> 02105b1c
    static constexpr std::uint32_t kSpacePool = 0x021058d8; // -> " "
    static constexpr std::uint32_t kBoxPool = 0x021058dc;   // -> "msg_a_name%02d"
    static constexpr int kPage = 0x390, kRow = 0xe4, kKey = 16, kKeySize = 19;
    static constexpr int kPages = 4, kRowKeys = 13;

    static void put(gss::Call& c, std::size_t i, int v) {
        if (Value* r = c.reg(i)) {
            *r = Value::ofInt(v);
        }
    }

    std::uint32_t word(std::uint32_t address) const {
        return assets_.codeWord(29, kOverlay29, address);
    }

    int shown() const { return std::min(count_, limit_ - 1); }

    bool keyAt(gss::Call& c) {
        if (c.count() < 8) {
            return false;
        }
        const std::uint32_t page = word(kTablePool) + std::uint32_t(c.integer(1) - 1) * kPage;
        const int x = c.integer(2), y = c.integer(3);
        // The row: the first whose band [y, y + 19) holds the pen.
        int row = -1;
        if (y >= std::int32_t(word(page))) {
            for (row = 0; row < 4; ++row) {
                const int top = std::int32_t(word(page + std::uint32_t(row) * kRow));
                if (y >= top && y < top + kKeySize) {
                    break;
                }
            }
        }
        int kind = 0, keyX = -1, rowY = -1;
        if (row >= 0 && row < 4) {
            const std::uint32_t r = page + std::uint32_t(row) * kRow;
            rowY = std::int32_t(word(r));
            std::uint32_t key = r + 4;
            for (;; key += kKey) {
                keyX = std::int32_t(word(key));
                if (keyX < 0 || (keyX <= x && x < keyX + kKeySize)) {
                    break;
                }
            }
            kind = std::int32_t(word(key + 4));
            if (kind == 1) {
                type(assets_.codeText(29, kOverlay29, key + 8));
            }
        }
        put(c, 4, keyX);
        put(c, 5, rowY);
        put(c, 6, kind);
        put(c, 7, shown());
        c.result = Value::ofText(name_);
        return true;
    }

    bool special(gss::Call& c) {
        if (c.count() < 3) {
            return false;
        }
        const int what = c.integer(1);
        if (what == 1) {
            type(assets_.codeText(29, kOverlay29, word(kSpacePool)));
        } else if (what == 2) {
            if (count_ > 0) {
                dropLast();
                --count_;
            }
            showAt(count_, "");
        } else {
            return false;
        }
        put(c, 2, shown());
        c.result = Value::ofText(name_);
        return true;
    }

    // 021058e4: a character goes on the end, or replaces the last one when
    // the name is full; its box (msg_a_nameNN) shows it.
    void type(const std::string& text) {
        if (count_ < limit_) {
            name_ += text;
            ++count_;
        } else {
            dropLast();
            name_ += text;
        }
        showAt(count_ - 1, text);
    }

    // The last character goes; an escape ("{:a}") counts as one.
    void dropLast() {
        if (name_.empty()) {
            return;
        }
        if (name_.back() == '}') {
            const auto open = name_.rfind('{');
            name_.erase(open == std::string::npos ? name_.size() - 1 : open);
        } else {
            name_.pop_back();
        }
    }

    void showAt(int index, const std::string& text) {
        char box[32];
        std::snprintf(box, sizeof box, assets_.codeText(29, kOverlay29, word(kBoxPool)).c_str(),
                      index);
        if (Message* m = scene_.message(box)) {
            scene_.setText(*m, text, true, false);
        }
    }

    // The menu script's statics (lt4_menu_name: 0 state, 1 key page, 14
    // the key lock).
    Value* scriptStatic(std::size_t i) const {
        const gss::Program* p = threads[0] ? threads[0]->program() : nullptr;
        return p && i < p->statics.size() ? const_cast<Value*>(&p->statics[i]) : nullptr;
    }

    struct KeyPlace {
        int page = 0, x = 0, y = 0;
    };

    // The key typing `c` (its text as plain Windows-1252, escapes such as
    // "{:a}" resolved), on the current page when it has one, else on the
    // first page that does. Pages 1..4 of the table; the accents' page has
    // no end mark, 13 keys a row.
    std::optional<KeyPlace> findKey(char c, int current) const {
        std::optional<KeyPlace> found;
        for (int page = 1; page <= kPages; ++page) {
            const std::uint32_t p = word(kTablePool) + std::uint32_t(page - 1) * kPage;
            for (int row = 0; row < 4; ++row) {
                const std::uint32_t r = p + std::uint32_t(row) * kRow;
                std::uint32_t k = r + 4;
                for (int n = 0; n < kRowKeys; ++n, k += kKey) {
                    const int x = std::int32_t(word(k));
                    if (x < 0) {
                        break;
                    }
                    if (std::int32_t(word(k + 4)) != 1) {
                        continue;
                    }
                    const std::string text =
                        plainText(assets_, assets_.codeText(29, kOverlay29, k + 8));
                    if (text.size() == 1 && text[0] == c) {
                        const KeyPlace place{page, x, std::int32_t(word(r))};
                        if (page == current) {
                            return place;
                        }
                        if (!found) {
                            found = place;
                        }
                    }
                }
            }
        }
        return found;
    }

    Assets& assets_;
    std::string name_;
    int mode_ = 0, limit_ = 0, count_ = 0;
    std::deque<NameInput> typed_;
};

// ----------------------------------------------------------------- event

// lt4CLoopEvent (ov16): a scene. Its first value is the event's number,
// the second a branch of its kind (negative: the first whose conditions
// hold). Runs Main of evt/GG/GG_NNNNNN with lt4_prog_event's macros, from
// the branch's label.
class EventLoop final : public StoryLoop {
public:
    EventLoop(Game& game, std::span<const Value> values) : StoryLoop(game, EventLoopId, values) {
        // 02104e24: it ends itself once Main is done.
        endsWithMain = true;
        // 02104bb0: an unknown event ends at once.
        Assets& assets = game.assets;
        event_ = event(assets, values_[0].toInt());
        if (!event_) {
            game.pop();
            return;
        }
        makeCurrent();
        if (const auto* branches = eventKind(assets, event_->kind)) {
            const int pick = values_[1].toInt();
            if (pick < 0) {
                // 020b7f4c: the first branch whose conditions hold.
                for (const Branch& b : *branches) {
                    if (holdsAll(assets, game.data, b.conditions)) {
                        branch_ = &b;
                        break;
                    }
                }
            } else if (std::size_t(pick) < branches->size()) {
                branch_ = &(*branches)[std::size_t(pick)];
            }
        }
        scene_.loadScript(assets.codeText(16, kOverlay16, kEventMacros));
        char path[40];
        std::snprintf(path, sizeof path, "evt/%02d/%02d_%06d.cq", event_->group, event_->group,
                      event_->number);
        scene_.loadScript(path);
    }

    ~EventLoop() override {
        // 02104cac: the branch's closing effects; no event is current.
        if (event_) {
            if (branch_) {
                applyAll(game_.assets, game_.data, branch_->end);
                advance(game_.assets, game_.data);
            }
            game_.data.event = -1;
        }
    }

    void resume() override {
        // 02104ce4: the event is current again with its sound set, and its
        // texts load under "lt4_evt" (txt/@/GG/GG_NNNNNN).
        Loop::resume();
        if (!event_) {
            return;
        }
        makeCurrent();
        if (game_.sound) {
            game_.sound->selectSet(event_->sound, 60, 0);
        }
        scene_.loadTexts(game_.assets.codeText(16, kOverlay16, kTextsName), event_->group,
                         event_->number);
    }

    void suspend() override {
        // 02104d8c: covered, no event is current.
        game_.data.event = -1;
        Loop::suspend();
    }

    void begin() override {
        // 02104db0: a scene still running goes on; otherwise the branch's
        // opening effects apply and Main starts at its label.
        if (!event_ || running(0)) {
            return;
        }
        std::string label;
        if (branch_) {
            applyAll(game_.assets, game_.data, branch_->begin);
            advance(game_.assets, game_.data);
            label = branch_->label;
        }
        if (const gss::Program* p = scene_.newestScript()) {
            start(*p, "Main", 0, label);
        }
    }

private:
    static constexpr std::uint32_t kOverlay16 = 0x021049e0;
    static constexpr std::uint32_t kEventMacros = 0x02105080; // evt/prg/lt4_prog_event.cq
    static constexpr std::uint32_t kTextsName = 0x0210509c;   // lt4_evt

    void makeCurrent() {
        // ov7 020c3824: the event and its puzzle become current.
        game_.data.event = event_->number;
        if (event_->puzzle > 0) {
            game_.data.puzzle = findPuzzle(game_.assets, event_->puzzle);
        }
    }

    const Event* event_ = nullptr;
    const Branch* branch_ = nullptr;
};

// ----------------------------------------------------------------- place

// lt4CLoopPlace (ov15): a room. Whenever it comes to the top it loads the
// current place's script (res/plc/pNNNN.cq) with lt4_prog_place's macros,
// lets the story move on and begins at PlaceMain.
class PlaceLoop final : public StoryLoop {
public:
    PlaceLoop(Game& game, std::span<const Value> values) : StoryLoop(game, PlaceLoopId, values) {}

    void resume() override {
        // 02104bd8: no place: the loop ends.
        Loop::resume();
        const int place = game_.data.variable(placeVariable());
        const auto* row = definitions(game_.assets).find(2151, 0, Value::ofInt(place));
        if (!row) {
            game_.pop();
            return;
        }
        scene_.loadScript(game_.assets.codeText(15, kOverlay15, kPlaceMacros));
        char path[32];
        std::snprintf(path, sizeof path, "res/plc/p%04d.cq", place);
        scene_.loadScript(path);
        advance(game_.assets, game_.data);
        // (020c28a8 then works out the routes between places for the map:
        // *not reconstructed yet*.)
    }

    void begin() override {
        // 02104cac.
        if (const gss::Program* p = scene_.newestScript()) {
            start(*p, game_.assets.codeText(15, kOverlay15, kPlaceMain));
        }
    }

private:
    static constexpr std::uint32_t kOverlay15 = 0x021049e0;
    static constexpr std::uint32_t kPlaceMacros = 0x02105d60; // evt/prg/lt4_prog_place.cq
    static constexpr std::uint32_t kPlaceMain = 0x02105d7c;   // PlaceMain

    std::string placeVariable() const {
        // ov7 020c0848 reads 現在のプレイス (pool 020c08d0).
        return game_.assets.codeText(7, kOverlay7, game_.assets.codeWord(7, kOverlay7, 0x020c08d0));
    }
};

// The script names of ov7 020c971c, by loop number.
struct ScriptName {
    int id;
    std::uint32_t name;
};

constexpr ScriptName kScriptLoops[] = {
    {2000, 0x020e3924}, // lt4_menu_title
    {2010, 0x020e3938}, // lt4_menu_logo
    {2020, 0x020e394c}, // lt4_menu_trial
    {2100, 0x020e3960}, // lt4_menu_bag
    {2110, 0x020e3970}, // lt4_menu_invest
    {2120, 0x020e3984}, // lt4_menu_fukamaru
    {2130, 0x020e399c}, // lt4_menu_jiten
    {2140, 0x020e39b0}, // lt4_menu_episode
    {2150, 0x020e39c4}, // lt4_menu_staffroll
    {2200, 0x020e39dc}, // lt4_nazo_intro
    {2210, 0x020e39f0}, // lt4_nazo_judge
    {2300, 0x020e3a04}, // lt4_menu_name
    {2400, 0x020e3a18}, // lt4_menu_nazoba
    {2500, 0x020e3a2c}, // lt4_vhcl_car
    {2510, 0x020e3a3c}, // lt4_vhcl_boat
    {2600, 0x020e3a50}, // lt4_menu_finish
    {4000, 0x020e3a64}, // lt4_scrt_mode
    {4100, 0x020e3a78}, // lt4_scrt_chlng
    {4200, 0x020e3a8c}, // lt4_scrt_menu
    {4210, 0x020e3aa0}, // lt4_scrt_chara
    {4220, 0x020e3ab4}, // lt4_scrt_image
    {4230, 0x020e3ac8}, // lt4_scrt_sound
    {4240, 0x020e3adc}, // lt4_scrt_voice
    {4250, 0x020e3af0}, // lt4_scrt_movie
    {4260, 0x020e3a04}, // lt4_menu_name
    {5000, 0x020e3b04}, // lt4_wifi_nazo
    {5010, 0x020e3b18}, // lt4_wifi_mcds
    {5100, 0x020e3b2c}, // lt4_wifi_load
    {6000, 0x020e3b40}, // lt4_mini_fish
    {6100, 0x020e3b8c}, // lt4_mini_puppet_main
    {6110, 0x020e3ba4}, // lt4_mini_puppet_menu
    {6120, 0x020e3bbc}, // lt4_mini_puppet_item
    {6200, 0x020e3bd4}, // lt4_mini_train
    {6300, 0x020e3be8}, // lt4_mini_mouse
    {6400, 0x020e3b54}, // lt4_mini_fish_intr_main
    {6401, 0x020e3b70}, // lt4_mini_fish_intr_menu
    {8100, 0x020e3c10}, // lt4_error_save
    {8200, 0x020e3c24}, // lt4_error_sram
};
} // namespace

StoryLoop::StoryLoop(Game& game, int number, std::span<const Value> values)
    : Loop(game.scene, game.services, values), game_(game) {
    id = number;
}

Game::Game(Assets& a, Adventure4Sound* s, bool withGameLoop, std::filesystem::path savesFolder)
    : assets(a), scene(a), saves(std::move(savesFolder)), sound(s), withGameLoop_(withGameLoop) {
    data.layout = &dataLayout(assets);
    services.audio = s;
    services.saves = &saves;
    showSlots(data, saves);
    services.loops = this;
    installServices(scene.machine, services);
    data.language = assets.languageIndex();
    scene.language = data.language;
    if (!withGameLoop) {
        // What the game loop below would have loaded.
        ++scene.level;
        loadResident();
    }
}

void Game::loadResident() {
    // 02105370: the SY effect bank 0 and the resident scripts.
    if (sound) {
        sound->loadBank(0, 0);
    }
    for (std::uint32_t name : {kCommonScript, kMemoScript, kSaveLoadScript}) {
        scene.loadScript(assets.codeText(13, kOverlay13, name));
    }
}

Game::~Game() {
    while (!stack_.empty()) {
        stack_.pop_back();
    }
}

bool Game::request(int id, std::array<int, 6> ints) {
    // 020c971c: refused while the top loop holds requests (+0x48 bit 1).
    std::array<Value, 8> values = requestValues(ints);
    for (const ScriptName& s : kScriptLoops) {
        if (s.id == id) {
            values[6] = Value::ofText(assets.codeText(7, kOverlay7, s.name));
            break;
        }
    }
    if (id == PuzzleLoopId) {
        // 020c971c: a session's values are the puzzle, its index (record
        // +6, 020c12c8) and the caller's second number.
        const GameData::Puzzle p = findPuzzle(assets, ints[0]);
        if (p.number == 0) {
            return false;
        }
        values[1] = Value::ofInt(p.index);
        values[2] = Value::ofInt(ints[1]);
    }
    switch (id) {
    case GameLoopId:
    case PlaceLoopId:
    case EventLoopId:
    case PuzzleLoopId:
    case 3100:
    case 3110:
    case 3120:
    case 3130:
    case 3140:
    case 3150:
    case 3170:
    case 3180:
    case 3190:
    case 3200:
        break;
    default:
        if (values[6].isText()) {
            break;
        }
        if (unknownLoop) {
            unknownLoop(id);
        }
        return false;
    }
    requests_.push_back({2, id, values});
    return true;
}

bool Game::pop() {
    // 020cc2e0.
    requests_.push_back({0, 0, {}});
    return true;
}

bool Game::restart() {
    // 020cc2a0.
    requests_.push_back({1, 0, {}});
    return true;
}

std::unique_ptr<Loop> Game::make(int id, std::span<const Value> values) {
    if (auto loop = makePuzzleLoop(id, scene, services, values, top())) {
        return loop;
    }
    if (id == GameLoopId) {
        return std::make_unique<GameLoop>(*this, values);
    }
    if (id == EventLoopId) {
        return std::make_unique<EventLoop>(*this, values);
    }
    if (id == PlaceLoopId) {
        return std::make_unique<PlaceLoop>(*this, values);
    }
    if (id == 2300) {
        return std::make_unique<NameEntryLoop>(*this, id, values);
    }
    if (id == 4260) {
        // 020cb0a0: the secret door's keyboard: mode 4 in the first value.
        std::vector<Value> door(values.begin(), values.end());
        if (!door.empty()) {
            door[0] = Value::ofInt(4);
        }
        return std::make_unique<NameEntryLoop>(*this, id, door);
    }
    if (auto loop = makeMinigameLoop(*this, id, values)) {
        return loop;
    }
    if (id == 5100 && values.size() > 6 && values[6].isText()) {
        return std::make_unique<WifiLoadLoop>(*this, id, values);
    }
    if (id == 4220 && values.size() > 6 && values[6].isText()) {
        return std::make_unique<SecretImageLoop>(*this, id, values);
    }
    if (values.size() > 6 && values[6].isText()) {
        return std::make_unique<ScriptLoop>(*this, id, values);
    }
    return nullptr;
}

void Game::carryOut() {
    // 020aa650: in order; a pop waits while the top loop holds its end.
    bool changed = false;
    while (!requests_.empty()) {
        Request r = requests_.front();
        Loop* t = top();
        if (r.kind == 0 && t && t->holdsEnd()) {
            break;
        }
        requests_.pop_front();
        if (t && t->onTop) {
            t->suspend();
            t->onTop = false;
        }
        if (r.kind == 0) {
            if (t) {
                stack_.pop_back();
            }
        } else if (r.kind == 2) {
            if (auto loop = make(r.id, r.values)) {
                stack_.push_back(std::move(loop));
            }
        }
        changed = true;
    }
    if (changed) {
        // The loop now on top resumes (vtable +0x20; gnCLoop 020a9c30:
        // counts it, not begun yet).
        if (Loop* t = top(); t && !t->onTop) {
            t->onTop = true;
            t->begun = false;
            ++t->resumes;
            t->resume();
        }
    }
}

void Game::frame(bool touching, int x, int y, std::uint16_t keys) {
    // 020b6820: an empty stack asks for a new game loop (values 0, 0, -1).
    if (withGameLoop_ && stack_.empty() && requests_.empty()) {
        requests_.push_back({2, GameLoopId, requestValues({0, 0, -1, -1, -1, -1})});
    }
    carryOut();
    ++data.frames;
    ++data.playFrames;
    services.keys.update(keys);
    Loop* t = top();
    if (!t) {
        return;
    }
    // 020aae6c: the top loop begins once (nothing loads in the port).
    if (!t->begun) {
        t->begun = true;
        t->begin();
    }
    t->frame(touching, x, y);
    // 020cc5c4 ends with the film process's frame (020ccdac); after the
    // drawing, 020cc330 sets a room up once its files are in (020cf080).
    scene.updateFilm();
    scene.updateRoom();
}

} // namespace picarat::adventure4
