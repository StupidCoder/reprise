#include "app/level_select.hpp"
#include <algorithm>

namespace shell {

// Selection 0 is the full game; 1..n are the title's levels in order;
// `back` follows.
LevelLayout::LevelLayout(const Title& title) {
    labels.push_back("Play full game");
    hints.emplace_back();
    const std::string* previous = nullptr;
    for (std::size_t i = 0; i < title.levels.size(); ++i) {
        const auto& l = title.levels[i];
        labels.push_back(l.label);
        hints.push_back(l.hint);
        const bool new_world = !previous || *previous != l.world;
        if (new_world || rows.back().count == 3) {
            rows.push_back({new_world ? l.world : std::string(), int(i) + 1, 0});
        }
        ++rows.back().count;
        previous = &l.world;
    }
    back = int(title.levels.size()) + 1;
    labels.push_back("Back to library");
    hints.emplace_back();
}

SelectRect LevelLayout::rect(int n) const {
    if (n == 0) {
        return {32, 106, 896, 60};
    }
    if (n == back) {
        return {32, 584, 208, 32};
    }
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const auto& row = rows[r];
        if (n >= row.first && n < row.first + row.count) {
            constexpr int w = 208;
            const int y = 186 + int(r) * 30;
            return {260 + (n - row.first) * (w + 16), y, w, 26};
        }
    }
    return {};
}

int LevelLayout::hit(int x, int y) const {
    for (int n = 0; n <= back; ++n) {
        const auto r = rect(n);
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
            return n;
        }
    }
    return -1;
}

int LevelLayout::move(int selection, int dx, int dy) const {
    // Rows: the full game entry, the level rows, then "Back".
    std::vector<std::pair<int, int>> all{{0, 1}};
    for (const auto& r : rows) {
        all.push_back({r.first, r.count});
    }
    all.push_back({back, 1});
    selection = std::clamp(selection, 0, back);
    int row = 0;
    while (row + 1 < int(all.size()) && selection >= all[std::size_t(row + 1)].first) {
        ++row;
    }
    int column = selection - all[std::size_t(row)].first;
    const int count = int(all.size());
    if (dy) {
        row = (row + count + dy) % count;
    }
    const auto& target = all[std::size_t(row)];
    column = std::min(column, target.second - 1);
    if (dx) {
        column = (column + target.second + dx) % target.second;
    }
    return target.first + column;
}

} // namespace shell
