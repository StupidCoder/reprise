#pragma once
// The fourth adventure's story bookkeeping: the small condition and effect
// language of the definitions, the story steps and the event kinds.
//
// How the story moves on
// ----------------------
// The game's progress is a chain of story steps (lt4_def 2201 {id, name,
// goal heading, goal text}). Entering a step applies its effects (2203:
// set flags, variables, the place, ...); a step ends when all of its
// conditions (2204) hold, and the next step in the table begins. The game
// checks this after every change it makes through effects (ov7 020bc024),
// whenever a room comes up and when events begin and end.
//
// Events have a kind (2211 {id, name}): a kind lists branches (2213
// {label, ...}) with conditions (2214), effects when the event begins
// (2215) and when it ends (2216). The first branch whose conditions hold
// is played, from its label in the event script's Main ("#ON", "#OFF",
// "#0".."#5", "#S", "#R", ...). Events keep a "done" flag and a counter
// under their own name.
//
// Conditions and effects are {code, arguments}: 151x flags, 152x
// variables, 155x/159x puzzles, 170x the place and the step, 18xx the
// event being played, 185x the step (ov7 020b6db8 conditions, 020b7564
// effects).
#include "system/a4_services.hpp"
#include <vector>

namespace picarat::adventure4 {

// One condition or effect.
struct Rule {
    int code = 0;
    std::vector<gss::Value> args;
};

// ov7 020b6db8: whether a condition holds.
bool holds(Assets& assets, const GameData& data, const Rule& rule);
bool holdsAll(Assets& assets, const GameData& data, const std::vector<Rule>& rules);
// ov7 020b7564: applies an effect.
void apply(Assets& assets, GameData& data, const Rule& rule);
// ov7 020b7d1c: applies effects in order, then lets the story move on.
void applyAll(Assets& assets, GameData& data, const std::vector<Rule>& rules);

// A story step (lt4_def 2201 with its 2203 effects and 2204 conditions).
struct Step {
    int id = 0;
    std::string name, heading, goal;
    std::vector<Rule> effects, conditions;
};

const std::vector<Step>& steps(Assets& assets);

// The step the story is at (variable "現在のフェーズ", named in ov7 at
// 020bc0b0; 0 at the start).
int currentStep(Assets& assets, const GameData& data);
// ov7 020bc0b8: the story enters a step: the variable takes its id and
// its effects apply.
void enterStep(Assets& assets, GameData& data, const Step& step);
// ov7 020bc024: while the current step's conditions hold, the next step
// begins. True when the step changed.
bool advance(Assets& assets, GameData& data);

// An event kind's branch (lt4_def 2213..2216).
struct Branch {
    std::string label;
    int a = 0, b = 0; // 2213's two numbers (*roles not identified*)
    std::vector<Rule> conditions, begin, end;
};

// The branches of an event kind (2211 id), or null.
const std::vector<Branch>* eventKind(Assets& assets, int kind);

// An event's record (lt4_def 2321): group, number, name, kind id (2211 by
// name), the puzzle (2301 by name), the note (2021 by name).
struct Event {
    int group = 0, number = -1;
    std::string name;
    int kind = -1, puzzle = -1, memo = -1;
    int sound = -1; // the sound set (2321 field 4; record +8, game +0x5b98)
};

// ov7 020c2398: an event by number; null when there is none.
const Event* event(Assets& assets, int number);

// ov7 020bb00c: an item of the collections (lt4_def 2021 {id, name, kind,
// mask, text, items...}, by name or number) is received: a set (416 and
// up) gives its items; others are marked (32..383), counted (below 32) or
// raise a flag (400, 401, and the "new" flags of three id ranges), and a
// notice goes in the queue (at most 16; the oldest goes).
void giveItem(Assets& assets, GameData& data, const gss::Value& key, int index = 0);

// ov7 020c2c5c: a puzzle's state changes (0 new, 1 seen, 2 solved), with
// the counts and flags that follow it.
// True when the state changed.
bool setPuzzleState(Assets& assets, GameData& data, int number, int state);

} // namespace picarat::adventure4
