// The fourth adventure's script machine: values, loading, frames, threads
// and the machine's own commands (opcodes below 1000). See a4_gss.hpp.
#include "system/a4_gss.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace picarat::gss {

namespace {

using Kind = Value::Kind;

// 02029318: float to int, truncated toward zero; out of range (and NaN)
// saturates by sign.
std::int32_t floatToInt(float f) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(f);
    const int shift = 0x9e - int((bits & 0x7fffffff) >> 23);
    if (shift <= 0) {
        return (bits & 0x80000000u) ? INT32_MIN : INT32_MAX;
    }
    if (shift >= 32) {
        return 0;
    }
    const std::uint32_t magnitude = ((bits << 8) | 0x80000000u) >> shift;
    return (bits & 0x80000000u) ? -std::int32_t(magnitude) : std::int32_t(magnitude);
}

// 0202934c: float to unsigned; negative gives 0, too large all ones.
std::uint32_t floatToUint(float f) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(f);
    if (bits & 0x80000000u) {
        return (bits << 1) > 0xff000000u ? 0xffffffffu : 0;
    }
    const int shift = 0x9e - int(bits >> 23);
    if (shift < 0) {
        return 0xffffffffu;
    }
    if (shift >= 32) {
        return 0;
    }
    return ((bits << 8) | 0x80000000u) >> shift;
}

// 02028f28 / 02029134: the library's division; a zero divisor leaves the
// dividend as quotient and 0 as remainder.
std::int32_t divide(std::int32_t a, std::int32_t b) {
    if (b == 0) {
        return a;
    }
    if (a == INT32_MIN && b == -1) {
        return INT32_MIN;
    }
    return a / b;
}

std::int32_t remainder(std::int32_t a, std::int32_t b) {
    if (b == 0 || b == -1) {
        return 0;
    }
    return a % b;
}

std::uint32_t divideUnsigned(std::uint32_t a, std::uint32_t b) {
    return b ? a / b : a;
}

std::uint32_t remainderUnsigned(std::uint32_t a, std::uint32_t b) {
    return b ? a % b : 0;
}

// 01ffa04c (02033860): a mixed integer/float result is stored as an integer
// when it has no fraction (and fits), else as a float.
Value fromDouble(double d) {
    const std::int32_t i = floatToInt(float(d)); // 02028d8c: double to int
    if (double(i) == d) {
        return Value::ofInt(i);
    }
    return Value::ofFloat(float(d));
}

const Value& noValue() {
    static const Value none;
    return none;
}

// The game's sprintf (ARM9 02021ae4) for one conversion: int arguments
// pass their bits, texts their characters.
std::string format(const std::string& spec, const Value& v) {
    char buffer[512];
    // Conversions taking a string get the text; everything else the raw
    // four value bytes, as the game passes them (01ffa5bc).
    const bool wantsText =
        spec.find('s') != std::string::npos && spec.find_first_of("dixXuoc") > spec.find('s');
    if (v.isText()) {
        if (wantsText) {
            std::snprintf(buffer, sizeof buffer, spec.c_str(), v.text.c_str());
        } else {
            std::snprintf(buffer, sizeof buffer, spec.c_str(), 0);
        }
    } else if (wantsText) {
        std::snprintf(buffer, sizeof buffer, spec.c_str(), "");
    } else if (spec.find_first_of("fFeEgG") != std::string::npos &&
               spec.find_first_of("dixXuoc") == std::string::npos) {
        // The game would read a double from the four bytes; no script
        // formats floats this way (hypothesis: unused).
        std::snprintf(buffer, sizeof buffer, spec.c_str(), double(v.toFloat()));
    } else {
        std::snprintf(buffer, sizeof buffer, spec.c_str(), int(v.bits));
    }
    return buffer;
}

// 503: the length in bytes of the Shift-JIS character at `p`.
int characterBytes(unsigned char c) {
    return ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc)) ? 2 : 1;
}

} // namespace

std::uint16_t hash(std::string_view name) {
    return import::adventure4::nameHash(name);
}

// ---------------------------------------------------------------- values

Value Value::ofFloat(float v) {
    return {Kind::Float, std::bit_cast<std::uint32_t>(v), {}};
}

std::int32_t Value::toInt() const {
    switch (kind) {
    case Kind::Int:
    case Kind::Uint:
        return std::int32_t(bits);
    case Kind::Float:
        return floatToInt(std::bit_cast<float>(bits));
    default:
        return 0;
    }
}

std::uint32_t Value::toUint() const {
    switch (kind) {
    case Kind::Int:
    case Kind::Uint:
        return bits;
    case Kind::Float:
        return floatToUint(std::bit_cast<float>(bits));
    default:
        return 0;
    }
}

float Value::toFloat() const {
    switch (kind) {
    case Kind::Int:
        return float(std::int32_t(bits));
    case Kind::Uint:
        return float(bits);
    case Kind::Float:
        return std::bit_cast<float>(bits);
    default:
        return 0;
    }
}

double Value::toDouble() const {
    switch (kind) {
    case Kind::Int:
        return double(std::int32_t(bits));
    case Kind::Uint:
        return double(bits);
    case Kind::Float:
        return double(std::bit_cast<float>(bits));
    default:
        return 0;
    }
}

bool Value::truthy() const {
    switch (kind) {
    case Kind::Int:
    case Kind::Uint:
        return bits != 0;
    case Kind::Float:
        return std::bit_cast<float>(bits) != 0.0f;
    case Kind::Ref:
        return std::int32_t(bits) >= 0;
    case Kind::Text:
    case Kind::Name:
        return true;
    default:
        return false;
    }
}

// --------------------------------------------------------------- program

Program::Program(std::string name, const import::adventure4::Script& script)
    : name_(std::move(name)) {
    for (const auto& b : script.blocks) {
        Block block;
        block.name = script.text(b.name);
        block.hash = b.hash;
        block.first = b.firstCommand;
        block.end = b.endCommand;
        block.firstLabel = b.firstLabel;
        block.labelCount = b.labelCount;
        block.temporaries = b.unknown16;
        block.locals = int(b.unknown32 & 0xffff);
        block.arguments = int(b.unknown32 >> 16);
        blocks.push_back(std::move(block));
    }
    for (const auto& l : script.labels) {
        labels.push_back({script.text(l.name), l.hash, l.command});
    }
    for (const auto& o : script.operands) {
        Operand operand;
        const unsigned kind = o.type & 15;
        if (kind == 4) {
            operand.reg = true;
            operand.value = {Kind::Ref, o.value, {}};
        } else if (kind == 8 || kind == 9) {
            operand.value = {Kind(kind), 0, script.text(o.value)};
        } else {
            operand.value = {Kind(kind), o.value, {}};
        }
        operands.push_back(std::move(operand));
    }
    for (const auto& c : script.commands) {
        commands.push_back({c.opcode, std::int16_t(c.group), c.first, c.count});
    }
    statics.resize(script.values);
}

const Program::Block* Program::block(std::uint16_t h) const {
    const auto it = std::lower_bound(blocks.begin(), blocks.end(), h,
                                     [](const Block& b, std::uint16_t v) { return b.hash < v; });
    return it != blocks.end() && it->hash == h ? &*it : nullptr;
}

const Program::Block* Program::block(std::string_view name) const {
    return block(hash(name));
}

int Program::label(const Block& b, std::uint16_t h) const {
    // The block's labels are sorted by hash (0208bc78 binary-searches them).
    for (int i = b.firstLabel; i < b.firstLabel + b.labelCount; ++i) {
        if (labels[std::size_t(i)].hash == h) {
            return labels[std::size_t(i)].command;
        }
    }
    return -1;
}

// ----------------------------------------------------------------- frame

Value* Frame::reg(int n) {
    if (n >= 4000) {
        const std::size_t i = std::size_t(n - 4000);
        return program && i < program->statics.size() ? const_cast<Value*>(&program->statics[i])
                                                      : nullptr;
    }
    auto pick = [](std::vector<Value>& v, int i) -> Value* {
        return i >= 0 && std::size_t(i) < v.size() ? &v[std::size_t(i)] : nullptr;
    };
    if (n >= 3000) {
        return pick(arguments, n - 3000);
    }
    if (n >= 2000) {
        return pick(locals, n - 2000);
    }
    if (n > 1000) {
        return pick(temporaries, n - 1001);
    }
    if (n == 1000) {
        return result;
    }
    if (n < 0 || !thread) {
        return nullptr;
    }
    return pick(thread->values, n);
}

// ------------------------------------------------------------------ call

const Value& Call::at(std::size_t i) const {
    if (i >= operands.size()) {
        return noValue();
    }
    const auto& o = operands[i];
    if (o.reg) {
        if (Value* v = frame.reg(std::int32_t(o.value.bits))) {
            return *v;
        }
    }
    return o.value;
}

Value* Call::reg(std::size_t i) const {
    if (i >= operands.size() || !operands[i].reg) {
        return nullptr;
    }
    return frame.reg(std::int32_t(operands[i].value.bits));
}

const std::string* Call::text(std::size_t i) const {
    const Value& v = at(i);
    return v.kind == Kind::Text ? &v.text : nullptr;
}

const std::string* Call::name(std::size_t i) const {
    const Value& v = at(i);
    return v.kind == Kind::Name ? &v.text : nullptr;
}

const std::string* Call::textOrName(std::size_t i) const {
    const Value& v = at(i);
    return v.isText() ? &v.text : nullptr;
}

// ---------------------------------------------------------------- thread

Thread::Thread(Machine& machine, std::size_t count) : values(count), machine_(machine) {
    root_.thread = this;
}

Thread::~Thread() = default;

bool Thread::start(const Program& program, std::string_view block, std::string_view label) {
    if (executing_) {
        // 0208c044: the running blocks are aborted (01ffaf58) and the start
        // waits until the current run returns; reports whether the block
        // exists.
        for (Frame* f = root_.child.get(); f; f = f->child.get()) {
            f->aborted = true;
        }
        const Program* where = &program;
        while (where && !where->block(block)) {
            where = where->next;
        }
        if (where) {
            pending_ = std::make_unique<Pending>(
                Pending{&program, std::string(block), std::string(label)});
        }
        return where != nullptr;
    }
    pending_.reset();
    result = {};
    if (!machine_.call(root_, program, hash(block), &result, {})) {
        return false;
    }
    program_ = &program;
    if (!label.empty() && root_.child) {
        // 01ffaf3c: the new block starts at the label.
        const int at = root_.child->program->label(*root_.child->block, hash(label));
        if (at >= 0) {
            root_.child->pc = at;
        }
    }
    return true;
}

bool Thread::callback(std::string_view block, std::span<const Value> arguments) {
    if (!program_) {
        return false;
    }
    Frame* innermost = &root_;
    while (innermost->child) {
        innermost = innermost->child.get();
    }
    callbackResult_ = {};
    return machine_.call(*innermost, *program_, hash(block), &callbackResult_, arguments);
}

void Thread::stop() {
    root_.child.reset();
    pending_.reset();
}

void Thread::run() {
    // A port safeguard (not in the game): a run that never waits stops.
    machine_.budget = 1000000;
    for (;;) {
        if (root_.child) {
            executing_ = true;
            do {
                const Result r = machine_.run(*root_.child);
                if (r == Result::Wait) {
                    break;
                }
                if (r == Result::Return || r == Result::Stop) {
                    root_.child.reset();
                    break;
                }
            } while (root_.child);
            executing_ = false;
        }
        if (!pending_) {
            return;
        }
        const auto next = std::move(pending_);
        if (!start(*next->program, next->block, next->label)) {
            return;
        }
    }
}

// --------------------------------------------------------------- machine

Machine::Machine() {
    seed(0);
}

void Machine::seed(std::uint32_t value) {
    // 0207b698 (hypothesis until read: the reference xorshift128 constants
    // with the seed in the first word).
    rng_[0] = value;
    rng_[1] = 362436069u;
    rng_[2] = 521288629u;
    rng_[3] = 88675123u;
}

std::uint32_t Machine::random(std::uint32_t n) {
    const std::uint32_t t = rng_[0] ^ (rng_[0] << 11);
    rng_[0] = rng_[1];
    rng_[1] = rng_[2];
    rng_[2] = rng_[3];
    rng_[3] = rng_[3] ^ (rng_[3] >> 19) ^ (t ^ (t >> 8));
    return n ? rng_[3] % n : rng_[3];
}

float Machine::randomFloat(float x) {
    return float(random(0) & 0x00ffffffu) / 16777215.0f * x;
}

bool Machine::call(Frame& caller, const Program& program, std::uint16_t h, Value* result,
                   std::span<const Value> arguments) {
    // 01ffaed4 / 01ffaf80: any frame the caller still has ends first.
    caller.child.reset();
    const Program* where = &program;
    const Program::Block* block = nullptr;
    while (where && !(block = where->block(h))) {
        where = where->next;
    }
    if (!block || std::size_t(block->arguments) > arguments.size()) {
        return false;
    }
    auto frame = std::make_unique<Frame>();
    frame->program = where;
    frame->block = block;
    frame->pc = block->first;
    frame->arguments.assign(arguments.begin(), arguments.end());
    frame->locals.resize(std::size_t(block->locals));
    frame->temporaries.resize(std::size_t(block->temporaries));
    frame->result = result;
    frame->thread = caller.thread;
    caller.child = std::move(frame);
    return true;
}

Result Machine::run(Frame& frame) {
    for (;;) {
        if (frame.aborted) {
            return Result::Stop;
        }
        while (frame.child) {
            const Result r = run(*frame.child);
            if (r == Result::Wait) {
                return Result::Wait;
            }
            frame.child.reset();
            if (r == Result::Stop) {
                return Result::Stop;
            }
        }
        if (frame.pc >= frame.block->end) {
            return Result::Return;
        }
        if (budget-- <= 0) {
            if (runaway) {
                runaway(*frame.program, frame.pc);
            }
            return Result::Stop;
        }
        const int index = frame.pc++;
        const auto& command = frame.program->commands[std::size_t(index)];
        if (frame.thread && frame.thread->trace) {
            frame.thread->trace(*frame.program, *frame.block, index);
        }
        if (trace) {
            trace(*frame.program, *frame.block, index);
        }
        Value* destination = frame.reg(command.destination);
        Result r = Result::Next;
        if (destination) {
            const std::span<const Program::Operand> operands(
                frame.program->operands.data() + command.first, std::size_t(command.count));
            if (command.opcode < 1000) {
                r = core(frame, command.opcode, *destination, operands);
            } else if (const auto it = services_.find(command.opcode); it != services_.end()) {
                Call call{*this, *frame.thread, frame, *destination, operands, command.opcode};
                if (!it->second(call)) {
                    *destination = {};
                }
            } else {
                if (missing) {
                    missing(command.opcode, *frame.program, index);
                }
                *destination = {};
            }
        }
        if (!frame.child) {
            if (r == Result::Wait) {
                // 01ffb4e4: a waiting block's temporaries are cleared.
                std::fill(frame.temporaries.begin(), frame.temporaries.end(), Value{});
                return Result::Wait;
            }
            if (r == Result::Return || r == Result::Stop) {
                return r;
            }
        }
    }
}

bool Machine::jump(Frame& frame, const std::string* label) {
    // 01ffb64c: to a label of the running block, by name hash.
    if (!label) {
        return false;
    }
    const int at = frame.program->label(*frame.block, hash(*label));
    if (at < 0) {
        return false;
    }
    frame.pc = at;
    return true;
}

// 01ffb88c: the machine's own commands.
Result Machine::core(Frame& frame, int opcode, Value& result,
                     std::span<const Program::Operand> operands) {
    Call call{*this, *frame.thread, frame, result, operands, opcode};
    const std::size_t n = operands.size();
    auto numeric = [](const Value& v) {
        return v.isNumber();
    };
    bool ok = false; // false: the destination is cleared
    switch (opcode) {
    case 10: // wait a frame
        result = {};
        return Result::Wait;
    case 11: // return (with a value)
        if (n == 0) {
            result = {};
        } else {
            result = call.at(0);
        }
        return Result::Return;
    case 12: // stop the thread
        result = {};
        return Result::Stop;
    case 20:   // call a block by name
    case 21: { // ... whose name is in a register
        if (n == 0) {
            break;
        }
        std::vector<Value> arguments;
        for (std::size_t i = 1; i < n; ++i) {
            arguments.push_back(call.at(i));
        }
        const std::string* name = call.textOrName(0);
        const bool called =
            name && frame.thread->program() &&
            this->call(frame, *frame.thread->program(), hash(*name), &result, arguments);
        result = Value::ofInt(called ? 1 : 0);
        return Result::Next;
    }
    case 30: // jump back and wait while the condition holds
        if (n == 0) {
            break;
        }
        if (n > 1 && call.at(1).truthy()) {
            result = Value::ofInt(jump(frame, call.text(0)) ? 1 : 0);
            return Result::Wait;
        }
        return Result::Next;
    case 31: { // jump to one of the labels, picked at random
        if (n == 0) {
            break;
        }
        const std::size_t pick = random(std::uint32_t(n));
        result = Value::ofInt(jump(frame, call.text(pick)) ? 1 : 0);
        return Result::Next;
    }
    case 32: // jump if (no condition or) the condition holds
    case 33: // jump unless the condition holds
        if (n == 0) {
            break;
        }
        if (n < 2 || call.at(1).truthy() == (opcode == 32)) {
            result = Value::ofInt(jump(frame, call.text(0)) ? 1 : 0);
            return Result::Next;
        }
        break; // not taken: the destination is cleared
    case 100:  // assign
        if (n == 0) {
            break;
        }
        if (&call.at(0) != &result) {
            result = call.at(0);
        }
        return Result::Next;
    case 110: { // bitwise not
        if (n == 0) {
            break;
        }
        const Value& v = call.at(0);
        if (v.kind == Kind::Uint) {
            result = Value::ofUint(~v.toUint());
            ok = true;
        } else if (v.kind == Kind::Int || v.kind == Kind::Float) {
            result = Value::ofInt(~v.toInt());
            ok = true;
        }
        break;
    }
    case 111: { // the number itself
        if (n == 0) {
            break;
        }
        const Value& v = call.at(0);
        if (v.kind == Kind::Int) {
            result = Value::ofInt(v.toInt());
            ok = true;
        } else if (v.kind == Kind::Uint) {
            result = Value::ofUint(v.toUint());
            ok = true;
        } else if (v.kind == Kind::Float) {
            result = Value::ofFloat(v.toFloat());
            ok = true;
        }
        break;
    }
    case 112: { // negate
        if (n == 0) {
            break;
        }
        const Value& v = call.at(0);
        if (v.kind == Kind::Int || v.kind == Kind::Uint) {
            result = Value::ofInt(-v.toInt());
            ok = true;
        } else if (v.kind == Kind::Float) {
            result = Value::ofFloat(0.0f - v.toFloat());
            ok = true;
        }
        break;
    }
    case 120: // not
        if (n == 0) {
            break;
        }
        result = Value::ofInt(call.at(0).truthy() ? 0 : 1);
        return Result::Next;
    case 121: // and
    case 122: // or
        if (n < 2) {
            break;
        }
        result = Value::ofInt(opcode == 121 ? (call.at(0).truthy() && call.at(1).truthy())
                                            : (call.at(0).truthy() || call.at(1).truthy()));
        return Result::Next;
    case 130:   // ==
    case 131:   // !=
    case 132:   // >=
    case 133:   // <=
    case 134:   // >
    case 135: { // <
        if (n < 2) {
            break;
        }
        const Value& a = call.at(0);
        const Value& b = call.at(1);
        if (a.kind == Kind::Text || b.kind == Kind::Text) {
            // Texts compare only with texts, and only for (in)equality.
            if (a.kind == Kind::Text && b.kind == Kind::Text && opcode <= 131) {
                result = Value::ofInt((a.text == b.text) == (opcode == 130) ? 1 : 0);
                return Result::Next;
            }
            break;
        }
        if (!numeric(a) || !numeric(b)) {
            break;
        }
        int c; // -1, 0, 1 (or 2: unordered)
        if (a.kind != b.kind && (a.kind == Kind::Float || b.kind == Kind::Float)) {
            const double x = a.toDouble(), y = b.toDouble();
            c = x < y ? -1 : x > y ? 1 : x == y ? 0 : 2;
        } else if (a.kind == Kind::Int) {
            const std::int32_t x = a.toInt(), y = b.toInt();
            c = x < y ? -1 : x > y ? 1 : 0;
        } else if (a.kind == Kind::Uint) {
            const std::uint32_t x = a.toUint(), y = b.toUint();
            c = x < y ? -1 : x > y ? 1 : 0;
        } else {
            const float x = a.toFloat(), y = b.toFloat();
            c = x < y ? -1 : x > y ? 1 : x == y ? 0 : 2;
        }
        bool r = false;
        switch (opcode) {
        case 130:
            r = c == 0;
            break;
        case 131:
            r = c != 0;
            break;
        case 132:
            r = c == 0 || c == 1;
            break;
        case 133:
            r = c == 0 || c == -1;
            break;
        case 134:
            r = c == 1;
            break;
        default:
            r = c == -1;
            break;
        }
        result = Value::ofInt(r ? 1 : 0);
        return Result::Next;
    }
    case 140:   // +1
    case 141: { // -1
        if (n == 0) {
            break;
        }
        const Value& v = call.at(0);
        const int step = opcode == 140 ? 1 : -1;
        if (v.kind == Kind::Int) {
            result = Value::ofInt(std::int32_t(v.bits + std::uint32_t(step)));
            ok = true;
        } else if (v.kind == Kind::Uint) {
            result = Value::ofUint(v.bits + std::uint32_t(step));
            ok = true;
        } else if (v.kind == Kind::Float) {
            result = Value::ofFloat(v.toFloat() + float(step));
            ok = true;
        }
        break;
    }
    case 150:   // +
    case 151:   // -
    case 152:   // *
    case 153:   // /
    case 154: { // %
        if (n < 2) {
            break;
        }
        const Value& a = call.at(0);
        const Value& b = call.at(1);
        if (a.kind == Kind::Text || b.kind == Kind::Text) {
            if (opcode != 150) {
                break;
            }
            // Text joins: numbers are written with %d / %f.
            auto piece = [](const Value& v) -> std::string {
                if (v.kind == Kind::Text) {
                    return v.text;
                }
                char buffer[64] = "";
                if (v.kind == Kind::Int || v.kind == Kind::Uint) {
                    std::snprintf(buffer, sizeof buffer, "%d", int(v.bits));
                } else if (v.kind == Kind::Float) {
                    std::snprintf(buffer, sizeof buffer, "%f", double(v.toFloat()));
                }
                return buffer;
            };
            result = Value::ofText(piece(a) + piece(b));
            return Result::Next;
        }
        if (!numeric(a) || !numeric(b)) {
            break;
        }
        if (a.kind != b.kind && (a.kind == Kind::Float || b.kind == Kind::Float)) {
            const double x = a.toDouble(), y = b.toDouble();
            double d = 0;
            switch (opcode) {
            case 150:
                d = x + y;
                break;
            case 151:
                d = x - y;
                break;
            case 152:
                d = x * y;
                break;
            case 153:
                d = x / y;
                break;
            default:
                d = std::fmod(x, y);
                break;
            }
            result = fromDouble(d);
            return Result::Next;
        }
        if (a.kind == Kind::Int) {
            const std::int32_t x = a.toInt(), y = b.toInt();
            std::int32_t r = 0;
            switch (opcode) {
            case 150:
                r = std::int32_t(std::uint32_t(x) + std::uint32_t(y));
                break;
            case 151:
                r = std::int32_t(std::uint32_t(x) - std::uint32_t(y));
                break;
            case 152:
                r = std::int32_t(std::uint32_t(x) * std::uint32_t(y));
                break;
            case 153:
                r = divide(x, y);
                break;
            default:
                r = remainder(x, y);
                break;
            }
            result = Value::ofInt(r);
        } else if (a.kind == Kind::Uint) {
            const std::uint32_t x = a.toUint(), y = b.toUint();
            std::uint32_t r = 0;
            switch (opcode) {
            case 150:
                r = x + y;
                break;
            case 151:
                r = x - y;
                break;
            case 152:
                r = x * y;
                break;
            case 153:
                r = divideUnsigned(x, y);
                break;
            default:
                r = remainderUnsigned(x, y);
                break;
            }
            result = Value::ofUint(r);
        } else {
            const float x = a.toFloat(), y = b.toFloat();
            float r = 0;
            switch (opcode) {
            case 150:
                r = x + y;
                break;
            case 151:
                r = x - y;
                break;
            case 152:
                r = x * y;
                break;
            case 153:
                r = x / y;
                break;
            default:
                r = float(std::fmod(double(x), double(y)));
                break;
            }
            result = Value::ofFloat(r);
        }
        return Result::Next;
    }
    case 160:   // &
    case 161:   // |
    case 162:   // ^
    case 170:   // <<
    case 171: { // >>
        if (n < 2) {
            break;
        }
        const Value& a = call.at(0);
        const Value& b = call.at(1);
        const bool integral = [](const Value& v) {
            return v.kind == Kind::Int || v.kind == Kind::Uint;
        }(a) && (b.kind == Kind::Int || b.kind == Kind::Uint);
        if (!integral) {
            break;
        }
        const std::uint32_t x = a.bits, y = b.bits;
        std::uint32_t r = 0;
        switch (opcode) {
        case 160:
            r = x & y;
            break;
        case 161:
            r = x | y;
            break;
        case 162:
            r = x ^ y;
            break;
        case 170:
            r = (y & 0xff) >= 32 ? 0 : x << (y & 0xff);
            break;
        default:
            if (a.kind == Kind::Int) {
                r = std::uint32_t(std::int32_t(x) >> std::min<std::uint32_t>(y & 0xff, 31));
            } else {
                r = (y & 0xff) >= 32 ? 0 : x >> (y & 0xff);
            }
            break;
        }
        result = a.kind == Kind::Int ? Value::ofInt(std::int32_t(r)) : Value::ofUint(r);
        return Result::Next;
    }
    case 500: // leaves the destination as it is
        return Result::Next;
    case 501: { // format: "...%d...%s..." with the following operands
        const std::string* f = call.text(0);
        if (!f) {
            break;
        }
        if (n < 2) {
            result = Value::ofText(*f);
            return Result::Next;
        }
        std::size_t at = f->find('%');
        std::string out = f->substr(0, at);
        for (std::size_t i = 1; at != std::string::npos && i < n; ++i) {
            const std::size_t next = f->find('%', at + 1);
            const std::string spec =
                f->substr(at, next == std::string::npos ? std::string::npos : next - at);
            const Value& v = call.at(i);
            out += v.kind == Kind::None ? spec : format(spec, v);
            at = next;
        }
        if (out.size() > 0x3ff) {
            out.resize(0x3ff);
        }
        result = Value::ofText(std::move(out));
        return Result::Next;
    }
    case 502: { // the text in full width (0207e7e4)
        const std::string* t = call.text(0);
        if (!t) {
            break;
        }
        // Digits, '+' and '-' become their Shift-JIS full-width forms (the
        // tables at 020b5418, 020b53f4 and 020b53f8); double-byte
        // characters and everything else are copied.
        std::string out;
        for (std::size_t p = 0; p < t->size();) {
            const unsigned char ch = (unsigned char)(*t)[p];
            const int bytes = characterBytes(ch);
            if (bytes == 2) {
                out += t->substr(p, 2);
            } else if (ch >= '0' && ch <= '9') {
                out += '\x82';
                out += char(0x4f + (ch - '0'));
            } else if (ch == '+') {
                out += "\x81\x7b";
            } else if (ch == '-') {
                out += "\x81\x7c";
            } else {
                out += char(ch);
            }
            p += std::size_t(bytes);
        }
        result = Value::ofText(out);
        return Result::Next;
    }
    case 503: { // the character at an index (Shift-JIS)
        const std::string* t = call.text(0);
        if (!t) {
            break;
        }
        std::int32_t index = n > 1 ? call.integer(1) : 0;
        std::size_t p = 0;
        while (index > 0 && p < t->size()) {
            --index;
            p += std::size_t(characterBytes((unsigned char)(*t)[p]));
        }
        if (index != 0) {
            break;
        }
        result = Value::ofText(
            p < t->size() ? t->substr(p, std::size_t(characterBytes((unsigned char)(*t)[p])))
                          : std::string());
        return Result::Next;
    }
    case 510: // the number of arguments the block was given
        result = Value::ofInt(std::int32_t(frame.arguments.size()));
        return Result::Next;
    case 520: { // random: a float in [0, 1], below n, or one of the operands
        if (n == 0) {
            result = Value::ofFloat(randomFloat(1.0f));
            return Result::Next;
        }
        if (n > 1) {
            result = call.at(random(std::uint32_t(n)));
            return Result::Next;
        }
        const Value& v = call.at(0);
        if (v.kind == Kind::Int) {
            result = Value::ofInt(std::int32_t(random(std::uint32_t(std::max(v.toInt(), 0)))));
            return Result::Next;
        }
        if (v.kind == Kind::Uint) {
            result = Value::ofUint(random(v.toUint()));
            return Result::Next;
        }
        if (v.kind == Kind::Float) {
            result = Value::ofFloat(randomFloat(v.toFloat()));
            return Result::Next;
        }
        break;
    }
    case 600: // |x|
    case 602: // floor
    case 603: // round (floor of x + 0.5)
    case 604: // ceil
    case 610: // sin
    case 611: // cos
    case 612: // tan
    case 620:
    case 621:
    case 622: { // inverse trigonometry (not used by any script)
        if (n == 0) {
            break;
        }
        const double x = opcode == 603 ? double(call.real(0) + 0.5f) : double(call.real(0));
        double r = 0;
        switch (opcode) {
        case 600:
            r = std::fabs(x);
            break;
        case 602:
        case 603:
            r = std::floor(x);
            break;
        case 604:
            r = std::ceil(x);
            break;
        case 610:
            r = std::sin(x);
            break;
        case 611:
            r = std::cos(x);
            break;
        case 612:
            r = std::tan(x);
            break;
        case 620:
            r = std::asin(x); // hypothesis: ARM9 02023628 not identified
            break;
        case 621:
            r = std::acos(x); // hypothesis: 02022e14 (checks |x| against 1)
            break;
        default:
            r = std::atan(x); // hypothesis: 02026a78
            break;
        }
        result = Value::ofFloat(float(r));
        return Result::Next;
    }
    case 601: // square root (float)
        if (n == 0) {
            break;
        }
        result = Value::ofFloat(std::sqrt(call.real(0)));
        return Result::Next;
    case 623: // atan2(y, x)
        if (n < 2) {
            break;
        }
        result = Value::ofFloat(float(std::atan2(double(call.real(0)), double(call.real(1)))));
        return Result::Next;
    case 605:   // min
    case 606: { // max
        if (n == 0) {
            break;
        }
        const Value& first = call.at(0);
        auto better = [&](auto x, auto y) {
            return opcode == 605 ? std::min(x, y) : std::max(x, y);
        };
        if (first.kind == Kind::Int) {
            std::int32_t r = first.toInt();
            for (std::size_t i = 1; i < n; ++i) {
                r = better(r, call.integer(i));
            }
            result = Value::ofInt(r);
        } else if (first.kind == Kind::Uint) {
            std::uint32_t r = first.toUint();
            for (std::size_t i = 1; i < n; ++i) {
                r = better(r, call.unsignedInt(i));
            }
            result = Value::ofUint(r);
        } else if (first.kind == Kind::Float) {
            float r = first.toFloat();
            for (std::size_t i = 1; i < n; ++i) {
                r = better(r, call.real(i));
            }
            result = Value::ofFloat(r);
        } else {
            break;
        }
        return Result::Next;
    }
    case 607: { // clamp(x, low, high)
        if (n < 3) {
            break;
        }
        const Value& first = call.at(0);
        if (first.kind == Kind::Int) {
            result =
                Value::ofInt(std::min(std::max(first.toInt(), call.integer(1)), call.integer(2)));
        } else if (first.kind == Kind::Uint) {
            result = Value::ofUint(
                std::min(std::max(first.toUint(), call.unsignedInt(1)), call.unsignedInt(2)));
        } else if (first.kind == Kind::Float) {
            result =
                Value::ofFloat(std::min(std::max(first.toFloat(), call.real(1)), call.real(2)));
        } else {
            break;
        }
        return Result::Next;
    }
    default:
        break;
    }
    if (!ok) {
        result = {};
    }
    return Result::Next;
}

} // namespace picarat::gss
