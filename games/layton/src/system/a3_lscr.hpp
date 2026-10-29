#pragma once
// The third adventure's script machine: runs the game's LSCR command lists
// (events, menus, the puzzle scenes and each puzzle's own sections).
//
// How the game's scripts work
// ---------------------------
// An LSCR file is first compiled: its section commands (1100 "NAME" n ...
// 1101) become sections - named macros taking n arguments - holding the
// statements (1110 opcode operands...) between them; 1111 marks a label,
// 1102 gives a name to a variable or constant for the rest of the section
// (or the file, outside a section). At run time a thread executes a
// section statement by statement. Every statement is one command: control
// flow, arithmetic, game state, or a request to the game's services
// (objects, messages, sound...), which register their own commands.
//
// Values are integers, floats (IEEE single), texts or names. Variable
// operands refer to a slot: 0..15 the frame's locals, 100..111 the frame's
// arguments, 200..215 the thread's own variables (set by whoever starts the
// thread, e.g. the arguments of a button callback), 300..315 the machine's
// globals. Commands receive variables as references and may write them.
//
// A thread runs its top frame until a command waits (resumed on the next
// frame), calls a section (a new frame), returns (the frame ends) or stops
// the thread. At most eight frames are active; a call beyond that is
// ignored, as in the game.
//
// Reconstructed from overlay 0 (the machine: 0204ff54 command dispatch,
// 02050678 frame execution, 02051898 thread run, 02051a0c call, 020506e8..
// 02050dc4 compiler) and the command handlers of overlay 9. See
// .analysis/adventure3/docs/script-machine.md.
#include "import/a3_formats.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace picarat::lscr {

// A script value. Arithmetic follows the game (0203e7b8...): two integers
// give an integer, anything else a float; texts are compared by content.
struct Value {
    enum class Kind : std::uint8_t { Int, Float, Text, Name, Ref };
    Kind kind = Kind::Int;
    std::int32_t integer = 0;
    float real = 0;
    std::string text; // Text and Name
    Value* ref = nullptr;

    static Value of(int v) { return {Kind::Int, v, 0, {}, nullptr}; }

    static Value ofFloat(float v) { return {Kind::Float, 0, v, {}, nullptr}; }

    static Value ofText(std::string v) { return {Kind::Text, 0, 0, std::move(v), nullptr}; }

    static Value refTo(Value& slot) { return {Kind::Ref, 0, 0, {}, &slot}; }

    // The value a reference (chain) leads to.
    const Value& get() const { return kind == Kind::Ref && ref ? ref->get() : *this; }

    Value& get() { return kind == Kind::Ref && ref ? ref->get() : *this; }

    bool isNumber() const { return get().kind == Kind::Int || get().kind == Kind::Float; }

    bool isText() const { return get().kind == Kind::Text; }

    // 0203e144: integers as they are, floats truncated toward zero
    // (saturating), anything else 0.
    int toInt() const;
    // 0203e1b4: floats as they are, integers converted (nearest), else 0.
    float toFloat() const;
    // 0203e224: the text, or "" for other kinds.
    const std::string& toText() const;
};

// Comparisons (0203eb10, ec40, ed88, ee60, ef38, f010): numbers compare as
// doubles, texts by content (byte order); values of different kinds are
// neither equal nor unequal - every comparison is false.
bool equal(const Value& a, const Value& b);
bool unequal(const Value& a, const Value& b);
bool atLeast(const Value& a, const Value& b);
bool atMost(const Value& a, const Value& b);
bool greater(const Value& a, const Value& b);
bool less(const Value& a, const Value& b);

// One compiled statement.
struct Operand {
    bool variable = false;
    int slot = 0; // variable slot (see the top)
    Value constant;
};

struct Command {
    int opcode = 0;
    std::vector<Operand> operands;
};

// A section: a named macro and its statements.
struct Section {
    std::string name;
    int arguments = 0;
    std::vector<Command> commands;
    std::unordered_map<std::string, int> labels; // label -> index of the next command
    std::vector<int> labelOrder;                 // label positions by definition order
    std::string file;                            // where it was compiled from
};

// Compiles an LSCR file into its sections (020506e8 1100, 02050744 1101,
// 020507b8 1102, 02050a2c 1110, 02050dc4 1111).
std::vector<Section> compile(const import::adventure3::Lscr& script, const std::string& file);

class Machine;
class Thread;

// What a command returns (0204ff54, 02050678, 02051898).
enum class Result {
    Next,   // continue with the next statement (also the game's error 0)
    Called, // a frame was pushed (or a call was refused): run the top frame
    Wait,   // stop for this frame; the thread resumes at its current position
    Return, // the frame ends
    Stop,   // the thread ends
};

// A running macro.
struct Frame {
    const Section* section = nullptr;
    int pc = 0, delta = 0; // the statement executed next is pc + delta
    std::array<Value, 16> locals{};
    std::array<Value, 12> arguments{};
    int argumentCount = 0;
    Thread* thread = nullptr;
};

// The context a command runs in.
struct Call {
    Machine& machine;
    Thread& thread;
    Frame& frame;
    std::span<Value> args; // operands, variables as references
    int opcode = 0;

    std::size_t count() const { return args.size(); }

    const Value& at(std::size_t i) const;

    int integer(std::size_t i) const { return at(i).toInt(); }

    float real(std::size_t i) const { return at(i).toFloat(); }

    const std::string& text(std::size_t i) const { return at(i).toText(); }

    // The variable an operand names, or null when it is not a variable.
    Value* variable(std::size_t i) const;
    // Jumps to the label an operand names (text) or numbers (integer); a
    // missing label continues with the next statement (02050624).
    void jump(std::size_t operand);
    // Evaluates the condition given by the operands from `first` on.
    bool condition(std::size_t first) const;
};

using CommandHandler = std::function<Result(Call&)>;
// A condition receives the operands after the condition's own number.
using ConditionHandler = std::function<bool(Call&, std::span<Value>)>;

// A thread of script execution.
class Thread {
public:
    explicit Thread(Machine& machine) : machine_(machine) {}

    // Pushes a frame for section `name` (at label `label`, or the start);
    // false when the section is unknown or eight frames are active.
    bool call(const std::string& name, std::span<const Value> arguments,
              const std::string& label = {});
    // Replaces the top frame with a call (1102).
    bool replace(const std::string& name, std::span<const Value> arguments,
                 const std::string& label = {});
    // Ends every frame and calls `name` (020519d4).
    bool restart(const std::string& name, std::span<const Value> arguments);

    // Ends every frame; from inside one of the thread's own commands, after
    // that command.
    void stop();

    // Runs until the top frame waits or no frame is left (02051898);
    // `elapsed` (frames) is added to the thread's clock.
    void run(int elapsed = 1);

    bool running() const { return !frames_.empty(); }

    std::size_t depth() const { return frames_.size(); }

    const Frame* top() const { return frames_.empty() ? nullptr : &frames_.back(); }

    // Thread variables 200..215 and the clock (1090..1092; 4096 per frame).
    std::array<Value, 16> variables{};
    std::int32_t clock = 0;
    // The object whose thread this is (0: the scene's own thread); commands
    // 43xx act on it (02063d58).
    int owner = 0;

    // Optional trace of executed statements: (section, pc, opcode).
    std::function<void(const Section&, int, int)> trace;

private:
    Result execute(Frame& frame);
    Machine& machine_;
    std::vector<Frame> frames_;
    bool executing_ = false, cancelled_ = false;
};

// Flags and variables of the game state, declared by rc/ge/lt3_rc/lt3_gmd
// (5501 flags {bit, name}, 5511 variables {offset, size, protected, name},
// 5521 texts). Offsets below 0x240 (variables) and bits below 0x600 (flags)
// belong to the saved data; the rest to the temporary area (020dfc0c,
// 020dec70).
class GameState {
public:
    void declare(const import::adventure3::Lscr& definitions);

    // Variables by name or number, element `index` of an array (the size of
    // one element apart). Sizes are bytes; a negative size reads signed.
    int variable(const Value& id, int index = 0) const;
    // Writes are ignored for protected variables unless `force` (020dfd50).
    void setVariable(const Value& id, int value, int index = 0, bool force = false);
    bool flag(const Value& id, int index = 0) const;
    void setFlag(const Value& id, bool on, int index = 0);
    std::string textVariable(int index) const;
    void setTextVariable(int index, std::string value);
    bool declared(const Value& id) const;
    // The saved part (variables below 0x240, flags below 0x600): cleared
    // for a new game (020db3bc), and as bytes for a save file - variables
    // first, then the flags.
    void resetSaved();
    std::vector<std::uint8_t> savedBytes() const;
    void restoreSaved(std::span<const std::uint8_t> bytes);

    // 0202070c, the C library's rand(): the game's one random sequence
    // (seed * 0x41c64e6d + 0x3039, bits 16..30). Its seed at power-on is
    // not recovered; 1 is the library default (hypothesis).
    int random() {
        seed_ = seed_ * 0x41c64e6du + 0x3039u;
        return int((seed_ >> 16) & 0x7fff);
    }

private:
    struct Variable {
        int offset = 0, size = 1;
        bool isSigned = false, protectedWrite = false;
    };

    const Variable* findVariable(const Value& id) const;
    int findFlag(const Value& id) const;
    std::unordered_map<std::string, Variable> variablesByName_;
    std::map<int, Variable> variablesByOffset_;
    std::unordered_map<std::string, int> flagsByName_;
    std::vector<std::uint8_t> saved_ = std::vector<std::uint8_t>(0x240 + 0x40), temporary_;
    std::vector<std::uint8_t> savedFlags_ = std::vector<std::uint8_t>(0x600 / 8), temporaryFlags_;
    std::map<int, std::string> texts_;
    std::uint32_t seed_ = 1;
};

// The machine: compiled sections, command and condition tables, globals.
class Machine {
    std::unique_ptr<GameState> own_; // when the state is not shared

public:
    Machine();
    // A machine on a game state that outlives it (scenes come and go; the
    // game's flags and variables stay).
    explicit Machine(GameState& shared);

    // Compiles a file and adds its sections; an earlier section of the same
    // name stays (lookup order).
    void load(const import::adventure3::Lscr& script, const std::string& file);
    const Section* find(const std::string& name) const;

    void command(int opcode, CommandHandler handler) { commands_[opcode] = std::move(handler); }

    void condition(int opcode, ConditionHandler handler) {
        conditions_[opcode] = std::move(handler);
    }

    bool hasCommand(int opcode) const { return commands_.contains(opcode); }

    bool hasCondition(int opcode) const { return opcode < 1000 || conditions_.contains(opcode); }

    // Runs one statement's command (unknown commands are an error: the
    // reconstruction must not silently skip what the game does).
    Result dispatch(Call& call);
    bool evaluate(Call& call, std::span<Value> operands);

    std::array<Value, 16> globals{};
    GameState& state;
    // Called for a call of the owning object's thread (1100/1110).
    std::function<void(Call&, const std::string&, std::span<const Value>)> ownerCall;

private:
    std::vector<std::unique_ptr<Section>> sections_;
    std::unordered_map<std::string, const Section*> byName_;
    std::unordered_map<int, CommandHandler> commands_;
    std::unordered_map<int, ConditionHandler> conditions_;
};

// Registers the machine's own commands and conditions: control flow,
// calls, variables and arithmetic (1000..1357) and the game state
// (1500..1527). See a3_lscr_core.cpp.
void installCore(Machine& machine);

} // namespace picarat::lscr
