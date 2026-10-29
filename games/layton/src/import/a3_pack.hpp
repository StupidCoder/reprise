#pragma once
// Extracts the third adventure's resources into a pack folder. No engine
// plays this game yet, so the pack keeps every resource in its original
// layout, only without the compression and archive wrappers:
//
//  * lt3/<dir>/<name>.c<ext> becomes <dir>/<name>.l<ext> (.cimg -> .limg,
//    .cbin -> .lbin, .cfnt -> .lfnt, .cdcm -> .ldcm), the name the game's
//    own archives use for the unwrapped forms;
//  * an LPC2 archive (.cani, .cpck, .cdat, .csnd) becomes the folder
//    <dir>/<name>/ holding its members under their stored names;
//  * streams (.SAD), wave banks (.SWD) and movies (.mods) are copied;
//  * code/arm9.bin and code/overlay<N>.bin hold the expanded program, for
//    the constant tables a later engine will read from it;
//  * escapes.bin holds the text escape table (ov0 0206daa8) for GameText.
//
// Every picture, script, font and sound file is parsed while it is packed,
// so a damaged ROM or an unknown variant fails the import instead of
// producing a pack with unreadable files.
#include "import/asset_pack.hpp"

namespace picarat::import::adventure3 {

struct PackSummary {
    std::size_t pictures = 0, scripts = 0, fonts = 0, sounds = 0, streams = 0, movies = 0;
};

PackResult buildPack(const NdsRom& rom, const std::filesystem::path& output, PackSummary& summary,
                     const PackProgress& progress = {});

} // namespace picarat::import::adventure3
