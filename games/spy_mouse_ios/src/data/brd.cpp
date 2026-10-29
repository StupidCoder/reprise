#include "data/brd.hpp"

#include "data/obfuscated_file.hpp"

#include <cstring>

namespace doc::data {
namespace {

class Cursor {
public:
    explicit Cursor(const Bytes& b) : b_(b) {}
    bool ok() const { return ok_; }
    std::size_t pos() const { return p_; }
    void seek(std::size_t p) { p_ = p; }
    std::uint8_t u8() {
        if (p_ + 1 > b_.size()) return fail(), 0;
        return b_[p_++];
    }
    std::uint32_t u32be() {
        if (p_ + 4 > b_.size()) return fail(), 0;
        std::uint32_t v = (std::uint32_t(b_[p_]) << 24) | (std::uint32_t(b_[p_ + 1]) << 16) |
                          (std::uint32_t(b_[p_ + 2]) << 8) | b_[p_ + 3];
        p_ += 4;
        return v;
    }
    std::uint16_t u16be() {
        if (p_ + 2 > b_.size()) return fail(), 0;
        std::uint16_t v = std::uint16_t((b_[p_] << 8) | b_[p_ + 1]);
        p_ += 2;
        return v;
    }
    float f32le() {
        if (p_ + 4 > b_.size()) return fail(), 0.0f;
        float f;
        std::memcpy(&f, &b_[p_], 4);  // host is little-endian (ManagerFiles::readFloat copies)
        p_ += 4;
        return f;
    }
    std::string cstr() {
        std::size_t start = p_;
        while (p_ < b_.size() && b_[p_] != 0) ++p_;
        if (p_ >= b_.size()) return fail(), std::string();
        std::string s(reinterpret_cast<const char*>(&b_[start]), p_ - start);
        ++p_;
        return s;
    }

private:
    void fail() { ok_ = false; }
    const Bytes& b_;
    std::size_t p_ = 0;
    bool ok_ = true;
};

bool parseChunk(Cursor& c, BrdNode& node, int depth) {
    if (depth > 64) return false;
    std::size_t start = c.pos();
    std::uint32_t size = c.u32be();
    node.name = c.cstr();
    std::uint32_t attributeCount = c.u32be();
    for (std::uint32_t i = 0; i < attributeCount && c.ok(); ++i) {
        std::string key = c.cstr();
        BrdNode::Value value;
        switch (c.u8()) {
        case 0: value = c.cstr(); break;
        case 1: value = c.u8() != 0; break;
        case 2: value = int(std::int8_t(c.u8())); break;
        case 3: value = int(std::int16_t(c.u16be())); break;
        case 4: value = int(std::int32_t(c.u32be())); break;
        case 5: value = c.f32le(); break;
        default: return false;  // "Unknown var type ... very bad!" in the original
        }
        node.attributes.emplace_back(std::move(key), std::move(value));
    }
    std::uint32_t childCount = c.u32be();
    for (std::uint32_t i = 0; i < childCount && c.ok(); ++i) {
        node.children.emplace_back();
        if (!parseChunk(c, node.children.back(), depth + 1)) return false;
    }
    c.seek(start + size);
    return c.ok();
}

}  // namespace

const BrdNode::Value* BrdNode::find(const std::string& key) const {
    for (const auto& [k, v] : attributes)
        if (k == key) return &v;
    return nullptr;
}

std::optional<int> BrdNode::getInt(const std::string& key) const {
    const Value* v = find(key);
    if (!v) return std::nullopt;
    if (auto* i = std::get_if<int>(v)) return *i;
    return std::nullopt;
}

std::optional<float> BrdNode::getFloat(const std::string& key) const {
    const Value* v = find(key);
    if (!v) return std::nullopt;
    if (auto* f = std::get_if<float>(v)) return *f;
    if (auto* i = std::get_if<int>(v)) return float(*i);
    return std::nullopt;
}

std::optional<std::string> BrdNode::getString(const std::string& key) const {
    const Value* v = find(key);
    if (!v) return std::nullopt;
    if (auto* s = std::get_if<std::string>(v)) return *s;
    return std::nullopt;
}

std::optional<bool> BrdNode::getBool(const std::string& key) const {
    const Value* v = find(key);
    if (!v) return std::nullopt;
    if (auto* b = std::get_if<bool>(v)) return *b;
    return std::nullopt;
}

const BrdNode* BrdNode::child(const std::string& childName) const {
    for (const BrdNode& c : children)
        if (c.name == childName) return &c;
    return nullptr;
}

std::vector<const BrdNode*> BrdNode::childrenNamed(const std::string& childName) const {
    std::vector<const BrdNode*> out;
    for (const BrdNode& c : children)
        if (c.name == childName) out.push_back(&c);
    return out;
}

std::optional<BrdNode> parseChunkTree(const Bytes& body) {
    Cursor c(body);
    BrdNode wrapper;
    if (!parseChunk(c, wrapper, 0) || wrapper.children.empty()) return std::nullopt;
    return std::move(wrapper.children.front());
}

std::optional<BrdNode> loadChunkTree(const GameFiles& files, const std::string& relative) {
    auto file = files.readResource(relative);
    if (!file) return std::nullopt;
    auto body = decodeChunkTree(*file, files.keys().chunkTreeKey);
    if (!body) return std::nullopt;
    return parseChunkTree(*body);
}

}  // namespace doc::data
