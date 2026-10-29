#pragma once
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>

// Services of the GBA BIOS that games call (software interrupts), written
// from GBATEK's descriptions. They are platform services: games use them as
// library routines.
namespace gba {

// SWI 11h/12h: LZ77 decompression. The source starts with a header word
// (type 1 in bits 4-7, decompressed size in bits 8-31), then blocks of a
// flag byte (bit 7 first) and eight items: a literal byte (flag 0) or a
// reference (flag 1: 4 bits length - 3, 12 bits displacement - 1).
inline std::vector<std::uint8_t>
lz77_decompress(const std::function<std::uint8_t(std::uint32_t)>& byte, std::uint32_t source) {
    const std::uint32_t header =
        std::uint32_t(byte(source)) | (std::uint32_t(byte(source + 1)) << 8) |
        (std::uint32_t(byte(source + 2)) << 16) | (std::uint32_t(byte(source + 3)) << 24);
    const std::uint32_t size = header >> 8;
    std::uint32_t at = source + 4;
    std::vector<std::uint8_t> out;
    out.reserve(size);
    while (out.size() < size) {
        std::uint8_t flags = byte(at++);
        for (int i = 0; i < 8 && out.size() < size; ++i, flags = std::uint8_t(flags << 1)) {
            if (flags & 0x80) {
                const std::uint8_t b1 = byte(at++), b2 = byte(at++);
                const std::uint32_t length = (b1 >> 4) + 3u;
                const std::uint32_t back = (std::uint32_t(b1 & 0xF) << 8 | b2) + 1;
                if (back > out.size()) {
                    throw std::runtime_error("LZ77 reference before the start of the output");
                }
                for (std::uint32_t k = 0; k < length && out.size() < size; ++k) {
                    out.push_back(out[out.size() - back]);
                }
            } else {
                out.push_back(byte(at++));
            }
        }
    }
    return out;
}

// SWI 14h/15h: run-length decompression. The source starts with a header
// word (type 3 in bits 4-7, decompressed size in bits 8-31), then blocks: a
// flag byte with bit 7 set repeats the next byte (flag & 7Fh) + 3 times,
// otherwise (flag & 7Fh) + 1 bytes follow as they are.
inline std::vector<std::uint8_t>
rl_decompress(const std::function<std::uint8_t(std::uint32_t)>& byte, std::uint32_t source) {
    const std::uint32_t header =
        std::uint32_t(byte(source)) | (std::uint32_t(byte(source + 1)) << 8) |
        (std::uint32_t(byte(source + 2)) << 16) | (std::uint32_t(byte(source + 3)) << 24);
    const std::uint32_t size = header >> 8;
    std::uint32_t at = source + 4;
    std::vector<std::uint8_t> out;
    out.reserve(size);
    while (out.size() < size) {
        const std::uint8_t flag = byte(at++);
        if (flag & 0x80) {
            const std::uint8_t v = byte(at++);
            for (std::uint32_t k = 0; k < (flag & 0x7Fu) + 3 && out.size() < size; ++k) {
                out.push_back(v);
            }
        } else {
            for (std::uint32_t k = 0; k < (flag & 0x7Fu) + 1 && out.size() < size; ++k) {
                out.push_back(byte(at++));
            }
        }
    }
    return out;
}

// SWI 09h: ArcTan of a tangent in 1.14 fixed point; the result is in
// 1/65536 turns (GBATEK's polynomial).
inline std::int32_t arctan(std::int32_t t) {
    const std::int32_t a = -((t * t) >> 14);
    std::int32_t b = ((0xA9 * a) >> 14) + 0x390;
    b = ((b * a) >> 14) + 0x91C;
    b = ((b * a) >> 14) + 0xFB6;
    b = ((b * a) >> 14) + 0x16AA;
    b = ((b * a) >> 14) + 0x2081;
    b = ((b * a) >> 14) + 0x3651;
    b = ((b * a) >> 14) + 0xA2F9;
    return (t * b) >> 16;
}

// SWI 0Ah: the angle of the vector (x, y), 0000-FFFF for a full turn.
inline std::int32_t arctan2(std::int32_t x, std::int32_t y) {
    const auto at = [](std::int32_t n, std::int32_t d) {
        return arctan((n << 14) / d);
    };
    if (y == 0) {
        return x >= 0 ? 0 : 0x8000;
    }
    if (x == 0) {
        return y >= 0 ? 0x4000 : 0xC000;
    }
    if (y >= 0) {
        if (x >= 0) {
            if (x >= y) {
                return at(y, x);
            }
        } else if (-x >= y) {
            return at(y, x) + 0x8000;
        }
        return 0x4000 - at(x, y);
    }
    if (x <= 0) {
        if (-x > -y) {
            return at(y, x) + 0x8000;
        }
    } else if (x >= -y) {
        return at(y, x) + 0x10000;
    }
    return 0xC000 - at(x, y);
}

} // namespace gba
