#pragma once
#include <array>
#include <cstdint>

// The keyboard as Wizball reads it. Both scans walk the C64 key matrix from
// row 7 down and, in a row, from column 7 down, numbering the keys from 1,
// and keep the number of the last key found down: $73D4 all 64 keys, $7429
// only columns 7-5 of rows 7-5 (RUN/STOP, Q, C=; /, up arrow, =; comma, @,
// colon). The game then acts on a number only when it changes ($B254 is
// the new number, else 0; $B255 the last).
namespace wizball::game {

using KeyMatrix = std::array<bool, 64>; // index row * 8 + column

inline int scan_all_keys(const KeyMatrix& keys) { // $73D4
    int last = 0, code = 0;
    for (int row = 7; row >= 0; --row)
        for (int column = 7; column >= 0; --column) {
            ++code;
            if (keys[std::size_t(row * 8 + column)]) last = code;
        }
    return last;
}

inline int scan_few_keys(const KeyMatrix& keys) { // $7429
    int last = 0, code = 0;
    for (int row = 7; row >= 5; --row)
        for (int column = 7; column >= 5; --column) {
            ++code;
            if (keys[std::size_t(row * 8 + column)]) last = code;
        }
    return last;
}

// $B254/$B255.
struct KeyLatch {
    std::uint8_t last{}, code{};
    void update(int scanned) {
        if (scanned != last) {
            last = std::uint8_t(scanned);
            code = std::uint8_t(scanned);
        } else {
            code = 0;
        }
    }

  public:
    // Save schema v1: explicit values; imported resources stay bound to this instance.
    template <class A> void persist(A &ar) { ar(last, code); }
};

// Key numbers the game tests.
namespace keys {
constexpr int run_stop = 1, quit = 2, louder = 5, quieter = 6; // $7429 numbers
constexpr int space = 4;                                         // $73D4 number
} // namespace keys

} // namespace wizball::game
