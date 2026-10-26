#pragma once
#include "wizball_c64/data/assets.hpp"
#include <array>
#include <cstdint>

// The front end's colour effects ($9884-$999D): runs of colours that move
// along a table a step every few frames, fed from small colour lists. The
// screens' raster handlers paint text, sprites and backgrounds from this
// table, each from its own part of it ($EBC1-$EBF2), so it is kept as one
// table: the logo's 16 colours ($EBC1), a pulse ($EBD1), a run of four
// ($EBD2), a pulse ($EBD6), a run of 13 moving right ($EBD7), a run of 13
// moving left ($EBE4), two pulses ($EBF1, $EBF2). The title starts the
// effects one after another ($9829, by a frame counter $EBF3).
namespace wizball::game {

class ColourRuns {
public:
    explicit ColourRuns(const data::GameImage& image) : image_(image) {}

    void clear(); // $8FA0: $EBB2-$EBF3 cleared

    void title_frame();  // $9829
    void all();          // $9436: every run but the logo's
    void logo();         // $9884
    void run_right();    // $98BE ($EBD7 on)
    void run_left();     // $98E9 ($EBE4 on)
    void run_four();     // $9916 ($EBD2-$EBD5)
    void pulse_d1();     // $994A
    void pulse_d6();     // $996A
    void pulse_f1();     // $997D
    void pulse_f2();     // $999D

    // The table, by the original's address ($EBC1-$EBF2).
    std::uint8_t at(int address) const { return table_[std::size_t(address - first)]; }
    std::uint8_t newest() const { return at(0xEBD7); }

    // The run right's state, which play also moves (the lab's words).
    struct RightRun {
        std::uint8_t clock, index;
        std::array<std::uint8_t, 13> colours;
    };
    RightRun right_run() const;
    void set_right_run(const RightRun& r);
    std::uint8_t right_index() const { return d7_index_; } // $EBB8 (also a phase for the wiztips)

private:
    static constexpr int first = 0xEBC1;
    std::uint8_t& cell(int address) { return table_[std::size_t(address - first)]; }

    const data::GameImage& image_;
    std::array<std::uint8_t, 0x32> table_{};
    std::uint8_t logo_clock_{}, d7_clock_{}, e4_clock_{}, d2_clock_{};  // $EBB2-$EBB5
    std::uint8_t e4_index_{}, d7_index_{}, d1_index_{}, d1_clock_{};    // $EBB7-$EBBA
    std::uint8_t d2_index_{}, f1_clock_{}, f1_index_{}, f2_clock_{};    // $EBBB-$EBBE
    std::uint8_t f2_index_{}, logo_index_{};                            // $EBBF, $EBC0
    std::uint8_t schedule_{};                                           // $EBF3
};

} // namespace wizball::game
