// Level map files (mapNN/mapNN.mbn), read as Map::loadBinVersion @0x13de30 does.
// Container body, big-endian integers, little-endian floats, NUL-terminated strings:
//   u32 0x0FFBADFA, str background image,
//   u32 vertex count, vertices { f32 x, f32 y, u32 flags, s32 type, s32 index, s32 d, s32 e, str name }
//   u32 line count, lines { s32 a, s32 b, s32 type, s32 tag }   (tag < 0: line disabled, |tag| kept)
//   u32 second line count, second lines { s32 ×4 }, u8 end
// Map space is y-up with the origin at the bottom-left of the 480x320 screen.
// Vertices with flag 2 are entities (player start, cheese, holes, cats, ...); types 500..574
// are map decorations ("fluff") handled by the map itself.
#pragma once

#include "data/game_files.hpp"

#include <optional>
#include <string>
#include <vector>

namespace doc::data {

struct MapVertex {
    float x = 0, y = 0;
    std::uint32_t flags = 0;
    std::int32_t type = 0;
    std::int32_t index = 0;  // field 3 of the record; equals the vertex number in shipped maps
    std::int32_t d = 0, e = 0;
    std::string name;  // e.g. "rand(20000, 60000)" for ambient triggers
    bool isEntity() const { return (flags & 2) != 0; }
};

struct MapLine {
    bool enabled = true;  // tag >= 0
    std::int32_t a = 0, b = 0;
    std::int32_t type = 0;  // 0 = solid wall; other values are special edges
    std::int32_t tag = 0;   // |tag|
};

struct MapFile {
    std::string background;
    std::vector<MapVertex> vertices;
    std::vector<MapLine> lines;
    std::vector<MapLine> secondLines;  // read like the original; meaning not yet recovered
};

std::optional<MapFile> parseMapFile(const Bytes& body, std::string& error);
std::optional<MapFile> loadMapFile(const GameFiles& files, int mapNumber, std::string& error);
// Any map in the same format by its path under res/ (the world maps: world/world_NN.mbn).
std::optional<MapFile> loadMapFile(const GameFiles& files, const std::string& name, std::string& error);

}  // namespace doc::data
