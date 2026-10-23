#include "app/catalog.hpp"
#include "fort_apocalypse_c64/entry.hpp"

namespace app {

std::vector<GameEntry> catalog() {
    // Planned games have no importer yet; they show what the collection is
    // heading for.
    return {
        fort_apocalypse::entry(),
        {.id = "bruce_lee_c64", .title = "Bruce Lee", .machine = Machine::c64,
         .media = "the tape or disk image"},
        {.id = "mindwalker_amiga", .title = "Mindwalker", .machine = Machine::amiga,
         .media = "the disk image (ADF)"},
        {.id = "marble_madness_amiga", .title = "Marble Madness", .machine = Machine::amiga,
         .media = "the disk image (ADF)"},
    };
}

const char* machine_name(Machine machine) {
    return machine == Machine::c64 ? "C64" : "Amiga";
}

} // namespace app
