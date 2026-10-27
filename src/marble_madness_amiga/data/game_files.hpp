#pragma once
#include "amiga/hunk.hpp"
#include "marble_madness_amiga/data/assets.hpp"
#include <filesystem>
#include <memory>

// The game program's data and the courses' Track files, read from the
// installed content folder.
//
// The program (program.hunks) is laid out as in the research notes: hunks
// from $100000, 8 bytes apart (.analysis/marble_madness_amiga/work/
// dat.hunkmap.txt). The game code reads constant tables from it by those
// addresses; they are named where they are used. Track files are laid out
// from $200000, sound files from $400000 (only their own pointers matter).
namespace marble_madness::data {

class GameFiles {
public:
    static constexpr std::uint32_t program_base = 0x100000;
    static constexpr std::uint32_t track_base = 0x200000;
    static constexpr std::uint32_t marble_data_base = 0x300000;
    static constexpr std::uint32_t sound_base = 0x400000;

    explicit GameFiles(const std::filesystem::path& content);

    const amiga::LoadedImage& program() const { return program_; }
    // A course's Track file, and the address of its header (ten pointers,
    // read by the course start at $103216).
    const amiga::LoadedImage& track(Course course) const { return tracks_[std::size_t(course)]; }
    std::uint32_t track_header(Course course) const { return tracks_[std::size_t(course)].first_filled_hunk(); }
    // marbdat: the marble's animation lists, bonus points and pictures
    // (the table the program keeps at $115448).
    const amiga::LoadedImage& marble_data() const { return marble_data_; }
    // A course's sound file (its table, sound records, envelopes, music
    // and samples; loaded by $122218).
    const amiga::LoadedImage& sounds(Course course) const { return sounds_[std::size_t(course)]; }
    // Rows of a course's tile map the scroll can reach (.mlb header word).
    int map_rows(Course course) const { return map_rows_[std::size_t(course)]; }

private:
    amiga::LoadedImage program_;
    amiga::LoadedImage marble_data_;
    std::array<amiga::LoadedImage, course_count> tracks_;
    std::array<amiga::LoadedImage, course_count> sounds_;
    std::array<int, course_count> map_rows_{};
};

} // namespace marble_madness::data
