#include "puzzles/circle_parameters.hpp"
#include "system/script.hpp"
#include <bit>
#include <iostream>
#include <stdexcept>
#include <tuple>
using namespace picarat;

namespace {
void check(bool condition, const char* detail) {
    if (!condition) {
        throw std::runtime_error(detail);
    }
}

template <class F>
void rejects(F f) {
    bool threw = false;
    try {
        f();
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "invalid script accepted");
}

void half(Bytes& b, unsigned v) {
    b.push_back(v);
    b.push_back(v >> 8);
}

void word(Bytes& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        b.push_back(v >> (i * 8));
    }
}

void command(Bytes& b, unsigned op) {
    half(b, 0);
    half(b, op);
}

void number(Bytes& b, unsigned tag, std::uint32_t bits) {
    half(b, tag);
    word(b, bits);
}

Bytes file(const Bytes& body) {
    Bytes b;
    word(b, body.size());
    b.insert(b.end(), body.begin(), body.end());
    return b;
}
} // namespace

int main() try {
    // Every context uses the same parser and dispatch path; bindings stay local.
    Bytes body;
    command(body, 2);
    half(body, 12);
    command(body, 255);
    int first = 0, second = 0;
    ScriptContext a, b;
    a.bind(2, [&](CommandArguments&) { ++first; });
    b.bind(2, [&](CommandArguments&) { ++second; });
    ScriptInterpreter x(file(body)), y(file(body));
    x.run(a);
    y.run(b);
    check(first == 1 && second == 1 && x.reader.position() == 6, "context isolation or end token");
    rejects([&] { a.bind(2, [](CommandArguments&) {}); });
    rejects([&] {
        a.bind(0, [](CommandArguments&) {});
    }); // command 1 is ordinary in the first adventure
    body.clear();
    command(body, 1);
    half(body, 13);
    ScriptInterpreter stop(file(body));
    stop.run(a);
    check(stop.reader.position() == stop.reader.size(), "stop must skip the remaining body");

    // Context callback precedes argument decoding, and floats retain all bits.
    body.clear();
    command(body, 0x99);
    number(body, 1, 0xffffffff);
    number(body, 2, 0x80000000);
    number(body, 2, 0x7fc01234);
    number(body, 1, 1);
    half(body, 12);
    ScriptInterpreter audio(file(body));
    ScriptContext ctx;
    std::string order;
    ctx.beforeCommand = [&] {
        order += 'B';
    };
    audio.reader.onToken = [&](const ScriptToken& t, const ScriptValues&) {
        if (t.tag == 1 || t.tag == 2) {
            order += 'T';
        }
    };
    audio.onCommand = [&](CommandBoundary c) {
        order += c.entering ? 'E' : 'R';
    };
    ctx.bind(0x99, [&](CommandArguments& v) {
        order += 'H';
        check(v.integer() == -1, "signed integer");
        check(v.floatingBits() == 0x80000000, "negative zero");
        check(v.floatingBits() == 0x7fc01234, "NaN payload");
        check(v.integer() == 1, "argument order");
    });
    audio.run(ctx);
    check(order == "EBTTTTHR", "callback/token/handler order changed");

    // Reentrant scene work must not replace an outer script's cursor or context.
    body.clear();
    command(body, 0x31);
    number(body, 1, 7);
    command(body, 2);
    half(body, 12);
    ScriptInterpreter outer(file(body));
    ScriptContext outerContext;
    int nested = 0;
    outerContext.bind(0x31, [&](CommandArguments& args) {
        check(args.integer() == 7, "outer argument");
        Bytes innerBody;
        command(innerBody, 2);
        half(innerBody, 12);
        ScriptContext innerContext;
        innerContext.bind(2, [&](CommandArguments&) { ++nested; });
        ScriptInterpreter inner(file(innerBody));
        inner.run(innerContext);
    });
    outerContext.bind(2, [&](CommandArguments&) { nested += 10; });
    outer.run(outerContext);
    check(nested == 11 && outer.reader.position() == outer.reader.size(),
          "nested interpreter state leaked");

    // Unavailable opcodes must fail before context callbacks or argument reads.
    body.clear();
    command(body, 0x31);
    number(body, 1, 4);
    half(body, 12);
    ScriptContext empty;
    empty.beforeCommand = [] {
        throw std::logic_error("callback must not run");
    };
    ScriptInterpreter missing(file(body));
    int callbacks = 0;
    empty.beforeCommand = [&] {
        ++callbacks;
    };
    rejects([&] { missing.run(empty); });
    check(callbacks == 0 && missing.reader.position() == 4,
          "unsupported command consumed arguments");
    ScriptContext wait;
    wait.bind(0x31, [](CommandArguments& v) { v.integer(); });
    body.clear();
    command(body, 0x31);
    number(body, 2, 4);
    half(body, 12);
    ScriptInterpreter wrongType(file(body));
    rejects([&] { wrongType.run(wait); });
    body.clear();
    command(body, 0x31);
    half(body, 1);
    half(body, 4);
    ScriptInterpreter truncated(file(body));
    rejects([&] { truncated.run(wait); });
    body.clear();
    command(body, 6);
    half(body, 3);
    half(body, 1);
    body.push_back('x');
    half(body, 12);
    ScriptContext scene;
    scene.bind(6, [](CommandArguments& v) { v.string(); });
    ScriptInterpreter unterminated(file(body));
    rejects([&] { unterminated.run(scene); });
    body.clear();
    command(body, 0x31);
    number(body, 1, 4);
    half(body, 12);
    ScriptContext broken;
    broken.bind(0x31, [](CommandArguments&) {});
    ScriptInterpreter unread(file(body));
    rejects([&] { unread.run(broken); });
    // ov2 truth conversion: integer/noninteger branches and exact ASCII word.
    for (auto [value, expected] : std::array<std::pair<std::string, bool>, 6>{{{"true", true},
                                                                               {"TrUe", true},
                                                                               {"false", false},
                                                                               {"1", false},
                                                                               {"true ", false},
                                                                               {"", false}}}) {
        body.clear();
        half(body, 3);
        half(body, value.size() + 1);
        body.insert(body.end(), value.begin(), value.end());
        body.push_back(0);
        ScriptReader reader(file(body));
        ScriptArguments args(reader);
        check(args.truth() == expected, "truth string conversion");
    }
    for (auto [tag, bits, expected] : std::array<std::tuple<unsigned, unsigned, bool>, 4>{
             {{1, 0, false}, {1, 0xffffffff, true}, {1, 2, true}, {2, 0x3f800000, false}}}) {
        body.clear();
        number(body, tag, bits);
        ScriptReader reader(file(body));
        ScriptArguments args(reader);
        check(args.truth() == expected, "truth token conversion");
    }
    body.clear();
    command(body, 0x0c);
    number(body, 1, 0xfffffff8);
    number(body, 1, 256);
    number(body, 1, 0);
    for (unsigned n = 0; n < 26; ++n) {
        command(body, 0x18);
        number(body, 1, n);
        number(body, 1, 7);
        number(body, 2, 0x80000000);
        number(body, 1, n == 0);
    }
    half(body, 12);
    CircleParameters circles(file(body));
    check(circles.drawingColor == 1023, "color must add shifted values without clamping");
    check(circles.regions.size() == 25 && circles.regions.back().x == 24, "region capacity");
    check(circles.regions[0].answer && !circles.regions[1].answer, "region truth");
    check(std::bit_cast<unsigned>(circles.regions[0].extent) == 0x80000000, "region float bits");

    std::cout << "Shared script interpreter tests passed\n";
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
