#pragma once
#include <cstdint>

// The two random sources every Mind Walker stage carries its own copies of.
namespace mindwalker::world {

// A 32-bit feedback shift register (A $209A7E, T $6049D2, the same routine in
// B, C and P): each step shifts right by one and feeds bit 3 XOR bit 19 into
// bit 31; it returns the low 16 bits of the shifted-out state. A zero state
// stays zero, so the game never seeds it with zero ($FED45123 instead).
struct ShiftRegister {
    std::uint32_t state{};

    std::uint16_t next() {
        const std::uint32_t old = state;
        state = (((old << 16 ^ old) & 0x80000u) << 12) | old >> 1;
        return std::uint16_t(old >> 1);
    }
};

// A mixing generator of three words (A $2099C8 seeds it, $209A20 steps it): a
// shift register as above, a mixing word and a counter. Each step shifts the
// register, then XORs (register AND counter) into the mixing word and returns
// it; the counter counts up by one per step (16 bits, wrapping).
struct Mixer {
    std::uint32_t reg{};
    std::uint16_t mix{};
    std::uint16_t counter{};

    // Zero seeds are replaced by $7531 and $E1D3 (A $2099C8).
    static Mixer seeded(std::uint16_t high, std::uint16_t low) {
        if (high == 0) high = 0x7531;
        if (low == 0) low = 0xE1D3;
        return {std::uint32_t(high) << 16 | low, std::uint16_t(low << 8 ^ high >> 8), std::uint16_t(high ^ 0xABCD)};
    }

    std::uint16_t step() {
        reg = (((reg << 16 ^ reg) & 0x80000u) << 12) | reg >> 1;
        const std::uint16_t count = counter++;
        mix = std::uint16_t(mix ^ (reg & count));
        return mix;
    }
};

} // namespace mindwalker::world
