// The fifth adventure's story bookkeeping. See a5_progress.hpp.
#include "system/a5_progress.hpp"
#include "graphics/a56_assets.hpp"
#include "import/a56_formats.hpp"
#include "system/a5_game.hpp"
#include <cstring>
#include <stdexcept>

namespace picarat::adventure5 {

namespace fs = std::filesystem;
using xq::Value;

namespace {

// Names of the game's own variables (Shift-JIS).
constexpr const char* kStepVariable =
    "\x8c\xbb\x8d\xdd\x82\xcc\x83\x74\x83\x46\x81\x5b\x83\x59"; // 現在のフェーズ
constexpr const char* kPlaceVariable =
    "\x8c\xbb\x8d\xdd\x82\xcc\x83\x76\x83\x8c\x83\x43\x83\x58"; // 現在のプレイス
constexpr const char* kFoundTotal =
    "\x82\xdd\x82\xc2\x82\xaf\x82\xbd\x83\x69\x83\x5d\x91\x8d\x90\x94"; // みつけたナゾ総数
constexpr const char* kSolvedTotal =
    "\x82\xc6\x82\xaf\x82\xbd\x83\x69\x83\x5d\x91\x8d\x90\x94"; // とけたナゾ総数

std::string text(const Value& v) {
    return v.isText() ? v.text : std::string();
}

int integer(const std::vector<Value>& row, std::size_t i, int fallback = 0) {
    return i < row.size() && row[i].isNumber() ? row[i].toInt() : fallback;
}

std::string textAt(const std::vector<Value>& row, std::size_t i) {
    return i < row.size() ? text(row[i]) : std::string();
}

Rule rule(const std::vector<Value>& row) {
    Rule r;
    r.code = integer(row, 0);
    r.args.assign(row.begin() + (row.empty() ? 0 : 1), row.end());
    return r;
}

std::string nameArg(const Rule& r, std::size_t i) {
    return i < r.args.size() ? text(r.args[i]) : std::string();
}

int intArg(const Rule& r, std::size_t i) {
    return i < r.args.size() ? r.args[i].toInt() : 0;
}

// A flag by name (2001), -1 when the definitions have none (the original
// then neither reads nor writes: 0x2d5214).
int flagOf(const Game& game, const std::string& name) {
    return name.empty() ? -1 : game.flagId(name);
}

const Definitions::Variable* variableOf(const Game& game, const std::string& name) {
    const auto it = game.defs.variables.find(name);
    return it == game.defs.variables.end() ? nullptr : &it->second;
}

// The current event's flag / counter (the flag and variable of its name,
// game +0x12fd0).
std::string eventName(const Game& game) {
    return game.currentEvent ? game.currentEvent->name : std::string();
}

// A puzzle's saved state (0 new, 1 seen, 2 solved; 0x33f54c) by its name.
int puzzleState(const Game& game, const std::string& name) {
    const PuzzleRecord* p = game.defs.puzzleByName(name);
    if (!p) {
        return 0;
    }
    const auto it = game.progress.find(p->id);
    return it == game.progress.end() ? 0 : it->second.state;
}

bool compare(int code, std::int32_t value, std::int32_t operand) {
    switch (code) {
    case 0:
        return value == operand;
    case 1:
        return value != operand;
    case 2:
        return value >= operand;
    case 3:
        return value <= operand;
    case 4:
        return value > operand;
    case 5:
        return value < operand;
    case 6:
        return (value & operand) != 0;
    default:
        return (value & operand) == 0;
    }
}

} // namespace

const EventRecord* StoryTables::event(int number) const {
    const auto it = eventByNumber.find(number);
    return it == eventByNumber.end() ? nullptr : &events[it->second];
}

const EventRecord* StoryTables::event(const std::string& name) const {
    const auto it = eventByName.find(name);
    return it == eventByName.end() ? nullptr : &events[it->second];
}

const PlaceRecord* StoryTables::place(int id) const {
    for (const auto& p : places) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

const PlaceRecord* StoryTables::place(const std::string& name) const {
    const auto it = placeByName.find(name);
    return it == placeByName.end() ? nullptr : &places[it->second];
}

const CharacterRecord* StoryTables::character(const std::string& name) const {
    for (const auto& c : characters) {
        if (c.name == name) {
            return &c;
        }
    }
    return nullptr;
}

const CharacterRecord* StoryTables::character(int id) const {
    for (const auto& c : characters) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

const RoomRecord* StoryTables::room(int id) const {
    for (const auto& r : rooms) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

const RoomRecord* StoryTables::room(const std::string& name) const {
    for (const auto& r : rooms) {
        if (r.name == name) {
            return &r;
        }
    }
    return nullptr;
}

const Step* StoryTables::step(int id) const {
    const int i = stepIndex(id);
    return i < 0 ? nullptr : &steps[std::size_t(i)];
}

const Step* StoryTables::step(const std::string& name) const {
    for (const auto& s : steps) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

int StoryTables::stepIndex(int id) const {
    for (std::size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].id == id) {
            return int(i);
        }
    }
    return -1;
}

StoryTables loadStoryTables(const adventure56::Pack& pack, Edition edition) {
    StoryTables t;
    const fs::path file = pack.find(definitionsPath(edition), ".xs");
    if (file.empty()) {
        throw std::runtime_error(definitionsPath(edition) + " not found");
    }
    const auto list = import::adventure56::parseXscr(pack.read(file));
    // A named condition's index (0x2adfd8 on the 2220 table; "" and
    // unknown names: -1, which always holds).
    auto conditionOf = [&t](const std::string& name) {
        const auto it = t.conditionIndex.find(name);
        return it == t.conditionIndex.end() || name.empty() ? -1 : it->second;
    };
    // The named conditions come after the places that use them: their names
    // first.
    for (const auto& c : list.commands) {
        if (c.opcode == 2221 && c.count >= 1) {
            const auto& o = list.operands[c.first];
            if ((o.type & 15) == 8) {
                const auto* s = reinterpret_cast<const char*>(list.strings.data() + o.value);
                const std::string name(s, strnlen(s, list.strings.size() - o.value));
                t.conditionIndex.emplace(name, int(t.conditions.size()));
                t.conditions.push_back({name, {}});
            }
        }
    }
    int condition = -1; // the 2221 entry the 2223 rules belong to
    Step* step = nullptr;
    EventKind* kind = nullptr;
    Branch* branch = nullptr;
    for (const auto& c : list.commands) {
        std::vector<Value> row;
        for (unsigned i = 0; i < c.count; ++i) {
            const auto& o = list.operands[c.first + i];
            switch (o.type & 15) {
            case 1:
                row.push_back(Value::ofInt(std::int32_t(o.value)));
                break;
            case 2:
                row.push_back(Value::ofUint(o.value));
                break;
            case 3: {
                float f;
                std::memcpy(&f, &o.value, sizeof f);
                row.push_back(Value::ofFloat(f));
                break;
            }
            case 8: {
                const auto* p = reinterpret_cast<const char*>(list.strings.data() + o.value);
                row.push_back(
                    Value::ofText(std::string(p, strnlen(p, list.strings.size() - o.value))));
                break;
            }
            default:
                row.push_back(Value{});
                break;
            }
        }
        switch (c.opcode) {
        // Steps: 2201 opens one, 2203 effects, 2204 conditions, 2202 closes.
        case 2201:
            t.steps.push_back({integer(row, 0),
                               textAt(row, 1),
                               textAt(row, 2),
                               textAt(row, 3),
                               textAt(row, 4),
                               {},
                               {}});
            step = &t.steps.back();
            break;
        case 2202:
            step = nullptr;
            break;
        case 2203:
            if (step) {
                step->effects.push_back(rule(row));
            }
            break;
        case 2204:
            if (step) {
                step->conditions.push_back(rule(row));
            }
            break;
        // Event kinds: 2211 a kind, 2213 a branch with 2214 conditions,
        // 2215 opening and 2216 closing effects; 2212 closes the kind.
        case 2211: {
            EventKind k;
            k.id = integer(row, 0);
            k.name = textAt(row, 1);
            kind = &(t.kinds[k.id] = std::move(k));
            branch = nullptr;
            break;
        }
        case 2212:
            kind = nullptr;
            branch = nullptr;
            break;
        case 2213:
            if (kind) {
                kind->branches.push_back(
                    {textAt(row, 0), integer(row, 1), integer(row, 2), {}, {}, {}});
                branch = &kind->branches.back();
            }
            break;
        case 2214:
            if (branch) {
                branch->conditions.push_back(rule(row));
            }
            break;
        case 2215:
            if (branch) {
                branch->begin.push_back(rule(row));
            }
            break;
        case 2216:
            if (branch) {
                branch->end.push_back(rule(row));
            }
            break;
        // Events.
        case 2321: {
            EventRecord e;
            e.group = integer(row, 0);
            e.number = integer(row, 1);
            e.name = textAt(row, 2);
            e.kindName = textAt(row, 3);
            e.sound = integer(row, 4, -1);
            e.puzzle = textAt(row, 5);
            e.note = textAt(row, 6);
            e.episode = textAt(row, 7);
            e.place = textAt(row, 8);
            e.step = textAt(row, 9);
            e.english = textAt(row, 11);
            t.eventByNumber[e.number] = t.events.size();
            t.eventByName.emplace(e.name, t.events.size());
            t.events.push_back(std::move(e));
            break;
        }
        // Named conditions: 2221 a name, 2223 its rules, 2222 the end.
        case 2221:
            ++condition;
            break;
        case 2223:
            if (condition >= 0 && std::size_t(condition) < t.conditions.size()) {
                t.conditions[std::size_t(condition)].rules.push_back(rule(row));
            }
            break;
        // Characters.
        case 2111: {
            CharacterRecord ch;
            ch.id = integer(row, 0) & 0x3ff;
            ch.name = textAt(row, 1);
            ch.model = integer(row, 2) & 0x3ff;
            ch.other = integer(row, 3) & 0x3ff;
            ch.typing = integer(row, 4) & 3;
            for (std::size_t i = 0; i < 4; ++i) {
                ch.values[i] = 5 + i < row.size() ? row[5 + i].toFloat() : 0;
            }
            t.characters.push_back(std::move(ch));
            break;
        }
        // Rooms.
        case 2121: {
            RoomRecord r;
            r.id = integer(row, 0);
            r.name = textAt(row, 1);
            r.map = integer(row, 2);
            r.variant = textAt(row, 3);
            r.fields = row;
            t.rooms.push_back(std::move(r));
            break;
        }
        // Areas: 2132 an area, 2134 its choices, 2133 the end.
        case 2132:
            t.areas.push_back({integer(row, 0), textAt(row, 1), {}});
            break;
        case 2134:
            if (!t.areas.empty()) {
                t.areas.back().choices.push_back(
                    {conditionOf(textAt(row, 0)), integer(row, 1), integer(row, 2)});
            }
            break;
        // Places: 2142 a place, 2144 sound scenes, 2145 rooms, 2146 exits,
        // 2143 the end (0x18067c, 0x140764, 0x14063c, 0x180490).
        case 2142: {
            PlaceRecord p;
            p.id = integer(row, 0);
            p.name = textAt(row, 1);
            p.german = textAt(row, 2);
            p.area = textAt(row, 3);
            p.iconPlace = textAt(row, 4).empty() ? p.name : textAt(row, 4);
            p.step = textAt(row, 5);
            for (const auto& a : t.areas) {
                if (a.name == p.area) {
                    p.areaId = a.id;
                }
            }
            t.placeByName.emplace(p.name, t.places.size());
            t.places.push_back(std::move(p));
            break;
        }
        case 2144:
            if (!t.places.empty()) {
                t.places.back().sounds.push_back({conditionOf(textAt(row, 0)), integer(row, 1)});
            }
            break;
        case 2145:
            if (!t.places.empty()) {
                int room = -1;
                for (const auto& r : t.rooms) {
                    if (r.name == textAt(row, 1)) {
                        room = r.id;
                    }
                }
                t.places.back().rooms.push_back({conditionOf(textAt(row, 0)), room});
            }
            break;
        case 2146:
            if (!t.places.empty()) {
                PlaceRecord::Exit e;
                e.name = textAt(row, 0);
                e.kind = integer(row, 1);
                e.extra = integer(row, 2);
                for (std::size_t i = 3; i < row.size(); ++i) {
                    const int c = conditionOf(textAt(row, i));
                    if (c >= 0) {
                        e.conditions.push_back(c);
                    }
                }
                t.places.back().exits.push_back(std::move(e));
            }
            break;
        default:
            break;
        }
        t.rows[c.opcode].push_back(std::move(row));
    }
    // Events find their kind by name (0x17c6ac: the kinds' sorted name
    // table at +0x1b0).
    for (auto& e : t.events) {
        for (const auto& [id, k] : t.kinds) {
            if (k.name == e.kindName) {
                e.kind = id;
                break;
            }
        }
    }
    return t;
}

int currentStep(const Game& game) {
    return game.variable(kStepVariable);
}

bool holds(Game& game, const Rule& r) {
    const int code = r.code;
    switch (code) {
    // 0 / 1: a flag (name, value [, index]) is / is not the value.
    case 0:
    case 1: {
        if (r.args.size() < 2) {
            return false;
        }
        const int id = flagOf(game, nameArg(r, 0));
        const bool v = id >= 0 && game.flag(id + intArg(r, 2));
        const bool want = intArg(r, 1) != 0;
        return code == 0 ? v == want : v != want;
    }
    // 2 / 3: a flag (name [, index]) is on / off.
    case 2:
    case 3: {
        if (r.args.empty()) {
            return false;
        }
        const int id = flagOf(game, nameArg(r, 0));
        const bool v = id >= 0 && game.flag(id + intArg(r, 1));
        return code == 2 ? v : !v;
    }
    // 4..11: a variable (name, operand [, index]) == != >= <= > < & !&.
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11: {
        if (r.args.size() < 2) {
            return false;
        }
        const auto* v = variableOf(game, nameArg(r, 0));
        const std::int32_t value = v ? game.variable(*v, intArg(r, 2)) : 0;
        return compare(code - 4, value, intArg(r, 1));
    }
    // 12 / 13: a puzzle (name) is / is not in a state (0 new, 1 seen, 2
    // solved).
    case 12:
    case 13: {
        if (r.args.size() < 2) {
            return false;
        }
        const bool same = puzzleState(game, nameArg(r, 0)) == (intArg(r, 1) & 0xff);
        return code == 12 ? same : !same;
    }
    // 14 / 15: puzzles found / solved in all at least the operand.
    case 14:
    case 15:
        if (r.args.empty()) {
            return false;
        }
        return game.variable(code == 14 ? kFoundTotal : kSolvedTotal) >= intArg(r, 0);
    // 16 / 17: the current event's played flag is / is not the operand;
    // 18 / 19: it is set / not set (0x33f898).
    case 16:
    case 17:
    case 18:
    case 19: {
        if ((code == 16 || code == 17) && r.args.empty()) {
            return false;
        }
        const int id = flagOf(game, eventName(game));
        const bool v = id >= 0 && game.flag(id);
        if (code == 18) {
            return v;
        }
        if (code == 19) {
            return !v;
        }
        const bool want = intArg(r, 0) != 0;
        return code == 16 ? v == want : v != want;
    }
    // 20..27: the current event's counter (0x2d50bc) == != >= <= > < & !&.
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27: {
        if (r.args.empty()) {
            return false;
        }
        const auto* v = variableOf(game, eventName(game));
        return compare(code - 20, v ? game.variable(*v) : 0, intArg(r, 0));
    }
    // 28..33: the current step against a named step: == != >= <= > <
    // (current first; 0x2d5008, 0x2d5034).
    case 28:
    case 29:
    case 30:
    case 31:
    case 32:
    case 33: {
        if (r.args.empty()) {
            return false;
        }
        const Step* s = game.story.step(nameArg(r, 0));
        if (!s) {
            return false;
        }
        return compare(code - 28, currentStep(game), s->id);
    }
    // 34..37: the current step against two named steps (-1 for an
    // unknown one): 34 inside [a, b]... as 0x121c84 compares.
    case 34:
    case 35:
    case 36:
    case 37: {
        if (r.args.size() < 2) {
            return false;
        }
        const int cur = currentStep(game);
        const Step* sa = game.story.step(nameArg(r, 0));
        const Step* sb = game.story.step(nameArg(r, 1));
        const int a = sa ? sa->id : -1, b = sb ? sb->id : -1;
        switch (code) {
        case 34: // a <= cur <= b
            return (a <= cur) && (cur <= b);
        case 35: // a < cur <= b
            return a < cur && cur <= b;
        case 36: // a <= cur < b
            return a <= cur && cur < b;
        default: // a < cur < b
            return a < cur && cur < b;
        }
    }
    // 38 / 39: the current puzzle's state is / is not the operand
    // (0x2d4f24).
    case 38:
    case 39: {
        if (r.args.empty()) {
            return false;
        }
        const bool same = puzzleState(game, game.currentPuzzleName) == (intArg(r, 0) & 0xff);
        return code == 38 ? same : !same;
    }
    default:
        return false;
    }
}

bool holdsAll(Game& game, const std::vector<Rule>& rules) {
    for (const auto& r : rules) {
        if (!holds(game, r)) {
            return false;
        }
    }
    return true;
}

void apply(Game& game, const Rule& r) {
    auto flagSet = [&](const std::string& name, int index, bool on) {
        const int id = flagOf(game, name);
        if (id >= 0) {
            game.setFlag(id + index, on);
        }
    };
    auto variableChange = [&](const std::string& name, int index, auto change) {
        if (const auto* v = variableOf(game, name)) {
            game.setVariable(*v, change(game.variable(*v, index)), index);
        }
    };
    switch (r.code) {
    // 1511: a flag (name, value [, index]); 1512 / 1513: on / off (name
    // [, index]).
    case 1511:
        if (r.args.size() >= 2) {
            flagSet(nameArg(r, 0), intArg(r, 2), intArg(r, 1) != 0);
        }
        break;
    case 1512:
    case 1513:
        if (!r.args.empty()) {
            flagSet(nameArg(r, 0), intArg(r, 1), r.code == 1512);
        }
        break;
    // 1521: a variable (name, value [, index]); 1522 / 1523 ++ / -- (name
    // [, index]); 1524..1527 += -= &= |= (name, value [, index]).
    case 1521:
        if (r.args.size() >= 2) {
            const std::int32_t value = intArg(r, 1);
            variableChange(nameArg(r, 0), intArg(r, 2), [value](std::int32_t) { return value; });
        }
        break;
    case 1522:
    case 1523: {
        const int d = r.code == 1522 ? 1 : -1;
        variableChange(nameArg(r, 0), intArg(r, 1), [d](std::int32_t v) { return v + d; });
        break;
    }
    case 1524:
    case 1525:
    case 1526:
    case 1527: {
        if (r.args.empty()) {
            break;
        }
        const std::int32_t x = intArg(r, 1);
        const int code = r.code;
        variableChange(nameArg(r, 0), intArg(r, 2), [x, code](std::int32_t v) {
            return code == 1524 ? v + x : code == 1525 ? v - x : code == 1526 ? v & x : v | x;
        });
        break;
    }
    // 1551: a puzzle (name) gets a state (0x2d2470 -> 0x2d1fcc).
    case 1551:
        if (r.args.size() >= 2) {
            if (const PuzzleRecord* p = game.defs.puzzleByName(nameArg(r, 0))) {
                game.setPuzzleState(p->id, intArg(r, 1) & 0xff);
            }
        }
        break;
    // 1591: an item of the collections is received (0x2abd4c ...).
    case 1591:
        if (!r.args.empty()) {
            game.giveItem(nameArg(r, 0));
        }
        break;
    // 1702: the place (by name) becomes the current one.
    case 1702: {
        const PlaceRecord* p = game.story.place(nameArg(r, 0));
        game.setVariable(kPlaceVariable, p ? p->id : -1);
        break;
    }
    // 1703: the story enters a step (by name).
    case 1703:
        if (const Step* s = game.story.step(nameArg(r, 0))) {
            enterStep(game, *s);
        }
        break;
    // 1804: the current event's note is received (event +14).
    case 1804:
        if (game.currentEvent && !game.currentEvent->note.empty()) {
            game.giveItem(game.currentEvent->note);
        }
        break;
    // 1811: the current event's played flag to the operand; 1812 / 1813
    // on / off.
    case 1811:
        if (!r.args.empty()) {
            flagSet(eventName(game), 0, intArg(r, 0) != 0);
        }
        break;
    case 1812:
    case 1813:
        flagSet(eventName(game), 0, r.code == 1812);
        break;
    // 1821: the event's counter to the operand; 1822 / 1823 ++ / --;
    // 1824..1827 += -= &= |=.
    case 1821:
    case 1822:
    case 1823:
    case 1824:
    case 1825:
    case 1826:
    case 1827: {
        const int code = r.code;
        if (code != 1822 && code != 1823 && r.args.empty()) {
            break;
        }
        const std::int32_t x = intArg(r, 0);
        variableChange(eventName(game), 0, [x, code](std::int32_t v) {
            switch (code) {
            case 1821:
                return x;
            case 1822:
                return v + 1;
            case 1823:
                return v - 1;
            case 1824:
                return v + x;
            case 1825:
                return v - x;
            case 1826:
                return v & x;
            default:
                return v | x;
            }
        });
        break;
    }
    // 1871: the current puzzle's state (0x2d24b8).
    case 1871:
        if (!r.args.empty()) {
            if (const PuzzleRecord* p = game.defs.puzzleByName(game.currentPuzzleName)) {
                game.setPuzzleState(p->id, intArg(r, 0) & 0xff);
            }
        }
        break;
    default:
        if (game.log) {
            game.log("effect " + std::to_string(r.code) + " not reconstructed");
        }
        break;
    }
}

void applyAll(Game& game, const std::vector<Rule>& rules) {
    for (const auto& r : rules) {
        apply(game, r);
    }
    advance(game);
}

namespace {

// 0x2abbc4: the step variable takes the step's id, its effects apply (one
// by one: the list's closing advance is the caller's), and the puzzles it
// leaves behind are passed by.
void beginStep(Game& game, const Step& step) {
    game.setVariable(kStepVariable, step.id);
    for (const auto& r : step.effects) {
        apply(game, r);
    }
    // Unsolved puzzles whose record names this step (2301 field 9) get bit
    // 8 of their saved halfword and join the passed-by list (game +0x8d82,
    // at most the puzzles + 15 entries).
    for (const auto& p : game.defs.puzzles) {
        if (p.lostStep.empty() || p.lostStep != step.name) {
            continue;
        }
        auto& pr = game.progress[p.id];
        if ((pr.state & 3) == 2) {
            continue; // solved (0x2abbc4 tests the low two bits)
        }
        pr.passedBy = true;
        if (game.passedBy.size() < game.defs.puzzles.size() + 15) {
            game.passedBy.push_back(p.number - 1);
        }
    }
    if (game.log) {
        game.log("step " + std::to_string(step.id));
    }
}

} // namespace

void enterStep(Game& game, const Step& step) {
    beginStep(game, step);
    advance(game);
}

bool advance(Game& game) {
    bool changed = false;
    for (int guard = 0; guard < 1000; ++guard) {
        const int i = game.story.stepIndex(currentStep(game));
        if (i < 0) {
            break;
        }
        const Step& s = game.story.steps[std::size_t(i)];
        if (!holdsAll(game, s.conditions) || std::size_t(i + 1) >= game.story.steps.size()) {
            break;
        }
        beginStep(game, game.story.steps[std::size_t(i + 1)]);
        changed = true;
    }
    return changed;
}

bool holdsNamed(Game& game, int index) {
    if (index < 0 || std::size_t(index) >= game.story.conditions.size()) {
        return false;
    }
    return holdsAll(game, game.story.conditions[std::size_t(index)].rules);
}

namespace {

int placeIndex(const Game& game, const PlaceRecord* p) {
    return p ? int(p - game.story.places.data()) : -1;
}

void relaxGuide(Game& game, int u) {
    const PlaceRecord& place = game.story.places[std::size_t(u)];
    const int next = game.guideDistance[std::size_t(u)] + 1;
    for (const auto& exit : place.exits) {
        const int v = placeIndex(game, game.story.place(exit.name));
        if (v < 0 || v == u) {
            continue;
        }
        bool blocked = false;
        for (const int c : exit.conditions) {
            blocked = blocked || (std::size_t(c) < game.conditionBits.size() &&
                                  game.conditionBits[std::size_t(c)]);
        }
        if (blocked || game.guideDistance[std::size_t(v)] <= next) {
            continue;
        }
        game.guideDistance[std::size_t(v)] = next;
        game.guideFrom[std::size_t(v)] = place.id;
        relaxGuide(game, v);
    }
}

} // namespace

void refreshConditions(Game& game) {
    game.conditionBits.assign(game.story.conditions.size(), false);
    for (std::size_t i = 0; i < game.story.conditions.size(); ++i) {
        game.conditionBits[i] = holdsNamed(game, int(i));
    }
    const std::size_t n = game.story.places.size();
    game.guideFrom.assign(n, -1);
    game.guideDistance.assign(n, 0xffff);
    const Step* step = game.story.step(currentStep(game));
    const int goal = step ? placeIndex(game, game.story.place(step->goalPlace)) : -1;
    if (goal >= 0) {
        game.guideDistance[std::size_t(goal)] = 0;
        relaxGuide(game, goal);
    }
}

bool onGuideWay(const Game& game, const PlaceRecord& from, const PlaceRecord& on) {
    int i = placeIndex(game, &from);
    for (std::size_t steps = 0;
         i >= 0 && std::size_t(i) < game.guideFrom.size() && steps <= game.guideFrom.size();
         ++steps) {
        const PlaceRecord& p = game.story.places[std::size_t(i)];
        if (&p == &on) {
            return true;
        }
        i = placeIndex(game, game.story.place(game.guideFrom[std::size_t(i)]));
    }
    return false;
}

namespace {

// 0x2ac6ec: a named condition's bit (false past the set).
bool conditionBit(const Game& game, int index) {
    return index >= 0 && std::size_t(index) < game.conditionBits.size() &&
           game.conditionBits[std::size_t(index)];
}

int firstChoice(const Game& game, const std::vector<PlaceRecord::Choice>& list) {
    for (const auto& c : list) {
        if (c.condition < 0 || conditionBit(game, c.condition)) {
            return c.value;
        }
    }
    return -1;
}

} // namespace

int placeSound(const Game& game, const PlaceRecord& place) {
    return firstChoice(game, place.sounds);
}

int placeRoom(const Game& game, const PlaceRecord& place) {
    return firstChoice(game, place.rooms);
}

const Branch* pickBranch(Game& game, const EventRecord& event) {
    const auto it = game.story.kinds.find(event.kind);
    if (it == game.story.kinds.end()) {
        return nullptr;
    }
    for (const auto& b : it->second.branches) {
        if (holdsAll(game, b.conditions)) {
            return &b;
        }
    }
    return nullptr;
}

} // namespace picarat::adventure5
