#include "system/a2_character_names.hpp"
#include "system/resources.hpp"
#include <stdexcept>

namespace picarat {
CharacterNames loadCharacterNames(const std::filesystem::path& root) {
    // "L2NM", version 1, then four length-prefixed strings in struct order.
    const Bytes b = readFile(root / "names.bin");
    if (b.size() < 8 || std::string(b.begin(), b.begin() + 4) != "L2NM" || read32(b, 4) != 1) {
        throw std::runtime_error("names table header");
    }
    CharacterNames names;
    std::size_t pos = 8;
    for (auto* list : {&names.nameEntry, &names.teaRequest}) {
        for (std::string& name : *list) {
            const auto n = read32(b, pos);
            pos += 4;
            if (n > 16 || n > b.size() - pos) {
                throw std::runtime_error("names table bounds");
            }
            name.assign(b.begin() + std::ptrdiff_t(pos), b.begin() + std::ptrdiff_t(pos + n));
            pos += n;
        }
    }
    if (pos != b.size()) {
        throw std::runtime_error("names table tail");
    }
    return names;
}
} // namespace picarat
