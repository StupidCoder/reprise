#pragma once
// The 3DS adventures' script machine: runs the games' XQ32 scripts (every
// menu, event, the puzzle frame and each puzzle's own script).
//
// How the scripts work
// --------------------
// The design is the fourth adventure's GSS1 machine widened to 32 bits (see
// a4_gss.hpp): a script file holds named blocks (functions) with labels;
// every command names a destination register, an opcode and its operands.
// Opcodes below 1000 belong to the machine (flow, arithmetic, texts,
// arrays), the others are the game's services, bound by number when the
// file is loaded.
//
// Registers: 0..999 the thread's own values, 1000 the register the caller
// wants the block's result in, 1001.. the block's temporaries (cleared
// whenever the thread waits), 2000.. its locals, 3000.. the arguments it
// was given and 4000.. values that belong to the script file. A register
// operand passes the register itself, so commands can write their operands.
//
// New against the DS machine: values can be arrays (opcode 530 makes one,
// 531 reads an element); assignments and the in-place operators (240..271)
// address an element of the destination when given indices; block names
// and labels are CRC-32 hashes, usually stored as unsigned operands.
//
// Reconstructed from the fifth adventure's code (.code at 0x00100000):
// loader 0x2810a4 (binds opcodes < 1000 to the core table built at 0x441928,
// the others to the service tables), block lookup 0x280fc4, frame run
// 0x24da70, register lookup 0x2c4108, call 0x22aa28 / 0x280e80, frame set-up
// 0x39c4d4, label jump 0x288984, value copy 0x2c046c, the core commands at
// 0x14ffdc..0x1636f8 (research/core-table.tsv). See
// .analysis/adventure56/docs/script-machine.md.
#include "import/a56_formats.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace picarat::xq {

// A script value (8 bytes in the game: a kind halfword, a count halfword
// for arrays and four value bytes).
struct Value {
    enum class Kind : std::uint8_t {
        None = 0,
        Int = 1,   // signed 32-bit
        Uint = 2,  // unsigned 32-bit (also block and label hashes)
        Float = 3, // IEEE single
        Ref = 4,   // a register number (an operand whose register is missing)
        Text = 8,
        Name = 9,
        Array = 10,
    };
    Kind kind = Kind::None;
    std::uint32_t bits = 0;   // Int, Uint, Float (raw bits), Ref
    std::string text;         // Text and Name
    std::vector<Value> items; // Array

    static Value ofInt(std::int32_t v) { return {Kind::Int, std::uint32_t(v), {}, {}}; }

    static Value ofUint(std::uint32_t v) { return {Kind::Uint, v, {}, {}}; }

    static Value ofFloat(float v);

    static Value ofText(std::string v) { return {Kind::Text, 0, std::move(v), {}}; }

    bool isNumber() const { return kind == Kind::Int || kind == Kind::Uint || kind == Kind::Float; }

    bool isText() const { return kind == Kind::Text; }

    // 0x2d497c: integers as they are, floats truncated toward zero; else 0.
    std::int32_t toInt() const;
    // 0x2d5e64: the same for unsigned results.
    std::uint32_t toUint() const;
    // 0x2bf8c4: floats as they are, integers converted; else 0.
    float toFloat() const;
    // 0x2bf838: as a double (mixed integer/float arithmetic).
    double toDouble() const;
    // 0x2d02d4: numbers when not 0; texts always; arrays when not empty; a
    // register number when it is not negative.
    bool truthy() const;
};

// A loaded script file (0x2810a4).
class Program {
public:
    struct Block {
        std::string name;
        std::uint32_t hash = 0;
        int first = 0, end = 0; // its commands, end exclusive
        int firstLabel = 0, labelCount = 0;
        int temporaries = 0; // registers 1001..
        int locals = 0;      // registers 2000..
        int arguments = 0;   // fewest arguments a call must pass
    };

    struct Label {
        std::string name;
        std::uint32_t hash = 0;
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

    Program(std::string name, const import::adventure56::Script& script);

    // An empty program for tests; fill blocks/labels/commands/operands
    // (blocks sorted by hash).
    explicit Program(std::string name) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }

    // 0x280fc4 (one script): the block with this name hash, or null.
    const Block* block(std::uint32_t hash) const;
    const Block* block(std::string_view name) const;
    // 0x288984: the command index of a label of `block`, or -1.
    int label(const Block& block, std::uint32_t hash) const;

    // Scripts a block lookup continues in when this one lacks the block
    // (0x280fc4 follows the script's link at +8).
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

// What running a frame or a command reports (0x24da70).
enum class Result {
    Next = 0,   // continue with the next command
    Wait = 1,   // stop for this frame; resume here on the next
    Return = 2, // the block ends
    Stop = 3,   // the thread ends
};

// A running block (0x40-byte frame objects, 0x39c4d4).
struct Frame {
    const Program* program = nullptr;
    const Program::Block* block = nullptr;
    int pc = 0;
    std::vector<Value> arguments, locals, temporaries;
    Value* result = nullptr; // register 1000: where the caller wants the result
    std::unique_ptr<Frame> child;
    Thread* thread = nullptr;
    bool aborted = false; // +0x2e bit 15: the frame stops

    // 0x2c4108: the register, or null when it does not exist.
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

    // The operand's value (a register's content; a missing register reads
    // as its number, kind Ref).
    const Value& at(std::size_t i) const;
    // The register an operand names, or null (writes go there).
    Value* reg(std::size_t i) const;

    std::int32_t integer(std::size_t i) const { return at(i).toInt(); }

    std::uint32_t unsignedInt(std::size_t i) const { return at(i).toUint(); }

    float real(std::size_t i) const { return at(i).toFloat(); }

    // 0x2c0668: a text operand, else null.
    const std::string* text(std::size_t i) const;
};

using Service = std::function<bool(Call&)>;

// A thread of script execution: its values, its root frame, the result of
// the started block and a deferred start.
class Thread {
public:
    // `values`: registers 0..values-1.
    Thread(Machine& machine, std::size_t values);
    ~Thread();

    // Starts `block` of `program` (searching the linked scripts too), at
    // `label` when given; any running block ends. From inside the thread's
    // own run the start is deferred until the run waits or ends.
    // `arguments` become the block's arguments (5044 passes up to eight).
    bool start(const Program& program, std::string_view block, std::string_view label = {},
               std::span<const Value> arguments = {});
    // Calls a block from the innermost running frame (a callback: the
    // interrupted block resumes when it returns).
    bool callback(std::string_view block, std::span<const Value> arguments = {});
    // Ends every block.
    void stop();

    // Runs until a command waits or nothing is left to run.
    void run();

    bool running() const { return root_.child != nullptr; }

    bool executing() const { return executing_; }

    // Research: "script:block#pc" of each running frame, outermost first.
    std::string location() const;

    Machine& machine() const { return machine_; }

    const Program* program() const { return program_; }

    std::vector<Value> values; // registers 0..999
    Value result;              // what the started block returned

    // Optional trace of executed commands: (program, block, command index).
    std::function<void(const Program&, const Program::Block&, int)> trace;

private:
    friend class Machine;
    Machine& machine_;
    Frame root_;
    const Program* program_ = nullptr;
    Value callbackResult_;
    bool executing_ = false;

    struct Pending {
        const Program* program = nullptr;
        std::string block, label;
        std::vector<Value> arguments;
    };

    std::unique_ptr<Pending> pending_;
};

// The machine: the service table and the random numbers (xorshift128 at
// 0x2c40b8).
class Machine {
public:
    Machine();

    void service(int opcode, Service handler) { services_[opcode] = std::move(handler); }

    bool hasService(int opcode) const { return services_.contains(opcode); }

    // A service's handler (research tools wrap them), or nothing.
    Service handler(int opcode) const {
        const auto it = services_.find(opcode);
        return it == services_.end() ? Service{} : it->second;
    }

    // Commands the reconstruction has not rebuilt yet raise this (the game
    // would clear the destination of an unbound opcode).
    std::function<void(int opcode, const Program&, int command)> missing;
    // Optional trace of every thread's executed commands (research).
    std::function<void(const Program&, const Program::Block&, int)> trace;
    // Research: calls (20/21) of blocks no loaded script has.
    std::function<void(std::uint32_t hash)> missingBlock;
    // A port safeguard: commands a thread's run may still execute before it
    // is stopped (set per run); reported here.
    std::int64_t budget = 1000000;
    std::function<void(const Program&, int command)> runaway;

    // 0x2c40b8: the next random number; its remainder by `n` when n > 0.
    std::uint32_t random(std::uint32_t n);
    // 0x2bcea8: a float in [0, x].
    float randomFloat(float x);
    void seed(std::uint32_t value);

    std::array<std::uint32_t, 4> state() const { return {rng_[0], rng_[1], rng_[2], rng_[3]}; }

    void setState(const std::array<std::uint32_t, 4>& s) {
        for (std::size_t i = 0; i < 4; ++i) {
            rng_[i] = s[i];
        }
    }

    // 0x22aa28 / 0x280e80: pushes a frame calling `block` (looked up from
    // `program` on) as the child of `caller`; false when the block is
    // unknown or given too few arguments. The result goes to `result`.
    bool call(Frame& caller, const Program& program, std::uint32_t hash, Value* result,
              std::span<const Value> arguments);
    // 0x24da70: runs a frame (and its callees) until it waits or ends.
    Result run(Frame& frame);

private:
    Result core(Frame& frame, int opcode, Value& result,
                std::span<const Program::Operand> operands);
    bool jump(Frame& frame, const Value& label);
    std::unordered_map<int, Service> services_;
    std::uint32_t rng_[4] = {};
};

// Block and label names: CRC-32 (zlib's, 0x2d4900).
std::uint32_t hash(std::string_view name);

// The hash the DS engine used, CRC-16/X.25 (0x2c1b5c; opcode 521).
std::uint16_t shortHash(std::string_view name);

} // namespace picarat::xq
