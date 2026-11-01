// Recognizes the supported cartridge before asking the bounded engine importer to decode it.
#include "import.hpp"
#include "../src/importer/Importer.hpp"
#include "../src/importer/Inputs.hpp"
#include <array>
#include <cstring>

namespace captain_toad::reprise {
std::string import_into(::platform::FileSource& source, const std::filesystem::path& folder,
                        const ::app::ImportProgress& progress) {
    // Recognition is intentionally shallow. Once the identity matches, malformed data is a
    // useful import error, not NotThisGame (which would hide it while trying another engine).
    if (source.size() < 0x200) {
        throw ::app::NotThisGame("Not a supported cartridge");
    }
    Bytes header(0x200);
    source.read(0, header);
    if (std::memcmp(header.data() + 0x100, "NCSD", 4)) {
        throw ::app::NotThisGame("Not a CCI image");
    }
    const uint64_t offset = uint64_t(u32(header, 0x120)) * 0x200;
    if (offset > source.size() || 0x200 > source.size() - offset) {
        throw ::app::NotThisGame("Cannot identify cartridge partition");
    }
    source.read(offset, header);
    if (std::memcmp(header.data() + 0x100, "NCCH", 4) || u64(header, 0x118) != kSupportedProgramId) {
        throw ::app::NotThisGame("Not the supported cartridge release");
    }
    RomImage image(source.size(), [&](uint64_t at, std::span<uint8_t> out) { source.read(at, out); });
    importInto(image, folder, [&](double fraction, const std::string& phase) {
        if (progress) {
            progress(phase, fraction);
        }
    });
    return "European release, program 00040000001CB200";
}
}   // namespace captain_toad::reprise
