// Reading the imported game for the library screen. See library.hpp.
#include "shell/library.hpp"
#include "graphics/graphics.hpp"
#include "graphics/text.hpp"
#include "import/a3_formats.hpp"
#include "import/rom_import.hpp"
#include "puzzles/a1_catalogue.hpp"
#include "puzzles/a1_engines.hpp"
#include "puzzles/a3_puzzle_loop.hpp"
#include "puzzles/a4_puzzle.hpp"
#include "puzzles/a5_puzzle.hpp"
#include "shell/library_screen.hpp"
#include "shell/style.hpp"
#include "story/a1_state.hpp"
#include "story/a3_saves.hpp"
#include "system/a1_language.hpp"
#include "system/a2_checkpoint.hpp"
#include "system/a2_save_file.hpp"
#include "system/a5_game.hpp"
#include "system/resources.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>

namespace picarat::shell {

namespace {

// Windows-1252 code points 0x80..0x9f (0 where the code page has none).
constexpr std::array<char32_t, 32> kCp1252High{
    0x20ac, 0,      0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160,
    0x2039, 0x0152, 0,      0x017d, 0,      0,      0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
    0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0,      0x017e, 0x0178};

// The importer's import.json at `pack`, or an empty string.
std::string importManifest(const std::filesystem::path& pack) {
    std::ifstream marker(pack / "import.json");
    std::stringstream buffer;
    buffer << marker.rdbuf();
    return buffer.str();
}

// The value after "key": in the small import.json the importer writes.
std::string jsonValue(const std::string& json, const std::string& key) {
    const auto at = json.find("\"" + key + "\"");
    if (at == std::string::npos) {
        return {};
    }
    auto start = json.find(':', at);
    if (start == std::string::npos) {
        return {};
    }
    start = json.find_first_not_of(" \t\r\n\"", start + 1);
    const auto end = json.find_first_of(",\"\r\n}", start);
    return start == std::string::npos ? std::string{} : json.substr(start, end - start);
}

// Puzzle types (the record's type byte) in words, for the collection list.
const std::map<unsigned, const char*> kKinds{
    {2, "Multiple choice"}, {3, "Selection"},        {5, "Drawing"},         {6, "Tracing"},
    {9, "Paths"},           {10, "Cycling"},         {11, "Tile placement"}, {13, "Pancake towers"},
    {15, "Lines"},          {16, "Written answer"},  {17, "Knight tours"},   {18, "Rotating tiles"},
    {20, "Written answer"}, {21, "Written answer"},  {22, "Written answer"}, {23, "Toggles"},
    {24, "Roses"},          {25, "Sliding blocks"},  {26, "Tiles"},          {27, "Ice skating"},
    {28, "Written answer"}, {29, "Peg solitaire"},   {30, "Moving pairs"},   {31, "Street lamps"},
    {32, "Written answer"}, {33, "Stepping stones"}, {34, "Enclosing"},      {35, "Written answer"},
};

// The third adventure's puzzle types in words. The game's own (German)
// labels group them the same way: 5, 7, 18 and 22 are all "Anordnen", 9 and
// 10 "Linien", 4, 20 and 21 "Auswahl".
const std::map<unsigned, const char*> kAdventure3Kinds{
    {1, "Written answer"}, {2, "Circling"},        {3, "Multiple selection"}, {4, "Selection"},
    {5, "Arrangement"},    {7, "Arrangement"},     {8, "Matchsticks"},        {9, "Lines"},
    {10, "Lines"},         {11, "Sliding blocks"}, {12, "Tangram"},           {13, "Bread rolls"},
    {14, "Bananas"},       {15, "Frogs"},          {16, "Tumbling blocks"},   {17, "Bombs"},
    {18, "Arrangement"},   {19, "Weighing"},       {20, "Selection"},         {21, "Selection"},
    {22, "Arrangement"},
};

} // namespace

std::string utf8FromCp1252(std::string_view text) {
    std::string out;
    for (const char c : text) {
        const auto byte = std::uint8_t(c);
        const char32_t code = byte >= 0x80 && byte < 0xa0 ? kCp1252High[byte - 0x80] : byte;
        if (code) {
            style::appendUtf8(out, code);
        }
    }
    return out;
}

std::string cp1252FromUtf8(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        const char32_t c = style::nextCodePoint(text, i);
        if (c < 0x80 || (c >= 0xa0 && c <= 0xff)) {
            out += char(c);
            continue;
        }
        for (std::size_t k = 0; k < kCp1252High.size(); ++k) {
            if (kCp1252High[k] == c) {
                out += char(0x80 + k);
                break;
            }
        }
    }
    return out;
}

Library loadLibrary(const std::filesystem::path& pack) {
    Library library;
    const std::string meta = importManifest(pack);
    if (meta.empty()) {
        return library;
    }
    if (jsonValue(meta, "schema") != std::to_string(import::kPackSchema) ||
        jsonValue(meta, "rom_sha256") != import::knownRom(import::Game::Adventure2).sha256) {
        return library; // an older pack layout or another ROM: import again
    }
    try {
        // Opening the graphics and the text checks that the pack is complete.
        GraphicsAssets assets(pack);
        GameText text(pack);
        std::set<unsigned> supported;
        std::ifstream supportedFile(pack / "puzzle/library.txt");
        unsigned id = 0;
        while (supportedFile >> id) {
            supported.insert(id);
        }
        std::ifstream ids(pack / "puzzle/catalogue.txt");
        while (ids >> id) {
            const PuzzleData puzzle(readFile(pack / ("puzzle/n" + std::to_string(id) + ".dat")),
                                    text);
            PuzzleRow row;
            row.id = id;
            row.order = puzzle.number ? puzzle.number : 1000 + id;
            char number[8];
            std::snprintf(number, sizeof number, "%03u", puzzle.number);
            row.number = puzzle.number == 0 ? "—" : puzzle.number >= 154 ? "W" : number;
            row.title = utf8FromCp1252(puzzle.title);
            if (row.title.empty()) {
                row.title = "Untitled";
            }
            const auto kind = kKinds.find(puzzle.type);
            row.kind = kind != kKinds.end() ? kind->second : "Type " + std::to_string(puzzle.type);
            row.supported = supported.count(id) != 0;
            library.puzzles.push_back(std::move(row));
        }
        // The weekly (WiFi) puzzles carry numbers 154..205 with gaps; the 33
        // present in the ROM are numbered W01..W33 in that order.
        std::vector<PuzzleRow*> weekly;
        for (auto& row : library.puzzles) {
            if (row.number == "W") {
                weekly.push_back(&row);
            }
        }
        std::sort(weekly.begin(), weekly.end(),
                  [](const PuzzleRow* a, const PuzzleRow* b) { return a->order < b->order; });
        for (std::size_t i = 0; i < weekly.size(); ++i) {
            char label[8];
            std::snprintf(label, sizeof label, "W%02zu", i + 1);
            weekly[i]->number = label;
        }
        if (std::to_string(supported.size()) != jsonValue(meta, "puzzles") ||
            library.puzzles.size() != 187) {
            throw std::runtime_error("Incomplete puzzle catalogue");
        }
        library.playable = supported.size();
        library.ready = true;
    } catch (const std::exception& error) {
        library = {};
        library.problem = std::string("The imported game needs attention: ") + error.what();
    }
    return library;
}

Library loadAdventure1(const std::filesystem::path& pack) {
    Library library;
    const std::string meta = importManifest(pack);
    if (meta.empty() ||
        jsonValue(meta, "schema") != std::to_string(import::kAdventure1PackSchema) ||
        jsonValue(meta, "rom_sha256") != import::knownRom(import::Game::Adventure1).sha256) {
        return library; // nothing imported, an older pack layout or another ROM
    }
    std::ifstream list(pack / "languages.txt");
    int index = 0;
    std::string code;
    while (list >> index >> code) {
        library.languages.push_back({index, code, adventure1::nativeName(code)});
    }
    std::sort(library.languages.begin(), library.languages.end(),
              [](const GameLanguage& a, const GameLanguage& b) { return a.code < b.code; });
    if (library.languages.empty() || !fileExists(pack / "code/arm9.bin")) {
        library.problem = "The imported Curious Village needs attention: incomplete pack";
        library.languages.clear();
        return library;
    }
    library.ready = true;
    return library;
}

Library loadImportOnly(const std::filesystem::path& pack, int card) {
    // Per game: its ROM, pack schema and a file every complete pack holds.
    const auto [game, schema, required] =
        card == LibraryScreen::kAdventure4Card
            ? std::tuple{import::Game::Adventure4, import::kAdventure4PackSchema,
                         "main/fnt/main.lf"}
        : card == LibraryScreen::kAdventure5Card
            ? std::tuple{import::Game::Adventure5, import::kAdventure5PackSchema, "code/code.bin"}
            : std::tuple{import::Game::Adventure6, import::kAdventure6PackSchema, "code/code.bin"};
    Library library;
    library.story = false;
    const std::string meta = importManifest(pack);
    if (meta.empty() || jsonValue(meta, "schema") != std::to_string(schema) ||
        jsonValue(meta, "rom_sha256") != import::knownRom(game).sha256) {
        return library; // nothing imported, an older pack layout or another ROM
    }
    if (!fileExists(pack / required) || !fileExists(pack / "provenance.json")) {
        library.problem = std::string("The imported ") + import::knownRom(game).title +
                          " needs attention: incomplete pack";
        return library;
    }
    library.ready = true;
    if (card == LibraryScreen::kAdventure4Card) {
        library.story = true; // the story from its title, with its own save slots
        loadAdventure4Puzzles(library, pack);
    }
    if (card == LibraryScreen::kAdventure5Card) {
        library.story = true; // the story from the boot (reconstructed into chapter 6)
        loadAdventure5Puzzles(library, pack);
    }
    if (card == LibraryScreen::kAdventure6Card) {
        loadAdventure5Puzzles(library, pack, true);
    }
    return library;
}

void loadAdventure5Puzzles(Library& library, const std::filesystem::path& pack, bool sixth) {
    const auto edition = sixth ? adventure5::Edition::Sixth : adventure5::Edition::Fifth;
    // The catalogue (res/@/lt5_def 2301; the sixth adventure's lt6_def) in
    // the game's order; the puzzles on the shared play loop (3100) and the
    // special loops reconstructed so far are playable.
    library.puzzles.clear();
    library.playable = 0;
    try {
        const adventure5::Definitions defs =
            adventure5::loadDefinitions(adventure56::Pack(pack), edition);
        for (const auto& puzzle : defs.puzzles) {
            PuzzleRow row;
            row.id = unsigned(puzzle.id);
            row.order = unsigned(puzzle.number);
            char number[8];
            std::snprintf(number, sizeof number, "%03d", puzzle.number);
            row.number = number;
            row.title = adventure5::displayText(puzzle.title);
            row.kind = adventure5::displayText(puzzle.typeName);
            row.supported = adventure5::PuzzleSession::playable(puzzle.type, edition);
            library.playable += row.supported ? 1 : 0;
            library.puzzles.push_back(std::move(row));
        }
    } catch (const std::exception& error) {
        library.puzzles.clear();
        library.problem =
            std::string("The imported ") +
            (edition == adventure5::Edition::Sixth ? "Azran Legacy" : "Miracle Mask") +
            " needs attention: " + error.what();
    }
}

void loadAdventure4Puzzles(Library& library, const std::filesystem::path& pack) {
    // The catalogue (res/@/lt4_def 2301) in the order the game numbers its
    // puzzles (index + 1); the types the port plays are marked supported.
    library.puzzles.clear();
    library.playable = 0;
    try {
        adventure4::Assets assets(pack);
        for (const auto& entry : adventure4::puzzleCatalogue(assets)) {
            const auto& puzzle = entry.puzzle;
            PuzzleRow row;
            row.id = unsigned(puzzle.number);
            row.order = unsigned(puzzle.index + 1);
            char number[8];
            std::snprintf(number, sizeof number, "%03u", row.order);
            row.number = number;
            row.title = utf8FromCp1252(adventure4::plainText(assets, puzzle.title));
            row.kind = utf8FromCp1252(adventure4::plainText(assets, entry.typeName));
            row.supported = adventure4::PuzzleSession::playable(puzzle.type);
            library.playable += row.supported ? 1 : 0;
            library.puzzles.push_back(std::move(row));
        }
        std::sort(library.puzzles.begin(), library.puzzles.end(),
                  [](const PuzzleRow& a, const PuzzleRow& b) { return a.order < b.order; });
    } catch (const std::exception& error) {
        library.puzzles.clear();
        library.problem =
            std::string("The imported Spectre's Call needs attention: ") + error.what();
    }
}

void loadAdventure1Puzzles(Library& library, const std::filesystem::path& pack,
                           const std::string& code) {
    library.puzzles.clear();
    library.playable = 0;
    if (!library.ready) {
        return;
    }
    try {
        const adventure1::Catalogue catalogue(pack, code);
        for (const auto& [id, info] : catalogue.puzzles()) {
            PuzzleRow row;
            row.id = id;
            // Numbered puzzles first, in their order; the special puzzles
            // (no number of their own) after them.
            row.order = info.number ? info.number : 1000 + id;
            char number[8];
            std::snprintf(number, sizeof number, "%03u", info.number);
            row.number = info.number ? number : "—";
            row.title = info.title.empty() ? "Untitled" : utf8FromCp1252(info.title);
            row.kind = utf8FromCp1252(info.kind.empty() ? info.engine : info.kind);
            row.supported = adventure1::engineReconstructed(info.engine);
            library.playable += row.supported ? 1 : 0;
            library.puzzles.push_back(std::move(row));
        }
    } catch (const std::exception& error) {
        library.puzzles.clear();
        library.problem =
            std::string("The imported Curious Village needs attention: ") + error.what();
    }
}

int usedLanguage(const std::vector<GameLanguage>& languages, int chosen, std::string_view isoCode) {
    if (chosen >= 0 && chosen < int(languages.size())) {
        return chosen;
    }
    const int index = adventure1::folderIndex(adventure1::firmwareLanguage(isoCode));
    for (std::size_t i = 0; i < languages.size(); ++i) {
        if (languages[i].index == index) {
            return int(i);
        }
    }
    return 0;
}

Library loadAdventure3(const std::filesystem::path& pack) {
    Library library;
    const std::string meta = importManifest(pack);
    if (meta.empty() ||
        jsonValue(meta, "schema") != std::to_string(import::kAdventure3PackSchema) ||
        jsonValue(meta, "rom_sha256") != import::knownRom(import::Game::Adventure3).sha256) {
        return library; // nothing imported, an older pack layout or another ROM
    }
    try {
        using import::adventure3::LscrOperand;
        const GameText text(pack);
        const auto list =
            import::adventure3::parseLscr(readFile(pack / "rc/ge/lt3_rc/lt3_nazo.lbin"));
        // Opcode 3000 gives the count; each 3001 row is {number, developer
        // label, title, record name "n%03u", type, type label, picarats for
        // 0/1/2+ wrong answers, title picture}.
        std::size_t expected = 0;
        for (const auto& command : list.commands) {
            const auto& o = command.operands;
            if (command.opcode == 3000 && o.size() == 1) {
                expected = o[0].value;
            }
            if (command.opcode != 3001) {
                continue;
            }
            if (o.size() != 10 || o[0].type != 0 || o[2].type != 2 || o[3].type != 2 ||
                o[4].type != 0) {
                throw std::runtime_error("unexpected puzzle list row");
            }
            const std::string record = list.text(o[3]);
            PuzzleRow row;
            row.order = o[0].value;
            row.id = unsigned(std::stoul(record.substr(1)));
            char number[8];
            std::snprintf(number, sizeof number, "%03u", row.order);
            row.number = number;
            row.title = utf8FromCp1252(text.expand(list.text(o[2])));
            if (row.title.empty()) {
                row.title = "Untitled";
            }
            const auto kind = kAdventure3Kinds.find(o[4].value);
            row.kind = kind != kAdventure3Kinds.end() ? kind->second
                                                      : "Type " + std::to_string(o[4].value);
            library.puzzles.push_back(std::move(row));
        }
        if (library.puzzles.empty() || library.puzzles.size() != expected) {
            throw std::runtime_error("incomplete puzzle list");
        }
        // Which puzzles the scene can play; rows start by the puzzle's
        // number (the scene reads its record through the catalogue).
        adventure3::Assets assets(pack);
        adventure3::PuzzleSupport support(assets, text);
        for (auto& row : library.puzzles) {
            row.id = row.order;
            try {
                row.supported = support.check(row.order).empty();
            } catch (const std::exception&) {
                row.supported = false;
            }
            library.playable += row.supported ? 1 : 0;
        }
        library.ready = true;
    } catch (const std::exception& error) {
        library = {};
        library.problem = std::string("The imported Lost Future needs attention: ") + error.what();
    }
    return library;
}

// --- Checkpoints ---------------------------------------------------------------

namespace {
std::string hoursAndMinutes(int hours, int minutes) {
    const std::string m = std::to_string(minutes);
    return std::to_string(hours) + ":" + (m.size() < 2 ? "0" + m : m);
}
} // namespace

namespace {
// The file dialog's place label (02085fbc): jiten p_<place>, place 92
// shown as place 38; " · <label>", or nothing.
std::string adventure2PlaceLabel(const std::filesystem::path& pack, const GameText& text,
                                 int place) {
    place = place == 92 ? 38 : place;
    const auto label = pack / "text" / "jiten" / ("p_" + std::to_string(place) + ".txt");
    if (!fileExists(label)) {
        return {};
    }
    const auto raw = readFile(label);
    std::string name(raw.begin(), raw.end());
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) {
        name.pop_back();
    }
    return " · " + utf8FromCp1252(text.expand(name));
}
} // namespace

std::optional<StorySave> adventure2Checkpoint(const std::filesystem::path& pack,
                                              const std::filesystem::path& checkpoint) {
    try {
        const auto saved = CheckpointStore(checkpoint).load();
        if (!saved) {
            return std::nullopt;
        }
        const SaveProgress& p = saved->progress;
        const std::uint32_t seconds = std::uint32_t(p.s32(0x244)); // StoryGame::foldPlayTime
        StorySave save{true, "Checkpoint",
                       hoursAndMinutes(int(seconds / 3600), int(seconds / 60 % 60))};
        if (fileExists(pack)) {
            save.detail += adventure2PlaceLabel(pack, GameText(pack), p.place());
        }
        unsigned solved = 0;
        for (std::uint32_t i = 0; i < 138; ++i) { // GameState::solvedCount (02083e7c)
            solved += (p.puzzleByte(i) & 3) == 2;
        }
        save.detail += " · " + std::to_string(solved) + " " + std::string(style::kIconPuzzle);
        return save;
    } catch (const std::exception&) {
        return std::nullopt; // an older checkpoint the game cannot continue from
    }
}

std::optional<StorySave> adventure3Checkpoint(const std::filesystem::path& saves) {
    const adventure3::SaveSlots slots(saves);
    const auto* record = slots.slot(adventure3::SaveSlots::kCheckpoint);
    if (!record || record->header.name.empty()) {
        return std::nullopt;
    }
    const auto& h = record->header;
    StorySave save{true, "Checkpoint", hoursAndMinutes(h.hours, h.minutes)};
    if (!h.place.empty()) {
        save.detail += " · " + h.place;
    }
    save.detail += " · " + std::to_string(h.solved) + " " + std::string(style::kIconPuzzle);
    return save;
}

std::optional<StorySave> adventure1Checkpoint(const std::filesystem::path& checkpoint) {
    try {
        if (!fileExists(checkpoint)) {
            return std::nullopt;
        }
        const adventure1::SaveImage image(std::string(), readFile(checkpoint));
        const auto info = image.slot(0);
        if (!info.used) {
            return std::nullopt;
        }
        StorySave save{true, "Checkpoint", hoursAndMinutes(int(info.hours), int(info.minutes))};
        if (!info.place.empty()) {
            save.detail += " · " + utf8FromCp1252(info.place);
        }
        save.detail += " · " + std::to_string(info.solved) + " " + std::string(style::kIconPuzzle);
        return save;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace picarat::shell
