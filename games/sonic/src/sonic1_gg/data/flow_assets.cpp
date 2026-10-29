#include "sonic1_gg/data/flow_assets.hpp"
#include <algorithm>
#include <stdexcept>

namespace sonic1_gg {
unsigned FlowAssets::byte(unsigned address) const {
    if (address >= 0x2255 && address < 0x24b2) {
        return ending_tables.at(address - 0x2255);
    }
    if (address >= 0x1b69 && address < 0x1cd7) {
        return ending_motion.at(address - 0x1b69);
    }
    if (address >= 0x49f0 && address < 0x4a94) {
        return ending_results.at(address - 0x49f0);
    }
    if (address >= 0x1f14 && address < 0x1fae) {
        return logo_tables.at(address - 0x1f14);
    }
    if (address >= 0x43b3 && address < 0x4457) {
        return title_tables.at(address - 0x43b3);
    }
    if (address >= 0x0e0f && address < 0x0e23) {
        return map_intro.at(address - 0x0e0f);
    }
    if (address >= 0x0ea8 && address < 0x11e1) {
        return map_tables.at(address - 0x0ea8);
    }
    if (address >= 0x1306 && address < 0x1356) {
        return demo_inputs.at(address - 0x1306);
    }
    if (address >= 0x12c7 && address < 0x12e9) {
        return continue_tables.at(address - 0x12c7);
    }
    if (address >= 0x4457 && address < 0x449d) {
        return result_bonus.at(address - 0x4457);
    }
    if (address >= 0x4a94 && address < 0x4ad0) {
        return result_digits.at(address - 0x4a94);
    }
    if (address >= 0x4645 && address < 0x464d) {
        return result_emerald.at(address - 0x4645);
    }
    throw std::runtime_error("Unknown frontend data address");
}

Bytes FlowAssets::palette(unsigned background, unsigned sprites) const {
    // 0586 emits 16 background colors, repeats background color zero for
    // sprite color zero, then emits sprite colors 1..15 (D2A7 retains the base).
    if (background >= 56 || sprites >= 56) {
        throw std::runtime_error("Unknown frontend palette");
    }
    Bytes result(palettes.begin() + background * 34, palettes.begin() + background * 34 + 32);
    result.push_back(palettes[background * 34]);
    result.push_back(palettes[background * 34 + 1]);
    result.insert(result.end(), palettes.begin() + sprites * 34 + 2,
                  palettes.begin() + sprites * 34 + 32);
    return result;
}

FlowAssets extract_flow(const Rom& rom) {
    FlowAssets result;
    auto patterns = [&](ScreenAssets& screen, unsigned bg, unsigned sprites, bool common) {
        screen.patterns.resize(0x3800);
        screen.tiles.resize(0x700);
        // The logo initializes all 28 rows to tile 70. Later screens replace
        // 24 rows, leaving the off-screen bottom four rows intact.
        for (unsigned i = 0; i < 0x380; ++i) {
            screen.tiles[i * 2] = 0x70;
        }
        for (auto [offset, address] : std::array<std::pair<unsigned, unsigned>, 3>{
                 {{0, bg}, {0x2000, sprites}, {0x3000, common ? 0x2f354u : 0u}}}) {
            if (!address) {
                continue;
            }
            const auto decoded = decode_pattern_rows(rom, address);
            if (offset + decoded.size() > screen.patterns.size()) {
                throw std::runtime_error("Frontend pattern upload exceeds region");
            }
            std::copy(decoded.begin(), decoded.end(), screen.patterns.begin() + offset);
        }
    };
    // 0502 shares the duplicate-byte RLE format but FF advances over an
    // existing tile. This is how the map's ordinary and priority layers merge.
    auto overlay = [&](ScreenAssets& screen, unsigned address, unsigned size, unsigned attribute) {
        const auto decoded = decode_layout(rom, address, size);
        if (decoded.size() * 2 > screen.tiles.size()) {
            throw std::runtime_error("Frontend tilemap exceeds 32x28");
        }
        for (unsigned i = 0; i < decoded.size(); ++i) {
            if (decoded[i] != 255) {
                screen.tiles[i * 2] = decoded[i];
                screen.tiles[i * 2 + 1] = attribute;
            }
        }
    };
    patterns(result.screens[0], 0x3fa74, 0x1f600, false); // 1CEE/1CF9
    for (unsigned i = 0; i < 0x380; ++i) {
        result.screens[0].tiles[i * 2] = 0x70;
    }
    patterns(result.screens[1], 0x25500, 0x2836f, false); // 42E9/42F4
    overlay(result.screens[1], 0x16738, 0x12d, 0);
    patterns(result.screens[2], 0x30000, 0x28ad0, true); // 0C2B/0C36
    overlay(result.screens[2], 0x16962, 0x19b, 0x10);
    overlay(result.screens[2], 0x16afd, 0x170, 0);
    patterns(result.screens[3], 0x3171a, 0x291a7, true); // 0C89/0C94
    overlay(result.screens[3], 0x16c6d, 0x156, 0x10);
    overlay(result.screens[3], 0x16dc3, 0x198, 0);
    patterns(result.screens[4], 0x26a31, 0, true);
    overlay(result.screens[4], 0x16865, 0x84, 0);
    patterns(result.screens[5], 0x26a31, 0, true);
    overlay(result.screens[5], 0x168e9, 0x79, 0); // 44DA: special results.
    patterns(result.screens[6], 0x30000, 0, true);
    overlay(result.screens[6], 0x16f86, 0x1e4, 0);
    result.screens[7] = result.screens[6];
    overlay(result.screens[7], 0x1716a, 0x1c0, 0);
    patterns(result.screens[8], 0x3171a, 0x2836f, true);
    overlay(result.screens[8], 0x1732a, 0x106, 0);
    // 11F1/11FC: game over. Bank 9 2A31 patterns at VRAM 0 and bank 5 6F5B
    // tile rows. VRAM 3000 keeps the common sprite set from gameplay; its
    // countdown digits match 2F354 (observed on the original).
    patterns(result.screens[9], 0x26a31, 0, true);
    overlay(result.screens[9], 0x16f5b, 0x2b, 0);
    result.ending_tables = rom.slice(0x2255, 0x24b2 - 0x2255);
    result.ending_motion = rom.slice(0x1b69, 0x1cd7 - 0x1b69);
    result.ending_results = rom.slice(0x49f0, 0x4a94 - 0x49f0);
    result.result_bonus = rom.slice(0x4457, 0x46);
    result.result_digits = rom.slice(0x4a94, 0x3c);
    result.result_emerald = rom.slice(0x4645, 8);
    for (unsigned i = 0; i < 56; ++i) {
        auto colors = rom.slice(0x23400 + rom.word(0x23400 + 2 * i), 34);
        result.palettes.insert(result.palettes.end(), colors.begin(), colors.end());
    }
    result.logo_tables = rom.slice(0x1f14, 0x1fae - 0x1f14);
    result.title_tables = rom.slice(0x43b3, 0x4457 - 0x43b3);
    result.map_intro = rom.slice(0x0e0f, 20);
    result.map_tables = rom.slice(0x0ea8, 0x11e1 - 0x0ea8);
    result.demo_inputs = rom.slice(0x1306, 0x50);
    result.continue_tables = rom.slice(0x12c7, 0x22);
    // 23C0A: 5982 packed bytes plus the next nibble used for interpolation.
    result.voice = rom.slice(0x23d24, 0x175f);
    return result;
}

void save_flow(const FlowAssets& data, PackWriter& out) {
    for (unsigned i = 0; i < 10; ++i) {
        out.write("flow_patterns_" + std::to_string(i) + ".bin", data.screens[i].patterns);
        out.write("flow_tiles_" + std::to_string(i) + ".bin", data.screens[i].tiles);
    }
    out.write("flow_ending.bin", data.ending_tables);
    out.write("flow_ending_motion.bin", data.ending_motion);
    out.write("flow_ending_results.bin", data.ending_results);
    out.write("flow_palettes.bin", data.palettes);
    out.write("flow_logo.bin", data.logo_tables);
    out.write("flow_title.bin", data.title_tables);
    out.write("flow_map_intro.bin", data.map_intro);
    out.write("flow_map.bin", data.map_tables);
    out.write("flow_demo.bin", data.demo_inputs);
    out.write("flow_continue.bin", data.continue_tables);
    out.write("flow_voice.bin", data.voice);
    out.write("result_bonus.bin", data.result_bonus);
    out.write("result_digits.bin", data.result_digits);
    out.write("result_emerald.bin", data.result_emerald);
}

FlowAssets load_flow(const std::filesystem::path& directory) {
    FlowAssets result;
    for (unsigned i = 0; i < 10; ++i) {
        result.screens[i].patterns =
            read_file(directory / ("flow_patterns_" + std::to_string(i) + ".bin"));
        result.screens[i].tiles =
            read_file(directory / ("flow_tiles_" + std::to_string(i) + ".bin"));
        if (result.screens[i].patterns.size() != 0x3800 ||
            result.screens[i].tiles.size() != 0x700) {
            throw std::runtime_error("Incomplete frontend screen assets");
        }
    }
    result.ending_tables = read_file(directory / "flow_ending.bin");
    result.ending_motion = read_file(directory / "flow_ending_motion.bin");
    result.ending_results = read_file(directory / "flow_ending_results.bin");
    result.palettes = read_file(directory / "flow_palettes.bin");
    result.logo_tables = read_file(directory / "flow_logo.bin");
    result.title_tables = read_file(directory / "flow_title.bin");
    result.map_intro = read_file(directory / "flow_map_intro.bin");
    result.map_tables = read_file(directory / "flow_map.bin");
    result.demo_inputs = read_file(directory / "flow_demo.bin");
    result.continue_tables = read_file(directory / "flow_continue.bin");
    result.voice = read_file(directory / "flow_voice.bin");
    result.result_bonus = read_file(directory / "result_bonus.bin");
    result.result_digits = read_file(directory / "result_digits.bin");
    result.result_emerald = read_file(directory / "result_emerald.bin");
    if (result.ending_tables.size() != 0x25d || result.ending_motion.size() != 0x16e ||
        result.ending_results.size() != 0xa4 || result.palettes.size() != 56 * 34 ||
        result.logo_tables.size() != 0x9a || result.title_tables.size() != 0xa4 ||
        result.map_tables.size() != 0x339 || result.demo_inputs.size() != 0x50 ||
        result.continue_tables.size() != 0x22 || result.voice.size() != 0x175f ||
        result.result_bonus.size() != 0x46 || result.result_digits.size() != 0x3c ||
        result.result_emerald.size() != 8) {
        throw std::runtime_error("Incomplete frontend data tables");
    }
    return result;
}
} // namespace sonic1_gg
