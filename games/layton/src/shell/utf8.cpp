// UTF-8 decoding and encoding for the shell's text (declared in shell/style.hpp; on its own so
// the embedded build, which has no shell drawing, can use it too).
#include "shell/style.hpp"

namespace picarat::shell::style {

char32_t nextCodePoint(std::string_view s, std::size_t& i) {
    const auto byte = [&](std::size_t k) {
        return std::uint8_t(s[k]);
    };
    const std::uint8_t lead = byte(i);
    int length = lead < 0x80         ? 1
                 : (lead >> 5) == 6  ? 2
                 : (lead >> 4) == 14 ? 3
                 : (lead >> 3) == 30 ? 4
                                     : 0;
    if (length > 1 && i + length <= s.size()) {
        char32_t c = lead & (0x7f >> length);
        bool valid = true;
        for (int k = 1; k < length; ++k) {
            valid = valid && (byte(i + k) & 0xc0) == 0x80;
            c = (c << 6) | (byte(i + k) & 0x3f);
        }
        if (valid) {
            i += length;
            return c;
        }
    }
    ++i;
    return lead; // ASCII, or a stray byte read as Latin-1
}

void appendUtf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += char(c);
    } else if (c < 0x800) {
        out += char(0xc0 | (c >> 6));
        out += char(0x80 | (c & 0x3f));
    } else if (c < 0x10000) {
        out += char(0xe0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 0x3f));
        out += char(0x80 | (c & 0x3f));
    } else {
        out += char(0xf0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 0x3f));
        out += char(0x80 | ((c >> 6) & 0x3f));
        out += char(0x80 | (c & 0x3f));
    }
}

} // namespace picarat::shell::style
