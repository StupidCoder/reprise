#pragma once
// The game library's data, per game: whether an imported game is present
// and usable, and the puzzle collection's rows (number, title, kind).
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace picarat::shell {

// The library folders of the imported packs. Changing a pack layout bumps
// its schema (import::kPackSchema, kAdventure1PackSchema ... kAdventure6PackSchema)
// and its folder name together.
inline constexpr const char* kPackFolder = "yltd-v11";
inline constexpr const char* kAdventure1PackFolder = "a5fp-v3";
inline constexpr const char* kAdventure3PackFolder = "c3jd-v2";
inline constexpr const char* kAdventure4PackFolder = "blfd-v1";
inline constexpr const char* kAdventure5PackFolder = "akkd-v1";
inline constexpr const char* kAdventure6PackFolder = "al6d-v1";

struct PuzzleRow {
    unsigned id = 0;    // internal puzzle ID (n<id>.dat; third adventure: n<id> scripts)
    unsigned order = 0; // sort key for the "No." column: the puzzle's number
    std::string number; // "001", "W01".."W33" for the weekly puzzles, "—" for unnumbered ones
    std::string title;  // UTF-8 (converted from the game's Windows-1252)
    std::string kind;   // the puzzle type in words
    bool supported = false;
};

// One of the languages a multi-language cartridge holds.
struct GameLanguage {
    int index = 0;    // in the game's folder table (adventure1::folderIndex)
    std::string code; // its folder, "de"
    std::string name; // in itself, "Deutsch"
};

struct Library {
    bool ready = false;       // a complete, verified import is present
    bool story = true;        // the story can be played
    std::size_t playable = 0; // puzzles the engines can play
    std::vector<PuzzleRow> puzzles;
    std::string problem; // why an existing import cannot be used
    // The cartridge's languages, by code (empty: one language only).
    std::vector<GameLanguage> languages;
};

// Reads the imported pack at `pack`; an empty Library when nothing usable is
// there.
Library loadLibrary(const std::filesystem::path& pack);

// The first adventure's pack: present and complete, with its languages
// (languages.txt). The story cannot be played yet.
Library loadAdventure1(const std::filesystem::path& pack);
// Fills the first adventure's puzzle rows from its catalogue
// (puzzles/a1_catalogue.hpp) in the language `code` ("de").
void loadAdventure1Puzzles(Library& library, const std::filesystem::path& pack,
                           const std::string& code);

// The fourth adventure's puzzle rows from its catalogue (res/@/lt4_def),
// by the number the game shows; the types the port plays are supported.
void loadAdventure4Puzzles(Library& library, const std::filesystem::path& pack);
// The 3DS adventures' catalogues (`sixth`: the sixth adventure's).
void loadAdventure5Puzzles(Library& library, const std::filesystem::path& pack, bool sixth = false);

// The language a game with `languages` uses: `chosen` (an index into them)
// if set, else the one the game itself picks for the host's language
// (`isoCode`, "de-DE"), as on the console (adventure1::folderIndex), else
// the first. Returns an index into `languages`.
int usedLanguage(const std::vector<GameLanguage>& languages, int chosen, std::string_view isoCode);

// The third adventure's pack: its puzzle list (rc/lt3_rc.cpck, lt3_nazo,
// opcode 3001 rows), with the puzzles its scene can play marked supported.
// Row IDs are the puzzles' numbers.
Library loadAdventure3(const std::filesystem::path& pack);

// The pack of the fourth to sixth adventures: present and complete. The
// fourth's story and puzzle list play; the fifth and sixth are import-only
// (no story, puzzle list or extras).
Library loadImportOnly(const std::filesystem::path& pack, int card);

// A story checkpoint as the game hub shows it.
struct StorySave {
    bool used = false;
    std::string title;  // "Checkpoint"
    std::string detail; // time played, place, puzzles solved (style::kIconPuzzle)
};

// The app's checkpoint of each game, as the hub's Continue shows it (detail
// only: time played, place, puzzles solved); none when there is no
// checkpoint the game can continue from.
std::optional<StorySave> adventure2Checkpoint(const std::filesystem::path& pack,
                                              const std::filesystem::path& checkpoint);
std::optional<StorySave> adventure3Checkpoint(const std::filesystem::path& saves);
std::optional<StorySave> adventure1Checkpoint(const std::filesystem::path& checkpoint);

// Converts Windows-1252 text (the game's character set) to UTF-8.
std::string utf8FromCp1252(std::string_view text);
// Converts UTF-8 to Windows-1252; characters it cannot hold are dropped.
std::string cp1252FromUtf8(std::string_view text);

} // namespace picarat::shell
