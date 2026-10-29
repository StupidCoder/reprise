// The 3DS adventures' script machine: hand-made programs (flow, calls,
// registers, waits, arrays, the value rules) and, with a pack, every script
// of the game loaded and checked against the machine's tables.
#include "system/a56_xq.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace picarat;
using xq::Program;
using xq::Value;

namespace {
void check(bool condition, const char* detail) {
    if (!condition) {
        throw std::runtime_error(detail);
    }
}

// Builds a program in memory: blocks of commands with operands.
struct Builder {
    std::shared_ptr<Program> program = std::make_shared<Program>("test");

    static Program::Operand reg(int n) {
        return {true, {Value::Kind::Ref, std::uint32_t(n), {}, {}}};
    }

    static Program::Operand num(int v) { return {false, Value::ofInt(v)}; }

    static Program::Operand real(float v) { return {false, Value::ofFloat(v)}; }

    static Program::Operand text(std::string s) { return {false, Value::ofText(std::move(s))}; }

    // A block or label name as the files store it: its CRC-32, unsigned.
    static Program::Operand name(const std::string& s) { return {false, Value::ofUint(xq::hash(s))}; }

    void block(const std::string& name, int temporaries = 8, int locals = 4, int arguments = 0) {
        Program::Block b;
        b.name = name;
        b.hash = xq::hash(name);
        b.first = b.end = int(program->commands.size());
        b.firstLabel = int(program->labels.size());
        b.temporaries = temporaries;
        b.locals = locals;
        b.arguments = arguments;
        program->blocks.push_back(b);
    }

    void label(const std::string& name) {
        program->labels.push_back({name, xq::hash(name), int(program->commands.size())});
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

    Program& done() {
        std::sort(program->blocks.begin(), program->blocks.end(),
                  [](const auto& a, const auto& b) { return a.hash < b.hash; });
        return *program;
    }
};

using B = Builder;

void flowAndArithmetic() {
    B b;
    b.block("Main");
    b.op(0, 100, {B::num(0)});
    b.label("@000@");
    b.op(0, 150, {B::reg(0), B::num(1)});
    b.op(1001, 135, {B::reg(0), B::num(3)});
    b.op(1002, 30, {B::name("@000@"), B::reg(1001)});
    b.op(1, 100, {B::text("done")});
    Program& p = b.done();
    xq::Machine machine;
    xq::Thread thread(machine, 4);
    check(thread.start(p, "Main"), "start Main");
    thread.run();
    check(thread.values[0].toInt() == 1 && thread.running(), "first frame waits after one step");
    thread.run();
    thread.run();
    check(thread.values[0].toInt() == 3, "counted to three");
    check(thread.values[1].isText() && thread.values[1].text == "done", "fell through");
    check(!thread.running(), "block ended");
}

void callsAndReturns() {
    B b;
    b.block("Main");
    b.op(0, 20, {B::name("Add"), B::num(2), B::num(5)});
    b.op(1, 20, {B::name("Missing")});
    b.op(2, 20, {B::name("Add"), B::num(1)}); // too few arguments
    b.op(3, 20, {B::text("Add"), B::num(4), B::num(4)}); // by name
    b.block("Add", 4, 2, 2);
    b.op(1001, 150, {B::reg(3000), B::reg(3001)});
    b.op(1000, 11, {B::reg(1001)});
    Program& p = b.done();
    xq::Machine machine;
    xq::Thread thread(machine, 4);
    check(thread.start(p, "Main"), "start");
    thread.run();
    check(thread.values[0].toInt() == 7, "callee's result reaches the caller's register");
    check(thread.values[1].kind == Value::Kind::Int && thread.values[1].toInt() == 0,
          "unknown block reports 0");
    check(thread.values[2].toInt() == 0, "too few arguments fail");
    check(thread.values[3].toInt() == 8, "a text names the block too");
}

void valueRules() {
    B b;
    b.block("Main", 4, 0);
    b.op(0, 153, {B::num(7), B::num(2)});        // 3
    b.op(1, 153, {B::num(7), B::num(0)});        // 0 (division-by-zero hook)
    b.op(2, 154, {B::num(7), B::num(0)});        // 7
    b.op(3, 150, {B::num(1), B::real(0.5f)});    // 1.5 (float)
    b.op(4, 150, {B::num(1), B::real(1.0f)});    // 2 (integral mixed -> int)
    b.op(5, 150, {B::text("n"), B::num(12)});    // cleared: no text joins
    b.op(6, 501, {B::text("a%03db%sc%d"), B::num(5), B::text("x")});
    b.op(7, 130, {B::text("ab"), B::text("ab")});
    b.op(8, 130, {B::text("ab"), B::num(1)}); // cleared
    b.op(9, 503, {B::text("\x82\xa0z"), B::num(1)});
    b.op(10, 607, {B::num(15), B::num(0), B::num(9)});
    b.op(11, 500, {B::text("remark")});
    b.op(12, 522, {B::text("Main")});
    b.op(13, 40, {B::real(1.0f)});
    Program& p = b.done();
    xq::Machine machine;
    xq::Thread thread(machine, 14);
    thread.values[11] = Value::ofInt(5);
    thread.start(p, "Main");
    thread.run();
    auto& v = thread.values;
    check(v[0].toInt() == 3 && v[1].toInt() == 0 && v[2].toInt() == 7, "integer division");
    check(v[3].kind == Value::Kind::Float && v[3].toFloat() == 1.5f, "mixed stays float");
    check(v[4].kind == Value::Kind::Int && v[4].toInt() == 2, "integral mixed result");
    check(v[5].kind == Value::Kind::None, "texts do not join");
    check(v[6].text == "a005bxc", "format drops what has no argument");
    check(v[7].toInt() == 1 && v[8].kind == Value::Kind::None, "text comparison");
    check(v[9].text == "z", "Shift-JIS character index");
    check(v[10].toInt() == 9, "clamp");
    check(v[11].kind == Value::Kind::None, "500 clears");
    check(v[12].toUint() == xq::hash("Main"), "CRC-32 of a text");
    check(v[13].toInt() == 3, "kind of a value");
}

void arrays() {
    B b;
    b.block("Main", 4, 0);
    b.op(0, 530, {B::num(3), B::num(2)});          // 3 x 2
    b.op(0, 100, {B::num(7), B::num(1), B::num(0)}); // r0[1][0] = 7
    b.op(0, 250, {B::num(5), B::num(1), B::num(0)}); // r0[1][0] += 5
    b.op(1, 531, {B::reg(0), B::num(1), B::num(0)}); // 12
    b.op(0, 240, {B::num(2), B::num(1)});           // r0[2][1]++ (nothing there: cleared)
    b.op(2, 530, {B::num(2)});
    b.op(2, 100, {B::real(1.5f), B::num(0)});
    b.op(2, 252, {B::num(2), B::num(0)}); // 3 (mixed, integral)
    b.op(3, 531, {B::reg(2), B::num(0)});
    b.op(4, 531, {B::reg(2), B::num(5)}); // out of range: cleared
    Program& p = b.done();
    xq::Machine machine;
    xq::Thread thread(machine, 5);
    thread.values[4] = Value::ofInt(1);
    thread.start(p, "Main");
    thread.run();
    auto& v = thread.values;
    check(v[0].kind == Value::Kind::None, "++ of an empty element clears the destination");
    check(v[1].toInt() == 12, "element assignment and in-place add");
    check(v[3].kind == Value::Kind::Int && v[3].toInt() == 3, "in-place mixed multiply");
    check(v[4].kind == Value::Kind::None, "out-of-range element");
}

void waitsClearTemporaries() {
    B b;
    b.block("Main", 2, 0);
    b.op(1001, 100, {B::num(5)});
    b.op(1002, 10);
    b.op(0, 100, {B::reg(1001)});
    Program& p = b.done();
    xq::Machine machine;
    xq::Thread thread(machine, 1);
    thread.start(p, "Main");
    thread.run();
    thread.run();
    check(thread.values[0].kind == Value::Kind::None, "temporaries do not survive a wait");
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// Every script of the game loads; blocks and labels resolve by hash.
int loadAll(const std::filesystem::path& pack) {
    std::set<int> core, services;
    int files = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(pack)) {
        if (entry.path().extension() != ".xq") {
            continue;
        }
        const auto bytes = readFile(entry.path());
        if (bytes.size() >= 4 && std::equal(bytes.begin(), bytes.begin() + 4, "XSEQ")) {
            continue; // one event (05/06_060000) is a stray XSEQ file
        }
        const auto script = import::adventure56::parseXq32(bytes);
        const Program program(entry.path().filename().string(), script);
        for (const auto& c : program.commands) {
            check(c.first + c.count <= int(program.operands.size()), "operands in range");
            (c.opcode >= 1000 ? services : core).insert(c.opcode);
        }
        for (const auto& block : program.blocks) {
            check(program.block(block.hash) == &block, "block lookup by hash");
            for (int i = block.firstLabel; i < block.firstLabel + block.labelCount; ++i) {
                const auto& l = program.labels[std::size_t(i)];
                check(program.label(block, l.hash) == l.command, "label lookup by hash");
            }
        }
        ++files;
    }
    check(files == 1276, "all 1276 scripts");
    const std::set<int> known = {10,  11,  12,  20,  21,  30,  31,  32,  33,  40,  100, 110, 111,
                                 112, 120, 121, 122, 130, 131, 132, 133, 134, 135, 140, 141, 150,
                                 151, 152, 153, 154, 160, 161, 162, 170, 171, 240, 241, 250, 251,
                                 252, 253, 254, 260, 261, 262, 270, 271, 500, 501, 502, 503, 510,
                                 511, 512, 513, 520, 521, 522, 530, 531, 600, 601, 602, 603, 604,
                                 605, 606, 607, 608, 610, 611, 612, 620, 621, 622, 623, 630, 631};
    for (int op : core) {
        check(known.contains(op), "every core opcode used is in the machine's table");
    }
    std::cout << files << " scripts, " << core.size() << " core and " << services.size()
              << " service opcodes\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) try {
    flowAndArithmetic();
    callsAndReturns();
    valueRules();
    arrays();
    waitsClearTemporaries();
    std::cout << "xq machine: ok\n";
    if (argc < 2 || !std::filesystem::exists(argv[1])) {
        std::cout << "no pack: data part skipped\n";
        return 0;
    }
    return loadAll(argv[1]);
} catch (const std::exception& e) {
    std::cerr << "a56_xq_tests: " << e.what() << '\n';
    return 1;
}
