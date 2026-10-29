#pragma once
// Extracts the fourth adventure's resources into a pack folder. No engine
// plays this game yet, so the pack keeps every resource in its original
// layout, only without the archive and compression wrappers:
//
//  * each GFSA archive becomes a folder: lt4_main_ge.fa -> main/,
//    lt4_seq.fa -> seq/, lt4_sub.fa -> sub/ (the last two hold the same
//    program scripts except the bag's; the game plays sub/, seq/ is what
//    it mounts when the boot's integrity check fails, ov8 020ef440);
//  * an archive file holding a GFSP pack (.ca, .ci, .cse, .cb) becomes the
//    folder <dir>/<name>/ with its members under their stored names;
//    any other file is expanded and its 'c' extension prefix becomes 'l'
//    (.cq -> .lq, .cb -> .lb, .cf -> .lf, .cp -> .lp), the naming the
//    game's own packs use for unwrapped members;
//  * streams (lt4/str/*.SAD), wave banks (lt4/wav/*.SWD) and movies
//    (lt4/mov/*.mods) are copied to str/, wav/ and mov/;
//  * code/arm9.bin and code/overlay<N>.bin hold the expanded program, for
//    the constant tables a later engine will read from it.
//
// Every script, data list, picture, palette, font and sound file is parsed
// while it is packed, so a damaged ROM or an unknown variant fails the
// import instead of producing a pack with unreadable files.
#include "import/asset_pack.hpp"

namespace picarat::import::adventure4 {

struct PackSummary {
    std::size_t scripts = 0, dataLists = 0, pictures = 0, palettes = 0, fonts = 0, sounds = 0,
                streams = 0, movies = 0;
};

PackResult buildPack(const NdsRom& rom, const std::filesystem::path& output, PackSummary& summary,
                     const PackProgress& progress = {});

} // namespace picarat::import::adventure4
