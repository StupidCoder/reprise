// The third adventure's resource extraction. See a3_pack.hpp.
#include "import/a3_pack.hpp"
#include "import/a3_formats.hpp"
#include "import/asset_codecs.hpp"
#include "import/pack_writer.hpp"
#include <algorithm>

namespace picarat::import::adventure3 {

namespace {

constexpr std::string_view kDataRoot = "lt3/";

// The text escapes "{xy}": ov0 0206aacc compares the two characters with
// 3-byte entries {x, y, character} here, first match wins, until an entry
// whose first byte is 0.
constexpr std::uint32_t kEscapeTable = 0x0206daa8;

bool tagIs(std::span<const std::uint8_t> data, std::string_view tag) {
    return data.size() >= tag.size() && std::equal(tag.begin(), tag.end(), data.begin());
}

// Parses one unwrapped resource by its contents and counts it. Unknown
// contents are accepted only where the ROM has them (the handwriting
// database), so a new variant shows up as an import error.
void validate(std::span<const std::uint8_t> data, std::string_view name, PackSummary& summary) {
    if (tagIs(data, "LIMG")) {
        parseLimg(data);
        ++summary.pictures;
    } else if (tagIs(data, "LSCR")) {
        parseLscr(data);
        ++summary.scripts;
    } else if (tagIs(data, "RTFN")) {
        decodeFont(data);
        ++summary.fonts;
    } else if (tagIs(data, "smdl") || tagIs(data, "swdl") || tagIs(data, "sedl")) {
        ++summary.sounds;
    } else {
        check(name.ends_with(".ldcm"), "resource of an unknown kind");
    }
}

} // namespace

PackResult buildPack(const NdsRom& rom, const std::filesystem::path& output, PackSummary& summary,
                     const PackProgress& progressCallback) {
    check(rom.identity().game == Game::Adventure3, "not the third adventure's ROM");
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

    // The text escape table, as escapes.bin for the shared GameText.
    const RomFile* overlay0 = rom.overlay(true, 0);
    check(overlay0 != nullptr, "missing ARM9 overlay 0");
    const Bytes text = decompressCode(rom.contents(*overlay0));
    const std::uint32_t base = overlay0->overlay->loadAddress;
    check(kEscapeTable >= base, "escape table outside overlay 0");
    std::vector<std::pair<std::string, std::uint32_t>> escapes;
    for (std::size_t at = kEscapeTable - base;; at += 3) {
        check(at + 3 <= text.size(), "unterminated escape table");
        if (text[at] == 0) {
            break;
        }
        const std::string key{char(text[at]), char(text[at + 1])};
        if (std::none_of(escapes.begin(), escapes.end(),
                         [&](const auto& e) { return e.first == key; })) {
            escapes.emplace_back(key, text[at + 2]);
        }
    }
    check(escapes.size() == 82, "unexpected escape table size");
    pack.save("escapes.bin", escapeTableFile(escapes, '{', '}'));

    // The game's files, in FAT order, weighted by size for the progress bar.
    std::vector<const RomFile*> files;
    std::uint64_t totalBytes = 0, doneBytes = 0;
    for (const RomFile& file : rom.files()) {
        if (file.path.starts_with(kDataRoot)) {
            files.push_back(&file);
            totalBytes += file.size;
        }
    }
    check(!files.empty(), "the ROM has no game data folder");
    auto save = [&](const std::string& name, std::span<const std::uint8_t> data) {
        check(!pack.contains(name), "two resources map to the same pack file");
        pack.save(name, data);
    };
    for (const RomFile* file : files) {
        const std::string relative = file->path.substr(kDataRoot.size());
        const auto dot = relative.rfind('.');
        const std::string stem = relative.substr(0, dot);
        const std::string extension = dot == std::string::npos ? "" : relative.substr(dot + 1);
        const auto raw = rom.contents(*file);
        if (extension == "SAD" || extension == "SWD" || extension == "mods") {
            const std::string_view tag = extension == "SAD"   ? "sadl"
                                         : extension == "SWD" ? "swdl"
                                                              : "MODS";
            check(tagIs(raw, tag), "stream, wave bank or movie with an unexpected header");
            save(relative, raw);
            ++(extension == "SAD"   ? summary.streams
               : extension == "SWD" ? summary.sounds
                                    : summary.movies);
        } else if (extension.size() == 4 && extension[0] == 'c' && raw.size() > 0) {
            const Bytes data = expandAsset(raw, file->path);
            if (tagIs(data, "LPC2")) {
                for (const Lpc2Member& member : lpc2Members(data)) {
                    const std::string name = stem + "/" + member.name;
                    validate(member.data, name, summary);
                    save(name, member.data);
                }
            } else {
                const std::string name = stem + ".l" + extension.substr(1);
                validate(data, name, summary);
                save(name, data);
            }
        }
        // Not packed: empty placeholders (evt/<n>/dummy.cbin), the
        // localisation batch files and the version note left by the build.
        doneBytes += file->size;
        progress(double(doneBytes) / double(totalBytes), "Extracting game resources…");
    }

    pack.writeProvenance(rom.sha256());
    return {pack.outputs()};
}

} // namespace picarat::import::adventure3
