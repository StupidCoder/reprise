#pragma once
#include "app/titles.hpp"
#include <string>
#include <vector>

// Layout of the level selector screen: one "full game" entry, the title's
// start points in rows (one row per world, at most three per row, in the
// same columns) and "Back".
namespace shell {

struct SelectRect {
    int x, y, w, h;
};

struct LevelLayout {
    struct Row {
        std::string world; // empty for the continuation of a world
        int first, count;  // selection indices
    };

    std::vector<Row> rows;
    std::vector<std::string> labels; // by selection index (1-based levels)
    std::vector<std::string> hints;  // by selection index; empty: the default
    int back{};                      // the "Back" selection index

    explicit LevelLayout(const Title& title);
    SelectRect rect(int selection) const;
    // Selection under a mouse position, or -1.
    int hit(int x, int y) const;
    // Moves the selection by one row/column, wrapping at the edges.
    int move(int selection, int dx, int dy) const;
};

} // namespace shell
