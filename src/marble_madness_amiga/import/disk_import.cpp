#include "marble_madness_amiga/import/disk_import.hpp"
#include "amiga/adf.hpp"
#include "amiga/hunk.hpp"
#include "amiga/rom_font.hpp"
#include "marble_madness_amiga/import/loader_cipher.hpp"
#include "platform/sha256.hpp"
#include <array>
#include <string_view>

namespace marble_madness::import {
namespace {

// The release this was recovered from (Marble_Madness.adf, MD5
// 735dc697d64b3eeaa000778eb0b1153a). The hash is of the decrypted program.
constexpr std::string_view release_name = "Electronic Arts disk (1986)";
constexpr std::string_view volume_name = "MarbleMadness!";
constexpr std::string_view program_sha256 = "a03ea01d7e018250b6dd4a7539259ac4c1948652b0afcc8b78deed1642af19a2";

// The files the game program opens by name (the name tables at $1034FC,
// $103556, $1035AE, the sound and object loaders), installed under the same
// names. Ultimate's image library is Silly.ilb (the table names it twice);
// ultima.ilb is on the disk but never loaded, so it is not kept.
constexpr std::array data_files = {
    "practy.mlb", "beginr.mlb", "interm.mlb", "aerial.mlb", "silly.mlb", "ultima.mlb",
    "practy.ilb", "beginr.ilb", "interm.ilb", "aerial.ilb", "silly.ilb",
    "marbdat.vlb", "slink.vlb", "ooze.vlb", "birdink.vlb",
    "prcobsc.vlb", "begobsc.vlb", "intobsc.vlb", "aerobsc.vlb", "silobsc.vlb", "ultobsc.vlb",
    "PrcTrack", "BegTrack", "IntTrack", "aertrack", "SilTrack", "ulttrack",
    "PrcSnd", "BegSnd", "IntSnd", "AerSnd", "SilSnd", "UltSnd",
    "marbdat", "defcop",
};

// c/xxx's key for the game program (xxx $248-$294): entry i is 1499 / ((i +
// 1) * 300) for the 20 entries the launcher asks for. 1499 replaces the
// track length xxx measures (the compare at $240 is followed by a NOP and
// the constant). The launcher then XORs each entry with the checksum of the
// loaded c/xxx (MarbleMadness! $56E-$5D4).
std::vector<std::uint32_t> program_key(std::uint16_t xxx_checksum) {
    constexpr int entries = 20;
    std::vector<std::uint32_t> key;
    for (int i = 0; i < entries; ++i) {
        key.push_back(std::uint32_t(1499 / ((i + 1) * 300)) ^ xxx_checksum);
    }
    return key;
}

} // namespace

app::ImportResult import_disk(std::span<const std::uint8_t> image) {
    if (!amiga::is_adf(image)) {
        throw app::NotThisGame("This is not an Amiga disk image (ADF).");
    }
    const amiga::Volume volume(image);
    if (volume.name() != volume_name || volume.find("c/MarbleMadness!.dat") == nullptr ||
        volume.find("c/xxx") == nullptr) {
        throw app::NotThisGame("This disk is not Marble Madness.");
    }
    const auto machine = MachineKey::kickstart_1x();
    const auto xxx = decrypt_load_file(volume.read("c/xxx"), {}, machine);
    const auto program =
        decrypt_load_file(volume.read("c/MarbleMadness!.dat"), program_key(relocation_free_checksum(xxx)), machine);
    if (platform::sha256(program) != program_sha256) {
        throw std::runtime_error("This disk holds a different release of Marble Madness, which is not supported yet.");
    }
    app::ImportResult result;
    result.release = std::string(release_name);
    result.files.push_back({"program.hunks", program});
    for (const char* name : data_files) {
        result.files.push_back({name, volume.read(name)});
    }
    result.files.push_back({"splash.iff", volume.read("c/splash")});
    return result;
}

app::ImportResult import_kickstart(std::span<const std::uint8_t> rom) {
    const bool encrypted = rom.size() >= 11 && std::string_view(reinterpret_cast<const char*>(rom.data()), 11) == "AMIROMTYPE1";
    if (!encrypted && !amiga::is_kickstart(rom)) {
        throw app::NotThisGame("This is not a Kickstart ROM.");
    }
    const auto font = amiga::find_rom_font(rom, "topaz.font", 9);
    app::ImportResult result;
    result.release = "the Kickstart's topaz 9 font";
    result.files.push_back({"topaz9.font", amiga::save_font(font), true});
    return result;
}

} // namespace marble_madness::import
