#pragma once
// Bounds-checked readers for the first adventure's containers that the
// second one does not share. Layouts were derived from this ROM's files and
// checked against all of them:
//
//  * .pcm: an LPCK archive (named text files) stored as one LZ10 stream.
//    The second adventure's PCK2 (.plz) has the same member records but a
//    different header.
//  * data/sound/sound_data.sdat: the Nintendo SDK's sound archive (SDAT,
//    general documentation); the sequels replaced it with SMD/SWD/SED files.
//
// Shared with the second adventure, and read by the existing decoders:
// .arc/.arj (4-byte codec tag + stream; backgrounds and sprites), .bgx
// (a background stored uncompressed), GDS scripts, NFTR fonts, SAD streams
// and Mobiclip movies.
#include "import/rom_image.hpp"
#include <string>
#include <vector>

namespace picarat::import::adventure1 {

struct LpckMember {
    std::string name;
    std::span<const std::uint8_t> data; // inside the archive passed in
};

// An expanded .pcm: header size (16), total size, member count, "LPCK";
// then records of {header size, record size, 0, payload size,
// NUL-terminated name padded to the header size}, payload, padding.
std::vector<LpckMember> lpckMembers(std::span<const std::uint8_t> data);

struct SoundArchive {
    // Files in its FAT by kind: sequences (SSEQ), sequence archives (SSAR,
    // the sound effects), instrument banks (SBNK), wave archives (SWAR).
    std::size_t sequences = 0, sequenceArchives = 0, banks = 0, waveArchives = 0;
};

// "SDAT", byte-order mark, version 1.0, file size, header size, block
// count; then offset/size pairs of the SYMB, INFO, FAT and FILE blocks.
// Every FAT entry must lie inside FILE and start with a known tag.
SoundArchive parseSoundArchive(std::span<const std::uint8_t> data);

} // namespace picarat::import::adventure1
