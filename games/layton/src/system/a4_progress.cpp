// The fourth adventure's story bookkeeping. See a4_progress.hpp.
#include "system/a4_progress.hpp"
#include "system/a4_definitions.hpp"
#include <memory>

namespace picarat::adventure4 {

namespace {
constexpr std::uint32_t kOverlay7 = 0x020b6820;

// Names the game's code uses for its own variables and flags (ov7).
struct CodeName {
    std::uint32_t pool; // a literal pool word holding the string's address
};

constexpr CodeName kStepVariable{0x020bc0b0};   // 現在のフェーズ
constexpr CodeName kFoundMain{0x020c2f88};      // puzzles found (main)
constexpr CodeName kFoundChallenge{0x020c2f8c}; // ... challenges
constexpr CodeName kFoundWifi{0x020c2f90};      // ... Wi-Fi
constexpr CodeName kFoundAll{0x020c2f94};
constexpr CodeName kAddedMark{0x020c2f98}; // collection: the puzzle is in the index
constexpr CodeName kSolvedMain{0x020c2f9c};
constexpr CodeName kAllMainMark{0x020c2fa0};
constexpr CodeName kEpisode20Mark{0x020c2fa4};
constexpr CodeName kSolvedChallenge{0x020c2fa8};
constexpr CodeName kGameCompleteMark{0x020c2fac};
constexpr CodeName kSolvedWifi{0x020c2fb0};
constexpr CodeName kAllButLastMark{0x020c2fb4};
constexpr CodeName kSolvedAll{0x020c2fb8};
constexpr CodeName kPicarats{0x020c2fbc};
// Flags 020bb00c raises (ov7 020bb1ec..020bb200).
constexpr CodeName kNewFishing{0x020bb1ec};
constexpr CodeName kNewTrain{0x020bb1f0};
constexpr CodeName kNewPuppets{0x020bb1f4};
constexpr CodeName kPuzzlesComplete{0x020bb1fc};
constexpr CodeName kGameComplete{0x020bb200};

std::string codeName(Assets& assets, CodeName n) {
    return assets.codeText(7, kOverlay7, assets.codeWord(7, kOverlay7, n.pool));
}

using Value = gss::Value;

int number(const Rule& r, std::size_t i, int otherwise = 0) {
    return i < r.args.size() && !r.args[i].isText() ? r.args[i].toInt() : otherwise;
}

// A flag argument: a name, or a number of lt4_def 2001 {id, name}.
std::string flagName(Assets& assets, const Value& v) {
    if (v.isText()) {
        return v.text;
    }
    const auto* row = definitions(assets).find(2001, 0, v);
    return row ? Definitions::text(*row, 1) : std::string();
}

// A flag by an argument: a name, or a number (the bit itself; numbers past
// the named ones are bits of a name's run, 020b9708).
bool flagOn(Assets& assets, const GameData& data, const Value& v, int index) {
    return v.isText() ? data.flag(flagName(assets, v), index) : data.flagAt(v.toInt() + index);
}

void setFlagArg(Assets& assets, GameData& data, const Value& v, bool on, int index) {
    if (v.isText()) {
        data.setFlag(flagName(assets, v), on, index);
    } else {
        data.setFlagAt(v.toInt() + index, on);
    }
}

std::string variableName(Assets& assets, const Value& v) {
    if (v.isText()) {
        return v.text;
    }
    const auto* row = definitions(assets).find(2011, 0, v);
    return row ? Definitions::text(*row, 2) : std::string();
}

// A variable's value as its storage keeps it (lt4_def 2011 {id, kind,
// name}: kind ±1 / ±2 / ±4 bytes, negative: signed).
int stored(Assets& assets, const std::string& name, int value) {
    const auto* row = definitions(assets).find(2011, 2, Value::ofText(name));
    if (!row) {
        return value;
    }
    switch (Definitions::number(*row, 1)) {
    case 1:
        return value & 0xff;
    case -1:
        return std::int8_t(value);
    case 2:
        return value & 0xffff;
    case -2:
        return std::int16_t(value);
    default:
        return value;
    }
}

void setVariable(Assets& assets, GameData& data, const std::string& name, int value, int index) {
    data.setVariable(name, stored(assets, name, value), index);
}

int puzzleNumberOf(Assets& assets, const Value& v) {
    return puzzleNumber(assets, v);
}

int puzzleState(const GameData& data, int number) {
    const auto it = data.puzzles.find(number);
    return it == data.puzzles.end() ? 0 : it->second.state;
}

bool compare(int code, int base, int a, int b) {
    switch (code - base) {
    case 0:
        return a == b;
    case 1:
        return a != b;
    case 2:
        return a >= b;
    case 3:
        return a <= b;
    case 4:
        return a > b;
    case 5:
        return a < b;
    case 6:
        return (a & b) != 0;
    case 7:
        return (a & b) == 0;
    default:
        return false;
    }
}

const Step* stepByName(Assets& assets, const std::string& name) {
    for (const Step& s : steps(assets)) {
        if (s.name == name) {
            return &s;
        }
    }
    return nullptr;
}

const Event* currentEvent(Assets& assets, const GameData& data) {
    return data.event >= 0 ? event(assets, data.event) : nullptr;
}

// The puzzle the current event plays (ov7 020c3824 makes it the current
// puzzle; +0x59e4).
int eventPuzzle(Assets& assets, const GameData& data) {
    const Event* e = currentEvent(assets, data);
    return e ? e->puzzle : -1;
}

Rule rule(const Definitions::Row& row) {
    Rule r;
    r.code = row.empty() ? 0 : row[0].toInt();
    r.args.assign(row.begin() + (row.empty() ? 0 : 1), row.end());
    return r;
}
} // namespace

// ------------------------------------------------------------ conditions

bool holds(Assets& assets, const GameData& data, const Rule& r) {
    const int code = r.code;
    const auto arg = [&](std::size_t i) -> const Value& {
        static const Value none = Value::ofInt(0);
        return i < r.args.size() ? r.args[i] : none;
    };
    if (code == 1510 || code == 1511) {
        if (r.args.size() < 2) {
            return false;
        }
        const bool want = number(r, 1) != 0;
        const bool on = flagOn(assets, data, arg(0), number(r, 2));
        return code == 1510 ? on == want : on != want;
    }
    if (code == 1512 || code == 1513) {
        if (r.args.empty()) {
            return false;
        }
        return flagOn(assets, data, arg(0), number(r, 1)) == (code == 1512);
    }
    if (code >= 1520 && code <= 1527) {
        if (r.args.size() < 2) {
            return false;
        }
        const int v = data.variable(variableName(assets, arg(0)), number(r, 2));
        return compare(code, 1520, v, number(r, 1));
    }
    if (code == 1590 || code == 1591) {
        if (r.args.size() < 2) {
            return false;
        }
        const int s = puzzleState(data, puzzleNumberOf(assets, arg(0)));
        return code == 1590 ? s == number(r, 1) : s != number(r, 1);
    }
    if (code == 1592 || code == 1593) {
        if (r.args.empty()) {
            return false;
        }
        const int n = data.variable(codeName(assets, code == 1592 ? kFoundMain : kSolvedMain));
        return number(r, 0) <= n;
    }
    const Event* e = currentEvent(assets, data);
    if (code >= 1810 && code <= 1813) {
        const bool done = e && data.flag(e->name);
        if (code <= 1811) {
            if (r.args.empty()) {
                return false;
            }
            const bool want = number(r, 0) != 0;
            return code == 1810 ? done == want : done != want;
        }
        return done == (code == 1812);
    }
    if (code >= 1820 && code <= 1827) {
        if (r.args.empty()) {
            return false;
        }
        const int counter = e ? data.variable(e->name) : 0;
        return compare(code, 1820, counter, number(r, 0));
    }
    if (code >= 1850 && code <= 1855) {
        if (r.args.empty()) {
            return false;
        }
        const Step* s = arg(0).isText() ? stepByName(assets, arg(0).text) : nullptr;
        if (!s) {
            return false;
        }
        return compare(code, 1850, currentStep(assets, data), s->id);
    }
    if (code >= 1856 && code <= 1859) {
        if (r.args.size() < 2) {
            return false;
        }
        const Step* a = arg(0).isText() ? stepByName(assets, arg(0).text) : nullptr;
        const Step* b = arg(1).isText() ? stepByName(assets, arg(1).text) : nullptr;
        const int lo = a ? a->id : -1, hi = b ? b->id : -1, now = currentStep(assets, data);
        switch (code) {
        case 1856:
            return lo <= now && now <= hi;
        case 1857:
            return lo < now && now <= hi;
        case 1858:
            return lo <= now && now < hi;
        default:
            return lo < now && now < hi;
        }
    }
    if (code == 1890 || code == 1891) {
        if (r.args.empty()) {
            return false;
        }
        const int s = puzzleState(data, eventPuzzle(assets, data));
        return code == 1890 ? s == number(r, 0) : s != number(r, 0);
    }
    return false;
}

bool holdsAll(Assets& assets, const GameData& data, const std::vector<Rule>& rules) {
    // ov7 020b7c98: every one, stopping at the first that fails.
    for (const Rule& r : rules) {
        if (!holds(assets, data, r)) {
            return false;
        }
    }
    return true;
}

// --------------------------------------------------------------- effects

void apply(Assets& assets, GameData& data, const Rule& r) {
    const int code = r.code;
    const auto arg = [&](std::size_t i) -> const Value& {
        static const Value none = Value::ofInt(0);
        return i < r.args.size() ? r.args[i] : none;
    };
    if (code == 1511) {
        if (r.args.size() >= 2) {
            setFlagArg(assets, data, arg(0), number(r, 1) != 0, number(r, 2));
        }
        return;
    }
    if (code == 1512 || code == 1513) {
        if (!r.args.empty()) {
            setFlagArg(assets, data, arg(0), code == 1512, number(r, 1));
        }
        return;
    }
    if (code == 1521) {
        if (r.args.size() >= 2) {
            setVariable(assets, data, variableName(assets, arg(0)), number(r, 1), number(r, 2));
        }
        return;
    }
    if (code == 1522 || code == 1523) {
        if (!r.args.empty()) {
            const std::string name = variableName(assets, arg(0));
            const int index = number(r, 1);
            setVariable(assets, data, name, data.variable(name, index) + (code == 1522 ? 1 : -1),
                        index);
        }
        return;
    }
    if (code >= 1524 && code <= 1527) {
        if (r.args.empty()) {
            return;
        }
        const std::string name = variableName(assets, arg(0));
        const int index = number(r, 2), v = data.variable(name, index), x = number(r, 1);
        const int result = code == 1524   ? v + x
                           : code == 1525 ? v - x
                           : code == 1526 ? (v & x)
                                          : (v | x);
        setVariable(assets, data, name, result, index);
        return;
    }
    if (code == 1551) {
        if (r.args.size() >= 2) {
            setPuzzleState(assets, data, puzzleNumberOf(assets, arg(0)), number(r, 1));
        }
        return;
    }
    if (code == 1591) {
        // 020bb204: an item received.
        if (!r.args.empty()) {
            giveItem(assets, data, arg(0), 0);
        }
        return;
    }
    if (code == 1702) {
        if (!r.args.empty()) {
            setPlace(assets, data, arg(0));
        }
        return;
    }
    if (code == 1703) {
        if (!r.args.empty() && arg(0).isText()) {
            if (const Step* s = stepByName(assets, arg(0).text)) {
                enterStep(assets, data, *s);
            }
        }
        return;
    }
    const Event* e = currentEvent(assets, data);
    if (code == 1804) {
        // 020c391c: the event's investigation note.
        if (e && e->memo >= 0) {
            giveItem(assets, data, Value::ofInt(e->memo), 0);
        }
        return;
    }
    if (code == 1811) {
        if (e && !r.args.empty()) {
            data.setFlag(e->name, number(r, 0) != 0);
        }
        return;
    }
    if (code == 1812 || code == 1813) {
        if (e) {
            data.setFlag(e->name, code == 1812);
        }
        return;
    }
    if (code >= 1821 && code <= 1827) {
        if (!e) {
            return;
        }
        const int v = data.variable(e->name);
        int result = v;
        switch (code) {
        case 1821:
            if (r.args.empty()) {
                return;
            }
            result = number(r, 0);
            break;
        case 1822:
            result = v + 1;
            break;
        case 1823:
            result = v - 1;
            break;
        default:
            if (r.args.empty()) {
                return;
            }
            result = code == 1824   ? v + number(r, 0)
                     : code == 1825 ? v - number(r, 0)
                     : code == 1826 ? (v & number(r, 0))
                                    : (v | number(r, 0));
            break;
        }
        setVariable(assets, data, e->name, result, 0);
        return;
    }
    if (code == 1911) {
        if (!r.args.empty()) {
            setPuzzleState(assets, data, eventPuzzle(assets, data), number(r, 0));
        }
    }
}

void applyAll(Assets& assets, GameData& data, const std::vector<Rule>& rules) {
    for (const Rule& r : rules) {
        apply(assets, data, r);
    }
    advance(assets, data);
}

// ----------------------------------------------------------------- steps

const std::vector<Step>& steps(Assets& assets) {
    static std::map<std::string, std::unique_ptr<std::vector<Step>>> cache;
    auto& out = cache[assets.root.string()];
    if (out) {
        return *out;
    }
    out = std::make_unique<std::vector<Step>>();
    // The rows in file order: 2201 opens a step, 2203 / 2204 add to it,
    // 2202 ends it.
    const auto list = assets.data("res/@/lt4_def.cb");
    Step* current = nullptr;
    for (const auto& c : list.commands) {
        if (c.opcode < 2201 || c.opcode > 2204) {
            continue;
        }
        Definitions::Row row;
        for (unsigned i = 0; i < c.count; ++i) {
            const auto& o = list.operands[c.first + i];
            const auto kind = gss::Value::Kind(o.type & 15);
            row.push_back(kind == gss::Value::Kind::Text || kind == gss::Value::Kind::Name
                              ? gss::Value::ofText(list.text(o))
                              : gss::Value::ofInt(std::int32_t(o.value)));
        }
        switch (c.opcode) {
        case 2201:
            out->push_back({Definitions::number(row, 0),
                            Definitions::text(row, 1),
                            Definitions::text(row, 2),
                            Definitions::text(row, 3),
                            {},
                            {}});
            current = &out->back();
            break;
        case 2203:
            if (current) {
                current->effects.push_back(rule(row));
            }
            break;
        case 2204:
            if (current) {
                current->conditions.push_back(rule(row));
            }
            break;
        default:
            current = nullptr;
            break;
        }
    }
    return *out;
}

int currentStep(Assets& assets, const GameData& data) {
    // Variables start at 0: step 0 ("初期状態").
    return data.variable(codeName(assets, kStepVariable));
}

void enterStep(Assets& assets, GameData& data, const Step& step) {
    // 020bc0b8: the step's id, its effects, then the puzzles it leaves
    // behind: of the main puzzles (the first 155 rows of the index order,
    // lt4_def 2311), each not solved whose third field names this step is
    // passed by - its save record's bit 8 is set (020c30a8; the lost-puzzle
    // menu lists it) and it joins the list the next notice shows
    // (020c338c, at most 170).
    setVariable(assets, data, codeName(assets, kStepVariable), step.id, 0);
    for (const Rule& r : step.effects) {
        apply(assets, data, r);
    }
    const auto& rows = definitions(assets).rows(2311);
    for (std::size_t i = 0; i < rows.size() && i < 155; ++i) {
        if (Definitions::text(rows[i], 2) != step.name) {
            continue;
        }
        const int number = puzzleNumber(assets, gss::Value::ofText(Definitions::text(rows[i], 0)));
        if (number <= 0) {
            continue;
        }
        GameData::Progress& p = data.puzzles[number];
        if (p.state == 2) {
            continue;
        }
        p.flag100 = true;
        if (data.passedBy.size() < 170) {
            data.passedBy.push_back(int(i));
        }
    }
}

bool advance(Assets& assets, GameData& data) {
    const auto& all = steps(assets);
    bool changed = false;
    for (int guard = 0; guard < 1000; ++guard) {
        const int now = currentStep(assets, data);
        std::size_t i = 0;
        while (i < all.size() && all[i].id != now) {
            ++i;
        }
        if (i == all.size() || !holdsAll(assets, data, all[i].conditions) || i + 1 >= all.size()) {
            break;
        }
        enterStep(assets, data, all[i + 1]);
        changed = true;
    }
    return changed;
}

// ---------------------------------------------------------------- events

const std::vector<Branch>* eventKind(Assets& assets, int kind) {
    static std::map<std::string, std::unique_ptr<std::map<int, std::vector<Branch>>>> cache;
    auto& kinds = cache[assets.root.string()];
    if (!kinds) {
        kinds = std::make_unique<std::map<int, std::vector<Branch>>>();
        const auto list = assets.data("res/@/lt4_def.cb");
        std::vector<Branch>* branches = nullptr;
        Branch* b = nullptr;
        for (const auto& c : list.commands) {
            if (c.opcode < 2211 || c.opcode > 2216) {
                continue;
            }
            Definitions::Row row;
            for (unsigned i = 0; i < c.count; ++i) {
                const auto& o = list.operands[c.first + i];
                const auto k = gss::Value::Kind(o.type & 15);
                row.push_back(k == gss::Value::Kind::Text || k == gss::Value::Kind::Name
                                  ? gss::Value::ofText(list.text(o))
                                  : gss::Value::ofInt(std::int32_t(o.value)));
            }
            switch (c.opcode) {
            case 2211:
                branches = &(*kinds)[Definitions::number(row, 0)];
                b = nullptr;
                break;
            case 2213:
                if (branches) {
                    branches->push_back({Definitions::text(row, 0),
                                         Definitions::number(row, 1),
                                         Definitions::number(row, 2),
                                         {},
                                         {},
                                         {}});
                    b = &branches->back();
                }
                break;
            case 2214:
                if (b) {
                    b->conditions.push_back(rule(row));
                }
                break;
            case 2215:
                if (b) {
                    b->begin.push_back(rule(row));
                }
                break;
            case 2216:
                if (b) {
                    b->end.push_back(rule(row));
                }
                break;
            default:
                branches = nullptr;
                b = nullptr;
                break;
            }
        }
    }
    const auto it = kinds->find(kind);
    return it == kinds->end() ? nullptr : &it->second;
}

const Event* event(Assets& assets, int number) {
    static std::map<std::string, std::unique_ptr<std::map<int, Event>>> cache;
    auto& events = cache[assets.root.string()];
    if (!events) {
        events = std::make_unique<std::map<int, Event>>();
        const Definitions& defs = definitions(assets);
        auto idOf = [&](int opcode, std::size_t field, const std::string& name) {
            if (name.empty()) {
                return -1;
            }
            const auto* row = defs.find(opcode, field, Value::ofText(name));
            return row ? Definitions::number(*row, 0) : -1;
        };
        for (const auto& row : defs.rows(2321)) {
            Event e;
            e.group = Definitions::number(row, 0);
            e.number = Definitions::number(row, 1) & 0xffffff;
            e.name = Definitions::text(row, 2);
            const auto* kind = defs.find(2211, 1, Value::ofText(Definitions::text(row, 3)));
            e.kind = kind ? Definitions::number(*kind, 0) : -1;
            e.puzzle = idOf(2301, 1, Definitions::text(row, 5));
            e.memo = idOf(2021, 1, Definitions::text(row, 6));
            e.sound = Definitions::number(row, 4);
            events->emplace(e.number, e);
        }
    }
    const auto it = events->find(number);
    return it == events->end() ? nullptr : &it->second;
}

// ----------------------------------------------------------------- items

void giveItem(Assets& assets, GameData& data, const Value& key, int index) {
    const Definitions& defs = definitions(assets);
    const auto* row = defs.find(2021, key.isText() ? 1 : 0, key);
    if (!row) {
        return;
    }
    const int id = Definitions::number(*row, 0);
    if (id >= 416) {
        // A set: its four items.
        for (std::size_t f = 5; f <= 8; ++f) {
            const std::string item = Definitions::text(*row, f);
            if (!item.empty()) {
                giveItem(assets, data, Value::ofText(item), 0);
            }
        }
        return;
    }
    while (data.notices.size() > 15) {
        data.notices.pop_front();
    }
    const std::string name = Definitions::text(*row, 1);
    if (id >= 32 && id < 384) {
        data.setMark(name, 1, 0);
        if (id >= 58 && id <= 86) {
            data.setFlag(codeName(assets, kNewFishing), true);
        } else if (id >= 87 && id <= 99) {
            data.setFlag(codeName(assets, kNewTrain), true);
        } else if (id >= 100 && id <= 127) {
            data.setFlag(codeName(assets, kNewPuppets), true);
        }
    } else if (id < 32) {
        // Counted (020d5f20 / 020d5f7c): a byte of the game's values at
        // the item's number, which is the variable placed there (2011
        // {offset, size, name}: 0 the mysteries, 31 the market medals).
        if (const auto* v = defs.find(2011, 0, Value::ofInt(id))) {
            const std::string var = Definitions::text(*v, 2);
            data.setVariable(var, data.variable(var) + 1);
        } else {
            data.setMark(name, data.marks[name + "#0"] + 1, 0);
        }
    } else if (id == 400) {
        data.setFlag(codeName(assets, kPuzzlesComplete), true);
    } else if (id == 401) {
        data.setFlag(codeName(assets, kGameComplete), true);
    }
    data.notices.push_back({id, index, Definitions::number(*row, 3), Definitions::number(*row, 2)});
}

// --------------------------------------------------------------- puzzles

bool setPuzzleState(Assets& assets, GameData& data, int number, int state) {
    // 020c2c5c: the puzzle's index (record +6) sorts it: below 155 the
    // main puzzles, then 15 challenges, then 52 Wi-Fi puzzles.
    if (number <= 0) {
        return false;
    }
    const GameData::Puzzle p = findPuzzle(assets, number);
    auto& progress = data.puzzles[number];
    const int before = progress.state;
    progress.state = state;
    if (before == state) {
        return false;
    }
    if (p.index < 0) {
        return true;
    }
    const int i = p.index;
    auto bump = [&](CodeName n) {
        const std::string name = codeName(assets, n);
        setVariable(assets, data, name, data.variable(name) + 1, 0);
    };
    auto count = [&](CodeName n) {
        return data.variable(codeName(assets, n));
    };
    if (before == 0) {
        if (i < 155) {
            bump(kFoundMain);
        } else if (i - 155 < 15) {
            bump(kFoundChallenge);
        } else if (i - 170 < 52) {
            bump(kFoundWifi);
        }
        bump(kFoundAll);
    }
    if (state != 2) {
        return true;
    }
    // The puzzle's place record (2311, in index order): its fourth field
    // names a collection entry that opens with it.
    const auto& places = definitions(assets).rows(2311);
    const std::string opens = std::size_t(i) < places.size()
                                  ? Definitions::text(places[std::size_t(i)], 3)
                                  : std::string();
    bool counted = false;
    if (i < 155) {
        giveItem(assets, data, Value::ofText(codeName(assets, kAddedMark)), i);
        if (!opens.empty()) {
            giveItem(assets, data, Value::ofText(opens), 0);
        }
        bump(kSolvedMain);
        if (count(kSolvedMain) > 154) {
            giveItem(assets, data, Value::ofText(codeName(assets, kAllMainMark)), 0);
            giveItem(assets, data, Value::ofText(codeName(assets, kEpisode20Mark)), 0);
        }
        counted = true;
    } else if (i - 155 < 15) {
        giveItem(assets, data, Value::ofText(codeName(assets, kAddedMark)), i);
        if (!opens.empty()) {
            giveItem(assets, data, Value::ofText(opens), 0);
        }
        bump(kSolvedChallenge);
        if (count(kSolvedChallenge) > 14) {
            giveItem(assets, data, Value::ofText(codeName(assets, kGameCompleteMark)), 0);
        }
        counted = true;
    } else if (i - 170 < 52) {
        bump(kSolvedWifi);
    }
    if (counted && count(kSolvedMain) + count(kSolvedChallenge) == 167) {
        giveItem(assets, data, Value::ofText(codeName(assets, kAllButLastMark)), 0);
    }
    bump(kSolvedAll);
    // The picarats for the wrong answers given (at most the third value).
    const int earned = p.picarats[std::size_t(std::min(progress.failures, 2))];
    const std::string total = codeName(assets, kPicarats);
    setVariable(assets, data, total, data.variable(total) + earned, 0);
    return true;
}

} // namespace picarat::adventure4
