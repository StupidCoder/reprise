#include "app/catalog.hpp"
#include "bruce_lee_c64/entry.hpp"
#include "fort_apocalypse_c64/entry.hpp"
#include "marble_madness_amiga/entry.hpp"
#include "wizball_c64/entry.hpp"

namespace app {

std::vector<GameEntry> catalog() {
    // Planned games have no importer yet; they show what the collection is
    // heading for.
    return {
        fort_apocalypse::entry(),
        bruce_lee::entry(),
        wizball::entry(),
        {.id = "mindwalker_amiga", .title = "Mindwalker", .machine = Machine::amiga,
         .media = "the disk image (ADF)"},
        marble_madness::entry(),
        {.id = "cannon_fodder_amiga", .title = "Cannon Fodder", .machine = Machine::amiga,
         .media = "the disk images (ADF)"},
        {.id = "rock_n_roll_amiga", .title = "Rock 'n Roll", .machine = Machine::amiga,
         .media = "the disk image (ADF)"},
        {.id = "wings_of_fury_amiga", .title = "Wings of Fury", .machine = Machine::amiga,
         .media = "the disk image (ADF)"},
    };
}

const char* machine_name(Machine machine) {
    return machine == Machine::c64 ? "C64" : "Amiga";
}

} // namespace app
