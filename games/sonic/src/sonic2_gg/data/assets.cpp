// The tile decoder's running time.

#include "sonic2_gg/data/assets.hpp"

namespace sonic2_gg {

// The decoder's instruction timings (Z80 clocks): the header and the
// buffer clear, then per tile the 2-bit code (1BDA, a new code byte every
// four tiles), the tile's bytes (code 0: the zero buffer; 1: 32 bytes
// copied; 2: a 32-bit mask selecting literal bytes, zeros elsewhere; 3:
// the same, then rows XORed down, 1BA9) and the 32-byte output (1C11,
// through the mirror table when mirrored). Checked against the original
// running in the tracer to within a few clocks per call.
unsigned decode_clocks(const Assets& assets, unsigned bank, unsigned src, bool mirrored) {
    auto b = [&](unsigned a) {
        return assets.byte(bank, a & 0xFFFF);
    };
    const unsigned count = b(src + 2) | (b(src + 3) << 8);
    unsigned codes = (src + (b(src + 4) | (b(src + 5) << 8))) & 0xFFFF;
    unsigned data = (src + 6) & 0xFFFF;
    unsigned t = 13 + 11 + 6 + 6 + 7 + 13 + 6 + 7 + 13 + 6 + 7 + 6 + 7 + 6 + 16 + 10 + 11 + 16;
    t += 10 + 10 + 10 + 10 + (30 * 21 + 16) + 4 + 13;
    const unsigned out =
        mirrored ? 13 + 4 + 10 + 10 + 7 + (32 * (7 + 7 + 7 + 11 + 11 + 10 + 6) + 31 * 13 + 8) + 10
                 : 13 + 4 + 10 + 10 + 7 + (32 * (7 + 11 + 11 + 10 + 6) + 31 * 13 + 8) + 10;
    unsigned phase = 0;
    for (unsigned n = 0; n < count; ++n) {
        t += 17 + 13 + 7; // 1BDA
        if (phase == 4) {
            t += 7 + 16 + 6 + 16 + 4 + 13;
            ++codes;
            phase = 0;
        } else {
            t += 12;
        }
        t += 4 + 16 + 7 + phase * 32 + 14;
        const unsigned code = (b(codes) >> (2 * phase)) & 3;
        t += 7 + 11 + 13 + 4 + 13 + 10 + 10;
        ++phase;
        t += 7;
        if (code == 0) {
            t += 7 + 17 + 10 + 10 + 10 + (31 * 21 + 16) + out + 12;
        } else if (code == 1) {
            t += 12 + 7 + 12 + 7 + 12;
            t += 17 + 10 + 16 + 10 + 10 + (31 * 21 + 16) + 16 + 10;
            data += 32;
            t += 17 + out;
        } else {
            t += 12 + 7 + (code == 2 ? 7 : 12 + 7 + 7);
            t += 17 + 14 + 16 + 7 + 6 + 7 + 6 + 7 + 6 + 7 + 6 + 7; // 1B78
            const std::uint32_t mask = b(data) | (b(data + 1) << 8) | (b(data + 2) << 16) |
                                       (std::uint32_t(b(data + 3)) << 24);
            data += 4;
            for (unsigned i = 0; i < 32; ++i) {
                t += 11 + 8 + 8 + 8 + 8;
                if ((mask >> i) & 1) {
                    t += 12 + 7 + 19 + 6;
                    ++data;
                } else {
                    t += 7 + 19 + 12;
                }
                t += 10 + 10 + 4 + (i < 31 ? 12 : 7);
            }
            t += 16 + 10;
            if (code == 3) {
                t += 17 + 14 + 7 + 7 * (228 + 20) + 6 * 13 + 8 + 10; // 1BA9
            }
            t += 17 + out + 12;
        }
        t += 16 + 6 + 16 + 4 + 4 + (n + 1 < count ? 12 : 7); // 1B5A
    }
    return t + 10;
}

} // namespace sonic2_gg
