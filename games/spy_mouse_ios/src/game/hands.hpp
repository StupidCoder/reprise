// Poker hands of a cheese trail: the five cheese kinds play like card ranks, and the mix the
// mouse carries home multiplies the trail's points (Player::getCurrentHand @0x120cf4,
// getPokerHand @0x220400, Player::scoreCurrentTrail @0x120e40).
#pragma once

namespace doc::game {

enum class PokerHand : int {
    Bluff = 0,
    Pair = 1,
    Threes = 2,
    TwoPair = 3,
    Fours = 4,
    FullHouse = 5,
    Straight = 6,  // five cheeses, all different (the order along the trail does not matter)
    Flush = 7,     // exactly five of one kind
};

// counts: number of carried cheeses per kind (types 6..10). Only the two largest counts and
// the total decide the hand; six or more of one kind is no hand at all.
PokerHand pokerHand(const int (&counts)[5]);

// The kinds (0..4 = types 6..10) of the hand's largest and second group as getCurrentHand
// records them for the HUD (-1 if none): the first kind with the most cheese, and the kind of the
// second group when it has two or more.
void handKinds(const int (&counts)[5], int& top, int& second);

}  // namespace doc::game
