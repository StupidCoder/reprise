#include "wizball_c64/game/colour_runs.hpp"

namespace wizball::game {

void ColourRuns::clear() {
    table_.fill(0);
    logo_clock_ = d7_clock_ = e4_clock_ = d2_clock_ = 0;
    e4_index_ = d7_index_ = d1_index_ = d1_clock_ = 0;
    d2_index_ = f1_clock_ = f1_index_ = f2_clock_ = 0;
    f2_index_ = logo_index_ = 0;
    schedule_ = 0;
}

void ColourRuns::title_frame() {
    // $9829: the effects join in as the counter passes $0A, $23, $5A, $69,
    // $9B, $C3; the last two at $FE, where it stops.
    if (schedule_ != 0xFF) ++schedule_;
    if (schedule_ < 0x0A) return;
    logo();
    if (schedule_ < 0x23) return;
    pulse_d1();
    if (schedule_ < 0x5A) return;
    run_four();
    if (schedule_ < 0x69) return;
    pulse_f2();
    if (schedule_ < 0x9B) return;
    run_right();
    if (schedule_ < 0xC3) return;
    run_left();
    if (schedule_ < 0xFE) return;
    pulse_d6();
    pulse_f1();
}

void ColourRuns::all() {
    // $9436
    run_right();
    run_left();
    run_four();
    pulse_d1();
    pulse_d6();
    pulse_f1();
    pulse_f2();
}

void ColourRuns::logo() {
    // $9884: every second frame the logo's colours spread out from the
    // middle (the left half moves left, the right half right) and a new
    // colour from $B5FB comes in at the middle.
    if (++logo_clock_ != 2) return;
    logo_clock_ = 0;
    for (int y = 0, x = 0x0E; y < 8; ++y, --x) {
        cell(0xEBC1 + y) = cell(0xEBC2 + y);
        cell(0xEBC2 + x) = cell(0xEBC1 + x);
    }
    const auto colour = image_.byte(std::uint16_t(0xB5FB + ((++logo_index_ >> 1) & 0x1F)));
    cell(0xEBC9) = cell(0xEBCA) = colour;
}

void ColourRuns::run_right() {
    // $98BE
    if (++d7_clock_ != 3) return;
    d7_clock_ = 0;
    for (int y = 12; y >= 1; --y) cell(0xEBD7 + y) = cell(0xEBD6 + y);
    cell(0xEBD7) = image_.byte(std::uint16_t(0xB5DF + (++d7_index_ & 7)));
}

void ColourRuns::run_left() {
    // $98E9
    if (++e4_clock_ != 3) return;
    e4_clock_ = 0;
    for (int y = 0; y < 12; ++y) cell(0xEBE4 + y) = cell(0xEBE5 + y);
    cell(0xEBF0) = image_.byte(std::uint16_t(0xB5CB + (++e4_index_ & 7)));
}

void ColourRuns::run_four() {
    // $9916
    if (++d2_clock_ != 3) return;
    d2_clock_ = 0;
    for (int y = 0; y < 3; ++y) cell(0xEBD2 + y) = cell(0xEBD3 + y);
    if (++d2_index_ == 8) d2_index_ = 0;
    cell(0xEBD5) = image_.byte(std::uint16_t(0xB5E7 + d2_index_));
}

void ColourRuns::pulse_d1() {
    // $994A
    if (++d1_clock_ != 7) return;
    d1_clock_ = 0;
    cell(0xEBD1) = image_.byte(std::uint16_t(0xB5EF + (++d1_index_ & 3)));
}

void ColourRuns::pulse_d6() {
    // $996A: in step with $994A.
    if (d1_clock_ != 0) return;
    cell(0xEBD6) = image_.byte(std::uint16_t(0xB5F3 + (d1_index_ & 3)));
}

void ColourRuns::pulse_f1() {
    // $997D
    if (++f1_clock_ != 4) return;
    f1_clock_ = 0;
    cell(0xEBF1) = image_.byte(std::uint16_t(0xB5DF + (++f1_index_ & 7)));
}

void ColourRuns::pulse_f2() {
    // $999D
    if (++f2_clock_ != 4) return;
    f2_clock_ = 0;
    cell(0xEBF2) = image_.byte(std::uint16_t(0xB5CB + (++f2_index_ & 7)));
}

ColourRuns::RightRun ColourRuns::right_run() const {
    RightRun r{d7_clock_, d7_index_, {}};
    for (int i = 0; i < 13; ++i) r.colours[std::size_t(i)] = at(0xEBD7 + i);
    return r;
}

void ColourRuns::set_right_run(const RightRun& r) {
    d7_clock_ = r.clock;
    d7_index_ = r.index;
    for (int i = 0; i < 13; ++i) cell(0xEBD7 + i) = r.colours[std::size_t(i)];
}

} // namespace wizball::game
