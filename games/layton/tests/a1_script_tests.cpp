// Tests of the first adventure's script interpreter: structured flow control
// (if / else-if / else, while, repeat, not / and / or, break, stop), argument
// slots, the engine hand-off, and - when a pack is available - a run of every
// script of the ROM against the command table.
//
// usage: a1_script_tests [PACK]   (PACK defaults to $PICARAT_A1_PACK)
#include "puzzles/a1_catalogue.hpp"
#include "system/a1_script.hpp"
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
using namespace picarat;

namespace {
void check(bool condition, const char* detail) {
    if (!condition) {
        throw std::runtime_error(detail);
    }
}

// Builds a token stream. Jump targets are patched once the label is known.
struct Script {
    Bytes body;
    std::map<std::string, std::size_t> labels;
    std::vector<std::pair<std::size_t, std::string>> jumps;

    void half(unsigned v) {
        body.push_back(std::uint8_t(v));
        body.push_back(std::uint8_t(v >> 8));
    }

    void word(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            body.push_back(std::uint8_t(v >> (i * 8)));
        }
    }

    Script& op(unsigned command) {
        half(0);
        half(command);
        return *this;
    }

    Script& integer(std::int32_t v) {
        half(1);
        word(std::uint32_t(v));
        return *this;
    }

    Script& real(float v) {
        half(2);
        word(std::bit_cast<std::uint32_t>(v));
        return *this;
    }

    Script& string(const std::string& s) {
        half(3);
        half(unsigned(s.size() + 1));
        body.insert(body.end(), s.begin(), s.end());
        body.push_back(0);
        return *this;
    }

    Script& tag(unsigned t) {
        half(t);
        return *this;
    }

    Script& jump(const std::string& label) {
        half(6);
        jumps.emplace_back(body.size(), label);
        word(0);
        return *this;
    }

    Script& label(const std::string& name) {
        labels[name] = body.size();
        return *this;
    }

    Bytes file() {
        half(12);
        for (const auto& [at, name] : jumps) {
            const auto target = std::uint32_t(labels.at(name));
            for (int i = 0; i < 4; ++i) {
                body[at + std::size_t(i)] = std::uint8_t(target >> (i * 8));
            }
        }
        Bytes out;
        const auto size = std::uint32_t(body.size());
        for (int i = 0; i < 4; ++i) {
            out.push_back(std::uint8_t(size >> (i * 8)));
        }
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }
};

// Command numbers used by the synthetic scripts (layouts from the table).
constexpr unsigned kLog = 0x10;   // "i"
constexpr unsigned kTest = 0x13;  // "i", a condition
constexpr unsigned kFloat = 0x39; // "f"
constexpr unsigned kIf = 0x12, kRepeat = 0x14, kWhile = 0x15, kElseIf = 0x16, kElse = 0x17;
constexpr unsigned kTrue = 0x19, kFalse = 0x1a, kStop = 0x4f, kEngine = 0x1b;

struct Recorder {
    std::vector<std::int32_t> log;
    std::vector<float> floats;
    int before = 0;

    ScriptContext context() {
        ScriptContext c;
        c.bind(kLog, [this](CommandArguments& a) { log.push_back(a.integer()); });
        c.bind(kFloat, [this](CommandArguments& a) { floats.push_back(a.real()); });
        c.bindCondition(kTest, [](CommandArguments& a) { return a.integer() != 0; });
        c.beforeCommand = [this] {
            ++before;
        };
        return c;
    }
};

std::vector<std::int32_t> run(Script& s) {
    Recorder r;
    auto bytes = s.file();
    ScriptInterpreter interpreter(bytes, adventure1::scripts());
    interpreter.run(r.context());
    return r.log;
}

// if COND { log 1 } elseif COND2 { log 2 } else { log 3 }; log 9
Script chain(int first, int second) {
    Script s;
    s.op(kIf).op(kTest).integer(first).jump("b2");
    s.op(kLog).integer(1);
    s.label("b2").op(kElseIf).op(kTest).integer(second).jump("b3");
    s.op(kLog).integer(2);
    s.label("b3").op(kElse).jump("end");
    s.op(kLog).integer(3);
    s.label("end").op(kLog).integer(9);
    return s;
}

void flowTests() {
    using V = std::vector<std::int32_t>;
    {
        auto s = chain(1, 1);
        check(run(s) == V{1, 9}, "if: true branch, then skip to the end");
    }
    {
        auto s = chain(0, 1);
        check(run(s) == V{2, 9}, "else-if: tested after a false if");
    }
    {
        auto s = chain(0, 0);
        check(run(s) == V{3, 9}, "else: entered when every chain is false");
    }
    {
        // not: negates every later condition of the chain (flag never cleared).
        Script s;
        s.op(kIf).tag(8).op(kTest).integer(0).tag(9).op(kTest).integer(0).jump("end");
        s.op(kLog).integer(1);
        s.label("end").op(kLog).integer(9);
        check(run(s) == V{1, 9}, "not applies to the whole rest of the chain");
    }
    {
        // and / or combine; without them a later result replaces the earlier.
        Script a, o, r;
        a.op(kIf).op(kTrue).tag(9).op(kFalse).jump("e").op(kLog).integer(1).label("e");
        o.op(kIf).op(kFalse).tag(10).op(kTrue).jump("e").op(kLog).integer(1).label("e");
        r.op(kIf).op(kFalse).op(kTrue).jump("e").op(kLog).integer(1).label("e");
        check(run(a).empty(), "and");
        check(run(o) == V{1}, "or");
        check(run(r) == V{1}, "a later condition replaces the earlier");
    }
    {
        // while with a break token: the condition stays true.
        Script s;
        s.op(kWhile).op(kTrue).jump("end");
        s.op(kLog).integer(5).tag(11);
        s.label("end").op(kLog).integer(9);
        check(run(s) == V{5, 9}, "while: break leaves the loop");
    }
    {
        Script s;
        s.op(kWhile).op(kFalse).jump("end").op(kLog).integer(5);
        s.label("end").op(kLog).integer(9);
        check(run(s) == V{9}, "while: false condition skips the body");
    }
    {
        Script s;
        s.op(kRepeat).integer(3).jump("end").op(kLog).integer(4);
        s.label("end").op(kLog).integer(9);
        check(run(s) == V{4, 4, 4, 9}, "repeat 3");
    }
    {
        Script s;
        s.op(kRepeat).integer(0).jump("end").op(kLog).integer(4);
        s.label("end").op(kLog).integer(9);
        check(run(s) == V{4, 9}, "repeat 0 runs the body once (02011138 quirk)");
    }
    {
        Script s;
        s.op(kLog).integer(1).op(kStop).op(kLog).integer(2);
        check(run(s) == V{1}, "stop ends the script");
    }
    {
        // An integer token where a float is expected leaves the older float.
        Recorder r;
        Script s;
        s.op(kFloat).real(2.5f).op(kFloat).integer(7);
        auto bytes = s.file();
        ScriptInterpreter(bytes, adventure1::scripts()).run(r.context());
        check(r.floats == std::vector<float>{2.5f, 2.5f}, "arguments come from value slots");
        check(r.before == 2, "virtual +8 before every command");
    }
    {
        // The engine command hands the rest of the script to another object.
        Recorder outer, inner;
        auto innerContext = inner.context();
        Script s;
        s.op(kEngine).string("Tile").op(kLog).integer(1).op(kStop).op(kLog).integer(2);
        auto bytes = s.file();
        ScriptInterpreter interpreter(bytes, adventure1::scripts());
        auto c = outer.context();
        std::string name;
        c.bind(kEngine, [&](CommandArguments& a) {
            name = a.string();
            interpreter.runRest(innerContext);
        });
        interpreter.run(c);
        check(name == "Tile" && outer.log.empty(), "engine command");
        check(inner.log == std::vector<std::int32_t>{1, 2},
              "after the engine command, stop is an ordinary command");
    }
}

// Runs every script of the pack with handlers that accept every command
// (conditions false, so loops end), and checks each command's arguments
// against the token kinds the table expects (counting the known mismatches).
int packTest(const std::filesystem::path& pack) {
    namespace fs = std::filesystem;
    const auto& dialect = adventure1::scripts();

    // A "while not ..." loop never ends when every condition answers false;
    // a budget stops such runs (they are counted, not failures).
    struct Budget {};

    unsigned budget = 0;
    ScriptContext context;
    context.beforeCommand = [&budget] {
        if (++budget > 100000) {
            throw Budget{};
        }
    };
    for (const auto& info : dialect.commands) {
        const auto c = info.command;
        if (c == dialect.ifCommand || c == dialect.elseIfCommand || c == dialect.elseCommand ||
            c == dialect.repeatCommand || c == dialect.whileCommand || c == dialect.trueCommand ||
            c == dialect.falseCommand || c == dialect.stop) {
            continue;
        }
        const auto layout = info.arguments;
        auto consume = [layout](CommandArguments& a) {
            for (char t : layout) {
                switch (t) {
                case 'i':
                    a.integer();
                    break;
                case 'f':
                    a.floatingBits();
                    break;
                case 'b':
                    a.truth();
                    break;
                case 'x':
                    break;
                default:
                    a.string();
                }
            }
        };
        context.bindCondition(c, [consume](CommandArguments& a) {
            consume(a);
            return false;
        });
    }
    unsigned files = 0, commands = 0, mismatches = 0, endless = 0;
    for (const auto& folder : {"script", "debug"}) {
        for (const auto& entry : fs::recursive_directory_iterator(pack / folder)) {
            if (entry.path().extension() != ".gds") {
                continue;
            }
            const auto bytes = readFile(entry.path());
            ++files;
            ScriptInterpreter interpreter(bytes, dialect);
            budget = 0;
            try {
                interpreter.run(context);
            } catch (const Budget&) {
                ++endless;
                std::cout << "endless with false conditions: " << entry.path().filename() << "\n";
            }
            // Static check: each command's following value tokens vs its layout.
            ScriptReader reader(bytes);
            const ScriptCommandInfo* info = nullptr;
            std::size_t index = 0;
            while (reader.position() < reader.size()) {
                const auto token = reader.next();
                if (token.tag == 12) {
                    break;
                }
                if (token.tag == 0) {
                    info = dialect.find(reader.values().command);
                    check(info != nullptr, "command missing from the table");
                    index = 0;
                    ++commands;
                    continue;
                }
                if (!info || token.tag >= 5 || index >= info->arguments.size()) {
                    continue;
                }
                const char want = info->arguments[index++];
                const bool ok = (want == 'i' && token.tag == 1) ||
                                (want == 'f' && token.tag == 2) ||
                                ((want == 's' || want == 't') && token.tag == 3) ||
                                (want == 'b' && (token.tag == 1 || token.tag == 3)) || want == 'x';
                if (!ok) {
                    ++mismatches;
                    std::cout << "kind mismatch " << entry.path().filename() << " command "
                              << std::hex << info->command << std::dec << " argument " << index
                              << "\n";
                }
            }
        }
    }
    std::cout << files << " scripts, " << commands << " commands, " << mismatches
              << " argument kind mismatches, " << endless << " endless runs\n";
    check(files == 930, "expected the ROM's 930 scripts");
    // Six events give integers to float parameters (0x78/0x79); see the
    // research notes. Puzzle 114 runs out of tokens instead (no kind change).
    check(mismatches == 6, "unexpected argument kind mismatches");
    return 0;
}
} // namespace

int main(int argc, char** argv) try {
    flowTests();
    std::filesystem::path pack;
    if (argc > 1) {
        pack = argv[1];
    } else if (const char* env = std::getenv("PICARAT_A1_PACK")) {
        pack = env;
    }
    if (pack.empty() || !std::filesystem::exists(pack / "script")) {
        std::cout << "flow tests passed; no first-adventure pack, data test skipped\n";
        return 0;
    }
    packTest(pack);
    // The catalogue, against what the ROM shows for internal puzzle 1 (title
    // card "009", 35 picarats, German title) and the table's extent.
    const adventure1::Catalogue catalogue(pack, "de");
    check(catalogue.puzzles().size() == 164,
          "catalogue size (q92_paramOneColor and q92_paramold are leftovers)");
    const auto* first = catalogue.find(1);
    check(first && first->number == 9 && first->engine == "Match" &&
              first->picarats == std::array<unsigned, 3>{35, 31, 28} &&
              first->title == "Ein Hundeelend!",
          "catalogue entry 1");
    unsigned numbered = 0;
    for (const auto& [id, info] : catalogue.puzzles()) {
        numbered += info.number != 0;
    }
    check(numbered == 135, "135 numbered puzzles");
    check(catalogue.find(24)->number == 1, "the new game's first puzzle is 001");
    check(catalogue.text(1, "q").starts_with("Die Streichh\xf6lzer"), "question text with escapes");
    check(!catalogue.text(1, "h_3").empty() && catalogue.text(1, "h_4").empty(), "hint texts");
    std::cout << "adventure1 script tests passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "a1_script_tests: " << e.what() << "\n";
    return 1;
}
