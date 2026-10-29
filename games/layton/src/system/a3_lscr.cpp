// The third adventure's script machine. See a3_lscr.hpp.
#include "system/a3_lscr.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace picarat::lscr {
namespace {
using import::adventure3::Lscr;
using import::adventure3::LscrCommand;
using import::adventure3::LscrOperand;

const std::string kEmpty;

// 02023f2c: float to integer, toward zero, saturating (NaN by its sign).
int truncate(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, 4);
    const std::uint32_t exponent = (bits & 0x7fffffff) >> 23;
    if (exponent >= 0x9f) {
        return (bits & 0x80000000) ? std::numeric_limits<int>::min()
                                   : std::numeric_limits<int>::max();
    }
    return int(f);
}

// The text at an operand's string-pool offset (types 2 and 3 both name
// strings; type 3 marks a name to resolve).
std::string poolText(const Lscr& s, const LscrOperand& o) {
    LscrOperand t = o;
    t.type = 2;
    return s.text(t);
}

Value fromOperand(const Lscr& s, const LscrOperand& o) {
    switch (o.type) {
    case 1: {
        float f;
        std::memcpy(&f, &o.value, 4);
        return Value::ofFloat(f);
    }
    case 2:
        return Value::ofText(s.text(o));
    case 3: {
        Value v;
        v.kind = Value::Kind::Name;
        v.text = poolText(s, o);
        return v;
    }
    default:
        return Value::of(int(std::int32_t(o.value)));
    }
}

int compareNumbers(const Value& a, const Value& b) {
    const double x = a.get().kind == Value::Kind::Int ? double(a.get().integer) : a.get().real;
    const double y = b.get().kind == Value::Kind::Int ? double(b.get().integer) : b.get().real;
    return x < y ? -1 : x > y ? 1 : 0;
}

// The relation of two values: -2 when they cannot be compared.
int relation(const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber()) {
        const double x = a.get().kind == Value::Kind::Int ? double(a.get().integer) : a.get().real;
        const double y = b.get().kind == Value::Kind::Int ? double(b.get().integer) : b.get().real;
        if (std::isnan(x) || std::isnan(y)) {
            return -2;
        }
        return compareNumbers(a, b);
    }
    if (a.isText() && b.isText()) {
        const int c = std::strcmp(a.get().text.c_str(), b.get().text.c_str());
        return c < 0 ? -1 : c > 0 ? 1 : 0;
    }
    return -2;
}
} // namespace

int Value::toInt() const {
    const Value& v = get();
    return v.kind == Kind::Int ? v.integer : v.kind == Kind::Float ? truncate(v.real) : 0;
}

float Value::toFloat() const {
    const Value& v = get();
    return v.kind == Kind::Float ? v.real : v.kind == Kind::Int ? float(v.integer) : 0.0f;
}

const std::string& Value::toText() const {
    const Value& v = get();
    return v.kind == Kind::Text || v.kind == Kind::Name ? v.text : kEmpty;
}

bool equal(const Value& a, const Value& b) {
    if (a.get().kind == Value::Kind::Name && b.get().kind == Value::Kind::Name) {
        return a.get().text == b.get().text;
    }
    return relation(a, b) == 0;
}

bool unequal(const Value& a, const Value& b) {
    if (a.get().kind == Value::Kind::Name && b.get().kind == Value::Kind::Name) {
        return a.get().text != b.get().text;
    }
    const int r = relation(a, b);
    return r == -1 || r == 1 || (r == -2 && a.isNumber() && b.isNumber());
}

bool atLeast(const Value& a, const Value& b) {
    const int r = relation(a, b);
    return r == 0 || r == 1;
}

bool atMost(const Value& a, const Value& b) {
    const int r = relation(a, b);
    return r == 0 || r == -1;
}

bool greater(const Value& a, const Value& b) {
    return relation(a, b) == 1;
}

bool less(const Value& a, const Value& b) {
    return relation(a, b) == -1;
}

// --- Compiler -----------------------------------------------------------------

std::vector<Section> compile(const Lscr& s, const std::string& file) {
    std::vector<Section> out;
    std::unordered_map<std::string, Operand> fileNames, sectionNames;
    bool inSection = false;
    Section current;
    for (const LscrCommand& c : s.commands) {
        switch (c.opcode) {
        case 1100: // begin section {name, argument count}
            current = {};
            current.name = c.operands.empty() ? "" : s.text(c.operands[0]);
            current.arguments = c.operands.size() > 1 ? int(std::int32_t(c.operands[1].value)) : 0;
            current.file = file;
            sectionNames.clear();
            inSection = true;
            break;
        case 1101: // end section
            if (inSection) {
                out.push_back(std::move(current));
            }
            inSection = false;
            break;
        case 1102: { // name a value {name (type 3), value}
            if (c.operands.size() < 2 || c.operands[0].type != 3) {
                break;
            }
            Operand o;
            const auto& v = c.operands[1];
            if (v.type == 4) {
                o.variable = true;
                o.slot = int(std::int32_t(v.value));
            } else {
                o.constant = fromOperand(s, v);
            }
            (inSection ? sectionNames : fileNames)[poolText(s, c.operands[0])] = o;
            break;
        }
        case 1110: { // statement {opcode, operands...}
            if (!inSection || c.operands.empty()) {
                break;
            }
            Command cmd;
            cmd.opcode = int(std::int32_t(c.operands[0].value));
            for (size_t i = 1; i < c.operands.size(); ++i) {
                const auto& v = c.operands[i];
                Operand o;
                if (v.type == 4) {
                    o.variable = true;
                    o.slot = int(std::int32_t(v.value));
                } else if (v.type == 3) {
                    // A name: the section's naming first, then the file's;
                    // unnamed names stay names.
                    const std::string name = poolText(s, v);
                    if (auto it = sectionNames.find(name); it != sectionNames.end()) {
                        o = it->second;
                    } else if (auto jt = fileNames.find(name); jt != fileNames.end()) {
                        o = jt->second;
                    } else {
                        o.constant = fromOperand(s, v);
                    }
                } else {
                    o.constant = fromOperand(s, v);
                }
                cmd.operands.push_back(std::move(o));
            }
            current.commands.push_back(std::move(cmd));
            break;
        }
        case 1111: // label {name}
            if (inSection && !c.operands.empty()) {
                const int at = int(current.commands.size());
                current.labels.try_emplace(s.text(c.operands[0]), at);
                current.labelOrder.push_back(at);
            }
            break;
        default:
            break; // file header (0) and the like
        }
    }
    return out;
}

// --- Machine ------------------------------------------------------------------

const Value& Call::at(std::size_t i) const {
    static const Value none;
    return i < args.size() ? args[i].get() : none;
}

Value* Call::variable(std::size_t i) const {
    return i < args.size() && args[i].kind == Value::Kind::Ref ? args[i].ref : nullptr;
}

void Call::jump(std::size_t operand) {
    // 02050624: a text names a label, a number counts labels in order; a
    // missing label continues after the statement.
    int target = frame.pc + 1;
    const Value& v = at(operand);
    if (v.kind == Value::Kind::Text) {
        if (auto it = frame.section->labels.find(v.text); it != frame.section->labels.end()) {
            target = it->second;
        }
    } else {
        const int n = v.toInt();
        if (n >= 0 && n < int(frame.section->labelOrder.size())) {
            target = frame.section->labelOrder[size_t(n)];
        }
    }
    frame.delta = target - frame.pc;
}

bool Call::condition(std::size_t first) const {
    if (first >= args.size()) {
        return false;
    }
    Call copy = *this;
    return machine.evaluate(copy, args.subspan(first));
}

bool Thread::call(const std::string& name, std::span<const Value> arguments,
                  const std::string& label) {
    // 02051a0c: at most eight frames; the frame starts at the label (or at
    // the first statement); arguments are copied by value, at most twelve,
    // and the slot after the last one is set to 0.
    if (frames_.size() >= 8) {
        return false;
    }
    const Section* section = machine_.find(name);
    if (!section) {
        return false;
    }
    Frame f;
    f.section = section;
    f.thread = this;
    f.pc = 0;
    if (!label.empty()) {
        if (auto it = section->labels.find(label); it != section->labels.end()) {
            f.pc = it->second;
        }
    }
    f.delta = 0;
    const size_t n = std::min<size_t>(arguments.size(), 12);
    for (size_t i = 0; i < n; ++i) {
        const Value& v = arguments[i].get();
        f.arguments[i] = v.kind == Value::Kind::Ref ? Value{} : v;
    }
    f.argumentCount = int(n);
    if (n < 12) {
        f.arguments[n] = Value::of(0);
    }
    frames_.push_back(std::move(f));
    return true;
}

bool Thread::replace(const std::string& name, std::span<const Value> arguments,
                     const std::string& label) {
    // 02051b24: the top frame goes first. The arguments may live in that
    // frame, so they are copied before it ends.
    std::vector<Value> copied;
    for (const Value& v : arguments) {
        copied.push_back(v.get());
    }
    if (!frames_.empty()) {
        frames_.pop_back();
    }
    return call(name, copied, label);
}

bool Thread::restart(const std::string& name, std::span<const Value> arguments) {
    std::vector<Value> copied;
    for (const Value& v : arguments) {
        copied.push_back(v.get());
    }
    frames_.clear();
    return call(name, copied);
}

Result Thread::execute(Frame& frame) {
    // 02050678: advance, run the statement, repeat until a command waits,
    // calls, returns or stops; running off the end returns.
    for (;;) {
        frame.pc += frame.delta;
        frame.delta = 1;
        if (frame.pc < 0 || frame.pc >= int(frame.section->commands.size())) {
            return Result::Return;
        }
        const Command& cmd = frame.section->commands[size_t(frame.pc)];
        if (trace) {
            trace(*frame.section, frame.pc, cmd.opcode);
        }
        // 0204ff54 / 0204fd18: variable operands become references to
        // their slots; constants are copies.
        std::vector<Value> operands;
        operands.reserve(cmd.operands.size());
        for (const Operand& o : cmd.operands) {
            if (!o.variable) {
                operands.push_back(o.constant);
                continue;
            }
            Value* slot = nullptr;
            const int s = o.slot;
            if (s >= 0 && s < 100) {
                slot = s < 16 ? &frame.locals[size_t(s)] : nullptr;
            } else if (s >= 100 && s < 200) {
                slot = s - 100 < frame.argumentCount ? &frame.arguments[size_t(s - 100)] : nullptr;
            } else if (s >= 200 && s < 300) {
                slot = s - 200 < 16 ? &variables[size_t(s - 200)] : nullptr;
            } else if (s >= 300) {
                slot = s - 300 < 16 ? &machine_.globals[size_t(s - 300)] : nullptr;
            }
            operands.push_back(slot ? Value::refTo(*slot) : Value::of(0));
        }
        Call call{machine_, *this, frame, operands, cmd.opcode};
        const Result r = machine_.dispatch(call);
        if (cancelled_) {
            return Result::Stop;
        }
        if (r != Result::Next) {
            return r;
        }
    }
}

void Thread::stop() {
    if (executing_) {
        cancelled_ = true;
    } else {
        frames_.clear();
    }
}

void Thread::run(int elapsed) {
    // 02051898.
    executing_ = true;
    while (!frames_.empty()) {
        const Result r = execute(frames_.back());
        if (r == Result::Wait) {
            break;
        }
        if (r == Result::Return) {
            frames_.pop_back(); // 02051b48
        } else if (r == Result::Stop) {
            frames_.clear();
        }
    }
    executing_ = cancelled_ = false;
    clock += elapsed * 4096;
}

const Section* Machine::find(const std::string& name) const {
    auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : it->second;
}

void Machine::load(const Lscr& script, const std::string& file) {
    for (auto& s : compile(script, file)) {
        auto owned = std::make_unique<Section>(std::move(s));
        byName_.try_emplace(owned->name, owned.get());
        sections_.push_back(std::move(owned));
    }
}

Result Machine::dispatch(Call& call) {
    auto it = commands_.find(call.opcode);
    if (it == commands_.end()) {
        throw std::runtime_error("script command " + std::to_string(call.opcode) +
                                 " is not reconstructed yet (section " + call.frame.section->name +
                                 ")");
    }
    return it->second(call);
}

bool Machine::evaluate(Call& call, std::span<Value> operands) {
    if (operands.empty()) {
        return false;
    }
    const int op = operands[0].toInt();
    if (op < 1000) {
        // 02103fe0: 0 is false; any other number that names no condition
        // (scripts pass computed values, e.g. CHR_SWITCH) counts as true.
        return op != 0;
    }
    auto it = conditions_.find(op);
    if (it == conditions_.end()) {
        throw std::runtime_error("script condition " + std::to_string(op) +
                                 " is not reconstructed yet (section " + call.frame.section->name +
                                 ")");
    }
    return it->second(call, operands.subspan(1));
}

} // namespace picarat::lscr
