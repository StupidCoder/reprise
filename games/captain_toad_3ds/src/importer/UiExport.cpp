// See UiExport.hpp. Texture and font conversion as in the opening's layout export
// (SceneExport.cpp layouts()), for every layout of the archive.
#include "UiExport.hpp"

#include "Importer.hpp"
#include "Layout.hpp"
#include "Message.hpp"
#include "Texture.hpp"

#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unistd.h>

namespace captain_toad {

namespace fs = std::filesystem;
using json::Value;

namespace {

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

class UiBuilder {
public:
    UiBuilder(Inputs& in, std::string archive, fs::path out)
        : in_(in), name_(std::move(archive)), out_(std::move(out)) {}

    void run() {
        std::string arc = "UIX2/Eu/" + name_ + ".arc";
        Value index = Value::object();
        std::vector<SarcMember> merged;
        if (name_ == "LayoutData") {
            // The second layout system (LayoutData/<name>.szs: layout.lyarc with the layout, its
            // animations and textures, and actor BYMLs), merged into one package; members found in
            // several archives (shared parts) are taken once.
            std::set<std::string> seen;
            for (const auto& path : romfsPaths()) {
                if (path.rfind("LayoutData/", 0) != 0 || !endsWith(path, ".szs")) {
                    continue;
                }
                for (const auto& m : in_.archive(path)) {
                    if (m.name != "layout.lyarc") {
                        continue;
                    }
                    for (auto& x : sarc(m.data)) {
                        if (seen.insert(x.name).second) {
                            merged.push_back(std::move(x));
                        }
                    }
                }
            }
            index["definition"] = json::parse("{\"ui\": {\"lyt\": []}}");
            arc = "LayoutData/*.szs";
        } else {
            const Bytes& definition = in_.romfs("UIX2/Eu/" + name_ + ".json");
            index["definition"] = json::parse(std::string(definition.begin(), definition.end()));
        }
        const auto& files = name_ == "LayoutData" ? merged : in_.archive(arc);
        index["schema"] = kUiSchema;
        index["archive"] = "rom:" + arc;
        Value layouts = Value::array();
        for (const auto& m : files) {
            if (m.name.rfind("blyt/", 0) != 0 || !endsWith(m.name, ".bflyt")) {
                continue;
            }
            const std::string layout = m.name.substr(5, m.name.size() - 11);
            Value lyt = readBflyt(m.data);
            Value tex = Value::array(), fnt = Value::array();
            for (const auto& t : lyt.at("textures").items()) {
                tex.push(texture(t.str(), files));
            }
            for (const auto& f : lyt.at("fonts").items()) {
                fnt.push(font(f.str(), files));
            }
            lyt["texture_files"] = tex;
            lyt["font_files"] = fnt;
            Value anims = Value::object();
            const std::string prefix = "anim/" + layout + "_";
            for (const auto& a : files) {
                if (a.name.rfind(prefix, 0) == 0 && endsWith(a.name, ".bflan")) {
                    Value an = readBflan(a.data);
                    // The tag name is the part after the layout name (several layouts share
                    // prefixes, e.g. CourseBook_TOP and CourseBookParts: keep only exact owners).
                    const std::string tag = a.name.substr(prefix.size(), a.name.size() - prefix.size() - 6);
                    if (an.has("tag") && an.at("tag").at("name").str() == tag) {
                        anims[tag] = an;
                    } else {
                        anims[tag] = an;
                    }
                }
            }
            lyt["animations"] = anims;
            Value src = Value::object();
            src["archive"] = "rom:" + arc;
            src["member"] = m.name;
            lyt["source"] = src;
            writeFile(out_ / "layouts" / (layout + ".json"), json::dump(lyt) + "\n");
            Value e = Value::object();
            e["name"] = layout;
            e["file"] = "layouts/" + layout + ".json";
            layouts.push(e);
        }
        index["layouts"] = layouts;
        index["missing_textures"] = missing_;
        index["substituted_fonts"] = substituted_;
        // Messages (English, Europe).
        Value messages = Value::array();
        for (const char* group : {"LayoutMessage", "SystemMessage"}) {
            const std::string path = std::string("LocalizedData/EUen/MessageData/") + group + ".szs";
            for (const auto& m : in_.archive(path)) {
                if (!endsWith(m.name, ".msbt")) {
                    continue;
                }
                const std::string n = m.name.substr(0, m.name.size() - 5);
                writeFile(out_ / "messages" / group / (n + ".json"), json::dump(readMsbt(m.data)) + "\n");
                messages.push(std::string(group) + "/" + n);
            }
        }
        index["messages"] = messages;
        // The texture packs the course book draws from (illustrations and course screenshots):
        // CTPK containers of PICA textures (general structure from public format documentation:
        // header, 32-byte entries with name, size, data offset, format, width and height).
        if (name_ == "CourseSelection") {
            Value packs = Value::object();
            for (const char* pack : {"BookIllustrations", "BottomBookIllustrations", "Screenshots"}) {
                const Bytes& b = in_.romfs(std::string("UIX2_Textures/") + pack + "_textures.ctpk");
                if (b.size() < 0x20 || std::memcmp(b.data(), "CTPK", 4) != 0) {
                    throw ImportError("Not a CTPK texture pack");
                }
                const uint32_t count = u16(b, 6), data = u32(b, 8);
                for (uint32_t i = 0; i < count; ++i) {
                    const size_t e = 0x20 + 0x20 * size_t(i);
                    const std::string tex = cstring(b, u32(b, e));
                    const uint32_t size = u32(b, e + 4), offset = u32(b, e + 8), format = u32(b, e + 12);
                    const uint32_t w = u16(b, e + 16), h = u16(b, e + 18);
                    std::string stem = tex.substr(0, tex.rfind('.'));
                    const std::string file = std::string("packs/") + pack + "/" + stem + ".rgba";
                    writeFile(out_ / file, decodePica(slice(b, data + offset, size), w, h, format));
                    Value t = Value::object();
                    t["file"] = file;
                    t["width"] = w;
                    t["height"] = h;
                    t["format"] = format;
                    packs[std::string(pack) + "/" + stem] = t;
                }
            }
            index["packs"] = packs;
        }
        writeFile(out_ / "ui.json", json::dump(index, 1) + "\n");
    }

private:
    Value texture(const std::string& name, const std::vector<SarcMember>& files) {
        if (auto it = textures_.find(name); it != textures_.end()) {
            return it->second;
        }
        const std::string member = "timg/" + name;
        const SarcMember* found = nullptr;
        for (const auto& m : files) {
            if (m.name == member) {
                found = &m;
            }
        }
        if (!found) {
            missing_.push(name);
            return textures_[name] = Value();
        }
        const Flim f = decodeFlim(found->data);
        writeFile(out_ / "layouts" / "textures" / (name + ".rgba"), f.rgba);
        Value t = Value::object();
        t["file"] = "layouts/textures/" + name + ".rgba";
        t["width"] = f.allocWidth;
        t["height"] = f.allocHeight;
        t["image"] = Value::list(std::vector<uint32_t>{f.width, f.height});
        t["format"] = f.format;
        t["rotated"] = bool(f.flags & 4);
        return textures_[name] = t;
    }

    Value font(const std::string& name, const std::vector<SarcMember>& files) {
        if (auto it = fonts_.find(name); it != fonts_.end()) {
            return it->second;
        }
        // Fonts outside the archive: the large text font comes in companion archives per season
        // (UIX2/Eu/<Arc>_RodinFont32_EB+Season<N>.arc; the first season's is used); the console's
        // shared system font (cbf_std) is not on the cartridge and is substituted by the game's
        // RodinFont32_EB (listed in ui.json "substituted_fonts").
        const SarcMember* found = nullptr;
        for (const auto& m : files) {
            if (m.name == "font/" + name) {
                found = &m;
            }
        }
        Bytes companion;
        if (!found) {
            const std::string stem = name.substr(0, name.rfind('.'));
            std::string source =
                "UIX2/Eu/" + (name_ == "LayoutData" ? std::string("Gameplay") : name_) + "_" + stem + "+Season1.arc";
            std::string member = "font/" + name;
            if (name == "cbf_std.bffnt") {
                source = "UIX2/Eu/Gameplay_RodinFont32_EB+Season1.arc";
                member = "font/RodinFont32_EB.bffnt";
                substituted_.push(name + " -> rom:" + source + "/" + member);
            } else if (!Inputs::has(source)) {
                source = "UIX2/Eu/Gameplay_" + stem + "+Season1.arc";
            }
            if (!Inputs::has(source)) {
                missing_.push("font:" + name);
                return fonts_[name] = Value();
            }
            companion = in_.file(source, member);
        }
        const Font f = readFont(found ? found->data : companion);
        const fs::path dst = out_ / "layouts";
        const std::string ext = f.color ? ".rgba" : ".a8";
        for (size_t i = 0; i < f.sheets.size(); ++i) {
            writeFile(dst / "fonts" / (name + "-" + std::to_string(i) + ext), f.sheets[i]);
        }
        const int64_t perRow = f.sheet.at("cells_per_row").integer();
        const int64_t per = perRow * f.sheet.at("cells_per_column").integer();
        const int64_t cw = f.sheet.at("cell_width").integer(), ch = f.sheet.at("cell_height").integer();
        Value glyphs = Value::object();
        for (const auto& [code, index] : f.cmap) {
            auto w = f.widths.count(index) ? f.widths.at(index) : f.defaultWidth;
            const int64_t sheet = index / per, cell = index % per;
            Value g = Value::object();
            g["sheet"] = sheet;
            g["x"] = (cell % perRow) * (cw + 1);
            g["y"] = (cell / perRow) * (ch + 1);
            g["left"] = w.left;
            g["glyph"] = w.glyph;
            g["char"] = w.chr;
            glyphs[std::to_string(code)] = g;
        }
        Value j = Value::object();
        j["name"] = name;
        j["height"] = f.info.at("height");
        j["width"] = f.info.at("width");
        j["ascent"] = f.info.at("ascent");
        j["line_feed"] = f.info.at("line_feed");
        j["default_width"] = f.info.at("default_width");
        j["sheet"] = f.sheet;
        j["sheets"] = Value::array();
        for (size_t i = 0; i < f.sheets.size(); ++i) {
            j["sheets"].push("layouts/fonts/" + name + "-" + std::to_string(i) + ext);
        }
        j["color"] = f.color;
        j["glyphs"] = glyphs;
        writeFile(dst / "fonts" / (name + ".json"), json::dump(j) + "\n");
        Value ref = Value::object();
        ref["file"] = "layouts/fonts/" + name + ".json";
        return fonts_[name] = ref;
    }

    Inputs& in_;
    std::string name_;
    fs::path out_;
    std::map<std::string, Value> textures_, fonts_;
    Value missing_ = Value::array();
    Value substituted_ = Value::array();
};

}   // namespace

void exportUi(Inputs& in, const std::string& archive, const fs::path& out) {
    UiBuilder(in, archive, out).run();
}

fs::path uiDirectory(const fs::path& dataDirectory, const std::string& archive) {
    return dataDirectory / "Ui" / archive;
}

fs::path buildUi(const fs::path& dataDirectory, const fs::path& cacheDirectory, const std::string& archive) {
    const fs::path target = uiDirectory(cacheDirectory, archive);
    {
        std::ifstream f(target / "ui.json");
        std::stringstream s;
        s << f.rdbuf();
        if (f && s.str().find("\"schema\": " + std::to_string(kUiSchema) + ",") != std::string::npos) {
            return target;
        }
    }
    const fs::path staging = cacheDirectory / "Ui" / (".build-" + std::to_string(getpid()));
    fs::remove_all(staging);
    try {
        Inputs in(romDirectory(dataDirectory));
        exportUi(in, archive, staging);
        fs::remove_all(target);
        fs::rename(staging, target);
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
        throw;
    }
    return target;
}

}   // namespace captain_toad
