#pragma once
#include "amiga/iff.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The panel on the left of the play view (.analysis/cannon_fodder_amiga/
// game.md "The panel").
//
// The original shows it with three attached hardware-sprite pairs: a
// 48 x 224 picture in the colours 16-31, laid over the play view's left
// columns. It composes that picture from pieces of `pstuff.lbm` (piece table
// $89658: x, y, width, height - 1): opaque pieces on 16-pixel columns
// ($895BC/$894F0/$9C65A, every pixel copied) and masked pieces at any x
// ($8A0E0/$8A1D0, pixels of colour 0 left alone).
namespace fodder::game {

constexpr int panel_width = 48;
constexpr int panel_height = 224;

// A soldier on the panel: one roster entry ($80664, 12 bytes each).
struct PanelSoldier {
    int recruit{}; // index into the recruit names ($917E0, 9 bytes each)
    int rank{};    // 0-15
    int squad{};   // the squad its object belongs to
    int name_colour{}; // roster byte 9, bit 0: the selected squad's name colour
    bool shown{true};  // alive or dying (the test at $8925A)
};

// The state the panel shows, per squad (up to 3).
struct PanelState {
    int selected_squad{};
    std::array<int, 3> soldiers{};  // $815E0
    std::array<int, 3> grenades{}, rockets{};
    std::array<int, 3> weapon{};    // $821CC: 1 grenades, 3 rockets
    bool marking{};                 // $821D4: names are being picked (the split)
    std::array<int, 3> icon{};      // $81F58 ($88E30): 0 one soldier, 1 more; vehicles open
    std::vector<PanelSoldier> roster; // in roster order
    bool globe{true}; // every mission but the last ($88454)
};

class PanelArt {
public:
    // `program_file` = the installed `fodderc`; `pstuff` = pstuff.lbm.
    PanelArt(std::span<const std::uint8_t> program_file, amiga::IlbmPicture pstuff);

    struct Piece {
        int x{}, y{}, width{}, lines{};
    };
    Piece piece(int number) const;
    std::string recruit_name(int recruit) const; // up to 6 letters, as stored
    int word(std::uint32_t address) const;       // a program word ($80000 layout)
    int byte(std::uint32_t address) const;
    const amiga::IlbmPicture& pstuff() const { return pstuff_; }

private:
    std::vector<std::uint8_t> program_;
    amiga::IlbmPicture pstuff_;
};

// The panel as colour indices 0-15 (0 = transparent; the screen colour is
// 16 + index), 48 x 224, row by row, built as at the start of a mission
// ($88566): all colour 15, every squad's block ($88C6A; a later squad 8
// lines below the previous one's last bar), and the globe ($88472).
// The ammunition counts as $892FC.
std::vector<std::uint8_t> compose_panel(const PanelArt& art, const PanelState& state);

// The hill's column ($A63BA): HEROES (the hall of fame, five places) and
// SQUAD (the recruits going into the phase), 48 x 224 like the panel.
struct HillColumn {
    struct Entry {
        int recruit{}, rank{}, kills{};
    };
    std::vector<Entry> fame, squad;
};
std::vector<std::uint8_t> compose_hill_column(const PanelArt& art, const HillColumn& column);

} // namespace fodder::game
