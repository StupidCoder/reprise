#pragma once
#include "c64/program.hpp"
#include "c64/text_frame.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

// Wizball's graphics and level data, decoded from the installed game file
// the way the game's own code reads it (addresses are the original's; the
// evidence is in .analysis/wizball_c64/game.md). Nothing here is animated.
namespace wizball::data {

using Glyph = c64::Glyph;
using Charset = std::array<Glyph, 256>;

// The installed game file, read by C64 address ($0400-$FD00).
class GameImage {
public:
    explicit GameImage(c64::Program game);

    std::uint8_t byte(std::uint16_t address) const;
    std::uint16_t word(std::uint16_t address) const {
        return std::uint16_t(byte(address) | byte(std::uint16_t(address + 1)) << 8);
    }
    std::span<const std::uint8_t> bytes(std::uint16_t address, std::size_t count) const;

private:
    c64::Program game_;
};

// The playfield's three multicolour colours: $D022, $D023 and the
// characters' own colour (colour RAM, multicolour bit included).
struct PlayColours {
    std::uint8_t shared1{}, shared2{}, character{};
};

// A tile: 5 x 5 character codes, row by row ($2642 + 25 * tile; read by
// $78E4, which adds the tile column and then 0, 5, 10, 15, 20 for the rows).
using Tile = std::array<std::uint8_t, 25>;

// The level map: 16 rows of 96 tiles. Row r starts at the address in
// $0460[r] (low) / $0470[r] (high); in this release that is $0532 + 96 * r.
struct LevelMap {
    static constexpr int rows = 16, columns = 96;
    std::array<std::array<std::uint8_t, columns>, rows> tiles{};
    std::uint8_t at(int row, int column) const { return tiles[std::size_t(row)][std::size_t(column)]; }
};

// A place the game can be in ($B128 indexes the tables). Places 0-7 are
// the eight levels: a strip of 4 map rows, 45 tiles wide, the left or the
// right half of a block of 4 rows.
struct Place {
    // $B2F0 + 4 * place: the first map column (stored minus one: $FF or
    // $2C), the first map row, and two bytes not decoded yet.
    int first_column{};
    int first_row{};
    std::array<std::uint8_t, 2> extra{};
    // The 95 characters $A0-$FE of the playfield charset, copied to $FD00
    // by $77C6 from the 760-byte block at the word $B31C + 2 * place.
    std::array<Glyph, 95> characters{};
    std::uint16_t characters_address{};
    // The colours once the level is painted ($B1D1 + 4 * place, applied
    // by $7799 as the paint is collected); places 8-10 have none.
    PlayColours painted{};
    std::uint8_t painted_extra{}; // the fourth byte, stored to $B51A
};

// A sprite frame, 24 x 21 pixels (3 bytes a row). Byte 63 looks like
// $10 + the sprite's colour (hypothesis: not yet traced to the code).
struct Sprite {
    std::array<std::uint8_t, 63> rows{};
    std::uint8_t extra{};
};

struct Assets {
    LevelMap map;
    std::vector<Tile> tiles;                     // $2642, as many as the map uses
    std::vector<Place> places;                   // 11 entries of $B2F0/$B31C
    static constexpr int level_count = 8;
    // The playfield charset's common part, codes $00-$9F ($F800-$FCFF; the
    // file ends at $FD00). Codes $A0-$FE are a place's characters.
    Charset play_charset{};
    // The front-end charset ($F000-$F7FF): letters, digits and the title
    // logo. $8C96 and $8CC7 flip it in place ($F000-$F1FF and $F200-$F7FF;
    // the bytes $F100 and $F6F0 tell which way round each part is; the file
    // holds the first part inverted). Stored here with both flags clear.
    Charset front_charset{};
    // The colours of an unpainted level ($778A).
    PlayColours grey{};
    // Sprite frames $C000-$DFFF (pointers $00-$7F of the VIC bank $C000).
    std::vector<Sprite> sprites;
};

Assets decode_assets(const GameImage&);

// Reads wizball.prg from an installed content folder. Throws
// std::runtime_error when it is missing or not the expected file.
GameImage load_game_image(const std::filesystem::path& content);
Assets load_assets(const std::filesystem::path& content);

} // namespace wizball::data
