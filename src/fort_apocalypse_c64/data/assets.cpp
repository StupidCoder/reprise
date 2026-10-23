// Decoders for the game file's graphics data. Each follows the setup routine
// named in its comment; evidence and the comparison against the game's own
// code running in an emulator are in .analysis/fort_apocalypse_c64/assets.md.

#include "fort_apocalypse_c64/data/assets.hpp"
#include "fort_apocalypse_c64/data/game_file.hpp"
#include "platform/files.hpp"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace fort_apocalypse::data {
namespace {

// Where the data lives in the game file (addresses it loads to).
constexpr std::uint16_t terrain_sources = 0x8D46;  // level -> terrain stream
constexpr std::uint16_t radar_sources = 0x9116;    // level -> radar stream
constexpr std::uint16_t level_multicolours = 0x9107;
constexpr std::uint16_t terrain_run_codes = 0x8D2B; // 23 codes
constexpr std::uint16_t radar_run_codes = 0x8D42;   // 4 codes
constexpr std::uint16_t hud_screen = 0x8603;        // 6 rows x 40 cells
constexpr std::uint16_t sprite_shape_table = 0x86F3; // 14 pointers
constexpr std::uint16_t helicopter_tilt_table = 0xA320;
constexpr std::uint16_t bullet_pattern = 0xB0C2;
constexpr std::uint16_t hud_charset_source = 0xB298;
constexpr std::uint16_t playfield_charset_source = 0xB561;

// Run-length decoding, as the decompressor at $8CDB does it. A byte found in
// the stream's run-code list is followed by a count (0 meaning 256) and
// stands for that many copies of itself; any other byte stands for itself.
// Both are written with the top bit cleared, so a code and its copy with
// the top bit set ($58 and $D8, say) are two run codes for the same output.
// The original only checks for the end after a whole run; what a final run
// writes past the end is returned as the spill.
struct Decoded {
    std::vector<std::uint8_t> bytes;
    Spill spill;
};

Decoded decode_runs(const GameFile& file, std::uint16_t source, std::size_t size,
                    std::span<const std::uint8_t> run_codes) {
    std::vector<std::uint8_t> out;
    out.reserve(size);
    std::uint16_t at = source;
    while (out.size() < size) {
        const std::uint8_t code = file.byte(at++);
        std::size_t count = 1;
        if (std::ranges::find(run_codes, code) != run_codes.end()) {
            count = file.byte(at++);
            if (count == 0) {
                count = 256;
            }
        }
        out.insert(out.end(), count, std::uint8_t(code & 0x7F));
    }
    Spill spill;
    if (out.size() > size) {
        spill = {out.back(), int(out.size() - size)};
        out.resize(size);
    }
    return {std::move(out), spill};
}

// The character sets ($899C): the init copies one continuous stream into
// each set, from a fixed byte offset up to `end`; the rest of the set
// stays empty (its first characters are drawn at run time).
Charset charset(const GameFile& file, std::uint16_t source, std::size_t first_byte,
                std::size_t end) {
    Charset set{};
    const auto data = file.bytes(source, end - first_byte);
    for (std::size_t i = 0; i < data.size(); ++i) {
        const std::size_t at = first_byte + i;
        set[at / 8][at % 8] = data[i];
    }
    return set;
}

// The helicopter shapes ($B044): each pointer in the table locates 18 bytes
// for the left half of the rows followed by 18 for the right half.
std::array<SpriteShape, 14> helicopter_shapes(const GameFile& file) {
    std::array<SpriteShape, 14> shapes{};
    for (std::size_t n = 0; n < shapes.size(); ++n) {
        const auto shape = file.bytes(file.word(std::uint16_t(sprite_shape_table + 2 * n)), 36);
        for (std::size_t row = 0; row < SpriteShape::height; ++row) {
            shapes[n].rows[row] = std::uint16_t(shape[row] << 8 | shape[18 + row]);
        }
    }
    return shapes;
}

// A level ($8F8F, $9038): the terrain stream decodes into 40 rows of 256
// cells, the radar stream into 40 lines of 40 bytes.
Level level(const GameFile& file, int number) {
    Level level;
    const auto terrain = decode_runs(file, file.word(std::uint16_t(terrain_sources + 2 * number)),
                                     TerrainMap::rows * TerrainMap::stored_columns,
                                     file.bytes(terrain_run_codes, 23));
    level.terrain_spill = terrain.spill;
    for (int row = 0; row < TerrainMap::rows; ++row) {
        std::copy_n(terrain.bytes.begin() + row * TerrainMap::stored_columns,
                    TerrainMap::stored_columns, level.terrain.cells[std::size_t(row)].begin());
    }
    const auto radar = decode_runs(file, file.word(std::uint16_t(radar_sources + 2 * number)),
                                   RadarImage::lines * RadarImage::bytes_per_line,
                                   file.bytes(radar_run_codes, 4));
    level.radar_spill = radar.spill;
    for (int y = 0; y < RadarImage::lines; ++y) {
        std::copy_n(radar.bytes.begin() + y * RadarImage::bytes_per_line, RadarImage::bytes_per_line,
                    level.radar.bytes[std::size_t(y)].begin());
    }
    level.multicolour = file.byte(std::uint16_t(level_multicolours + number)) & 0x0F;
    return level;
}
Text text(const GameFile& file, std::uint16_t address) {
    Text result;
    for (std::uint8_t b; (b = file.byte(address++)) != 0xFF;) {
        result.push_back(b);
        if (result.size() > 40) {
            throw std::runtime_error("A Fort Apocalypse text has no end mark.");
        }
    }
    return result;
}

// The texts, at the addresses the title code ($8A2D-$8A8D) and the options
// code ($9393-$9439) pass to the text routine $9B56. The value texts are
// found through the tables at $94BA, $94C0 and $94C6.
FrontEndTexts front_end_texts(const GameFile& file) {
    FrontEndTexts t;
    const std::uint16_t title[] = {0x8B30, 0x8B41, 0x8B50, 0x8B63, 0x8B85};
    for (std::size_t i = 0; i < t.title.size(); ++i) {
        t.title[i] = text(file, title[i]);
    }
    t.copyright = text(file, 0x8B73);
    std::ranges::copy(file.bytes(0x8B7D, t.year.size()), t.year.begin());
    t.options = text(file, 0x943C);
    t.option = text(file, 0x9444);
    t.select = text(file, 0x944B);
    const std::uint16_t names[] = {0x9452, 0x9460, 0x946C};
    const std::uint16_t value_tables[] = {0x94BA, 0x94C0, 0x94C6};
    for (std::size_t s = 0; s < 3; ++s) {
        t.setting_names[s] = text(file, names[s]);
        for (std::size_t v = 0; v < 3; ++v) {
            t.setting_values[s][v] = text(file, file.word(std::uint16_t(value_tables[s] + 2 * v)));
        }
    }
    return t;
}
} // namespace

Assets decode_assets(const c64::Program& program) {
    const GameFile file(program);
    Assets assets;
    // HUD set: the copy fills $500F-$52FF, but $9AB3 clears characters
    // $5C-$7F (from $52E0) before anything is shown: they are the radar
    // window, drawn at run time. The file's bytes for them are never seen.
    assets.hud_charset = charset(file, hud_charset_source, 0x0F, 0x2E0);
    // Playfield set: $5908-$5BFF (characters $21-$7F).
    assets.playfield_charset = charset(file, playfield_charset_source, 0x108, 0x400);

    // The HUD rows ($9BC6) are copied to the screen with the top bit cleared.
    const auto hud = file.bytes(hud_screen, HudScreen::rows * HudScreen::columns);
    for (std::size_t i = 0; i < hud.size(); ++i) {
        assets.hud.cells[i / HudScreen::columns][i % HudScreen::columns] = hud[i] & 0x7F;
    }

    assets.helicopter = helicopter_shapes(file);
    const auto tilt = file.bytes(helicopter_tilt_table, assets.helicopter_tilt.size());
    for (std::size_t i = 0; i < tilt.size(); ++i) {
        // The table holds sprite block numbers; block 1 is shape 0.
        if (tilt[i] < 1 || tilt[i] > assets.helicopter.size()) {
            throw std::runtime_error("The helicopter animation table is not as expected.");
        }
        assets.helicopter_tilt[i] = std::uint8_t(tilt[i] - 1);
    }
    std::ranges::copy(file.bytes(bullet_pattern, assets.bullet_dots.size()),
                      assets.bullet_dots.begin());

    for (int n = 0; n < int(assets.levels.size()); ++n) {
        assets.levels[std::size_t(n)] = level(file, n);
    }
    assets.texts = front_end_texts(file);
    assets.idle_pattern = file.byte(0x7FFF);
    return assets;
}

Assets load_assets(const std::filesystem::path& content_directory) {
    return decode_assets(c64::Program::from_prg(platform::read_file(content_directory / "game.prg")));
}

} // namespace fort_apocalypse::data
