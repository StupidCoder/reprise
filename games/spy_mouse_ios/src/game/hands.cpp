#include "game/hands.hpp"

namespace doc::game {

PokerHand pokerHand(const int (&counts)[5]) {
    int top = 0, second = 0, total = 0;
    for (int c : counts) {
        total += c;
        if (c > top) {
            second = top;
            top = c;
        } else if (c > second) {
            second = c;
        }
    }
    if (total == 5 && top == 1 && second == 1) return PokerHand::Straight;
    switch (top) {
    case 2: return second == 2 ? PokerHand::TwoPair : PokerHand::Pair;
    case 3: return second > 1 ? PokerHand::FullHouse : PokerHand::Threes;
    case 4: return PokerHand::Fours;
    case 5: return PokerHand::Flush;
    default: return PokerHand::Bluff;
    }
}

void handKinds(const int (&counts)[5], int& top, int& second) {
    top = second = -1;
    int best = 0, next = 0;
    if (counts[0] > 0) {
        top = 0;
        best = counts[0];
    }
    for (int k = 1; k < 5; ++k) {
        const int c = counts[k];
        if (best < c) {
            if (best > 1) second = top;
            top = k;
            next = best;
            best = c;
        } else if (next < c) {
            next = c;
            if (c > 1) second = k;
        }
    }
}

}  // namespace doc::game
