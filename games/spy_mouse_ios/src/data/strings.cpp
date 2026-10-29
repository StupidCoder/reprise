#include "data/strings.hpp"

#include "data/mach_o.hpp"
#include "data/program_layout.hpp"

namespace doc::data {
namespace {

using layout::kStrIdEnd;
using layout::kStrIdStart;
constexpr std::uint32_t kStrIdModulus = 0x106D643D;

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(char(cp));
    } else if (cp < 0x800) {
        out.push_back(char(0xC0 | (cp >> 6)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(char(0xE0 | (cp >> 12)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    }
}

// A tiny reader for the Thumb-2 instructions the decision tree is built from.
class Thumb {
public:
    Thumb(const Bytes& code, std::uint32_t base) : code_(code), base_(base) {}
    bool has(std::uint32_t a, std::uint32_t n = 2) const { return a >= base_ && a + n <= base_ + code_.size(); }
    std::uint16_t hw(std::uint32_t a) const {
        if (!has(a)) return 0;
        const std::size_t i = a - base_;
        return std::uint16_t(code_[i] | code_[i + 1] << 8);
    }
    std::uint8_t byte(std::uint32_t a) const { return has(a, 1) ? code_[a - base_] : 0; }

    static std::uint32_t imm16(std::uint16_t h1, std::uint16_t h2) {
        return ((h1 & 0xFu) << 12) | (((h1 >> 10) & 1u) << 11) | (((h2 >> 12) & 7u) << 8) | (h2 & 0xFFu);
    }
    // Modified immediate of 32-bit data-processing instructions.
    static std::uint32_t modImm(std::uint16_t h1, std::uint16_t h2) {
        const std::uint32_t i = (h1 >> 10) & 1, imm3 = (h2 >> 12) & 7, a = h2 & 0xFF;
        const std::uint32_t code = (i << 3) | imm3;
        if (code < 4) {
            switch (code) {
            case 0: return a;
            case 1: return (a << 16) | a;
            case 2: return (a << 24) | (a << 8);
            default: return (a << 24) | (a << 16) | (a << 8) | a;
            }
        }
        const std::uint32_t rot = (code << 1) | (a >> 7);
        const std::uint32_t x = 0x80 | (a & 0x7F);
        return (x >> rot) | (x << (32 - rot));
    }
    std::optional<std::uint32_t> movwR0(std::uint32_t a) const {
        const std::uint16_t h1 = hw(a), h2 = hw(a + 2);
        if ((h1 & 0xFBF0) == 0xF240 && ((h2 >> 8) & 0xF) == 0) return imm16(h1, h2);
        return std::nullopt;
    }
    std::optional<std::uint32_t> movtR0(std::uint32_t a) const {
        const std::uint16_t h1 = hw(a), h2 = hw(a + 2);
        if ((h1 & 0xFBF0) == 0xF2C0 && ((h2 >> 8) & 0xF) == 0) return imm16(h1, h2);
        return std::nullopt;
    }
    // "r0 = id; bx lr" (movs, movw or mov.w).
    std::optional<int> returnValue(std::uint32_t a) const {
        const std::uint16_t h = hw(a), h2 = hw(a + 2);
        if ((h & 0xFF00) == 0x2000 && h2 == 0x4770) return h & 0xFF;
        if (auto v = movwR0(a); v && hw(a + 4) == 0x4770) return int(*v);
        if ((h & 0xFBEF) == 0xF04F && ((h2 >> 8) & 0xF) == 0 && hw(a + 4) == 0x4770) return int(modImm(h, h2));
        return std::nullopt;
    }
    // r0 = constant as (movw | movs #imm8) followed by an optional movt; returns the end.
    std::optional<std::uint32_t> constantR0(std::uint32_t a, std::uint32_t& end) const {
        std::uint32_t lo = 0, n = 4;
        if (auto v = movwR0(a)) {
            lo = *v;
        } else if ((hw(a) & 0xFF00) == 0x2000 && movtR0(a + 2)) {
            lo = hw(a) & 0xFF;
            n = 2;
        } else {
            return std::nullopt;
        }
        if (auto hi = movtR0(a + n)) {
            end = a + n + 4;
            return (*hi << 16) | lo;
        }
        end = a + n;
        return lo;
    }
    int bneLength(std::uint32_t a) const {
        const std::uint16_t h = hw(a);
        if ((h & 0xFF00) == 0xD100) return 2;
        if ((h & 0xFBC0) == 0xF040 && (hw(a + 2) & 0xD000) == 0x8000) return 4;
        return 0;
    }
    int bhiLength(std::uint32_t a) const {
        const std::uint16_t h = hw(a);
        if ((h & 0xFF00) == 0xD800) return 2;
        if ((h & 0xFBC0) == 0xF200 && (hw(a + 2) & 0xD000) == 0x8000) return 4;
        return 0;
    }
    std::optional<std::uint32_t> beqTarget(std::uint32_t a) const {
        const std::uint16_t h = hw(a), h2 = hw(a + 2);
        if ((h & 0xFF00) == 0xD000) {
            const int off = std::int8_t(h & 0xFF);
            return std::uint32_t(std::int64_t(a) + 4 + off * 2);
        }
        if ((h & 0xFBC0) == 0xF000 && (h2 & 0xD000) == 0x8000) {
            const std::uint32_t s = (h >> 10) & 1, imm6 = h & 0x3F, j1 = (h2 >> 13) & 1, j2 = (h2 >> 11) & 1;
            std::int32_t off = std::int32_t((s << 20) | (j2 << 19) | (j1 << 18) | (imm6 << 12) | ((h2 & 0x7FF) << 1));
            if (s) off -= 1 << 21;
            return std::uint32_t(std::int64_t(a) + 4 + off);
        }
        return std::nullopt;
    }

private:
    const Bytes& code_;
    std::uint32_t base_;
};

std::map<std::uint32_t, int> readNameIds(const ExecutableImage& exe) {
    std::map<std::uint32_t, int> ids;
    auto code = exe.read(kStrIdStart, kStrIdEnd - kStrIdStart);
    if (!code) return ids;
    const Thumb t(*code, kStrIdStart);
    for (std::uint32_t a = kStrIdStart; a < kStrIdEnd; a += 2) {
        std::uint32_t b = 0;
        auto key = t.constantR0(a, b);
        if (!key) continue;
        if (t.hw(b) == 0x4281) {  // cmp r1, r0
            if (int n = t.bneLength(b + 2)) {
                if (auto v = t.returnValue(b + 2 + std::uint32_t(n))) ids[*key] = *v;
            } else if (auto target = t.beqTarget(b + 2)) {
                if (auto v = t.returnValue(*target)) ids[*key] = *v;
            }
        } else if (t.hw(b) == 0x4401) {  // add r1, r0: a switch over hashes -key .. -key + top
            std::uint32_t c = b + 2;
            std::uint32_t top = 0;
            const std::uint16_t h = t.hw(c);
            if ((h & 0xFF00) == 0x2900) {
                top = h & 0xFF;
                c += 2;
            } else if ((h & 0xFBF0) == 0xF1B0 && (h & 0xF) == 1 && (t.hw(c + 2) & 0x0F00) == 0x0F00) {
                top = Thumb::modImm(h, t.hw(c + 2));
                c += 4;
            } else {
                continue;
            }
            const int n = t.bhiLength(c);
            if (!n) continue;
            c += std::uint32_t(n);
            const std::uint16_t h2 = t.hw(c + 2);
            if (t.hw(c) != 0xE8DF || ((h2 & 0xFFF0) != 0xF010 && (h2 & 0xFFF0) != 0xF000)) continue;
            const bool half = (h2 & 0xFFF0) == 0xF010;
            const std::uint32_t table = c + 4;
            for (std::uint32_t i = 0; i <= top && i < 4096; ++i) {
                const std::uint32_t off = half ? t.hw(table + 2 * i) : t.byte(table + i);
                if (auto v = t.returnValue(table + 2 * off)) ids[i - *key] = *v;
            }
        }
    }
    return ids;
}

}  // namespace

std::uint32_t StringTable::nameHash(const std::string& name) {
    std::uint32_t h = 5381;
    for (char c : name) h = h * 33 + std::uint32_t(std::int32_t(static_cast<signed char>(c)));
    return h % kStrIdModulus;
}

std::optional<StringTable> StringTable::load(const GameFiles& files, const std::string& language, std::string& error) {
    auto bytes = files.readResource("text/text_" + language + ".txt");
    if (!bytes || bytes->size() < 2 || (*bytes)[0] != 0xFF || (*bytes)[1] != 0xFE) {
        error = "text table missing";
        return std::nullopt;
    }
    StringTable table;
    std::string line;
    bool escape = false;
    for (std::size_t i = 2; i + 1 < bytes->size(); i += 2) {
        const std::uint32_t c = (*bytes)[i] | ((*bytes)[i + 1] << 8);
        if (c == '\r') continue;
        if (c == '\n') {
            table.strings_.push_back(std::move(line));
            line.clear();
            escape = false;
            continue;
        }
        if (escape) {
            escape = false;
            if (c == 'n') {
                line.back() = '\n';  // "\n": a line break
                continue;
            }
        } else {
            escape = c == '\\';
        }
        appendUtf8(line, c);
    }
    if (!line.empty()) table.strings_.push_back(std::move(line));
    table.ids_ = readNameIds(files.executable());
    return table;
}

const std::string& StringTable::text(int id) const {
    static const std::string empty;
    return id >= 0 && id < int(strings_.size()) ? strings_[std::size_t(id)] : empty;
}

int StringTable::id(const std::string& name) const {
    auto it = ids_.find(nameHash(name));
    return it == ids_.end() ? -1 : it->second;
}

}  // namespace doc::data
