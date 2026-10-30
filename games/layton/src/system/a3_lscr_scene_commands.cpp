// The scene's script commands and conditions. Each comment names the
// overlay-9 handler it reproduces (see a3_lscr_scene.hpp for the services).
//
// Commands that name an object or message which does not exist do nothing,
// as in the game. Script numbers given as floats are converted to fixed
// point by truncation (fx::fromFloat); frame counts become time (x 4096).
#include "sound/a3_sound.hpp"
#include "system/a3_lscr_scene.hpp"
#include <cstdio>
#include <stdexcept>

namespace picarat::lscr {
namespace {

// Background / resource filter matrices (Scene::BackgroundFilter): 020471fc
// scales the channels (a diagonal matrix); 020479dc scales luma and the two
// chroma axes instead - RGB->YIQ x diag x YIQ->RGB, the static matrices at
// 0x020709a0 / 0x020709e8 built by 0206e980 (sepia flashbacks keep y and
// part of i).
Tween<10>::V channelMatrix(const std::array<int, 3>& d, int level) {
    return {d[0], 0, 0, 0, d[1], 0, 0, 0, d[2], level};
}

Tween<10>::V yiqMatrix(const std::array<int, 3>& d, int level) {
    static constexpr std::array<int, 9> toYiq{1224,  2441, 868,   2404, -1122,
                                              -2142, 466,  -1318, 1273};
    static constexpr std::array<int, 9> fromYiq{4096,  4096, 4096,  3915, -1114,
                                                -4526, 2543, -2650, 6971};
    Tween<10>::V v{};
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) {
            int sum = 0;
            for (int j = 0; j < 3; ++j) {
                sum += fx::mul(fx::mul(toYiq[size_t(r * 3 + j)], d[size_t(j)]),
                               fromYiq[size_t(j * 3 + k)]);
            }
            v[size_t(r * 3 + k)] = sum;
        }
    }
    v[9] = level;
    return v;
}

void put(Call& c, std::size_t i, Value v) {
    if (Value* slot = c.variable(i)) {
        slot->get() = std::move(v);
    }
}

int fixed(const Call& c, std::size_t i) {
    return fx::fromFloat(c.real(i));
}

// The ink colours, entries 0xf0 + n of the lower screen's palette (RGB555;
// 0206c594). Where the game puts them there was not found in its code or
// files: 1..8 (the memo's black, white, red, blue, green, skin, orange,
// brown) are measured from the ROM's screen with each memo colour drawn -
// the memo buttons' own palette (menu/ge/memo/memo_btn) is close but not
// equal. The others are taken as black (hypothesis).
std::uint16_t inkColour(int n) {
    constexpr std::array<std::uint16_t, 9> kMeasured{0x0000, 0x0000, 0x77bd, 0x04f3, 0x45e6,
                                                     0x09ee, 0x3efc, 0x01da, 0x1993};
    return std::uint16_t(0x8000 | (n >= 0 && n < int(kMeasured.size()) ? kMeasured[size_t(n)] : 0));
}

// 0203e514: a value as text (integers in decimal).
std::string asText(const Value& v) {
    const Value& g = v.get();
    switch (g.kind) {
    case Value::Kind::Int:
        return std::to_string(g.integer);
    case Value::Kind::Float: {
        char b[32];
        std::snprintf(b, sizeof b, "%g", double(g.real));
        return b;
    }
    default:
        return g.text;
    }
}

// 020206e0 with the script's arguments: the format and up to two values.
std::string format(const Call& c, std::size_t first) {
    const std::string& f = c.text(first);
    char out[1024];
    const Value& a = c.at(first + 1);
    const Value& b = c.at(first + 2);
    auto arg = [](const Value& v) -> long {
        return v.isText() ? 0 : long(v.toInt());
    };
    if (c.count() > first + 2) {
        if (a.isText() || b.isText()) {
            std::snprintf(out, sizeof out, f.c_str(), a.isText() ? a.toText().c_str() : "",
                          b.isText() ? b.toText().c_str() : "");
        } else {
            std::snprintf(out, sizeof out, f.c_str(), arg(a), arg(b));
        }
    } else if (a.isText()) {
        std::snprintf(out, sizeof out, f.c_str(), a.toText().c_str());
    } else {
        std::snprintf(out, sizeof out, f.c_str(), arg(a));
    }
    return out;
}

// The arguments of an object command after the object's name (41xx/42xx)
// or of a command on the thread's own object (43xx).
struct On {
    Call& call;
    Object& o;
    std::size_t first;

    std::size_t count() const { return call.count() > first ? call.count() - first : 0; }

    const Value& at(std::size_t i) const { return call.at(first + i); }

    int integer(std::size_t i) const { return call.integer(first + i); }

    int fixed(std::size_t i) const { return fx::fromFloat(call.real(first + i)); }

    const std::string& text(std::size_t i) const { return call.text(first + i); }

    void store(std::size_t i, Value v) const { put(call, first + i, std::move(v)); }

    bool condition(std::size_t i) const { return call.condition(first + i); }
};

using ObjectCommand = std::function<void(On&)>;
using ObjectCondition = std::function<bool(Object&, std::span<Value>)>;

} // namespace

const adventure3::Animation::Sequence* sequenceOf(const Object& o, const Value& which) {
    if (!o.animation) {
        return nullptr;
    }
    if (which.isText()) {
        return o.animation->sequence(which.toText());
    }
    const int i = which.toInt();
    return i >= 0 && i < int(o.animation->sequences.size()) ? &o.animation->sequences[size_t(i)]
                                                            : nullptr;
}

void play(Object& o, const adventure3::Animation::Sequence* s, int flags) {
    if (!s) {
        return;
    }
    // 02066114: a "*" sequence (the mouth moving) marks the object as
    // speaking.
    if (s->name.find('*') != std::string::npos) {
        o.talking = true;
    }
    o.playFlags = flags;
    if (!(flags & 4) || o.sequence != s) {
        o.time = 0;
        o.speed = fx::kOne;
        o.loops = 0;
        o.finished = false;
        o.sequence = s;
    }
    o.paused = flags & 1;
    o.once = flags & 2;
}

namespace {

std::array<int, 2> xy(int x, int y) {
    return {x, y};
}
} // namespace

void installScene(Scene& scene) {
    Machine& m = scene.machine;
    Scene* sc = &scene;

    // Object commands: 41xx/42xx name the object, 43xx act on the thread's
    // own object (02063d58).
    auto object = [&m, sc](int opcode, ObjectCommand fn) {
        m.command(opcode, [sc, fn](Call& c) {
            if (Object* o = sc->find(c.text(0))) {
                On on{c, *o, 1};
                fn(on);
            }
            return Result::Next;
        });
        m.command(opcode + 200, [sc, fn](Call& c) {
            if (Object* o = sc->owner(c.thread)) {
                On on{c, *o, 0};
                fn(on);
            }
            return Result::Next;
        });
    };
    auto message = [&m, sc](int opcode, std::function<void(Call&, Object&)> fn) {
        m.command(opcode, [sc, fn](Call& c) {
            if (Object* o = sc->message(c.text(0))) {
                fn(c, *o);
            }
            return Result::Next;
        });
    };

    // --- Resources (3xxx) -------------------------------------------------------
    m.command(3010, [sc](Call&) { // 0210bccc: a resource scope begins
        sc->pushScope();
        return Result::Next;
    });
    m.command(3011, [sc](Call&) { // 0210bcec: ...and ends, freeing its resources
        sc->popScope();
        return Result::Next;
    });
    m.command(3000, [sc](Call&) { // 0210bcac: what this level created goes
        sc->clearLevel();
        return Result::Next;
    });
    m.command(3020, [sc](Call&) { // 0210bd0c: every object's switches saved
        sc->saveSwitches();
        return Result::Next;
    });
    m.command(3021, [sc](Call&) { // 0210bd2c: ...and restored
        sc->restoreSwitches();
        return Result::Next;
    });
    // 3100 (0210be8c -> 020c1704): the programs 3110 loaded are dropped -
    // their sections stay known here (the machine keeps what it compiled;
    // nothing calls them after this in the scripts).
    m.command(3100, [](Call&) { return Result::Next; });
    m.command(3110, [sc](Call& c) { // 0210beac (020f1c8c): load a program {path, [member]}
        std::string path = c.text(0);
        if (c.count() >= 2 && !c.text(1).empty()) {
            // A member of an archive: rc/lt3_plc.cpck + p1002.lbin.
            const auto dot = path.rfind('.');
            path = path.substr(0, dot) + "/" + c.text(1);
        }
        if (sc->assets.exists(path)) {
            sc->load(path);
        }
        sc->loading = true;
        return Result::Next;
    });
    m.command(3500, [sc](Call&) { // 0210d5cc: every picture freed
        sc->freeAll(true);
        return Result::Next;
    });
    m.command(3600, [sc](Call&) { // 0210d754: every animation freed
        sc->freeAll(false);
        return Result::Next;
    });
    m.command(3611, [sc](Call& c) { // 0210d848 (020f2238): animation {name, bank, path, [..]}
        sc->loadAnimation(c.text(0), c.integer(1), c.text(2));
        sc->loading = true;
        return Result::Next;
    });
    // 3311 (0210c7dc -> 020f1dc4): a text table {name, file, [part]} kept
    // with all its rows (020d2664; read like 3310's here).
    m.command(3311, [sc](Call& c) {
        sc->loadTextTable(c.text(0), c.integer(1), c.count() >= 3 ? c.integer(2) : -1);
        return Result::Next;
    });
    m.command(3510, [sc](Call& c) { // 0210d62c: picture {name, bank, path}
        sc->loadPicture(c.text(0), c.integer(1), c.text(2));
        sc->loading = true;
        return Result::Next;
    });
    m.command(3610, [sc](Call& c) { // 0210d7b4: animation {name, bank, path}
        sc->loadAnimation(c.text(0), c.integer(1), c.text(2));
        sc->loading = true;
        return Result::Next;
    });
    m.command(3501, [sc](Call& c) { // 0210d5ec: free a picture
        sc->freeResource(c.text(0));
        return Result::Next;
    });
    m.command(3601, [sc](Call& c) { // 0210d774: free an animation
        sc->freeResource(c.text(0));
        return Result::Next;
    });
    // 0210bc9c: services the loader's queue. Resources load at once in
    // Picarat, so there is nothing to do.
    m.command(2100, [](Call&) { return Result::Next; });
    // 3000: loading finished. Picarat loads at once, but the game's loader
    // works in the background and is never done before the next frame: a
    // wait on it lets the pen's press of this frame pass (puzzle 11's help
    // pages wait for a touch after each picture; the ROM takes 9 frames).
    m.condition(3000, [sc](Call&, std::span<Value>) { return !sc->loading; });

    // --- String buffers (32xx) and text tables (33xx) ----------------------------
    m.command(3201, [sc](Call& c) { // 0210bfb4: a buffer goes
        sc->strings.erase(c.text(0));
        return Result::Next;
    });
    m.command(3210, [sc](Call& c) { // 0210bff4: a buffer {name, size}
        sc->strings[c.text(0)].clear();
        return Result::Next;
    });
    for (int op : {3221, 3223}) { // 0210c0b4 / 0210c3c0: set {name, text | format, values}
        m.command(op, [sc](Call& c) {
            auto it = sc->strings.find(c.text(0));
            if (it != sc->strings.end() && c.count() >= 2) {
                it->second = c.count() >= 3 ? format(c, 1) : asText(c.at(1));
            }
            return Result::Next;
        });
    }
    for (int op : {3222, 3224}) { // 0210c248 / 0210c554: append
        m.command(op, [sc](Call& c) {
            auto it = sc->strings.find(c.text(0));
            if (it != sc->strings.end() && c.count() >= 2) {
                it->second += c.count() >= 3 ? format(c, 1) : asText(c.at(1));
            }
            return Result::Next;
        });
    }
    m.command(3220, [sc](Call& c) { // 0210c044: read {name, variable}
        auto it = sc->strings.find(c.text(0));
        if (it != sc->strings.end()) {
            put(c, 1, Value::ofText(it->second));
        }
        return Result::Next;
    });
    m.command(3310, [sc](Call& c) { // 0210c748: text table {name, file, [part]}
        sc->loadTextTable(c.text(0), c.integer(1), c.count() >= 3 ? c.integer(2) : -1);
        return Result::Next;
    });
    // 0210c9b8: entry {table, out a, out b, id, [index]}. The id is searched
    // in the table's index; `index` picks among that id's rows (0210cad8..
    // 0210caf8: first row + index, if within the id's rows).
    m.command(3321, [sc](Call& c) {
        const int id = c.integer(3);
        const int index = c.count() > 4 ? c.integer(4) : 0;
        std::array<std::string, 2> texts;
        bool found = false;
        if (auto t = sc->textTables.find(c.text(0)); t != sc->textTables.end()) {
            if (auto e = t->second.find(id);
                e != t->second.end() && index >= 0 && index < int(e->second.size())) {
                texts = e->second[std::size_t(index)];
                found = true;
            }
        }
        put(c, 1, found ? Value::ofText(texts[0]) : Value::of(0));
        put(c, 2, found ? Value::ofText(texts[1]) : Value::of(0));
        return Result::Next;
    });

    // --- Puzzle data ---------------------------------------------------------------
    m.command(1540, [sc](Call& c) { // 02108e78: puzzle string {index, variable}
        put(c, 1, Value::ofText(sc->host.puzzleText ? sc->host.puzzleText(c.integer(0)) : ""));
        return Result::Next;
    });
    m.command(1620, [sc](Call& c) { // 0210a4f8: catalogue row {puzzle, outs...}
        const auto rows = sc->assets.script("rc/ge/lt3_rc/lt3_nazo.lbin");
        const import::adventure3::LscrCommand* row = nullptr;
        for (const auto& r : rows.commands) {
            if (r.opcode == 3001 && !r.operands.empty() &&
                int(std::int32_t(r.operands[0].value)) == c.integer(0)) {
                row = &r;
            }
        }
        auto num = [&](size_t i) {
            return Value::of(int(std::int32_t(row->operands[i].value)));
        };
        // Outputs: title, picarats for 0 / 1 / 2+ failures, type, type name,
        // found.
        const std::array<std::function<Value()>, 7> outs{
            [&] { return Value::ofText(sc->text.expand(rows.text(row->operands[2]))); },
            [&] { return num(6); },
            [&] { return num(7); },
            [&] { return num(8); },
            [&] { return num(4); },
            [&] { return Value::ofText(sc->text.expand(rows.text(row->operands[5]))); },
            [&] {
                return Value::of(1);
            }};
        for (size_t i = 0; i < outs.size() && i + 1 < c.count(); ++i) {
            put(c, i + 1, row ? outs[i]() : Value::of(0));
        }
        return Result::Next;
    });
    m.command(1916, [sc](Call& c) { // 0210b414 (020da928): hints opened {count}
        if (sc->host.setPuzzleHints) {
            sc->host.setPuzzleHints(c.integer(0));
        }
        return Result::Next;
    });
    m.command(1917, [sc](Call&) { // 0210b45c (020da908): one hint more
        if (sc->host.setPuzzleHints && sc->host.puzzleHints) {
            sc->host.setPuzzleHints(sc->host.puzzleHints() + 1);
        }
        return Result::Next;
    });
    m.command(1583, [](Call& c) { // 02109458: downloaded puzzles' hint data
        // {index (-1: the counts), outs...} from the downloaded-puzzle table
        // (020dacdc); Picarat has no downloaded puzzles, so every count and
        // entry is 0.
        for (std::size_t i = 1; i < c.count() && i <= 4; ++i) {
            put(c, i, Value::of(0));
        }
        return Result::Next;
    });
    m.command(1910, [sc](Call& c) { // 0210b270: the puzzle's state (2 solved)
        put(c, 0, Value::of(sc->host.puzzleState ? sc->host.puzzleState() : 0));
        return Result::Next;
    });
    m.command(1912, [sc](Call& c) { // 0210b308: failed answers so far
        put(c, 0, Value::of(sc->host.puzzleFailures ? sc->host.puzzleFailures() : 0));
        return Result::Next;
    });

    // --- Screens ---------------------------------------------------------------------
    m.command(2000, [sc](Call& c) { // 0210b5c0: brightness {screen, level}
        if (c.integer(0) >= 0 && c.integer(0) < 2) {
            sc->brightness[size_t(c.integer(0))].set({fixed(c, 1)});
        }
        return Result::Next;
    });
    m.command(2001, [sc](Call& c) { // 0210b62c: a screen's brightness {screen, out}
        const int s = c.integer(0);
        if (s >= 0 && s < 2) {
            put(c, 1, Value::ofFloat(float(sc->brightness[size_t(s)].value[0]) / fx::kOne));
        }
        return Result::Next;
    });
    m.command(2030, [sc](Call& c) { // 0210bc00: the screens swapped {out}
        put(c, 0, Value::of(sc->swapped ? 1 : 0));
        return Result::Next;
    });
    m.command(2031, [sc](Call& c) { // 0210bc4c (0205a864): swap the screens {on}
        sc->swapped = c.integer(0) != 0;
        return Result::Next;
    });
    m.command(2010, [sc](Call& c) { // 0210b7ec (0205375c): both screens' brightness {level}
        for (auto& b : sc->brightness) {
            b.set({fixed(c, 0)});
        }
        return Result::Next;
    });
    for (int op : {2002, 2003}) { // 0210b6a4: fade {screen (0 lower), level, frames}
        m.command(op, [sc](Call& c) {
            const int s = c.integer(0);
            if (s >= 0 && s < 2) {
                sc->brightness[size_t(s)].to(1, {fixed(c, 1)}, fixed(c, 2));
            }
            return Result::Next;
        });
    }
    for (int op : {2012, 2013}) { // 0210b854: both screens {level, frames}; 2013 locked
        m.command(op, [sc, op](Call& c) {
            for (auto& b : sc->brightness) {
                b.to(1, {fixed(c, 0)}, fixed(c, 1), op == 2013);
            }
            return Result::Next;
        });
    }
    m.condition(2100, [sc](Call&, std::span<Value> a) { // fading {screen}
        const int s = a.empty() ? 0 : a[0].toInt();
        return s >= 0 && s < 2 && sc->brightness[size_t(s)].active();
    });
    m.condition(2101, [sc](Call&, std::span<Value>) { // either screen fading
        return sc->brightness[0].active() || sc->brightness[1].active();
    });
    // 2023 (0210babc -> 02053ad4): a screen layer's blend level {screen,
    // layer, level}; 2024 (0210bb48 -> 02053b30): to a level {screen, layer,
    // level, frames}, linearly - unless it is already there.
    m.command(2023, [sc](Call& c) {
        const int s = c.integer(0), l = c.integer(1);
        if (s >= 0 && s < 2 && l >= 0 && l < 6) {
            sc->layerAlpha[size_t(s)][size_t(l)].set({fixed(c, 2)});
        }
        return Result::Next;
    });
    m.command(2024, [sc](Call& c) {
        const int s = c.integer(0), l = c.integer(1);
        if (s >= 0 && s < 2 && l >= 0 && l < 6) {
            sc->layerAlpha[size_t(s)][size_t(l)].to(1, {fixed(c, 2)}, fixed(c, 3));
        }
        return Result::Next;
    });
    // 4220 (02053944): a layer of the screen is still changing {screen};
    // 4221 (02053990): of either screen.
    m.condition(4220, [sc](Call&, std::span<Value> a) {
        const int s = a.empty() ? 0 : a[0].toInt();
        if (s < 0 || s > 1) {
            return false;
        }
        for (const auto& l : sc->layerAlpha[size_t(s)]) {
            if (l.active()) {
                return true;
            }
        }
        return false;
    });
    m.condition(4221, [sc](Call&, std::span<Value>) {
        for (const auto& screen : sc->layerAlpha) {
            for (const auto& l : screen) {
                if (l.active()) {
                    return true;
                }
            }
        }
        return false;
    });
    m.command(4500, [sc](Call& c) { // 02111ee0: backdrop {screen, grey | r, g, b}
        auto channel = [&](size_t i) {
            const int v = std::clamp(fixed(c, i), 0, fx::kOne);
            return int((std::int64_t(v) * 0x1f + 0x800) >> 12) & 0x1f;
        };
        const int s = c.integer(0);
        if (s < 0 || s > 1) {
            return Result::Next;
        }
        const int r = channel(1), g = c.count() >= 4 ? channel(2) : r,
                  b = c.count() >= 4 ? channel(3) : r;
        sc->backdrop[size_t(s)] = std::uint16_t(r | g << 5 | b << 10);
        return Result::Next;
    });
    // 4501 (02112288) / 4502 (021122d8): screen picture {screen, path}
    // (both 020f2af0).
    for (int op : {4501, 4502}) {
        m.command(op, [sc](Call& c) {
            const int s = c.integer(0);
            if (s >= 0 && s < 2) {
                sc->setScreenPicture(
                    s, sc->assets.exists(c.text(1)) ? &sc->assets.picture(c.text(1)) : nullptr);
            }
            return Result::Next;
        });
    }

    // Background filters (02112328..02112d70; see Scene::BackgroundFilter):
    // the current record with one part replaced, set at once or moved to
    // over {frames} (linear, 020c2334). Screens other than 0 and 1 are
    // ignored (lt3_common passes 100 for "none").
    auto filter = [sc](Call& c, std::size_t framesAt, auto change) {
        const int s = c.integer(0);
        if (s < 0 || s > 1) {
            return Result::Next;
        }
        auto& record = sc->backgroundFilter[size_t(s)].record;
        auto next = record.value;
        change(next);
        if (framesAt) {
            record.to(1, next, fixed(c, framesAt));
        } else {
            record.set(next);
        }
        return Result::Next;
    };
    using Filter = lscr::Scene::BackgroundFilter;
    // 4510 / 4511: the level {screen, level -1..1[, frames]}.
    for (int op : {4510, 4511}) {
        m.command(op, [filter, op](Call& c) {
            return filter(c, op == 4511 ? 2 : 0, [&](Tween<10>::V& v) {
                v[Filter::kLevel] = std::clamp(fixed(c, 1), -fx::kOne, fx::kOne);
            });
        });
    }
    // 4520 / 4530: scale the channels {screen, r, g, b[, frames]}; 4521 /
    // 4531: scale y, i and q instead (see yiqMatrix).
    for (int op : {4520, 4521, 4530, 4531}) {
        m.command(op, [filter, op](Call& c) {
            return filter(c, op >= 4530 ? 4 : 0, [&](Tween<10>::V& v) {
                const std::array<int, 3> d{fixed(c, 1), fixed(c, 2), fixed(c, 3)};
                v = op == 4520 || op == 4530 ? channelMatrix(d, v[Filter::kLevel])
                                             : yiqMatrix(d, v[Filter::kLevel]);
            });
        });
    }
    // 3620 / 3621 (0210d8dc / 0210da58): a resource's filter matrix {name,
    // r, g, b} as 4520 / 4521; 3630 / 3631 (0210dd50 / 0210def0) move to it
    // {name, r, g, b, frames}. (3622 / 3632, the level, are unused.)
    for (int op : {3620, 3621, 3630, 3631}) {
        m.command(op, [sc, op](Call& c) {
            const auto* res = sc->resource(c.text(0));
            if (!res || !res->animation) {
                return Result::Next;
            }
            auto& record = sc->tints[res->animation].record;
            auto next = record.value;
            const std::array<int, 3> d{fixed(c, 1), fixed(c, 2), fixed(c, 3)};
            next = op == 3620 || op == 3630 ? channelMatrix(d, next[Filter::kLevel])
                                            : yiqMatrix(d, next[Filter::kLevel]);
            if (op >= 3630) {
                record.to(1, next, fixed(c, 4));
            } else {
                record.set(next);
            }
            return Result::Next;
        });
    }
    // 4540 (02112f20): the stage's offset {screen[, x, y]} - without x and y
    // back to none; 4541 (02112fec) moves it {screen, x, y, frames}.
    m.command(4540, [sc](Call& c) {
        if (c.integer(0) == 0) {
            sc->stage.scroll.set(c.count() >= 3 ? Tween<2>::V{fixed(c, 1), fixed(c, 2)}
                                                : Tween<2>::V{});
        }
        return Result::Next;
    });
    m.command(4541, [sc](Call& c) {
        if (c.integer(0) == 0) {
            sc->stage.scroll.to(1, {fixed(c, 1), fixed(c, 2)}, fixed(c, 3));
        }
        return Result::Next;
    });
    // Conditions (02103fe0): 4210 a screen's filter is moving {screen}
    // (020c22e4), 4211 either screen's (020c3bbc); 4212 the stage's offset is
    // moving {screen} (020c2f9c), 4213 either (020c3bf4).
    m.condition(4210, [sc](Call&, std::span<Value> a) {
        const int s = a.empty() ? 0 : a[0].toInt();
        return s >= 0 && s < 2 && sc->backgroundFilter[size_t(s)].record.active();
    });
    m.condition(4211, [sc](Call&, std::span<Value>) {
        return sc->backgroundFilter[0].record.active() || sc->backgroundFilter[1].record.active();
    });
    m.condition(4212, [sc](Call&, std::span<Value> a) {
        return (a.empty() || a[0].toInt() == 0) && sc->stage.scroll.active();
    });
    m.condition(4213, [sc](Call&, std::span<Value>) { return sc->stage.scroll.active(); });
    // 4301 (020c6f40): every message has loaded (loading is at once here).
    m.condition(4301, [](Call&, std::span<Value>) { return true; });
    // 4553 (021131cc): a stage part shown while a condition holds {sequence,
    // condition...}.
    m.command(4553, [sc](Call& c) {
        sc->showStagePart(c.text(0), c.condition(1));
        return Result::Next;
    });

    // --- Objects: creation -------------------------------------------------------------
    m.command(4001, [sc](Call& c) { // 0210e250: delete
        sc->remove(c.text(0));
        return Result::Next;
    });
    m.command(4301, [sc](Call& c) {
        if (Object* o = sc->owner(c.thread)) {
            sc->remove(o->name);
        }
        return Result::Next;
    });
    m.command(4013, [sc](Call& c) { // 0210e760: picture {name, resource}
        sc->create(Object::Kind::Picture, c.text(0), c.text(1));
        return Result::Next;
    });
    m.command(4014, [sc](Call& c) { // 0210e7b0: sprite {name, resource, [sequence], [flags]}
        if (Object* o = sc->create(Object::Kind::Sprite, c.text(0), c.text(1))) {
            if (c.count() >= 3) {
                play(*o, sequenceOf(*o, c.at(2)), c.count() >= 4 ? c.integer(3) : 0);
            }
        }
        return Result::Next;
    });
    m.command(4018, [sc](Call& c) { // 0210ea18: button {name, resource, N, T, C, values...}
        if (c.count() < 5) {
            return Result::Next;
        }
        if (Object* o = sc->create(Object::Kind::Button, c.text(0), c.text(1))) {
            std::vector<Value> values;
            for (std::size_t i = 5; i < c.count(); ++i) {
                values.push_back(c.at(i));
            }
            sc->objectThread(*o, values);
            o->releasedSection = c.text(2);
            o->touchedSection = c.text(3);
            o->clickedSection = c.text(4);
            o->touch = 0; // 020ae568: released at once
            sc->callOn(*o, o->releasedSection);
        }
        return Result::Next;
    });
    auto touchable = [sc](Call& c, Object::Style style, std::size_t firstValue) -> Object* {
        Object* o = sc->create(Object::Kind::Button, c.text(0), c.text(1), false);
        if (!o) {
            return nullptr;
        }
        std::vector<Value> values;
        for (std::size_t i = firstValue; i < c.count(); ++i) {
            values.push_back(c.at(i));
        }
        sc->objectThread(*o, values);
        o->style = style;
        o->touch = 0;
        return o;
    };
    m.command(4016, [touchable](Call& c) { // 0210e904: switch {name, resource, section, values...}
        if (c.count() >= 3) {
            if (Object* o = touchable(c, Object::Style::Switch, 3)) {
                o->touchedSection = c.text(2);
            }
        }
        return Result::Next;
    });
    m.command(4021,
              [sc, touchable](Call& c) { // 0210ec18: piece {name, resource, N, T, C, values...}
                  if (c.count() >= 5) {
                      if (Object* o = touchable(c, Object::Style::Piece, 5)) {
                          o->releasedSection = c.text(2);
                          o->touchedSection = c.text(3);
                          o->clickedSection = c.text(4);
                          sc->callOn(*o, o->releasedSection); // 020afb88
                      }
                  }
                  return Result::Next;
              });
    m.command(4022, [touchable](Call& c) { // 020b0350: slider {name, resource, N, T, values...}
        if (c.count() >= 4) {
            if (Object* o = touchable(c, Object::Style::Slider, 4)) {
                o->releasedSection = c.text(2);
                o->touchedSection = c.text(3);
            }
        }
        return Result::Next;
    });
    // 1280..1293: the puzzle engine's own commands (scene vtable 0x58 + 4k;
    // the common engine's do nothing, 020f1824).
    for (int k = 0; k < 14; ++k) {
        m.command(1280 + k, [sc, k](Call& c) {
            if (sc->engine) {
                sc->engine->command(*sc, k, c);
            }
            return Result::Next;
        });
    }
    m.command(4023, [sc](Call& c) { // 0210ed48 (020bc558): a falling object {name, resource,
                                    // [sequence], [flags...]}
        if (Object* o = sc->create(Object::Kind::Sprite, c.text(0), c.text(1))) {
            o->dynamic = true;
            if (c.count() >= 3) {
                int flags = 0;
                for (std::size_t i = 3; i < c.count(); ++i) {
                    flags |= c.integer(i);
                }
                play(*o, sequenceOf(*o, c.at(2)), flags);
            }
        }
        return Result::Next;
    });
    m.command(4005, [sc](Call& c) { // 0210e290 (020b46b8): the physics step on or off
        sc->physics = c.integer(0) != 0;
        return Result::Next;
    });
    // 4017 (0210e988 -> 020b8510): an examinable object {name, resource,
    // sequence, section, values...}: touched, it runs its section.
    m.command(4017, [touchable](Call& c) {
        if (c.count() >= 4) {
            if (Object* o = touchable(c, Object::Style::Switch, 4)) {
                o->touchedSection = c.text(3);
                play(*o, sequenceOf(*o, c.at(2)), 0);
            }
        }
        return Result::Next;
    });
    // 4019 (0210eab8 -> 020b9b04): a guide map icon {name, sequence}: a
    // sprite of the guide's icons ("lt3_map_anm_b").
    m.command(4019, [sc](Call& c) {
        Object* o = sc->find(c.text(0));
        if (!o) {
            o = sc->create(Object::Kind::Sprite, c.text(0), "lt3_map_anm_b");
        }
        if (o) {
            play(*o, sequenceOf(*o, c.at(1)), 0);
        }
        return Result::Next;
    });
    m.command(4015, [touchable](
                        Call& c) { // 0210e880 (020b6f04): a script object {name, resource, section}
        if (c.count() >= 3) {
            if (Object* o = touchable(c, Object::Style::Script, 3)) {
                o->releasedSection = c.text(2);
            }
        }
        return Result::Next;
    });
    // 0210ef38 (vtable 0x40 = 020adb48): a script object runs its section.
    object(4110, [sc](On& a) {
        if (a.o.style == Object::Style::Script) {
            sc->callOn(a.o, a.o.releasedSection);
        } else if (a.o.style == Object::Style::Switch) {
            sc->callOn(a.o, a.o.touchedSection);
        } else {
            sc->callOn(a.o, a.o.releasedSection);
        }
    });
    m.command(4090, [sc](Call& c) { // 0210e3d0: how many objects
        put(c, 0, Value::of(int(sc->creationOrder().size())));
        return Result::Next;
    });
    m.command(4091, [sc](Call& c) { // 0210e424: the name of object {variable, index}
        const auto all = sc->creationOrder();
        const int i = c.integer(1);
        put(c, 0,
            i >= 0 && i < int(all.size()) ? Value::ofText(all[size_t(i)]->name) : Value::of(0));
        return Result::Next;
    });
    m.command(4096, [sc](Call& c) { // 0210e6ec: object {point, variable, [index]} on a point
        // 020b2f8c: the index-th object in the draw list linked to the point.
        int k = c.count() >= 3 ? c.integer(2) : 0;
        for (Object* o : sc->drawOrder()) {
            if (o->link == c.text(0) && k-- == 0) {
                put(c, 1, Value::ofText(o->name));
                return Result::Next;
            }
        }
        put(c, 1, Value{});
        return Result::Next;
    });
    // --- Anchor points (34xx) -------------------------------------------------
    m.command(3410, [sc](Call& c) { // 0210cbe4: a free point {name, [x, y]}
        auto& p = sc->point(c.text(0));
        if (c.count() >= 3) {
            p.local = {fixed(c, 1), fixed(c, 2)};
        }
        return Result::Next;
    });
    m.command(3411, [sc](Call& c) { // 0210cc94: a point on an object {name, object, [x, y]}
        if (c.count() < 2) {
            return Result::Next;
        }
        auto& p = sc->point(c.text(0));
        p.attachedTo = c.text(1);
        if (c.count() >= 4) {
            p.local = {fixed(c, 2), fixed(c, 3)};
        }
        return Result::Next;
    });
    m.command(3421, [sc](Call& c) { // 0210ce00: a point's own place
        auto it = sc->points.find(c.text(0));
        const std::array<int, 2> v =
            it == sc->points.end() ? std::array<int, 2>{} : it->second.local;
        put(c, 1, Value::ofFloat(fx::toFloat(v[0])));
        put(c, 2, Value::ofFloat(fx::toFloat(v[1])));
        return Result::Next;
    });
    m.command(3422, [sc](Call& c) { // 0210cef8: a point's place on the screen
        const auto v = sc->pointPosition(c.text(0)).value_or(std::array<int, 2>{});
        put(c, 1, Value::ofFloat(fx::toFloat(v[0])));
        put(c, 2, Value::ofFloat(fx::toFloat(v[1])));
        return Result::Next;
    });
    m.command(3440, [sc](Call& c) { // 0210d088: {point, kind, object}
        auto it = sc->points.find(c.text(0));
        if (it == sc->points.end()) {
            put(c, 1, Value::of(-1));
            put(c, 2, Value::of(0));
        } else {
            const bool attached = !it->second.attachedTo.empty();
            put(c, 1, Value::of(attached ? 1 : 0));
            put(c, 2, attached ? Value::ofText(it->second.attachedTo) : Value::of(0));
        }
        return Result::Next;
    });
    // 3480 / 3482: the nearest point to {x, y} (Manhattan distance below 512
    // pixels, 0x200000), skipping points that would make `object` carry
    // itself (020d3e68 / 020d419c); 3480 keeps the kinds in {mask} (bit 0
    // free, bit 1 attached; -1 all), 3482 only points nothing is linked to.
    auto nearest = [sc](Call& c, bool freeOnly, unsigned mask, const std::string& object) {
        const std::string found =
            sc->nearestPoint({fixed(c, 1), fixed(c, 2)}, mask, object, freeOnly);
        put(c, 0, found.empty() ? Value::of(0) : Value::ofText(found));
    };
    m.command(3480, [nearest](Call& c) { // 0210d194: {variable, x, y, [mask], [object]}
        nearest(c, false, c.count() >= 4 ? unsigned(c.integer(3)) : ~0u,
                c.count() >= 5 ? c.text(4) : "");
        return Result::Next;
    });
    m.command(3482, [nearest](Call& c) { // 0210d424: {variable, x, y, [object]}
        nearest(c, true, ~0u, c.count() >= 4 ? c.text(3) : "");
        return Result::Next;
    });
    m.command(3490, [sc](Call& c) { // 0210d558: {object, variable, [index]}: its points
        int k = c.count() >= 3 ? c.integer(2) : 0;
        for (const auto& [name, p] : sc->pointList()) {
            if (p->attachedTo == c.text(0) && k-- == 0) {
                put(c, 1, Value::ofText(name));
                return Result::Next;
            }
        }
        put(c, 1, Value{});
        return Result::Next;
    });
    m.command(1130, [sc](Call& c) { // 02106dd4: scene variable {index, variable}
        const Value* v = sc->sceneVariable(c.integer(0));
        put(c, 1, v ? *v : Value{});
        return Result::Next;
    });
    m.command(1131, [sc](Call& c) { // 02106e70: set scene variable {index, value}
        if (Value* v = sc->sceneVariable(c.integer(0))) {
            *v = c.at(1).get();
        }
        return Result::Next;
    });
    m.command(4020,
              [sc](Call& c) { // 0210eb10: number {name, resource, format, prefix, spacing, ...}
                  if (Object* o = sc->create(Object::Kind::Number, c.text(0), c.text(1))) {
                      o->format = c.text(2);
                      o->prefix = c.text(3);
                      o->spacing = c.integer(4);
                  }
                  return Result::Next;
              });

    // --- Objects: state ---------------------------------------------------------------
    // 0210ee18..0210eef0: the four switches (shown, input, moving, fourth).
    for (int i = 0; i < 4; ++i) {
        object(4105 + i, [i](On& a) { a.o.switches[size_t(i)].on = a.condition(0); });
    }
    m.command(3023, [sc](Call& c) { // 0210bd9c: input for every object
        sc->setAllInput(c.condition(0));
        return Result::Next;
    });
    object(4111, [sc](On& a) { // 0210ef80 (020b4440): layer, mirror and place from lt3_pos
        if (auto p = sc->assets.position(a.text(0))) {
            sc->setLayer(a.o, p->layer);
            a.o.mirror = p->mirror;
            a.o.position.set(xy(p->x << 12, p->y << 12));
        }
    });
    object(4112, [sc](On& a) { sc->setLayer(a.o, a.integer(0)); });      // 0210efd0
    object(4113, [](On& a) { a.o.mirror = a.integer(0) != 0 ? 1 : 0; }); // 0210f020
    object(4116, [](On& a) { a.store(0, Value::of(a.o.kindTag)); });     // 0210f08c
    object(4117, [](On& a) { a.o.kindTag = a.integer(0); });             // 0210f0cc
    // 0210f0fc / 0210f190: an object's values; a script object's values
    // are its thread's variables 200.. (020bd520 / 020bd530).
    object(4118, [](On& a) {
        const int i = a.integer(0);
        if (a.o.thread) {
            a.store(1, i >= 0 && i < 16 ? a.o.thread->variables[size_t(i)] : Value{});
            return;
        }
        auto it = a.o.user.find(i);
        a.store(1, it == a.o.user.end() ? Value{} : it->second);
    });
    object(4119, [](On& a) {
        const int i = a.integer(0);
        if (a.o.thread) {
            if (i >= 0 && i < 16) {
                a.o.thread->variables[size_t(i)] = a.at(1).get();
            }
            return;
        }
        a.o.user[i] = a.at(1).get();
    });
    object(4120, [](On& a) { a.o.areas.clear(); }); // 0210f1e4
    object(4121, [](On& a) {                        // 0210f200
        if (a.count() >= 5) {
            a.o.areas.push_back(
                {a.integer(0), a.integer(1), a.integer(2), a.integer(3), a.integer(4)});
        }
    });
    object(4124, [](On& a) { // 0210f384: the point it is linked to
        a.store(0, a.o.link.empty() ? Value::of(0) : Value::ofText(a.o.link));
    });
    object(4125, [sc](On& a) { // 0210f3f0: link to a point ("": unlink)
        sc->linkTo(a.o, a.count() ? a.text(0) : "");
    });
    object(4130, [](On& a) { // 0210f638: sequence {name | index, flags...}
        int flags = 0;
        for (std::size_t i = 1; i < a.count(); ++i) {
            flags |= a.integer(i);
        }
        play(a.o, sequenceOf(a.o, a.at(0)), flags);
    });
    object(4131, [](On& a) { // 0210f88c: the sequence's name
        a.store(0, Value::ofText(a.o.sequence ? a.o.sequence->name : ""));
    });
    object(4135, [](On& a) { a.o.starVariant = a.integer(0) != 0; });      // 0210f840
    object(4114, [](On& a) { a.o.trail = a.count() ? a.integer(0) : 0; }); // 0210f050 (02065e18)
    object(4122, [sc](On& a) { // 0210f28c (02065ec8): touch box {out x, y, w, h}
        const auto b = touchBox(a.o);
        const auto [px, py] = sc->pixel(a.o);
        a.store(0, Value::of(px + b[0]));
        a.store(1, Value::of(py + b[1]));
        a.store(2, Value::of(b[2] - b[0]));
        a.store(3, Value::of(b[3] - b[1]));
    });
    object(4126, [](On& a) { // 0210f510: shake {x, y} (none: stop)
        a.o.shake.set(a.count() >= 2 ? xy(a.fixed(0), a.fixed(1)) : xy(0, 0));
    });
    object(4127, [](On& a) { // 0210f594 (02065c88): shake to {x, y, frames}
        if (a.count() >= 3) {
            a.o.shake.to(1, xy(a.fixed(0), a.fixed(1)), a.fixed(2));
        }
    });
    object(4140, [](On& a) { a.o.alpha.set({a.fixed(0)}); });               // 0210f8cc
    object(4141, [](On& a) { a.o.alpha.to(1, {a.fixed(0)}, a.fixed(1)); }); // 0210f92c
    object(4142, [](On& a) { // 0210f9b4: pulse {period, [from, to]}
        const int from = a.count() >= 3 ? a.fixed(1) : 0;
        const int to = a.count() >= 3 ? a.fixed(2) : fx::kOne;
        a.o.alpha.set({from});
        a.o.alpha.to(7, {to}, a.fixed(0));
    });
    object(4200, [](On& a) { // 0210fc94: position into two variables
        a.store(0, Value::ofFloat(fx::toFloat(a.o.position.value[0])));
        a.store(1, Value::ofFloat(fx::toFloat(a.o.position.value[1])));
    });
    object(4201, [](On& a) { a.o.position.set(xy(a.fixed(0), a.fixed(1))); }); // 0210fd30
    object(4202, [](On& a) { // 0210fdb4: move to {x, y, frames, curve}
        a.o.position.to(a.integer(3), xy(a.fixed(0), a.fixed(1)), a.fixed(2));
    });
    object(4203, [](On& a) { // 0210fe6c: move by {dx, dy, frames, curve}
        const auto& p = a.o.position.value;
        a.o.position.to(a.integer(3), xy(p[0] + a.fixed(0), p[1] + a.fixed(1)), a.fixed(2));
    });
    object(4204, [](On& a) { // 0210ff40: offset into two variables
        a.store(0, Value::ofFloat(fx::toFloat(a.o.offset.value[0])));
        a.store(1, Value::ofFloat(fx::toFloat(a.o.offset.value[1])));
    });
    object(4205, [](On& a) { a.o.offset.set(xy(a.fixed(0), a.fixed(1))); });
    object(4206,
           [](On& a) { a.o.offset.to(a.integer(3), xy(a.fixed(0), a.fixed(1)), a.fixed(2)); });
    object(4207, [](On& a) {
        const auto& p = a.o.offset.value;
        a.o.offset.to(a.integer(3), xy(p[0] + a.fixed(0), p[1] + a.fixed(1)), a.fixed(2));
    });

    auto floats = [](On& a, std::array<int, 2> v) {
        a.store(0, Value::ofFloat(fx::toFloat(v[0])));
        a.store(1, Value::ofFloat(fx::toFloat(v[1])));
    };
    object(4208, [floats](On& a) { // 021101ec: position + offset
        floats(a, {a.o.position.value[0] + a.o.offset.value[0],
                   a.o.position.value[1] + a.o.offset.value[1]});
    });
    object(4209, [sc, floats](On& a) { floats(a, sc->absolute(a.o)); }); // 021102cc
    // Grid (0211040c..021105a0; 02042014, 02041f00, 02042148).
    object(4210, [](On& a) { // the current cell
        a.store(0, Value::of(a.o.cell[0]));
        a.store(1, Value::of(a.o.cell[1]));
    });
    object(4211, [](On& a) { // put it in a cell at once
        auto& g = a.o.grid;
        if (g.size <= 0) {
            a.o.cell = {0, 0};
            return;
        }
        a.o.cell = {std::clamp(a.integer(0), 0, g.last[0]), std::clamp(a.integer(1), 0, g.last[1])};
        a.o.position.set({(a.o.cell[0] * g.size + g.origin[0]) << 12,
                          (a.o.cell[1] * g.size + g.origin[1]) << 12});
    });
    object(4212, [](On& a) { // {x, y, last x, last y, size, snap frames}
        auto& g = a.o.grid;
        g = {};
        a.o.cell = {0, 0};
        if (a.count() >= 6 && a.integer(4) > 0) {
            g.origin = {a.integer(0), a.integer(1)};
            g.last = {a.integer(2), a.integer(3)};
            g.size = a.integer(4);
            g.time = a.fixed(5);
        }
    });
    object(4213, [sc](On& a) { // snap to the nearest cell [of a position]
        if (a.count() >= 2) {
            sc->snap(a.o, {a.fixed(0), a.fixed(1)});
        } else {
            sc->snap(a.o, a.o.position.value);
        }
    });
    // Scale (0211062c..02110804).
    object(4220, [floats](On& a) { floats(a, a.o.scale.value); });
    object(4221, [](On& a) { a.o.scale.set({a.fixed(0), a.fixed(1)}); });
    object(4222, [](On& a) { // scale to {x, y, frames, curve}
        a.o.scale.to(a.integer(3), {a.fixed(0), a.fixed(1)}, a.fixed(2));
    });
    object(4223, [](On& a) {
        const auto& v = a.o.scale.value;
        a.o.scale.to(a.integer(3), {v[0] + a.fixed(0), v[1] + a.fixed(1)}, a.fixed(2));
    });
    // Rotation (021108d8..02110a74).
    object(4240, [](On& a) { a.store(0, Value::ofFloat(fx::toFloat(a.o.rotation.value[0]))); });
    object(4241, [](On& a) { a.o.rotation.set({a.fixed(0)}); });
    object(4242, [](On& a) { a.store(0, Value::of(a.o.step)); });
    object(4243, [sc](On& a) { sc->setStep(a.o, a.integer(0)); });
    object(4244, [](On& a) { // steps {count, frames per turn} (020423ac)
        a.o.steps = std::max(0, a.integer(0));
        a.o.step = 0;
        a.o.turnTime = a.o.steps ? a.fixed(1) : 0;
    });
    object(4245, [sc](On& a) { // turn to the nearest step [of an angle]
        sc->turnToward(a.o, a.count() ? a.fixed(0) : a.o.rotation.value[0]);
    });
    // Stacks (020b30cc and neighbours): an object rests on the object its
    // point is attached to.
    auto carrier = [sc](const Object& o) -> Object* {
        auto it = sc->points.find(o.link);
        return o.link.empty() || it == sc->points.end() || it->second.attachedTo.empty()
                   ? nullptr
                   : sc->find(it->second.attachedTo);
    };
    object(4150, [carrier](On& a) { // 0210faac (020b3000): what it rests on
        Object* c = carrier(a.o);
        a.store(0, c ? Value::ofText(c->name) : Value::of(0));
    });
    object(4151, [carrier](On& a) { // 0210fafc (020b31dc): the bottom of its stack
        Object* o = &a.o;
        for (int guard = 0; guard < 64; ++guard) {
            Object* c = carrier(*o);
            if (!c) {
                break;
            }
            o = c;
        }
        a.store(0, Value::ofText(o->name));
    });
    object(4152, [sc, carrier](On& a) { // 0210fb4c (020b3654): how many rest on it
        int n = 0;
        for (Object* o : sc->drawOrder()) {
            n += carrier(*o) == &a.o ? 1 : 0;
        }
        a.store(0, Value::of(n));
    });
    object(4153, [sc, carrier](On& a) { // 0210fb9c (020b3770): its stack from it up
        int n = 1;
        for (Object* o : sc->drawOrder()) {
            for (Object* c = carrier(*o); c; c = carrier(*c)) {
                if (c == &a.o) {
                    ++n;
                    break;
                }
            }
        }
        a.store(0, Value::of(n));
    });
    object(4160, [](On& a) { // 0210fbec
        const int i = a.integer(0);
        a.store(1, Value::of(i >= 0 && i < 2 ? a.o.flags[size_t(i)] : -1));
    });
    object(4161, [](On& a) { // 0210fc48
        const int i = a.integer(0);
        if (i >= 0 && i < 2) {
            a.o.flags[size_t(i)] = a.integer(1);
        }
    });

    // Object conditions: 40xx name the object, 41xx (the same plus 100)
    // ask about the thread's own object (02103fe0).
    auto objectCondition = [&m, sc](int opcode, ObjectCondition fn) {
        m.condition(opcode, [sc, fn](Call&, std::span<Value> a) {
            Object* o = a.empty() ? nullptr : sc->find(a[0].toText());
            return o && fn(*o, a.subspan(1));
        });
        m.condition(opcode + 100, [sc, fn](Call& c, std::span<Value> a) {
            Object* o = sc->owner(c.thread);
            return o && fn(*o, a);
        });
    };
    auto text = [](std::span<Value> a, size_t i) {
        return i < a.size() ? a[i].toText() : "";
    };
    auto num = [](std::span<Value> a, size_t i) {
        return i < a.size() ? a[i].toInt() : 0;
    };
    objectCondition(4000, [](Object&, auto) { return true; }); // exists
    // 4014 (02065d40): shaking or its shake changing.
    objectCondition(4014, [](Object& o, auto) {
        return o.shown() && (o.shake.active() || o.shake.value[0] != 0 || o.shake.value[1] != 0);
    });
    objectCondition(4010, [](Object& o, auto) { return o.shown() && o.alpha.active(); });
    objectCondition(4012, [](Object& o, auto) { // 0206557c: moving or turning
        return o.shown() && (o.position.active() || o.offset.active() || o.rotation.active());
    });
    m.condition(4013, [sc](Call&, std::span<Value>) { // 020b0bf8: any object moving or turning
        for (Object* o : sc->creationOrder()) {
            if (o->shown() &&
                (o->position.active() || o->offset.active() || o->rotation.active())) {
                return true;
            }
        }
        return false;
    });
    objectCondition(4020, [](Object& o, auto) { return o.loops != 0; }); // played through
    objectCondition(4021, [text](Object& o, std::span<Value> a) {
        return o.sequence && o.sequence->name == text(a, 0);
    });
    objectCondition(4030, [](Object& o, auto) { // being touched (vtable 0x3c)
        return o.kind == Object::Kind::Button && o.touch != 0;
    });
    objectCondition(4040, [num](Object& o, std::span<Value> a) { return o.kindTag == num(a, 0); });
    objectCondition(4041, [num](Object& o, std::span<Value> a) { return o.kindTag != num(a, 0); });
    objectCondition(4042, [text](Object& o, std::span<Value> a) { return o.link == text(a, 0); });
    objectCondition(4043, [text](Object& o, std::span<Value> a) { return o.link != text(a, 0); });
    auto value = [](Object& o, int i) -> Value {
        if (o.thread) {
            return i >= 0 && i < 16 ? o.thread->variables[size_t(i)] : Value{};
        }
        auto it = o.user.find(i);
        return it == o.user.end() ? Value{} : it->second;
    };
    objectCondition(4044, [value](Object& o, std::span<Value> a) {
        return a.size() >= 2 && equal(value(o, a[0].toInt()), a[1]);
    });
    objectCondition(4045, [value](Object& o, std::span<Value> a) {
        return a.size() >= 2 && unequal(value(o, a[0].toInt()), a[1]);
    });

    // 4050/4051: the grid cell is (not) within {x, y, [distance]}.
    auto nearCell = [](Object& o, std::span<Value> a) {
        if (a.size() < 2) {
            return false;
        }
        const int d = a.size() >= 3 ? std::max(0, a[2].toInt()) : 0;
        return std::abs(a[0].toInt() - o.cell[0]) <= d && std::abs(a[1].toInt() - o.cell[1]) <= d;
    };
    objectCondition(4050, nearCell);
    objectCondition(4051, [nearCell](Object& o, std::span<Value> a) {
        return a.size() >= 2 && !nearCell(o, a);
    });
    // 4060/4061: the turn step is (not) {step, [also the opposite one]}
    // (02065a38: with an even step count, half a turn apart counts too).
    auto atStep = [](Object& o, std::span<Value> a) {
        if (a.empty()) {
            return false;
        }
        const int want = a[0].toInt();
        const bool symmetric = a.size() >= 2 && a[1].toInt() != 0;
        if (symmetric && o.steps > 0 && (o.steps & 1) == 0) {
            const int half = o.steps >> 1;
            return want % half == o.step % half;
        }
        return want == o.step;
    };
    objectCondition(4060, atStep);
    objectCondition(
        4061, [atStep](Object& o, std::span<Value> a) { return !a.empty() && !atStep(o, a); });

    // --- Input (the game's input object: 02060c00 held, 02060c30 pressed,
    // 02060c60 released, 0206078c keys) -------------------------------------
    m.command(1400, [sc](Call& c) { // 0210860c: where the pen is (scene +0x240/0x244)
        put(c, 0, Value::of(sc->input().x));
        put(c, 1, Value::of(sc->input().y));
        return Result::Next;
    });
    m.condition(1100, [sc](Call&, std::span<Value>) { return sc->input().held; });
    m.condition(1101, [sc](Call&, std::span<Value>) { return sc->input().pressed; });
    m.condition(1102, [sc](Call&, std::span<Value>) { return sc->input().released; });
    m.condition(1190, [sc](Call&, std::span<Value>) { // decide: A or a touch
        return (sc->input().keysPressed & 1) || sc->input().pressed;
    });

    // --- Messages (46xx) -----------------------------------------------------------
    m.command(4610, [sc](Call& c) { // 021141e4: {name, screen (0 lower), capacity, [font]}
        sc->createMessage(c.text(0), c.integer(1) == 1 ? Screen::Upper : Screen::Lower,
                          c.integer(2), c.count() >= 4 ? c.integer(3) : 0);
        return Result::Next;
    });
    m.command(4601, [sc](Call& c) { // 02113368
        sc->removeMessage(c.text(0));
        return Result::Next;
    });
    // 4605..4608 (021133a8..02113510): a message's four switches {name,
    // condition...} (its vtable +0..+0xc): active, input, moving, shown -
    // overlay 33 shows and hides its error message with the fourth.
    for (int i = 0; i < 4; ++i) {
        message(4605 + i, [i](Call& c, Object& o) { o.switches[size_t(i)].on = c.condition(1); });
    }
    message(4635, [](Call& c, Object& o) { // 02113a1c (02069d98): offset {x, y}
        o.offset.set(xy(fixed(c, 1), fixed(c, 2)));
    });
    message(4636, [](Call& c, Object& o) { // 02113ac8 (02069ea4): offset to {x, y, frames, curve}
        o.offset.to(c.integer(4), xy(fixed(c, 1), fixed(c, 2)), fixed(c, 3));
    });
    message(4637, [](Call& c, Object& o) { // 02113ba8: offset by {dx, dy, frames, curve}
        const auto& v = o.offset.value;
        o.offset.to(c.integer(4), xy(v[0] + fixed(c, 1), v[1] + fixed(c, 2)), fixed(c, 3));
    });
    message(4640, [](Call& c, Object& o) { // 02113dc0 (02069cd4): clip {x, y, w, h}
        o.clip = {c.integer(1), c.integer(2), c.integer(3), c.integer(4)};
    });
    m.command(4600, [sc](Call&) { // 02113348 (020f2e34): every message goes
        for (Object* o : sc->creationOrder()) {
            if (o->kind == Object::Kind::Message) {
                sc->removeMessage(o->name);
            }
        }
        return Result::Next;
    });
    m.command(4000, [sc](Call&) { // 0210e230 (020f24f0): every object goes
        for (Object* o : sc->creationOrder()) {
            if (o->kind != Object::Kind::Message) {
                sc->remove(o->name);
            }
        }
        return Result::Next;
    });
    message(4631, [](Call& c, Object& o) { // 021136c8: position
        o.position.set(xy(fixed(c, 1), fixed(c, 2)));
    });
    message(4633, [](Call& c, Object& o) { // 02113854: move by {dx, dy, frames, curve}
        const auto& p = o.position.value;
        o.position.to(c.integer(4), xy(p[0] + fixed(c, 1), p[1] + fixed(c, 2)), fixed(c, 3));
    });
    message(4641, [](Call& c, Object& o) { // 02113e68: spacing {character, line}
        o.text.style.characterSpacing = c.integer(1);
        o.text.style.lineSpacing = c.integer(2);
    });
    // 02113ee8 (02069c54): the colours are indices into the message
    // palette {text, [emphasis]} (defaults 1 and 9); Picarat keeps the
    // TextBox colours until that palette is reconstructed.
    message(4642, [](Call&, Object&) {});
    message(4643, [](Call& c, Object& o) { o.text.style.centerX = c.integer(1) != 0; }); // 02113f7c
    message(4644, [](Call& c, Object& o) { o.text.style.centerY = c.integer(1) != 0; }); // 02113ff0
    // 02114064 (0206b4e0): while a message is revealed, sound {id} plays
    // every {interval} frames (default 5).
    message(4645, [](Call& c, Object& o) {
        o.text.setTyping(c.integer(1), c.count() >= 3 ? c.integer(2) : 5);
    });
    message(4650, [sc](Call& c, Object& o) { // 021140f8 (0206ad94): text {text, [at once]}
        // At once with two operands or a non-zero third, else revealed at
        // the message's speed (0x2000: two steps a frame, 02069b3c).
        o.text.set(c.text(1), sc->text, c.count() < 3 || c.integer(2));
    });
    message(4651, [](Call&, Object& o) { // 02114194: show it all
        o.text.update(false, true, 0);
    });
    m.condition(4310, [sc](Call&, std::span<Value> a) { // message revealed
        Object* o = a.empty() ? nullptr : sc->message(a[0].toText());
        return o && (o->behaviour ? o->behaviour->done(*o) : o->text.finished());
    });

    // --- Handwriting canvases (47xx) ----------------------------------------
    auto canvas = [&m, sc](int opcode, std::function<void(Call&, Object&)> fn) {
        m.command(opcode, [sc, fn](Call& c) {
            if (Object* o = sc->canvas(c.text(0))) {
                fn(c, *o);
            }
            return Result::Next;
        });
    };
    m.command(4712, [sc](Call& c) { // 0211519c (020ce6f4): a writing canvas {name}
        sc->createCanvas(c.text(0));
        return Result::Next;
    });
    m.command(4711, [sc](Call& c) { // 0211515c (020cdedc): a circling canvas {name}
        sc->createCanvas(c.text(0), true);
        return Result::Next;
    });
    // 4713 (021151dc -> 020cef7c): a memo canvas {name} - the paint layer
    // the memo pad and some events draw on.
    m.command(4713, [sc](Call& c) {
        if (Object* o = sc->createCanvas(c.text(0))) {
            o->memo = true;
        }
        return Result::Next;
    });
    canvas(4705, [](Call& c, Object& o) { o.switches[0].on = c.condition(1); }); // 02114870
    // 4706 (021148e8, the paint layer's vtable +4): takes the pen or not
    // {name, condition}.
    canvas(4706, [](Call& c, Object& o) { o.switches[1].on = c.condition(1); });
    canvas(4731, [](Call& c, Object& o) { o.erasing = c.integer(1) == 0; }); // 02114bd0
    canvas(4732, [](Call&, Object& o) { o.erasing = false; });               // 02114c30
    // 4741 (02114cd0, vtable +0x2c) {name, x, y, w, h}: the memo clears the
    // area its "clear all?" window covers (hypothesis: the call's effect
    // was not traced).
    canvas(4741, [](Call& c, Object& o) {
        for (int y = std::max(0, c.integer(2)); y < std::min(192, c.integer(2) + c.integer(4));
             ++y) {
            for (int x = std::max(0, c.integer(1)); x < std::min(256, c.integer(1) + c.integer(3));
                 ++x) {
                o.ink.pixels[size_t(y) * 256 + size_t(x)] = 0;
            }
        }
    });
    // The memo's ink kept between uses: 4761 / 4771 keep it (02114e2c,
    // vtable +0x30), 4762 / 4772 bring it back (flags 0x200 and 0x10000
    // the layer acts on when it is next drawn); which store each pair uses
    // - the puzzle's or the story's memo - is not told apart here. 4760
    // (flag 0x100) brings nothing back: the memo's "clear all" wipes and
    // then calls it, and calls 4762 first only when the answer was no
    // (hypothesis from the script: a redraw of the layer).
    for (int op : {4761, 4771}) {
        canvas(op, [sc](Call&, Object& o) {
            if (sc->host.memos) {
                (*sc->host.memos)[o.name] = o.ink;
            }
        });
    }
    for (int op : {4762, 4772}) {
        canvas(op, [sc](Call&, Object& o) {
            if (sc->host.memos) {
                if (auto it = sc->host.memos->find(o.name); it != sc->host.memos->end()) {
                    o.ink = it->second;
                }
            }
        });
    }
    canvas(4760, [](Call&, Object&) {});
    canvas(4720, [sc](Call& c, Object& o) { // 02114a50 (0206c4f4): area {x, y, w, h}
        o.area = {c.integer(1), c.integer(2), c.integer(3), c.integer(4)};
        if (o.writingMode >= 0) {
            sc->setWritingMode(o, o.writingMode);
        }
    });
    canvas(4721, [](Call& c, Object& o) { // 02114af8: pen width (fixed point)
        o.penWidth = fixed(c, 1) >> 12;
    });
    // 02114b70 (0206c594): the ink colour, entry 0xf0 + n of the screen's
    // palette (n below 16; otherwise black).
    canvas(4730, [](Call& c, Object& o) { o.inkColour = inkColour(c.integer(1)); });
    canvas(4740, [sc](Call&, Object& o) { // 02114c80 (0206c83c): wipe
        if (o.circling) {
            sc->wipeCircle(o);
            return;
        }
        if (o.writing) {
            o.writing->erase(0);
            sc->showReading(o);
        }
        // 0206c880: the box's paint area is cleared.
        for (int y = std::max(0, o.area.y); y < std::min(192, o.area.y + o.area.height); ++y) {
            for (int x = std::max(0, o.area.x); x < std::min(256, o.area.x + o.area.width); ++x) {
                o.ink.pixels[size_t(y) * 256 + size_t(x)] = 0;
            }
        }
    });
    m.command(4750, [sc](Call& c) { // 02114d88: set up for writing {name, mode}
        Object* o = sc->canvas(c.text(0));
        if (o && o->circling) {
            // 020cb450: values 0..2 {most points, error sprite, marker
            // sprite}; both sprites start hidden.
            sc->wipeCircle(*o);
            for (std::size_t i = 1; i < c.count() && i <= 3; ++i) {
                o->canvasValues[i - 1] = c.at(i).get();
            }
            for (int k : {1, 2}) {
                if (Object* s = sc->find(o->canvasValues[size_t(k)].toText())) {
                    s->switches[0].on = false;
                }
            }
        } else if (o && o->memo) {
            // 020f2ffc -> lt3CPaintMemo vtable +0x50 (020cc264): the
            // operands after the name are the thread's variables 200..
            // (the puzzle screen passes the pad's name, then 0).
            std::vector<Value> arguments;
            for (std::size_t i = 1; i < c.count(); ++i) {
                arguments.push_back(c.at(i).get());
            }
            sc->startMemo(*o, arguments);
        } else if (o) {
            sc->setWritingMode(*o, c.integer(1));
        }
        return Result::Next;
    });
    canvas(4780, [](Call& c, Object& o) { // 02114fec: a reading {index, variable}
        if (o.circling) {
            const int i = c.integer(1);
            put(c, 2, i >= 0 && i < 5 ? o.canvasValues[size_t(i)] : Value::of(0));
            return;
        }
        // Index 1: the character read - "" nothing written, "?" unreadable.
        put(c, 2,
            o.writing && c.integer(1) == 1 ? Value::ofText(o.writing->shown(0)) : Value::of(0));
    });

    // --- Sound (48xx) ------------------------------------------------------------
    // Through the third adventure's sound requests (sound/a3_sound.hpp);
    // times are frames. Flags are the OR of the operands after the fixed ones.
    auto flags = [](Call& c, std::size_t from) {
        unsigned f = 0;
        for (std::size_t i = from; i < c.count(); ++i) {
            f |= unsigned(c.integer(i));
        }
        return f;
    };
    auto sound = [sc](auto&& f) {
        if (sc->host.sound) {
            f(*sc->host.sound);
        }
        return Result::Next;
    };
    m.command(4800, [sound, flags](Call& c) { // 0211521c: a sound set {set, flags...}
        return sound([&](Adventure3Sound& s) {
            if (c.count() >= 1) {
                s.selectSet(c.integer(0), flags(c, 1), 60);
            }
        });
    });
    m.command(4801, [sound, flags](Call& c) { // 02115280: {set, frames, flags...}
        return sound([&](Adventure3Sound& s) {
            if (c.count() >= 2) {
                s.selectSet(c.integer(0), flags(c, 2), c.integer(1));
            }
        });
    });
    // 021152f4..02115414: the set, SY, GE and SI banks and music asked for
    // last {variable}.
    for (int k = 0; k < 5; ++k) {
        m.command(4810 + k, [sc, k](Call& c) {
            if (c.count() >= 1 && sc->host.sound) {
                const auto& s = *sc->host.sound;
                const int v = k == 0 ? s.set() : k == 4 ? s.music() : s.bank(k - 1);
                put(c, 0, Value::of(v));
            }
            return Result::Next;
        });
    }
    m.command(4820, [sound](Call& c) { // 0211545c -> 020601ec: an effect {id}
        return sound([&](Adventure3Sound& s) {
            if (c.count() >= 1) {
                s.playEffect(c.integer(0));
            }
        });
    });
    m.command(4821, [sound](Call& c) { // 02115480 -> 02060210: stop {id, [frames]}
        return sound([&](Adventure3Sound& s) {
            if (c.count() >= 1) {
                s.stopEffect(c.integer(0), c.count() >= 2 ? c.integer(1) : 0);
            }
        });
    });
    m.command(4830, [sound](Call& c) { // 021154d4 -> 020d04fc: load music {id}
        return sound([&](Adventure3Sound& s) {
            if (c.count() >= 1) {
                s.loadMusic(c.integer(0), 30);
            }
        });
    });
    m.command(4831, [sound, flags](Call& c) { // 021154fc: play {[volume], [frames], flags...}
        return sound([&](Adventure3Sound& s) {
            s.playMusic(c.count() >= 1 ? c.real(0) : 1.0f, c.count() >= 2 ? c.integer(1) : 0,
                        flags(c, 2));
        });
    });
    m.command(4832, [sound](Call& c) { // 021155d4: stop the music {[frames]}
        return sound([&](Adventure3Sound& s) { s.stopMusic(c.count() >= 1 ? c.integer(0) : 0); });
    });
    m.command(4835, [sound](Call& c) { // 02115600: music volume {volume, [frames]}
        return sound([&](Adventure3Sound& s) {
            s.musicVolume(c.count() >= 1 ? c.real(0) : 1.0f, c.count() >= 2 ? c.integer(1) : 0);
        });
    });
    m.command(4850, [sound](Call& c) { // 02115648: stop the stream {[frames]}
        return sound([&](Adventure3Sound& s) { s.stopStream(c.count() >= 1 ? c.integer(0) : 0); });
    });
    // 02115674 / 02115698 / 021156bc: open a music, ordinary or film stream {id}.
    for (auto [op, route] :
         {std::pair{4851, Adventure3Sound::Music}, std::pair{4852, Adventure3Sound::Ordinary},
          std::pair{4853, Adventure3Sound::Film}}) {
        m.command(op, [sound, route](Call& c) {
            return sound([&](Adventure3Sound& s) {
                if (c.count() >= 1) {
                    s.openStream(route, c.integer(0));
                }
            });
        });
    }
    m.command(4854, [sound](Call& c) { // 021156e0: an event voice {a, b, c}
        return sound([&](Adventure3Sound& s) {
            if (c.count() >= 3) {
                s.openStream(Adventure3Sound::Event, c.integer(0) << 16 | (c.integer(1) & 0xffff),
                             c.integer(2));
            }
        });
    });
    m.command(4860, [sound, flags](Call& c) { // 0211572c: start {[volume], [frames], flags...}
        return sound([&](Adventure3Sound& s) {
            s.startStream(c.count() >= 1 ? c.real(0) : 1.0f, c.count() >= 2 ? c.integer(1) : 0,
                          flags(c, 2));
        });
    });
    m.command(4861, [sound](Call& c) { // 02115804: stop {[frames]}
        return sound([&](Adventure3Sound& s) { s.stopStream(c.count() >= 1 ? c.integer(0) : 0); });
    });
    // Conditions (02103fe0): 4810 the music plays, 4811 a stream is still
    // opening, 4812 it is starting or playing, 4813 {id} an effect plays.
    m.condition(4810, [sc](Call&, std::span<Value>) {
        return sc->host.sound && sc->host.sound->musicPlaying();
    });
    m.condition(4811, [sc](Call&, std::span<Value>) {
        return sc->host.sound && sc->host.sound->streamOpening();
    });
    m.condition(4812, [sc](Call&, std::span<Value>) {
        return sc->host.sound && sc->host.sound->streamPlaying();
    });
    m.condition(4813, [sc](Call&, std::span<Value> a) {
        return !a.empty() && sc->host.sound && sc->host.sound->effectPlaying(a[0].toInt());
    });

    m.condition(1890, [sc](Call&, std::span<Value> a) { // puzzle state ==
        return !a.empty() && sc->host.puzzleState && sc->host.puzzleState() == a[0].toInt();
    });
    m.condition(1891, [sc](Call&, std::span<Value> a) { // puzzle state !=
        return !a.empty() && sc->host.puzzleState && sc->host.puzzleState() != a[0].toInt();
    });
    m.command(1295, [sc](Call&) { // 02107a10 -> 0211ebb4: remember the pieces
        if (!sc->host.pieces) {
            return Result::Next;
        }
        sc->host.pieces->clear();
        for (Object* o : sc->creationOrder()) {
            const bool piece = o->kindTag == 3 || o->kindTag == 6 || o->kindTag == 7 ||
                               (o->kindTag == 0 && o->kind == Object::Kind::Touch);
            if (!piece) {
                continue;
            }
            Scene::Piece p;
            p.name = o->name;
            p.link = o->link;
            p.kindTag = o->kindTag;
            if (o->kindTag == 6 && o->thread) {
                p.value = o->thread->variables[1].toInt();
            }
            p.rotation = o->rotation.value[0];
            p.x = (o->position.value[0] + 0x800) >> 12;
            p.y = (o->position.value[1] + 0x800) >> 12;
            sc->host.pieces->push_back(p);
        }
        return Result::Next;
    });
    m.command(1294, [sc](Call&) { // 021079d4 -> 0211ea34: put the pieces back
        if (!sc->host.pieces) {
            return Result::Next;
        }
        for (const auto& p : *sc->host.pieces) {
            Object* o = sc->find(p.name);
            if (!o) {
                continue;
            }
            if (p.kindTag == 6 && o->thread) {
                o->thread->variables[1] = Value::of(p.value);
            }
            o->link.clear();
            o->rotation.set({p.rotation});
            o->position.set({p.x << 12, p.y << 12});
            if (!p.link.empty()) {
                sc->linkTo(*o, p.link);
            }
        }
        return Result::Next;
    });
    m.command(9999, [](Call&) { return Result::Next; }); // 021158b0: does nothing
    m.command(2022, [sc](Call& c) { // 0210ba54: layer priority {screen, layer, priority}
        const int s = c.integer(0), l = c.integer(1), p = c.integer(2);
        if (s >= 0 && s < 2 && l >= 0 && l < 4 && p >= 0 && p < 4) {
            sc->layerPriority[size_t(s)][size_t(l)] = p;
        }
        return Result::Next;
    });
    m.command(2021, [sc](Call& c) { // 0210b9dc: {screen, layer, variable}
        const int s = c.integer(0), l = c.integer(1);
        put(c, 2,
            Value::of(s >= 0 && s < 2 && l >= 0 && l < 4 ? sc->layerPriority[size_t(s)][size_t(l)]
                                                         : -1));
        return Result::Next;
    });
}

} // namespace picarat::lscr
