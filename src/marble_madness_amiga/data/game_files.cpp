#include "marble_madness_amiga/data/game_files.hpp"
#include "amiga/byte_run1.hpp"
#include "platform/files.hpp"
#include <array>
#include <stdexcept>

namespace marble_madness::data {
namespace {

// The program's Track names ($1035E4), as installed.
constexpr std::array<const char*, course_count> track_files = {"PrcTrack", "BegTrack", "IntTrack",
                                                               "aertrack", "SilTrack", "ulttrack"};
// The sound files ($122612), as installed.
constexpr std::array<const char*, course_count> sound_files = {"PrcSnd", "BegSnd", "IntSnd",
                                                               "AerSnd", "SilSnd", "UltSnd"};
constexpr std::array<const char*, course_count> map_files = {"practy.mlb", "beginr.mlb", "interm.mlb",
                                                             "aerial.mlb", "silly.mlb",  "ultima.mlb"};

amiga::LoadFile load(const std::filesystem::path& file) {
    try {
        return amiga::read_load_file(platform::read_file(file));
    } catch (const std::exception& error) {
        throw std::runtime_error("The installed Marble Madness data is damaged (" + file.filename().string() +
                                 ": " + error.what() + "); please import the disk again.");
    }
}

} // namespace

GameFiles::GameFiles(const std::filesystem::path& content)
    : program_(load(content / "program.hunks"), program_base),
      marble_data_(load(content / "marbdat"), marble_data_base) {
    for (std::size_t c = 0; c < course_count; ++c) {
        tracks_[c] = amiga::LoadedImage(load(content / track_files[c]), track_base);
        sounds_[c] = amiga::LoadedImage(load(content / sound_files[c]), sound_base);
        const auto head = amiga::unpack_byte_run1(platform::read_file(content / map_files[c]), 0x20000);
        if (head.size() < 2) {
            throw std::runtime_error("The installed Marble Madness data is damaged; please import the disk again.");
        }
        map_rows_[c] = head[0] << 8 | head[1];
    }
}

} // namespace marble_madness::data
