// Text lists of the 3DS adventures. See a56_text.hpp.
#include "graphics/a56_text.hpp"
#include "graphics/a56_assets.hpp"
#include <cstring>

namespace picarat::adventure56 {

std::shared_ptr<TextList> loadTextList(const Pack& pack, const std::filesystem::path& file) {
    const auto list = import::adventure56::parseXscr(pack.read(file));
    auto out = std::make_shared<TextList>();
    auto text = [&](const import::adventure56::Operand& o) -> std::string {
        if ((o.type & 15) != 8 || o.value >= list.strings.size()) {
            return {};
        }
        const auto* p = reinterpret_cast<const char*>(list.strings.data() + o.value);
        return std::string(p, strnlen(p, list.strings.size() - o.value));
    };
    for (const auto& c : list.commands) {
        if (c.opcode != 1001 || c.count < 3) {
            continue;
        }
        const auto& index = list.operands[c.first];
        out->entries[int(index.value)].push_back(text(list.operands[c.first + 2u]));
        out->names[int(index.value)].push_back(text(list.operands[c.first + 1u]));
    }
    return out;
}

} // namespace picarat::adventure56
