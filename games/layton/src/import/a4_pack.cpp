// The fourth adventure's resource extraction. See a4_pack.hpp.
#include "import/a4_pack.hpp"
#include "import/a4_formats.hpp"
#include "import/asset_codecs.hpp"
#include "import/pack_writer.hpp"
#include <algorithm>

namespace picarat::import::adventure4 {

namespace {

// The archives the game mounts (overlay 8 names them "../lt4_main_%s.fa"
// with the language, "../lt4_sub.fa" and "../lt4_seq.fa") and their pack
// folders.
constexpr struct {
    std::string_view rom, folder;
} kArchives[] = {
    {"lt4_main_ge.fa", "main/"},
    {"lt4_seq.fa", "seq/"},
    {"lt4_sub.fa", "sub/"},
};

// Plain files under lt4/: streams, wave banks and movies.
constexpr struct {
    std::string_view folder, extension, tag;
} kStreams[] = {
    {"lt4/str/", ".SAD", "sadl"},
    {"lt4/wav/", ".SWD", "swdl"},
    {"lt4/mov/", ".mods", "MODS"},
};

bool tagIs(std::span<const std::uint8_t> data, std::string_view tag) {
    return data.size() >= tag.size() && std::equal(tag.begin(), tag.end(), data.begin());
}

// Parses one unwrapped resource by its contents and counts it. Unknown
// contents are accepted only where the ROM has them (the handwriting
// data), so a new variant shows up as an import error.
void validate(std::span<const std::uint8_t> data, std::string_view name, PackSummary& summary) {
    if (tagIs(data, "GSS1")) {
        parseGss1(data);
        ++summary.scripts;
    } else if (tagIs(data, "GSD1")) {
        parseGsd1(data);
        ++summary.dataLists;
    } else if (tagIs(data, "GTXT")) {
        parseGtxt(data);
        ++summary.pictures;
    } else if (tagIs(data, "GPLT")) {
        parseGplt(data);
        ++summary.palettes;
    } else if (tagIs(data, "GFNT")) {
        parseGfnt(data);
        ++summary.fonts;
    } else if (tagIs(data, "smdl") || tagIs(data, "swdl") || tagIs(data, "sedl")) {
        ++summary.sounds;
    } else {
        // The handwriting box: its database (the same bytes as the third
        // adventure's) and a conversion list.
        check(name.ends_with("/scrdb.dat") || name.ends_with("/conv_list.gu"),
              "resource of an unknown kind");
    }
}

} // namespace

PackResult buildPack(const NdsRom& rom, const std::filesystem::path& output, PackSummary& summary,
                     const PackProgress& progressCallback) {
    check(rom.identity().game == Game::Adventure4, "not the fourth adventure's ROM");
    PackWriter pack(output);
    auto progress = [&](double fraction, const char* message) {
        if (progressCallback) {
            progressCallback(fraction, message);
        }
    };

    // The resident program and every ARM9 overlay, expanded.
    pack.save("code/arm9.bin", rom.arm9Program());
    for (const RomFile& file : rom.files()) {
        if (file.overlay && file.overlay->arm9) {
            pack.save("code/overlay" + std::to_string(file.overlay->id) + ".bin",
                      decompressCode(rom.contents(file)));
        }
    }

    // Progress is weighted by the stored size of what is unpacked.
    std::uint64_t totalBytes = 0, doneBytes = 0;
    for (const auto& archive : kArchives) {
        const RomFile* file = rom.find(archive.rom);
        check(file != nullptr, "the ROM lacks one of the game's archives");
        totalBytes += file->size;
    }
    for (const RomFile& file : rom.files()) {
        for (const auto& kind : kStreams) {
            if (file.path.starts_with(kind.folder) && file.path.ends_with(kind.extension)) {
                totalBytes += file.size;
            }
        }
    }
    auto save = [&](const std::string& name, std::span<const std::uint8_t> data) {
        check(!pack.contains(name), "two resources map to the same pack file");
        pack.save(name, data);
    };

    for (const auto& archive : kArchives) {
        const RomFile& romFile = *rom.find(archive.rom);
        const auto files = gfsaFiles(rom.contents(romFile));
        for (const ArchiveFile& file : files) {
            const Bytes data = decompressMethodWord(file.stored);
            const auto dot = file.path.rfind('.');
            const auto slash = file.path.rfind('/');
            check(dot != std::string::npos && (slash == std::string::npos || dot > slash) &&
                      file.path[dot + 1] == 'c',
                  "archive file without a packed extension");
            const std::string stem = std::string(archive.folder) + file.path.substr(0, dot);
            if (tagIs(data, "GFSP")) {
                for (const PackMember& member : gfspMembers(data)) {
                    const std::string name = stem + "/" + member.name;
                    validate(member.data, name, summary);
                    save(name, member.data);
                }
            } else {
                const std::string name = stem + ".l" + file.path.substr(dot + 2);
                validate(data, name, summary);
                save(name, data);
            }
            doneBytes += file.stored.size();
            progress(double(doneBytes) / double(totalBytes), "Extracting game resources…");
        }
    }

    for (const RomFile& file : rom.files()) {
        for (const auto& kind : kStreams) {
            if (!file.path.starts_with(kind.folder) || !file.path.ends_with(kind.extension)) {
                continue;
            }
            const auto raw = rom.contents(file);
            check(tagIs(raw, kind.tag), "stream, wave bank or movie with an unexpected header");
            save(file.path.substr(4), raw); // without "lt4/"
            ++(kind.tag == "sadl"   ? summary.streams
               : kind.tag == "swdl" ? summary.sounds
                                    : summary.movies);
            doneBytes += file.size;
            progress(double(doneBytes) / double(totalBytes), "Copying music and movies…");
        }
    }
    // Not packed: the Wi-Fi library's utility file (dwc/utility.bin).

    pack.writeProvenance(rom.sha256());
    return {pack.outputs()};
}

} // namespace picarat::import::adventure4
