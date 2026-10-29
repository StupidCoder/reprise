// The script machine's own commands: control flow, calls, variables,
// arithmetic and the game state. Each comment names the overlay-9 handler it
// reproduces; the value rules are those of a3_lscr.hpp.
#include "system/a3_lscr.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace picarat::lscr {
namespace {

// Stores an integer or float into the variable an operand names.
void store(Call& c, std::size_t i, Value v) {
    if (Value* slot = c.variable(i)) {
        slot->get() = std::move(v);
    }
}

// The game's integer arithmetic wraps around (32 bits).
int wrap(std::int64_t v) {
    return int(std::uint32_t(v));
}

// 0203e7b8 / e83c / e8c0: + - * on two values.
Value arithmetic(const Value& a, const Value& b, char op) {
    if (a.get().kind == Value::Kind::Int && b.get().kind == Value::Kind::Int) {
        const std::int64_t x = a.get().integer, y = b.get().integer;
        return Value::of(op == '+' ? wrap(x + y) : op == '-' ? wrap(x - y) : wrap(x * y));
    }
    const float x = a.toFloat(), y = b.toFloat();
    return Value::ofFloat(op == '+' ? x + y : op == '-' ? x - y : x * y);
}

// 0203e940: division; integer division truncates toward zero. Division by
// an integer zero gives 0 here (hypothesis: the ARM runtime's result is not
// recovered).
Value divide(const Value& a, const Value& b) {
    if (a.get().kind == Value::Kind::Int && b.get().kind == Value::Kind::Int) {
        const int y = b.get().integer;
        return Value::of(y == 0 ? 0 : int(std::int64_t(a.get().integer) / y));
    }
    return Value::ofFloat(a.toFloat() / b.toFloat());
}

// 0203e9cc: remainder; floats through fmod in double precision.
Value remainder(const Value& a, const Value& b) {
    if (a.get().kind == Value::Kind::Int && b.get().kind == Value::Kind::Int) {
        const int y = b.get().integer;
        return Value::of(y == 0 ? 0 : int(std::int64_t(a.get().integer) % y));
    }
    return Value::ofFloat(float(std::fmod(double(a.toFloat()), double(b.toFloat()))));
}

// Copies an operand's value into a variable (1320, 0204fd18).
void assign(Call& c, std::size_t to, std::size_t from) {
    if (Value* slot = c.variable(to)) {
        const Value v = c.at(from);
        slot->get() = v;
    }
}

int optionalIndex(Call& c, std::size_t i) {
    return i < c.count() ? c.integer(i) : 0;
}

std::vector<Value> rest(Call& c, std::size_t from) {
    std::vector<Value> v;
    for (std::size_t i = from; i < c.count(); ++i) {
        v.push_back(c.at(i));
    }
    return v;
}

} // namespace

void installCore(Machine& m) {
    using R = Result;

    // --- Control flow -------------------------------------------------------
    m.command(1000, [](Call&) { return R::Wait; }); // 02106828: next frame
    m.command(1010, [](Call&) { return R::Stop; }); // 02106830: end the thread
    m.command(1011, [](Call& c) { return c.condition(0) ? R::Stop : R::Next; });
    m.command(1020, [](Call&) { return R::Return; }); // 02106868
    m.command(1021, [](Call& c) { return c.condition(0) ? R::Return : R::Next; });
    m.command(1022, [](Call& c) { return c.condition(0) ? R::Next : R::Return; });
    m.command(1030, [](Call& c) { // 021068a0: wait forever
        c.frame.delta = 0;
        return R::Wait;
    });
    m.command(1031, [](Call& c) { // 021068b0: wait while the condition holds
        if (!c.condition(0)) {
            return R::Next;
        }
        c.frame.delta = 0;
        return R::Wait;
    });
    m.command(1032, [](Call& c) { // 021068d4: wait until the condition holds
        if (c.condition(0)) {
            return R::Next;
        }
        c.frame.delta = 0;
        return R::Wait;
    });
    m.command(1040, [](Call& c) { // 021068f8: go to a label
        if (c.count()) {
            c.jump(0);
        }
        return R::Next;
    });
    m.command(1041, [](Call& c) { // 0210692c: go to a label if
        if (c.count() && c.condition(1)) {
            c.jump(0);
        }
        return R::Next;
    });
    m.command(1042, [](Call& c) { // 02106980: go to a label unless
        if (c.count() && !c.condition(1)) {
            c.jump(0);
        }
        return R::Next;
    });
    m.command(1043, [](Call& c) { // 021069d4: go to one of the labels at random
        if (c.count()) {
            const int n = c.machine.state.random() % int(c.count());
            c.jump(std::size_t(n));
        }
        return R::Next;
    });

    // --- The thread's clock (4096 per frame) ---------------------------------
    m.command(1090, [](Call& c) { // 02106a1c: the clock starts again
        c.thread.clock = 0;
        return Result::Next;
    });
    // 1202..1205 (02103fe0): the clock against {frames} (float -> fixed,
    // 02024098 / 02023f2c): 1202 at or past, 1203 not yet past, 1204 past,
    // 1205 not yet at.
    for (int op : {1202, 1203, 1204, 1205}) {
        m.condition(op, [op](Call& c, std::span<Value> a) {
            if (a.empty()) {
                return false;
            }
            const std::int64_t at = std::int64_t(a[0].toFloat() * 4096.0f);
            const std::int64_t now = c.thread.clock;
            switch (op) {
            case 1202:
                return at <= now;
            case 1203:
                return now <= at;
            case 1204:
                return at < now;
            default:
                return now < at;
            }
        });
    }
    m.command(1091, [](Call& c) { // 02106a30: frames elapsed
        store(c, 0, Value::of(c.thread.clock >> 12));
        return R::Next;
    });
    m.command(1092, [](Call& c) { // 02106a70: set the clock (frames, float)
        if (c.count()) {
            c.thread.clock = int(c.real(0) * 4096.0f);
        }
        return R::Next;
    });

    // --- Calls --------------------------------------------------------------
    m.command(1101, [](Call& c) { // 02106b14: call a section
        if (c.count()) {
            const auto args = rest(c, 1);
            c.thread.call(c.text(0), args);
        }
        return R::Called;
    });
    m.command(1102, [](Call& c) { // 02106b6c: continue in a section (tail call)
        if (c.count()) {
            const auto args = rest(c, 1);
            c.thread.replace(c.text(0), args);
        }
        return R::Called;
    });
    m.command(1111, [](Call& c) { // 02106c24: call a section at a label
        if (c.count() > 1) {
            const auto args = rest(c, 2);
            c.thread.call(c.text(0), args, c.text(1));
        }
        return R::Called;
    });
    m.command(1112, [](Call& c) { // 02106c94: continue in a section at a label
        if (c.count() > 1) {
            const auto args = rest(c, 2);
            c.thread.replace(c.text(0), args, c.text(1));
        }
        return R::Called;
    });
    m.command(1120, [](Call& c) { // 02106d04: call a section if
        if (c.count() > 1 && c.condition(1)) {
            c.thread.call(c.text(0), {});
            return R::Called;
        }
        return R::Next;
    });
    m.command(1121, [](Call& c) { // 02106d6c: call a section unless
        if (c.count() > 1 && !c.condition(1)) {
            c.thread.call(c.text(0), {});
            return R::Called;
        }
        return R::Next;
    });
    m.command(1100, [](Call& c) { // 02106ab0: restart the owner's thread on a section
        if (!c.count() || !c.machine.ownerCall) {
            throw std::runtime_error("script command 1100 needs an owner");
        }
        const auto args = rest(c, 1);
        c.machine.ownerCall(c, c.text(0), args);
        return R::Called;
    });

    // --- Variables and arithmetic ---------------------------------------------
    m.command(1300, [](Call& c) { // 02107a4c: negate
        if (Value* v = c.variable(0)) {
            Value& t = v->get();
            if (t.kind == Value::Kind::Int) {
                t = Value::of(wrap(-std::int64_t(t.integer)));
            } else if (t.kind == Value::Kind::Float) {
                t = Value::ofFloat(0.0f - t.real);
            }
        }
        return R::Next;
    });
    m.command(1301, [](Call& c) { // 02107ac4: + 1
        if (Value* v = c.variable(0)) {
            v->get() = arithmetic(*v, Value::of(1), '+');
        }
        return R::Next;
    });
    m.command(1302, [](Call& c) { // 02107b28: - 1
        if (Value* v = c.variable(0)) {
            v->get() = arithmetic(*v, Value::of(1), '-');
        }
        return R::Next;
    });
    for (auto [op, sign] : {std::pair{1303, '+'}, {1304, '-'}, {1305, '*'}}) {
        m.command(op, [sign](Call& c) { // 02107b8c / bc4 / bfc
            if (Value* v = c.variable(0); v && c.count() > 1) {
                v->get() = arithmetic(*v, c.at(1), sign);
            }
            return R::Next;
        });
    }
    m.command(1306, [](Call& c) { // 02107c34: /
        if (Value* v = c.variable(0); v && c.count() > 1) {
            v->get() = divide(*v, c.at(1));
        }
        return R::Next;
    });
    m.command(1307, [](Call& c) { // 02107c6c: remainder
        if (Value* v = c.variable(0); v && c.count() > 1) {
            v->get() = remainder(*v, c.at(1));
        }
        return R::Next;
    });
    m.command(1310, [](Call& c) { // 02107ca4: |
        if (Value* v = c.variable(0); v && c.count() > 1) {
            v->get() = Value::of(int(std::uint32_t(v->toInt()) | std::uint32_t(c.integer(1))));
        }
        return R::Next;
    });
    m.command(1311, [](Call& c) { // 02107cdc: ^
        if (Value* v = c.variable(0); v && c.count() > 1) {
            v->get() = Value::of(int(std::uint32_t(v->toInt()) ^ std::uint32_t(c.integer(1))));
        }
        return R::Next;
    });
    m.command(1312, [](Call& c) { // 02107d14: &
        if (Value* v = c.variable(0); v && c.count() > 1) {
            v->get() = Value::of(int(std::uint32_t(v->toInt()) & std::uint32_t(c.integer(1))));
        }
        return R::Next;
    });
    m.command(1320, [](Call& c) { // 02107d4c: assign
        if (c.count() > 1) {
            assign(c, 0, 1);
        }
        return R::Next;
    });
    m.command(1321, [](Call& c) { // 02107d8c: the frame's argument count
        store(c, 0, Value::of(c.frame.argumentCount));
        return R::Next;
    });
    m.command(1322, [](Call& c) { // 02107dc4: a condition's result
        store(c, 0, Value::of(c.condition(1) ? 1 : 0));
        return R::Next;
    });
    m.command(1323, [](Call& c) { // 02107e18: a random number below n
        if (c.count() < 2) {
            return R::Next;
        }
        const Value& n = c.at(1);
        if (n.kind == Value::Kind::Int) {
            // 0203b5fc: rand() % n.
            const int r = c.machine.state.random();
            store(c, 0, Value::of(n.integer != 0 ? r % n.integer : 0));
        } else if (n.kind == Value::Kind::Float) {
            // 0203b618: n * rand() / 32767.
            store(c, 0, Value::ofFloat(n.real * float(c.machine.state.random()) / 32767.0f));
        } else {
            store(c, 0, Value::of(0));
        }
        return R::Next;
    });
    // 1330..1332: read locals / arguments / thread variables by number.
    m.command(1330, [](Call& c) {
        const int i = c.integer(1);
        store(c, 0, i >= 0 && i < 16 ? c.frame.locals[size_t(i)].get() : Value{});
        return R::Next;
    });
    m.command(1331, [](Call& c) {
        const int i = c.integer(1);
        store(c, 0,
              i >= 0 && i < c.frame.argumentCount ? c.frame.arguments[size_t(i)].get() : Value{});
        return R::Next;
    });
    m.command(1332, [](Call& c) {
        const int i = c.integer(1);
        store(c, 0, i >= 0 && i < 16 ? c.thread.variables[size_t(i)].get() : Value{});
        return R::Next;
    });
    // 1340..1342: fill locals / arguments / thread variables from a number on.
    m.command(1340, [](Call& c) {
        int i = c.integer(0);
        for (std::size_t k = 1; k < c.count(); ++k, ++i) {
            if (i >= 0 && i < 16) {
                c.frame.locals[size_t(i)] = c.at(k);
            }
        }
        return R::Next;
    });
    m.command(1341, [](Call& c) {
        int i = c.integer(0);
        for (std::size_t k = 1; k < c.count(); ++k, ++i) {
            if (i >= 0 && i < c.frame.argumentCount) {
                c.frame.arguments[size_t(i)] = c.at(k);
            }
        }
        return R::Next;
    });
    m.command(1342, [](Call& c) {
        int i = c.integer(0);
        for (std::size_t k = 1; k < c.count(); ++k, ++i) {
            if (i >= 0 && i < 16) {
                c.thread.variables[size_t(i)] = c.at(k);
            }
        }
        return R::Next;
    });
    m.command(1350, [](Call& c) { // 02108238: as a float
        store(c, 0, Value::ofFloat(c.real(1)));
        return R::Next;
    });
    m.command(1351, [](Call& c) { // 02108288: the smallest (as floats)
        float v = c.real(1);
        for (std::size_t i = 2; i < c.count(); ++i) {
            v = c.real(i) < v ? c.real(i) : v;
        }
        store(c, 0, Value::ofFloat(v));
        return R::Next;
    });
    m.command(1352, [](Call& c) { // 0210835c: the largest (as floats)
        float v = c.real(1);
        for (std::size_t i = 2; i < c.count(); ++i) {
            v = c.real(i) > v ? c.real(i) : v;
        }
        store(c, 0, Value::ofFloat(v));
        return R::Next;
    });
    m.command(1353, [](Call& c) { // 02108430: clamp(value, low, high)
        float v = c.real(1);
        v = v > c.real(2) ? v : c.real(2);
        v = v < c.real(3) ? v : c.real(3);
        store(c, 0, Value::ofFloat(v));
        return R::Next;
    });
    m.command(1354, [](Call& c) { // 021084bc: floor, as an integer
        store(c, 0, Value::of(Value::ofFloat(float(std::floor(double(c.real(1))))).toInt()));
        return R::Next;
    });
    m.command(1355, [](Call& c) { // 02108510: ceiling, as an integer
        store(c, 0, Value::of(Value::ofFloat(float(std::ceil(double(c.real(1))))).toInt()));
        return R::Next;
    });
    m.command(1356, [](Call& c) { // 02108564: rounded (floor of + 0.5), as an integer
        const float v = 0.5f + c.real(1);
        store(c, 0, Value::of(Value::ofFloat(float(std::floor(double(v)))).toInt()));
        return R::Next;
    });
    m.command(1357, [](Call& c) { // 021085c4: square root
        store(c, 0, Value::ofFloat(std::sqrt(c.real(1))));
        return R::Next;
    });

    // --- Game state (by name or number; optional array index) ----------------
    m.command(1500, [](Call& c) { // 021086f0: a text variable
        store(c, 1, Value::ofText(c.machine.state.textVariable(c.integer(0))));
        return R::Next;
    });
    m.command(1501, [](Call& c) { // 0210875c
        c.machine.state.setTextVariable(c.integer(0), c.text(1));
        return R::Next;
    });
    m.command(1510, [](Call& c) { // 021087b8: read a flag
        store(c, 1, Value::of(c.machine.state.flag(c.at(0), optionalIndex(c, 2)) ? 1 : 0));
        return R::Next;
    });
    m.command(1511, [](Call& c) { // 02108830: set a flag to a value
        c.machine.state.setFlag(c.at(0), c.integer(1) != 0, optionalIndex(c, 2));
        return R::Next;
    });
    m.command(1512, [](Call& c) { // 021088a4: set a flag
        c.machine.state.setFlag(c.at(0), true, optionalIndex(c, 1));
        return R::Next;
    });
    m.command(1513, [](Call& c) { // 02108900: clear a flag
        c.machine.state.setFlag(c.at(0), false, optionalIndex(c, 1));
        return R::Next;
    });
    m.command(1520, [](Call& c) { // 0210895c: read a variable
        store(c, 1, Value::of(c.machine.state.variable(c.at(0), optionalIndex(c, 2))));
        return R::Next;
    });
    m.command(1521, [](Call& c) { // 021089d4: set a variable
        c.machine.state.setVariable(c.at(0), c.integer(1), optionalIndex(c, 2));
        return R::Next;
    });
    m.command(1522, [](Call& c) { // 02108a58: + 1
        auto& s = c.machine.state;
        const int i = optionalIndex(c, 1);
        s.setVariable(c.at(0), s.variable(c.at(0), i) + 1, i);
        return R::Next;
    });
    m.command(1523, [](Call& c) { // 02108ae0: - 1
        auto& s = c.machine.state;
        const int i = optionalIndex(c, 1);
        s.setVariable(c.at(0), s.variable(c.at(0), i) - 1, i);
        return R::Next;
    });
    m.command(1524, [](Call& c) { // 02108b68: + n
        auto& s = c.machine.state;
        const int i = optionalIndex(c, 2);
        s.setVariable(c.at(0), s.variable(c.at(0), i) + c.integer(1), i);
        return R::Next;
    });
    m.command(1525, [](Call& c) { // 02108bf8: - n
        auto& s = c.machine.state;
        const int i = optionalIndex(c, 2);
        s.setVariable(c.at(0), s.variable(c.at(0), i) - c.integer(1), i);
        return R::Next;
    });
    m.command(1526, [](Call& c) { // 02108c88: & n
        auto& s = c.machine.state;
        const int i = optionalIndex(c, 2);
        s.setVariable(c.at(0), s.variable(c.at(0), i) & c.integer(1), i);
        return R::Next;
    });
    m.command(1527, [](Call& c) { // 02108d18: | n
        auto& s = c.machine.state;
        const int i = optionalIndex(c, 2);
        s.setVariable(c.at(0), s.variable(c.at(0), i) | c.integer(1), i);
        return R::Next;
    });

    // --- Conditions (02103fe0) -------------------------------------------------
    auto compare = [](bool (*rel)(const Value&, const Value&)) {
        return [rel](Call&, std::span<Value> a) {
            return a.size() >= 2 && rel(a[0], a[1]);
        };
    };
    m.condition(1000, compare(equal));
    m.condition(1001, compare(unequal));
    m.condition(1002, compare(atLeast));
    m.condition(1003, compare(atMost));
    m.condition(1004, compare(greater));
    m.condition(1005, compare(less));
    m.condition(1006, [](Call&, std::span<Value> a) {
        return a.size() >= 2 && (a[0].toInt() & a[1].toInt()) != 0;
    });
    m.condition(1007, [](Call&, std::span<Value> a) {
        return a.size() >= 2 && (a[0].toInt() & a[1].toInt()) == 0;
    });
    auto index = [](std::span<Value> a, std::size_t i) {
        return i < a.size() ? a[i].toInt() : 0;
    };
    m.condition(1510, [index](Call& c, std::span<Value> a) { // flag == value
        return a.size() >= 2 && c.machine.state.flag(a[0], index(a, 2)) == (a[1].toInt() != 0);
    });
    m.condition(1511, [index](Call& c, std::span<Value> a) { // flag != value
        return a.size() >= 2 && c.machine.state.flag(a[0], index(a, 2)) != (a[1].toInt() != 0);
    });
    m.condition(1512, [index](Call& c, std::span<Value> a) { // flag set
        return !a.empty() && c.machine.state.flag(a[0], index(a, 1));
    });
    m.condition(1513, [index](Call& c, std::span<Value> a) { // flag clear
        return !a.empty() && !c.machine.state.flag(a[0], index(a, 1));
    });
    const std::pair<int, bool (*)(int, int)> variableTests[] = {
        {1520,
         [](int v, int x) {
             return v == x;
         }},
        {1521,
         [](int v, int x) {
             return v != x;
         }},
        {1522,
         [](int v, int x) {
             return v >= x;
         }},
        {1523,
         [](int v, int x) {
             return v <= x;
         }},
        {1524,
         [](int v, int x) {
             return v > x;
         }},
        {1525,
         [](int v, int x) {
             return v < x;
         }},
        {1526,
         [](int v, int x) {
             return (v & x) != 0;
         }},
        {1527,
         [](int v, int x) {
             return (v & x) == 0;
         }},
    };
    for (auto [op, test] : variableTests) {
        m.condition(op, [index, test](Call& c, std::span<Value> a) {
            return a.size() >= 2 && test(c.machine.state.variable(a[0], index(a, 2)), a[1].toInt());
        });
    }
}

// --- Game state -------------------------------------------------------------

void GameState::declare(const import::adventure3::Lscr& d) {
    int temporaryBytes = 0, temporaryBits = 0;
    for (const auto& c : d.commands) {
        auto num = [&](size_t i) {
            return int(std::int32_t(c.operands.at(i).value));
        };
        if (c.opcode == 5501 && c.operands.size() >= 2) {
            const int bit = num(0);
            flagsByName_[d.text(c.operands[1])] = bit;
            temporaryBits = std::max(temporaryBits, bit - 0x600 + 1);
        } else if (c.opcode == 5511 && c.operands.size() >= 4) {
            Variable v;
            v.offset = num(0);
            v.size = std::abs(num(1));
            v.isSigned = num(1) < 0;
            v.protectedWrite = num(2) != 0;
            variablesByName_[d.text(c.operands[3])] = v;
            variablesByOffset_[v.offset] = v;
            temporaryBytes = std::max(temporaryBytes, v.offset - 0x240 + v.size * 16);
        }
    }
    temporary_.assign(size_t(std::max(temporaryBytes, 0x400)), 0);
    temporaryFlags_.assign(size_t(std::max(temporaryBits / 8 + 1, 0x100)), 0);
}

const GameState::Variable* GameState::findVariable(const Value& id) const {
    if (id.isText()) {
        auto it = variablesByName_.find(id.toText());
        return it == variablesByName_.end() ? nullptr : &it->second;
    }
    auto it = variablesByOffset_.find(id.toInt());
    return it == variablesByOffset_.end() ? nullptr : &it->second;
}

int GameState::findFlag(const Value& id) const {
    if (id.isText()) {
        auto it = flagsByName_.find(id.toText());
        return it == flagsByName_.end() ? -1 : it->second;
    }
    return id.toInt();
}

bool GameState::declared(const Value& id) const {
    return findVariable(id) || (id.isText() && flagsByName_.contains(id.toText()));
}

int GameState::variable(const Value& id, int index) const {
    // 020dfc0c / 020db204: little-endian bytes, sign-extended when the
    // declared size is negative.
    const Variable* v = findVariable(id);
    if (!v) {
        return 0;
    }
    const int at = v->offset + index * v->size;
    const auto& store = at < 0x240 ? saved_ : temporary_;
    const int base = at < 0x240 ? at : at - 0x240;
    if (base < 0 || base + v->size > int(store.size()) || v->size < 1 || v->size > 4) {
        return 0;
    }
    std::uint32_t value = 0;
    for (int i = 0; i < v->size; ++i) {
        value |= std::uint32_t(store[size_t(base + i)]) << (8 * i);
    }
    if (v->isSigned && v->size < 4 && (value >> (8 * v->size - 1)) & 1) {
        value |= ~0u << (8 * v->size);
    }
    return int(value);
}

void GameState::setVariable(const Value& id, int value, int index, bool force) {
    const Variable* v = findVariable(id);
    if (!v || (v->protectedWrite && !force)) {
        return;
    }
    const int at = v->offset + index * v->size;
    auto& store = at < 0x240 ? saved_ : temporary_;
    const int base = at < 0x240 ? at : at - 0x240;
    if (base < 0 || base + v->size > int(store.size()) || v->size < 1 || v->size > 4) {
        return;
    }
    for (int i = 0; i < v->size; ++i) {
        store[size_t(base + i)] = std::uint8_t(std::uint32_t(value) >> (8 * i));
    }
}

bool GameState::flag(const Value& id, int index) const {
    // 020dec70 / 020db194: bits below 0x600 are saved, the rest temporary.
    const int bit = findFlag(id);
    if (bit < 0) {
        return false;
    }
    const int at = bit + index;
    const auto& bits = at < 0x600 ? savedFlags_ : temporaryFlags_;
    const int b = at < 0x600 ? at : at - 0x600;
    return b >= 0 && size_t(b / 8) < bits.size() && (bits[size_t(b / 8)] >> (b % 8)) & 1;
}

void GameState::setFlag(const Value& id, bool on, int index) {
    const int bit = findFlag(id);
    if (bit < 0) {
        return;
    }
    const int at = bit + index;
    auto& bits = at < 0x600 ? savedFlags_ : temporaryFlags_;
    const int b = at < 0x600 ? at : at - 0x600;
    if (b < 0 || size_t(b / 8) >= bits.size()) {
        return;
    }
    if (on) {
        bits[size_t(b / 8)] |= std::uint8_t(1 << (b % 8));
    } else {
        bits[size_t(b / 8)] &= std::uint8_t(~(1 << (b % 8)));
    }
}

void GameState::resetSaved() {
    std::fill(saved_.begin(), saved_.end(), 0);
    std::fill(savedFlags_.begin(), savedFlags_.end(), 0);
}

std::vector<std::uint8_t> GameState::savedBytes() const {
    std::vector<std::uint8_t> out(saved_);
    out.insert(out.end(), savedFlags_.begin(), savedFlags_.end());
    return out;
}

void GameState::restoreSaved(std::span<const std::uint8_t> bytes) {
    resetSaved();
    for (size_t i = 0; i < bytes.size() && i < saved_.size() + savedFlags_.size(); ++i) {
        (i < saved_.size() ? saved_[i] : savedFlags_[i - saved_.size()]) = bytes[i];
    }
}

std::string GameState::textVariable(int index) const {
    auto it = texts_.find(index);
    return it == texts_.end() ? std::string() : it->second;
}

void GameState::setTextVariable(int index, std::string value) {
    texts_[index] = std::move(value);
}

Machine::Machine() : own_(std::make_unique<GameState>()), state(*own_) {}

Machine::Machine(GameState& shared) : state(shared) {}

} // namespace picarat::lscr
