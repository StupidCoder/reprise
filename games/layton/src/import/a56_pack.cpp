// The fifth and sixth adventures' resource extraction. See
// a56_pack.hpp.
#include "import/a56_pack.hpp"
#include "import/a56_formats.hpp"
#include "import/pack_writer.hpp"
#include <algorithm>
#include <cstdio>

namespace picarat::import::adventure56 {

namespace {

bool tagIs(std::span<const std::uint8_t> data, std::string_view tag) {
    return data.size() >= tag.size() && std::equal(tag.begin(), tag.end(), data.begin());
}

// Saves one resource, expanding packs into folders and parsing what the
// importer can read.
void store(PackWriter& pack, const std::string& name, std::span<const std::uint8_t> data,
           PackSummary& summary) {
    if (tagIs(data, "XPCK") || tagIs(data, "XFSP")) {
        ++summary.packs;
        for (const PackMember& member : packMembers(data)) {
            store(pack, name + "/" + member.name, member.data, summary);
        }
        return;
    }
    if (tagIs(data, "IMGC")) {
        parseImgc(data);
        ++summary.pictures;
    } else if (tagIs(data, "XQ32")) {
        parseXq32(data);
        ++summary.scripts;
    } else if (tagIs(data, "XSCR")) {
        parseXscr(data);
        ++summary.dataLists;
    } else {
        ++summary.others;
    }
    check(!pack.contains(name), "two resources map to the same pack file");
    pack.save(name, data);
}

} // namespace

PackResult buildPack(CtrRom& rom, const std::filesystem::path& output, PackSummary& summary,
                     const PackProgress& progressCallback) {
    const Game game = rom.identity().game;
    check(game == Game::Adventure5 || game == Game::Adventure6, "not a 3DS adventure's ROM");
    const std::string root = game == Game::Adventure5 ? "lt5/" : "lt6/";
    PackWriter pack(output);
    auto progress = [&](double fraction, const char* message) {
        if (progressCallback) {
            progressCallback(fraction, message);
        }
    };

    pack.save("code/code.bin", rom.code());
    std::string segments;
    for (const auto& segment : rom.segments()) {
        char line[40];
        std::snprintf(line, sizeof line, "%08x %08x\n", segment.address, segment.size);
        segments += line;
    }
    pack.save("code/segments.txt", segments);

    // What is unpacked, weighted by size for the progress bar.
    std::vector<const CtrFile*> archives, copies;
    std::uint64_t totalBytes = 0, doneBytes = 0;
    for (const CtrFile& file : rom.files()) {
        if (file.path.starts_with(root + "arc/") && file.path.ends_with(".fa")) {
            archives.push_back(&file);
        } else if (file.path.starts_with(root + "snd/") || file.path.starts_with(root + "mov/")) {
            copies.push_back(&file);
        } else {
            continue;
        }
        totalBytes += file.size;
    }
    check(archives.size() == 3, "the ROM lacks one of the game's archives");

    for (const CtrFile* archive : archives) {
        // arc/lt5_ge.fa -> arc/ge/
        const std::string stem = archive->path.substr(archive->path.rfind('/') + 1);
        const std::string folder = "arc/" + stem.substr(4, stem.size() - 7) + "/";
        Bytes header(0x48);
        rom.read(archive->offset, header);
        const std::uint32_t tablesSize = archiveTablesSize(header);
        check(tablesSize <= archive->size && tablesSize <= (16u << 20), "invalid archive tables");
        Bytes tables(tablesSize);
        rom.read(archive->offset, tables);
        ++summary.archives;
        for (const ArchiveFile& file : archiveFiles(tables, archive->size)) {
            Bytes data(file.size);
            rom.read(archive->offset + file.offset, data);
            store(pack, folder + file.path, data, summary);
            doneBytes += file.size;
            progress(double(doneBytes) / double(totalBytes), "Extracting game resources…");
        }
    }

    for (const CtrFile* file : copies) {
        // lt5/snd/product/ge/stream/x.bcstm -> snd/stream/x.bcstm (the
        // language folder is the only one these games have).
        std::string name = file->path.substr(root.size());
        for (const std::string_view part : {"product/", "ge/"}) {
            if (const auto at = name.find(part);
                at != std::string::npos && at == name.find('/') + 1) {
                name.erase(at, part.size());
            }
        }
        const Bytes data = rom.read(*file);
        const std::string_view tag = name.ends_with(".bcsar")   ? "CSAR"
                                     : name.ends_with(".bcstm") ? "CSTM"
                                                                : "";
        if (name.ends_with(".moflex")) {
            // Mobiclip's container: a fixed synchronisation word.
            static constexpr std::uint8_t kSync[] = {0x4c, 0x32, 0xaa, 0xab};
            check(data.size() >= 4 && std::equal(kSync, kSync + 4, data.begin()),
                  "movie with an unexpected header");
            ++summary.movies;
        } else {
            check(!tag.empty() && tagIs(data, tag), "sound file with an unexpected header");
            ++summary.streams;
        }
        check(!pack.contains(name), "two resources map to the same pack file");
        pack.save(name, data);
        doneBytes += file->size;
        progress(double(doneBytes) / double(totalBytes), "Copying music and movies…");
    }

    pack.writeProvenance(rom.sha256());
    return {pack.outputs()};
}

} // namespace picarat::import::adventure56
