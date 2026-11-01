// See Message.hpp.
#include "Message.hpp"

#include <cstring>

namespace captain_toad {

namespace {

void appendUtf8(std::string& out, uint32_t c) {
    if (c < 0x80) {
        out += char(c);
    } else if (c < 0x800) {
        out += char(0xc0 | (c >> 6));
        out += char(0x80 | (c & 63));
    } else if (c < 0x10000) {
        out += char(0xe0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 63));
        out += char(0x80 | (c & 63));
    } else {
        out += char(0xf0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 63));
        out += char(0x80 | ((c >> 6) & 63));
        out += char(0x80 | (c & 63));
    }
}

std::string hex(const Bytes& b, size_t at, size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        s += digits[b[at + i] >> 4];
        s += digits[b[at + i] & 15];
    }
    return s;
}

}   // namespace

json::Value readMsbt(const Bytes& b) {
    if (b.size() < 0x20 || std::memcmp(b.data(), "MsgStdBn", 8) != 0 || u16(b, 8) != 0xfeff) {
        throw ImportError("Unsupported MSBT (little-endian MsgStdBn expected)");
    }
    if (u8(b, 0x0c) != 1) {
        throw ImportError("Unsupported MSBT encoding (UTF-16 expected)");
    }
    const uint16_t sections = u16(b, 0x0e);
    json::Value labels = json::Value::object();
    json::Value texts = json::Value::array();
    size_t p = 0x20;
    for (uint16_t s = 0; s < sections && p + 16 <= b.size(); ++s) {
        const std::string magic(reinterpret_cast<const char*>(&b[p]), 4);
        const uint32_t size = u32(b, p + 4);
        const size_t d = p + 16;
        checkRange(b, d, size, "MSBT section");
        if (magic == "LBL1") {
            // Hash slots: (label count, offset); labels: length byte, name, string index.
            const uint32_t slots = u32(b, d);
            for (uint32_t i = 0; i < slots; ++i) {
                const uint32_t count = u32(b, d + 4 + 8 * i), offset = u32(b, d + 8 + 8 * i);
                size_t q = d + offset;
                for (uint32_t k = 0; k < count; ++k) {
                    const uint8_t length = u8(b, q);
                    const std::string name(reinterpret_cast<const char*>(&b[q + 1]), length);
                    labels[name] = u32(b, q + 1 + length);
                    q += 1 + length + 4;
                }
            }
        } else if (magic == "TXT2") {
            const uint32_t count = u32(b, d);
            for (uint32_t i = 0; i < count; ++i) {
                const size_t start = d + u32(b, d + 4 + 4 * i);
                const size_t end = i + 1 < count ? d + u32(b, d + 8 + 4 * i) : d + size;
                json::Value runs = json::Value::array();
                std::string text;
                auto flush = [&] {
                    if (!text.empty()) {
                        runs.push(text);
                        text.clear();
                    }
                };
                for (size_t q = start; q + 1 < end;) {
                    uint32_t c = u16(b, q);
                    q += 2;
                    if (c == 0) {
                        break;
                    }
                    if (c == 0x0e) {   // tag: group, type, parameter size, parameters
                        flush();
                        const uint16_t group = u16(b, q), type = u16(b, q + 2), n = u16(b, q + 4);
                        json::Value tag = json::Value::object();
                        tag["tag"] = json::Value::list(std::vector<int>{group, type});
                        tag["data"] = hex(b, q + 6, n);
                        runs.push(tag);
                        q += 6 + n;
                        continue;
                    }
                    if (c == 0x0f) {   // tag end (version 3): group, type
                        flush();
                        json::Value tag = json::Value::object();
                        tag["end"] = json::Value::list(std::vector<int>{u16(b, q), u16(b, q + 2)});
                        runs.push(tag);
                        q += 4;
                        continue;
                    }
                    if (c >= 0xd800 && c < 0xdc00 && q + 1 < end) {
                        const uint32_t lo = u16(b, q);
                        q += 2;
                        c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
                    }
                    appendUtf8(text, c);
                }
                flush();
                texts.push(runs);
            }
        }
        p = d + ((size + 15) & ~size_t(15));
    }
    json::Value out = json::Value::object();
    out["labels"] = labels;
    out["texts"] = texts;
    return out;
}

}   // namespace captain_toad
