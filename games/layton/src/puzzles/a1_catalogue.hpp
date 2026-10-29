#pragma once
// The first adventure's puzzle catalogue: for every puzzle its number as the
// player sees it, its picarat values, title, kind and place, and its texts.
// Everything comes from the game's own data, run through its script
// interpreter (system/a1_script.hpp):
//
//  * the number shown for internal puzzle N is entry N of the int table at
//    ARM9 020daeb0 (read from the pack's code/arm9.bin); 0 means the puzzle
//    has no number (the special puzzles, which take theirs from the save);
//  * script/pcarot/pscript.gds, command 0xc3: number, picarats for 0, 1 and
//    2 or more wrong answers (run by the title card, 0203c85c);
//  * script/puzzletitle/<lang>/qtitle.gds, command 0xba: number, title;
//  * script/qinfo/<lang>/qscript.gds, command 0xdc: number, kind, place;
//  * script/qscript/q<N>_param.gds: its engine command (0x1b) names the
//    engine that plays the puzzle;
//  * qtext/<lang>/q000, q050, q100 (puzzles below 50, below 100, the rest):
//    q_N question, c_N correct, f_N wrong, h_N_1..3 hints, t_N title
//    (02041ac4, 0203c85c: the folder is chosen by the internal number).
#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace picarat::adventure1 {

struct PuzzleInfo {
    unsigned id = 0;     // internal number (q<id>_param.gds)
    unsigned number = 0; // as shown ("009"); 0 = none
    std::array<unsigned, 3> picarats{};
    std::string title, kind, place; // the game's text (Windows-1252, escapes expanded)
    std::string engine;             // the engine command's name ("Free Button")
};

class Catalogue {
public:
    // `language` is the pack's folder code ("de").
    Catalogue(const std::filesystem::path& pack, const std::string& language);

    // Every puzzle with a parameter script, by internal number.
    const std::map<unsigned, PuzzleInfo>& puzzles() const { return puzzles_; }

    const PuzzleInfo* find(unsigned id) const;

    // A text of puzzle `id`: "q", "c", "f", "t" or "h_1".."h_3"; empty when
    // the file does not exist. Escapes are expanded.
    std::string text(unsigned id, const std::string& kind) const;

private:
    std::filesystem::path pack_;
    std::string language_;
    std::map<unsigned, PuzzleInfo> puzzles_;
};

} // namespace picarat::adventure1
