// The first adventure's resource extraction. See a1_pack.hpp.
#include "import/a1_pack.hpp"
#include "import/a1_formats.hpp"
#include "import/asset_codecs.hpp"
#include "import/pack_writer.hpp"
#include "import/sha256.hpp"
#include "system/a1_language.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace picarat::import::adventure1 {

namespace {

constexpr std::string_view kDataRoot = "data/";

bool tagIs(std::span<const std::uint8_t> data, std::string_view tag) {
    return data.size() >= tag.size() && std::equal(tag.begin(), tag.end(), data.begin());
}

// The path with its language folder replaced by '?', as the program writes
// it, and that folder's code; an empty code for shared resources.
std::pair<std::string, std::string> splitLanguage(const std::string& path,
                                                  const std::set<std::string>& codes) {
    for (std::size_t start = 0, end; (end = path.find('/', start)) != std::string::npos;
         start = end + 1) {
        const std::string part = path.substr(start, end - start);
        if (codes.count(part)) {
            return {path.substr(0, start) + "?" + path.substr(end), part};
        }
    }
    return {path, {}};
}

} // namespace

PackResult buildPack(const NdsRom& rom, const std::filesystem::path& output, PackSummary& summary,
                     const PackProgress& progressCallback) {
    check(rom.identity().game == Game::Adventure1, "not the first adventure's ROM");
    PackWriter pack(output);
    auto progress = [&](double fraction, const char* message) {
        if (progressCallback) {
            progressCallback(fraction, message);
        }
    };

    // The resident program and its one ARM9 overlay (the WiFi code the
    // second adventure keeps in its overlay 4), expanded.
    const Bytes arm9 = rom.arm9Program();
    pack.save("code/arm9.bin", arm9);
    // The ARM7 program: its sound driver's tables (sdat_sequencer.hpp).
    pack.save("code/arm7.bin", rom.arm7Program());
    for (const RomFile& file : rom.files()) {
        if (file.overlay && file.overlay->arm9) {
            pack.save("code/overlay" + std::to_string(file.overlay->id) + ".bin",
                      decompressCode(rom.contents(file)));
        }
    }

    // The text escapes ("<:o>" for o-umlaut ...): ARM9 0200c2d8 expands them
    // through the {name, character} table at 020bbf10, ending at a null name.
    // It ends a name at a '>' not followed by another '>' and lets the last
    // matching entry win; no text of this ROM depends on either (all names
    // are unique and none contains '>'), so the shared "<name>" rule of
    // GameText applies.
    {
        constexpr std::uint32_t base = 0x02000000, table = 0x020bbf10;
        auto word = [&](std::uint32_t address) {
            check(address >= base && address + 4 <= base + arm9.size(),
                  "escape table outside ARM9");
            return read32(arm9, address - base);
        };
        std::vector<std::pair<std::string, std::uint32_t>> escapes;
        for (std::uint32_t entry = table; word(entry); entry += 8) {
            std::string name;
            for (std::uint32_t c = word(entry); arm9.at(c - base); ++c) {
                name += char(arm9.at(c - base));
            }
            check(std::none_of(escapes.begin(), escapes.end(),
                               [&](const auto& e) { return e.first == name; }),
                  "repeated text escape");
            escapes.emplace_back(name, word(entry + 4));
        }
        check(escapes.size() == 70, "unexpected text escape table");
        pack.save("escapes.bin", escapeTableFile(escapes, '<', '>'));
    }

    // The language folders the program can choose from.
    const auto folders = picarat::adventure1::folderNames(arm9);
    const std::set<std::string> codes(folders.begin(), folders.end());

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
    // Each language-specific resource ('?' path) and the languages it has.
    std::map<std::string, std::set<std::string>> localized;

    for (const RomFile* file : files) {
        const std::string relative = file->path.substr(kDataRoot.size());
        const auto dot = relative.rfind('.');
        const std::string stem = relative.substr(0, dot);
        const std::string extension = dot == std::string::npos ? "" : relative.substr(dot + 1);
        const std::string top = relative.substr(0, relative.find('/'));
        const auto raw = rom.contents(*file);
        bool packed = true;
        if (extension == "arc" || extension == "arj") {
            const Bytes data = expandAsset(raw, file->path);
            if (top == "bg") {
                check(extension == "arc", "tiled sprite among the backgrounds");
                save(stem + ".l2bg", nativeBackground(data, false));
                ++summary.backgrounds;
            } else {
                check(top == "ani", "graphic outside the picture folders");
                // ani/clock_numbers.arc breaks off after two frames (the
                // third frame's pixel rows do not match its header). No
                // program string or script names it and its .arj twin is
                // sound, so it is left out (hypothesis: an unused leftover).
                if (relative == "ani/clock_numbers.arc") {
                    packed = false;
                } else {
                    const Sprite sprite = decodeSprite(data, extension == "arj");
                    // No sprite of this game names an attached child sprite
                    // (the second adventure's ani/sub/ has no counterpart).
                    check(spriteChild(sprite).empty(), "sprite with a child sprite");
                    save(relative + ".l2sp", nativeSprite(sprite, {}, false));
                    ++summary.sprites;
                }
            }
        } else if (extension == "bgx") {
            // The sixteen staff-roll pictures bg/staff_top<n> exist both as
            // .arc and stored as .bgx; both decode to the same picture, which
            // is packed once.
            const Bytes data = nativeBackground(raw, false);
            const std::string name = stem + ".l2bg";
            if (!pack.contains(name) || pack.outputs().at(name) != sha256(data)) {
                save(name, data);
                ++summary.backgrounds;
            }
        } else if (extension == "pcm") {
            const Bytes data = decompress(raw);
            for (const LpckMember& member : lpckMembers(data)) {
                save(stem + "/" + member.name, member.data);
                ++summary.texts;
            }
        } else if (extension == "gds") {
            scriptTokens(raw);
            ++summary.scripts;
            save(relative, raw);
        } else if (extension == "NFTR") {
            // Text advances come from the program's 224-entry width tables
            // (characters 0x20..0xff), not from the fonts: fontevent (text
            // layer font +4) uses 020d2710 (0200d270), fontq (+0x14, the
            // question text) 020d2630 (0200d2c0); font18 has none.
            const std::uint32_t table = relative == "font/fontevent.NFTR" ? 0x020d2710
                                        : relative == "font/fontq.NFTR"   ? 0x020d2630
                                                                          : 0;
            const auto widths = table ? std::span(arm9).subspan(table - 0x02000000, 224)
                                      : std::span<const std::uint8_t>{};
            save(relative + ".l2ft", nativeFont(decodeFont(raw), widths));
            ++summary.fonts;
        } else if (extension == "SAD" || extension == "mods") {
            check(tagIs(raw, extension == "SAD" ? "sadl" : "MODS"),
                  "stream or movie with an unexpected header");
            ++(extension == "SAD" ? summary.streams : summary.movies);
            save(relative, raw);
        } else if (extension == "sdat") {
            const SoundArchive sound = parseSoundArchive(raw);
            summary.sequences += sound.sequences;
            summary.soundEffectArchives += sound.sequenceArchives;
            save(relative, raw);
        } else if (relative == "decuma/_databas_le.bin" || relative == "debug/puzzledata.dat") {
            // The handwriting recogniser's database (the same file as the
            // second adventure's) and a debug puzzle table.
            save(relative, raw);
        } else if (extension == "txt" && relative.starts_with("otext/en/")) {
            // Not packed: loose copies of the members of otext/en/otext.pcm
            // (byte-identical; the program only opens the archive).
            packed = false;
        } else {
            throw FormatError("resource of an unknown kind: " + relative);
        }
        if (packed) {
            const auto [shared, code] = splitLanguage(relative, codes);
            if (!code.empty()) {
                localized[shared].insert(code);
            }
        }
        doneBytes += file->size;
        progress(double(doneBytes) / double(totalBytes), "Extracting game resources…");
    }

    // Every language-specific resource must exist in the same languages,
    // so that any of them can be chosen.
    check(!localized.empty(), "the ROM has no language folders");
    const std::set<std::string>& available = localized.begin()->second;
    for (const auto& [path, languages] : localized) {
        check(languages == available, "a resource is missing in one of the languages");
    }
    std::string list;
    for (int i = 0; i < picarat::adventure1::kFolderCount; ++i) {
        if (available.count(folders[std::size_t(i)])) {
            summary.languages.push_back(folders[std::size_t(i)]);
            list += std::to_string(i) + " " + folders[std::size_t(i)] + "\n";
        }
    }
    check(summary.languages.size() == available.size(), "a language folder the program lacks");
    pack.save("languages.txt", list);

    pack.writeProvenance(rom.sha256());
    return {pack.outputs()};
}

} // namespace picarat::import::adventure1
