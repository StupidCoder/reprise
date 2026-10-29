// The puzzle collection's own progress. See puzzle_mode.hpp.
#include "shell/puzzle_mode.hpp"
#include <fstream>
#include <string>

namespace picarat::shell {

namespace {
constexpr const char* kHeader = "picarat puzzle mode 1";
} // namespace

PuzzleModeProgress::PuzzleModeProgress(std::filesystem::path file) : file_(std::move(file)) {
    std::ifstream in(file_);
    std::string header;
    if (!in || !std::getline(in, header) || header != kHeader) {
        return;
    }
    unsigned id;
    while (in >> id) {
        solved_.insert(id);
    }
}

void PuzzleModeProgress::markSolved(unsigned id) {
    if (!solved_.insert(id).second || file_.empty()) {
        return;
    }
    // Written to a new file first, so a crash cannot leave half a file.
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    const auto temporary = file_.string() + ".new";
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << kHeader << '\n';
        for (unsigned solved : solved_) {
            out << solved << '\n';
        }
        if (!out) {
            return;
        }
    }
    std::filesystem::rename(temporary, file_, ec);
}

void PuzzleModeProgress::reset() {
    solved_.clear();
    if (!file_.empty()) {
        std::error_code ec;
        std::filesystem::remove(file_, ec);
    }
}

} // namespace picarat::shell
