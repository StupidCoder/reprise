// The game's two file obfuscations.
//
// Container files ("PFile", maps *.mbn, GUI layouts, saves): an optional 12-byte header
// {u32le 0xFFFFA329, u32le djb2 hash of the plain body, u32le hash ^ file size} followed by
// the body XORed with a repeating key (the key's trailing NUL included). Without the header
// the whole file is XORed from offset 0 and carries no checksum.
// Evidence: PFile::PFile @0x1517d4, PFile::checkSafeValues @0x150930, PFile::openRead @0x151458.
//
// Chunk-tree files (*.brd, compiled XML): bytes {0x7B, 0x02}, then the body XORed with a
// 29-byte key; the XOR of all plain body bytes is 0. Evidence: BrdReader::loadFile @0x21b388.
#pragma once

#include "data/game_files.hpp"

#include <optional>

namespace doc::data {

// Plain container body (header removed). nullopt if a header is present and the checksum
// does not match.
std::optional<Bytes> decodeContainer(const Bytes& file, const Bytes& key);

// Plain chunk-tree body, starting at the file's offset 2. nullopt on a bad header or
// integrity check.
std::optional<Bytes> decodeChunkTree(const Bytes& file, const Bytes& key);

}  // namespace doc::data
