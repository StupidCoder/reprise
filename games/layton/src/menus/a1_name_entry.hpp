#pragma once
// The first adventure's name entry (ARM9 0203a994 / 0203b6cc / 0203ae68).
//
// How it plays
// ------------
// A keyboard on the lower screen: four key sets (small letters, capitals,
// symbols and accented letters; the first three share one layout of 45
// keys, the accented set has 52), switched by the buttons beside and below
// the keys, plus "erase" and "space". The name (up to ten characters) shows
// above the keys with a cursor. "OK" accepts a name that is not empty;
// the names of the two heroes are refused with a message until the stylus
// is lifted, and the name is cleared.
#include <string>

namespace picarat::adventure1 {
class Story;

// Asks for a name; the scene number tells which name it is (scene 0: the
// player's, kept in the state; another (0x28) for a second purpose, shown
// with its own upper picture). Ends faded to black.
std::string nameEntry(Story& story);

} // namespace picarat::adventure1
