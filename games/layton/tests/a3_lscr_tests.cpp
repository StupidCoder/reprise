// The third adventure's script machine on small hand-made scripts: control
// flow, calls and frames, names, waits and the value rules.
#include "system/a3_lscr.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace picarat;
using import::adventure3::Lscr;
using import::adventure3::LscrCommand;
using import::adventure3::LscrOperand;

namespace {
void check(bool condition, const char* detail) {
    if (!condition) {
        throw std::runtime_error(detail);
    }
}

// Builds an LSCR command list in memory.
struct Builder {
    Lscr script;

    LscrOperand text(const std::string& s, std::uint8_t type = 2) {
        const auto at = std::uint32_t(script.strings.size());
        script.strings.insert(script.strings.end(), s.begin(), s.end());
        script.strings.push_back(0);
        return {type, at};
    }

    static LscrOperand number(int v) { return {0, std::uint32_t(v)}; }

    static LscrOperand var(int slot) { return {4, std::uint32_t(slot)}; }

    static LscrOperand real(float f) {
        std::uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return {1, bits};
    }

    void raw(std::uint16_t op, std::vector<LscrOperand> o) { script.commands.push_back({op, o}); }

    void section(const std::string& name, int arguments) {
        raw(1100, {text(name), number(arguments)});
    }

    void end() { raw(1101, {}); }

    void label(const std::string& name) { raw(1111, {text(name)}); }

    void statement(int op, std::vector<LscrOperand> o) {
        o.insert(o.begin(), number(op));
        raw(1110, o);
    }
};

// A machine with a recording command 9000 (records its first operand).
struct Fixture {
    lscr::Machine machine;
    std::vector<lscr::Value> seen;

    Fixture() {
        lscr::installCore(machine);
        machine.command(9000, [this](lscr::Call& c) {
            seen.push_back(c.at(0));
            return lscr::Result::Next;
        });
    }
};
} // namespace

int main() try {
    {
        // Loops, arithmetic, labels, calls with arguments and return.
        Builder b;
        b.section("MAIN", 0);
        b.statement(1320, {b.var(0), b.number(0)});
        b.label("loop");
        b.statement(1101, {b.text("SQUARE"), b.var(0)});
        b.statement(1301, {b.var(0)});
        b.statement(1041, {b.text("loop"), b.number(1005), b.var(0), b.number(4)});
        b.end();
        b.section("SQUARE", 1);
        b.statement(1320, {b.var(1), b.var(100)});
        b.statement(1305, {b.var(1), b.var(100)});
        b.statement(9000, {b.var(1)});
        b.end();
        Fixture f;
        f.machine.load(b.script, "test");
        lscr::Thread t(f.machine);
        check(t.call("MAIN", {}), "call MAIN");
        t.run();
        check(!t.running(), "MAIN should finish");
        check(f.seen.size() == 4 && f.seen[3].toInt() == 9, "squares 0, 1, 4, 9");
    }
    {
        // Waits: 1000 resumes after itself, 1032 repeats until true.
        Builder b;
        b.section("MAIN", 0);
        b.statement(9000, {b.number(1)});
        b.statement(1000, {});
        b.statement(9000, {b.number(2)});
        b.statement(1032, {b.number(1000), b.var(200), b.number(1)});
        b.statement(9000, {b.number(3)});
        b.end();
        Fixture f;
        f.machine.load(b.script, "test");
        lscr::Thread t(f.machine);
        t.call("MAIN", {});
        t.run();
        check(f.seen.size() == 1, "stops at 1000");
        t.run();
        t.run();
        check(f.seen.size() == 2 && t.running(), "waits for variable 200");
        t.variables[0] = lscr::Value::of(1);
        t.run();
        check(f.seen.size() == 3 && !t.running(), "continues once true");
        check(t.clock == 4 * 4096, "clock counts frames");
    }
    {
        // Values: integer and float rules, comparisons across kinds, names.
        Builder b;
        b.section("MAIN", 0);
        b.raw(1102, {b.text("RESULT", 3), b.var(2)});
        b.statement(1320, {b.var(0), b.number(7)});
        b.statement(1306, {b.var(0), b.number(2)}); // 7 / 2 = 3
        b.statement(9000, {b.var(0)});
        b.statement(1305, {b.var(0), b.real(0.5f)}); // 1.5
        b.statement(9000, {b.var(0)});
        b.statement(1356, {b.text("RESULT", 3), b.var(0)}); // round(1.5) = 2
        b.statement(9000, {b.var(2)});
        b.statement(1322, {b.var(3), b.number(1001), b.text("1"), b.number(1)});
        b.statement(9000, {b.var(3)});               // text vs number: not unequal either
        b.statement(1307, {b.var(2), b.number(-3)}); // 2 % -3 = 2
        b.statement(9000, {b.var(2)});
        b.end();
        Fixture f;
        f.machine.load(b.script, "test");
        lscr::Thread t(f.machine);
        t.call("MAIN", {});
        t.run();
        check(f.seen.size() == 5, "five values");
        check(f.seen[0].kind == lscr::Value::Kind::Int && f.seen[0].integer == 3, "7/2");
        check(f.seen[1].kind == lscr::Value::Kind::Float && f.seen[1].real == 1.5f, "3*0.5");
        check(f.seen[2].toInt() == 2, "named variable and rounding");
        check(f.seen[3].toInt() == 0, "different kinds compare false");
        check(f.seen[4].toInt() == 2, "remainder");
    }
    {
        // Eight frames at most; tail calls replace the frame; missing labels
        // continue; stop ends the thread.
        Builder b;
        b.section("DEEP", 1);
        b.statement(9000, {b.var(100)});
        b.statement(1320, {b.var(0), b.var(100)});
        b.statement(1301, {b.var(0)});
        b.statement(1101, {b.text("DEEP"), b.var(0)});
        b.end();
        b.section("TAIL", 0);
        b.statement(1102, {b.text("AFTER")});
        b.statement(9000, {b.number(-1)}); // never reached
        b.end();
        b.section("AFTER", 0);
        b.statement(1040, {b.text("missing")});
        b.statement(9000, {b.number(100)});
        b.statement(1010, {});
        b.statement(9000, {b.number(-2)}); // never reached
        b.end();
        Fixture f;
        f.machine.load(b.script, "test");
        lscr::Thread t(f.machine);
        t.call("DEEP", std::vector{lscr::Value::of(1)});
        t.run();
        check(f.seen.size() == 8 && f.seen.back().toInt() == 8, "eight frames deep");
        f.seen.clear();
        t.call("TAIL", {});
        t.run();
        check(f.seen.size() == 1 && f.seen[0].toInt() == 100 && !t.running(), "tail call, stop");
    }
    {
        // Game state declared by a definition list: flags, signed and
        // protected variables.
        Builder d;
        d.raw(5501, {Builder::number(3), d.text("FLAG")});
        d.raw(5511,
              {Builder::number(10), Builder::number(-1), Builder::number(0), d.text("SIGNED")});
        d.raw(5511,
              {Builder::number(12), Builder::number(2), Builder::number(1), d.text("LOCKED")});
        d.raw(5511,
              {Builder::number(0x250), Builder::number(1), Builder::number(0), d.text("TEMP")});
        lscr::GameState s;
        s.declare(d.script);
        s.setVariable(lscr::Value::ofText("SIGNED"), -1);
        check(s.variable(lscr::Value::ofText("SIGNED")) == -1, "signed byte");
        s.setVariable(lscr::Value::ofText("LOCKED"), 5);
        check(s.variable(lscr::Value::ofText("LOCKED")) == 0, "protected");
        s.setVariable(lscr::Value::ofText("LOCKED"), 300, 0, true);
        check(s.variable(lscr::Value::of(12)) == 300, "forced write, by offset");
        s.setVariable(lscr::Value::ofText("TEMP"), 7, 1);
        check(s.variable(lscr::Value::ofText("TEMP"), 1) == 7, "temporary array");
        s.setFlag(lscr::Value::ofText("FLAG"), true, 2);
        check(s.flag(lscr::Value::of(5)) && !s.flag(lscr::Value::ofText("FLAG")), "flag index");
    }
    std::cout << "a3_lscr_tests passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "a3_lscr_tests failed: " << e.what() << '\n';
    return 1;
}
