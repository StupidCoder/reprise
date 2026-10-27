#include "cannon_fodder_amiga/entry.hpp"
#include "cannon_fodder_amiga/data/preview.hpp"
#include "cannon_fodder_amiga/import/disk_import.hpp"

namespace fodder {

app::GameEntry entry() {
    return {
        .id = "cannon_fodder_amiga",
        .title = "Cannon Fodder",
        .machine = app::Machine::amiga,
        .media = "three disk images (ADF), one after another or together",
        .import = import::import_disk,
        .previews = [](const std::filesystem::path& content) { return data::previews(content); },
        .progress = "Import of all three disks and the decoded screens, sprite sheets, tile sets, colours and the 72 "
                    "mission maps. Nothing can be played yet.",
    };
}

} // namespace fodder
