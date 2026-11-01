// See Archive.hpp. General container formats used across the game's RomFS.
#include "Archive.hpp"

#include <functional>
#include <set>

namespace captain_toad {

// Yaz0: an LZ77 variant. After the 16-byte header (magic, big-endian output size), each flag
// byte describes eight items, high bit first: a set bit copies one literal byte, a clear bit is
// a back-reference (12-bit distance, 4-bit length; length 0 means an extra length byte + 18).
Bytes yaz0(const Bytes& raw) {
    if (raw.size() < 16 || std::memcmp(raw.data(), "Yaz0", 4) != 0) {
        return raw;
    }
    const size_t n = be32(raw, 4);
    if (n > 256u * 1024 * 1024) {
        throw ImportError("Unreasonable Yaz0 output");
    }
    Bytes out;
    out.reserve(n);
    size_t pos = 16;
    while (out.size() < n) {
        const uint8_t flags = u8(raw, pos++);
        for (int bit = 7; bit >= 0 && out.size() < n; --bit) {
            if (flags & (1 << bit)) {
                out.push_back(u8(raw, pos++));
                continue;
            }
            const uint8_t x = u8(raw, pos);
            const uint8_t y = u8(raw, pos + 1);
            pos += 2;
            const size_t distance = (size_t(x & 15) << 8 | y) + 1;
            size_t count = x >> 4;
            count = count ? count + 2 : size_t(u8(raw, pos++)) + 18;
            if (distance > out.size() || out.size() + count > n) {
                throw ImportError("Invalid Yaz0 backreference");
            }
            for (size_t k = 0; k < count; ++k) {
                out.push_back(out[out.size() - distance]);
            }
        }
    }
    return out;
}

// SARC: a flat archive. SFAT lists (name hash, name offset, data start, data end) per member;
// SFNT holds the names. Member names are verified against their hash and rejected when they
// could escape the output folder.
std::vector<SarcMember> sarc(const Bytes& raw) {
    if (raw.size() < 0x14 || std::memcmp(raw.data(), "SARC", 4) != 0) {
        throw ImportError("Not a SARC archive");
    }
    if (!(raw[6] == 0xff && raw[7] == 0xfe)) {
        throw ImportError("Big-endian SARC is not supported");
    }
    const size_t h = u16(raw, 4);
    const size_t data = u32(raw, 12);
    checkRange(raw, h, 12, "SARC allocation table");
    if (data > raw.size()) {
        throw ImportError("Invalid SARC data offset");
    }
    if (u32(raw, 8) != raw.size() || std::memcmp(raw.data() + h, "SFAT", 4) != 0) {
        throw ImportError("Invalid SARC");
    }
    const size_t fatLength = u16(raw, h + 4);
    const size_t count = u16(raw, h + 6);
    const uint32_t multiplier = u32(raw, h + 8);
    const size_t entries = h + fatLength;
    const size_t fnt = entries + count * 16;
    checkRange(raw, entries, count * 16, "SARC entries");
    checkRange(raw, fnt, 8, "SARC name table");
    if (std::memcmp(raw.data() + fnt, "SFNT", 4) != 0) {
        throw ImportError("Invalid SARC name table");
    }
    const size_t names = fnt + u16(raw, fnt + 4);
    std::vector<SarcMember> out;
    for (size_t i = 0; i < count; ++i) {
        const size_t e = entries + i * 16;
        const uint32_t hash = u32(raw, e);
        const uint32_t name = u32(raw, e + 4);
        const uint32_t start = u32(raw, e + 8);
        const uint32_t end = u32(raw, e + 12);
        if (name >> 24 != 1 || start > end || end > raw.size() - data) {
            throw ImportError("Invalid SARC entry");
        }
        std::string path = cstring(raw, names + 4 * (name & 0xffffff));
        uint32_t value = 0;
        for (unsigned char c : path) {
            value = value * multiplier + c;
        }
        if (value != hash || path.find("..") != std::string::npos || path.empty() || path[0] == '/' ||
            path.find('\\') != std::string::npos || path.find(':') != std::string::npos) {
            throw ImportError("Invalid SARC member name");
        }
        out.push_back({path, slice(raw, data + start, end - start)});
    }
    return out;
}

const Bytes& member(const std::vector<SarcMember>& archive, const std::string& name) {
    for (const auto& m : archive) {
        if (m.name == name) {
            return m.data;
        }
    }
    throw ImportError("Archive lacks " + name);
}

// BYML: a binary JSON-like document. A key table and a string table, then a tree of
// dictionaries (0xc1) and arrays (0xc0) whose entries are typed values (string 0xa0, bool 0xd0,
// int 0xd1, float 0xd2, uint 0xd3, null 0xff) or offsets of nested containers.
json::Value byml(const Bytes& b) {
    if (b.size() < 16 || std::memcmp(b.data(), "YB\x02\0", 4) != 0) {
        throw ImportError("Unsupported BYML");
    }
    auto strings = [&](size_t p) {
        std::vector<std::string> result;
        if (!p) {
            return result;
        }
        if (u8(b, p) != 0xc2) {
            throw ImportError("Invalid BYML string table");
        }
        const size_t n = u24(b, p + 1);
        for (size_t i = 0; i < n; ++i) {
            result.push_back(cstring(b, p + u32(b, p + 4 + i * 4)));
        }
        return result;
    };
    const auto keys = strings(u32(b, 4));
    const auto values = strings(u32(b, 8));
    // Containers being read. Placement documents link objects to each other and back (a door and
    // its switch); a container met again inside itself becomes a reference {"$ref": offset}.
    std::set<size_t> active;
    std::function<json::Value(size_t)> node;

    auto value = [&](uint8_t t, uint32_t v) -> json::Value {
        switch (t) {
        case 0xa0:
            return values.at(v);
        case 0xc0:
        case 0xc1:
            return node(v);
        case 0xd0:
            if (v > 1) {
                throw ImportError("Invalid BYML bool");
            }
            return bool(v);
        case 0xd1:
            return int32_t(v);
        case 0xd2: {
            float f;
            std::memcpy(&f, &v, 4);
            return double(f);
        }
        case 0xd3:
            return v;
        case 0xff:
            return nullptr;
        }
        throw ImportError("Unsupported BYML type");
    };

    node = [&](size_t p) -> json::Value {
        if (!active.insert(p).second) {
            json::Value ref = json::Value::object();
            ref["$ref"] = p;
            return ref;
        }
        const uint8_t t = u8(b, p);
        const size_t n = u24(b, p + 1);
        json::Value result;
        if (t == 0xc1) {
            // Dictionary: n entries of (key index u24, type u8, value u32).
            result = json::Value::object();
            for (size_t i = 0; i < n; ++i) {
                const size_t o = p + 4 + i * 8;
                const std::string& k = keys.at(u24(b, o));
                if (result.has(k)) {
                    throw ImportError("Duplicate BYML key");
                }
                result[k] = value(u8(b, o + 3), u32(b, o + 4));
            }
        } else if (t == 0xc0) {
            // Array: n type bytes, padded to 4, then n values.
            result = json::Value::array();
            const size_t v = (p + 4 + n + 3) & ~size_t(3);
            for (size_t i = 0; i < n; ++i) {
                result.push(value(u8(b, p + 4 + i), u32(b, v + i * 4)));
            }
        } else {
            throw ImportError("Unsupported BYML container");
        }
        active.erase(p);
        return result;
    };
    // An empty document has no root container.
    if (u32(b, 12) == 0) {
        return json::Value();
    }
    return node(u32(b, 12));
}

}   // namespace captain_toad
