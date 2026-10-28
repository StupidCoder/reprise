#pragma once
#include "bruce_lee_c64/data/assets.hpp"
#include "bruce_lee_c64/game/playfield.hpp"
#include "c64/text_frame.hpp"
#include <cstdint>

// The screens around play: the title ($0584), the player screen ($0E9B)
// and the game-over screen ($18A6). Their upper rows use the charset at
// $8000 (big letters), the lower ones the text font.
namespace bruce_lee::game {

// The title's text rows (screen rows 10-24) as text codes, built with the
// game's printer and changed by the option keys.
struct TitleText {
    Playfield rows; // only the codes of rows 10-24 are used

    // $064B-$06A1: the three option lines, their key labels and the
    // current choices shown inverted.
    void build(const data::Assets&, bool alternate, bool yamo_player);
    void toggle_alternate(bool now_on);   // $0E59
    void toggle_yamo_player(bool now_on); // $0E40
    void print(const data::Assets&, int text, int row_index, int column); // $17FA, single height

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(rows); }
};

c64::TextFrame title_frame(const data::Assets&, const TitleText&);
c64::TextFrame player_frame(const data::Assets&, int player);
c64::TextFrame game_over_frame(const data::Assets&, int player, std::uint32_t score);

} // namespace bruce_lee::game
