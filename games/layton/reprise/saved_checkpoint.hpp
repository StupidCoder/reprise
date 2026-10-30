#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace layton_reprise {

// The quick save of a game with checkpoints (all but Azran Legacy): the
// session's last automatic checkpoint - the game's own save record of the last room that was
// ready (Pandora's Box also of an event's or a puzzle's start) - not the exact moment. Loading
// it continues the story there through the game's own load path. Written with state::Writer.
struct SavedCheckpoint {
    std::string magic{"Reprise puzzle adventure checkpoint"};
    int version{1};
    std::string game, language; // the catalog id; the cartridge language played (Curious Village)
    std::vector<std::uint8_t> checkpoint;
    template <class A> void persist(A& a) { a(magic, version, game, language, checkpoint); }
};

} // namespace layton_reprise
