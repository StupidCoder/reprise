#include "data/map_file.hpp"

#include "data/obfuscated_file.hpp"

#include <cstdio>
#include <cstring>

namespace doc::data {
namespace {

class Reader {
public:
    explicit Reader(const Bytes& b) : b_(b) {}
    bool ok() const { return ok_; }
    std::int32_t s32() {
        if (p_ + 4 > b_.size()) return ok_ = false, 0;
        std::int32_t v = std::int32_t((std::uint32_t(b_[p_]) << 24) | (std::uint32_t(b_[p_ + 1]) << 16) |
                                      (std::uint32_t(b_[p_ + 2]) << 8) | b_[p_ + 3]);
        p_ += 4;
        return v;
    }
    float f32() {  // PFile::getFloat @0x1507d4 returns the bytes unswapped
        if (p_ + 4 > b_.size()) return ok_ = false, 0.0f;
        float f;
        std::memcpy(&f, &b_[p_], 4);
        p_ += 4;
        return f;
    }
    std::string str() {
        std::size_t start = p_;
        while (p_ < b_.size() && b_[p_] != 0) ++p_;
        if (p_ >= b_.size()) return ok_ = false, std::string();
        std::string s(reinterpret_cast<const char*>(&b_[start]), p_ - start);
        ++p_;
        return s;
    }

private:
    const Bytes& b_;
    std::size_t p_ = 0;
    bool ok_ = true;
};

}  // namespace

std::optional<MapFile> parseMapFile(const Bytes& body, std::string& error) {
    Reader r(body);
    if (std::uint32_t(r.s32()) != 0x0FFBADFAu) {
        error = "map: bad magic";
        return std::nullopt;
    }
    MapFile map;
    map.background = r.str();
    std::int32_t vertexCount = r.s32();
    for (std::int32_t i = 0; i < vertexCount && r.ok(); ++i) {
        MapVertex v;
        v.x = r.f32();
        v.y = r.f32();
        v.flags = std::uint32_t(r.s32());
        v.type = r.s32();
        v.index = r.s32();
        v.d = r.s32();
        v.e = r.s32();
        v.name = r.str();
        map.vertices.push_back(std::move(v));
    }
    auto readLines = [&](std::vector<MapLine>& out) {
        std::int32_t count = r.s32();
        for (std::int32_t i = 0; i < count && r.ok(); ++i) {
            MapLine line;
            line.a = r.s32();
            line.b = r.s32();
            line.type = r.s32();
            std::int32_t tag = r.s32();
            line.enabled = tag >= 0;
            line.tag = tag < 0 ? -tag : tag;
            out.push_back(line);
        }
    };
    readLines(map.lines);
    readLines(map.secondLines);
    if (!r.ok()) {
        error = "map: truncated";
        return std::nullopt;
    }
    for (const MapLine& line : map.lines)
        if (line.a < 0 || line.b < 0 || line.a >= vertexCount || line.b >= vertexCount) {
            error = "map: line refers to a missing vertex";
            return std::nullopt;
        }
    return map;
}

std::optional<MapFile> loadMapFile(const GameFiles& files, int mapNumber, std::string& error) {
    char name[64];
    std::snprintf(name, sizeof name, "map%02d/map%02d.mbn", mapNumber, mapNumber);
    return loadMapFile(files, std::string(name), error);
}

std::optional<MapFile> loadMapFile(const GameFiles& files, const std::string& name, std::string& error) {
    auto file = files.readResource(name);
    if (!file) {
        error = std::string("map file missing: ") + name;
        return std::nullopt;
    }
    auto body = decodeContainer(*file, files.keys().containerKey);
    if (!body) {
        error = std::string("map file damaged: ") + name;
        return std::nullopt;
    }
    return parseMapFile(*body, error);
}

}  // namespace doc::data
