#pragma once
#include "gamegear/video.hpp"
#include "gg_engine/types.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

// What the game has set up to be shown: tile art, the background tile map,
// the sprites, the colours and the scroll. render() makes the Game Gear's
// 160x144 picture from it.
//
// The Game Gear keeps all of this in its video chip: 16 KiB of video memory
// (tile art, then the background map and the sprite table at its end), 32
// colours and a few settings. Here each part has its own type. Of the
// chip's settings, the games change whether the picture is shown, whether
// the line interrupt may fire, and the scroll; the rest stays as the
// cartridges set it at power-on (Triple Trouble: table 265A), the same in
// both: 8x16 sprites using tiles 0-255, the background map at 3800, the
// sprite table at 3F00, backdrop colour 16. Sonic 2 also blanks the leftmost
// 8 columns of the 256-pixel picture, which lie outside the LCD's columns
// 48-207 and so never show. render() uses those settings.
//
// One property of the shared memory remains visible: background tiles
// 448-511 have no art of their own. Their pixels are the bytes of the
// background map (448-503) and of the sprite table (504-511), and both games
// show such tiles (Sonic 2: map entries FFFF and 7FFF on some screens; Triple
// Trouble's title writes art past tile 447).
namespace gg_engine {

struct Display {
    // 8x8 pixels in the cartridge's format: per row, four bit planes, the
    // leftmost pixel in bit 7.
    using Tile = std::array<u8, 32>;
    static constexpr unsigned art_tiles = 448;
    std::array<Tile, art_tiles> tiles{};

    // The background: 32 x 28 entries, row by row. Bits 0-8 tile, 9 mirrored,
    // 10 upside down, 11 sprite colours, 12 in front of sprites.
    static constexpr int map_columns = 32, map_rows = 28;
    std::array<std::uint16_t, map_columns * map_rows> map{};

    // The sprite table as shown: 64 entries; a Y of D0 ends it. A sprite is
    // drawn one line below its Y; X 0-7 is under the blank column.
    struct Sprite {
        u8 y{}, x{}, tile{};
        // For larger views (each game's sprite slots): the full position
        // when `placed`, the table entry and the object type that placed it.
        int full_x{}, full_y{};
        bool placed{}, repeat{};
        u8 entry{}, owner{};
    };

    std::array<Sprite, 64> sprites{};

    // Video memory between the sprite table's Y and X parts (3F40-3F7F):
    // unused, but tile art may be written there.
    std::array<u8, 64> gap{};

    // 32 colours, two bytes each (----BBBB GGGGRRRR): 0-15 background,
    // 16-31 sprites.
    std::array<u8, 64> colors{};

    bool shown{};          // The picture is on (otherwise the backdrop colour)
    bool level_shown{};    // The background is the act's map, not `map`
    bool line_interrupt{}; // The water line may split the picture
    u8 scroll_x{}, scroll_y{};

    // Colours replaced while the picture is drawn: from picture line `line`
    // (0 = the top of the 192-line picture) on. Cleared every frame.
    struct ColorChange {
        int line;
        std::array<u8, 64> colors;
    };

    std::vector<ColorChange> changes;

    // Horizontal scrolls set while the picture is drawn: from picture line
    // `line` on. Cleared every frame.
    struct ScrollChange {
        int line;
        u8 scroll_x;
    };

    std::vector<ScrollChange> scroll_changes;

    // Writes `bytes` of tile art starting `offset` bytes into tile 0 (the
    // cartridge loads art by byte position).
    void put_art(unsigned offset, std::span<const u8> bytes);
    void put_byte(unsigned offset, u8 value);
    void fill_art(unsigned offset, std::size_t count, u8 value);

    // While `level_shown`, the background map's cells come from the act's
    // map: the entry a cell (column 0-31, row 0-27) holds around the camera.
    using LevelBackground = std::function<std::uint16_t(int column, int row)>;

    // The background map entry of a cell.
    std::uint16_t cell(int column, int row, const LevelBackground& level = {}) const {
        return level_shown && level ? level(column, row)
                                    : map[std::size_t(row * map_columns + column)];
    }

    // A byte of a tile, as the picture uses it (tiles 448-511: the map and
    // the sprite table, see above).
    u8 tile_byte(unsigned tile, unsigned offset, const LevelBackground& level = {}) const;

    // The four bit-plane bytes of a tile's row (`tile` may run past 511
    // into the next tiles, as 8x16 sprites do).
    std::array<u8, 4> tile_row(unsigned tile, unsigned row,
                               const LevelBackground& level = {}) const {
        return {tile_byte(tile, row * 4, level), tile_byte(tile, row * 4 + 1, level),
                tile_byte(tile, row * 4 + 2, level), tile_byte(tile, row * 4 + 3, level)};
    }

    // A cell of the background map (column 0-31, row 0-27). The cartridge
    // names cells by their name table address, 3800 + 64 * row + 2 * column.
    struct Cell {
        int column, row;
    };

    std::uint16_t& entry(Cell c) { return map.at(std::size_t(c.row * map_columns + c.column)); }

    // The 160x144 picture (with `level` while the act's map is shown).
    gg::Screen render(const LevelBackground& level = {}) const;
};

} // namespace gg_engine
