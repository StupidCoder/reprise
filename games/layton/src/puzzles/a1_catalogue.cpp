// The first adventure's puzzle catalogue. See a1_catalogue.hpp.
#include "puzzles/a1_catalogue.hpp"
#include "graphics/text.hpp"
#include "system/a1_script.hpp"
#include <regex>
#include <stdexcept>

namespace picarat::adventure1 {

namespace {
constexpr std::uint32_t kArm9Base = 0x02000000;
constexpr std::uint32_t kNumberTable = 0x020daeb0; // int per internal number
constexpr unsigned kNumberEntries = 256;           // 02001044 accepts 1..0xfe

// Runs a catalogue script with one command bound.
void runList(const std::filesystem::path& path, std::uint16_t command,
             const ScriptContext::Handler& handler) {
    const Bytes bytes = readFile(path);
    ScriptContext context;
    context.bind(command, handler);
    ScriptInterpreter(bytes, scripts()).run(context);
}

std::string folderFor(unsigned id) {
    return id < 50 ? "q000" : id < 100 ? "q050" : "q100";
}
} // namespace

Catalogue::Catalogue(const std::filesystem::path& pack, const std::string& language)
    : pack_(pack), language_(language) {
    const GameText text(pack);
    // Every puzzle with a parameter script; its engine command names the engine.
    const std::regex name("q([0-9]+)_param\\.gds");
    for (const auto& entry : std::filesystem::directory_iterator(pack / "script/qscript")) {
        std::smatch m;
        const std::string file = entry.path().filename().string();
        if (!std::regex_match(file, m, name)) {
            continue;
        }
        PuzzleInfo info;
        info.id = unsigned(std::stoul(m[1].str()));
        const Bytes bytes = readFile(entry.path());
        ScriptInterpreter interpreter(bytes, scripts());
        // Every parameter script starts with the engine command; reading
        // stops there (the rest are the engine's parameters).
        ScriptContext context;
        context.bind(command::Engine, [&](CommandArguments& a) {
            info.engine = a.string();
            interpreter.reader.finish();
        });
        interpreter.run(context);
        puzzles_[info.id] = std::move(info);
    }
    // The numbers the player sees.
    const Bytes arm9 = readFile(pack / "code/arm9.bin");
    for (auto& [id, info] : puzzles_) {
        if (id < kNumberEntries) {
            info.number = read32(arm9, kNumberTable - kArm9Base + 4 * id);
        }
    }
    auto at = [&](unsigned id) -> PuzzleInfo* {
        auto it = puzzles_.find(id);
        return it == puzzles_.end() ? nullptr : &it->second;
    };
    runList(pack / "script/pcarot/pscript.gds", 0xc3, [&](CommandArguments& a) {
        const auto id = unsigned(a.integer());
        std::array<unsigned, 3> values{};
        for (auto& v : values) {
            v = unsigned(a.integer());
        }
        if (auto* p = at(id)) {
            p->picarats = values;
        }
    });
    const auto languageFolder = pack / "script/puzzletitle" / language / "qtitle.gds";
    runList(languageFolder, command::PuzzleTitle, [&](CommandArguments& a) {
        const auto id = unsigned(a.integer());
        const std::string title = a.string();
        if (auto* p = at(id)) {
            p->title = text.expand(title);
        }
    });
    runList(pack / "script/qinfo" / language / "qscript.gds", command::PuzzleInfo,
            [&](CommandArguments& a) {
                const auto id = unsigned(a.integer());
                const std::string kind = a.string(), place = a.string();
                if (auto* p = at(id)) {
                    p->kind = text.expand(kind);
                    p->place = text.expand(place);
                }
            });
}

const PuzzleInfo* Catalogue::find(unsigned id) const {
    auto it = puzzles_.find(id);
    return it == puzzles_.end() ? nullptr : &it->second;
}

std::string Catalogue::text(unsigned id, const std::string& kind) const {
    std::string file;
    if (kind.starts_with("h_")) {
        file = "h_" + std::to_string(id) + "_" + kind.substr(2) + ".txt";
    } else {
        file = kind + "_" + std::to_string(id) + ".txt";
    }
    const auto path = pack_ / "qtext" / language_ / folderFor(id) / file;
    if (!fileExists(path)) {
        return {};
    }
    const Bytes raw = readFile(path);
    return GameText(pack_).expand(std::string(raw.begin(), raw.end()));
}

} // namespace picarat::adventure1
