#pragma once
#include "amiga/hunk.hpp"
#include <array>
#include <filesystem>

// The stage programs, read from the installed content folder.
//
// Each program is laid out as in the research notes: hunks from its own base,
// 8 bytes apart, relocated there (.analysis/mindwalker_amiga/work/bases.txt).
// The decoders read the programs' constant data (images, palettes, texts,
// tables, samples) by those addresses; each address is named where it is used.
namespace mindwalker::data {

enum class Stage { setup, title, landscape, tunnel, arena, psyche, error };
constexpr int stage_count = 7;

class GameFiles {
public:
    explicit GameFiles(const std::filesystem::path& content);

    // S (setup) $700000, T (title) $600000, A (landscape) $200000, B (tunnel)
    // $300000, C (arena) $400000, P (psyche) $500000, E (error) $780000.
    const amiga::LoadedImage& program(Stage stage) const { return programs_[std::size_t(stage)]; }

private:
    std::array<amiga::LoadedImage, stage_count> programs_;
};

// The installed name of a stage's program ("MindWlkA", ...).
const char* program_file(Stage);

} // namespace mindwalker::data
