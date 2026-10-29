#pragma once
// Text lists of the 3DS adventures (XSCR files under txt/<language>/):
// texts by key. Command 1000 gives the counts, each 1001 a text {key,
// name, text}; rows with the same key are that key's pages, in order
// (0x2ce84c builds the index {key, first, count}; 3210 reads the count,
// 3211 a page). Puzzle texts: 0 title, 1 question, 2 correct, 3 wrong,
// 4..7 hints.
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace picarat::adventure56 {

class Pack;

struct TextList {
    std::map<int, std::vector<std::string>> entries; // the texts
    std::map<int, std::vector<std::string>> names;   // the rows' names (speakers)

    // 3211 (0x2ce6a0): a row's string field (0 the name, 1 the text).
    const std::string* field(int key, unsigned page, int n) const {
        const auto& m = n == 0 ? names : entries;
        const auto it = m.find(key);
        if ((n != 0 && n != 1) || it == m.end() || page >= it->second.size()) {
            return nullptr;
        }
        return &it->second[page];
    }

    const std::string* at(int key, unsigned page = 0) const {
        const auto it = entries.find(key);
        return it == entries.end() || page >= it->second.size() ? nullptr : &it->second[page];
    }

    unsigned pages(int key) const {
        const auto it = entries.find(key);
        return it == entries.end() ? 0 : unsigned(it->second.size());
    }
};

std::shared_ptr<TextList> loadTextList(const Pack& pack, const std::filesystem::path& file);

} // namespace picarat::adventure56
