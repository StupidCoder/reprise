// The first adventure's language choice. See a1_language.hpp.
#include "system/a1_language.hpp"
#include <cctype>
#include <stdexcept>

namespace picarat::adventure1 {

int folderIndex(FirmwareLanguage language) {
    // 020199b8 switches on the firmware value 0..5 and stores the index in
    // its state word (+0x758), which the path routine 0201bc8c uses to look
    // up the folder that replaces '?'.
    switch (language) {
    case FirmwareLanguage::French:
        return 3;
    case FirmwareLanguage::German:
        return 5;
    case FirmwareLanguage::Italian:
        return 4;
    case FirmwareLanguage::Spanish:
        return 2;
    default: // Japanese, English and values above 5
        return 1;
    }
}

std::array<std::string, kFolderCount> folderNames(std::span<const std::uint8_t> arm9) {
    constexpr std::uint32_t kBase = 0x02000000;
    auto word = [&](std::uint32_t address) {
        const std::size_t at = address - kBase;
        if (address < kBase || at + 4 > arm9.size()) {
            throw std::runtime_error("language table outside the program");
        }
        return std::uint32_t(arm9[at]) | std::uint32_t(arm9[at + 1]) << 8 |
               std::uint32_t(arm9[at + 2]) << 16 | std::uint32_t(arm9[at + 3]) << 24;
    };
    std::array<std::string, kFolderCount> names;
    for (int i = 0; i < kFolderCount; ++i) {
        const std::uint32_t text = word(kFolderTable + 4 * std::uint32_t(i));
        word(text); // the two letters and their NUL lie inside the program
        const std::size_t at = text - kBase;
        names[std::size_t(i)] = {char(arm9[at]), char(arm9[at + 1])};
        if (arm9[at + 2] != 0 || !std::islower(arm9[at]) || !std::islower(arm9[at + 1])) {
            throw std::runtime_error("unexpected language folder name");
        }
    }
    return names;
}

FirmwareLanguage firmwareLanguage(std::string_view isoCode) {
    const std::string code{isoCode.substr(0, 2)};
    if (code == "ja") {
        return FirmwareLanguage::Japanese;
    }
    if (code == "fr") {
        return FirmwareLanguage::French;
    }
    if (code == "de") {
        return FirmwareLanguage::German;
    }
    if (code == "it") {
        return FirmwareLanguage::Italian;
    }
    if (code == "es") {
        return FirmwareLanguage::Spanish;
    }
    if (code == "zh") {
        return FirmwareLanguage::Chinese;
    }
    if (code == "ko") {
        return FirmwareLanguage::Korean;
    }
    return FirmwareLanguage::English;
}

const char* nativeName(std::string_view folder) {
    if (folder == "en") {
        return "English";
    }
    if (folder == "fr") {
        return "Français";
    }
    if (folder == "de") {
        return "Deutsch";
    }
    if (folder == "es") {
        return "Español";
    }
    if (folder == "it") {
        return "Italiano";
    }
    return "?";
}

} // namespace picarat::adventure1
