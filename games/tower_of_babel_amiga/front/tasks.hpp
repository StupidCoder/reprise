#pragma once
#include "tower_of_babel_amiga/data/assets.hpp"
#include "tower_of_babel_amiga/front/data_disk.hpp"
#include "tower_of_babel_amiga/front/play.hpp"
#include "tower_of_babel_amiga/front/screen.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// The front end's routines as tasks: what each of the original's routines
// keeps while it waits for vertical blanks ($1261A, the menus' waits),
// held as values so that a quick save can keep them. A task's `at` says
// where it goes on; FrontEnd::run(task) does its work up to the next wait
// and says whether it waits for the next blank, has called another task
// (which runs at once, and hands back its result) or has ended. The
// work itself is in front_end.cpp and designer.cpp.
namespace babel::front {

// A line editor ($12096): its record (place, most characters, rightmost
// x, click box) and text.
struct LineEditor {
    int x{}, y{}, most{}, right{};
    int box_x0{}, box_y0{}, box_x1{}, box_y1{};
    std::string text;
    int cursor{};
    bool digits{}; // only 0-9 ($C(a5) set)

    template <class A> void persist(A& ar) { ar(x, y, most, right, box_x0, box_y0, box_x1, box_y1, text, cursor, digits); }
};

// What a requester does with the name it is given (the original's
// routine pointer passed to $126C0).
enum class DiskAction {
    load_position, save_position, update_scores, load_custom_group, // the game's
    load_group, save_group, load_tower, save_tower, delete_group, delete_tower // the designer's
};

namespace tasks {

// --- waiting

struct WaitFrames { // a number of vertical blanks
    int left{};
    template <class A> void persist(A& ar) { ar(left); }
};
struct WaitRelease { // $11318: until no button is held
    template <class A> void persist(A&) {}
};
struct GameFrameWait { // $87EE: a game frame is 14 ticks of the CIA timer
    template <class A> void persist(A&) {}
};
struct Select { // $112EA: the area clicked (the result)
    std::uint32_t areas{};
    bool looking{};
    Menu menu;
    template <class A> void persist(A& ar) { ar(areas, looking, menu); }
};
struct Fade { // $1252C / $1255C, or at once with fast fades ($12628)
    std::array<data::Colour, 48> target{};
    int first{}, count{48};
    bool started{}, fast{};
    int steps{}; // done so far
    std::array<std::array<int, 3>, 48> value{}, step{};
    template <class A> void persist(A& ar) { ar(target, first, count, started, fast, steps, value, step); }
};

// --- the game

struct Game { // run: title, credits, then the player's name and the main menu in turn
    enum class At : std::uint8_t { start, player };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct Title { // $7EF6
    enum class At : std::uint8_t { start, music };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct Credits { // $15A2E
    enum class At : std::uint8_t { start, faded, show_credits, credits_wait, credits_shown, globe_wait, globe_shown, leave, left };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct CreditsWait { // 100 waits of 11 timer ticks, watching for Space (or a click)
    int count{};
    double start{};
    bool timing{};
    template <class A> void persist(A& ar) { ar(count, start, timing); }
};
struct NameEntry { // $8BD6
    enum class At : std::uint8_t { start, faded, shown };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct EditField { // $12096 on a record: until Return or a click outside its box (the text: FrontEnd::edited_)
    LineEditor editor;
    enum class At : std::uint8_t { start, draw, typed, released };
    At at{};
    int end_x{};
    template <class A> void persist(A& ar) { ar(editor, at, end_x); }
};
struct MainMenu { // $8D84
    enum class At : std::uint8_t { start, redraw, drawn, choose, chosen, restart };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct PlayGame { // $8EAE
    enum class At : std::uint8_t { start, drawn, choose, chosen, played };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct ChooseTower { // $A2DA + $A542 + menu $38EC: the tower chosen (the result), -1 back
    int game_type{};
    int completed{}, available{};
    enum class At : std::uint8_t { start, faded, choose, chosen };
    At at{};
    template <class A> void persist(A& ar) { ar(game_type, completed, available, at); }
};
struct Information { // $A396: whether to play (the result)
    int game_type{}, letter{};
    enum class At : std::uint8_t { start, chosen, faded };
    At at{};
    bool play{};
    template <class A> void persist(A& ar) { ar(game_type, letter, at, play); }
};
struct StandardGame { // $91B4
    enum class At : std::uint8_t { start, choose, chosen, informed, played, after, final_faded };
    At at{};
    int choice{};
    template <class A> void persist(A& ar) { ar(at, choice); }
};
struct TutorialGame { // $8F20
    enum class At : std::uint8_t { start, choose, chosen, informed, played, after };
    At at{};
    int choice{};
    template <class A> void persist(A& ar) { ar(at, choice); }
};
struct CustomGame { // $8FE4
    enum class At : std::uint8_t { start, drawn, choose, chosen, group_loaded, choose_tower, tower_chosen, informed, played, after };
    At at{};
    int choice{};
    template <class A> void persist(A& ar) { ar(at, choice); }
};
struct PlayTower { // $969A: how it ended (the result: 1 won, 2-4 lost, 5 abandoned)
    PlayTower() = default;
    PlayTower(data::Tower t, int type, bool freeze = false) : tower(std::move(t)), game_type(type), frozen(freeze) {}
    data::Tower tower;
    int game_type{};
    bool frozen{};
    enum class At : std::uint8_t { start, unpack, faded_in, frame, framed, jingle, released, paused, after };
    At at{};
    int reason{}, leave{};
    std::optional<Play> play; // (kept by FrontEnd::persist_task)
    template <class A> void persist(A& ar) { ar(tower, game_type, frozen, at, reason, leave); }
};
struct PauseMenu { // $9704: how to leave the tower (the result: 0 go on, 5 abandon, 6 restart)
    enum class At : std::uint8_t { start, choose, chosen, released, viewed, exited };
    At at{};
    int choice{};
    template <class A> void persist(A& ar) { ar(at, choice); }
};
struct SetupPage { // $9AC4
    enum class At : std::uint8_t { start, choose, chosen };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct ViewPage { // $9C68
    enum class At : std::uint8_t { start, choose, chosen };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct InfoPage { // $9FB6
    enum class At : std::uint8_t { start, chosen, task_closed };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct ExitPage { // $A060: the result as PauseMenu's
    int game_type{};
    enum class At : std::uint8_t { start, chosen, confirmed };
    At at{};
    int choice{}, leave{};
    template <class A> void persist(A& ar) { ar(game_type, at, choice, leave); }
};
struct Confirm { // $110D2: yes (the result); `question` in place of the title text when given
    Confirm() = default;
    explicit Confirm(std::uint32_t text) : title(text) {}
    explicit Confirm(std::string q) : question(std::move(q)) {}
    std::uint32_t title{};
    std::string question;
    enum class At : std::uint8_t { start, released, chosen, answered };
    At at{};
    bool yes{};
    template <class A> void persist(A& ar) { ar(title, question, at, yes); }
};
struct WonScreen { // $92EC
    int letter{}, tries{};
    enum class At : std::uint8_t { start, faded, settle, settled, bonus, count, counted, shown, next_bonus, pause, paused, ok, finished };
    At at{};
    int k{}, chunk{}, i{};
    template <class A> void persist(A& ar) { ar(letter, tries, at, k, chunk, i); }
};
struct EndScreen { // $97AA
    int game_type{}, reason{};
    enum class At : std::uint8_t { start, faded, finished };
    At at{};
    template <class A> void persist(A& ar) { ar(game_type, reason, at); }
};
struct InterludeOk { // $948C
    enum class At : std::uint8_t { start, framed, clicked, faded };
    At at{};
    bool lit{};
    template <class A> void persist(A& ar) { ar(at, lit); }
};
struct DemoSequence { // $15780
    enum class At : std::uint8_t { start, show, frame, framed, next, ended };
    At at{};
    int index{12}, next{};
    template <class A> void persist(A& ar) { ar(at, index, next); }
};

// --- the data disk

struct SaveLoad { // $A6B6
    enum class At : std::uint8_t { start, drawn, choose, chosen, done };
    At at{};
    int choice{};
    template <class A> void persist(A& ar) { ar(at, choice); }
};
struct HighScores { // $A81E
    enum class At : std::uint8_t { start, faded, chosen, updated };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct Requester { // $126C0: whether the action was carried out without an error (the result)
    Requester() = default;
    Requester(FileKind k, std::uint32_t text, DiskAction a) : kind(k), title(text), action(a) {}
    FileKind kind{};
    std::uint32_t title{};
    DiskAction action{};
    enum class At : std::uint8_t { start, draw, typed, act, replace_answered, error_shown, closed, finished };
    At at{};
    int page{}, error{}, end_x{};
    bool ok{}, done{};
    std::vector<std::string> names;
    Menu menu;
    template <class A> void persist(A& ar) { ar(kind, title, action, at, page, error, end_x, ok, done, names, menu); }
};
struct ReplaceFile { // $12D6C: yes (the result)
    enum class At : std::uint8_t { start, released, chosen, answered };
    At at{};
    bool yes{};
    template <class A> void persist(A& ar) { ar(at, yes); }
};
struct DiskError { // $12CFE
    int code{};
    enum class At : std::uint8_t { start, chosen };
    At at{};
    template <class A> void persist(A& ar) { ar(code, at); }
};

// --- the tower designer (designer.cpp)

struct Designer { // $E386
    enum class At : std::uint8_t { start, top, look, looking, chosen, filed, views, leave };
    At at{};
    Menu menu;
    bool exit{}, wait{true};
    int choice{-1};
    template <class A> void persist(A& ar) { ar(at, menu, exit, wait, choice); }
};
struct ChooseSlot { // Get / Put / Swap Tower: a letter, its name below, then Yes/No (the result: the slot, -1 none)
    int y0{}, y1{};
    std::uint32_t title{}, areas{}, question{};
    enum class At : std::uint8_t { start, choose, chosen, answered };
    At at{};
    int choice{};
    template <class A> void persist(A& ar) { ar(y0, y1, title, areas, question, at, choice); }
};
struct TowerUnlocked { // $EBD2: a locked tower asks for its password (the result: allowed)
    std::array<std::uint8_t, data::tower_record_size> record{};
    int letter{};
    enum class At : std::uint8_t { start, typed, shown };
    At at{};
    bool allowed{};
    template <class A> void persist(A& ar) { ar(record, letter, at, allowed); }
};
struct DesignerFile { // the File menu (the result: exit the designer)
    enum class At : std::uint8_t {
        start, choose, chosen, next,
        get_chosen, get_unlocked, put_chosen, swap_chosen, swap_unlocked, locked, lock_shown,
        tower_loaded, tower_unlocked, prepare_chosen, prepare_confirmed, prepared, exit_confirmed
    };
    At at{};
    int command{}, slot{};
    bool close{true}, exit{};
    template <class A> void persist(A& ar) { ar(at, command, slot, close, exit); }
};
struct DesignerOptions {
    enum class At : std::uint8_t { start, choose, chosen, named, authored, tested };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct TextPage { // Name Tower, Tower's Author ($F27E, $F2AE): the text (FrontEnd::edited_)
    TextPage() = default;
    TextPage(int top, std::uint32_t text, LineEditor e) : y0(top), title(text), editor(std::move(e)) {}
    int y0{};
    std::uint32_t title{};
    LineEditor editor;
    enum class At : std::uint8_t { start, edited, shown };
    At at{};
    std::string text;
    template <class A> void persist(A& ar) { ar(y0, title, editor, at, text); }
};
struct EffectsPage { // $ED56
    enum class At : std::uint8_t { start, choose, chosen };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct SoundPage { // $EE0C
    enum class At : std::uint8_t { start, choose, chosen };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct MoonPage { // $EEB2
    enum class At : std::uint8_t { start, choose, chosen };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct BehaviourPage { // $F008
    enum class At : std::uint8_t { start, choose, chosen };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct FinishingPage { // $F0B0
    enum class At : std::uint8_t { start, choose, chosen, timed };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct TestDesign { // Play/View Tower ($F2DE)
    enum class At : std::uint8_t { start, played };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct DesignerToolbox {
    enum class At : std::uint8_t { start, choose, chosen, coloured, paged, cleared_level, cleared_all };
    At at{};
    bool done{};
    template <class A> void persist(A& ar) { ar(at, done); }
};
struct ScrollPage { // $F978 / $FA9E (the result: something was done)
    bool rotate{};
    enum class At : std::uint8_t { start, choose, chosen };
    At at{};
    template <class A> void persist(A& ar) { ar(rotate, at); }
};
struct LevelPage { // $FBB0 / $FC76 (the result as ScrollPage's)
    bool swap{};
    enum class At : std::uint8_t { start, chosen };
    At at{};
    template <class A> void persist(A& ar) { ar(swap, at); }
};
struct ColoursPage { // $F38E
    enum class At : std::uint8_t { start, choose, chosen, preset_chosen, step_choose, step_chosen, after, tested };
    At at{};
    bool leave{}, test{};
    template <class A> void persist(A& ar) { ar(at, leave, test); }
};
struct DesignerExtras {
    enum class At : std::uint8_t { start, choose, chosen, set_up };
    At at{};
    bool done{};
    template <class A> void persist(A& ar) { ar(at, done); }
};
struct DesignerSetup { // $1027E
    enum class At : std::uint8_t { start, choose, chosen };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct StatisticsPage { // $10384
    enum class At : std::uint8_t { start, chosen };
    At at{};
    template <class A> void persist(A& ar) { ar(at); }
};
struct SupportPage { // $104AE
    enum class At : std::uint8_t { start, mark, chosen };
    At at{};
    std::vector<int> reached;
    std::size_t next{};
    bool passed{};
    Screen::Cursor cursor;
    template <class A> void persist(A& ar) { ar(at, reached, next, passed, cursor); }
};
struct PathPage { // $10614
    enum class At : std::uint8_t { start, spider, spider_waited, mark, spider_done, chosen };
    At at{};
    int spider{};
    std::array<int, 256> marks{};
    std::vector<int> reached;
    std::size_t next{};
    bool passed{};
    template <class A> void persist(A& ar) { ar(at, spider, marks, reached, next, passed); }
};
struct MousePage { // $1082E / $1087C
    bool style{};
    enum class At : std::uint8_t { start, choose, chosen };
    At at{};
    template <class A> void persist(A& ar) { ar(style, at); }
};

} // namespace tasks

using Task = std::variant<
    tasks::WaitFrames, tasks::WaitRelease, tasks::GameFrameWait, tasks::Select, tasks::Fade,
    tasks::Game, tasks::Title, tasks::Credits, tasks::CreditsWait, tasks::NameEntry, tasks::EditField, tasks::MainMenu,
    tasks::PlayGame, tasks::ChooseTower, tasks::Information, tasks::StandardGame, tasks::TutorialGame, tasks::CustomGame,
    tasks::PlayTower, tasks::PauseMenu, tasks::SetupPage, tasks::ViewPage, tasks::InfoPage, tasks::ExitPage, tasks::Confirm,
    tasks::WonScreen, tasks::EndScreen, tasks::InterludeOk, tasks::DemoSequence,
    tasks::SaveLoad, tasks::HighScores, tasks::Requester, tasks::ReplaceFile, tasks::DiskError,
    tasks::Designer, tasks::ChooseSlot, tasks::TowerUnlocked, tasks::DesignerFile, tasks::DesignerOptions, tasks::TextPage,
    tasks::EffectsPage, tasks::SoundPage, tasks::MoonPage, tasks::BehaviourPage, tasks::FinishingPage, tasks::TestDesign,
    tasks::DesignerToolbox, tasks::ScrollPage, tasks::LevelPage, tasks::ColoursPage, tasks::DesignerExtras,
    tasks::DesignerSetup, tasks::StatisticsPage, tasks::SupportPage, tasks::PathPage, tasks::MousePage>;

} // namespace babel::front
