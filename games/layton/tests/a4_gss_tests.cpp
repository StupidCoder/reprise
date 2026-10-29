// The fourth adventure's script machine: hand-made programs (flow, calls,
// registers, waits, the value rules) and, with a pack, every script of the
// game loaded and checked against the machine's tables.
#include "system/a4_gss.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace picarat;
using gss::Program;
using gss::Value;

namespace {
void check(bool condition, const char* detail) {
    if (!condition) {
        throw std::runtime_error(detail);
    }
}

// Builds a program in memory: blocks of commands with operands.
struct Builder {
    std::shared_ptr<Program> program = std::make_shared<Program>("test");

    static Program::Operand reg(int n) { return {true, {Value::Kind::Ref, std::uint32_t(n), {}}}; }

    static Program::Operand num(int v) { return {false, Value::ofInt(v)}; }

    static Program::Operand real(float v) { return {false, Value::ofFloat(v)}; }

    static Program::Operand text(std::string s) { return {false, Value::ofText(std::move(s))}; }

    static Program::Operand name(std::string s) { return {false, Value::ofName(std::move(s))}; }

    Program::Block* open = nullptr;

    void block(const std::string& name, int temporaries = 8, int locals = 4, int arguments = 0) {
        Program::Block b;
        b.name = name;
        b.hash = gss::hash(name);
        b.first = b.end = int(program->commands.size());
        b.firstLabel = int(program->labels.size());
        b.temporaries = temporaries;
        b.locals = locals;
        b.arguments = arguments;
        program->blocks.push_back(b);
    }

    void label(const std::string& name) {
        program->labels.push_back({name, gss::hash(name), int(program->commands.size())});
        ++program->blocks.back().labelCount;
    }

    void op(int destination, int opcode, std::vector<Program::Operand> operands = {}) {
        const int first = int(program->operands.size());
        for (auto& o : operands) {
            program->operands.push_back(o);
        }
        program->commands.push_back({opcode, destination, first, int(operands.size())});
        program->blocks.back().end = int(program->commands.size());
    }

    // The loader keeps blocks sorted by hash.
    Program& done() {
        std::sort(program->blocks.begin(), program->blocks.end(),
                  [](const auto& a, const auto& b) { return a.hash < b.hash; });
        return *program;
    }
};

using B = Builder;

void flowAndArithmetic() {
    B b;
    // Main: r0 = 0; loop: r0 = r0 + 1; wait; jump back while r0 < 3.
    b.block("Main");
    b.op(0, 100, {B::num(0)});
    b.label("@000@");
    b.op(0, 150, {B::reg(0), B::num(1)});
    b.op(1001, 135, {B::reg(0), B::num(3)});
    b.op(1002, 30, {B::text("@000@"), B::reg(1001)});
    b.op(1, 100, {B::text("done")});
    Program& p = b.done();
    gss::Machine machine;
    gss::Thread thread(machine, 4);
    check(thread.start(p, "Main"), "start Main");
    thread.run();
    check(thread.values[0].toInt() == 1 && thread.running(), "first frame waits after one step");
    thread.run();
    thread.run();
    check(thread.values[0].toInt() == 3, "counted to three");
    check(thread.values[1].kind == Value::Kind::Text && thread.values[1].text == "done",
          "fell through after the loop");
    check(!thread.running(), "block ended");
}

void callsAndReturns() {
    B b;
    b.block("Main");
    b.op(0, 20, {B::name("Add"), B::num(2), B::num(5)});
    b.op(1, 20, {B::name("Missing")});
    b.op(2, 20, {B::name("Add"), B::num(1)}); // too few arguments
    b.block("Add", 4, 2, 2);
    b.op(1001, 150, {B::reg(3000), B::reg(3001)});
    b.op(1000, 11, {B::reg(1001)});
    Program& p = b.done();
    gss::Machine machine;
    gss::Thread thread(machine, 4);
    check(thread.start(p, "Main"), "start");
    thread.run();
    check(thread.values[0].toInt() == 7, "callee's result reaches the caller's register");
    check(thread.values[1].kind == Value::Kind::Int && thread.values[1].toInt() == 0,
          "unknown block reports 0");
    check(thread.values[2].toInt() == 0, "too few arguments fail");
}

void valueRules() {
    B b;
    b.block("Main", 4, 0);
    b.op(0, 153, {B::num(7), B::num(2)});        // 3
    b.op(1, 153, {B::num(7), B::num(0)});        // 7 (library division)
    b.op(2, 150, {B::num(1), B::real(0.5f)});    // 1.5 (float)
    b.op(3, 150, {B::real(0.5f), B::real(1.5f)}); // 2.0 float
    b.op(4, 150, {B::num(1), B::real(1.0f)});    // 2 (integral mixed -> int)
    b.op(5, 150, {B::text("n"), B::num(12)});    // "n12"
    b.op(6, 501, {B::text("a%03db%sc%d"), B::num(5), B::text("x")});
    b.op(7, 130, {B::text("ab"), B::text("ab")});
    b.op(8, 130, {B::text("ab"), B::num(1)});   // cleared
    b.op(9, 503, {B::text("\x82\xa0z"), B::num(1)}); // second character
    b.op(10, 607, {B::num(15), B::num(0), B::num(9)});
    b.op(11, 110, {B::real(2.5f)});
    Program& p = b.done();
    gss::Machine machine;
    gss::Thread thread(machine, 12);
    thread.start(p, "Main");
    thread.run();
    auto& v = thread.values;
    check(v[0].toInt() == 3 && v[1].toInt() == 7, "integer division");
    check(v[2].kind == Value::Kind::Float && v[2].toFloat() == 1.5f, "mixed stays float");
    check(v[3].kind == Value::Kind::Float && v[3].toFloat() == 2.0f, "float + float");
    check(v[4].kind == Value::Kind::Int && v[4].toInt() == 2, "integral mixed result");
    check(v[5].text == "n12", "text join");
    check(v[6].text == "a005bxc", "format drops what has no argument");
    check(v[7].toInt() == 1 && v[8].kind == Value::Kind::None, "text comparison");
    check(v[9].text == "z", "Shift-JIS character index");
    check(v[10].toInt() == 9, "clamp");
    check(v[11].kind == Value::Kind::Int && v[11].toInt() == ~2, "not of a float");
}

void waitsClearTemporaries() {
    B b;
    b.block("Main", 2, 0);
    b.op(1001, 100, {B::num(5)});
    b.op(1002, 10);
    b.op(0, 100, {B::reg(1001)});
    Program& p = b.done();
    gss::Machine machine;
    gss::Thread thread(machine, 1);
    thread.start(p, "Main");
    thread.run();
    thread.run();
    check(thread.values[0].kind == Value::Kind::None, "temporaries do not survive a wait");
}

void callbacksInterrupt() {
    B b;
    b.block("Main");
    b.label("@000@");
    b.op(1001, 30, {B::text("@000@"), B::num(1)}); // wait forever
    b.block("Hit");
    b.op(0, 140, {B::reg(0)});
    Program& p = b.done();
    gss::Machine machine;
    gss::Thread thread(machine, 1);
    thread.values[0] = Value::ofInt(0);
    thread.start(p, "Main");
    thread.run();
    check(thread.callback("Hit"), "callback starts");
    thread.run();
    check(thread.values[0].toInt() == 1 && thread.running(), "callback ran, main resumes");
}

void randomNumbers() {
    gss::Machine machine;
    // xorshift128 reference sequence with the seed in the first word.
    machine.seed(123456789u);
    const std::uint32_t first = machine.random(0);
    check(first == 3701687786u, "xorshift128 first value");
}

// Every script of the game loads; reports the service opcodes they use.
int loadAll(const std::filesystem::path& pack) {
    std::set<int> services;
    int files = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(pack)) {
        if (entry.path().extension() != ".lq") {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        const auto script = import::adventure4::parseGss1(bytes);
        const Program program(entry.path().filename().string(), script);
        for (const auto& c : program.commands) {
            check(c.first + c.count <= int(program.operands.size()), "operands in range");
            if (c.opcode >= 1000) {
                services.insert(c.opcode);
            }
        }
        for (const auto& block : program.blocks) {
            check(program.block(block.hash) == &block, "block lookup by hash");
        }
        ++files;
    }
    check(files == 1310, "all 1310 scripts");
    std::cout << files << " scripts, " << services.size() << " service opcodes\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) try {
    flowAndArithmetic();
    callsAndReturns();
    valueRules();
    waitsClearTemporaries();
    callbacksInterrupt();
    randomNumbers();
    std::cout << "gss machine: ok\n";
    if (argc < 2 || !std::filesystem::exists(argv[1])) {
        std::cout << "no pack: data part skipped\n";
        return 0;
    }
    return loadAll(argv[1]);
} catch (const std::exception& e) {
    std::cerr << "a4_gss_tests: " << e.what() << '\n';
    return 1;
}
