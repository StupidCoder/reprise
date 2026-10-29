#include "puzzles/circle_input.hpp"
#include "puzzles/circle_stroke.hpp"
#include <iostream>
using namespace picarat;

void check(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

int main() try {
    std::array<CircleRegion, 2> regions{{{38, 37, 20, true}, {80, 80, 20, false}}};
    CircleStroke s;
    s.answer = true;
    s.invalid = true;
    s.markerX = 42;
    s.markerY = 43;
    s.lastX = 2;
    s.lastY = 3;
    s.reset();
    check(s.answer && !s.invalid && s.markerX == -1 && s.lastY == -1,
          "reset preserves answer byte only");
    s.evaluate(regions, 1, 24);
    check(!s.answer && !s.invalid && s.markerX == 0 && s.markerY == 0 && !s.solved(),
          "empty stroke");
    s.addPoint(35, 33);
    s.addPoint(41, 41);
    s.evaluate(regions, 1, 24);
    check(s.solved() && s.points.empty() && s.markerY == 13,
          "distance 10 inclusive, open stroke allowed");
    s.reset();
    s.addPoint(36, 35);
    s.addPoint(40, 39);
    s.evaluate(regions, 1, 24);
    check(s.invalid && !s.answer && !s.solved(), "short stroke rejected");
    s.reset();
    s.addPoint(20, 20);
    s.addPoint(90, 90);
    s.evaluate(regions, 1, 24);
    check(s.invalid && !s.answer, "too many centers");
    s.reset();
    s.addPoint(20, 20);
    s.addPoint(90, 90);
    s.evaluate(regions, 2, 24);
    check(s.solved() && s.markerX == 38, "first enclosed center is marker");
    s.reset();
    s.addPoint(70, 70);
    s.addPoint(90, 90);
    s.evaluate(regions, 1, 24);
    check(!s.solved() && !s.invalid && s.markerX == 80, "wrong region is a valid submission");
    s.reset();
    s.addPoint(10, 10);
    s.addPoint(20, 20);
    s.markerX = 7;
    s.markerY = 8;
    s.invalid = true;
    s.evaluate({}, 1, 24);
    check(s.points.empty() && s.markerX == 7 && s.invalid && !s.answer,
          "empty group retains marker and invalid state");
    s.reset();
    s.addPoint(-1, 0);
    s.addPoint(0, -1);
    check(s.points.empty(), "negative samples");
    s.addPoint(1, 1);
    s.addPoint(2, 2);
    s.addPoint(1, 1);
    check(s.points.size() == 2, "global duplicate suppression");
    for (int i = 0; i < 300; ++i) {
        s.addPoint(i, 10);
    }
    check(s.points.size() == 256, "point capacity");
    // Host rectangle clipping belongs to the input adapter, not addPoint.
    check(s.points.back() == std::pair<std::int32_t, std::int32_t>{253, 10},
          "capacity keeps earliest samples");
    s.markerX = 2;
    s.markerY = 0;
    s.answer = true;
    s.invalid = false;
    check(!s.solved(), "marker x submission bound");
    s.markerX = 3;
    check(s.solved(), "marker x lower valid value");
    s.markerY = -1;
    check(!s.solved(), "marker y submission bound");
    CircleInput input;
    auto ink = input.update({1, 195, 100, -1, 195}, regions, 1, 24);
    check(ink.kind == CircleInkCommand::Kind::None && input.stroke.points.empty(),
          "outside start ignored");
    ink = input.update({2, 170, 100, -1, 195}, regions, 1, 24);
    check(input.stroke.points.empty(), "outside start cannot enter drawing area");
    ink = input.update({1, 186, 20, -1, 186}, regions, 1, 24);
    check(ink.clear && ink.kind == CircleInkCommand::Kind::Dot && input.firstX == 186,
          "inclusive drawing edge/start clear");
    ink = input.update({2, 195, 40, -1, 186}, regions, 1, 24);
    check(ink.kind == CircleInkCommand::Kind::Line && ink.x1 == 186 && input.stroke.lastX == 186,
          "held input clipping");
    input.update({3, -1, -1, 195, 186}, regions, 1, 24);
    check(input.stroke.invalid && input.stroke.lastX == -1 && !input.stroke.points.empty(),
          "outside release retains points and invalidates");
    ink = input.update({1, 20, 20, -1, 20}, regions, 1, 24);
    check(ink.clear && !input.stroke.invalid && input.stroke.points.size() == 1,
          "new stroke resets invalid input");
    input.update({2, 55, 55, -1, 20}, regions, 1, 24);
    input.update({3, -1, -1, 55, 20}, regions, 1, 24);
    check(input.stroke.solved(), "input adapter submits recovered open stroke");
    ink = input.update({3, -1, -1, 55, 20}, regions, 1, 24);
    check(ink.clear && !input.stroke.solved() && !input.stroke.invalid,
          "empty release requests layer clear");
    std::cout << "Circle stroke branch checks passed\n";
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
