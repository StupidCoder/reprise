#pragma once
// The 3DS adventures' sound archive (NintendoWare BCSAR, snd/product.bcsar):
// the table of every sound with its name, kind and volume, and the effects
// (sequences with their banks and wave archives).
//
// BCSAR: "CSAR", blocks STRG (names: a table of {offset, size} after a
// 16-byte header, then the strings), INFO (references to the tables of
// sounds, sound groups, banks, wave archives, groups, players, files) and
// FILE. A sound entry: file id, player id, volume (0..127), the kind's
// details (0x2201 stream, 0x2202 wave sound, 0x2203 sequence), option flags
// (bit 0: a string id, the name). The fifth adventure: 632 streams
// (BGM_, EVS_, EVT_, JNG_, MOV_, SUR_) and 469 effect sequences named
// <set>_<label> ("SI_291_577", "SY_000_101").
//
// A sequence sound's details: a reference to its bank list (item ids
// 0x03nnnnnn, bank n), the track mask, the entry offset into the
// sequence's DATA, the priority. A bank (BCBNK, INFO block 0x5800): its
// wave list (wave archive item 0x05nnnnnn, wave index) and per program an
// instrument (0x5900; 0x5903 empty) whose region tree (0x6000 direct,
// 0x6001 key ranges, 0x6002 index table, 0x5901 key region, 0x5902
// velocity region) ends in {wave list index, parameter flags, then one
// word per flag: bit 0 original key, 1 volume, 2 pan, 3 pitch (float),
// 4 note-off and key group options, 9 the envelope (attack, decay,
// sustain, hold, release)}. A wave archive (BCWAR, INFO 0x6800 and FILE
// 0x6801): the waves' {offset, size} in FILE. Every effect bank of the
// fifth adventure uses one direct region per instrument (cbnk.py).
#include "sound/a56_sequence.hpp"
#include "sound/a56_stream.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace picarat::adventure56 {

class SoundArchive {
public:
    explicit SoundArchive(std::vector<std::uint8_t> bytes);
    ~SoundArchive();

    bool has(const std::string& name) const { return sounds_.contains(name); }

    int volume(const std::string& name) const; // -1: unknown
    // A stream's external file relative to the archive's folder, from the
    // file table (0x220d entries: the path after the 12-byte header;
    // fifth adventure "stream/BGM_004.bcstm", sixth
    // "stream/JNG_415_Failure.dspadpcm.bcstm"); empty when unknown.
    std::string externalFile(const std::string& name) const;
    // A player for an effect sequence (null when it cannot be played).
    std::unique_ptr<SequencePlayer> openSequence(const std::string& name);

    struct Sound {
        int index = 0, kind = 0, file = 0, volume = 127;
        std::size_t info = 0; // the entry's offset
    };

    const std::map<std::string, Sound>& sounds() const { return sounds_; }

private:
    std::span<const std::uint8_t> file(std::uint32_t id) const;
    std::shared_ptr<const std::vector<std::uint8_t>> sequenceData(std::uint32_t file);
    const std::vector<Instrument>* bank(std::uint32_t index);
    std::shared_ptr<const Pcm> wave(std::uint32_t archive, std::uint32_t index);

    std::vector<std::uint8_t> bytes_;
    std::size_t infoBase_ = 0, fileBlock_ = 0;
    std::map<std::string, Sound> sounds_;
    std::map<std::uint32_t, std::shared_ptr<const std::vector<std::uint8_t>>> sequences_;
    std::map<std::uint32_t, std::vector<Instrument>> banks_;
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::shared_ptr<const Pcm>> waves_;
};

} // namespace picarat::adventure56
