// The 3DS adventures' script machine: values, loading, frames, threads and
// the machine's own commands (opcodes below 1000). See a56_xq.hpp.
#include "system/a56_xq.hpp"
#include "import/a4_formats.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>

namespace picarat::xq {

namespace {

using Kind = Value::Kind;

// The ARM11's VFP conversions (round toward zero, saturating; NaN gives 0).
std::int32_t floatToInt(float f) {
    if (std::isnan(f)) {
        return 0;
    }
    if (f >= 2147483648.0f) {
        return INT32_MAX;
    }
    if (f <= -2147483648.0f) {
        return INT32_MIN;
    }
    return std::int32_t(f);
}

std::uint32_t floatToUint(float f) {
    if (std::isnan(f) || f <= 0.0f) {
        return 0;
    }
    if (f >= 4294967296.0f) {
        return 0xffffffffu;
    }
    return std::uint32_t(f);
}

// 0x2bf5b4 / its unsigned twin: the library's division. A zero divisor
// calls the division-by-zero hook, which returns 0: the quotient is 0 and
// the remainder the dividend.
std::int32_t divide(std::int32_t a, std::int32_t b) {
    if (b == 0) {
        return 0;
    }
    return std::int32_t(std::int64_t(a) / b);
}

std::int32_t remainder(std::int32_t a, std::int32_t b) {
    if (b == 0) {
        return a;
    }
    return std::int32_t(std::int64_t(a) % b);
}

std::uint32_t divideUnsigned(std::uint32_t a, std::uint32_t b) {
    return b ? a / b : 0;
}

std::uint32_t remainderUnsigned(std::uint32_t a, std::uint32_t b) {
    return b ? a % b : a;
}

// 0x2bf7e0: a mixed integer/float result is stored as an integer when it
// converts back unchanged, else as a float.
Value fromDouble(double d) {
    const double t = std::trunc(d);
    if (!std::isnan(d) && t == d && d >= -2147483648.0 && d <= 2147483647.0) {
        return Value::ofInt(std::int32_t(d));
    }
    return Value::ofFloat(float(d));
}

const Value& noValue() {
    static const Value none;
    return none;
}

// The length in bytes of the Shift-JIS character starting with `c` (the
// test the text routines repeat: (c + 0x7f) & 0xff > 0x1e, then
// (c + 0x20) & 0xff > 0x1c means a single byte).
int characterBytes(unsigned char c) {
    const unsigned char a = (unsigned char)(c + 0x7f);
    if (a > 0x1e) {
        const unsigned char b = (unsigned char)(c + 0x20);
        if (b > 0x1c) {
            return 1;
        }
    }
    return 2;
}

// The game's sprintf for one "%..." segment and one value (0x2bcd50):
// integers pass their bits, texts their characters, floats a double.
std::string format(const std::string& spec, const Value& v) {
    char buffer[512];
    const std::size_t conversion = spec.find_first_of("diouxXcsfFeEgGp", 1);
    const char c = conversion == std::string::npos ? 'd' : spec[conversion];
    if (c == 's') {
        std::snprintf(buffer, sizeof buffer, spec.c_str(), v.isText() ? v.text.c_str() : "");
    } else if (c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == 'g' || c == 'G') {
        std::snprintf(buffer, sizeof buffer, spec.c_str(), v.toDouble());
    } else {
        std::snprintf(buffer, sizeof buffer, spec.c_str(), int(v.bits));
    }
    return buffer;
}

// 0x2bcdf8: the next '%' that starts a conversion ("%%" is skipped), or npos.
std::size_t nextConversion(const std::string& f, std::size_t from) {
    for (std::size_t p = from; p < f.size();) {
        const unsigned char ch = (unsigned char)f[p];
        if (characterBytes(ch) == 2) {
            p += 2;
            continue;
        }
        if (ch == '%') {
            if (p + 1 < f.size() && f[p + 1] == '%') {
                p += 2;
                continue;
            }
            return p;
        }
        ++p;
    }
    return std::string::npos;
}

} // namespace

std::uint32_t hash(std::string_view name) {
    return import::adventure56::nameHash(name);
}

std::uint16_t shortHash(std::string_view name) {
    return import::adventure4::nameHash(name);
}

// ---------------------------------------------------------------- values

Value Value::ofFloat(float v) {
    return {Kind::Float, std::bit_cast<std::uint32_t>(v), {}, {}};
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
    case Kind::Array:
        return !items.empty();
    default:
        return false;
    }
}

// --------------------------------------------------------------- program

Program::Program(std::string name, const import::adventure56::Script& script)
    : name_(std::move(name)) {
    for (const auto& b : script.blocks) {
        Block block;
        block.name = b.name;
        block.hash = b.hash;
        block.first = b.first;
        block.end = b.end;
        block.firstLabel = b.firstLabel;
        block.labelCount = b.labelCount;
        block.temporaries = b.temporaries;
        block.locals = b.locals;
        block.arguments = b.arguments;
        blocks.push_back(std::move(block));
    }
    // The loader relies on the file's order (sorted by hash); keep that
    // invariant even for hand-made files.
    std::stable_sort(blocks.begin(), blocks.end(),
                     [](const Block& a, const Block& b) { return a.hash < b.hash; });
    for (const auto& l : script.labels) {
        labels.push_back({l.name, l.hash, int(l.command)});
    }
    for (const auto& o : script.operands) {
        Operand operand;
        const unsigned kind = o.type & 15;
        if (kind == 4) {
            operand.reg = true;
            operand.value = {Kind::Ref, o.value, {}, {}};
        } else if (kind == 8 || kind == 9) {
            operand.value = {Kind(kind), 0, script.text(o.value), {}};
        } else {
            operand.value = {Kind(kind), o.value, {}, {}};
        }
        operands.push_back(std::move(operand));
    }
    for (const auto& c : script.commands) {
        commands.push_back({c.opcode, std::int16_t(c.group), c.first, c.count});
    }
    statics.resize(script.values);
}

const Program::Block* Program::block(std::uint32_t h) const {
    const auto it = std::lower_bound(blocks.begin(), blocks.end(), h,
                                     [](const Block& b, std::uint32_t v) { return b.hash < v; });
    return it != blocks.end() && it->hash == h ? &*it : nullptr;
}

const Program::Block* Program::block(std::string_view name) const {
    return block(hash(name));
}

int Program::label(const Block& b, std::uint32_t h) const {
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

// ---------------------------------------------------------------- thread

Thread::Thread(Machine& machine, std::size_t count) : values(count), machine_(machine) {
    root_.thread = this;
}

Thread::~Thread() = default;

bool Thread::start(const Program& program, std::string_view block, std::string_view label,
                   std::span<const Value> arguments) {
    if (executing_) {
        // From inside a run: the running blocks are aborted and the start
        // waits until the run returns; reports whether the block exists.
        for (Frame* f = root_.child.get(); f; f = f->child.get()) {
            f->aborted = true;
        }
        const Program* where = &program;
        while (where && !where->block(block)) {
            where = where->next;
        }
        if (where) {
            pending_ = std::make_unique<Pending>(
                Pending{&program, std::string(block), std::string(label),
                        std::vector<Value>(arguments.begin(), arguments.end())});
        }
        return where != nullptr;
    }
    pending_.reset();
    result = {};
    if (!machine_.call(root_, program, hash(block), &result, arguments)) {
        return false;
    }
    program_ = &program;
    if (!label.empty() && root_.child) {
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

std::string Thread::location() const {
    std::string out;
    for (const Frame* f = root_.child.get(); f; f = f->child.get()) {
        if (!out.empty()) {
            out += " > ";
        }
        out += f->program->name() + ":" + f->block->name + "#" + std::to_string(f->pc);
    }
    return out;
}

void Thread::stop() {
    pending_.reset();
    if (executing_) {
        // From inside its own run (a block on the loop's second thread
        // stopping it with 2220 ""): the frames still run, so they are
        // aborted and freed when the run unwinds.
        for (Frame* f = root_.child.get(); f; f = f->child.get()) {
            f->aborted = true;
        }
        return;
    }
    root_.child.reset();
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
        if (!start(*next->program, next->block, next->label, next->arguments)) {
            return;
        }
    }
}

// --------------------------------------------------------------- machine

Machine::Machine() {
    seed(0);
}

void Machine::seed(std::uint32_t value) {
    // 0x26484c: the fixed state, and with a seed the first three words from
    // it (x ^ x >> 30) * 1812433253 + i, the fourth kept. The boot seeds it
    // from a hash of the console's own data and clock (0x381xxx), so no two
    // consoles start alike; the port starts from the unseeded state.
    rng_[0] = 0x6c078966u;
    rng_[1] = 0xdd5254a5u;
    rng_[2] = 0xb9523b81u;
    rng_[3] = 0x03df95b3u;
    if (value != 0) {
        std::uint32_t x = value;
        for (std::uint32_t i = 0; i < 3; ++i) {
            x = (x ^ (x >> 30)) * 0x6c078965u + (i + 1);
            rng_[i] = x;
        }
    }
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
    // 0x2bcea8: the low 24 bits times the float 0x33800001 (just above
    // 2^-24), times x.
    constexpr float kUnit = std::bit_cast<float>(0x33800001u);
    return float(random(0) & 0x00ffffffu) * kUnit * x;
}

bool Machine::call(Frame& caller, const Program& program, std::uint32_t h, Value* result,
                   std::span<const Value> arguments) {
    // 0x22aa28: any frame the caller still has ends first.
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
                // A waiting block's temporaries are cleared.
                std::fill(frame.temporaries.begin(), frame.temporaries.end(), Value{});
                return Result::Wait;
            }
            if (r == Result::Return || r == Result::Stop) {
                return r;
            }
        }
    }
}

bool Machine::jump(Frame& frame, const Value& label) {
    // 0x2c3fcc / 0x288984: to a label of the running block, by hash (an
    // unsigned operand) or by name (a text, hashed).
    std::uint32_t h = 0;
    if (label.kind == Kind::Uint) {
        h = label.bits;
    } else if (label.kind == Kind::Text) {
        h = hash(label.text);
    } else {
        return false;
    }
    const int at = frame.program->label(*frame.block, h);
    if (at < 0) {
        return false;
    }
    frame.pc = at;
    return true;
}

// The machine's own commands (the core table at 0x441928; handler
// addresses in .analysis/adventure56/research/core-table.tsv).
Result Machine::core(Frame& frame, int opcode, Value& result,
                     std::span<const Program::Operand> operands) {
    Call call{*this, *frame.thread, frame, result, operands, opcode};
    const std::size_t n = operands.size();
    // An element of `base` addressed by operands [from, n) (indices; arrays
    // only), or null (0x2d497c index, compared unsigned with the count).
    auto element = [&](Value* base, std::size_t from) -> Value* {
        for (std::size_t i = from; base && i < n; ++i) {
            const std::uint32_t index = std::uint32_t(call.at(i).toInt());
            if (base->kind != Kind::Array || index >= base->items.size()) {
                return nullptr;
            }
            base = &base->items[index];
        }
        return base;
    };
    bool ok = false; // false: the destination is cleared
    switch (opcode) {
    case 10: // 0x155dd0 wait a frame
        result = {};
        return Result::Wait;
    case 11: // 0x15031c return (with a value)
        if (n == 0) {
            result = {};
        } else {
            const Value v = call.at(0);
            result = v;
        }
        return Result::Return;
    case 12: // 0x145dc0 stop the thread
        result = {};
        return Result::Stop;
    case 20:   // 0x145cf4 call a block (its hash, or its name as a text)
    case 21: { // the same handler
        if (n == 0) {
            break;
        }
        std::vector<Value> arguments;
        for (std::size_t i = 1; i < n; ++i) {
            arguments.push_back(call.at(i));
        }
        const Value& name = call.at(0);
        std::uint32_t h = 0;
        if (name.kind == Kind::Uint) {
            h = name.bits;
        } else if (name.kind == Kind::Text) {
            h = hash(name.text);
        }
        const bool called = (name.kind == Kind::Uint || name.kind == Kind::Text) &&
                            frame.thread->program() &&
                            this->call(frame, *frame.thread->program(), h, &result, arguments);
        if (!called && missingBlock) {
            missingBlock(h);
        }
        result = Value::ofInt(called ? 1 : 0);
        return Result::Next;
    }
    case 30: // 0x145e24 jump back and wait while the condition holds
        if (n == 0) {
            break;
        }
        if (n > 1 && call.at(1).truthy()) {
            result = Value::ofInt(jump(frame, call.at(0)) ? 1 : 0);
            return Result::Wait;
        }
        return Result::Next; // not taken: the destination stays
    case 31: {               // 0x145dd0 jump to one of the labels, picked at random
        if (n == 0) {
            break;
        }
        const std::size_t pick = random(std::uint32_t(n));
        result = Value::ofInt(jump(frame, call.at(pick)) ? 1 : 0);
        return Result::Next;
    }
    case 32: // 0x1532b8 jump if (no condition or) the condition holds
    case 33: // 0x158a00 jump unless the condition holds
        if (n >= 2 && call.at(1).truthy() != (opcode == 32)) {
            break; // not taken: the destination is cleared
        }
        result = Value::ofInt(jump(frame, call.at(0)) ? 1 : 0);
        return Result::Next;
    case 40: // 0x158990 the kind of a value
        if (n == 0) {
            break;
        }
        result = Value::ofInt(int(call.at(0).kind));
        return Result::Next;
    case 100: { // 0x1544bc assign (to an element of the destination)
        if (n == 0) {
            break;
        }
        Value* target = element(&result, 1);
        if (!target) {
            break;
        }
        if (&call.at(0) != target) {
            const Value v = call.at(0);
            *target = v;
        }
        return Result::Next;
    }
    case 110: { // 0x155cbc bitwise not
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
    case 111: { // 0x158cf8 the number itself
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
    case 112: { // 0x15c630 negate
        if (n == 0) {
            break;
        }
        const Value& v = call.at(0);
        if (v.kind == Kind::Int || v.kind == Kind::Uint) {
            result = Value::ofInt(-v.toInt());
            ok = true;
        } else if (v.kind == Kind::Float) {
            result = Value::ofFloat(-v.toFloat());
            ok = true;
        }
        break;
    }
    case 120: // 0x154814 not
        if (n == 0) {
            break;
        }
        result = Value::ofInt(call.at(0).truthy() ? 0 : 1);
        return Result::Next;
    case 121: // 0x154754 and
    case 122: // 0x15025c or
        if (n < 2) {
            break;
        }
        result = Value::ofInt(opcode == 121 ? (call.at(0).truthy() && call.at(1).truthy())
                                            : (call.at(0).truthy() || call.at(1).truthy()));
        return Result::Next;
    case 130:   // 0x154880 ==
    case 131:   // 0x1551e0 !=
    case 132:   // 0x154a5c >=
    case 133:   // 0x154e20 <=
    case 134:   // 0x154c44 >
    case 135: { // 0x154ffc <
        if (n < 2) {
            break;
        }
        const Value& a = call.at(0);
        const Value& b = call.at(1);
        if (!a.isNumber() || !b.isNumber()) {
            // Texts compare only with texts, and only for (in)equality.
            if (a.kind == Kind::Text && b.kind == Kind::Text && opcode <= 131) {
                result = Value::ofInt((a.text == b.text) == (opcode == 130) ? 1 : 0);
                return Result::Next;
            }
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
    case 140:   // 0x155bf0 +1
    case 141: { // 0x155b24 -1
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
    case 150: // 0x152698 +
    case 151: // 0x153028 -
    case 152: // 0x152c94 *
    case 153: // 0x152928 /
    case 154: // 0x152acc %
    case 250: // 0x153350 += (to an element of the destination)
    case 251: // 0x1542b8 -=
    case 252: // 0x153dbc *=
    case 253: // 0x153818 /=
    case 254: // 0x153b7c %=
    {
        const bool inPlace = opcode >= 250;
        Value* target = nullptr;
        const Value* x;
        const Value* y;
        if (inPlace) {
            if (n == 0 || !(target = element(&result, 1))) {
                break;
            }
            x = target;
            y = &call.at(0);
        } else {
            if (n < 2) {
                break;
            }
            x = &call.at(0);
            y = &call.at(1);
        }
        const Value& a = *x;
        const Value& b = *y;
        if (!a.isNumber() || !b.isNumber()) {
            break; // texts no longer join (501 formats instead)
        }
        const int op = inPlace ? opcode - 100 : opcode;
        Value out;
        if (a.kind != b.kind && (a.kind == Kind::Float || b.kind == Kind::Float)) {
            const double p = a.toDouble(), q = b.toDouble();
            double d = 0;
            switch (op) {
            case 150:
                d = p + q;
                break;
            case 151:
                d = p - q;
                break;
            case 152:
                d = p * q;
                break;
            case 153:
                d = p / q;
                break;
            default:
                d = q != 0 ? std::fmod(p, q) : p;
                break;
            }
            out = fromDouble(d);
        } else if (a.kind == Kind::Int) {
            const std::int32_t p = a.toInt(), q = b.toInt();
            std::int32_t r = 0;
            switch (op) {
            case 150:
                r = std::int32_t(std::uint32_t(p) + std::uint32_t(q));
                break;
            case 151:
                r = std::int32_t(std::uint32_t(p) - std::uint32_t(q));
                break;
            case 152:
                r = std::int32_t(std::uint32_t(p) * std::uint32_t(q));
                break;
            case 153:
                r = divide(p, q);
                break;
            default:
                r = remainder(p, q);
                break;
            }
            out = Value::ofInt(r);
        } else if (a.kind == Kind::Uint) {
            const std::uint32_t p = a.toUint(), q = b.toUint();
            std::uint32_t r = 0;
            switch (op) {
            case 150:
                r = p + q;
                break;
            case 151:
                r = p - q;
                break;
            case 152:
                r = p * q;
                break;
            case 153:
                r = divideUnsigned(p, q);
                break;
            default:
                r = remainderUnsigned(p, q);
                break;
            }
            out = Value::ofUint(r);
        } else {
            const float p = a.toFloat(), q = b.toFloat();
            float r = 0;
            switch (op) {
            case 150:
                r = p + q;
                break;
            case 151:
                r = p - q;
                break;
            case 152:
                r = p * q;
                break;
            case 153:
                r = p / q;
                break;
            default:
                r = q != 0.0f ? std::fmod(p, q) : p;
                break;
            }
            out = Value::ofFloat(r);
        }
        (inPlace ? *target : result) = std::move(out);
        return Result::Next;
    }
    case 240:   // 0x153a34 ++ in place (operands: indices into the destination)
    case 241: { // 0x1536d0 -- in place
        Value* target = element(&result, 0);
        if (!target) {
            break;
        }
        const int step = opcode == 240 ? 1 : -1;
        if (target->kind == Kind::Int) {
            *target = Value::ofInt(std::int32_t(target->bits + std::uint32_t(step)));
        } else if (target->kind == Kind::Uint) {
            *target = Value::ofUint(target->bits + std::uint32_t(step));
        } else if (target->kind == Kind::Float) {
            *target = Value::ofFloat(target->toFloat() + float(step));
        } else {
            break;
        }
        return Result::Next;
    }
    case 160:   // 0x152824 &
    case 161:   // 0x14ffdc |
    case 162:   // 0x1531b4 ^
    case 170:   // 0x152e20 <<
    case 171:   // 0x152f24 >>
    case 260:   // 0x153554 &= (to an element of the destination)
    case 261:   // 0x1500e0 |=
    case 262:   // 0x1545d8 ^=
    case 270:   // 0x153fc0 <<=
    case 271: { // 0x15413c >>=
        const bool inPlace = opcode >= 260;
        Value* target = nullptr;
        const Value* x;
        const Value* y;
        if (inPlace) {
            if (n == 0 || !(target = element(&result, 1))) {
                break;
            }
            x = target;
            y = &call.at(0);
        } else {
            if (n < 2) {
                break;
            }
            x = &call.at(0);
            y = &call.at(1);
        }
        auto integral = [](const Value& v) {
            return v.kind == Kind::Int || v.kind == Kind::Uint;
        };
        if (!integral(*x) || !integral(*y)) {
            break;
        }
        const int op = inPlace ? opcode - 100 : opcode;
        const Kind kind = x->kind;
        const std::uint32_t p = x->bits, q = y->bits;
        std::uint32_t r = 0;
        switch (op) {
        case 160:
            r = p & q;
            break;
        case 161:
            r = p | q;
            break;
        case 162:
            r = p ^ q;
            break;
        case 170:
            r = (q & 0xff) >= 32 ? 0 : p << (q & 0xff);
            break;
        default:
            if (kind == Kind::Int) {
                r = std::uint32_t(std::int32_t(p) >> std::min<std::uint32_t>(q & 0xff, 31));
            } else {
                r = (q & 0xff) >= 32 ? 0 : p >> (q & 0xff);
            }
            break;
        }
        (inPlace ? *target : result) =
            kind == Kind::Int ? Value::ofInt(std::int32_t(r)) : Value::ofUint(r);
        return Result::Next;
    }
    case 500: // 0x15ca38 clears the destination (scripts use it as a remark)
        break;
    case 501: { // 0x15eee0 format: "...%d...%s..." with the following operands
        const std::string* f = call.text(0);
        if (!f) {
            break;
        }
        if (n < 2) {
            result = Value::ofText(*f);
            return Result::Next;
        }
        std::size_t at = nextConversion(*f, 0);
        std::string out = f->substr(0, at);
        for (std::size_t i = 1; at != std::string::npos && i < n; ++i) {
            const std::size_t next = nextConversion(*f, at + 1);
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
    case 502: { // 0x158e2c the text in full width (0x2c9054)
        const std::string* t = call.text(0);
        if (!t) {
            break;
        }
        // Hypothesis (table at 0x2c91b0 not read): as the DS engine, digits,
        // '+' and '-' become their Shift-JIS full-width forms.
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
    case 503: { // 0x15c710 the character at an index (Shift-JIS)
        const std::string* t = call.text(0);
        if (!t) {
            break;
        }
        std::int32_t index = n > 1 ? call.integer(1) : 0;
        std::size_t p = 0;
        while (index > 0) {
            if (p >= t->size()) {
                break;
            }
            --index;
            p += std::size_t(characterBytes((unsigned char)(*t)[p]));
        }
        if (index > 0) {
            break; // past the end
        }
        result = Value::ofText(
            p < t->size() ? t->substr(p, std::size_t(characterBytes((unsigned char)(*t)[p])))
                          : std::string());
        return Result::Next;
    }
    case 510: // 0x16150c the number of arguments the block was given
        result = Value::ofInt(std::int32_t(frame.arguments.size()));
        return Result::Next;
    case 511: // 0x155d64 as an integer
        if (n == 0) {
            break;
        }
        result = Value::ofInt(call.integer(0));
        return Result::Next;
    case 512: // 0x158dc0 as a truth value
        if (n == 0) {
            break;
        }
        result = Value::ofInt(call.at(0).truthy() ? 1 : 0);
        return Result::Next;
    case 513: // 0x15c9cc as a float
        if (n == 0) {
            break;
        }
        result = Value::ofFloat(call.real(0));
        return Result::Next;
    case 520: { // 0x15ed60 random: a float in [0, 1], below n, or one of the operands
        if (n == 0) {
            result = Value::ofFloat(randomFloat(1.0f));
            return Result::Next;
        }
        if (n > 1) {
            const Value v = call.at(random(std::uint32_t(n)));
            result = v;
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
    case 521: { // 0x15c884 the DS engine's 16-bit hash of a text (numbers: low 16 bits)
        if (n == 0) {
            break;
        }
        const Value& v = call.at(0);
        result = Value::ofUint(v.kind == Kind::Text ? shortHash(v.text) : (v.toUint() & 0xffff));
        return Result::Next;
    }
    case 522: { // 0x15c928 the CRC-32 of a text (numbers as they are)
        if (n == 0) {
            break;
        }
        const Value& v = call.at(0);
        result = Value::ofUint(v.kind == Kind::Text ? hash(v.text) : v.toUint());
        return Result::Next;
    }
    case 530: { // 0x15c6d0 make an array (one size per dimension, 0x2bd78c)
        if (n == 0) {
            break;
        }
        std::vector<std::uint32_t> sizes;
        for (std::size_t i = 0; i < n; ++i) {
            sizes.push_back(call.unsignedInt(i));
        }
        auto make = [&](auto&& self, std::size_t dimension, Value& into) -> bool {
            into = {};
            const std::uint32_t size = sizes[dimension];
            if (size == 0 || size > 0xffff) {
                return false;
            }
            into.kind = Kind::Array;
            into.items.resize(size);
            if (dimension + 1 < sizes.size()) {
                for (Value& item : into.items) {
                    if (!self(self, dimension + 1, item)) {
                        into = {};
                        return false;
                    }
                }
            }
            return true;
        };
        Value made;
        if (!make(make, 0, made)) {
            break;
        }
        result = std::move(made);
        return Result::Next;
    }
    case 531: { // 0x1636f8 an element of an array
        if (n == 0) {
            break;
        }
        const Value* v = &call.at(0);
        for (std::size_t i = 1; v && i < n; ++i) {
            const std::uint32_t index = std::uint32_t(call.integer(i));
            v = v->kind == Kind::Array && index < v->items.size() ? &v->items[index] : nullptr;
        }
        if (!v) {
            break;
        }
        const Value copy = *v;
        result = copy;
        return Result::Next;
    }
    case 600: { // 0x1553bc |x|
        if (n == 0) {
            break;
        }
        const Value& v = call.at(0);
        if (v.kind == Kind::Int) {
            result = Value::ofInt(v.toInt() < 0 ? -v.toInt() : v.toInt());
        } else if (v.kind == Kind::Uint) {
            result = Value::ofUint(v.toUint());
        } else if (v.kind == Kind::Float) {
            result = Value::ofFloat(std::fabs(v.toFloat()));
        } else {
            break;
        }
        return Result::Next;
    }
    case 601: // 0x158c78 square root
    case 602: // 0x15c2c4 floor
    case 603: // 0x15c5b4 round (floor of x + 0.5)
    case 604: // 0x158c00 ceil
    case 608: // 0x15c128 (hypothesis: round toward zero; unused by puzzle scripts)
    case 610: // 0x155a34 sin (radians, single precision)
    case 611: // 0x15549c cos
    case 612: // 0x155aac tan
    case 620: // 0x158b10 (hypothesis: asin)
    case 621: // 0x158a98 (hypothesis: acos)
    case 622: // 0x158b88 (hypothesis: atan)
    case 631: // 0x155514 (hypothesis: exp)
    {
        if (n == 0) {
            break;
        }
        const float x = call.real(0);
        float r = 0;
        switch (opcode) {
        case 601:
            r = std::sqrt(x);
            break;
        case 602:
            r = std::floor(x);
            break;
        case 603:
            r = std::floor(x + 0.5f);
            break;
        case 604:
            r = std::ceil(x);
            break;
        case 608:
            r = std::trunc(x);
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
            r = std::asin(x);
            break;
        case 621:
            r = std::acos(x);
            break;
        case 622:
            r = std::atan(x);
            break;
        default:
            r = std::exp(x);
            break;
        }
        result = Value::ofFloat(r);
        return Result::Next;
    }
    case 623: // 0x15c1e8 atan2(y, x)
    case 630: // 0x155974 (hypothesis: pow(x, y))
        if (n < 2) {
            break;
        }
        result = Value::ofFloat(opcode == 623 ? std::atan2(call.real(0), call.real(1))
                                              : std::pow(call.real(0), call.real(1)));
        return Result::Next;
    case 605:   // 0x155780 min
    case 606: { // 0x15558c max
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
    case 607: { // 0x15c33c clamp(x, low, high)
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

} // namespace picarat::xq
