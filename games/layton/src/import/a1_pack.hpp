#pragma once
// Extracts the first adventure's resources into a pack folder. The pack
// keeps every resource under its ROM path (without the leading "data/"),
// without the compression and archive wrappers; pictures and fonts are
// converted to the native files GraphicsAssets reads, the same as the second
// adventure's (asset_pack.hpp):
//
//  * backgrounds bg/<name>.arc and the stored bg/<name>.bgx become
//    bg/<name>.l2bg; sprites ani/<name>.arc / .arj become
//    ani/<name>.arc.l2sp / .arj.l2sp; fonts font/<name>.NFTR become
//    font/<name>.NFTR.l2ft;
//  * an LPCK archive (.pcm) becomes the folder <dir>/<name>/ holding its
//    members (the game's texts) under their stored names;
//  * scripts (.gds), fonts, the sound archive, streams (.SAD), movies
//    (.mods), the handwriting database and the debug files are copied;
//  * code/arm9.bin and code/overlay0.bin hold the expanded program, for the
//    constant tables a later engine will read from it;
//  * escapes.bin holds the text escapes (ARM9 table 020bbf10) for GameText;
//  * languages.txt lists the cartridge's text languages, one
//    "<index> <folder>" line each in the order of the game's folder table
//    (ARM9 020d2ad0), for the language setting (system/a1_language).
//
// Language-specific resources stay in their two-letter folders (ani/de/,
// qtext/fr/...), all five side by side; the language is chosen at run time,
// as the game does. Every picture, script, font, archive and the sound
// archive is parsed while it is packed, so a damaged ROM or an unknown
// variant fails the import instead of producing a pack with unreadable
// files.
#include "import/asset_pack.hpp"
#include <string>
#include <vector>

namespace picarat::import::adventure1 {

struct PackSummary {
    std::size_t backgrounds = 0, sprites = 0, scripts = 0, texts = 0, fonts = 0, streams = 0,
                movies = 0;
    std::size_t sequences = 0, soundEffectArchives = 0; // in the sound archive
    std::vector<std::string> languages;                 // folder codes, table order
};

PackResult buildPack(const NdsRom& rom, const std::filesystem::path& output, PackSummary& summary,
                     const PackProgress& progress = {});

} // namespace picarat::import::adventure1
