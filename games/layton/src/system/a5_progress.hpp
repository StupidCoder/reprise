#pragma once
// The fifth adventure's story bookkeeping: story steps, event kinds and
// their branches, the event and place records, and the small condition and
// effect language of the definitions table (lt5_def).
//
// How the story moves on
// ----------------------
// The game is a chain of story steps (lt5_def 2201 {id, name, heading, goal,
// goal place}). Entering a step applies its effects (2203: set flags,
// variables, the place); a step ends when all of its conditions (2204) hold
// - mostly "this event has been played" - and the next step of the table
// begins. The game checks this after every list of effects it applies
// (0x33c83c -> 0x261bb8).
//
// Events (2321 {group, number, name, kind, sound set, puzzle, note, ...})
// have a kind (2211 {id, name}) with branches (2213 {label, a, b}): the
// first branch whose conditions (2214) hold is played from its label in
// the event script's Main ("#OFF", "#ON", "#0".., "#S", "#R", "#F"), with
// effects when the event begins (2215) and ends (2216). An event's "played"
// flag and its counter are the flag and the variable of the event's own
// name.
//
// Conditions (0x121c84) and effects (0x124870) are {code, arguments}; the
// codes are listed at holds() and apply(). The fourth adventure used the
// same design (its effects even keep their numbers).
#include "system/a56_xq.hpp"
#include <map>
#include <string>
#include <vector>

namespace picarat::adventure56 {
class Pack;
}

namespace picarat::adventure5 {

class Game;

// One condition or effect.
struct Rule {
    int code = 0;
    std::vector<xq::Value> args;
};

// A story step (2201 with its 2203 effects and 2204 conditions).
struct Step {
    int id = 0;
    std::string name, heading, goal, goalPlace;
    std::vector<Rule> effects, conditions;
};

// An event kind's branch (2213 {label, a, b}, 2214 conditions, 2215 effects
// at the start, 2216 at the end).
struct Branch {
    std::string label;
    int a = 0, b = 0; // *roles not identified*
    std::vector<Rule> conditions, begin, end;
};

struct EventKind {
    int id = 0;
    std::string name;
    std::vector<Branch> branches;
};

// An event (2321; record built by 0x17c6ac: +0 the name's hash, +4 group <<
// 24 | number, +8 the sound set, +10 the kind, +12 the puzzle, +14 the
// note).
struct EventRecord {
    int group = 0, number = 0;
    std::string name, kindName;
    int sound = -1;
    std::string puzzle, note, episode, place, step, english;
    int kind = -1; // 2211 id, -1 unknown
};

// A named condition (2221 {name} with 2223 rules; "無条件" has none and
// always holds). Places and their exits refer to them by index; the game
// keeps their truth in a bit set refreshed when a place comes on top and
// when an event ends (0x1842fc).
struct NamedCondition {
    std::string name;
    std::vector<Rule> rules;
};

// A place (2142 {id, name, German name, area, icon place, step} with 2144
// {condition, sound scene}, 2145 {condition, room} and 2146 exits {place
// or event, kind, ?, conditions...}): its script is res/plc/p<id>
// (record: +4 id, +6 area, +8 the icon place's name, +0xc the German name,
// lists +0x10 / +0x14 / +0x18).
struct PlaceRecord {
    int id = 0;
    std::string name, german, area, iconPlace, step;
    int areaId = -1;

    struct Choice {
        int condition = -1; // named condition index, -1: always
        int value = -1;     // a sound scene / a room id
    };

    std::vector<Choice> sounds, rooms;

    struct Exit {
        std::string name; // a place or an event
        int kind = 0, extra = 0;
        std::vector<int> conditions;
    };

    std::vector<Exit> exits;
};

// A room (2121 {id, name, map number, variant, ...}).
struct RoomRecord {
    int id = 0;
    std::string name;
    int map = 0;
    std::string variant;
    std::vector<xq::Value> fields; // the whole row
};

// An area (2132 {id, name} with 2134 {condition, value, flag}).
struct AreaRecord {
    int id = 0;
    std::string name;

    struct Choice {
        int condition = -1;
        int value = 0, flag = 0;
    };

    std::vector<Choice> choices;
};

// A character (2111 {id, name, model, ?, typing sound 40..42, x, y, ?, ?};
// record 0x17b87c: +4 id | model << 10 | field 3 << 20 | sound << 30, then
// the four numbers scaled).
struct CharacterRecord {
    int id = 0;
    std::string name;
    int model = 0;  // chr/c%03d.xc (0: none)
    int other = 0;  // 2111 field 3 (*role not identified*)
    int typing = 0; // 0..2: typing sound 40 + typing
    float values[4] = {};
};

// The definitions' story tables.
struct StoryTables {
    std::vector<Step> steps;
    std::map<int, EventKind> kinds;
    std::vector<EventRecord> events;
    std::map<int, std::size_t> eventByNumber;
    std::map<std::string, std::size_t> eventByName;
    std::vector<PlaceRecord> places;
    std::map<std::string, std::size_t> placeByName;
    std::vector<RoomRecord> rooms;
    std::vector<AreaRecord> areas;
    std::vector<NamedCondition> conditions;
    std::vector<CharacterRecord> characters;
    const CharacterRecord* character(const std::string& name) const;
    const CharacterRecord* character(int id) const;
    std::map<std::string, int> conditionIndex;
    // Every row of lt5_def by opcode, as values (for the table services).
    std::map<int, std::vector<std::vector<xq::Value>>> rows;

    const EventRecord* event(int number) const;
    const EventRecord* event(const std::string& name) const;
    const PlaceRecord* place(int id) const;
    const PlaceRecord* place(const std::string& name) const;
    const RoomRecord* room(int id) const;
    const RoomRecord* room(const std::string& name) const;
    const Step* step(int id) const;
    const Step* step(const std::string& name) const;
    int stepIndex(int id) const;
};

enum class Edition;
StoryTables loadStoryTables(const adventure56::Pack& pack, Edition edition);

// 0x121c84: whether a condition holds.
bool holds(Game& game, const Rule& rule);
bool holdsAll(Game& game, const std::vector<Rule>& rules);
// 0x124870: applies one effect.
void apply(Game& game, const Rule& rule);
// 0x33c83c: applies effects in order, then lets the story move on.
void applyAll(Game& game, const std::vector<Rule>& rules);
// 0x261bb8: while the current step's conditions hold, the next step of the
// table begins. True when the step changed.
bool advance(Game& game);
// 0x2abbc4 (effect 1703): the story enters a step: "現在のフェーズ" takes
// its id and its effects apply; unsolved puzzles that name it are marked
// as passed by (bit 8 of their record).
void enterStep(Game& game, const Step& step);
// The step the story is at (variable "現在のフェーズ").
int currentStep(const Game& game);

// 0x33d3c0: whether a named condition holds now.
bool holdsNamed(Game& game, int index);
// 0x1842fc: the named conditions' bit set is computed anew, then the
// guide: every place's way towards the current step's goal place (2201's
// fifth field). From the goal (distance 0) each place's exits that lead to
// a place and are not blocked (an exit's listed named conditions: any that
// holds blocks it; no exit of the fifth adventure lists one) are followed
// depth first in their order (0x184598): a place gets distance + 1 and
// the place it was reached from whenever that is shorter.
void refreshConditions(Game& game);
// 16310 (0x1625c8 -> 0x184644): whether `on` lies on the guide's way from
// `from` to the goal (`from` itself counts).
bool onGuideWay(const Game& game, const PlaceRecord& from, const PlaceRecord& on);
// 0x33ca00 / 0x33c99c: a place's first sound scene / room whose condition
// (from the bit set) holds, else -1.
int placeSound(const Game& game, const PlaceRecord& place);
int placeRoom(const Game& game, const PlaceRecord& place);

// The branch of an event kind the event plays (first whose conditions
// hold), or null.
const Branch* pickBranch(Game& game, const EventRecord& event);

} // namespace picarat::adventure5
