#pragma once
#include "sonic_advance_gba/game/types.hpp"
#include <array>

// Video memory for sprite tiles handed out to the screens that animate
// sprites without fixed places (08006B54 allocates, 08006C34 frees): the
// last part of sprite tile memory (from 0x13200, 0x270 four-byte units:
// 0x9C blocks of four tiles), first fit in whole blocks. The map (03004A30)
// holds a run's length at its first block, 0 elsewhere; it is cleared only
// at power-on, so screens free what they take.
namespace sonic_advance_gba {

class SpriteTiles {
public:
    static constexpr u32 start = 0x13200;          // 03001200 = 06013200
    static constexpr unsigned blocks = 0x270 >> 2; // 03001148 = 0x270

    // A video memory offset for `tiles` tiles.
    u32 allocate(u32 tiles) {
        const u32 need = (tiles + 3) >> 2;
        for (u32 i = 0; i < blocks; i = (i + 1) & 0xFFFF) {
            if (map_[i] == 0) {
                u32 j = 0;
                for (; j < need; j = (j + 1) & 0xFFFF) {
                    if (i + j >= blocks) {
                        throw Unrecovered(
                            "sprite tile memory full (08006B54 returns its fallback)");
                    }
                    if (map_[i + j] != 0) {
                        break;
                    }
                }
                if (j == need) {
                    map_[i] = u16(need);
                    return start + i * 0x80;
                }
            } else {
                // Skip the run (the loop adds the last one).
                i = (map_[i] + i - 1) & 0xFFFF;
            }
        }
        throw Unrecovered("sprite tile memory full (08006B54 returns its fallback)");
    }

    void free(u32 offset) { map_.at((offset - start) >> 7) = 0; }

    // Run lengths by block (for the lockstep harness).
    const std::array<u16, 0x100>& map() const { return map_; }

private:
    std::array<u16, 0x100> map_{};
};

} // namespace sonic_advance_gba
