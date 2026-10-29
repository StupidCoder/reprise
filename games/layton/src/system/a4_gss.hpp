#pragma once
// The fourth adventure's script machine: runs the game's GSS1 scripts
// (events, every menu, the puzzle frame and each puzzle's own script).
//
// How the game's scripts work
// ---------------------------
// A script file holds named blocks (functions) with their labels. Every
// command names a destination register for its result, an opcode and its
// operands; opcodes below 1000 belong to the machine (flow, arithmetic,
// texts), the others are the game's services (objects, messages, sound...),
// bound by number when the file is loaded.
//
// Registers are numbered: 0..999 the thread's own values (set up by whoever
// owns the thread, e.g. a puzzle's number), 1000 the register the caller
// wants the block's result in, 1001.. the block's temporaries (cleared
// whenever the thread waits), 2000.. its locals, 3000.. its arguments and
// 4000.. values that belong to the script file. A register operand passes
// the register itself, so commands can write their operands.
//
// Values are integers (signed or unsigned), floats, texts and names. A
// command's result replaces its destination; a command that fails leaves
// "nothing" there.
//
// A thread runs its innermost block until a command waits (resumed on the
// next frame), a block ends (its caller continues at once) or the thread
// stops. Calling a block by name looks it up in the thread's script and
// then in the scripts that script is linked to.
//
// Reconstructed from the resident code the ARM9 image copies to ITCM
// (01ff8000..; listed as ARM9 file offsets 0x31a00..): frame run 01ffb6e4,
// core commands 01ffb88c, register lookup 01ffb424, call 01ffae80/01ffaf80,
// label jump 01ffb64c, values 01ff95c4..01ffa940; the loader ov6 0208b8a0,
// block lookup 0208bb60/0208bc24, label lookup 0208bc78 and the thread
// object 0208be48..0208c1f0. See .analysis/adventure4/docs/script-machine.md.
#include "import/a4_formats.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace picarat::gss {

// A script value (5 bytes in the game: a kind byte and four value bytes).
struct Value {
    enum class Kind : std::uint8_t {
        None = 0,
        Int = 1,   // signed 32-bit
        Uint = 2,  // unsigned 32-bit (own division, comparison, shifts)
        Float = 3, // IEEE single
        Ref = 4,   // a register number (an operand whose register is missing)
        Text = 8,
        Name = 9,
    };
    Kind kind = Kind::None;
    std::uint32_t bits = 0; // Int, Uint, Float (raw bits), Ref
    std::string text;       // Text and Name

    static Value ofInt(std::int32_t v) { return {Kind::Int, std::uint32_t(v), {}}; }

    static Value ofUint(std::uint32_t v) { return {Kind::Uint, v, {}}; }

    static Value ofFloat(float v);

    static Value ofText(std::string v) { return {Kind::Text, 0, std::move(v)}; }

    static Value ofName(std::string v) { return {Kind::Name, 0, std::move(v)}; }

    bool isNumber() const { return kind == Kind::Int || kind == Kind::Uint || kind == Kind::Float; }

    bool isText() const { return kind == Kind::Text || kind == Kind::Name; }

    // 01ff9934: integers as they are, floats truncated toward zero; else 0.
    std::int32_t toInt() const;
    // 01ff99fc: the same for unsigned results.
    std::uint32_t toUint() const;
    // 01ff9ac4: floats as they are, integers converted; else 0.
    float toFloat() const;
    // 01ff9b90: as a double (mixed integer/float arithmetic).
    double toDouble() const;
    // 01ff97a4: numbers when not 0; texts and names always; a register
    // number when it is not negative.
    bool truthy() const;
};

// A loaded script file (ov6 0208b8a0).
class Program {
public:
    struct Block {
        std::string name;
        std::uint16_t hash = 0;
        int first = 0, end = 0; // its commands, end exclusive
        int firstLabel = 0, labelCount = 0;
        int temporaries = 0; // registers 1001..
        int locals = 0;      // registers 2000..
        int arguments = 0;   // fewest arguments a call must pass
    };

    struct Label {
        std::string name;
        std::uint16_t hash = 0;
        int command = 0;
    };

    struct Operand {
        bool reg = false; // a register operand (kind 4): `value.bits` is its number
        Value value;      // the constant (texts read from the file)
    };

    struct Command {
        int opcode = 0;
        int destination = 1000;   // register for the result
        int first = 0, count = 0; // operands
    };

    Program(std::string name, const import::adventure4::Script& script);

    // An empty program for tests; fill blocks/labels/commands/operands.
    explicit Program(std::string name) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }

    // 0208bb60: the block with this name hash (binary search), or null.
    const Block* block(std::uint16_t hash) const;
    const Block* block(std::string_view name) const;
    // 0208bc78: the command index of a label of `block`, or -1.
    int label(const Block& block, std::uint16_t hash) const;

    // Scripts a block lookup continues in when this one lacks the block
    // (0208bc24 follows the script's link at +8).
    const Program* next = nullptr;

    std::vector<Block> blocks;
    std::vector<Label> labels;
    std::vector<Command> commands;
    std::vector<Operand> operands;
    // Registers 4000..: one set per loaded file, kept between calls.
    std::vector<Value> statics;

private:
    std::string name_;
};

class Machine;
class Thread;
struct Frame;

// What running a frame or a command reports (01ffb6e4, 01ffb88c).
enum class Result {
    Next = 0,   // continue with the next command
    Wait = 1,   // stop for this frame; resume here on the next
    Return = 2, // the block ends
    Stop = 3,   // the thread ends
};

// A running block (0x48-byte frame objects in the game, 01ffb134).
struct Frame {
    const Program* program = nullptr;
    const Program::Block* block = nullptr;
    int pc = 0;
    std::vector<Value> arguments, locals, temporaries;
    Value* result = nullptr; // register 1000: where the caller wants the result
    std::unique_ptr<Frame> child;
    Thread* thread = nullptr;
    bool aborted = false; // +0x44 bit 15 (01ffaf58): the frame stops

    // 01ffb424: the register, or null when it does not exist.
    Value* reg(int number);
};

// The context of a service command: its operands, its destination and the
// thread it runs in. Service handlers return false when they fail (the
// destination is then cleared, as the game does).
struct Call {
    Machine& machine;
    Thread& thread;
    Frame& frame;
    Value& result;
    std::span<const Program::Operand> operands;
    int opcode = 0;

    std::size_t count() const { return operands.size(); }

    // 01ffac20: the operand's value (a register's content; a missing
    // register reads as its number, kind Ref).
    const Value& at(std::size_t i) const;
    // The register an operand names, or null (writes go there).
    Value* reg(std::size_t i) const;

    std::int32_t integer(std::size_t i) const { return at(i).toInt(); }

    std::uint32_t unsignedInt(std::size_t i) const { return at(i).toUint(); }

    float real(std::size_t i) const { return at(i).toFloat(); }

    // 01ffabd8: a text operand (kind 8 only), else null.
    const std::string* text(std::size_t i) const;
    // 01ffabf0: a name operand (kind 9 only), else null.
    const std::string* name(std::size_t i) const;
    // 01ffac08: a text or a name, else null.
    const std::string* textOrName(std::size_t i) const;
};

using Service = std::function<bool(Call&)>;

// A thread of script execution (ov6 0208be48..0208c1f0: the thread object
// with its values at +0x44, its root frame at +0x1c, the result register
// at +0x38 and a deferred start at +0x4c).
class Thread {
public:
    // `values`: registers 0..values-1.
    Thread(Machine& machine, std::size_t values);
    ~Thread();

    // 0208c044: starts `block` of `program` (searching the linked scripts
    // too), at `label` when given; any running block ends. From inside the
    // thread's own run the start is deferred until the run waits or ends.
    bool start(const Program& program, std::string_view block, std::string_view label = {});
    // 0208c150: calls a block from the innermost running frame (a
    // callback: the interrupted block resumes when it returns).
    bool callback(std::string_view block, std::span<const Value> arguments = {});
    // 0208c1d0: ends every block.
    void stop();

    // 0208c1f0: runs until a command waits or nothing is left to run.
    void run();

    bool running() const { return root_.child != nullptr; }

    bool executing() const { return executing_; }

    Machine& machine() const { return machine_; }

    const Program* program() const { return program_; }

    std::vector<Value> values; // registers 0..999
    Value result;              // what the started block returned (+0x38)

    // Optional trace of executed commands: (program, block, command index).
    std::function<void(const Program&, const Program::Block&, int)> trace;

private:
    friend class Machine;
    Machine& machine_;
    Frame root_;
    const Program* program_ = nullptr;
    Value callbackResult_; // +0x3d
    bool executing_ = false;

    struct Pending {
        const Program* program = nullptr;
        std::string block, label;
    };

    std::unique_ptr<Pending> pending_;
};

// The machine: the service table and the random numbers (xorshift128 at
// ov6 020b66e0, 0207b6fc).
class Machine {
public:
    Machine();

    void service(int opcode, Service handler) { services_[opcode] = std::move(handler); }

    bool hasService(int opcode) const { return services_.contains(opcode); }

    // Commands the reconstruction has not rebuilt yet raise this (the game
    // would clear the destination of an unbound opcode).
    std::function<void(int opcode, const Program&, int command)> missing;
    // Optional trace of every thread's executed commands (research).
    std::function<void(const Program&, const Program::Block&, int)> trace;
    // A port safeguard: commands a thread's run may still execute before it
    // is stopped (set per run); reported here.
    std::int64_t budget = 1000000;
    std::function<void(const Program&, int command)> runaway;

    // 0207b734: the next random number; below `n` when n > 0 (w % n).
    std::uint32_t random(std::uint32_t n);
    // 0207b754: a float in [0, x] ((w & 0xffffff) / 16777215 * x).
    float randomFloat(float x);
    void seed(std::uint32_t value); // 0207b698

    // The generator's four words (020b66e0; research: matching a capture).
    std::array<std::uint32_t, 4> state() const { return {rng_[0], rng_[1], rng_[2], rng_[3]}; }

    void setState(const std::array<std::uint32_t, 4>& s) {
        for (std::size_t i = 0; i < 4; ++i) {
            rng_[i] = s[i];
        }
    }

    // 01ffaed4: pushes a frame calling `block` (looked up from `program`
    // on) as the child of `caller`; false when the block is unknown or
    // given too few arguments. The result goes to `result`.
    bool call(Frame& caller, const Program& program, std::uint16_t hash, Value* result,
              std::span<const Value> arguments);
    // 01ffb6e4: runs a frame (and its callees) until it waits or ends.
    Result run(Frame& frame);

private:
    Result core(Frame& frame, int opcode, Value& result,
                std::span<const Program::Operand> operands);
    bool jump(Frame& frame, const std::string* label);
    std::unordered_map<int, Service> services_;
    std::uint32_t rng_[4] = {};
};

// The block name hash (CRC-16/X.25, ov6 020b3f84).
std::uint16_t hash(std::string_view name);

} // namespace picarat::gss
