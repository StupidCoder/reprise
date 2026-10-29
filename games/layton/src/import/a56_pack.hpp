#pragma once
// Extracts the fifth or sixth adventure's resources into a pack folder.
// No engine plays these games yet, so the pack keeps every resource in its
// original layout, only without the archive and pack wrappers:
//
//  * each archive lt<N>/arc/lt<N>_<x>.fa becomes the folder arc/<x>/
//    (a: program data, b: shared art and models, ge: German resources);
//  * a file or member holding a pack (XPCK, XFSP) becomes a folder of the
//    same name holding its members, recursively; everything else keeps its
//    stored bytes (members the game expands itself, such as RES.bin, stay
//    compressed);
//  * the sound archive and its streams (lt<N>/snd/...) go to snd/, movies
//    (lt<N>/mov/*.moflex) to mov/;
//  * code/code.bin holds the expanded code segment (loaded at 0x00100000)
//    and code/segments.txt its text, read-only and data segments.
//
// Every picture, script, data list and pack is parsed while it is packed;
// the 3D model, animation and effect members are kept as they are (their
// formats are not read yet). The system files (banner, icons, manual) are
// not packed.
#include "import/asset_pack.hpp"
#include "import/ctr_image.hpp"

namespace picarat::import::adventure56 {

struct PackSummary {
    std::size_t archives = 0, packs = 0, pictures = 0, scripts = 0, dataLists = 0, others = 0,
                streams = 0, movies = 0;
};

PackResult buildPack(CtrRom& rom, const std::filesystem::path& output, PackSummary& summary,
                     const PackProgress& progress = {});

} // namespace picarat::import::adventure56
