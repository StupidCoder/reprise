// The fourth adventure's script services. See a4_services.hpp.
//
// Each handler names the overlay 12 routine it rebuilds. Handlers return
// false where the game's leaves the destination cleared.
#include "system/a4_services.hpp"
#include "sound/a4_sound.hpp"
#include "system/a4_definitions.hpp"
#include "system/a4_progress.hpp"
#include "system/a4_saves.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace picarat::adventure4 {
namespace {
using gss::Call;
using gss::Value;

// Script floats become 20.12 fixed point (x * 4096.0, truncated: 0202973c
// then 02029318).
std::int32_t fixed(float f) {
    return Value::ofFloat(f * 4096.0f).toInt();
}

float unfixed(std::int32_t v) {
    return float(v) / 4096.0f;
}

const std::string& textOr(const Call& c, std::size_t i) {
    static const std::string none;
    const std::string* t = c.text(i);
    return t ? *t : none;
}

bool setInt(Call& c, std::int32_t v) {
    c.result = Value::ofInt(v);
    return true;
}

// Writes a number into a register operand (01ffaccc: int, 01ffad34: float).
void store(Call& c, std::size_t i, Value v) {
    if (Value* r = c.reg(i)) {
        *r = std::move(v);
    }
}

// The animation flags 5133 passes: operand 2 as given (default 0).
unsigned playFlags(const Call& c, std::size_t i) {
    return c.count() > i ? unsigned(c.integer(i)) : 0u;
}

} // namespace

void Services::Touch::update(bool touching, int penX, int penY) {
    if (state == -1) {
        if (touching) {
            return;
        }
        state = 0;
    }
    if (state == 1 || state == 2) {
        if (!touching) {
            state = 3;
        } else {
            x = penX;
            y = penY;
            state = 2;
        }
    } else if (!touching) {
        state = 0;
    } else {
        state = 1;
        x = startX = penX;
        y = startY = penY;
    }
}

void Services::Keys::update(std::uint16_t now) {
    // 020a3814: opposite directions cancel (down masks up, left masks
    // right).
    before = held;
    now &= 0xfff;
    if (now & 0x80) {
        now &= ~0x40;
    }
    if (now & 0x20) {
        now &= ~0x10;
    }
    held = now;
}

GameData::Puzzle findPuzzle(Assets& assets, int number) {
    GameData::Puzzle p;
    const auto list = assets.data("res/@/lt4_def.cb");
    // 2311 lists the puzzles' Japanese names in the order they are shown;
    // a puzzle's index (record +6, shown as index + 1) is its row there
    // (all 229 checked against the records in RAM).
    std::vector<std::string> order;
    for (const auto& c : list.commands) {
        if (c.opcode == 2311 && c.count >= 1) {
            order.push_back(list.text(list.operands[c.first]));
        }
    }
    for (const auto& c : list.commands) {
        if (c.opcode != 2301 || c.count < 8) {
            continue;
        }
        auto num = [&](unsigned i) {
            return int(std::int32_t(list.operands[c.first + i].value));
        };
        if (num(0) != number) {
            continue;
        }
        p.number = number;
        const std::string name = list.text(list.operands[c.first + 1]);
        const auto at = std::find(order.begin(), order.end(), name);
        p.index = at == order.end() ? -1 : int(at - order.begin());
        p.title = list.text(list.operands[c.first + 2]);
        p.type = num(3);
        p.picarats = {num(5), num(6), num(7)};
        if (c.count >= 11) {
            p.localised = {num(8) != 0, num(9) != 0, num(10) != 0};
        }
    }
    return p;
}

std::vector<CatalogueEntry> puzzleCatalogue(Assets& assets) {
    std::vector<CatalogueEntry> out;
    const auto list = assets.data("res/@/lt4_def.cb");
    for (const auto& c : list.commands) {
        if (c.opcode == 2301 && c.count >= 8) {
            const int number = int(std::int32_t(list.operands[c.first].value));
            out.push_back({findPuzzle(assets, number), list.text(list.operands[c.first + 4])});
        }
    }
    return out;
}

int puzzleNumber(Assets& assets, const Value& key) {
    const auto list = assets.data("res/@/lt4_def.cb");
    for (const auto& c : list.commands) {
        if (c.opcode != 2301 || c.count < 2) {
            continue;
        }
        const int number = int(std::int32_t(list.operands[c.first].value));
        if (key.isText() ? list.text(list.operands[c.first + 1]) == key.text
                         : number == key.toInt()) {
            return number;
        }
    }
    return 0;
}

bool setPlace(Assets& assets, GameData& data, const Value& key) {
    // 020c0860 with a place of lt4_def 2151 {id, name, German name, sound
    // set, room, map area, ...} (in RAM: +4 id, +6 sound set, +8 room id
    // (2131 by name), +10 area id (2141 by name)): four variables of the
    // game data take them (names: ov7 020c08d0, 020c08d4, 020bf1a4,
    // 020bfc20); an unknown place leaves only "no place".
    constexpr std::uint32_t kOverlay7 = 0x020b6820;
    auto name = [&](std::uint32_t pool) {
        return assets.codeText(7, kOverlay7, assets.codeWord(7, kOverlay7, pool));
    };
    const Definitions& defs = definitions(assets);
    const Definitions::Row* row = defs.find(2151, key.isText() ? 1 : 0, key);
    if (!row) {
        data.setVariable(name(0x020c08d0), -1);
        return false;
    }
    auto idOf = [&](int opcode, const std::string& n) {
        const Definitions::Row* r = defs.find(opcode, 1, Value::ofText(n));
        return r ? Definitions::number(*r, 0) : -1;
    };
    data.setVariable(name(0x020c08d0), Definitions::number(*row, 0));
    data.setVariable(name(0x020bf1a4), idOf(2131, Definitions::text(*row, 4)));
    data.setVariable(name(0x020bfc20), idOf(2141, Definitions::text(*row, 5)));
    data.setVariable(name(0x020c08d4), Definitions::number(*row, 3));
    data.place = Definitions::number(*row, 0);
    return true;
}

EventRecord findEvent(Assets& assets, const Value& key) {
    const auto list = assets.data("res/@/lt4_def.cb");
    const bool byName = key.isText();
    const int number = byName ? -1 : key.toInt();
    for (const auto& c : list.commands) {
        if (c.opcode != 2321 || c.count < 4) {
            continue;
        }
        EventRecord e;
        e.group = int(std::int32_t(list.operands[c.first].value));
        e.number = int(std::int32_t(list.operands[c.first + 1].value)) & 0xffffff;
        e.name = list.text(list.operands[c.first + 2]);
        e.kind = list.text(list.operands[c.first + 3]);
        if (byName ? e.name == key.text : e.number == number) {
            return e;
        }
    }
    return {};
}

std::map<int, std::array<int, 3>> soundSets(Assets& assets) {
    // lt4_def 2111 {set, music, GE bank, SI bank} (ov11 020f0634 stores
    // them for 020bdce8).
    std::map<int, std::array<int, 3>> sets;
    const auto list = assets.data("res/@/lt4_def.cb");
    for (const auto& c : list.commands) {
        if (c.opcode == 2111 && c.count >= 4) {
            auto num = [&](unsigned i) {
                return int(std::int32_t(list.operands[c.first + i].value));
            };
            sets[num(0)] = {num(1), num(2), num(3)};
        }
    }
    return sets;
}

// ------------------------------------------------------------ data layout

const DataLayout& dataLayout(Assets& assets) {
    static std::map<std::string, std::unique_ptr<DataLayout>> cache;
    auto& l = cache[assets.root.string()];
    if (!l) {
        l = std::make_unique<DataLayout>();
        const Definitions& defs = definitions(assets);
        for (const auto& r : defs.rows(2001)) {
            const std::string name = Definitions::text(r, 1);
            if (!name.empty()) {
                l->flags.emplace(name, Definitions::number(r, 0));
            }
        }
        for (const auto& r : defs.rows(2011)) {
            const std::string name = Definitions::text(r, 2);
            const DataLayout::Field f{Definitions::number(r, 0), Definitions::number(r, 1)};
            if (!name.empty() && f.kind != 0) {
                l->variables.emplace(name, f);
                l->variablesAt.emplace(f.offset, f);
            }
        }
    }
    return *l;
}

namespace {
// The byte block's bytes, kept one key each ("#V<offset>").
int readField(const std::map<std::string, int>& bytes, int at, int kind) {
    const int size = std::abs(kind);
    std::uint32_t v = 0;
    for (int i = 0; i < size; ++i) {
        const auto it = bytes.find("#V" + std::to_string(at + i));
        v |= std::uint32_t(it == bytes.end() ? 0 : it->second & 0xff) << (8 * i);
    }
    if (kind < 0 && size < 4) {
        const std::uint32_t sign = 1u << (8 * size - 1);
        v = (v ^ sign) - sign;
    }
    return std::int32_t(v);
}

void writeField(std::map<std::string, int>& bytes, int at, int kind, int value) {
    for (int i = 0; i < std::abs(kind); ++i) {
        bytes["#V" + std::to_string(at + i)] = int((std::uint32_t(value) >> (8 * i)) & 0xff);
    }
}
} // namespace

int GameData::variable(const std::string& name, int index) const {
    if (layout) {
        if (const auto it = layout->variables.find(name); it != layout->variables.end()) {
            const auto& f = it->second;
            return readField(variables, f.offset + index * std::abs(f.kind), f.kind);
        }
    }
    const auto it = variables.find(name + "#" + std::to_string(index));
    return it == variables.end() ? 0 : it->second;
}

void GameData::setVariable(const std::string& name, int value, int index) {
    if (layout) {
        if (const auto it = layout->variables.find(name); it != layout->variables.end()) {
            const auto& f = it->second;
            writeField(variables, f.offset + index * std::abs(f.kind), f.kind, value);
            return;
        }
    }
    variables[name + "#" + std::to_string(index)] = value;
}

int GameData::variableAt(int offset, int index) const {
    if (layout) {
        if (const auto it = layout->variablesAt.find(offset); it != layout->variablesAt.end()) {
            return readField(variables, offset + index * std::abs(it->second.kind),
                             it->second.kind);
        }
    }
    return 0;
}

void GameData::setVariableAt(int offset, int value, int index) {
    if (layout) {
        if (const auto it = layout->variablesAt.find(offset); it != layout->variablesAt.end()) {
            writeField(variables, offset + index * std::abs(it->second.kind), it->second.kind,
                       value);
        }
    }
}

void GameData::normalize() {
    if (!layout) {
        return;
    }
    auto split = [](const std::string& key, std::string& name, int& index) {
        const auto hash = key.rfind('#');
        if (hash == std::string::npos || hash == 0 || key[0] == '#') {
            return false;
        }
        name = key.substr(0, hash);
        index = std::atoi(key.c_str() + hash + 1);
        return true;
    };
    std::map<std::string, int> f, v;
    for (const auto& [key, value] : flags) {
        std::string name;
        int index = 0;
        if (split(key, name, index)) {
            f[flagKey(name, index)] = value;
        } else {
            f[key] = value;
        }
    }
    flags = std::move(f);
    std::vector<std::pair<std::pair<std::string, int>, int>> legacy;
    for (const auto& [key, value] : variables) {
        std::string name;
        int index = 0;
        if (split(key, name, index) && layout->variables.count(name)) {
            legacy.push_back({{name, index}, value});
        } else {
            v[key] = value;
        }
    }
    variables = std::move(v);
    for (const auto& [ni, value] : legacy) {
        setVariable(ni.first, value, ni.second);
    }
}

std::string flagName(Assets& assets, int id) {
    const auto list = assets.data("res/@/lt4_def.cb");
    for (const auto& c : list.commands) {
        if (c.opcode == 2001 && c.count >= 2 && std::int32_t(list.operands[c.first].value) == id) {
            return list.text(list.operands[c.first + 1]);
        }
    }
    return {};
}

void loadPuzzleParameters(Assets& assets, GameData& data) {
    data.parameters = {};
    char path[40];
    std::snprintf(path, sizeof path, "res/naz/scr/@/n%03d.cb", data.puzzle.number);
    if (!assets.exists(path)) {
        return;
    }
    const auto list = assets.data(path);
    auto value = [&](const import::adventure4::Operand& o) {
        const auto kind = Value::Kind(o.type & 15);
        if (kind == Value::Kind::Text || kind == Value::Kind::Name) {
            return Value::ofText(list.text(o));
        }
        return Value{kind, o.value, {}};
    };
    for (const auto& c : list.commands) {
        const auto* ops = &list.operands[c.first];
        if (c.opcode == 3000 && c.count >= 5) { // 020b8008
            constexpr std::size_t slot[5] = {0, 1, 3, 2, 4};
            for (std::size_t i = 0; i < 5; ++i) {
                data.parameters[slot[i]] = value(ops[i]);
            }
        } else if (c.opcode >= 3001 && c.opcode <= 3007 && c.count >= 1) { // 020b809c
            data.parameters[std::size_t(c.opcode - 3001 + 5)] = value(ops[0]);
        }
    }
}

void installServices(gss::Machine& m, Services& s) {
    Scene& scene = s.scene;
    GameData& data = s.data;
    scene.playEffect = [&s](int id) {
        s.effect(id);
    };

    // ------------------------------------------------------------ input
    // 1000 020f03a0 (020a3e9c): a key press (020a39f0, not modelled) or
    // the pen just put down.
    // 020a3e9c: A pressed or the pen just down.
    m.service(1000,
              [&](Call& c) { return setInt(c, s.keys.pressed(0) || s.touch.state == 1 ? 1 : 0); });
    // The buttons, only while the loop takes input (+0x48 bit 0):
    // 1100 up (020a3994), 1101 held (020a3974), 1102 released (020a39b4),
    // 1103 pressed (020a39f0), 1104 repeating (020a3a2c; *taken as
    // pressed*, the repeat timing of +6 is not read).
    auto button = [&](Call& c, auto test) {
        if (c.count() == 0) {
            return false;
        }
        const bool input = !s.loop || !s.loop->inputOff();
        return setInt(c, input && test(c.integer(0)) ? 1 : 0);
    };
    m.service(1100, [&, button](Call& c) {
        return button(c, [&](int k) { return k >= 0 && k < 12 && !(s.keys.held >> k & 1); });
    });
    m.service(1101, [&, button](Call& c) {
        return button(c, [&](int k) { return k >= 0 && k < 12 && (s.keys.held >> k & 1); });
    });
    m.service(1102, [&, button](Call& c) {
        return button(c, [&](int k) { return s.keys.released(k); });
    });
    m.service(1103,
              [&, button](Call& c) { return button(c, [&](int k) { return s.keys.pressed(k); }); });
    m.service(1104,
              [&, button](Call& c) { return button(c, [&](int k) { return s.keys.pressed(k); }); });
    m.service(1200, [&](Call& c) { // 020f0660: the pen is up (while the loop takes input)
        const bool input = s.loop && !s.loop->inputOff();
        return setInt(c, input && s.touch.state != 1 && s.touch.state != 2);
    });
    m.service(1201, [&](Call& c) { // 020f06c0
        const bool busy = s.loop == nullptr;
        return setInt(c, !busy && (s.touch.state == 1 || s.touch.state == 2));
    });
    m.service(1202, [&](Call& c) { return setInt(c, s.touch.state == 3); }); // 020f0720
    m.service(1203, [&](Call& c) { return setInt(c, s.touch.state == 1); }); // 020f077c
    m.service(1210, [&](Call& c) {                                           // 020f07d8
        if (c.count() == 0) {
            return false;
        }
        store(c, 0, Value::ofInt(s.touch.x));
        store(c, 1, Value::ofInt(s.touch.y));
        return setInt(c, 1);
    });
    m.service(1211, [&](Call& c) { return setInt(c, s.touch.x); });
    m.service(1212, [&](Call& c) { return setInt(c, s.touch.y); });
    m.service(1213, [&](Call& c) { // 020f087c
        if (c.count() == 0) {
            return false;
        }
        store(c, 0, Value::ofInt(s.touch.startX));
        store(c, 1, Value::ofInt(s.touch.startY));
        return setInt(c, 1);
    });
    m.service(1214, [&](Call& c) { return setInt(c, s.touch.startX); });
    m.service(1215, [&](Call& c) { return setInt(c, s.touch.startY); });

    // ------------------------------------------------------------- loop
    // The loop manager (ov7 020c971c asks for loop numbers).
    m.service(2000, [&](Call& c) { // 020f0aa8: requests wait
        return setInt(c, s.loops && s.loops->pending() ? 1 : 0);
    });
    m.service(2002, [&](Call& c) { // 020f0af0: the top loop ends
        return setInt(c, s.loops && s.loops->pop() ? 1 : 0);
    });
    m.service(2004, [&](Call& c) { // 020f0b38 {loop, [a], [b]}
        if (c.count() == 0) {
            return false;
        }
        const int a = c.count() >= 2 ? c.integer(1) : -1;
        const int b = c.count() >= 3 ? c.integer(2) : -1;
        return setInt(c, s.loops && s.loops->request(c.integer(0), {a, b, -1, -1, -1, -1}) ? 1 : 0);
    });
    m.service(2010, [&](Call& c) { // 020f0ca8 {place, [b]}: a room (1100)
        if (c.count() == 0) {
            return false;
        }
        const int b = c.count() >= 2 ? c.integer(1) : -1;
        return setInt(c,
                      s.loops && s.loops->request(1100, {c.integer(0), b, -1, -1, -1, -1}) ? 1 : 0);
    });
    m.service(2011, [&](Call& c) { // 020f0d74 {event, [b]}: an event (1200)
        if (c.count() == 0) {
            return false;
        }
        const EventRecord e = findEvent(scene.assets, c.at(0));
        if (e.number < 0) {
            return false;
        }
        const int b = c.count() >= 2 ? c.integer(1) : -1;
        return setInt(c, s.loops && s.loops->request(1200, {e.number, b, -1, -1, -1, -1}) ? 1 : 0);
    });
    m.service(2012, [&](Call& c) { // 020f0e50 {puzzle}: a puzzle (3000)
        if (c.count() == 0) {
            return false;
        }
        const int number = puzzleNumber(scene.assets, c.at(0));
        if (number <= 0) {
            return false;
        }
        return setInt(c, s.loops && s.loops->request(3000, {number, -1, -1, -1, -1, -1}) ? 1 : 0);
    });
    // 2900 020f15e4 / 2901 020f160c (020cc8b4 / 020cc8cc): a frame count
    // starts; frames since then (unsigned).
    m.service(2900, [&](Call& c) {
        data.timerStart = data.frames;
        return setInt(c, 1);
    });
    m.service(2901, [&](Call& c) {
        c.result = Value::ofUint(std::uint32_t(data.frames - data.timerStart));
        return true;
    });
    // 3042 020f1f38 / 3043 020f1f9c: the play time's hours and minutes.
    auto playMinutes = [&] {
        return std::uint64_t(double(data.playFrames) / (59.8261 * 60.0));
    };
    m.service(3042, [&, playMinutes](Call& c) { return setInt(c, int(playMinutes() / 60)); });
    m.service(3043, [&, playMinutes](Call& c) { return setInt(c, int(playMinutes() % 60)); });
    m.service(3060, [&](Call& c) { // 020f21fc {item}: its number (020baf80), -1 none
        if (!c.count()) {
            return false;
        }
        const auto* r = definitions(scene.assets).find(2021, c.at(0).isText() ? 1 : 0, c.at(0));
        return setInt(c, r ? Definitions::number(*r, 0) : -1);
    });
    m.service(3061, [&](Call& c) { // 020f2238 {item}: its text (record +8, operand 4)
        if (!c.count()) {
            return false;
        }
        const auto* r = definitions(scene.assets).find(2021, c.at(0).isText() ? 1 : 0, c.at(0));
        if (!r) {
            return false;
        }
        c.result = Value::ofText(Definitions::text(*r, 4));
        return true;
    });
    m.service(2920, [&](Call& c) { return setInt(c, 1); }); // 020cc8e8: no time limit
    // 020cc914 {on, frames, ..., loop}: the trial version's time limit,
    // which asks for a loop when it runs out (020cc9e8). The full game
    // never has one (2930).
    m.service(2921, [&](Call& c) { return c.count() >= 4 && setInt(c, 0); });
    m.service(2930, [&](Call& c) { return setInt(c, 0); }); // 020f1734: trial (+0xaae5 bit 2)
    m.service(2922, [&](Call& c) { // 020f16f4 {on}: game +0xaaf8 (*role not identified*)
        if (c.count() == 0) {
            return false;
        }
        data.filmFlag = c.at(0).truthy();
        return setInt(c, 1);
    });
    m.service(2101, [&](Call& c) { // 020f0f30: callback on loop thread 0
        if (c.count() == 0 || !s.loop) {
            return false;
        }
        std::vector<Value> args;
        for (std::size_t i = 1; i < c.count() && i <= 8; ++i) {
            args.push_back(c.at(i));
        }
        return setInt(c, s.loop->callback(0, textOr(c, 0), args) ? 1 : 0);
    });
    m.service(2001, [&](Call& c) { return setInt(c, s.loops ? s.loops->depth() : 0); }); // 020f0acc
    m.service(2003, [&](Call& c) {
        return setInt(c, s.loops && s.loops->restart() ? 1 : 0);
    });                            // 020f0b14
    m.service(2100, [&](Call& c) { // 020f0ed4 {block}: loop thread 0 starts there
        if (c.count() == 0 || !s.loop) {
            return false;
        }
        return setInt(c, s.loop->startThread(0, textOr(c, 0)) ? 1 : 0);
    });
    m.service(2102, [&](Call& c) { // 020f0fe0 {n}: the loop's value n
        if (c.count() == 0 || !s.loop) {
            return false;
        }
        c.result = s.loop->loopValue(c.integer(0));
        return true;
    });
    m.service(2111, [&](Call& c) { // 020f109c {block, args...}: a callback on loop thread 1
        if (c.count() == 0 || !s.loop) {
            return false;
        }
        std::vector<Value> args;
        for (std::size_t i = 1; i < c.count() && i <= 8; ++i) {
            args.push_back(c.at(i));
        }
        return setInt(c, s.loop->callback(1, textOr(c, 0), args) ? 1 : 0);
    });
    m.service(2110, [&](Call& c) { // 020f103c: starts loop thread 1
        if (c.count() == 0 || !s.loop) {
            return false;
        }
        return setInt(c, s.loop->startThread(1, textOr(c, 0)) ? 1 : 0);
    });
    m.service(2200, [&](Call& c) { // 020f11b0
        if (c.count() == 0) {
            return false;
        }
        if (s.loop) {
            s.loop->setBusy(c.at(0).truthy());
        }
        return setInt(c, 1);
    });
    m.service(2301, [&](Call& c) { // 020f1238: clears the loop's touch record
        s.touch.state = 0;
        return setInt(c, 1);
    });
    // 2600 020f140c {mode}: the DS starts another program (020cc7b4: 0 the
    // trial's end, 4 London Life on the English cartridge, 8 the Nintendo
    // WFC settings utility, dwc/utility.bin) and the game boots again after
    // it. The port has none of these programs: the game starts over (every
    // loop ends; the game loop opens the title again).
    m.service(2600, [&](Call& c) {
        if (!s.loops) {
            return false;
        }
        for (int i = s.loops->depth(); i > 0; --i) {
            s.loops->pop();
        }
        return setInt(c, 1);
    });
    m.service(2410, [&](Call& c) { return setInt(c, s.loop && s.loop->jobRunning()); }); // 020f12d4
    m.service(2411, [&](Call& c) { return setInt(c, s.loop && s.loop->startJob()); });   // 020f12fc
    m.service(2400, [&](Call& c) { // 020f1264: the loop's own commands
        if (c.count() == 0 || !s.loop) {
            return false;
        }
        return s.loop->command(c, c.integer(0));
    });
    // 2502/2503 020f13b4/020f13e0: pending file requests (the port loads at
    // once).
    m.service(2502, [&](Call& c) { return setInt(c, 0); });
    m.service(2503, [&](Call& c) { return setInt(c, 0); });
    m.service(2700, [&](Call& c) { // 020f1470: whether a file exists
        if (c.count() == 0) {
            return false;
        }
        return setInt(c, scene.assets.exists(textOr(c, 0)) ? 1 : 0);
    });

    // 2910 020f1630: the game's language (game +0x18; an index into ov6
    // 020b51ec, "jp", "en", "sp", "fr", "it", "ge", ...). Puzzles keep a
    // variant per language.
    m.service(2910, [&](Call& c) { return setInt(c, data.language); });

    // -------------------------------------------------------- game data
    // A flag or variable operand: a name, or its number in lt4_def (ov7
    // 020b9708: a text looks the name up, anything else the number).
    auto flagOf = [&](const Call& c, int index) {
        return c.at(0).isText() ? data.flag(c.at(0).text, index)
                                : data.flagAt(c.integer(0) + index);
    };
    auto setFlagOf = [&](const Call& c, int index, bool on) {
        if (c.at(0).isText()) {
            data.setFlag(c.at(0).text, on, index);
        } else {
            data.setFlagAt(c.integer(0) + index, on);
        }
    };
    auto variableOf = [&](const Call& c, int index) {
        return c.at(0).isText() ? data.variable(c.at(0).text, index)
                                : data.variableAt(c.integer(0), index);
    };
    auto setVariableOf = [&](const Call& c, int index, int value) {
        if (c.at(0).isText()) {
            data.setVariable(c.at(0).text, value, index);
        } else {
            data.setVariableAt(c.integer(0), value, index);
        }
    };
    m.service(3000, [&](Call& c) { // 020f1758: a new game's data (020b854c)
        data.resetStory();
        return setInt(c, 1);
    });
    // ------------------------------------------------------------ saves
    // ov7 020b8978, after saving and loading: the table's flag 0 marks
    // that a finished game exists (the flag "ゲームクリアー", or a slot
    // whose header says finished).
    auto markFinished = [&] {
        if (!s.saves || s.saves->flag(0)) {
            return;
        }
        bool finished = data.flag(
            scene.assets.codeText(7, 0x020b6820, scene.assets.codeWord(7, 0x020b6820, 0x020b8a3c)));
        for (int i = 0; i < SaveSlots::kCount && !finished; ++i) {
            const SaveRecord* r = s.saves->slot(i);
            finished = r && r->header.cleared != 0;
        }
        if (finished) {
            s.saves->setFlag(0, true);
        }
    };
    // 2802 020f1528 {[slot], [flag]}: saves (020ccba4 starts the card job,
    // 020b8698 writes; no slot: the current one). The port writes at once.
    m.service(2802, [&, markFinished](Call& c) {
        const int slot = c.count() ? c.integer(0) : data.currentSlot;
        data.currentSlot = -1;
        bool ok = s.saves && slot >= 0 && slot < SaveSlots::kCount &&
                  s.saves->write(slot, saveRecord(scene.assets, data));
        if (ok) {
            data.currentSlot = slot;
            markFinished();
            showSlots(data, *s.saves);
        }
        return setInt(c, ok ? 1 : 0);
    });
    // 2801 020f14e8 {slot}: loads (020ccc2c, 020b85d0): the game data comes
    // back, the story catches up with it (020bc024).
    m.service(2801, [&, markFinished](Call& c) {
        const int slot = c.count() ? c.integer(0) : -1;
        data.currentSlot = -1;
        const SaveRecord* r = s.saves ? s.saves->slot(slot) : nullptr;
        if (!r) {
            return setInt(c, 0);
        }
        restore(data, *r);
        data.currentSlot = slot;
        markFinished();
        advance(scene.assets, data);
        return setInt(c, 1);
    });
    m.service(2803, [&](Call& c) { return setInt(c, 0); }); // 020f15bc: the card job busy (never)
    m.service(3052, [&](Call& c) { return setInt(c, data.currentSlot); }); // 020f21dc
    m.service(3050, [&](Call& c) { // 020f2094 {flag}: the slot table's flag (020d5d70)
        return setInt(c, s.saves && c.count() && s.saves->flag(c.integer(0)));
    });
    m.service(3051, [&](Call& c) { // 020f20d8 {slot, field}
        if (c.count() < 2) {
            return false;
        }
        const int slot = c.integer(0);
        if (slot < 0 || slot > 2) {
            return false;
        }
        const auto& v = data.slots[std::size_t(slot)];
        const int field = c.integer(1);
        if (field == 0 || field == 1) {
            c.result = Value::ofText(field == 0 ? v.first : v.second);
        } else if (field == 10) {
            c.result = Value::ofInt(v.used ? 1 : 0);
        } else if (field >= 20 && field <= 24) {
            c.result = Value::ofInt(v.numbers[std::size_t(field - 20)]);
        }
        return true;
    });
    m.service(3100, [&](Call& c) { // 020f2278 {character}: its number (lt4_def 2121; -1 none)
        if (c.count() == 0) {
            return false;
        }
        const auto* row = definitions(scene.assets).find(2121, c.at(0).isText() ? 1 : 0, c.at(0));
        return setInt(c, row ? (Definitions::number(*row, 0) & 0x3ff) : -1);
    });
    // A character's files and voice (2121 {id, name, portrait, pack,
    // sound}; record +4 bits 20..29, 10..19, 30..31 + 40): 3101 the talk
    // portrait (020b69fc "chr/@/c%03d.ci"), 3102 the pack (020b6a30
    // "chr/c%03d.ca"), 3103 the typing sound.
    auto character = [&](Call& c) -> const Definitions::Row* {
        return c.count() ? definitions(scene.assets).find(2121, c.at(0).isText() ? 1 : 0, c.at(0))
                         : nullptr;
    };
    m.service(3101, [&, character](Call& c) { // 020f22bc
        const auto* row = character(c);
        const int n = row ? Definitions::number(*row, 2) & 0x3ff : 0;
        if (!n) {
            return false;
        }
        char path[24];
        std::snprintf(path, sizeof path, "chr/@/c%03d.ci", n);
        c.result = Value::ofText(path);
        return true;
    });
    m.service(3102, [&, character](Call& c) { // 020f2358
        const auto* row = character(c);
        const int n = row ? Definitions::number(*row, 3) & 0x3ff : 0;
        if (!n) {
            return false;
        }
        char path[24];
        std::snprintf(path, sizeof path, "chr/c%03d.ca", n);
        c.result = Value::ofText(path);
        return true;
    });
    m.service(3103, [&, character](Call& c) { // 020f23f4
        const auto* row = character(c);
        return row && setInt(c, 40 + ((Definitions::number(*row, 4) - 40) & 3));
    });
    // An event's record (020c2640, by number or name): 3300 number, 3301
    // group, 3302 kind, 3303 sound set, 3304 puzzle, 3305 note; 3306
    // {event, 0|1, [branch]} one of its branch's two numbers (020b7f4c:
    // the first whose conditions hold).
    auto eventOf = [&](Call& c) -> const Event* {
        if (c.count() == 0) {
            return nullptr;
        }
        const Value& k = c.at(0);
        if (k.isText()) {
            const EventRecord r = findEvent(scene.assets, k);
            return r.number >= 0 ? event(scene.assets, r.number) : nullptr;
        }
        return event(scene.assets, k.toInt() & 0xffffff);
    };
    m.service(3300, [&, eventOf](Call& c) {
        const Event* e = eventOf(c);
        return c.count() && setInt(c, e ? e->number : -1);
    });
    m.service(3301, [&, eventOf](Call& c) {
        const Event* e = eventOf(c);
        return c.count() && setInt(c, e ? e->group : -1);
    });
    m.service(3302, [&, eventOf](Call& c) {
        const Event* e = eventOf(c);
        return e && setInt(c, e->kind);
    });
    m.service(3303, [&, eventOf](Call& c) {
        const Event* e = eventOf(c);
        return e && setInt(c, e->sound);
    });
    m.service(3304, [&, eventOf](Call& c) {
        const Event* e = eventOf(c);
        return e && setInt(c, e->puzzle);
    });
    m.service(3305, [&, eventOf](Call& c) {
        const Event* e = eventOf(c);
        return e && setInt(c, e->memo);
    });
    m.service(3306, [&, eventOf](Call& c) {
        const Event* e = c.count() >= 2 ? eventOf(c) : nullptr;
        if (!e) {
            return false;
        }
        const auto* branches = eventKind(scene.assets, e->kind);
        const Branch* b = nullptr;
        if (branches) {
            const int pick = c.count() > 2 ? c.integer(2) : -1;
            if (pick < 0) {
                for (const Branch& x : *branches) {
                    if (holdsAll(scene.assets, data, x.conditions)) {
                        b = &x;
                        break;
                    }
                }
            } else if (std::size_t(pick) < branches->size()) {
                b = &(*branches)[std::size_t(pick)];
            }
            if (!b) {
                return false;
            }
        }
        const int field = c.integer(1);
        return setInt(c, b && field >= 0 && field < 2 ? (field == 0 ? b->a : b->b) : -1);
    });
    m.service(3904, [&](Call& c) { return setInt(c, data.event); }); // 020f3e08 (game +0x5b94)
    // 3901..3903 (020bf14c, 020bfbc8, 020bddc8): the room, the map area and
    // the sound set the story is at (variables named in ov7).
    auto storyVariable = [&](std::uint32_t pool) {
        constexpr std::uint32_t kOverlay7 = 0x020b6820;
        return data.variable(
            scene.assets.codeText(7, kOverlay7, scene.assets.codeWord(7, kOverlay7, pool)));
    };
    m.service(3901, [&, storyVariable](Call& c) { return setInt(c, storyVariable(0x020bf1a4)); });
    m.service(3902, [&, storyVariable](Call& c) { return setInt(c, storyVariable(0x020bfc20)); });
    m.service(3903, [&, storyVariable](Call& c) { return setInt(c, storyVariable(0x020c08d4)); });
    // Item notices (game data +0x540c).
    m.service(3490, [&](Call& c) { // 020f3a68: none left (020bafd0)
        data.notices.clear();
        return setInt(c, 1);
    });
    m.service(3491, [&](Call& c) { // 020f3a94 {item}: received (020bb270)
        if (c.count() == 0) {
            return false;
        }
        giveItem(scene.assets, data, c.at(0), 0);
        return setInt(c, 1);
    });
    m.service(3492, [&](Call& c) { // 020f3ad0 {-> item, index, mask, kind, [masks...]} (020bb294)
        // The first notice whose mask meets the masks given (all without
        // any) leaves the queue; none: item -1.
        unsigned want = c.count() > 4 ? 0 : 0xffff;
        for (std::size_t i = 4; i < c.count(); ++i) {
            want |= unsigned(c.integer(i));
        }
        GameData::Notice n;
        for (auto it = data.notices.begin(); it != data.notices.end(); ++it) {
            if (unsigned(it->mask) & want) {
                n = *it;
                data.notices.erase(it);
                break;
            }
        }
        if (n.item < 0) {
            n = {-1, 0, 0, 0};
        }
        const int out[] = {n.item, n.index, n.mask, n.kind};
        for (std::size_t i = 0; i < 4 && i < c.count(); ++i) {
            store(c, i, Value::ofInt(out[i]));
        }
        return setInt(c, 1);
    });
    m.service(3493, [&](Call& c) { return setInt(c, data.notices.empty() ? 0 : 1); }); // 020f3bb0
    m.service(3900, [&](Call& c) { // 020f3d68: the current place (020c0848)
        return setInt(c, data.place);
    });
    m.service(3906, [&](Call& c) { // 020f3e50: the current story step (020bc0a0)
        return setInt(c, currentStep(scene.assets, data));
    });
    // 3502 020f3c58: the event a continued game opens with: the one named
    // after the current step's heading ("◆..."; 020bbdfc, record +8 by
    // 020c2464); -1 none.
    m.service(3502, [&](Call& c) {
        const int now = currentStep(scene.assets, data);
        for (const Step& st : steps(scene.assets)) {
            if (st.id == now) {
                const Event* e = nullptr;
                if (const auto* row =
                        definitions(scene.assets).find(2321, 2, Value::ofText(st.heading))) {
                    e = event(scene.assets, Definitions::number(*row, 1));
                }
                return setInt(c, e ? e->number : -1);
            }
        }
        return setInt(c, -1);
    });
    m.service(3500, [&](Call& c) { // 020f3bdc {step name}: its number (-1 none)
        if (c.count() == 0) {
            return false;
        }
        int id = -1;
        if (const std::string* n = c.textOrName(0)) {
            for (const Step& st : steps(scene.assets)) {
                if (st.name == *n) {
                    id = st.id;
                    break;
                }
            }
        }
        return setInt(c, id);
    });
    // ---------------------------------------------------- puzzle index
    // 3408 020f2f08 {puzzle}: its thumbnail (020b6d80, "nazo/@/thm/n%03dt.ci"
    // when language-specific).
    m.service(3408, [&](Call& c) {
        const int number = c.count() ? puzzleNumber(scene.assets, c.at(0)) : -1;
        if (number <= 0) {
            return false;
        }
        const GameData::Puzzle p = findPuzzle(scene.assets, number);
        char path[40];
        std::snprintf(path, sizeof path, "nazo/%sthm/n%03dt.ci", p.localised[2] ? "@/" : "",
                      number);
        c.result = Value::ofText(path);
        return true;
    });
    // 3410 020f2f94 {row}: the puzzle of a row of the index order (lt4_def
    // 2311, game data +0x5670; by name or number), -1 none.
    m.service(3410, [&](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        static std::map<std::string, std::vector<int>> rows; // pack -> numbers by row
        auto& order = rows[scene.assets.root.string()];
        if (order.empty()) {
            std::vector<int> byIndex;
            for (const auto& e : puzzleCatalogue(scene.assets)) {
                if (e.puzzle.index >= 0) {
                    if (std::size_t(e.puzzle.index) >= byIndex.size()) {
                        byIndex.resize(std::size_t(e.puzzle.index) + 1, -1);
                    }
                    byIndex[std::size_t(e.puzzle.index)] = e.puzzle.number;
                }
            }
            order = byIndex;
        }
        if (!c.at(0).isText()) {
            const int row = c.integer(0);
            return setInt(c, row >= 0 && std::size_t(row) < order.size() ? order[std::size_t(row)]
                                                                         : -1);
        }
        const int number = puzzleNumber(scene.assets, c.at(0));
        return setInt(c, number > 0 ? number : -1);
    });
    // 3405 020f2db0 {puzzle}: its type's name (record +0x10, lt4_def 2301
    // field 4).
    m.service(3405, [&](Call& c) {
        const int number = c.count() ? puzzleNumber(scene.assets, c.at(0)) : -1;
        for (const auto& e : puzzleCatalogue(scene.assets)) {
            if (e.puzzle.number == number) {
                c.result = Value::ofText(e.typeName);
                return true;
            }
        }
        return false;
    });
    // 3411 020f2ff0 {row}: the place of a row of the index order (2311
    // field 1, as its 2151 number; 020c04b4), -1 none.
    m.service(3411, [&](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const auto& defs = definitions(scene.assets);
        const auto& rows = defs.rows(2311);
        const Definitions::Row* row = nullptr;
        if (c.at(0).isText()) {
            row = defs.find(2311, 0, c.at(0));
        } else if (c.integer(0) >= 0 && std::size_t(c.integer(0)) < rows.size()) {
            row = &rows[std::size_t(c.integer(0))];
        }
        if (!row) {
            return setInt(c, -1);
        }
        const auto* place = defs.find(2151, 1, Value::ofText(Definitions::text(*row, 1)));
        return setInt(c, place ? Definitions::number(*place, 0) : -1);
    });
    // 3421..3429 020f3138..020f33f8 {puzzle, ...}: its save record (ov7
    // 020c2c5c state; 020d54f0 / 020d5520 / 020d5500 wrong answers, read /
    // set / one more; 020d5560 / 020d5590 / 020d5570 hints opened; 020d55d0
    // bit 8 read, 020c30a8 set).
    auto record = [&](Call& c) -> GameData::Progress* {
        const int number = c.count() ? puzzleNumber(scene.assets, c.at(0)) : -1;
        return number > 0 ? &data.puzzles[number] : nullptr;
    };
    m.service(3421, [&](Call& c) {
        const int number = c.count() > 1 ? puzzleNumber(scene.assets, c.at(0)) : -1;
        return number > 0 &&
               setInt(c, setPuzzleState(scene.assets, data, number, c.integer(1)) ? 1 : 0);
    });
    m.service(3422, [&, record](Call& c) {
        auto* r = record(c);
        return r && setInt(c, r->failures);
    });
    m.service(3423, [&, record](Call& c) {
        auto* r = record(c);
        if (!r || c.count() < 2) {
            return false;
        }
        r->failures = int(c.at(1).toUint() & 0xff);
        return setInt(c, 1);
    });
    m.service(3424, [&, record](Call& c) {
        auto* r = record(c);
        if (!r) {
            return false;
        }
        r->failures = std::min(r->failures + 1, 0xff);
        return setInt(c, 1);
    });
    m.service(3425, [&, record](Call& c) {
        auto* r = record(c);
        return r && setInt(c, r->hints);
    });
    m.service(3426, [&, record](Call& c) {
        auto* r = record(c);
        if (!r || c.count() < 2) {
            return false;
        }
        r->hints = int(std::min(c.at(1).toUint(), 4u));
        return setInt(c, 1);
    });
    m.service(3427, [&, record](Call& c) {
        auto* r = record(c);
        if (!r) {
            return false;
        }
        r->hints = std::min(r->hints + 1, 4);
        return setInt(c, 1);
    });
    m.service(3428, [&, record](Call& c) {
        auto* r = record(c);
        return r && setInt(c, r->flag100);
    });
    m.service(3429, [&, record](Call& c) {
        auto* r = record(c);
        if (!r || c.count() < 2) {
            return false;
        }
        r->flag100 = c.at(1).truthy();
        return setInt(c, 1);
    });
    // 3430 020f3464 {puzzle}: marked as a favourite (save record bit 9);
    // 3431 020f34b0 {puzzle, on}: marks it.
    m.service(3430, [&](Call& c) {
        const int number = c.count() ? puzzleNumber(scene.assets, c.at(0)) : -1;
        const auto it = data.puzzles.find(number);
        return setInt(c, it != data.puzzles.end() && it->second.favourite);
    });
    m.service(3431, [&](Call& c) {
        const int number = c.count() > 1 ? puzzleNumber(scene.assets, c.at(0)) : -1;
        if (number <= 0) {
            return setInt(c, 0);
        }
        data.puzzles[number].favourite = c.at(1).truthy();
        return setInt(c, 1);
    });
    // 3440 / 3441 / 3442 020f351c.. {n}: the index's lists (020c31d0, game
    // data +0x56f0 / +0x579a / +0x5844; n < 0: the count): the puzzles found,
    // the favourites, and the unsolved ones with the record's bit 8, as
    // indices in index order. (The game appends to them as puzzles are
    // found and rebuilds them sorted on loading: *the port keeps them
    // sorted*.)
    auto puzzleList = [&](Call& c, int which) {
        if (c.count() == 0) {
            return false;
        }
        std::vector<int> list;
        for (const auto& [number, progress] : data.puzzles) {
            const bool in = which == 0   ? progress.state != 0
                            : which == 1 ? progress.favourite
                                         : progress.state != 2 && progress.flag100;
            if (in) {
                const int index = findPuzzle(scene.assets, number).index;
                if (index >= 0) {
                    list.push_back(index);
                }
            }
        }
        std::sort(list.begin(), list.end());
        const int n = c.integer(0);
        if (n < 0) {
            return setInt(c, int(list.size()));
        }
        return setInt(c, std::size_t(n) < list.size() ? list[std::size_t(n)] : -1);
    };
    for (int k = 0; k < 3; ++k) {
        m.service(3440 + k, [&, k, puzzleList](Call& c) { return puzzleList(c, k); });
    }
    // 3443 020f3600 {n}: the Wi-Fi puzzles received (game +0x5214 +0x430
    // count, entries at +0x194): none can be received any more.
    m.service(3443, [&](Call& c) { return setInt(c, c.count() && c.integer(0) < 0 ? 0 : -1); });
    // 3444 020f365c {n}: a second Wi-Fi list (game +0x5214, count at
    // +0x232); 3450..3455: the received puzzles' entries (020d5a78, three
    // bytes each). Nothing is received any more: empty lists.
    m.service(3444, [&](Call& c) { return setInt(c, c.count() && c.integer(0) < 0 ? 0 : -1); });
    for (int k = 3450; k <= 3455; ++k) {
        m.service(k, [&](Call& c) { return setInt(c, -1); });
    }

    m.service(3420, [&](Call& c) { // 020f30ec {puzzle}: its state (0 new, 1 seen, 2 solved)
        if (c.count() == 0) {
            return false;
        }
        const auto it = data.puzzles.find(puzzleNumber(scene.assets, c.at(0)));
        return setInt(c, it == data.puzzles.end() ? 0 : it->second.state);
    });
    m.service(3910, [&](Call& c) { // 020f3e78 {place}: the story moves there (020c092c)
        if (c.count() == 0) {
            return false;
        }
        return setInt(c, setPlace(scene.assets, data, c.at(0)) ? 1 : 0);
    });
    m.service(3010, [&](Call& c) { // 020f1784 {i}: a name of the player's (save +0x194)
        if (c.count() == 0) {
            return false;
        }
        const auto it = data.names.find(c.integer(0));
        c.result = Value::ofText(it == data.names.end() ? std::string() : it->second);
        return true;
    });
    m.service(3011, [&](Call& c) { // 020f17cc {i, text}
        if (c.count() < 2) {
            return false;
        }
        data.names[c.integer(0)] = textOr(c, 1);
        return setInt(c, 1);
    });
    m.service(3020, [&, flagOf](Call& c) { // 020f1828
        if (c.count() == 0) {
            return false;
        }
        const int index = c.count() > 1 ? c.integer(1) : 0;
        return setInt(c, flagOf(c, index) ? 1 : 0);
    });
    auto setFlag = [&, setFlagOf](Call& c, int value) {
        const int index = c.count() > 1 && value >= 0 ? c.integer(1) : 0;
        setFlagOf(c, index, value != 0);
        return setInt(c, 1);
    };
    m.service(3021, [&, setFlagOf](Call& c) { // 020f1884: {name, [index], value}
        if (c.count() < 2) {
            return false;
        }
        const std::size_t vi = c.count() < 3 ? 1 : 2;
        const int index = c.count() < 3 ? 0 : c.integer(1);
        setFlagOf(c, index, c.at(vi).truthy());
        return setInt(c, 1);
    });
    m.service(3022, [&, setFlag](Call& c) { return c.count() && setFlag(c, 1); }); // 020f1908
    m.service(3023, [&, setFlag](Call& c) { return c.count() && setFlag(c, 0); }); // 020f1978
    m.service(3030, [&, variableOf](Call& c) {                                     // 020f19d8
        if (c.count() == 0) {
            return false;
        }
        const int index = c.count() > 1 ? c.integer(1) : 0;
        return setInt(c, variableOf(c, index));
    });
    m.service(3031, [&, setVariableOf](Call& c) { // 020f1a34: {name, [index], value}
        if (c.count() < 2) {
            return false;
        }
        const int index = c.count() < 3 ? 0 : c.integer(1);
        const std::size_t vi = c.count() < 3 ? 1 : 2;
        setVariableOf(c, index, c.integer(vi));
        return setInt(c, 1);
    });
    m.service(3032, [&, variableOf, setVariableOf](Call& c) { // 020f1ab8: +1
        if (c.count() == 0) {
            return false;
        }
        const int index = c.count() > 1 ? c.integer(1) : 0;
        setVariableOf(c, index, variableOf(c, index) + 1);
        return setInt(c, 1);
    });
    // The puzzle being played (record of res/ge/lt4_def 2301; 020c156c).
    m.service(3400, [&](Call& c) { return setInt(c, data.puzzle.number); }); // 020f2c24
    // 3401 020f2c60 {[puzzle]}: a puzzle's index (record +6; the current
    // one's without an operand).
    m.service(3401, [&](Call& c) {
        if (c.count() == 0) {
            return setInt(c, data.puzzle.index);
        }
        const int number = puzzleNumber(scene.assets, c.at(0));
        return setInt(c, number > 0 ? findPuzzle(scene.assets, number).index : -1);
    });
    // 3402..3407 {puzzle, ...}: a puzzle's record (020c156c finds it by
    // number or name; the current puzzle's when it is that one).
    auto puzzleOf = [&](const Call& c) {
        const int number = c.count() ? puzzleNumber(scene.assets, c.at(0)) : -1;
        return number == data.puzzle.number || number <= 0 ? data.puzzle
                                                           : findPuzzle(scene.assets, number);
    };
    m.service(3402, [&, puzzleOf](Call& c) { // 020f2c9c: the title (record +0xc)
        c.result = Value::ofText(puzzleOf(c).title);
        return true;
    });
    m.service(3403, [&, puzzleOf](Call& c) { // 020f2cdc {puzzle, n}: picarats after n wrong answers
        if (c.count() < 2) {
            return false;
        }
        const int n = std::clamp(c.integer(1), 0, 2);
        return setInt(c, puzzleOf(c).picarats[std::size_t(n)]);
    });
    m.service(3404, [&, puzzleOf](Call& c) { return setInt(c, puzzleOf(c).type); }); // 020f2d70
    // 3406/3407 020f2df0/020f2e7c: the puzzle's pictures (ov7 020b6d10,
    // 020b6d48: "nazo/@/bga/n%03da.ci" when language-specific).
    auto picture = [&, puzzleOf](Call& c, const char* folder, char suffix, int which) {
        const GameData::Puzzle p = puzzleOf(c);
        char path[48];
        std::snprintf(path, sizeof path, "nazo/%s%s/n%03d%c.ci",
                      p.localised[std::size_t(which)] ? "@/" : "", folder, p.number, suffix);
        c.result = Value::ofText(path);
        return true;
    };
    m.service(3406, [&, picture](Call& c) { return c.count() && picture(c, "bga", 'a', 0); });
    m.service(3407, [&, picture](Call& c) { return c.count() && picture(c, "bgb", 'b', 1); });
    // 3456 020f398c: a count kept at game +0x5214+0x232 (only read with
    // the Wi-Fi dictionary; hypothesis: downloaded puzzles).
    m.service(3456, [&](Call& c) {
        c.result = Value::ofUint(0);
        return true;
    });
    m.service(3905, [&](Call& c) { return setInt(c, 0); }); // 020f3e30: language (game +0x5b74)
    // The current puzzle's save record (020c3674 etc.).
    m.service(3920, [&](Call& c) { // 020f3f48
        return setInt(c, data.puzzles[data.puzzle.number].state);
    });
    m.service(3922, [&](Call& c) { // 020f3fb0
        return setInt(c, data.puzzles[data.puzzle.number].failures);
    });
    m.service(3926, [&](Call& c) { // 020f4068 (ov7 020d5590): the hints opened
        if (c.count() == 0) {
            return false;
        }
        data.puzzles[data.puzzle.number].hints = int(std::min(c.at(0).toUint(), 4u)); // 020c2fc0
        return setInt(c, 1);
    });
    m.service(3940, [&](Call& c) { // 020f41a0: a puzzle parameter (020c3610)
        if (c.count() == 0) {
            return false;
        }
        const int i = c.integer(0);
        c.result = i >= 0 && i < 12 ? data.parameters[std::size_t(i)] : Value{};
        return true;
    });

    // ------------------------------------------------- scopes, loading
    m.service(4001, [&](Call& c) { return setInt(c, 1); }); // 020f4210: ready
    m.service(4020, [&](Call& c) {                          // 020f441c: a new scope level
        ++scene.level;
        return setInt(c, 1);
    });
    m.service(4021, [&](Call& c) { // 020f4440: frees the level's things
        scene.freeLevel();
        return setInt(c, 1);
    });
    // 4010..4013 020f4234..020f436c {on, [screen]}: one switch of every
    // object, message and place, of one screen or all (ov7 020cdf0c,
    // 020ce12c, ..., 020ce54c).
    for (int k = 0; k < 4; ++k) {
        m.service(4010 + k, [&, k](Call& c) {
            if (c.count() == 0) {
                return false;
            }
            const bool on = c.at(0).truthy();
            const int screen = c.count() > 1 ? c.integer(1) : -1;
            const auto kind = Switches::Kind(k);
            for (Chara* o : scene.objects(screen >= 0 && screen < 2 ? screen : -1)) {
                o->setSwitch(kind, on);
            }
            for (Message* x : scene.messages(screen >= 0 && screen < 2 ? screen : -1)) {
                x->switches.set(kind, on);
            }
            return setInt(c, 1);
        });
    }
    m.service(4014, [&](Call& c) { // 020f43d4: saves every switch
        scene.pushSwitches();
        return setInt(c, 1);
    });
    m.service(4015, [&](Call& c) { // 020f43f8: restores them
        scene.popSwitches();
        return setInt(c, 1);
    });

    // ------------------------------------------------------ brightness
    m.service(4100, [&](Call& c) { // 020f4464
        if (c.count() == 0) {
            return false;
        }
        const int screen = c.integer(0);
        if (screen < 0 || screen > 1) {
            c.result = Value::ofFloat(0);
            return true;
        }
        c.result = Value::ofFloat(unfixed(scene.screens[std::size_t(screen)].brightness.value()));
        return true;
    });
    m.service(4101, [&](Call& c) { // 020f44d0
        if (c.count() < 2) {
            return false;
        }
        const int screen = c.integer(0);
        if (screen >= 0 && screen <= 1) {
            scene.screens[std::size_t(screen)].brightness.set(fixed(c.real(1)));
        }
        return setInt(c, 1);
    });
    m.service(4102, [&](Call& c) { // 020f4550 {screen, level, frames, [curve], [lock]}
        if (c.count() < 3) {
            return false;
        }
        const int screen = c.integer(0);
        const int curve = c.count() > 3 ? c.integer(3) : 0;
        const bool lock = c.count() > 4 && c.at(4).truthy();
        if (screen >= 0 && screen <= 1) {
            scene.screens[std::size_t(screen)].brightness.move(fixed(c.real(1)), fixed(c.real(2)),
                                                               curve, lock);
        }
        return setInt(c, 1);
    });
    m.service(4109, [&](Call& c) { // 020f46a4
        if (c.count() == 0) {
            return false;
        }
        const int screen = c.integer(0);
        return setInt(c, screen >= 0 && screen <= 1 &&
                             scene.screens[std::size_t(screen)].brightness.moving());
    });
    m.service(4131, [&](Call& c) { // 020f4928: the screen on top (0209ee30)
        if (c.count() == 0) {
            return false;
        }
        const int v = c.integer(0);
        if (v == 0 || v == 1) {
            scene.upper = v;
        }
        return setInt(c, v == 0 || v == 1);
    });
    m.service(4149, [&](Call& c) { // 020f4bac {screen}: the blend still changing (0209f420)
        const int screen = c.count() ? c.integer(0) : -1;
        return setInt(c, screen >= 0 && screen <= 1 &&
                             scene.screens[std::size_t(screen)].blend.moving());
    });
    m.service(4141, [&](Call& c) { // 020f49d8 {screen, blend} (0209f480)
        if (c.count() < 2) {
            return false;
        }
        const int screen = c.integer(0);
        if (screen < 0 || screen > 1) {
            return setInt(c, 0);
        }
        scene.screens[std::size_t(screen)].blend.set(fixed(c.real(1)));
        return setInt(c, 1);
    });
    m.service(4142, [&](Call& c) { // 020f4a58 {screen, blend, frames, [curve], [lock]}
        if (c.count() < 3) {
            return false;
        }
        const int screen = c.integer(0);
        if (screen < 0 || screen > 1) {
            return setInt(c, 0);
        }
        const int curve = c.count() > 3 ? c.integer(3) : 0;
        const bool lock = c.count() > 4 && c.at(4).truthy();
        scene.screens[std::size_t(screen)].blend.move(fixed(c.real(1)), fixed(c.real(2)), curve,
                                                      lock);
        return setInt(c, 1);
    });
    m.service(4112, [&](Call& c) { // 020f4740 {level, frames, [curve], [lock]}
        if (c.count() < 2) {
            return false;
        }
        const int curve = c.count() > 2 ? c.integer(2) : 0;
        const bool lock = c.count() > 3 && c.at(3).truthy();
        for (auto& screen : scene.screens) {
            screen.brightness.move(fixed(c.real(0)), fixed(c.real(1)), curve, lock);
        }
        return setInt(c, 1);
    });
    m.service(4119, [&](Call& c) { // 020f486c
        return setInt(c,
                      scene.screens[0].brightness.moving() || scene.screens[1].brightness.moving());
    });
    // 4121 020f48bc sets a value 4120 020f4898 reads back (game +0xfc
    // +0x584; *role not identified*).
    m.service(4120, [&](Call& c) { return setInt(c, data.setting4121); });
    m.service(4121, [&](Call& c) {
        if (!c.count()) {
            return false;
        }
        data.setting4121 = c.integer(0);
        return setInt(c, 1);
    });
    // 4241 020f56a0 {screen, x, y}: the shake; 4242 020f5760 {screen, x,
    // y, frames}: moves it (curve 0).
    m.service(4241, [&](Call& c) {
        if (c.count() < 3 || c.integer(0) < 0 || c.integer(0) > 1) {
            return setInt(c, 0);
        }
        scene.screens[std::size_t(c.integer(0))].shake.set({fixed(c.real(1)), fixed(c.real(2))});
        return setInt(c, 1);
    });
    m.service(4242, [&](Call& c) {
        if (c.count() < 4 || c.integer(0) < 0 || c.integer(0) > 1) {
            return setInt(c, 0);
        }
        scene.screens[std::size_t(c.integer(0))].shake.move({fixed(c.real(1)), fixed(c.real(2))},
                                                            fixed(c.real(3)), 0);
        return setInt(c, 1);
    });
    // 4252/4253 020f590c/020f5980 {screen, a, b}: puts background layer a
    // before / after layer b in the screen's layer order (gnCScreen
    // 0209a58c / 0209a6e4; the order sets the hardware priorities).
    // Not modelled yet: the port draws its layers in a fixed order.
    m.service(4252, [&](Call& c) { return c.count() >= 3 && setInt(c, 1); });
    m.service(4253, [&](Call& c) { return c.count() >= 3 && setInt(c, 1); });
    m.service(4200, [&](Call& c) { // 020f4bf0: a screen's background picture
        if (c.count() < 2) {
            return false;
        }
        const int screen = c.integer(0);
        const std::string& path = textOr(c, 1);
        if (screen < 0 || screen > 1) {
            return setInt(c, 0);
        }
        // 020cee30: the screen is reset first (screen 0 loses its room).
        if (screen == 0) {
            scene.resetRoom();
        } else {
            scene.resetArea();
        }
        if (!scene.assets.exists(path)) {
            return setInt(c, 0);
        }
        scene.screens[std::size_t(screen)].background = &scene.assets.picture(path);
        return setInt(c, 1);
    });
    // 020f4c4c {screen, r, g, b} (020ced60): the screen becomes a plain
    // colour - its background picture goes (020cefe8 / 020cfd64 reset the
    // screen first, as 4200 does) and the backdrop takes the colour.
    m.service(4201, [&](Call& c) {
        if (c.count() < 4) {
            return false;
        }
        auto channel = [&](std::size_t i) {
            const float v = std::clamp(c.real(i), 0.0f, 1.0f);
            return unsigned(fixed(v * 31.99998f) >> 12) & 31;
        };
        const int screen = c.integer(0);
        if (screen >= 0 && screen <= 1) {
            if (screen == 0) {
                scene.resetRoom();
            } else {
                scene.resetArea();
            }
            scene.screens[std::size_t(screen)].background = nullptr;
            scene.screens[std::size_t(screen)].backdrop =
                std::uint16_t(channel(1) | channel(2) << 5 | channel(3) << 10);
        }
        return setInt(c, 1);
    });

    // ---------------------------------------------- colour treatment
    // lt4_def 2101 {id, name, ?, frames, Y gain, I gain, Q gain, target r,
    // g, b, blend} (in RAM +8 frames, +0xa gains, +0x10 target, +0x16
    // blend; 020bd8d0 by name or number).
    auto applyColour = [&](int screen, const Value& key) {
        const auto* r = definitions(scene.assets).find(2101, key.isText() ? 1 : 0, key);
        if (!r || screen < 0 || screen > 1) {
            return false;
        }
        auto f = [&](std::size_t i) {
            const Value& v = (*r)[i];
            return v.kind == Value::Kind::Float ? fixed(v.toFloat()) : v.toInt() * kOne;
        };
        const ColourMatrix cm = ColourMatrix::of({f(4), f(5), f(6)}, {f(7), f(8), f(9)}, f(10));
        scene.screens[std::size_t(screen)].moveColour(cm, Definitions::number(*r, 3) * kOne);
        return true;
    };
    // The same for an animation pack (020f7690..020f7d6c).
    auto packColour = [&](AnimationResource& a, const ColourMatrix& to, std::int32_t frames) {
        a.colourFrom = a.colour;
        a.colourTo = to;
        a.colourTime = 0;
        a.colourFrames = frames;
        if (frames <= 0) {
            a.colour = a.colourFrom = to;
        }
    };
    m.service(4730, [&](Call& c) { // 020f7690 {pack}: still changing
        AnimationResource* a = c.count() ? scene.animation(textOr(c, 0)) : nullptr;
        return a && setInt(c, a->colourTime < a->colourFrames);
    });
    m.service(4731, [&, packColour](Call& c) { // 020f76f4 {pack, [frames]}: back to none
        AnimationResource* a = c.count() ? scene.animation(textOr(c, 0)) : nullptr;
        if (!a) {
            return false;
        }
        packColour(*a, {}, c.count() > 1 ? fixed(c.real(1)) : 0);
        return setInt(c, 1);
    });
    m.service(4732, [&, packColour](Call& c) { // 020f7d6c {pack, record}
        AnimationResource* a = c.count() > 1 ? scene.animation(textOr(c, 0)) : nullptr;
        if (!a) {
            return false;
        }
        const auto* r = definitions(scene.assets).find(2101, c.at(1).isText() ? 1 : 0, c.at(1));
        if (!r) {
            return setInt(c, 0);
        }
        auto f = [&](std::size_t i) {
            const Value& v = (*r)[i];
            return v.kind == Value::Kind::Float ? fixed(v.toFloat()) : v.toInt() * kOne;
        };
        packColour(*a, ColourMatrix::of({f(4), f(5), f(6)}, {f(7), f(8), f(9)}, f(10)),
                   Definitions::number(*r, 3) * kOne);
        return setInt(c, 1);
    });
    m.service(4230, [&](Call& c) { // 020f4ed8 {screen}: still changing (0209ed14)
        const int screen = c.count() ? c.integer(0) : -1;
        return c.count() && setInt(c, screen >= 0 && screen < 2 &&
                                          scene.screens[std::size_t(screen)].colourMoving());
    });
    m.service(4231, [&](Call& c) { // 020f4f1c {screen, [frames]}: back to none
        if (c.count() == 0) {
            return false;
        }
        const int screen = c.integer(0);
        if (screen < 0 || screen > 1) {
            return setInt(c, 0);
        }
        auto& sc = scene.screens[std::size_t(screen)];
        if (c.count() > 1) {
            sc.moveColour({}, fixed(c.real(1))); // 0209ed90
        } else {
            sc.setColour({}); // 0209ed60
        }
        return setInt(c, 1);
    });
    m.service(4232, [&, applyColour](Call& c) { // 020f5590 {screen, record}
        if (c.count() < 2) {
            return false;
        }
        return setInt(c, applyColour(c.integer(0), c.at(1)) ? 1 : 0);
    });

    // ------------------------------------------------- room, place, area
    // Rooms (lt4_def 2131, in RAM +4 id, +6 map, +8 variant, +9 has a pack
    // and script, +0xa upper picture, +0xc a lighting record's name hash;
    // ov7 020bf0fc by number or name), places (2151: +4 id, +6 sound set,
    // +8 room, +0xa area, +0xc the room's name as a place, +0x10 German
    // name; 020c05bc) and areas (2141: id, name; 020bfb78).
    const Definitions& defs = definitions(scene.assets);
    auto rowOf = [&defs](int opcode, const Value& key) {
        return defs.find(opcode, key.isText() ? 1 : 0, key);
    };
    auto text = [](Call& c, const std::string& t) {
        c.result = Value::ofText(t);
        return true;
    };
    m.service(3200, [&, rowOf](Call& c) { // 020f243c {room}
        const auto* r = c.count() ? rowOf(2131, c.at(0)) : nullptr;
        return c.count() && setInt(c, r ? Definitions::number(*r, 0) : -1);
    });
    m.service(3201, [&, rowOf, text](Call& c) { // 020f2478: the room's picture (020b6a64)
        const auto* r = c.count() ? rowOf(2131, c.at(0)) : nullptr;
        if (!r) {
            return false;
        }
        char path[24];
        const std::string v = Definitions::text(*r, 3);
        std::snprintf(path, sizeof path, "map/m%04d%c.ci", Definitions::number(*r, 2),
                      v.empty() ? 'a' : v[0]);
        return text(c, path);
    });
    m.service(3202, [&, rowOf, text](Call& c) { // 020f2504: its pack (020b6a8c), none: ""
        const auto* r = c.count() ? rowOf(2131, c.at(0)) : nullptr;
        if (!r) {
            return false;
        }
        if (!Definitions::number(*r, 4)) {
            return text(c, "");
        }
        char path[24];
        const std::string v = Definitions::text(*r, 3);
        std::snprintf(path, sizeof path, "map/m%04d%c.ca", Definitions::number(*r, 2),
                      v.empty() ? 'a' : v[0]);
        return text(c, path);
    });
    m.service(3203, [&, rowOf](Call& c) { // 020f2590: its lighting record (020bd7c8; -1 none)
        const auto* r = c.count() ? rowOf(2131, c.at(0)) : nullptr;
        // (*Lighting records not identified*: the field is empty for the
        // rooms seen.)
        return r && setInt(c, -1);
    });
    m.service(3204, [&, rowOf, text](Call& c) { // 020f25e4: its upper picture (020b6ac8)
        const auto* r = c.count() ? rowOf(2131, c.at(0)) : nullptr;
        if (!r) {
            return false;
        }
        const int n = Definitions::number(*r, 6);
        if (n < 0) {
            return text(c, "");
        }
        char path[24];
        std::snprintf(path, sizeof path, "img/i%04d.ci", n);
        return text(c, path);
    });
    m.service(3210, [&, rowOf](Call& c) { // 020f2670 {area}
        const auto* r = c.count() ? rowOf(2141, c.at(0)) : nullptr;
        return c.count() && setInt(c, r ? Definitions::number(*r, 0) : -1);
    });
    m.service(3211, [&](Call& c) { // 020f26ac {area, place, -> x, y}: *not reconstructed*
        if (c.count() < 4) {
            return false;
        }
        store(c, 2, Value{});
        store(c, 3, Value{});
        return setInt(c, 0);
    });
    auto place = [&, rowOf](Call& c, std::size_t field) {
        const auto* r = c.count() ? rowOf(2151, c.at(0)) : nullptr;
        if (!r) {
            return false;
        }
        return setInt(c, Definitions::number(*r, field));
    };
    m.service(3220, [&, rowOf](Call& c) { // 020f2770 {place}
        const auto* r = c.count() ? rowOf(2151, c.at(0)) : nullptr;
        return c.count() && setInt(c, r ? Definitions::number(*r, 0) : -1);
    });
    m.service(3221, [&, rowOf](Call& c) { // 020f27ac: its room
        const auto* r = c.count() ? rowOf(2151, c.at(0)) : nullptr;
        const auto* room =
            r ? defs.find(2131, 1, Value::ofText(Definitions::text(*r, 4))) : nullptr;
        return r && setInt(c, room ? Definitions::number(*room, 0) : -1);
    });
    m.service(3222, [&, rowOf](Call& c) { // 020f27ec: its area
        const auto* r = c.count() ? rowOf(2151, c.at(0)) : nullptr;
        const auto* area =
            r ? defs.find(2141, 1, Value::ofText(Definitions::text(*r, 5))) : nullptr;
        return r && setInt(c, area ? Definitions::number(*area, 0) : -1);
    });
    m.service(3223, [&, place](Call& c) { return place(c, 3); }); // 020f282c: its sound set
    m.service(3224, [&, rowOf](Call& c) { // 020f286c: the place its room is named after
        const auto* r = c.count() ? rowOf(2151, c.at(0)) : nullptr;
        const auto* same =
            r ? defs.find(2151, 1, Value::ofText(Definitions::text(*r, 4))) : nullptr;
        return r && setInt(c, same ? Definitions::number(*same, 0) : -1);
    });
    m.service(3225, [&](Call& c) { // 020f28c0 {?, -> a, b, c}: the map route (*not reconstructed*)
        if (c.count() < 4) {
            return false;
        }
        for (std::size_t i = 1; i < 4; ++i) {
            store(c, i, Value{});
        }
        return setInt(c, 0);
    });
    m.service(3226, [&, rowOf, text](Call& c) { // 020f2998: its German name
        const auto* r = c.count() ? rowOf(2151, c.at(0)) : nullptr;
        return r && text(c, Definitions::text(*r, 2));
    });
    // 3911..3913 (020bf1e0, 020bfc5c, 020bde40): the story's room, area and
    // sound set (variables named in ov7).
    auto setStory = [&, rowOf](Call& c, std::uint32_t pool, int opcode) {
        if (c.count() == 0) {
            return false;
        }
        constexpr std::uint32_t kOverlay7 = 0x020b6820;
        const std::string name =
            scene.assets.codeText(7, kOverlay7, scene.assets.codeWord(7, kOverlay7, pool));
        int v = -1;
        if (opcode == 0) {
            v = c.integer(0);
        } else if (const auto* r = rowOf(opcode, c.at(0))) {
            v = Definitions::number(*r, 0);
        }
        data.setVariable(name, v);
        return setInt(c, v >= 0 ? 1 : 0);
    };
    m.service(3911, [&, setStory](Call& c) { return setStory(c, 0x020bf1a4, 2131); });
    m.service(3912, [&, setStory](Call& c) { return setStory(c, 0x020bfc20, 2141); });
    m.service(3913, [&, setStory](Call& c) { return setStory(c, 0x020c08d4, 0); });
    m.service(3501, [&, text](Call& c) { // 020f3c18 {step}: its goal text (record +0xc)
        if (c.count() == 0) {
            return false;
        }
        for (const Step& st : steps(scene.assets)) {
            if (c.at(0).isText() ? st.name == c.at(0).text : st.id == c.integer(0)) {
                return text(c, st.goal);
            }
        }
        return false;
    });
    m.service(2300, [&](Call& c) { return setInt(c, s.loop && s.loop->holding()); }); // 020f1208
    m.service(4400, [&, rowOf](Call& c) { // 020f6584 {area} (020cfe54)
        if (c.count() == 0) {
            return false;
        }
        const auto* r = rowOf(2141, c.at(0));
        if (!r) {
            scene.resetArea();
            scene.screens[1].background = nullptr;
            return setInt(c, 0);
        }
        const int id = Definitions::number(*r, 0);
        if (id == scene.areaShown) {
            return setInt(c, 1);
        }
        scene.resetArea();
        // The pack and the text boxes outlive the loops (level 0); the
        // picture is the area's own (020b6b4c; the variants of 2143 by
        // story progress are *not reconstructed*).
        const int saved = scene.level;
        scene.level = 0;
        scene.loadAnimation("lt4_gmp", 1, "ani/@/icn_gmp.ca");
        char path[24];
        std::snprintf(path, sizeof path, "gmp/g%02d.ci", id);
        scene.screens[1].background =
            scene.assets.exists(path) ? &scene.assets.picture(path) : nullptr;
        for (const char* name : {"lt4_plc", "lt4_aim"}) {
            if (Message* x = scene.createMessage(name, 1, 256, 0)) {
                x->textColour = 1;
                x->edgeColour = 9;
                x->flags = 3;
                x->letterSpacing = 1;
                x->lineSpacing = 1;
                x->rubyOffset = -1;
                const bool plc = name[4] == 'p';
                x->position.set({(plc ? 182 : 128) * kOne, (plc ? 12 : 182) * kOne});
            }
        }
        scene.level = saved;
        scene.areaShown = id;
        return setInt(c, 1);
    });
    m.service(4811, [&](Call& c) { // 020f81ec {texts}: loaded (the port loads at once)
        return c.count() && setInt(c, 1);
    });
    // 3461 020f39e0 {n}: the puzzles the story has passed by since the last
    // notice (020c33d0 / 020c33f8; filled by 020bc0b8): n < 0 how many,
    // else the n-th's puzzle number (its row of the index order looked up
    // by name, 020c1a7c / 020c1390), -1 none. 3460 020f39b4: forgets them
    // (020c3350).
    m.service(3461, [&](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const int n = c.integer(0);
        if (n < 0) {
            return setInt(c, int(data.passedBy.size()));
        }
        if (std::size_t(n) >= data.passedBy.size()) {
            return setInt(c, -1);
        }
        const auto& rows = definitions(scene.assets).rows(2311);
        const int row = data.passedBy[std::size_t(n)];
        if (row < 0 || std::size_t(row) >= rows.size()) {
            return setInt(c, -1);
        }
        const int number =
            puzzleNumber(scene.assets, Value::ofText(Definitions::text(rows[std::size_t(row)], 0)));
        return setInt(c, number > 0 ? number : -1);
    });
    m.service(3460, [&](Call& c) {
        data.passedBy.clear();
        return setInt(c, 1);
    });
    m.service(4411, [&](Call& c) { return setInt(c, scene.areaShown); }); // 020f65e0
    m.service(4502, [&](Call& c) { // 020f65fc {script}: loads it in the current level
        if (c.count() == 0 || !scene.assets.exists(textOr(c, 0))) {
            return c.count() && setInt(c, 0);
        }
        scene.loadScript(textOr(c, 0));
        return setInt(c, 1);
    });
    m.service(3600, [&](Call& c) { // 020f3cb4 {sound set}: its music (020bdda8, record +2)
        if (c.count() == 0) {
            return false;
        }
        const auto* r = definitions(scene.assets).find(2111, 0, c.at(0));
        return setInt(c, r ? Definitions::number(*r, 1) : -1);
    });
    m.service(4721, [&](Call& c) { // 020f7614 {animation, on}: its flag +0x138 bit 2
        return c.count() >= 2 && setInt(c, scene.animation(textOr(c, 0)) ? 1 : 0);
    });
    m.service(5121, [&](Call& c) { // 020f9dc4 {object, screen, [priority]} (020d1a7c)
        if (c.count() < 2) {
            return false;
        }
        Chara* o = scene.find(textOr(c, 0));
        const int screen = c.integer(1);
        if (!o || screen < 0 || screen > 1) {
            return setInt(c, 0);
        }
        o->visual = Chara::Visual::Screen; // 020ac55c
        scene.place(*o, screen, c.count() > 2 ? c.integer(2) : -1);
        return setInt(c, 1);
    });
    // 4330..4332: the lower screen's colour treatment (the room's): 4330
    // still changing, 4331 none, 4332 the room's lighting record (2131
    // field 5, a 2101 record by name).
    m.service(4330, [&](Call& c) { return setInt(c, scene.screens[0].colourMoving()); });
    m.service(4331, [&](Call& c) { // 020f5e88
        scene.screens[0].setColour({});
        return setInt(c, 1);
    });
    m.service(4332, [&, applyColour](Call& c) { // 020f64d4
        const auto* room = defs.find(2131, 0, Value::ofInt(scene.room.shown));
        const std::string light = room ? Definitions::text(*room, 5) : std::string();
        return setInt(c, !light.empty() && applyColour(0, Value::ofText(light)));
    });

    // ---------------------------------------------------------- rooms
    m.service(4300, [&](Call& c) { // 020f5b38 {room} (020cf940)
        if (c.count() == 0) {
            return false;
        }
        const Definitions& defs = definitions(scene.assets);
        const auto* row = defs.find(2131, c.at(0).isText() ? 1 : 0, c.at(0));
        if (!row) {
            // No room: screen 0 turns a plain colour (020ced60).
            scene.resetRoom();
            scene.screens[0].background = nullptr;
            return setInt(c, 0);
        }
        // 2131 {id, name, map, variant, script and pack, ...}.
        const std::string variant = Definitions::text(*row, 3);
        return setInt(c, scene.showRoom(Definitions::number(*row, 0), Definitions::number(*row, 2),
                                        variant.empty() ? 'a' : variant[0],
                                        Definitions::number(*row, 4) != 0)
                             ? 1
                             : 0);
    });
    m.service(4310, [&](Call& c) { return setInt(c, scene.room.ready ? 1 : 0); }); // 020f5b78
    m.service(4311, [&](Call& c) { return setInt(c, scene.room.shown); });         // 020f5b94
    m.service(4320, [&](Call& c) { // 020f5bb0 {part}: shown (020cfad0)
        if (c.count() == 0) {
            return false;
        }
        const auto* p = scene.roomPart(textOr(c, 0));
        return setInt(c, p && !p->hidden && !p->held ? 1 : 0);
    });
    m.service(4321, [&](Call& c) { // 020f5bf0 {part, on, [keep]} (020cfb78, 020de3ec)
        if (c.count() < 2) {
            return false;
        }
        auto* p = scene.roomPart(textOr(c, 0));
        if (!p) {
            return setInt(c, 0);
        }
        const bool on = c.at(1).truthy(), keep = c.count() > 2 && c.at(2).truthy();
        if (on) {
            if (p->hidden || p->held) {
                p->hidden = p->held = false;
            }
            // Shown again, the sequence starts over.
            p->player.play(p->player.sequence(), AnimationPlayer::Restart);
        } else if (keep) {
            p->held = true;
        } else {
            p->hidden = true;
            p->held = false;
        }
        return setInt(c, 1);
    });
    m.service(4322, [&](Call& c) { // 020f5c90 {part, alpha} (020de470)
        if (c.count() < 2) {
            return false;
        }
        auto* p = scene.roomPart(textOr(c, 0));
        if (p) {
            p->alpha.set(fixed(c.real(1)));
        }
        return setInt(c, p ? 1 : 0);
    });
    m.service(4323, [&](Call& c) { // 020f5d0c {part, alpha, frames, [curve], [lock]} (020de490)
        if (c.count() < 3) {
            return false;
        }
        auto* p = scene.roomPart(textOr(c, 0));
        if (p) {
            const int curve = c.count() > 3 ? c.integer(3) : 0;
            const bool lock = c.count() > 4 && c.at(4).truthy();
            p->alpha.move(fixed(c.real(1)), fixed(c.real(2)), curve, lock);
        }
        return setInt(c, p ? 1 : 0);
    });

    // ---------------------------------------------------- resources
    m.service(4601,
              [&](Call& c) { return c.count() && setInt(c, scene.unloadImage(textOr(c, 0))); });
    m.service(4602, [&](Call& c) { // 020f6728 {name, screen, path}
        if (c.count() < 3) {
            return false;
        }
        return setInt(c, scene.loadImage(textOr(c, 0), c.integer(1), textOr(c, 2)) ? 1 : 0);
    });
    m.service(4604, [&](Call& c) {
        return c.count() && setInt(c, scene.image(textOr(c, 0)) != nullptr);
    });
    // 4611 020f68e8 (ov6 020a4d64): whether the picture is loaded (status
    // bit 1; the port loads at once).
    // 4210 020f4e74 (020ced2c), 4610 020f68c4, 4710 020f7508 (020d1544):
    // the screens', pictures' and animations' files are in (the port
    // loads at once).
    m.service(4210, [&](Call& c) { return setInt(c, 1); });
    m.service(4610, [&](Call& c) { return setInt(c, 1); });
    m.service(4710, [&](Call& c) { return setInt(c, 1); });
    // 4641 020f72ac {name, -> width, -> height}: a loaded picture's size
    // (picture header +0x2c / +0x2e); 0 when there is none.
    m.service(4641, [&](Call& c) {
        if (c.count() < 3) {
            return false;
        }
        const ImageResource* image = scene.image(textOr(c, 0));
        const Picture* p = image ? image->picture : nullptr;
        store(c, 1, Value::ofInt(p ? int(p->width) : 0));
        store(c, 2, Value::ofInt(p ? int(p->height) : 0));
        return setInt(c, p != nullptr);
    });
    m.service(4611, [&](Call& c) {
        return c.count() && setInt(c, scene.image(textOr(c, 0)) != nullptr);
    });
    // 4612 020f692c: whether a picture of that name exists (020d0e40).
    m.service(4612, [&](Call& c) {
        return c.count() && setInt(c, scene.image(textOr(c, 0)) != nullptr);
    });
    m.service(4701,
              [&](Call& c) { return c.count() && setInt(c, scene.unloadAnimation(textOr(c, 0))); });
    m.service(4702, [&](Call& c) { // 020f73b8 {name, screen, path}
        if (c.count() < 3) {
            return false;
        }
        return setInt(c, scene.loadAnimation(textOr(c, 0), c.integer(1), textOr(c, 2)) ? 1 : 0);
    });
    // 4711 020f752c (ov6 020a6830): whether the animation pack is loaded.
    m.service(4711, [&](Call& c) {
        return c.count() && setInt(c, scene.animation(textOr(c, 0)) != nullptr);
    });
    m.service(4761, [&](Call& c) { // 020f7f3c {animation, sequence, x, y} out
        if (c.count() < 4) {
            return false;
        }
        const AnimationResource* a = scene.animation(textOr(c, 0));
        const auto* seq = a ? a->animation->sequence(textOr(c, 1)) : nullptr;
        store(c, 2, Value::ofInt(seq ? seq->x : 0));
        store(c, 3, Value::ofInt(seq ? seq->y : 0));
        return seq && setInt(c, 1);
    });
    m.service(4801, [&](Call& c) { // 020f8050 {name}: the text list goes (020d0470)
        return c.count() && setInt(c, scene.removeTexts(textOr(c, 0)));
    });
    m.service(4802, [&](Call& c) { // 020f8094 {name, group, number}
        if (c.count() < 3) {
            return false;
        }
        return setInt(c, scene.loadTexts(textOr(c, 0), c.integer(1), c.integer(2)) ? 1 : 0);
    });
    m.service(4810, [&](Call& c) { return setInt(c, 1); }); // 020f81c8: texts ready
    m.service(4821, [&](Call& c) {                          // 020f82ec {name, id, index, field}
        if (c.count() < 4) {
            return false;
        }
        const std::string* t = scene.text(textOr(c, 0), c.integer(1), c.integer(2), c.integer(3));
        c.result = t ? Value::ofText(*t) : Value{};
        return t != nullptr;
    });

    // ---------------------------------------------------------- sound
    // ov12 020f8404..020f9058 onto the sound manager (Adventure4Sound).
    // Without one (headless runs) requests succeed and report silence.
    auto sound = [&](int op, auto&& f) {
        m.service(op, [&, op, f](Call& c) {
            if (s.sound) {
                s.sound(op, c);
            }
            return f(c, s.audio);
        });
    };
    auto frames = [](const Call& c, std::size_t i, int otherwise) {
        return c.count() > i ? c.integer(i) : otherwise;
    };
    auto flags = [](const Call& c, std::size_t i) {
        return c.count() > i ? c.unsignedInt(i) : 0u;
    };
    // 4900 020f8424 {set, [frames = 60], [flags]}: a sound set.
    sound(4900, [=](Call& c, Adventure4Sound* a) {
        if (c.count() == 0) {
            return false;
        }
        const bool done = a && a->selectSet(c.integer(0), frames(c, 1, 60), flags(c, 2));
        return setInt(c, done ? 1 : 0);
    });
    sound(4901, [](Call& c, Adventure4Sound* a) { return setInt(c, a ? a->set() : -1); });
    // 4910 020f84f8: every bank loaded (the object loads at once).
    sound(4910, [](Call& c, Adventure4Sound*) { return setInt(c, 1); });
    // 4911 020f851c {[frames]}: banks unloaded, music and stream faded out.
    sound(4911, [=](Call& c, Adventure4Sound* a) {
        if (a) {
            a->releaseAll(frames(c, 0, 0));
        }
        return setInt(c, 1);
    });
    sound(4912, [](Call& c, Adventure4Sound*) { return setInt(c, 0); }); // +0x9340
    sound(4913, [](Call& c, Adventure4Sound* a) { return setInt(c, a ? a->bank(1) : -1); });
    sound(4914, [](Call& c, Adventure4Sound* a) { return setInt(c, a ? a->bank(2) : -1); });
    sound(4920, [](Call& c, Adventure4Sound*) { return setInt(c, 1); }); // banks ready
    // 4921 {id}: the effect plays; 4922: any effect plays.
    sound(4921, [](Call& c, Adventure4Sound* a) {
        return c.count() && setInt(c, a && a->effectPlaying(c.integer(0)) ? 1 : 0);
    });
    sound(4922, [](Call& c, Adventure4Sound* a) {
        return setInt(c, a && a->anyEffectPlaying() ? 1 : 0);
    });
    // 4923 {group} unload, 4924 {group, bank} load.
    sound(4923, [](Call& c, Adventure4Sound* a) {
        return c.count() && setInt(c, a ? a->loadBank(c.integer(0), -1) : 1);
    });
    sound(4924, [](Call& c, Adventure4Sound* a) {
        return c.count() > 1 && setInt(c, a ? a->loadBank(c.integer(0), c.integer(1)) : 1);
    });
    // 4925 {id} play, 4926 {id, [frames]} stop, 4927 {frames} all stop.
    sound(4925, [](Call& c, Adventure4Sound* a) {
        if (!c.count()) {
            return false;
        }
        if (a) {
            a->playEffect(c.integer(0));
        }
        return setInt(c, 1);
    });
    sound(4926, [=](Call& c, Adventure4Sound* a) {
        if (!c.count()) {
            return false;
        }
        if (a) {
            a->stopEffect(c.integer(0), frames(c, 1, 0));
        }
        return setInt(c, 1);
    });
    sound(4927, [](Call& c, Adventure4Sound* a) {
        if (!c.count()) {
            return false;
        }
        if (a) {
            a->stopEffects(c.integer(0));
        }
        return setInt(c, 1);
    });
    sound(4930, [](Call& c, Adventure4Sound*) { return setInt(c, 0); }); // +0x9320 busy
    // 4931/4932 the music plays, 4933 its volume still changes.
    sound(4931, [](Call& c, Adventure4Sound* a) { return setInt(c, a && a->musicPlaying()); });
    sound(4932, [](Call& c, Adventure4Sound* a) { return setInt(c, a && a->musicPlaying()); });
    sound(4933, [](Call& c, Adventure4Sound* a) { return setInt(c, a && a->musicFading()); });
    // 4934 {[frames]} silence; 4935 {music, [frames], [flags]} select.
    sound(4934, [=](Call& c, Adventure4Sound* a) {
        return setInt(c, a ? a->selectMusic(0, frames(c, 0, 0), 0) : 1);
    });
    sound(4935, [=](Call& c, Adventure4Sound* a) {
        if (!c.count()) {
            return false;
        }
        return setInt(c, a ? a->selectMusic(c.integer(0), frames(c, 1, 0), flags(c, 2)) : 1);
    });
    // 4936 {[volume = 1], [frames], [flags]} start, 4937 {[frames], [flag]}
    // stop, 4938 {volume, [frames]} volume.
    sound(4936, [=](Call& c, Adventure4Sound* a) {
        if (a) {
            a->playMusic(c.count() ? c.real(0) : 1.0f, frames(c, 1, 0), flags(c, 2));
        }
        return setInt(c, 1);
    });
    sound(4937, [=](Call& c, Adventure4Sound* a) {
        if (a) {
            a->stopMusic(frames(c, 0, 0));
        }
        return setInt(c, 1);
    });
    sound(4938, [=](Call& c, Adventure4Sound* a) {
        if (!c.count()) {
            return false;
        }
        if (a) {
            a->musicVolume(c.real(0), frames(c, 1, 0));
        }
        return setInt(c, 1);
    });
    // Streams (020d9244..020d92e4 on the stream's load state): 4950 nothing
    // loading, 4951 still loading, 4952/4953 playing.
    sound(4950, [](Call& c, Adventure4Sound* a) { return setInt(c, !(a && a->streamLoading())); });
    sound(4951, [](Call& c, Adventure4Sound* a) { return setInt(c, a && a->streamLoading()); });
    sound(4952, [](Call& c, Adventure4Sound* a) { return setInt(c, a && a->streamPlaying()); });
    sound(4953, [](Call& c, Adventure4Sound* a) { return setInt(c, a && a->streamPlaying()); });
    // 4954 {[frames]} stop and close, 4955 close.
    sound(4954, [=](Call& c, Adventure4Sound* a) {
        if (a) {
            a->closeStream(frames(c, 0, 0));
        }
        return setInt(c, 1);
    });
    sound(4955, [](Call& c, Adventure4Sound* a) {
        if (a) {
            a->closeStream(0);
        }
        return setInt(c, 1);
    });
    // 4956 jingle, 4957 music stream, 4958 film sound {id}; 4959 event
    // voice {a, b, c}.
    for (int k = 0; k < 3; ++k) {
        sound(4956 + k, [k](Call& c, Adventure4Sound* a) {
            if (!c.count()) {
                return false;
            }
            if (a) {
                a->openStream(Adventure4Sound::Route(k), c.integer(0));
            }
            return setInt(c, 1);
        });
    }
    sound(4959, [](Call& c, Adventure4Sound* a) {
        if (c.count() < 3) {
            return false;
        }
        if (a) {
            a->openStream(Adventure4Sound::Event, c.integer(1), c.integer(0), c.integer(2));
        }
        return setInt(c, 1);
    });
    // 4960 {[volume = 1], [frames], [flags]} start, 4961 {[frames]} stop,
    // 4962 {volume, [frames]} volume.
    sound(4960, [=](Call& c, Adventure4Sound* a) {
        if (a) {
            a->startStream(c.count() ? c.real(0) : 1.0f, frames(c, 1, 0), flags(c, 2));
        }
        return setInt(c, 1);
    });
    sound(4961, [=](Call& c, Adventure4Sound* a) {
        if (a) {
            a->stopStream(frames(c, 0, 0));
        }
        return setInt(c, 1);
    });
    sound(4962, [=](Call& c, Adventure4Sound* a) {
        if (!c.count()) {
            return false;
        }
        if (a) {
            a->streamVolume(c.real(0), frames(c, 1, 0));
        }
        return setInt(c, 1);
    });

    // -------------------------------------------------------- objects
    auto object = [&](const Call& c) -> Chara* {
        return c.count() ? scene.find(textOr(c, 0)) : nullptr;
    };
    m.service(5001, [&](Call& c) { return c.count() && setInt(c, scene.remove(textOr(c, 0))); });
    m.service(5002, [&](Call& c) { // 020f913c {name, kind, values...}
        if (c.count() == 0) {
            return false;
        }
        // The object's values are the call's first 16 operands (name and
        // kind included; 01ffb534), none for a lone name.
        std::vector<Value> values;
        for (std::size_t i = 0; c.count() > 1 && i < c.count() && values.size() < 16; ++i) {
            values.push_back(c.at(i));
        }
        const int kind = c.count() > 1 ? c.integer(1) : -1;
        return setInt(c, scene.create(textOr(c, 0), kind, values) != nullptr);
    });
    m.service(5010, [&](Call& c) { // 020f9260: whether all the named objects exist
        for (std::size_t i = 0; i < c.count(); ++i) {
            if (!scene.find(textOr(c, i))) {
                return setInt(c, 0);
            }
        }
        return setInt(c, 1);
    });
    // 5011 020f92d0, 5012 020f934c, 5014 020f9444: one object's master,
    // touch and draw switch (vtable +0x10, +0x14, +0x1c).
    auto objectSwitch = [&, object](Call& c, Switches::Kind k) {
        Chara* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->setSwitch(k, c.at(1).truthy());
        return setInt(c, 1);
    };
    m.service(5011, [&, objectSwitch](Call& c) { return objectSwitch(c, Switches::Master); });
    m.service(5012, [&, objectSwitch](Call& c) { return objectSwitch(c, Switches::Touch); });
    m.service(5013, [&, objectSwitch](Call& c) { return objectSwitch(c, Switches::Update); });
    m.service(5014, [&, objectSwitch](Call& c) { return objectSwitch(c, Switches::Draw); });
    // 5181 020fb32c {object, x, y, sx, sy, w, h}: a box of the film's
    // texture drawn with the object (020aca3c).
    m.service(5181, [&, object](Call& c) {
        Chara* o = object(c);
        if (!o || c.count() < 7) {
            return false;
        }
        o->filmPart = {true,         c.integer(1), c.integer(2), c.integer(3),
                       c.integer(4), c.integer(5), c.integer(6)};
        return setInt(c, 1);
    });
    m.service(5120, [&, object](Call& c) { // 020f9d74: what the object shows (+0x264)
        Chara* o = object(c);
        return o && setInt(c, int(o->visual));
    });
    // The shake (+0x150): 5160 still changing (020ac1dc), 5161 set {x, y}
    // (020ac1a4), 5162 move {x, y, frames} (020ac1b4, curve 0).
    m.service(5160, [&, object](Call& c) {
        Chara* o = object(c);
        return o && setInt(c, o->switches.on(Switches::Master) && o->shake.moving());
    });
    m.service(5161, [&, object](Call& c) {
        Chara* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        o->shake.set({fixed(c.real(1)), fixed(c.real(2))});
        return setInt(c, 1);
    });
    m.service(5162, [&, object](Call& c) {
        Chara* o = object(c);
        if (!o || c.count() < 4) {
            return false;
        }
        o->shake.move({fixed(c.real(1)), fixed(c.real(2))}, fixed(c.real(3)), 0);
        return setInt(c, 1);
    });
    m.service(5101, [&, object](Call& c) { // 020f9528
        Chara* o = object(c);
        return o && setInt(c, o->enterState(0));
    });
    m.service(5103, [&, object](Call& c) { // 020f95f0: runs a block on the object's thread
        Chara* o = object(c);
        if (!o || c.count() < 2 || !o->program) {
            return false;
        }
        return setInt(c, o->thread.start(*o->program, textOr(c, 1)) ? 1 : 0);
    });
    // 5104 020f9660 {object, block, args...}: the block as a callback on
    // the object's thread (020db668; the interrupted block resumes after).
    m.service(5104, [&, object](Call& c) {
        Chara* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        std::vector<Value> args;
        for (std::size_t i = 2; i < c.count() && i < 10; ++i) {
            args.push_back(c.at(i));
        }
        return setInt(c, o->thread.callback(textOr(c, 1), args) ? 1 : 0);
    });
    m.service(5108, [&, object](Call& c) { // 020f972c: an object value
        Chara* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        const int i = c.integer(1);
        if (i < 0 || std::size_t(i) >= o->thread.values.size()) {
            c.result = {};
            return true;
        }
        c.result = o->thread.values[std::size_t(i)];
        return true;
    });
    m.service(5109, [&, object](Call& c) { // 020f97ac {name, index, value}
        Chara* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        const int i = c.integer(1);
        if (i < 0 || std::size_t(i) >= o->thread.values.size()) {
            return false;
        }
        o->thread.values[std::size_t(i)] = c.at(2);
        return setInt(c, 1);
    });
    // The button services name the blocks an object runs per touch state
    // and make it that kind of button (+0x348), then restart its normal
    // block (020db5ac).
    auto callbacks = [&, object](Call& c, int kind, std::initializer_list<int> slots) {
        Chara* o = object(c);
        if (!o) {
            return false;
        }
        o->touchKind = kind;
        std::size_t i = 1;
        for (int slot : slots) {
            o->callbacks[std::size_t(slot)] = i < c.count() ? textOr(c, i) : std::string();
            ++i;
        }
        return setInt(c, o->enterState(0) ? 1 : 0);
    };
    // 020db6b8: three blocks, not a button (slots 0, 1, 2).
    m.service(5111,
              [&, callbacks](Call& c) { return c.count() >= 2 && callbacks(c, 0, {0, 1, 2}); });
    // 020db710: normal, touch, then two more blocks (slots 0, 3, 1, 2).
    m.service(5112,
              [&, callbacks](Call& c) { return c.count() >= 3 && callbacks(c, 1, {0, 3, 1, 2}); });
    // 020db778: normal, select, click, then two more (slots 0, 4, 5, 1, 2).
    m.service(5113, [&, callbacks](Call& c) {
        return c.count() >= 4 && callbacks(c, 2, {0, 4, 5, 1, 2});
    });
    // 020db7f0: normal, slide, one more (slots 0, 1, 2).
    m.service(5114,
              [&, callbacks](Call& c) { return c.count() >= 2 && callbacks(c, 3, {0, 1, 2}); });
    m.service(5122, [&](Call& c) { // 020f9e70 {object, image, [priority]}
        if (c.count() < 2) {
            return false;
        }
        Chara* o = scene.find(textOr(c, 0));
        const ImageResource* image = scene.image(textOr(c, 1));
        if (!o || !image) {
            return setInt(c, 0);
        }
        o->visual = Chara::Visual::Image;
        o->image = image;
        scene.place(*o, image->screen, c.count() > 2 ? c.integer(2) : -1);
        return setInt(c, 1);
    });
    m.service(5123, [&](Call& c) { // 020f9f3c {object, animation, [priority]}
        if (c.count() < 2) {
            return false;
        }
        Chara* o = scene.find(textOr(c, 0));
        const AnimationResource* a = scene.animation(textOr(c, 1));
        if (!o || !a) {
            return setInt(c, 0);
        }
        o->visual = Chara::Visual::Animation;
        o->animation.attach(a);
        scene.place(*o, a->screen, c.count() > 2 ? c.integer(2) : -1);
        return setInt(c, 1);
    });
    m.service(5124, [&, object](Call& c) { // 020fa008 {object, [priority]}
        // 020d1ba4: on screen 0, empty (020ac9c4: kind 4).
        Chara* o = object(c);
        if (!o) {
            return setInt(c, 0);
        }
        o->visual = Chara::Visual::Empty;
        scene.place(*o, 0, c.count() > 1 ? c.integer(1) : -1);
        return setInt(c, 1);
    });
    m.service(5130, [&, object](Call& c) { // 020fa08c: the animation has played through
        Chara* o = object(c);
        return o && setInt(c, o->animation.done() ? 1 : 0);
    });
    m.service(5131, [&, object](Call& c) { // 020fa0e4: the sequence playing
        Chara* o = object(c);
        if (!o) {
            return false;
        }
        const auto* seq = o->animation.sequence();
        c.result = seq ? Value::ofText(seq->name) : Value{};
        return seq != nullptr;
    });
    m.service(5133, [&, object](Call& c) { // 020fa224 {object, sequence, [flags]}
        Chara* o = object(c);
        if (!o || c.count() < 2 || !o->animation.resource()) {
            return false;
        }
        const auto* seq = o->animation.resource()->animation->sequence(textOr(c, 1));
        return setInt(c, o->animation.play(seq, playFlags(c, 2)) ? 1 : 0);
    });
    m.service(5134, [&, object](Call& c) { // 020fa2c8 {object, a, b, [flags]}
        // 020ac790: the first sequence whose name starts "aa_bb_" (a pose
        // by two numbers), played as 5133 plays one.
        Chara* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        const int a = c.integer(1), b = c.integer(2);
        const Animation::Sequence* found = nullptr;
        if (o->animation.resource() && a >= 0 && b >= 0) {
            char prefix[16];
            std::snprintf(prefix, sizeof prefix, "%02d_%02d_", a, b);
            for (const auto& q : o->animation.resource()->animation->sequences) {
                if (q.name.rfind(prefix, 0) == 0) {
                    found = &q;
                    break;
                }
            }
        }
        return setInt(c, found && o->animation.play(found, playFlags(c, 3)) ? 1 : 0);
    });
    m.service(5138, [&, object](Call& c) { // 020fa558 {object, text, step, line, [flags]}
        Chara* o = object(c);
        if (!o || c.count() < 4) {
            return false;
        }
        return setInt(
            c, o->animation.showText(textOr(c, 1), c.integer(2), c.integer(3), playFlags(c, 4))
                   ? 1
                   : 0);
    });
    m.service(5141, [&, object](Call& c) { // 020fa714
        Chara* o = object(c);
        if (!o) {
            return false;
        }
        c.result = Value::ofFloat(unfixed(o->alpha.value()));
        return true;
    });
    m.service(5142, [&, object](Call& c) { // 020fa77c
        Chara* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->alpha.set(fixed(c.real(1)));
        return setInt(c, 1);
    });
    // 59x0..59x4: every object (5900..), of a kind (5910..) or of the cast
    // (kind 13, 5920..): whether an alpha still changes (020d1c8c,
    // 020aba24), set it, fade it, pulse it (020ab930), delete them (ov7
    // 020dcf14).
    auto forKind = [&](int kind, auto fn) {
        for (Chara* o : scene.objects()) {
            if (kind < 0 || o->kind == kind) {
                fn(*o);
            }
        }
    };
    auto fading = [&, forKind](int kind) {
        bool any = false;
        forKind(kind, [&](Chara& o) {
            any = any || (o.switches.on(Switches::Master) && o.alpha.moving());
        });
        return any;
    };
    m.service(5900, [&, fading](Call& c) { return setInt(c, fading(-1)); });
    m.service(5910, [&, fading](Call& c) { return c.count() && setInt(c, fading(c.integer(0))); });
    m.service(5920, [&, fading](Call& c) { return setInt(c, fading(13)); });
    auto setAlpha = [&, forKind](Call& c, int kind, std::size_t at) {
        if (c.count() <= at) {
            return false;
        }
        const std::int32_t a = fixed(c.real(at));
        forKind(kind, [&](Chara& o) { o.alpha.set(a); });
        return setInt(c, 1);
    };
    m.service(5901, [&, setAlpha](Call& c) { return setAlpha(c, -1, 0); });
    m.service(5921, [&, setAlpha](Call& c) { return setAlpha(c, 13, 0); });
    auto fade = [&, forKind](Call& c, int kind, std::size_t at) {
        if (c.count() < at + 2) {
            return false;
        }
        const int curve = c.count() > at + 2 ? c.integer(at + 2) : 0;
        const bool lock = c.count() > at + 3 && c.at(at + 3).truthy();
        const std::int32_t a = fixed(c.real(at)), frames = fixed(c.real(at + 1));
        forKind(kind, [&](Chara& o) { o.alpha.move(a, frames, curve, lock); });
        return setInt(c, 1);
    };
    m.service(5902, [&, fade](Call& c) { return fade(c, -1, 0); });
    m.service(5922, [&, fade](Call& c) { return fade(c, 13, 0); });
    auto pulse = [&, forKind](Call& c, int kind, std::size_t at) {
        if (c.count() < at + 3) {
            return false;
        }
        const bool lock = c.count() > at + 3 && c.at(at + 3).truthy();
        const std::int32_t a = fixed(c.real(at)), b = fixed(c.real(at + 1)),
                           frames = fixed(c.real(at + 2));
        forKind(kind, [&](Chara& o) { o.alpha.pulse(a, b, frames, lock); });
        return setInt(c, 1);
    };
    m.service(5903, [&, pulse](Call& c) { return pulse(c, -1, 0); });
    m.service(5913, [&, pulse](Call& c) { return c.count() && pulse(c, c.integer(0), 1); });
    m.service(5923, [&, pulse](Call& c) { return pulse(c, 13, 0); });
    auto removeKind = [&](int kind) {
        std::vector<std::string> names;
        for (Chara* o : scene.objects()) {
            if (kind < 0 || o->kind == kind) {
                names.push_back(o->name);
            }
        }
        for (const auto& n : names) {
            scene.remove(n);
        }
    };
    m.service(5904, [&, removeKind](Call& c) {
        removeKind(-1);
        return setInt(c, 1);
    });
    m.service(5914, [&, removeKind](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        removeKind(c.integer(0));
        return setInt(c, 1);
    });
    m.service(5924, [&, removeKind](Call& c) {
        removeKind(13);
        return setInt(c, 1);
    });
    m.service(5931, [&](Call& c) { // 020fd850 {names...} (020d2230)
        // The cast keeps only the named: every other kind-13 object goes,
        // with the animation of its name.
        std::vector<std::string> keep;
        for (std::size_t i = 0; i < c.count() && i < 16; ++i) {
            if (const std::string* t = c.text(i)) {
                keep.push_back(*t);
            }
        }
        std::vector<std::string> gone;
        for (Chara* o : scene.objects()) {
            if (o->kind == 13 && std::find(keep.begin(), keep.end(), o->name) == keep.end()) {
                gone.push_back(o->name);
            }
        }
        for (const auto& n : gone) {
            scene.remove(n);
            scene.unloadAnimation(n);
        }
        return setInt(c, 1);
    });
    // 020d1e08 / 020d1f2c: the alpha of every object of a kind (5002's
    // second operand, +0x3c4).
    m.service(5911, [&](Call& c) { // 020fd298 {kind, alpha}
        if (c.count() < 2) {
            return false;
        }
        for (Chara* o : scene.objects()) {
            if (o->kind == c.integer(0)) {
                o->alpha.set(fixed(c.real(1)));
            }
        }
        return setInt(c, 1);
    });
    m.service(5912, [&](Call& c) { // 020fd314 {kind, alpha, frames, [curve], [lock]}
        if (c.count() < 3) {
            return false;
        }
        const int curve = c.count() > 3 ? c.integer(3) : 0;
        const bool lock = c.count() > 4 && c.at(4).truthy();
        for (Chara* o : scene.objects()) {
            if (o->kind == c.integer(0)) {
                o->alpha.move(fixed(c.real(1)), fixed(c.real(2)), curve, lock);
            }
        }
        return setInt(c, 1);
    });
    m.service(5143, [&, object](Call& c) { // 020fa804 {object, alpha, frames, [curve], [lock]}
        Chara* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        const int curve = c.count() > 3 ? c.integer(3) : 0;
        const bool lock = c.count() > 4 && c.at(4).truthy();
        o->alpha.move(fixed(c.real(1)), fixed(c.real(2)), curve, lock);
        return setInt(c, 1);
    });
    m.service(5144, [&, object](Call& c) { // 020fa95c {object, from, to, frames, [lock]}
        Chara* o = object(c);
        if (!o || c.count() < 4) {
            return false;
        }
        const bool lock = c.count() > 4 && c.at(4).truthy();
        return setInt(c,
                      o->alpha.pulse(fixed(c.real(1)), fixed(c.real(2)), fixed(c.real(3)), lock));
    });
    m.service(5152, [&, object](Call& c) { // 020fab10 {object, x, y, w, h, [id]}: a touch area
        Chara* o = object(c);
        if (!o || c.count() < 5) {
            return false;
        }
        // ov6 020ac340: at most 16 areas.
        if (o->touchAreas.size() >= 16) {
            return setInt(c, 0);
        }
        auto s16 = [&](std::size_t i) {
            return int(std::int16_t(c.integer(i)));
        };
        o->touchAreas.push_back({s16(1), s16(2), s16(3), s16(4), c.count() > 5 ? s16(5) : 0});
        return setInt(c, 1);
    });
    m.service(5150, [&, object](Call& c) { // 020faa64 (020ac2b4): no touch areas
        Chara* o = object(c);
        if (!o) {
            return false;
        }
        o->touchAreas.clear();
        return setInt(c, 1);
    });
    m.service(5153, [&, object](Call& c) { // 020fac4c {object, index, x, y, w, h, id} out
        Chara* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        const auto i = std::size_t(c.at(1).toUint());
        if (i >= o->touchAreas.size()) {
            return false;
        }
        for (std::size_t k = 0; k < 5 && k + 2 < c.count(); ++k) {
            store(c, k + 2, Value::ofInt(o->touchAreas[i][k]));
        }
        return setInt(c, 1);
    });
    m.service(5154, [&, object](Call& c) { // 020fad48 {object, x, y, w, h} out
        Chara* o = object(c);
        if (!o) {
            return false;
        }
        const auto b = o->bounds();
        store(c, 1, Value::ofInt(b.x1));
        store(c, 2, Value::ofInt(b.y1));
        store(c, 3, Value::ofInt(std::int16_t(b.x2 - b.x1)));
        store(c, 4, Value::ofInt(std::int16_t(b.y2 - b.y1)));
        return setInt(c, 1);
    });
    m.service(5200, [&, object](Call& c) { // 020fb680: still moving
        Chara* o = object(c);
        return o && setInt(c, o->position.moving());
    });
    m.service(5201, [&, object](Call& c) { // 020fb6d4 {object, x, y} out
        if (c.count() < 3) {
            return false;
        }
        Chara* o = object(c);
        if (!o) {
            store(c, 1, Value::ofFloat(0));
            store(c, 2, Value::ofFloat(0));
            return setInt(c, 0);
        }
        store(c, 1, Value::ofFloat(unfixed(o->position.value()[0])));
        store(c, 2, Value::ofFloat(unfixed(o->position.value()[1])));
        return setInt(c, 1);
    });
    m.service(5202, [&, object](Call& c) { // 020fb798 {object, x, y}
        Chara* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        o->position.set({fixed(c.real(1)), fixed(c.real(2))});
        return setInt(c, 1);
    });
    m.service(5203, [&, object](Call& c) { // 020fb874 {object, x, y, frames, curve}
        Chara* o = object(c);
        if (!o || c.count() < 5) {
            return false;
        }
        o->position.move({fixed(c.real(1)), fixed(c.real(2))}, fixed(c.real(3)), c.integer(4));
        return setInt(c, 1);
    });
    m.service(5204, [&, object](Call& c) { // 020fb98c {object, dx, dy, frames, curve}
        Chara* o = object(c);
        if (!o || c.count() < 5) {
            return false;
        }
        const auto p = o->position.value();
        o->position.move({p[0] + fixed(c.real(1)), p[1] + fixed(c.real(2))}, fixed(c.real(3)),
                         c.integer(4));
        return setInt(c, 1);
    });
    m.service(5205, [&, object](Call& c) { // 020fbad8: to the playing sequence's position
        Chara* o = object(c);
        const auto* seq = o ? o->animation.sequence() : nullptr;
        if (!seq) {
            return false;
        }
        o->position.set({seq->x * kOne, seq->y * kOne});
        return setInt(c, 1);
    });
    // The scale (+0xd8): 5400 still changing (020ac124), 5401 read, 5402
    // set, 5403 move, 5404 move by, 5405 one of four mirrorings (020abe90:
    // as is, left-right, up-down, both).
    m.service(5400, [&, object](Call& c) {
        Chara* o = object(c);
        return o && setInt(c, o->scale.moving());
    });
    m.service(5401, [&, object](Call& c) { // 020fcb98 {object, x, y} out
        if (c.count() < 3) {
            return false;
        }
        Chara* o = object(c);
        store(c, 1, Value::ofFloat(o ? unfixed(o->scale.value()[0]) : 0));
        store(c, 2, Value::ofFloat(o ? unfixed(o->scale.value()[1]) : 0));
        return setInt(c, o ? 1 : 0);
    });
    m.service(5402, [&, object](Call& c) { // 020fcc5c {object, x, y}
        Chara* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        o->scale.set({fixed(c.real(1)), fixed(c.real(2))});
        return setInt(c, 1);
    });
    m.service(5403, [&, object](Call& c) { // 020fcd34 {object, x, y, frames, curve}
        Chara* o = object(c);
        if (!o || c.count() < 5) {
            return false;
        }
        o->scale.move({fixed(c.real(1)), fixed(c.real(2))}, fixed(c.real(3)), c.integer(4));
        return setInt(c, 1);
    });
    m.service(5404, [&, object](Call& c) { // 020fce48 {object, dx, dy, frames, curve}
        Chara* o = object(c);
        if (!o || c.count() < 5) {
            return false;
        }
        const auto v = o->scale.value();
        o->scale.move({v[0] + fixed(c.real(1)), v[1] + fixed(c.real(2))}, fixed(c.real(3)),
                      c.integer(4));
        return setInt(c, 1);
    });
    m.service(5405, [&, object](Call& c) { // 020fcf90 {object, mirroring 0..3}
        Chara* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        const int n = c.integer(1);
        if (n < 0 || n > 3) {
            return setInt(c, 0);
        }
        o->scale.set({n & 1 ? -kOne : kOne, n & 2 ? -kOne : kOne});
        return setInt(c, 1);
    });
    m.service(5140, [&, object](Call& c) { // 020fa6c0: the alpha still changes (020aba24)
        Chara* o = object(c);
        return o && setInt(c, o->switches.on(Switches::Master) && o->alpha.moving());
    });
    m.service(5220, [&, object](Call& c) { // 020fc078 (020abc00): position still moving
        Chara* o = object(c);
        return o && setInt(c, o->switches.on(Switches::Master) && o->position.moving());
    });
    m.service(5221, [&, object](Call& c) { // 020fc0cc {object, -> x, y}: position + offset
        if (c.count() < 3) {
            return false;
        }
        Chara* o = object(c);
        const auto p = o ? o->position.value() : std::array<std::int32_t, 2>{};
        const auto d = o ? o->offset.value() : std::array<std::int32_t, 2>{};
        store(c, 1, Value::ofFloat(unfixed(p[0] + d[0])));
        store(c, 2, Value::ofFloat(unfixed(p[1] + d[1])));
        return setInt(c, o ? 1 : 0);
    });
    m.service(5222, [&, object](Call& c) { // 020fc1d0 {object, -> x, y}: on the screen (+0x128)
        if (c.count() < 3) {
            return false;
        }
        Chara* o = object(c);
        if (o) {
            o->updateWorld();
        }
        store(c, 1, Value::ofFloat(o ? unfixed(o->world[0]) : 0));
        store(c, 2, Value::ofFloat(o ? unfixed(o->world[1]) : 0));
        return setInt(c, o ? 1 : 0);
    });
    m.service(5210, [&, object](Call& c) { // 020fbb84: offset still moving
        Chara* o = object(c);
        return o && setInt(c, o->offset.moving());
    });
    m.service(5211, [&, object](Call& c) { // 020fbbd8 {object, -> x, -> y}: the offset
        if (c.count() < 3) {
            return false;
        }
        Chara* o = object(c);
        store(c, 1, Value::ofFloat(o ? unfixed(o->offset.value()[0]) : 0.0f));
        store(c, 2, Value::ofFloat(o ? unfixed(o->offset.value()[1]) : 0.0f));
        return setInt(c, o ? 1 : 0);
    });
    m.service(5212, [&, object](Call& c) { // 020fbc9c {object, x, y}
        Chara* o = object(c);
        if (!o || c.count() < 3) {
            return false;
        }
        o->offset.set({fixed(c.real(1)), fixed(c.real(2))});
        return setInt(c, 1);
    });
    m.service(5213, [&, object](Call& c) { // 020fbd74 {object, x, y, frames, curve}
        Chara* o = object(c);
        if (!o || c.count() < 5) {
            return false;
        }
        o->offset.move({fixed(c.real(1)), fixed(c.real(2))}, fixed(c.real(3)), c.integer(4));
        return setInt(c, 1);
    });
    m.service(5214, [&, object](Call& c) { // 020fbe88 {object, dx, dy, frames, curve}
        Chara* o = object(c);
        if (!o || c.count() < 5) {
            return false;
        }
        const auto p = o->offset.value();
        o->offset.move({p[0] + fixed(c.real(1)), p[1] + fixed(c.real(2))}, fixed(c.real(3)),
                       c.integer(4));
        return setInt(c, 1);
    });

    m.service(5220, [&, object](Call& c) { // 020fc078 (020abc00): position or offset moving
        Chara* o = object(c);
        return o && setInt(c, o->switches.on(Switches::Master) &&
                                  (o->position.moving() || o->offset.moving()));
    });
    m.service(5290, [&, object](Call& c) { // 020fc294 {object, x, y, across, down, size, frames}
        Chara* o = object(c);
        if (!o || c.count() < 7) {
            return false;
        }
        auto s16 = [&](std::size_t i) {
            return int(std::int16_t(c.integer(i)));
        };
        o->position.setGrid(
            {{}, {s16(1), s16(2)}, {s16(3), s16(4)}, c.integer(5), fixed(c.real(6))});
        return setInt(c, 1);
    });
    m.service(5293, [&, object](Call& c) { // 020fc4d0 {object, [x, y]}: to the nearest cell
        Chara* o = object(c);
        if (!o) {
            return false;
        }
        const auto at = c.count() < 3 ? o->position.value()
                                      : TweenPoint::Point{fixed(c.real(1)), fixed(c.real(2))};
        return setInt(c, o->position.snap(at, 2) ? 1 : 0);
    });
    // Turning in steps (the rotation's step mode, ov6 020838d0..).
    m.service(5390, [&, object](Call& c) { // 020fc8f4 {object, steps, frames}
        Chara* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->setSteps(int(c.at(1).toUint()), c.count() > 2 ? fixed(c.real(2)) : 0);
        return setInt(c, 1);
    });
    m.service(5391, [&, object](Call& c) { // 020fc99c {object, step} out
        if (c.count() < 2) {
            return false;
        }
        Chara* o = object(c);
        store(c, 1, Value::ofInt(o ? o->steps.step : -1));
        return setInt(c, o != nullptr);
    });
    m.service(5392, [&, object](Call& c) { // 020fca1c {object, step}
        Chara* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        return setInt(c, o->setStep(c.integer(1)) ? 1 : 0);
    });
    m.service(5393, [&, object](Call& c) { // 020fca8c {object, [angle]}: to the nearest step
        Chara* o = object(c);
        if (!o) {
            return false;
        }
        const std::int32_t a = c.count() < 2 ? o->rotation.value() : fixed(c.real(1));
        return setInt(c, o->turnTo(a, 2, false) ? 1 : 0);
    });
    m.service(5222, [&, object](Call& c) { // 020fc1d0 {object, x, y} out: where it is
        if (c.count() < 3) {
            return false;
        }
        Chara* o = object(c);
        store(c, 1, Value::ofFloat(o ? unfixed(o->world[0]) : 0.0f));
        store(c, 2, Value::ofFloat(o ? unfixed(o->world[1]) : 0.0f));
        return setInt(c, o != nullptr);
    });

    // ------------------------------------------------------ places
    m.service(5170, [&, object](Call& c) { // 020fb058: off its place (020db500)
        Chara* o = object(c);
        if (!o) {
            return false;
        }
        if (o->placeHash) {
            o->parentMatrix = {kOne, 0, 0, kOne};
            o->parentTranslation = {0, 0};
            o->placeHash = 0;
        }
        return setInt(c, 1);
    });
    m.service(5171, [&, object](Call& c) { // 020fb0b0 {object, place}: onto a place (020db4a0)
        Chara* o = object(c);
        Place* p = o && c.count() > 1 ? scene.findPlace(textOr(c, 1)) : nullptr;
        if (!p) {
            return false;
        }
        o->placeHash = p->hash;
        o->parentMatrix = p->matrix;
        o->parentTranslation = p->position;
        return setInt(c, 1);
    });
    m.service(5172, [&, object](Call& c) { // 020fb150: the place an object is on
        Chara* o = object(c);
        if (!o) {
            return false;
        }
        const Place* p = o->placeHash ? scene.findPlace(o->placeHash) : nullptr;
        c.result = p ? Value::ofText(p->name) : Value{};
        return true;
    });
    m.service(5191, [&, object](Call& c) { // 020fb46c {object, [priority]}: to the front
        Chara* o = object(c);
        if (!o) {
            return setInt(c, 0);
        }
        scene.place(*o, o->screen, c.count() > 1 ? c.integer(1) : -1);
        return setInt(c, 1);
    });
    m.service(5940, [&](Call& c) { // 020fd8cc {kind, [index]}: an object's name
        if (c.count() == 0) {
            return false;
        }
        const Chara* o = scene.objectOfKind(c.integer(0), c.count() > 1 ? c.integer(1) : 0);
        c.result = o ? Value::ofText(o->name) : Value{};
        return true;
    });
    m.service(5941, [&](Call& c) { // 020fd93c {place, [index]}: an object on it
        if (c.count() == 0) {
            return false;
        }
        const Chara* o =
            scene.objectOnPlace(nameHash(textOr(c, 0)), c.count() > 1 ? c.integer(1) : 0);
        c.result = o ? Value::ofText(o->name) : Value{};
        return true;
    });
    m.service(8002, [&](Call& c) { // 020ffe48 {name, x, y, [parent]}
        if (c.count() < 3) {
            return false;
        }
        const Place* p = scene.createPlace(textOr(c, 0), {fixed(c.real(1)), fixed(c.real(2))},
                                           c.count() > 3 ? textOr(c, 3) : std::string());
        return setInt(c, p != nullptr);
    });
    m.service(8100, [&](Call& c) { // 021000a4 {place, x, y} out
        if (c.count() < 3) {
            return false;
        }
        const Place* p = scene.findPlace(textOr(c, 0));
        store(c, 1, Value::ofFloat(p ? unfixed(p->position[0]) : 0.0f));
        store(c, 2, Value::ofFloat(p ? unfixed(p->position[1]) : 0.0f));
        return setInt(c, p != nullptr);
    });
    m.service(8900, [&](Call& c) { // 02100168 {x, y, [radius]}: the nearest place
        if (c.count() < 2) {
            return false;
        }
        const std::int32_t radius = c.count() > 2 ? fixed(c.real(2)) : 512 * kOne;
        const Place* p = scene.nearestPlace({fixed(c.real(0)), fixed(c.real(1))}, radius);
        c.result = p ? Value::ofText(p->name) : Value{};
        return true;
    });

    m.service(8902, [&](Call& c) { // 021003f8 {x, y, w, h, n}: a place inside
        if (c.count() < 5) {
            return false;
        }
        const Place* p = scene.placeInside({fixed(c.real(0)), fixed(c.real(1))},
                                           {fixed(c.real(2)), fixed(c.real(3))}, c.integer(4));
        c.result = p ? Value::ofText(p->name) : Value{};
        return true;
    });
    m.service(8910, [&](Call& c) { // 02100520 {x, y, w, h}: places inside
        if (c.count() < 4) {
            return false;
        }
        return setInt(c, scene.placesInside({fixed(c.real(0)), fixed(c.real(1))},
                                            {fixed(c.real(2)), fixed(c.real(3))}));
    });

    // --------------------------------------------------------- paints
    auto paint = [&](const Call& c) -> Paint* {
        return c.count() ? scene.paint(textOr(c, 0)) : nullptr;
    };
    m.service(7002, [&](Call& c) { // 020ff1d4 {name, screen, [4]}
        if (c.count() < 2) {
            return false;
        }
        return setInt(c, scene.createPaint(textOr(c, 0), c.integer(1)) != nullptr);
    });
    m.service(7010, [&](Call& c) { // 020ff28c: whether all the named paints exist
        for (std::size_t i = 0; i < c.count(); ++i) {
            if (!scene.paint(textOr(c, i))) {
                return setInt(c, 0);
            }
        }
        return setInt(c, 1);
    });
    for (int k = 0; k < 4; ++k) { // 7011..7014 020ff2fc..: switches (vtable +0x10..+0x1c)
        m.service(7011 + k, [&, k, paint](Call& c) {
            Paint* p = paint(c);
            if (!p || c.count() < 2) {
                return false;
            }
            p->switches.set(Switches::Kind(k), c.at(1).truthy());
            return setInt(c, 1);
        });
    }
    // The memo's drawing kept between visits: 7101 (lt4CPaint 020dfde0)
    // puts the kept drawing back into the paint's area, 7102 (020dfd94)
    // keeps it; 7103 / 7104 the same with the second store.
    auto keepDrawing = [&](Paint& p, std::vector<std::uint16_t>& store, bool keep) {
        auto& d = scene.screens[std::size_t(p.screen & 1)].drawing;
        const int x1 = std::clamp(p.area[0], 0, 256), y1 = std::clamp(p.area[1], 0, 192);
        const int x2 = std::clamp(p.area[2], x1, 256), y2 = std::clamp(p.area[3], y1, 192);
        const std::size_t w = std::size_t(x2 - x1), size = w * std::size_t(y2 - y1);
        if (keep) {
            store.assign(size, 0);
        } else if (store.size() != size) {
            return;
        }
        for (int y = y1; y < y2; ++y) {
            auto* row = &d[std::size_t(y) * 256 + std::size_t(x1)];
            auto* kept = &store[std::size_t(y - y1) * w];
            keep ? std::copy_n(row, w, kept) : std::copy_n(kept, w, row);
        }
        p.dirty = true;
    };
    for (int k = 0; k < 4; ++k) {
        m.service(7101 + k, [&, k, paint, keepDrawing](Call& c) {
            Paint* p = paint(c);
            if (!p) {
                return false;
            }
            keepDrawing(*p, k < 2 ? data.memo : data.memoScratch, k % 2 == 1);
            return setInt(c, 1);
        });
    }
    m.service(7111, [&, paint](Call& c) { // 020ff730 {name, x, y, w, h}: clears a box (020b242c)
        Paint* p = paint(c);
        if (!p || c.count() < 5) {
            return false;
        }
        auto& layer = scene.screens[std::size_t(p->screen & 1)].drawing;
        const int x = c.integer(1), y = c.integer(2), w = c.integer(3), h = c.integer(4);
        for (int yy = std::max(0, y); yy < std::min(192, y + h); ++yy) {
            for (int xx = std::max(0, x); xx < std::min(256, x + w); ++xx) {
                layer[std::size_t(yy) * 256 + std::size_t(xx)] = 0;
            }
        }
        return setInt(c, 1);
    });
    m.service(7110, [&, paint](Call& c) { // 020ff6d8 (020b23c8): clears the drawing
        Paint* p = paint(c);
        if (!p) {
            return false;
        }
        scene.clearPaint(*p);
        return setInt(c, 1);
    });
    m.service(7112, [&, paint](Call& c) { // 020ff830 (020b1f64): the mode
        Paint* p = paint(c);
        if (!p || c.count() < 2) {
            return false;
        }
        p->pressed = false;
        p->mode = c.integer(1);
        return setInt(c, 1);
    });
    m.service(7113, [&, paint](Call& c) { // 020ff8d8 (020b1ff0) {name, x, y, w, h}: the area
        Paint* p = paint(c);
        if (!p || c.count() < 5) {
            return false;
        }
        auto s16 = [&](std::size_t i) {
            return int(std::int16_t(c.integer(i)));
        };
        p->area = {s16(1), s16(2), int(std::int16_t(s16(1) + s16(3))),
                   int(std::int16_t(s16(2) + s16(4)))};
        return setInt(c, 1);
    });
    m.service(7114, [&, paint](Call& c) { // 020ff9a0 (020b2054): the pen's width
        Paint* p = paint(c);
        if (!p || c.count() < 2) {
            return false;
        }
        p->width = fixed(c.real(1));
        return setInt(c, 1);
    });
    m.service(7115, [&, paint](Call& c) { // 020ffa28 (020b2040): the scale
        Paint* p = paint(c);
        if (!p || c.count() < 2) {
            return false;
        }
        p->scale = fixed(c.real(1));
        return setInt(c, 1);
    });
    m.service(7120, [&, paint](Call& c) { // 020ffab0: the colour
        Paint* p = paint(c);
        return p && setInt(c, std::uint8_t(p->colour));
    });
    // 7121 020ffb00 (020b2068): the colour, an entry of the drawing
    // layer's palette from 0xf0 (taken to be the text colours,
    // hypothesis).
    auto setColour = [&](Paint& p, int colour) {
        p.colour = std::uint8_t(colour);
        const auto& colours = textColours(scene.assets);
        p.ink = std::size_t(p.colour) < colours.size() ? colours[p.colour] & 0x7fff : 0;
    };
    m.service(7121, [&, paint, setColour](Call& c) {
        Paint* p = paint(c);
        if (!p || c.count() < 2) {
            return false;
        }
        setColour(*p, c.integer(1));
        return setInt(c, 1);
    });
    m.service(7140, [&, paint](Call& c) { // 020ffd30 (020dfce4): strokes, answer and drawing gone
        Paint* p = paint(c);
        if (!p) {
            return false;
        }
        p->answer.clear();
        scene.clearPaint(*p);
        return setInt(c, 1);
    });
    m.service(7141, [&, paint](Call& c) { // 020ffd88: what the box read (+0x120)
        Paint* p = paint(c);
        if (!p) {
            return false;
        }
        c.result = Value::ofText(p->answer);
        return true;
    });
    m.service(7130, [&, paint](Call& c) { // 020ffc48 {name, x, y, w, h} out: the drawn box
        Paint* p = paint(c);
        if (!p || c.count() < 5) {
            return false;
        }
        store(c, 1, Value::ofInt(p->drawn[0]));
        store(c, 2, Value::ofInt(p->drawn[1]));
        store(c, 3, Value::ofInt(std::int16_t(p->drawn[2] - p->drawn[0])));
        store(c, 4, Value::ofInt(std::int16_t(p->drawn[3] - p->drawn[1])));
        return setInt(c, 1);
    });

    // ------------------------------------------------------- messages
    // 6011 020fdb34 .. 6014 020fdca8 {message, on}: a message's master,
    // touch, update and draw switch (020ae114 / the switches' own vtable /
    // 020ae160).
    auto messageSwitch = [&](Call& c, Switches::Kind k) {
        if (c.count() < 2) {
            return false;
        }
        Message* m = scene.message(textOr(c, 0));
        if (!m) {
            return false;
        }
        m->switches.set(k, c.at(1).truthy());
        return setInt(c, 1);
    };
    m.service(6011, [&, messageSwitch](Call& c) { return messageSwitch(c, Switches::Master); });
    m.service(6012, [&, messageSwitch](Call& c) { return messageSwitch(c, Switches::Touch); });
    m.service(6013, [&, messageSwitch](Call& c) { return messageSwitch(c, Switches::Update); });
    m.service(6014, [&, messageSwitch](Call& c) { return messageSwitch(c, Switches::Draw); });
    m.service(4820, [&](Call& c) { // 020f827c {texts, id}: how many strings the id holds
        if (c.count() < 2) {
            return false;
        }
        return setInt(c, scene.textCount(textOr(c, 0), c.integer(1)));
    });
    m.service(4830, [&](Call& c) { // 020f8384 {texts, i}: the i-th id
        if (c.count() < 2) {
            return false;
        }
        return setInt(c, scene.textIdAt(textOr(c, 0), c.integer(1)));
    });
    // Everything of a kind goes.
    m.service(4600, [&](Call& c) { // 020f66c0
        scene.removeAllImages();
        return setInt(c, 1);
    });
    m.service(4700, [&](Call& c) { // 020f7350
        scene.removeAllAnimations();
        return setInt(c, 1);
    });
    m.service(5000, [&](Call& c) { // 020f90d4
        scene.removeAllObjects();
        return setInt(c, 1);
    });
    m.service(6000, [&](Call& c) { // 020fd9b0
        scene.removeAllMessages();
        return setInt(c, 1);
    });
    m.service(7000, [&](Call& c) { // 020ff16c
        scene.removeAllPaints();
        return setInt(c, 1);
    });

    // ----------------------------------------------------------- films
    // ov12 02100630..021007f0 on the film process (Scene::Film).
    m.service(9000,
              [&](Call& c) { return setInt(c, 1); }); // 020cd3c0: the screens' background films
    m.service(9001, [&](Call& c) { return setInt(c, 1); }); // 020cd4ac: ... stop
    m.service(9100, [&](Call& c) {                          // 02100678 {film, [flag]} (020cd00c)
        if (c.count() == 0) {
            return false;
        }
        const int n = c.integer(0);
        if (n < 0) {
            return setInt(c, 0);
        }
        scene.film.request = n;
        scene.film.flag = c.count() > 1 && c.at(1).truthy();
        return setInt(c, 1);
    });
    m.service(9101, [&](Call& c) { // 021006f0: stop (020efdc4)
        auto& f = scene.film;
        const bool ok = f.state == 1 || f.state == 2 || f.state == 3 || f.state == 5;
        if (ok) {
            f.state = 6;
        }
        return setInt(c, ok ? 1 : 0);
    });
    m.service(9102, [&](Call& c) { // 02100714: start (020efd94)
        auto& f = scene.film;
        const bool ok = f.state == 1;
        if (ok) {
            f.state = 2;
        }
        return setInt(c, ok ? 1 : 0);
    });
    m.service(9110, [&](Call& c) { return setInt(c, scene.film.state >= 0); }); // 02100738
    m.service(9111, [&](Call& c) { return setInt(c, scene.film.state == 0); }); // 020ef0cc: opening
    m.service(9112, [&](Call& c) {                                              // 020ef0f8: playing
        return setInt(c, scene.film.state == 3 || scene.film.state == 4);
    });
    m.service(9120, [&](Call& c) { // 020ef128 (+0x390): the frame due
        return setInt(c, scene.film.state >= 0 ? scene.film.frame : -1);
    });
    m.service(9121, [&](Call& c) { // 020ef144 (+0x394): frames shown
        return setInt(c, scene.film.state >= 0 ? scene.film.shown : -1);
    });
    m.service(9122, [&](Call& c) { // 020ef160 (+0x384): the film's time in 60ths
        return setInt(c, scene.film.state >= 0 ? scene.film.time : -1);
    });
    m.service(6133, [&](Call& c) { // 020fe6bc {message, n}: +0x11c (*role not identified*)
        if (c.count() < 2) {
            return false;
        }
        Message* m = scene.message(textOr(c, 0));
        if (!m) {
            return false;
        }
        m->setting11c = c.integer(1);
        return setInt(c, 1);
    });
    m.service(6002, [&](Call& c) { // 020fda18 {name, screen, width, [flag]}
        if (c.count() < 3) {
            return false;
        }
        const int font = c.count() > 3 && c.at(3).truthy() ? 1 : 0;
        return setInt(c, scene.createMessage(textOr(c, 0), c.integer(1), c.integer(2), font) !=
                             nullptr);
    });
    auto msg = [&](const Call& c) -> Message* {
        return c.count() ? scene.message(textOr(c, 0)) : nullptr;
    };
    m.service(6112, [&, msg](Call& c) { // 020fdec0
        Message* x = msg(c);
        if (!x || c.count() < 2) {
            return false;
        }
        x->textColour = c.integer(1);
        return setInt(c, 1);
    });
    m.service(6113, [&, msg](Call& c) { // 020fdf34
        Message* x = msg(c);
        if (!x || c.count() < 2) {
            return false;
        }
        x->edgeColour = c.integer(1);
        return setInt(c, 1);
    });
    m.service(6122, [&, msg](Call& c) { // 020fe058: flag bit 0
        Message* x = msg(c);
        if (!x || c.count() < 2) {
            return false;
        }
        x->flags = (x->flags & ~1u) | (c.at(1).truthy() ? 1u : 0u);
        return setInt(c, 1);
    });
    m.service(6123, [&, msg](Call& c) { // 020fe0d4: flag bit 1
        Message* x = msg(c);
        if (!x || c.count() < 2) {
            return false;
        }
        x->flags = (x->flags & ~2u) | (c.at(1).truthy() ? 2u : 0u);
        return setInt(c, 1);
    });
    m.service(6131, [&, msg](Call& c) { // 020fe1a0 {name, sound, [frames = 5]}
        Message* x = msg(c);
        if (!x || c.count() < 2) {
            return false;
        }
        x->typeSound = c.integer(1);
        x->typeSoundFrames = c.count() > 2 ? c.integer(2) : 5;
        return setInt(c, 1);
    });
    // The text appearing (gnCMessage 020b03d0).
    auto msgQuery = [&, msg](Call& c, auto&& what) {
        Message* x = msg(c);
        return x && setInt(c, what(*x) ? 1 : 0);
    };
    m.service(6100, [&, msgQuery](Call& c) { // 020fdd24 (020ae44c): all shown
        return msgQuery(c, [](const Message& x) { return x.done(); });
    });
    // 6101 020fdd78 (020ae4c4): needs the message's flag 6 (set by <K>
    // marks, not reconstructed); without it the answer is 0.
    m.service(6101,
              [&, msgQuery](Call& c) { return msgQuery(c, [](const Message&) { return false; }); });
    m.service(6102, [&, msgQuery](Call& c) { // 020fddcc (020ae594): waiting for a tap
        return msgQuery(c, [](const Message& x) { return x.waiting; });
    });
    // 4290 020f59f4 {screen, [x, y, w, h]}: clears a box of the screen's
    // text layer (02098328 on its layer 2; one operand: the whole screen).
    m.service(4290, [&](Call& c) {
        if (c.count() == 0 || c.integer(0) < 0 || c.integer(0) > 1) {
            return setInt(c, 0);
        }
        std::array<int, 4> box{0, 0, 256, 192};
        if (c.count() >= 5) {
            box = {c.integer(1), c.integer(2), c.integer(1) + c.integer(3),
                   c.integer(2) + c.integer(4)};
        }
        scene.eraseTexts(c.integer(0), box);
        return setInt(c, 1);
    });
    m.service(6230, [&, msg](Call& c) { // 020ff134 {name}: drawn again (020b037c)
        Message* x = msg(c);
        if (!x) {
            return false;
        }
        x->erased.clear();
        return setInt(c, 1);
    });
    // 6140 020fe448 {name, x, y, w, h}: the box the message's text shows in
    // (+0xd4, 020ae67c).
    m.service(6140, [&, msg](Call& c) {
        Message* x = msg(c);
        if (!x || c.count() < 5) {
            return false;
        }
        x->clip = {c.integer(1), c.integer(2), c.integer(1) + c.integer(3),
                   c.integer(2) + c.integer(4)};
        return setInt(c, 1);
    });
    m.service(6190, [&, msg](Call& c) { // 020fe248 (020afe20): no text
        Message* x = msg(c);
        if (!x) {
            return false;
        }
        x->clear();
        return setInt(c, 1);
    });
    m.service(6192, [&, msg](Call& c) { // 020fe398 (020ae5d4): show the rest at once
        Message* x = msg(c);
        if (!x) {
            return false;
        }
        x->instant = true;
        return setInt(c, 1);
    });
    m.service(6193, [&, msg](Call& c) { // 020fe3f0 (020ae5e4): a tap
        Message* x = msg(c);
        if (!x) {
            return false;
        }
        x->skip = true;
        return setInt(c, 1);
    });
    m.service(6142, [&, msg](Call& c) { // 020fe5e0 (020b0350): glyphs a frame
        Message* x = msg(c);
        if (!x || c.count() < 2) {
            return false;
        }
        x->speed = std::max(0, fixed(c.real(1)));
        return setInt(c, 1);
    });
    m.service(6141, [&, msg](Call& c) { // 020fe544 {name, letters, lines, ruby}
        Message* x = msg(c);
        if (!x || c.count() < 4) {
            return false;
        }
        x->letterSpacing = c.integer(1);
        x->lineSpacing = c.integer(2);
        x->rubyOffset = c.integer(3);
        return setInt(c, 1);
    });
    m.service(6191, [&, msg](Call& c) { // 020fe29c {name, text, [at once = 1], [tap at the end]}
        Message* x = msg(c);
        if (!x || c.count() < 2) {
            return false;
        }
        const bool instant = c.count() == 2 || c.at(2).truthy();
        const bool waitAtEnd = c.count() > 3 && c.at(3).truthy();
        return setInt(c, scene.setText(*x, textOr(c, 1), instant, waitAtEnd) ? 1 : 0);
    });
    m.service(6202, [&, msg](Call& c) { // 020fe844 {name, x, y}
        Message* x = msg(c);
        if (!x || c.count() < 3) {
            return false;
        }
        x->position.set({fixed(c.real(1)), fixed(c.real(2))});
        return setInt(c, 1);
    });
    m.service(6212, [&, msg](Call& c) { // 020fec9c {name, x, y}
        Message* x = msg(c);
        if (!x || c.count() < 3) {
            return false;
        }
        x->offset.set({fixed(c.real(1)), fixed(c.real(2))});
        return setInt(c, 1);
    });
    // 6204 020fe920 / 6214 020fed78 {name, x, y, frames, curve}: the
    // position (020ae740) or the offset (020ae768) moves to a point.
    m.service(6204, [&, msg](Call& c) {
        Message* x = msg(c);
        if (!x || c.count() < 5) {
            return false;
        }
        x->position.move({fixed(c.real(1)), fixed(c.real(2))}, fixed(c.real(3)), c.integer(4));
        return setInt(c, 1);
    });
    m.service(6214, [&, msg](Call& c) {
        Message* x = msg(c);
        if (!x || c.count() < 5) {
            return false;
        }
        x->offset.move({fixed(c.real(1)), fixed(c.real(2))}, fixed(c.real(3)), c.integer(4));
        return setInt(c, 1);
    });
    // 6205 020fea38 / 6215 020fee90 {name, dx, dy, frames, curve}: the
    // position or the offset moves by a distance.
    m.service(6205, [&, msg](Call& c) {
        Message* x = msg(c);
        if (!x || c.count() < 5) {
            return false;
        }
        const auto p = x->position.value();
        x->position.move({p[0] + fixed(c.real(1)), p[1] + fixed(c.real(2))}, fixed(c.real(3)),
                         c.integer(4));
        return setInt(c, 1);
    });
    m.service(6215, [&, msg](Call& c) {
        Message* x = msg(c);
        if (!x || c.count() < 5) {
            return false;
        }
        const auto p = x->offset.value();
        x->offset.move({p[0] + fixed(c.real(1)), p[1] + fixed(c.real(2))}, fixed(c.real(3)),
                       c.integer(4));
        return setInt(c, 1);
    });
}

void Services::effect(int id) const {
    if (audio) {
        audio->playEffect(id);
    }
}

void Services::stopEffect(int id) const {
    if (audio) {
        audio->stopEffect(id, 0);
    }
}

bool Services::effectPlaying(int id) const {
    return audio && audio->effectPlaying(id);
}

} // namespace picarat::adventure4
