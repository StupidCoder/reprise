// Slot saves and loads for the game state. See a2_save_slots.hpp.
#include "story/a2_save_slots.hpp"
#include <algorithm>

namespace picarat {
void saveToSlot(SaveFile& file, GameState& state, int slot, bool update, const std::string& name,
                std::uint32_t seconds) {
    file.invalidateHeader(); // 020821a8
    file.writeSlot(slot, Bytes(state.progress.bytes.begin(), state.progress.bytes.end()));
    if (update) {
        file.setUsed(slot, true);
        file.setName(slot, name);
        // The summary's place: place 17 shows as place 100 once event 11220
        // (0x2bd4) was seen (020836cc..020836f0).
        int place = state.progress.place() & 0xff;
        if (place == 17 && state.eventViewed(0x2bd4)) {
            place = 100;
        }
        file.setPlace(slot, place);
        file.setSeconds(slot, seconds);            // 02083f80
        file.setFound(slot, state.foundCount());   // 02083e40
        file.setSolved(slot, state.solvedCount()); // 02083e7c
    }
    // 02083750: 0 unfinished; finished: 3 with all 153 puzzles, 2 with the
    // 138 story puzzles, otherwise 1.
    unsigned level = 0;
    if (state.cleared) {
        level = state.allPuzzlesSolved(153) ? 3 : state.allPuzzlesSolved(138) ? 2 : 1;
    }
    file.setClearLevel(slot, level);
    file.commitHeader(); // 020821d0
}

void loadFromSlot(const SaveFile& file, GameState& state, int slot, std::string& name) {
    const auto block = file.readSlot(slot);
    std::copy(block.begin(), block.end(), state.progress.bytes.begin());
    name = file.name(slot);                     // 020837cc: state +0x81d4
    state.cleared = file.clearLevel(slot) != 0; // +0x821e
}
} // namespace picarat
