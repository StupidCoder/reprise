// The game's text: one string per line in res/text/text_<LANG>.txt (UTF-16 with BOM, CRLF;
// "\n" in a line is a line break; ManagerStrings::loadLang @0x16cd18, 1409 strings), and the
// names data files use for them. A name is hashed (getStrID @0x1ff4c0: djb2 over the signed
// bytes, modulo 0x106d643d) and the hash is looked up in a decision tree the compiler made of
// the original's name table. That tree is read from the user's executable here: its leaves
// compare the hash with a constant and return the id, or switch over a run of hashes.
#pragma once

#include "data/game_files.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace doc::data {

class StringTable {
public:
    // language: EN, FR, GR, IT, SP, DU, JP, KR, CH (the file suffixes).
    static std::optional<StringTable> load(const GameFiles& files, const std::string& language, std::string& error);

    // UTF-8 text of string `id`, empty if out of range.
    const std::string& text(int id) const;
    // The id a data file's name refers to, -1 if unknown.
    int id(const std::string& name) const;
    const std::string& text(const std::string& name) const { return text(id(name)); }
    int size() const { return int(strings_.size()); }

    static std::uint32_t nameHash(const std::string& name);

private:
    std::vector<std::string> strings_;
    std::map<std::uint32_t, int> ids_;  // name hash -> id
};

}  // namespace doc::data
