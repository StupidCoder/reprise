#pragma once
// The puzzle collection's own progress for one game: which puzzles were
// solved when played from the collection. It is kept apart from the story's
// saves: solving a puzzle here never changes a story save, and the story
// never changes this. The game hub shows it and can reset it.
//
// The file is plain text: a header line, then one puzzle ID per line (the
// collection's row IDs, see PuzzleRow::id).
#include <cstddef>
#include <filesystem>
#include <set>

namespace picarat::shell {

class PuzzleModeProgress {
public:
    PuzzleModeProgress() = default;
    // Reads `file`; a missing or damaged file means nothing solved yet.
    explicit PuzzleModeProgress(std::filesystem::path file);

    const std::set<unsigned>& solved() const { return solved_; }

    // Records a solved puzzle and writes the file.
    void markSolved(unsigned id);
    // Forgets every solved puzzle and removes the file.
    void reset();

private:
    std::filesystem::path file_;
    std::set<unsigned> solved_;
};

} // namespace picarat::shell
