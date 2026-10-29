// Command-line front end for the native importer (src/import/).
//   picarat_import ROM DESTINATION          transactional library import
//   picarat_import --pack ROM OUTPUT [IDS]   (re)write a development pack in place;
//                                            IDS is an optional puzzle-ID list file
//                                            (second adventure only)
#include "import/a1_pack.hpp"
#include "import/a3_pack.hpp"
#include "import/a4_pack.hpp"
#include "import/a56_pack.hpp"
#include "import/asset_pack.hpp"
#include "import/ctr_image.hpp"
#include "import/a2_puzzle_library.hpp"
#include "import/rom_import.hpp"
#include <fstream>
#include <iostream>

using namespace picarat;

int main(int argc, char** argv) try {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "--pack" && (argc == 4 || argc == 5)) {
        import::FileRomSource source(argv[2]);
        if (source.size() > import::kMaximumRomSize) {
            // A 3DS cartridge image: streamed, never read whole.
            import::CtrRom rom(source);
            import::adventure56::PackSummary summary;
            const auto result = import::adventure56::buildPack(rom, argv[3], summary);
            std::cout << "Packed " << result.outputs.size() << " files into " << argv[3] << ": "
                      << summary.archives << " archives, " << summary.packs << " packs, "
                      << summary.pictures << " pictures, " << summary.scripts << " scripts, "
                      << summary.dataLists << " data lists, " << summary.others
                      << " other resources, " << summary.streams << " sound files, "
                      << summary.movies << " movies\n";
            return 0;
        }
        const import::NdsRom rom(readFile(argv[2]));
        if (rom.identity().game == import::Game::Adventure1) {
            import::adventure1::PackSummary summary;
            const auto result = import::adventure1::buildPack(rom, argv[3], summary);
            std::cout << "Packed " << result.outputs.size() << " files into " << argv[3] << ": "
                      << summary.backgrounds << " backgrounds, " << summary.sprites << " sprites, "
                      << summary.scripts << " scripts, " << summary.texts << " texts, "
                      << summary.fonts << " fonts, " << summary.sequences << " sequences, "
                      << summary.soundEffectArchives << " sound effect archives, "
                      << summary.streams << " streams, " << summary.movies << " movies; languages";
            for (const std::string& code : summary.languages) {
                std::cout << " " << code;
            }
            std::cout << "\n";
            return 0;
        }
        if (rom.identity().game == import::Game::Adventure3) {
            import::adventure3::PackSummary summary;
            const auto result = import::adventure3::buildPack(rom, argv[3], summary);
            std::cout << "Packed " << result.outputs.size() << " files into " << argv[3] << ": "
                      << summary.pictures << " pictures, " << summary.scripts << " scripts, "
                      << summary.fonts << " fonts, " << summary.sounds << " sound files, "
                      << summary.streams << " streams, " << summary.movies << " movies\n";
            return 0;
        }
        if (rom.identity().game == import::Game::Adventure4) {
            import::adventure4::PackSummary summary;
            const auto result = import::adventure4::buildPack(rom, argv[3], summary);
            std::cout << "Packed " << result.outputs.size() << " files into " << argv[3] << ": "
                      << summary.scripts << " scripts, " << summary.dataLists << " data lists, "
                      << summary.pictures << " pictures, " << summary.palettes << " palettes, "
                      << summary.fonts << " fonts, " << summary.sounds << " sound files, "
                      << summary.streams << " streams, " << summary.movies << " movies\n";
            return 0;
        }
        std::vector<unsigned> library(import::kPuzzleLibrary.begin(), import::kPuzzleLibrary.end());
        if (argc == 5) {
            library.clear();
            std::ifstream ids(argv[4]);
            for (unsigned id; ids >> id;) {
                library.push_back(id);
            }
        }
        const auto result = import::buildNativePack(rom, argv[3], library);
        std::cout << "Packed " << result.outputs.size() << " assets into " << argv[3] << "\n";
        return 0;
    }
    if (argc != 3) {
        std::cerr << "usage: picarat_import ROM DESTINATION\n       picarat_import --pack ROM "
                     "OUTPUT [IDS]\n";
        return 2;
    }
    const std::filesystem::path destination = argv[2];
    const auto result = import::importRom(
        argv[1], [&](import::Game) { return destination; },
        [](double fraction, const std::string& message) {
            std::cout << int(fraction * 100) << "% " << message << "\n";
        });
    std::cout << "Imported " << import::knownRom(result.game).title << ": " << result.assetCount
              << " files, " << result.puzzles << " playable puzzles\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "picarat_import: " << error.what() << "\n";
    return 1;
}
