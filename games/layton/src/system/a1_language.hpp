#pragma once
// The first adventure's text and voice language. Its European cartridge
// holds five languages side by side: every language-specific resource sits
// in a two-letter folder (ani/de/..., qtext/fr/q000.pcm), and the program
// names such files with a '?' that it replaces with the folder of the
// language in use ("/data/qtext/?/q000.pcm").
//
// On the console the game does not ask: at start-up (ARM9 020199b8) it reads
// the language from the firmware's user settings (027FFC80 + 64h, bits 0-2,
// via the SDK routine at 0209f06c) and turns it into an index into its
// folder table (020d2ad0). The port offers the same choice as a setting,
// with "follow the system" as the console-like default.
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace picarat::adventure1 {

// The DS firmware's language numbers (GBATEK, firmware user settings).
enum class FirmwareLanguage : int {
    Japanese = 0,
    English = 1,
    French = 2,
    German = 3,
    Italian = 4,
    Spanish = 5,
    Chinese = 6,
    Korean = 7,
};

// ARM9 020199b8: the folder index for a firmware language. French, German,
// Italian and Spanish get their own folders; Japanese, English and anything
// else fall back to English (index 1).
int folderIndex(FirmwareLanguage language);

// ARM9 020d2ad0: eight pointers to the two-letter folder names, read from
// the resident program (`arm9`, loaded at 02000000). Index 0 is "jp"; the
// European cartridge has folders for indices 1 to 5 only.
inline constexpr std::uint32_t kFolderTable = 0x020d2ad0;
inline constexpr int kFolderCount = 8;
std::array<std::string, kFolderCount> folderNames(std::span<const std::uint8_t> arm9);

// A firmware language for an ISO 639-1 code ("de", "en-GB" also works), for
// mapping the host's preferred language the way the console's setting is
// mapped. Unknown codes give English, the firmware's default.
FirmwareLanguage firmwareLanguage(std::string_view isoCode);

// The language's name in itself ("Deutsch") for a folder code; the code
// itself when it is not one of the cartridge's.
const char* nativeName(std::string_view folder);

} // namespace picarat::adventure1
