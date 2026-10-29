#pragma once
// The game's script language (".gds" files) and its interpreter.
//
// Nearly everything the game does is driven by small scripts: the logo
// sequence, every story event, each puzzle's parameters, movies, the shack
// and more. A script file is a 32-bit length followed by a stream of tokens.
// Each token is a 16-bit tag and, depending on the tag, a value:
//
//   tag 0   command: a 16-bit command number
//   tag 1   32-bit signed integer argument
//   tag 2   32-bit float argument (kept as raw bits until used)
//   tag 3   string argument (16-bit length, then the bytes, NUL-terminated)
//   tag 4   a second string slot
//   tag 6   jump target: offset of a token, counted from the first token
//   tag 7   a second jump-target kind (no reader in either game uses it)
//   tag 8   condition chains: negate the following conditions ("not")
//   tag 9   condition chains: combine with "and"
//   tag 10  condition chains: combine with "or"
//   tag 11  leave the innermost loop ("break")
//   tag 12  end of the script
//   tag 5   carries no value; no reader uses it
//
// The second adventure's scripts use only tags 0-3 and 12. The first
// adventure's story scripts are structured (if / else-if / else, loops) and
// use tags 6-11; see ScriptDialect and a1_script.cpp.
//
// A command is followed by its arguments. What a command number means
// depends on the game and on the object running the script: in a puzzle's
// parameter script, command 0x14 adds an answer button; in a story event
// other numbers move characters or start dialogue. The interpreter is shared
// (second adventure: overlay 2, 0207cee0; first adventure: ARM9 02018050);
// each scene supplies a ScriptContext with handlers for the commands it
// supports. Each game's table of known commands is its ScriptDialect.
#include "system/resources.hpp"
#include <array>
#include <functional>
#include <span>
#include <string_view>
#include <variant>

namespace picarat {
// One token as read from the file.
struct ScriptToken {
    std::size_t offset = 0, next = 0; // Offsets in the copied body, excluding the 4-byte size.
    std::uint16_t tag = 0;
    Bytes payload;
};

// The most recent value of each kind. Reading a token only updates the slot
// of its own kind, as in the original reader.
struct ScriptValues {
    std::uint16_t command = 0;
    std::uint32_t integerBits = 0, floatBits = 0, addressBits = 0;
    Bytes string, alternateString;
};

// Walks a script's tokens (overlay 2, 0207ccd8 reads one token).
class ScriptReader {
public:
    // ov2:0207cc04 (+14384) owns a copy of the declared body. No native pointer
    // is stored in a 32-bit ROM field. Exact-length input is a native check.
    explicit ScriptReader(std::span<const std::uint8_t> file);
    ScriptToken next();

    std::size_t position() const { return cursor_; }

    std::size_t size() const { return body_.size(); }

    void finish() { cursor_ = body_.size(); }

    // Moves to a token offset (a jump target of the first adventure's flow
    // control).
    void seek(std::size_t offset);

    const ScriptValues& values() const { return values_; }

    std::function<void(const ScriptToken&, const ScriptValues&)> onToken;

private:
    Bytes body_;
    std::size_t cursor_ = 0;
    ScriptValues values_;
};

// What the logo sequence's script needs from its scene.
struct OpeningHost {
    virtual ~OpeningHost() = default;
    virtual void background(bool sub, const std::string& name, std::int32_t layer) = 0;
    virtual void fadeIn() = 0;
    virtual void fadeOut() = 0;
    virtual void wait(std::int32_t count, bool touchRelease) = 0;
    virtual void audioRequest(std::int32_t id, std::uint32_t firstFloatBits,
                              std::uint32_t secondFloatBits, std::int32_t route) = 0;
};

// Reported before and after each command (for research tracing).
struct CommandBoundary {
    std::uint16_t command;
    std::size_t offset, cursor;
    bool entering;
};

// Reads typed arguments straight from the token stream (also used for
// dialogue records, which are scripts that are never executed).
class ScriptArguments {
public:
    explicit ScriptArguments(ScriptReader& source) : reader_(source) {}

    std::int32_t integer();
    std::uint32_t floatingBits();
    float real();
    bool truth();
    std::string string();

private:
    ScriptReader& reader_;
    ScriptToken argument(std::uint16_t expectedTag);
};

// The arguments of one command, already read, in order. Floats keep their
// exact bits until a handler asks for a float.
class CommandArguments {
public:
    struct FloatBits {
        std::uint32_t bits;
    };

    using Value = std::variant<std::int32_t, FloatBits, std::string, bool>;
    std::int32_t integer();
    std::uint32_t floatingBits();
    float real();
    bool truth();
    std::string string();

    // Passes over the next argument, whatever its kind.
    void skip() { ++cursor_; }

    bool consumed() const { return cursor_ == values_.size(); }

private:
    friend class ScriptInterpreter;
    std::vector<Value> values_;
    std::size_t cursor_ = 0;

    template <class T>
    T take() {
        return std::get<T>(values_.at(cursor_++));
    }
};

// The commands a scene supports: one handler per command number.
// `beforeCommand` runs before each command's arguments are read (the
// original's virtual function +8). A condition is a command whose handler
// answers a question; the first adventure's if/while chains test it.
class ScriptContext {
public:
    using Handler = std::function<void(CommandArguments&)>;
    using Condition = std::function<bool(CommandArguments&)>;
    void bind(std::uint16_t command, Handler handler);
    void bindCondition(std::uint16_t command, Condition condition);
    // Replaces a bound handler (a script object overriding the base
    // object's function for a command, as the original's virtual functions).
    void rebind(std::uint16_t command, Handler handler);

    bool hasCondition(std::uint16_t command) const {
        return command < conditions_.size() && conditions_[command];
    }

    std::function<void()> beforeCommand;

private:
    friend class ScriptInterpreter;
    std::array<Handler, 256> handlers_;
    std::array<Condition, 256> conditions_;
};

// One entry of a game's command table: the types of the arguments in order
// (i = 32-bit integer, f = float, b = truth value, s = string, t = second
// string slot), a descriptive name and what the command does. The names are
// labels chosen for this project; the games only use the numbers.
struct ScriptCommandInfo {
    std::uint16_t command;
    std::string_view arguments;
    std::string_view name;
    std::string_view description;
};

// How one game's scripts are read and run.
struct ScriptDialect {
    std::span<const ScriptCommandInfo> commands;
    // The command that ends a script early.
    std::uint16_t stop;
    // false (second adventure): arguments are checked against their token
    // kind, and every command must be handled by the context.
    // true (first adventure): each argument is the next token, whatever its
    // kind, and the value is taken from the slot of the expected kind (as the
    // original handlers do); built-in flow control (if, else-if, else, while,
    // repeat, true, false) and conditions are available.
    bool structured;
    // Structured flow control command numbers.
    std::uint16_t ifCommand = 0, elseIfCommand = 0, elseCommand = 0, repeatCommand = 0,
                  whileCommand = 0, trueCommand = 0, falseCommand = 0;

    const ScriptCommandInfo* find(std::uint16_t command) const;
};

// The second adventure's commands (script.cpp).
const ScriptDialect& secondAdventureScripts();

// Runs a script against a scene's handlers.
class ScriptInterpreter {
public:
    explicit ScriptInterpreter(std::span<const std::uint8_t> file,
                               const ScriptDialect& dialect = secondAdventureScripts())
        : reader(file), dialect_(dialect) {}

    void run(const ScriptContext& context);

    // First adventure: lets a handler hand the rest of the script to another
    // object (the puzzle engine created by the "engine" command, ARM9
    // 02011610): runs the remaining commands with `context` up to the end
    // token, without the main loop's stop command.
    void runRest(const ScriptContext& context);

    ScriptReader reader;
    std::function<void(CommandBoundary)> onCommand;

private:
    const ScriptDialect& dialect_;
    // The result of the latest condition (the byte the original passes to
    // every handler; flow commands test it).
    bool condition_ = false;

    void runStructured(const ScriptContext& context);
    void dispatch(const ScriptContext& context, std::uint16_t command, std::size_t offset);
    CommandArguments readArguments(std::string_view layout);
    ScriptToken nextToken();
    bool conditionChain(const ScriptContext& context, std::size_t& endTarget, bool& stopped);
    void ifCommand(const ScriptContext& context);
    void skipBranch();
    void repeatCommand(const ScriptContext& context);
    void whileCommand(const ScriptContext& context);
    enum class BodyEnd { Done, Break, End, Stop };
    BodyEnd runBody(const ScriptContext& context, std::size_t end);
};

// The handlers of the logo sequence (backgrounds, fades, waits, audio).
ScriptContext openingContext(OpeningHost& host);
} // namespace picarat
