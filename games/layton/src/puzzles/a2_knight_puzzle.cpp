// Knight's tour engine. See a2_knight_puzzle.hpp for the rules.
#include "puzzles/a2_knight_puzzle.hpp"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace picarat {

KnightPuzzle::KnightPuzzle(std::span<const std::uint8_t> bytes) {
    ScriptContext context;
    context.bind(0x44, [&](CommandArguments& arguments) {
        // KnightBoard(originX, originY, cellSize, columns, rows, startColumn, startRow)
        originX = std::int16_t(arguments.integer());
        originY = std::int16_t(arguments.integer());
        cellSize = std::int16_t(arguments.integer());
        columns = std::int16_t(arguments.integer());
        rows = std::int16_t(arguments.integer());
        initialColumn = std::int16_t(arguments.integer());
        initialRow = std::int16_t(arguments.integer());
        // The game trusts its data; a native build rejects boards that would
        // have corrupted the original allocator instead of reproducing that.
        if (cellSize <= 0 || columns <= 0 || rows <= 0 || columns * rows > 256 ||
            initialColumn < 0 || initialColumn >= columns || initialRow < 0 || initialRow >= rows) {
            throw std::runtime_error("invalid knight board");
        }
        reset();
    });
    ScriptInterpreter(bytes).run(context);
    if (cells.empty()) {
        throw std::runtime_error("missing knight board");
    }
}

std::uint8_t* KnightPuzzle::cell(int x, int y) {
    // 020f7fec compares as unsigned 16-bit values, so negative coordinates
    // wrap to large numbers and fall outside the board as well.
    if (std::uint16_t(x) >= std::uint16_t(columns) || std::uint16_t(y) >= std::uint16_t(rows)) {
        return nullptr;
    }
    return &cells.at(y * columns + x);
}

void KnightPuzzle::markReachable() {
    // 020f8124: clear the previous highlights...
    for (auto& c : cells) {
        if (c == kReachable) {
            c = kUnvisited;
        } else if (c == kVisitedReachable) {
            c = kVisited;
        }
    }
    // ...then highlight every unvisited square one knight's move away, in the
    // original's order of the eight moves.
    static constexpr std::array<std::pair<int, int>, 8> kKnightMoves{
        {{-2, -1}, {-2, 1}, {2, 1}, {2, -1}, {-1, -2}, {-1, 2}, {1, 2}, {1, -2}}};
    for (auto [dx, dy] : kKnightMoves) {
        if (auto* c = cell(column + dx, row + dy); c && *c == kUnvisited) {
            *c = kReachable;
        }
    }
}

bool KnightPuzzle::move(int x, int y) {
    // 020f8038: only highlighted squares can be entered.
    auto* c = cell(x, y);
    if (!c || (*c != kReachable && *c != kVisitedReachable)) {
        return false;
    }
    // Start a jump from where the knight is drawn now.
    startX = displayX;
    startY = displayY;
    column = std::int16_t(x);
    row = std::int16_t(y);
    targetX = std::int16_t(cellSize * column + originX);
    targetY = std::int16_t(cellSize * row + originY);
    jump = kJumpFrames;
    *c = kVisited;
    markReachable();
    if (x != initialColumn || y != initialRow) {
        sounds.play(3);
    }
    return true;
}

void KnightPuzzle::reset() {
    // 020f7a0c: an empty board with only the starting square highlighted, then
    // "jump" there so it counts as visited. The knight appears in place at once.
    // The completion counter (+70) is left for update() to clear.
    state = kIdle;
    cells.assign(columns * rows, kUnvisited);
    *cell(initialColumn, initialRow) = kReachable;
    move(initialColumn, initialRow);
    displayX = startX = float(targetX);
    displayY = startY = float(targetY);
    jump = -1;
    pressedColumn = pressedRow = -1;
}

void KnightPuzzle::update(const Touch& t) {
    if (t.held) {
        lastX_ = t.x;
        lastY_ = t.y;
    }
    // Pixel-to-square divisions truncate towards zero, in the original's
    // division helper as in C++; a position just left of or above the board
    // therefore maps to square 0, as in the game.
    switch (state) {
    case kIdle:
        // Presses are ignored until the current jump has landed.
        if (jump >= 0) {
            return;
        }
        if (t.pressed) {
            pressedColumn = std::int16_t((t.x - originX) / cellSize);
            pressedRow = std::int16_t((t.y - originY) / cellSize);
            state = kPressed;
        }
        counter = 0;
        break;

    case kPressed:
        // The knight moves only if the stylus is released on the square where
        // it was pressed; sliding to another square cancels the tap.
        if (t.released) {
            const int x = (lastX_ - originX) / cellSize;
            const int y = (lastY_ - originY) / cellSize;
            if (x == pressedColumn && y == pressedRow) {
                move(x, y);
            }
            pressedColumn = pressedRow = -1;
            state = kIdle;
            // Complete when no square is left unvisited (highlighted squares
            // are unvisited too).
            const bool allVisited = std::none_of(cells.begin(), cells.end(), [](auto c) {
                return c == kUnvisited || c == kReachable;
            });
            if (allVisited) {
                state = kComplete;
            }
        }
        counter = 0;
        break;

    case kComplete:
        // Count frames towards the automatic judgment; 16-bit wrap as in the game.
        counter = std::bit_cast<std::int16_t>(std::uint16_t(std::uint16_t(counter) + 1));
        break;
    }
}

void KnightPuzzle::animate() {
    // 020f7ce4. The float operations keep the original order so that rounding
    // matches the game's software floating point exactly.
    if (jump < 0) {
        // Standing: snap to the square.
        displayX = startX = float(targetX);
        displayY = startY = float(targetY);
        jump = -1;
        return;
    }
    // t runs from 0 to 1 over the jump. The knight moves in a straight line
    // and is lifted by 4·t·(1−t) cells: a parabola peaking one cell high.
    const float t = 1.f - float(jump) / 15.f;
    displayX = startX + t * (float(targetX) - startX);
    displayY = startY + t * (float(targetY) - startY);
    float height = 4.f * t;
    height *= 1.f - t;
    height = float(cellSize) * height;
    displayY -= height;
    if (jump == 0) {
        sounds.play(0x11b);
    }
    --jump;
}

void KnightPuzzle::render(Surface& target, const Sprite& sprite, int yOffset) const {
    auto draw = [&](const char* animation, int x, int y) {
        const auto& frames = sprite.animations.at(sprite.animationNames.at(animation));
        blit(target, sprite.images.at(frames.at(0).image), x, yOffset + y);
    };
    // Square markers, row by row. During a jump only visited squares are
    // shown; the highlights for the next move appear after landing. All four
    // marker animations in the original sprite are single still frames.
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < columns; ++x) {
            const auto c = cells[y * columns + x];
            const char* marker = nullptr;
            if (jump < 0) {
                if (c == kVisited) {
                    marker = "red";
                } else if (c == kReachable) {
                    marker = "ki"; // the yellow highlight
                } else if (c == kVisitedReachable) {
                    marker = "orange";
                }
            } else if (c == kVisited || c == kVisitedReachable) {
                marker = "red";
            }
            if (marker) {
                draw(marker, originX + x * cellSize, originY + y * cellSize);
            }
        }
    }
    // The knight itself, rounded to the nearest pixel.
    draw("o", int(displayX + 0.5f), int(displayY + 0.5f));
}

} // namespace picarat
