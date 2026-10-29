// How long the original's loading routines take (CPU clocks), for the time
// model (busy() in game.hpp). The counts follow the routines' instructions
// (Z80 timings, I/O at the documented 11 clocks) and were checked against
// the original running in the tracer (.analysis/triple_trouble_gg/tools/
// measure.py): the logo's tile block (10:8000) decodes in 381873 clocks,
// the model gives 381949.

#include "triple_trouble_gg/game/game.hpp"

namespace triple_trouble_gg {

namespace {

// 2DD7: the decoder's 77-byte buffers cleared, with its CALL.
constexpr unsigned buffer_clear = 17 + 10 + 7 + 4 + (0x4D * 26 - 5) + 10;

} // namespace

// 2DC7 (with its CALL): the buffers cleared, the block decoded tile by
// tile (2DE2: the 2-bit code, then the tile's bytes: code 0 the zero
// buffer, 1 32 bytes copied, 2 a 32-bit mask selecting literal bytes, 3 the
// same with rows XORed down (2E9C)), each tile's 32 bytes written (2F04,
// through the bit-reversing table when mirrored), the buffers cleared again.
unsigned Game::decode_clocks(unsigned bank, unsigned src, bool mirrored) const {
    const auto parts = decode_clock_parts(bank, src, mirrored);
    unsigned t = parts.head + parts.tail;
    for (const unsigned c : parts.tiles) {
        t += c;
    }
    return t;
}

// The same in parts: before the first tile, per tile (with its counting at
// 2E3C), after the last.
Game::DecodeClocks Game::decode_clock_parts(unsigned bank, unsigned src, bool mirrored) const {
    DecodeClocks parts;
    auto b = [&](unsigned a) {
        return unsigned(rom.byte(bank, a & 0xFFFF));
    };
    const unsigned count = b(src + 2) | (b(src + 3) << 8);
    unsigned codes = (src + (b(src + 4) | (b(src + 5) << 8))) & 0xFFFF;
    unsigned data = (src + 6) & 0xFFFF;
    unsigned t = 17 + 11 + buffer_clear + 10 + 15 + 17;
    t += 13 + 11 + 6 + 6 + 7 + 13 + 6 + 7 + 13 + 6 + 7 + 6 + 7 + 6 + 16 + 10 + 11 + 16; // header
    t += 10 + 10 + 10 + 10 + (31 * 21 + 16) + 4 + 13; // zero tile, code position
    const unsigned out = mirrored ? 13 + 4 + 10 + 10 + 7 + (32 * 72 - 5) + 10
                                  : 13 + 4 + 10 + 10 + 7 + (32 * 58 - 5) + 10;
    parts.head = t;
    unsigned phase = 0;
    for (unsigned n = 0; n < count; ++n) {
        t = 0;
        t += 17 + 13 + 7; // 2ECD
        if (phase == 4) {
            t += 7 + 16 + 6 + 16 + 4 + 13;
            ++codes;
            phase = 0;
        } else {
            t += 12;
        }
        t += 4 + 16 + 7 + phase * 32 + 14 + 7 + 11 + 13 + 4 + 13 + 10 + 10;
        const unsigned code = (b(codes) >> (2 * phase)) & 3;
        ++phase;
        t += 7;
        if (code == 0) {
            t += 7 + 17 + 10 + 10 + 10 + (31 * 21 + 16) + out + 12;
        } else if (code == 1) {
            t += 12 + 7 + 12 + 7 + 12;
            t += 17 + 10 + 16 + 10 + 10 + (31 * 21 + 16) + 16 + 10 + 17 + out;
            data += 32;
        } else {
            t += code == 2 ? 12 + 7 + 7 : 12 + 7 + 12 + 7 + 7;
            t += 17 + 14 + 16 + 4 * (7 + 6) + 7; // 2E6B
            const std::uint32_t mask = b(data) | (b(data + 1) << 8) | (b(data + 2) << 16) |
                                       (std::uint32_t(b(data + 3)) << 24);
            data += 4;
            for (unsigned i = 0; i < 32; ++i) {
                if ((mask >> i) & 1) {
                    t += 123;
                    ++data;
                } else {
                    t += 117;
                }
            }
            t = t - 5 + 16 + 10;
            if (code == 3) {
                t += 17 + 14 + 7 + (7 * 261 - 5) + 10; // 2E9C
            }
            t += 17 + out + 12;
        }
        t += 16 + 6 + 16 + 13 + 4 + 12 + 4 + 4 + 4 + (n + 1 < count ? 12 : 7); // 2E3C
        parts.tiles.push_back(t);
        parts.codes_after.push_back(std::uint16_t(codes));
        parts.data_after.push_back(std::uint16_t(data));
    }
    parts.tail = 10 + 14 + buffer_clear + 10;
    return parts;
}

// 26AE (with its CALL): `words` map entries or tile words written.
unsigned Game::fill_clocks(unsigned words) {
    return 17 + 85 + 75 * words + 10;
}

// 2757: the sprite table cleared.
unsigned Game::sprite_clear_clocks() {
    return 10 + 10 + 4 + 7 + (64 * 55 - 5) + 7 + 13 + 10;
}

} // namespace triple_trouble_gg
