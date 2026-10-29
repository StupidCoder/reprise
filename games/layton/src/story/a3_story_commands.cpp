// The story's script commands and conditions: loop requests (12xx), the
// game's data (15xx..19xx), saves (169x), and a few the game registers but
// does nothing with. Each comment names the overlay-9 handler it reproduces.
#include "sound/a3_sound.hpp"
#include "story/a3_story.hpp"
#include "story/a3_talk.hpp"
#include <algorithm>
#include <cstdio>

namespace picarat::adventure3 {
namespace {
using lscr::Call;
using lscr::Result;
using lscr::Value;

void put(Call& c, std::size_t i, Value v) {
    if (Value* slot = c.variable(i)) {
        slot->get() = std::move(v);
    }
}

// Up to four numbers after operand `first` as a loop's arguments (02106ec4:
// the rest stay 0).
LoopArgs loopArgs(const Call& c, std::size_t first) {
    LoopArgs a{};
    for (std::size_t i = 0; i < 4 && first + i < c.count(); ++i) {
        a[i] = c.integer(first + i);
    }
    return a;
}

const Value& at(std::span<Value> a, std::size_t i) {
    static const Value none;
    return i < a.size() ? a[i] : none;
}
} // namespace

// The commands and conditions on the game's data alone (no scene): also
// used to apply and test the tables' rows (Story::apply / holds).
void installStoryData(lscr::Machine& m, Story& story) {
    Story* st = &story;
    // --- Puzzles ---------------------------------------------------------------------
    // 1590 / 1591 (02103fe0 -> 020e6114): a puzzle's state (0 not met, 1
    // met, 2 solved; the record's low two bits) {puzzle, value} equals / does
    // not equal.
    for (int op : {1590, 1591}) {
        m.condition(op, [st, op](Call&, std::span<Value> a) {
            const int n = st->data.puzzle(at(a, 0));
            auto it = st->puzzles.find(n);
            const int state = it == st->puzzles.end() ? 0 : it->second.state;
            return (state == at(a, 1).toInt()) == (op == 1590);
        });
    }
    m.command(9999, [](Call&) { return Result::Next; }); // 021158b0: debug print

    // --- Puzzle records (155x, 1580..1585) --------------------------------------------
    auto puzzleOf = [st](const Value& v) {
        return st->data.puzzle(v);
    };
    m.command(1550, [st, puzzleOf](Call& c) { // 020e6114: {puzzle, out state}
        put(c, 1, Value::of(st->puzzles[puzzleOf(c.at(0))].state));
        return Result::Next;
    });
    m.command(1551, [st, puzzleOf](Call& c) { // 020e6194: {puzzle, state}
        st->setPuzzleState(puzzleOf(c.at(0)), c.integer(1));
        return Result::Next;
    });
    m.command(1552, [st, puzzleOf](Call& c) { // 020e6548: {puzzle, out failures}
        put(c, 1, Value::of(st->puzzles[puzzleOf(c.at(0))].failures));
        return Result::Next;
    });
    m.command(1554, [st, puzzleOf](Call& c) { // 020e65d8: {puzzle, out hints}
        put(c, 1, Value::of(st->puzzles[puzzleOf(c.at(0))].hints));
        return Result::Next;
    });
    m.command(1556, [st, puzzleOf](Call& c) { // 020e6668: {puzzle, out available}
        put(c, 1, Value::of(st->puzzles[puzzleOf(c.at(0))].available ? 1 : 0));
        return Result::Next;
    });
    m.command(1557, [st, puzzleOf](Call& c) { // 020e66a0: {puzzle, available}
        st->puzzles[puzzleOf(c.at(0))].available = c.integer(1) != 0;
        return Result::Next;
    });
    m.command(1558, [st, puzzleOf](Call& c) { // 020e6720: {puzzle, out marked}
        put(c, 1, Value::of(st->puzzles[puzzleOf(c.at(0))].marked ? 1 : 0));
        return Result::Next;
    });
    m.command(1559, [st, puzzleOf](Call& c) { // 020e6758: {puzzle, marked}
        st->puzzles[puzzleOf(c.at(0))].marked = c.integer(1) != 0;
        return Result::Next;
    });
    // 1580 / 1582 / 1585 (020e68f4, 020e6944, 020e69f8): {index (< 0: the
    // count), out}: the found, the available, the newly available lists.
    auto listCommand = [](std::function<std::vector<int>()> list) {
        return [list](Call& c) {
            const auto l = list();
            const int i = c.integer(0);
            put(c, 1, Value::of(i < 0 ? int(l.size()) : i < int(l.size()) ? l[size_t(i)] : -1));
            return Result::Next;
        };
    };
    m.command(1580, listCommand([st] { return st->foundSlots(); }));
    m.command(1582, listCommand([st] { return st->availableSlots(); }));
    m.command(1585, listCommand([st] { return st->newlyAvailable; }));
    m.command(1584, [st](Call&) { // 020e6990
        st->newlyAvailable.clear();
        return Result::Next;
    });
    // 1530 / 1531 (020def5c / 020df058): a place's hint coin {place, index,
    // out | value} (the flag array ひらめきコイン[%04d] of the place).
    auto coinFlag = [st](const Value& place) {
        const PlaceInfo* p =
            place.isText() ? st->data.place(place.toText()) : st->data.place(place.toInt());
        char name[64];
        std::snprintf(name, sizeof name, st->assets.codeText(6, 0x020a7980, 0x020f6bf0).c_str(),
                      p ? p->id : place.toInt());
        return Value::ofText(name);
    };
    m.command(1530, [st, coinFlag](Call& c) {
        put(c, 2, Value::of(st->state.flag(coinFlag(c.at(0)), c.integer(1)) ? 1 : 0));
        return Result::Next;
    });
    m.command(1531, [st, coinFlag](Call& c) {
        st->state.setFlag(coinFlag(c.at(0)), c.integer(2) != 0, c.integer(1));
        return Result::Next;
    });

    // --- Tables (160x, 161x, 1621) ----------------------------------------------------
    m.command(1602, [st](Call& c) { // 02109af8: {place, out title, out map, out guide, out found}
        const PlaceInfo* p =
            c.at(0).isText() ? st->data.place(c.text(0)) : st->data.place(c.integer(0));
        const MapInfo* map = p ? st->data.map(p->map) : nullptr;
        const int guide = p ? st->data.guide(p->guide) : -1;
        put(c, 1, p ? Value::ofText(p->title) : Value::of(0));
        put(c, 2, Value::of(map ? map->id : -1));
        put(c, 3, Value::of(guide));
        put(c, 4, Value::of(p ? 1 : 0));
        return Result::Next;
    });
    m.command(1603, [st](Call& c) { // 02109c70: {guide, place, out x, out y, out found}
        const GuideInfo* g =
            st->data.guideInfo(c.at(0).isText() ? st->data.guide(c.text(0)) : c.integer(0));
        const PlaceInfo* p =
            c.at(1).isText() ? st->data.place(c.text(1)) : st->data.place(c.integer(1));
        const std::array<int, 2>* at = nullptr;
        if (g && p) {
            if (auto it = g->places.find(p->name); it != g->places.end()) {
                at = &it->second;
            }
        }
        put(c, 2, Value::of(at ? (*at)[0] : 0));
        put(c, 3, Value::of(at ? (*at)[1] : 0));
        put(c, 4, Value::of(at ? 1 : 0));
        return Result::Next;
    });
    m.command(1604, [st](Call& c) { // 02109da8: {guide, phase, out x, y, direction, found}
        const GuideInfo* g =
            st->data.guideInfo(c.at(0).isText() ? st->data.guide(c.text(0)) : c.integer(0));
        const Phase* ph =
            c.at(1).isText() ? st->data.phase(c.text(1)) : st->data.phase(c.integer(1));
        const std::array<int, 3>* at = nullptr;
        if (g && ph) {
            if (auto it = g->goals.find(ph->name); it != g->goals.end()) {
                at = &it->second;
            }
        }
        put(c, 2, Value::of(at ? (*at)[0] : 0));
        put(c, 3, Value::of(at ? (*at)[1] : 0));
        put(c, 4, Value::of(at ? (*at)[2] : -1));
        put(c, 5, Value::of(at ? 1 : 0));
        return Result::Next;
    });
    m.command(1605, [st](Call& c) { // 02109f20: {phase, out goal, [out event], [out found]}
        const Phase* ph =
            c.at(0).isText() ? st->data.phase(c.text(0)) : st->data.phase(c.integer(0));
        put(c, 1, ph ? Value::ofText(ph->goal) : Value::of(0));
        if (c.count() >= 3) {
            // The game writes the event into the first output (sic).
            const EventInfo* e = ph ? st->data.event(ph->save) : nullptr;
            put(c, 1, Value::of(e ? e->id : -1));
        }
        put(c, 3, Value::of(ph ? 1 : 0));
        return Result::Next;
    });
    m.command(1606, [st](Call& c) { // 0210a040: {item, out title, out found}
        const ItemInfo* it =
            c.at(0).isText() ? st->data.item(c.text(0)) : st->data.item(c.integer(0));
        put(c, 1, it ? Value::ofText(it->title) : Value::of(0));
        put(c, 2, Value::of(it ? 1 : 0));
        return Result::Next;
    });
    m.command(1607, [st](Call& c) { // 0210a108: {event, out id, out puzzle, out found}
        const EventInfo* e =
            c.at(0).isText() ? st->data.event(c.text(0)) : st->data.event(c.integer(0));
        put(c, 1, Value::of(e ? e->id : -1));
        put(c, 2,
            Value::of(e && !e->puzzle.empty() ? st->data.puzzle(Value::ofText(e->puzzle)) : -1));
        put(c, 3, Value::of(e ? 1 : 0));
        return Result::Next;
    });
    auto timings = [](Call& c, const std::array<int, 3>* t) {
        for (std::size_t i = 0; i < 3; ++i) {
            put(c, i + 1, Value::of(t ? (*t)[i] : 0));
        }
        put(c, 4, Value::of(t ? 1 : 0));
    };
    m.command(1610, [st, timings](Call& c) { // 0210a228: {program, out a, b, c, found}
        timings(c, st->data.programTimings(c.text(0)));
        return Result::Next;
    });
    m.command(1611, [st, timings](Call& c) { // 0210a370: {event, out ...}: its variant's base
        const EventInfo* e =
            c.at(0).isText() ? st->data.event(c.text(0)) : st->data.event(c.integer(0));
        const std::array<int, 3>* t = nullptr;
        if (const auto* variants = e ? st->data.program(e->program) : nullptr) {
            for (const auto& v : *variants) {
                if (st->holds(v.conditions)) {
                    t = st->data.programTimings(v.base);
                    break;
                }
            }
        }
        timings(c, t);
        return Result::Next;
    });
    // 1621 (0210a700): {slot, out number, title, type, title picture, place,
    // found}.
    m.command(1621, [st](Call& c) {
        const auto& slots = st->data.puzzleSlots();
        const int i = c.integer(0);
        const PuzzleSlot* s = i >= 0 && i < int(slots.size()) ? &slots[size_t(i)] : nullptr;
        const PuzzleInfo* p = s ? st->data.puzzleInfo(s->number) : nullptr;
        const PlaceInfo* place = s ? st->data.place(s->place) : nullptr;
        put(c, 1, Value::of(p ? p->number : 0));
        put(c, 2, p ? Value::ofText(p->title) : Value::of(0));
        put(c, 3, p ? Value::ofText(p->typeLabel) : Value::of(0));
        put(c, 4, p ? Value::ofText(p->titlePicture) : Value::of(0));
        put(c, 5, Value::of(p ? (place ? place->id : -1) : 0));
        put(c, 6, Value::of(p ? 1 : 0));
        return Result::Next;
    });
    m.command(1680, [st](Call& c) { // 0210a914: seconds played
        put(c, 0, Value::of(int(st->playFrames / 60)));
        return Result::Next;
    });
    m.command(1902, [st](Call& c) { // 0210b220: the current puzzle
        put(c, 0, Value::of(st->currentPuzzle));
        return Result::Next;
    });

    // --- Items (1580, 159x) --------------------------------------------------------
    m.command(1590, [st](Call&) { // 021096c4 (020e0c40): no notices
        st->itemNotices.clear();
        return Result::Next;
    });
    m.command(1591, [st](Call& c) { // 021096e8 (020e0f1c): collect {item}
        const ItemInfo* it =
            c.at(0).isText() ? st->data.item(c.text(0)) : st->data.item(c.integer(0));
        if (it) {
            st->collect(*it);
        }
        return Result::Next;
    });
    // 1592 (02109724 -> 020e0f40): the first notice of the kinds {out id,
    // out argument, out value, kinds... (all)}: taken from the list; id -1
    // when there is none.
    m.command(1592, [st](Call& c) {
        unsigned mask = 0xffff;
        if (c.count() > 3) {
            mask = 0;
            for (std::size_t i = 3; i < c.count(); ++i) {
                mask |= unsigned(c.integer(i));
            }
        }
        Story::ItemNotice n;
        for (auto it = st->itemNotices.begin(); it != st->itemNotices.end(); ++it) {
            if (unsigned(it->mask) & mask) {
                n = *it;
                st->itemNotices.erase(it);
                break;
            }
        }
        put(c, 0, Value::of(n.id));
        put(c, 1, Value::of(n.argument));
        put(c, 2, Value::of(n.value));
        return Result::Next;
    });
    // 1580 (020e10c0): a notice of the kinds {mask (all)} is waiting.
    m.condition(1580, [st](Call&, std::span<Value> a) {
        const unsigned mask = a.empty() ? 0xffffu : unsigned(a[0].toInt());
        return std::any_of(st->itemNotices.begin(), st->itemNotices.end(),
                           [mask](const Story::ItemNotice& n) { return unsigned(n.mask) & mask; });
    });

    // --- The current event (18xx; 0210ad04..0210b164) -------------------------------
    // Each event has a flag and a counter of its own name (020ea2a4 /
    // 020ea2d8: the game state's flag and variable named like the event).
    auto eventName = [st]() -> Value {
        return st->currentEvent ? Value::ofText(st->currentEvent->name) : Value::of(-1);
    };
    m.command(1800, [st](Call&) { // 020ea158
        st->setEvent(nullptr);
        return Result::Next;
    });
    m.command(1801, [st](Call& c) { // 020ea25c: {event name | number}
        st->setEvent(c.at(0).isText() ? st->data.event(c.text(0)) : st->data.event(c.integer(0)));
        return Result::Next;
    });
    m.command(1802, [st](Call& c) {
        put(c, 0, Value::of(st->currentEvent ? st->currentEvent->id : -1));
        return Result::Next;
    });
    m.command(1803, [st](Call& c) {
        put(c, 0, Value::of(st->currentEvent ? st->currentEvent->group : -1));
        return Result::Next;
    });
    m.command(1804, [st](Call&) { // 020ea310: the event's memo is collected
        if (st->currentEvent) {
            if (const ItemInfo* it = st->data.item(st->currentEvent->memo)) {
                st->collect(*it);
            }
        }
        return Result::Next;
    });
    m.command(1810, [st, eventName](Call& c) {
        put(c, 0, Value::of(st->state.flag(eventName()) ? 1 : 0));
        return Result::Next;
    });
    m.command(1811, [st, eventName](Call& c) {
        st->state.setFlag(eventName(), c.integer(0) != 0);
        return Result::Next;
    });
    m.command(1812, [st, eventName](Call&) {
        st->state.setFlag(eventName(), true);
        return Result::Next;
    });
    m.command(1813, [st, eventName](Call&) {
        st->state.setFlag(eventName(), false);
        return Result::Next;
    });
    m.command(1820, [st, eventName](Call& c) {
        put(c, 0, Value::of(st->state.variable(eventName())));
        return Result::Next;
    });
    auto counter = [st, eventName](int opcode, std::function<int(int, int)> f) {
        return std::pair{opcode, [st, eventName, f](Call& c) {
                             const int now = st->state.variable(eventName());
                             st->state.setVariable(eventName(),
                                                   f(now, c.count() ? c.integer(0) : 0), 0, true);
                             return Result::Next;
                         }};
    };
    for (auto [op, fn] : {counter(1821, [](int, int v) { return v; }),
                          counter(1822, [](int n, int) { return n + 1; }),
                          counter(1823, [](int n, int) { return n - 1; }),
                          counter(1824, [](int n, int v) { return n + v; }),
                          counter(1825, [](int n, int v) { return n - v; }),
                          counter(1826, [](int n, int v) { return n & v; }),
                          counter(1827, [](int n, int v) { return n | v; })}) {
        m.command(op, fn);
    }
    m.condition(1810, [st, eventName](Call&, std::span<Value> a) {
        return st->state.flag(eventName()) == (at(a, 0).toInt() != 0);
    });
    m.condition(1811, [st, eventName](Call&, std::span<Value> a) {
        return st->state.flag(eventName()) != (at(a, 0).toInt() != 0);
    });
    m.condition(1812,
                [st, eventName](Call&, std::span<Value>) { return st->state.flag(eventName()); });
    m.condition(1813,
                [st, eventName](Call&, std::span<Value>) { return !st->state.flag(eventName()); });
    const std::pair<int, std::function<bool(int, int)>> kCompares[] = {{1820,
                                                                        [](int n, int v) {
                                                                            return n == v;
                                                                        }},
                                                                       {1821,
                                                                        [](int n, int v) {
                                                                            return n != v;
                                                                        }},
                                                                       {1822,
                                                                        [](int n, int v) {
                                                                            return n >= v;
                                                                        }},
                                                                       {1823,
                                                                        [](int n, int v) {
                                                                            return n <= v;
                                                                        }},
                                                                       {1824,
                                                                        [](int n, int v) {
                                                                            return n > v;
                                                                        }},
                                                                       {1825,
                                                                        [](int n, int v) {
                                                                            return n < v;
                                                                        }},
                                                                       {1826,
                                                                        [](int n, int v) {
                                                                            return (n & v) != 0;
                                                                        }},
                                                                       {1827, [](int n, int v) {
                                                                            return (n & v) == 0;
                                                                        }}};
    for (const auto& [op, fn] : kCompares) {
        m.condition(op, [st, eventName, fn](Call&, std::span<Value> a) {
            return fn(st->state.variable(eventName()), at(a, 0).toInt());
        });
    }
    // 1830 / 1831 (020deec4): the current place's hint coin {index} found
    // (the flag array "ひらめきコイン[%04d]" of the place) is / is not {value};
    // 1832 / 1833 (jump table 0x02105500): found / not found {index}.
    auto coinFound = [st](int index) {
        char name[64];
        std::snprintf(name, sizeof name, st->assets.codeText(6, 0x020a7980, 0x020f6bf0).c_str(),
                      st->state.variable(st->codeName(0x020f6c88)));
        return st->state.flag(Value::ofText(name), index);
    };
    for (int op : {1830, 1831}) {
        m.condition(op, [op, coinFound](Call&, std::span<Value> a) {
            const bool found = coinFound(at(a, 0).toInt());
            return (found == (at(a, 1).toInt() != 0)) == (op == 1830);
        });
    }
    for (int op : {1832, 1833}) {
        m.condition(op, [op, coinFound](Call&, std::span<Value> a) {
            return coinFound(at(a, 0).toInt()) == (op == 1832);
        });
    }
    // 1890 / 1891 (02103fe0 -> 020ea490): the current puzzle's state is /
    // is not {state} (0 new, 1 seen, 2 solved).
    for (int op : {1890, 1891}) {
        m.condition(op, [st, op](Call&, std::span<Value> a) {
            auto it = st->puzzles.find(st->currentPuzzle);
            const int state = it == st->puzzles.end() ? 0 : it->second.state;
            return (state == at(a, 0).toInt()) == (op == 1890);
        });
    }
    // 1900 (02103fe0): a companion is along {number | name} - the flag
    // "＜同行者＞基本" [number], or "＜同行者＞<name>" (020decf8).
    m.condition(1900, [st](Call&, std::span<Value> a) {
        const Value v = a.empty() ? Value::of(0) : a[0];
        if (v.isText()) {
            char name[96];
            std::snprintf(name, sizeof name, st->codeName(0x0211b3c8, 9).toText().c_str(),
                          v.toText().c_str());
            return st->state.flag(Value::ofText(name));
        }
        return st->state.flag(st->codeName(0x0211b3d8, 9), v.toInt());
    });
    // 1850..1859 (02103fe0, jump table at 0x021043c0): the current phase
    // (020e843c) against phases given by name (020e8218 -> the phase's
    // number) or number. 1850 ==, 1851 !=, 1852 >=, 1853 <=, 1854 >, 1855 <
    // {phase}; 1856 a <= now <= b, 1857 a < now <= b, 1858 a <= now < b,
    // 1859 a < now < b {a, b}.
    auto phaseNumber = [st](const Value& v) {
        if (v.isText()) {
            const Phase* p = st->data.phase(v.toText());
            return p ? p->id : 0;
        }
        return v.toInt();
    };
    for (int op = 1850; op <= 1859; ++op) {
        m.condition(op, [st, op, phaseNumber](Call&, std::span<Value> a) {
            const int now = st->state.variable(st->codeName(0x020f6d6c));
            const int x = a.empty() ? 0 : phaseNumber(a[0]);
            const int y = a.size() < 2 ? 0 : phaseNumber(a[1]);
            switch (op) {
            case 1850:
                return now == x;
            case 1851:
                return now != x;
            case 1852:
                return now >= x;
            case 1853:
                return now <= x;
            case 1854:
                return now > x;
            case 1855:
                return now < x;
            case 1856:
                return x <= now && now <= y;
            case 1857:
                return x < now && now <= y;
            case 1858:
                return x <= now && now < y;
            default:
                return x < now && now < y;
            }
        });
    }

    // --- Places, maps, guides, phases (1700..1703) ------------------------------------
    m.command(1700, [st](Call& c) { // 0210ac24 (020e2990): the current place
        const PlaceInfo* p =
            c.at(0).isText() ? st->data.place(c.text(0)) : st->data.place(c.integer(0));
        if (p) {
            st->setPlace(*p);
        }
        return Result::Next;
    });
    m.command(1701, [st](Call& c) { // 0210ac5c (020e35c4): the current map
        const MapInfo* p = c.at(0).isText() ? st->data.map(c.text(0)) : st->data.map(c.integer(0));
        if (p) {
            st->state.setVariable(st->codeName(0x020f6cac), p->id, 0, true);
        }
        return Result::Next;
    });
    m.command(1702, [st](Call& c) { // 0210ac94 (020e43b4): the current guide
        const int g = c.at(0).isText() ? st->data.guide(c.text(0)) : c.integer(0);
        if (g >= 0) {
            st->state.setVariable(st->codeName(0x020f6cbc), g, 0, true);
        }
        return Result::Next;
    });
    m.command(1703, [st](Call& c) { // 0210accc (020e855c): a story phase
        const Phase* p =
            c.at(0).isText() ? st->data.phase(c.text(0)) : st->data.phase(c.integer(0));
        if (p) {
            st->enterPhase(*p);
        }
        return Result::Next;
    });
}

void installStoryCommands(lscr::Scene& scene, Story& story, Loop& loop) {
    lscr::Machine& m = scene.machine;
    Story* st = &story;
    installStoryData(m, story);

    // --- Loop requests (12xx) --------------------------------------------------------
    m.command(1200, [st](Call&) { // 02106eb4: the current loop ends
        st->request(Story::Request::Pop);
        return Result::Next;
    });
    // 1201 replace, 1202 push, 1203 overlay {loop name, up to four numbers}
    // (02106ec4, 02106f84, 02107034).
    for (auto [op, kind] :
         {std::pair{1201, Story::Request::Replace}, std::pair{1202, Story::Request::Push},
          std::pair{1203, Story::Request::Overlay}}) {
        m.command(op, [st, kind](Call& c) {
            if (c.count() > 0) {
                st->request(kind, c.text(0), loopArgs(c, 1));
            }
            return Result::Next;
        });
    }
    // 1220 replace, 1221 push, 1222 overlay an event {[event]} - by name or
    // number, else the current one (02107284..02107394); the event loop's
    // arguments {event, -1}.
    for (auto [op, kind] :
         {std::pair{1220, Story::Request::Replace}, std::pair{1221, Story::Request::Push},
          std::pair{1222, Story::Request::Overlay}}) {
        m.command(op, [st, kind](Call& c) {
            const EventInfo* e = st->currentEvent;
            if (c.count() > 0) {
                e = c.at(0).isText() ? st->data.event(c.text(0)) : st->data.event(c.integer(0));
            }
            st->request(kind, "EVENT", {e ? e->id : -1, -1, 0, 0});
            return Result::Next;
        });
    }
    // 1230 replace, 1231 push a place {[place]} - by name or number, else
    // the current one (0210741c, 021074a0); arguments {place, 0, -1}.
    for (auto [op, kind] :
         {std::pair{1230, Story::Request::Replace}, std::pair{1231, Story::Request::Push}}) {
        m.command(op, [st, kind](Call& c) {
            int id = st->state.variable(st->codeName(0x020f6c88));
            if (c.count() > 0) {
                const PlaceInfo* p =
                    c.at(0).isText() ? st->data.place(c.text(0)) : st->data.place(c.integer(0));
                if (p) {
                    id = p->id;
                }
            }
            st->request(kind, "PLACE", {id, 0, -1, 0});
            return Result::Next;
        });
    }
    // 1240 replace, 1241 push a puzzle {[puzzle, numbers...]} - else the
    // current one (02107524, 021075d8).
    for (auto [op, kind] :
         {std::pair{1240, Story::Request::Replace}, std::pair{1241, Story::Request::Push}}) {
        m.command(op, [st, kind](Call& c) {
            LoopArgs a = loopArgs(c, 0);
            a[0] = st->currentPuzzle;
            if (c.count() > 0) {
                const int n = st->data.puzzle(c.at(0));
                if (n >= 0) {
                    a[0] = n;
                }
            }
            st->request(kind, "NAZO", a);
            return Result::Next;
        });
    }
    // 1280..1299 (0210768c..02107a10): the loop's own commands. A puzzle
    // keeps its scenes' handlers (1294 / 1295 remember its pieces).
    if (loop.name() != "NAZO") {
        Loop* l = &loop;
        for (int i = 0; i < 20; ++i) {
            m.command(1280 + i, [l, i](Call& c) { return l->command(i, c); });
        }
        for (int i = 0; i < 8; ++i) {
            m.condition(2210 + i,
                        [l, i](Call& c, std::span<Value> a) { return l->condition(i, c, a); });
        }
    }
    // 4031, 4051 and 4250 appear in one chapter's events but are not in the
    // interpreter's command table (overlay 9): the game skips them.
    for (int op : {4031, 4051, 4250}) {
        m.command(op, [](Call&) { return Result::Next; });
    }
    // 2201 (02103fe0 -> loop +0x79c bit 0, kept by 020f3788): an object took
    // this frame's pen (the place's tap on the background is skipped).
    m.condition(2201, [&scene](Call&, std::span<Value>) { return scene.penTaken(); });
    m.condition(2200, [st](Call&, std::span<Value>) { // 02063d3c: requests waiting
        return st->requestsPending();
    });

    // --- Saves (169x) ----------------------------------------------------------------
    // 1690 (0210a994): a save slot's details {slot, field, out}: 0 the
    // player's name, 1 the place's title, 10 whether it holds a game, 20..24
    // hours, minutes, puzzles found, solved, cleared (see a3_saves).
    m.command(1690, [st](Call& c) {
        put(c, 2, Value::of(0));
        const SaveRecord* r = st->saves.slot(c.integer(0));
        if (!r) {
            return Result::Next;
        }
        const SaveHeader& h = r->header;
        switch (c.integer(1)) {
        case 0:
            put(c, 2, Value::ofText(h.name));
            break;
        case 1:
            put(c, 2, Value::ofText(h.place));
            break;
        case 10:
            put(c, 2, Value::of(1));
            break;
        case 20:
            put(c, 2, Value::of(h.hours));
            break;
        case 21:
            put(c, 2, Value::of(h.minutes));
            break;
        case 22:
            put(c, 2, Value::of(h.found));
            break;
        case 23:
            put(c, 2, Value::of(h.solved));
            break;
        case 24:
            put(c, 2, Value::of(h.cleared));
            break;
        default:
            break;
        }
        return Result::Next;
    });
    // 1691 (0210aae4): load {slot, [out: done]}; 1692 (0210ab44): save
    // {[slot (else the one in use)], [out: done]}. The game does it as a
    // job and the scripts wait on condition 2010 (the card busy) - here it
    // is done at once.
    m.command(1691, [st](Call& c) {
        const bool done = st->loadGame(c.integer(0));
        if (c.count() > 1) {
            put(c, 1, Value::of(done));
        }
        return Result::Next;
    });
    m.command(1692, [st](Call& c) {
        const bool done = st->saveGame(c.count() > 0 ? c.integer(0) : st->saveSlot);
        if (c.count() > 1) {
            put(c, 1, Value::of(done));
        }
        return Result::Next;
    });
    m.condition(2010, [](Call&, std::span<Value>) { return false; });
    m.command(1693, [st](Call&) { // 0210abb0 (020dda70): a new game's data
        st->resetGame();
        return Result::Next;
    });
    m.command(1694, [st](Call& c) { // 0210abd4: the slot in use
        put(c, 0, Value::of(st->saveSlot));
        return Result::Next;
    });

    // --- Characters (40xx, 41xx) -----------------------------------------------------
    lscr::Scene* sc = &scene;
    // 4012 (0210e358 -> 020f2658): the characters on stage {names...}:
    // those not listed leave, those missing come (020f2618 -> 020b50b0:
    // a sprite of chr/<animation>.cani named after the character, hidden
    // until the script shows it).
    m.command(4012, [st, sc](Call& c) {
        std::vector<std::string> wanted;
        for (std::size_t i = 0; i < c.count(); ++i) {
            if (!c.text(i).empty()) {
                wanted.push_back(c.text(i));
            }
        }
        for (const auto& n : std::vector<std::string>(sc->cast)) {
            if (std::find(wanted.begin(), wanted.end(), n) == wanted.end()) {
                sc->remove(n);
                std::erase(sc->cast, n);
            }
        }
        for (const auto& n : wanted) {
            if (sc->find(n)) {
                continue;
            }
            const CharacterInfo* ch = st->data.character(n);
            if (!ch) {
                continue;
            }
            sc->loadAnimation(n, 0, "chr/" + ch->animation + ".cani"); // the lower screen
            if (lscr::Object* o = sc->create(lscr::Object::Kind::Sprite, n, n)) {
                o->alpha.set({0});
                sc->cast.push_back(n);
            }
        }
        return Result::Next;
    });
    // 4092 (0210e484): the character name of object {variable, index} (the
    // object list of 4090 / 4091), else nothing.
    m.command(4092, [st, sc](Call& c) {
        const auto all = sc->creationOrder();
        const int i = c.integer(1);
        const bool character =
            i >= 0 && i < int(all.size()) && st->data.character(all[size_t(i)]->name);
        put(c, 0, character ? Value::ofText(all[size_t(i)]->name) : Value::of(0));
        return Result::Next;
    });
    // 4093 / 4094 / 4095 (0210e508, 0210e560, 0210e5ec -> 020b1d3c,
    // 020b1dac, 020b1e2c): every object of the loop: alpha {level}, alpha
    // fade {level, frames}, alpha pulse {period, [from, to]}.
    m.command(4093, [sc](Call& c) {
        for (lscr::Object* o : sc->creationOrder()) {
            o->alpha.set({lscr::fx::fromFloat(c.real(0))});
        }
        return Result::Next;
    });
    m.command(4094, [sc](Call& c) {
        for (lscr::Object* o : sc->creationOrder()) {
            o->alpha.to(1, {lscr::fx::fromFloat(c.real(0))}, lscr::fx::fromFloat(c.real(1)));
        }
        return Result::Next;
    });
    m.command(4095, [sc](Call& c) {
        const int from = c.count() >= 3 ? lscr::fx::fromFloat(c.real(1)) : 0;
        const int to = c.count() >= 3 ? lscr::fx::fromFloat(c.real(2)) : lscr::fx::kOne;
        for (lscr::Object* o : sc->creationOrder()) {
            // As 4142 (020654a0): from `from`, back and forth to `to`.
            o->alpha.set({from});
            o->alpha.to(7, {to}, lscr::fx::fromFloat(c.real(0)));
        }
        return Result::Next;
    });
    // 4132 (0211103c -> 02066510): the character's animation {name, pose,
    // face, flags...}: the first sequence named "pp_ff_...".
    m.command(4132, [sc](Call& c) {
        lscr::Object* o = sc->find(c.text(0));
        if (!o || !o->animation || c.count() < 3) {
            return Result::Next;
        }
        int flags = 0;
        for (std::size_t i = 3; i < c.count(); ++i) {
            flags |= c.integer(i);
        }
        char prefix[16];
        std::snprintf(prefix, sizeof prefix, "%02d_%02d_", c.integer(1), c.integer(2));
        for (const auto& seq : o->animation->sequences) {
            if (seq.name.rfind(prefix, 0) == 0) {
                lscr::play(*o, &seq, flags);
                break;
            }
        }
        return Result::Next;
    });
    // 4550 (021130b8): the stage {map name | number} (020f2b68 / 020f2b38 ->
    // the map row, 020c2398).
    m.command(4550, [st, sc](Call& c) {
        const MapInfo* map =
            c.at(0).isText() ? st->data.map(c.text(0)) : st->data.map(c.integer(0));
        if (map) {
            sc->setStage(map->id);
        }
        return Result::Next;
    });
    // 4560 (02113234): the guide {number | name} (020f2bf8 / 020f2c2c ->
    // the lt3_gmp row, 020c2398 with screen 1): the upper screen's guide map
    // map/g%02d.cimg and two centred messages for the place's title
    // ("lt3_map_msg_p" at 176, 11) and the current goal ("lt3_map_msg_f" at
    // 128, 182) (020c2d74; the place script writes them with 4650). The
    // same guide again keeps everything.
    m.command(4560, [st, sc](Call& c) {
        st->setGuide(*sc, c.at(0).isText() ? st->data.guide(c.text(0)) : c.integer(0));
        return Result::Next;
    });
    // 4552 (02113180) / 4562 (021132fc): the stage's / the guide's number
    // {out} (stage +0x74; -1 none).
    m.command(4552, [sc](Call& c) {
        put(c, 0, Value::of(sc->stage.id));
        return Result::Next;
    });
    m.command(4562, [sc](Call& c) {
        put(c, 0, Value::of(sc->guide.id));
        return Result::Next;
    });
    // 4620 (02113588 -> the talk window's +0x70): show talk lines {message,
    // number}.
    m.command(4620, [sc](Call& c) {
        if (lscr::Object* msg = sc->message(c.text(0))) {
            if (auto* talk = dynamic_cast<TalkWindow*>(msg->behaviour.get())) {
                talk->show(*sc, *msg, c.count() >= 2 ? c.integer(1) : 0);
            }
        }
        return Result::Next;
    });
    // 5000 (02115830): a movie on top {movie, fade in, fade out}.
    m.command(5000, [st](Call& c) {
        st->request(
            Story::Request::Push, "MOVIE",
            {c.integer(0), c.count() > 1 ? c.integer(1) : 0, c.count() > 2 ? c.integer(2) : 0, 0});
        return Result::Next;
    });
    // Object state over the whole loop (02103fe0): 4001 all loaded (020b3cfc;
    // loads are immediate here), 4011 any fading (020b0b8c -> 02065550),
    // 4014 {name} / 4015 any shaking or offset moving (02065d40).
    m.condition(4001, [](Call&, std::span<Value>) { return true; });
    m.condition(4011, [sc](Call&, std::span<Value>) {
        for (lscr::Object* o : sc->creationOrder()) {
            if (o->shown() && o->alpha.active()) {
                return true;
            }
        }
        return false;
    });
    m.condition(4015, [sc](Call&, std::span<Value>) { // 020b0c54: any object shaking
        for (lscr::Object* o : sc->creationOrder()) {
            if (o->shown() &&
                (o->shake.active() || o->shake.value[0] != 0 || o->shake.value[1] != 0)) {
                return true;
            }
        }
        return false;
    });
    // 4661 (021144f8): a talk message's line {message, out speaker, out text,
    // number, [index]}.
    m.command(4661, [sc](Call& c) {
        Value speaker = Value::of(0), text = Value::of(0);
        if (lscr::Object* msg = sc->message(c.text(0))) {
            if (auto* talk = dynamic_cast<TalkWindow*>(msg->behaviour.get())) {
                if (auto line = talk->line(c.integer(3), c.count() > 4 ? c.integer(4) : 0)) {
                    speaker = Value::ofText(line->first);
                    text = Value::ofText(line->second);
                }
            }
        }
        put(c, 1, speaker);
        put(c, 2, text);
        return Result::Next;
    });

    // --- Input -----------------------------------------------------------------------
    // 1150..1154 (0206075c, 02060774, 0206078c, 020607a8, 020607c4): buttons
    // {mask}: held, pressed, ...; 1191 (0206078c with 1): button A pressed.
    // Picarat's host has a touch screen and A only.
    m.condition(1150, [sc](Call&, std::span<Value> a) {
        return (sc->input().keysHeld & unsigned(at(a, 0).toInt())) != 0;
    });
    m.condition(1152, [sc](Call&, std::span<Value> a) {
        return (sc->input().keysPressed & unsigned(at(a, 0).toInt())) != 0;
    });
    m.condition(1191,
                [sc](Call&, std::span<Value>) { return (sc->input().keysPressed & 1u) != 0; });
}

// --- Story services used by the commands ---------------------------------------------

void Story::resetGame() {
    // 020dda70: the save record cleared (flags, variables, puzzles), ten hint
    // coins found and held, no event, puzzle or slot.
    state.resetSaved();
    puzzles.clear();
    itemNotices.clear();
    newlyAvailable.clear();
    playFrames = 0;
    state.setVariable(codeName(0x020f6b40), 10, 0, true);
    state.setVariable(codeName(0x020f6b58), 10, 0, true);
    currentEvent = nullptr;
    currentPuzzle = -1;
    saveSlot = -1;
    // The default names (020db124 with ov6's "..."s at 0x020f6b30/38).
    names = {assets.codeText(6, 0x020a7980, 0x020f6b30),
             assets.codeText(6, 0x020a7980, 0x020f6b38)};
}

namespace {
// A table row as a statement on a scratch thread of `m`.
struct RowRunner {
    lscr::Machine& m;
    lscr::Thread thread{m};
    lscr::Section section;
    lscr::Frame frame;

    explicit RowRunner(lscr::Machine& machine) : m(machine) {
        section.name = "(table)";
        frame.section = &section;
        frame.thread = &thread;
    }

    Result run(const TableCommand& row) {
        std::vector<Value> args(row.operands);
        Call c{m, thread, frame, args, row.opcode};
        return m.dispatch(c);
    }

    bool test(const TableCommand& row) {
        std::vector<Value> args;
        args.push_back(Value::of(row.opcode));
        args.insert(args.end(), row.operands.begin(), row.operands.end());
        Call c{m, thread, frame, std::span<Value>(args).subspan(1), row.opcode};
        return m.evaluate(c, args);
    }
};
} // namespace

void Story::apply(const std::vector<TableCommand>& commands) {
    lscr::Machine m(state);
    lscr::installCore(m);
    installStoryData(m, *this);
    RowRunner r(m);
    for (const auto& row : commands) {
        r.run(row);
    }
}

bool Story::holds(const std::vector<TableCommand>& conditions) {
    lscr::Machine m(state);
    lscr::installCore(m);
    installStoryData(m, *this);
    RowRunner r(m);
    return std::all_of(conditions.begin(), conditions.end(),
                       [&](const TableCommand& row) { return r.test(row); });
}

void Story::setGuide(lscr::Scene& scene, int id) {
    if (id < 0 || !data.guideInfo(id)) {
        return;
    }
    scene.guide.shown = true;
    if (id == scene.guide.id) {
        return;
    }
    scene.resetBackground(1); // 020c2398 -> 020c2130: the screen starts afresh
    scene.guide.shown = true;
    static constexpr std::array<std::pair<std::uint32_t, std::array<int, 2>>, 2> kMessages{
        {{0x020f5ad4, {176, 11}}, {0x020f5ae4, {128, 182}}}};
    for (const auto& [name, at] : kMessages) {
        const std::string n = codeName(name).text;
        scene.removeMessage(n);
        if (lscr::Object* msg = scene.createMessage(n, lscr::Screen::Upper, 0x40, 0)) {
            msg->position.set({at[0] * lscr::fx::kOne, at[1] * lscr::fx::kOne});
            msg->text.style.characterSpacing = 1; // 02069c74 (1, 0)
            msg->text.style.lineSpacing = 0;
            msg->text.style.centerX = true; // +0xf0 | 3
            msg->text.style.centerY = true;
        }
    }
    char path[32];
    std::snprintf(path, sizeof path, "map/g%02d.cimg", id);
    scene.guide.id = id;
    scene.guide.picture = assets.exists(path) ? &assets.picture(path) : nullptr;
}

bool Story::saveGame(int slot) {
    saveSlot = -1;
    if (slot < 0 || slot >= SaveSlots::kCount || !saves.write(slot, record())) {
        return false;
    }
    saveSlot = slot;
    return true;
}

void Story::saveCheckpoint() {
    if (checkpoints) {
        saves.write(SaveSlots::kCheckpoint, record());
    }
}

SaveRecord Story::record() const {
    // 020ddcc8: the header from the game as it is, then the record.
    SaveRecord r;
    SaveHeader& h = r.header;
    h.name = names[0];
    if (const PlaceInfo* p = data.place(state.variable(codeName(0x020f6c88)))) {
        h.place = p->title;
    }
    const std::uint64_t seconds = playFrames / 60;
    if (seconds >= 100 * 3600) {
        h.hours = 99;
        h.minutes = 59;
    } else {
        h.hours = int(seconds / 3600);
        h.minutes = int(seconds / 60 % 60);
    }
    auto count = [&](std::uint32_t name) {
        return state.variable(codeName(name));
    };
    h.found = count(0x020f6b94) + count(0x020f6ba4);
    h.solved = count(0x020f6bbc) + count(0x020f6bcc);
    if (state.flag(codeName(0x020f6be0))) {
        h.cleared = count(0x020f6bbc) < 153 ? 1 : count(0x020f6bcc) < 15 ? 2 : 3;
    }
    r.state = state.savedBytes();
    r.puzzles = puzzles;
    r.names = names;
    r.playFrames = playFrames;
    return r;
}

bool Story::loadCheckpoint() {
    const int slot = saveSlot;
    if (!loadGame(SaveSlots::kCheckpoint)) {
        return false;
    }
    saveSlot = slot;
    return true;
}

bool Story::loadGame(int slot) {
    // 020ddc00: only a slot with a name; the game continues from it.
    saveSlot = -1;
    const SaveRecord* r = saves.slot(slot);
    if (!r || r->header.name.empty()) {
        return false;
    }
    state.restoreSaved(r->state);
    puzzles = r->puzzles;
    names = r->names;
    playFrames = r->playFrames;
    itemNotices.clear();
    newlyAvailable.clear();
    saveSlot = slot;
    return true;
}

void Story::enterPhase(const Phase& phase) {
    // 020e8454: 現在のフェーズ, then the phase's actions. (The game also
    // marks puzzles that become available in this phase - not reproduced
    // yet, hypothesis: the table at +0x5de8.)
    state.setVariable(codeName(0x020f6d6c), phase.id, 0, true);
    apply(phase.actions);
}

void Story::collect(const ItemInfo& item, int argument, int depth) {
    // 020e0c7c.
    if (item.id < 0x100) {
        while (itemNotices.size() > 15) {
            itemNotices.pop_front();
        }
        const int id = item.id;
        if (id >= 0x20 && id < 0xd0) {
            state.setFlag(Value::of(id), true); // 020dedf4: the flag of that number
            const std::uint32_t modeNew = id < 0x50   ? 0x020f6c08
                                          : id < 0x72 ? 0x020f6c28
                                          : id < 0x7e ? 0x020f6c44
                                                      : 0;
            if (modeNew) {
                state.setFlag(codeName(modeNew), true);
            }
        } else if (id < 0x20) {
            state.setVariable(Value::of(id), state.variable(Value::of(id)) + 1, 0, true);
        } else if (id == 0xe0 || id == 0xe1) {
            state.setFlag(codeName(id == 0xe0 ? 0x020f6c60 : 0x020f6c74), true);
        }
        itemNotices.push_back({id, argument, item.value, item.mask});
        return;
    }
    if (depth < 4) {
        for (int m : item.members) {
            if (const ItemInfo* member = data.item(m)) {
                collect(*member, argument, depth + 1);
            }
        }
    }
}

void Story::setPuzzleState(int number, int newState) {
    const int slot = data.slotOf(number);
    if (slot < 0) {
        return;
    }
    PuzzleRecord& r = puzzles[number];
    const int old = r.state;
    r.state = newState & 3;
    if (old == r.state) {
        return;
    }
    auto bump = [&](std::uint32_t name) {
        state.setVariable(codeName(name), state.variable(codeName(name)) + 1, 0, true);
    };
    auto item = [&](std::uint32_t name, int argument) {
        if (const ItemInfo* it = data.item(codeName(name).text)) {
            collect(*it, argument);
        }
    };
    if (old == 0) {
        bump(slot < 0x99 ? 0x020f6b94 : slot < 0xa8 ? 0x020f6ba4 : 0x020f6ccc);
        bump(0x020f6cf4);
    }
    if (r.state != 2) {
        return;
    }
    const PuzzleSlot& s = data.puzzleSlots()[size_t(slot)];
    if (slot < 0xa8) {
        item(0x020f6d18, slot); // ＜ナゾ＞追加
        if (const ItemInfo* it = data.item(s.item)) {
            collect(*it);
        }
        const std::uint32_t solved = slot < 0x99 ? 0x020f6bbc : 0x020f6bcc;
        bump(solved);
        if (state.variable(codeName(solved)) > (slot < 0x99 ? 0x98 : 0xe)) {
            item(slot < 0x99 ? 0x020f6d28 : 0x020f6d40, 0);
        }
    } else {
        bump(0x020f6ce0);
    }
    bump(0x020f6d08);
    if (const PuzzleInfo* p = data.puzzleInfo(number)) {
        const Value total = codeName(0x020f6d58);
        state.setVariable(
            total, state.variable(total) + p->picarats[size_t(std::min(r.failures, 2))], 0, true);
    }
}

std::vector<int> Story::foundSlots() const {
    std::vector<int> out;
    const auto& slots = data.puzzleSlots();
    for (std::size_t i = 0; i < slots.size() && i < 0xa8; ++i) {
        auto it = puzzles.find(slots[i].number);
        if (it != puzzles.end() && it->second.state != 0) {
            out.push_back(int(i));
        }
    }
    return out;
}

std::vector<int> Story::markedSlots() const {
    std::vector<int> out;
    const auto& slots = data.puzzleSlots();
    for (std::size_t i = 0; i < slots.size() && i < 0xa8; ++i) {
        auto it = puzzles.find(slots[i].number);
        if (it != puzzles.end() && it->second.marked) {
            out.push_back(int(i));
        }
    }
    return out;
}

std::vector<int> Story::availableSlots() const {
    std::vector<int> out;
    const auto& slots = data.puzzleSlots();
    for (std::size_t i = 0; i < slots.size() && i < 0xa8; ++i) {
        auto it = puzzles.find(slots[i].number);
        if (it != puzzles.end() && it->second.state != 2 && it->second.available) {
            out.push_back(int(i));
        }
    }
    return out;
}

void Story::setEvent(const EventInfo* event) {
    currentEvent = event;
    currentPuzzle =
        event && !event->puzzle.empty() ? data.puzzle(Value::ofText(event->puzzle)) : -1;
}

void Story::advancePhases() {
    for (int guard = 0; guard < 1000; ++guard) {
        const Phase* now = data.phase(state.variable(codeName(0x020f6d6c)));
        if (!now || !holds(now->conditions)) {
            return;
        }
        const auto& all = data.phases();
        const auto next = std::size_t(now - all.data()) + 1;
        if (next >= all.size()) {
            return;
        }
        enterPhase(all[next]);
    }
}

void Story::setPlace(const PlaceInfo& place) {
    // 020e28b8: the place, its map and guide, and its sound set.
    state.setVariable(codeName(0x020f6c88), place.id, 0, true);
    if (const MapInfo* map = data.map(place.map)) {
        state.setVariable(codeName(0x020f6cac), map->id, 0, true);
    }
    if (const int g = data.guide(place.guide); g >= 0) {
        state.setVariable(codeName(0x020f6cbc), g, 0, true);
    }
    state.setVariable(codeName(0x020f6c98), place.soundSet, 0, true);
}

} // namespace picarat::adventure3
