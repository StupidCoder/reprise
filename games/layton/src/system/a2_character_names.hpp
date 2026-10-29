#pragma once
// Names of the two lead characters, as the original code embeds them: the
// name-entry overlay rejects them as player names, and the tea overlay puts
// them into the bag-menu tea request. The importer copies the strings from
// those overlays into names.bin (src/import/asset_pack.cpp), so the native
// code compares and displays the ROM's own bytes.
#include <array>
#include <filesystem>
#include <string>

namespace picarat {
struct CharacterNames {
    // [0] the protagonist, [1] the assistant.
    std::array<std::string, 2> nameEntry;  // ov12 literal pool 020f03cc / 020f03d0
    std::array<std::string, 2> teaRequest; // ov14 literal pool 020f1e68 / 020f1e6c
};

CharacterNames loadCharacterNames(const std::filesystem::path& assets);
} // namespace picarat
