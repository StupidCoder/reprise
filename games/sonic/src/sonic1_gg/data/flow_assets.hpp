#pragma once
#include "sonic1_gg/data/level_data.hpp"

namespace sonic1_gg {
struct ScreenAssets {
    Bytes patterns, tiles;
};

struct FlowAssets {
    std::array<ScreenAssets, 10>
        screens; // logo, title, first map, second map, ordinary results, ..., game over (9)
    Bytes ending_tables, ending_motion, ending_results;
    Bytes palettes, logo_tables, title_tables, map_tables, map_intro, demo_inputs, voice,
        result_bonus, result_digits, result_emerald;
    Bytes continue_tables; // 12C7-12E8: continue icon block and the two CONTINUE text rows.
    unsigned byte(unsigned address) const;

    unsigned word(unsigned address) const { return byte(address) | (byte(address + 1) << 8); }

    Bytes palette(unsigned background, unsigned sprites) const;
};

FlowAssets extract_flow(const Rom&);
void save_flow(const FlowAssets&, const std::filesystem::path&);
FlowAssets load_flow(const std::filesystem::path&);
} // namespace sonic1_gg
