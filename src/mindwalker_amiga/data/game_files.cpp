#include "mindwalker_amiga/data/game_files.hpp"
#include "platform/files.hpp"
#include <stdexcept>
#include <string>

namespace mindwalker::data {
namespace {

struct Layout {
    const char* file;
    std::uint32_t base;
};

// In Stage order.
constexpr std::array<Layout, stage_count> layouts = {{
    {"MindWlkS", 0x700000},
    {"MindWlkT", 0x600000},
    {"MindWlkA", 0x200000},
    {"MindWlkB", 0x300000},
    {"MindWlkC", 0x400000},
    {"MindWlkP", 0x500000},
    {"MindWlkE", 0x780000},
}};

amiga::LoadFile load(const std::filesystem::path& file) {
    try {
        return amiga::read_load_file(platform::read_file(file));
    } catch (const std::exception& error) {
        throw std::runtime_error("The installed Mind Walker data is damaged (" + file.filename().string() + ": " +
                                 error.what() + "); please import the disk again.");
    }
}

} // namespace

const char* program_file(Stage stage) {
    return layouts[std::size_t(stage)].file;
}

GameFiles::GameFiles(const std::filesystem::path& content) {
    for (std::size_t s = 0; s < stage_count; ++s) {
        programs_[s] = amiga::LoadedImage(load(content / layouts[s].file), layouts[s].base);
    }
}

} // namespace mindwalker::data
