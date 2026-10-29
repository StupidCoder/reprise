// Compiled-XML chunk trees (*.brd), the format of level tables, events, dialogue, cutscenes and
// animation sequences. Layout (BrdReader @0x21aca4..0x1fd020), offsets in the plain body:
//   chunk: u32be size (to the next sibling), name '\0', u32be attribute count, attributes,
//          u32be child count, children
//   attribute: name '\0', u8 type, value:
//          0 string '\0', 1 bool u8, 2 s8, 3 s16be, 4 s32be, 5 f32 little-endian
// The body's top chunk is an unnamed wrapper ("rt") around the document's root element.
#pragma once

#include "data/game_files.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace doc::data {

struct BrdNode {
    using Value = std::variant<std::string, bool, int, float>;
    std::string name;
    std::vector<std::pair<std::string, Value>> attributes;
    std::vector<BrdNode> children;

    const Value* find(const std::string& key) const;
    // Typed reads with the original's conversions (BrdReader::getInt/getFloat/getStr/getBool):
    // integers convert to float; strings are not numbers.
    std::optional<int> getInt(const std::string& key) const;
    std::optional<float> getFloat(const std::string& key) const;
    std::optional<std::string> getString(const std::string& key) const;
    std::optional<bool> getBool(const std::string& key) const;

    const BrdNode* child(const std::string& childName) const;
    std::vector<const BrdNode*> childrenNamed(const std::string& childName) const;
};

// Parses a plain chunk-tree body (see decodeChunkTree). nullopt on malformed data.
std::optional<BrdNode> parseChunkTree(const Bytes& body);

// Reads, decodes and parses "res/<relative>"; returns the document root element.
std::optional<BrdNode> loadChunkTree(const GameFiles& files, const std::string& relative);

}  // namespace doc::data
