#include "game/LevelDocument.hpp"

#include "data/Bytes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

namespace beacon::game {

namespace {

constexpr float kDegToRad = 3.14159265358979f / 180;

std::optional<data::XmlNode> readXml(const std::filesystem::path& path, std::string* error) {
    data::Bytes bytes;
    if (!data::loadFile(path, bytes)) {
        if (error) {
            *error = path.string() + ": cannot read";
        }
        return std::nullopt;
    }
    std::string err;
    auto xml = data::parseXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), &err);
    if (!xml && error) {
        *error = path.string() + ": " + err;
    }
    return xml;
}

LevelTransform readTransform(const data::XmlNode& n, const char* t, const char* r, const char* s) {
    LevelTransform x;
    if (auto v = n.childText(t)) {
        data::parseFloats(*v, x.translate);
    }
    if (auto v = n.childText(r)) {
        data::parseFloats(*v, x.rotate);
    }
    if (auto v = n.childText(s)) {
        data::parseFloats(*v, x.scale);
    }
    return x;
}

// Elements every placement has, read into LevelObject fields rather than the attribute list.
const std::set<std::string, std::less<>> kCommonElements = {
    "UUID",   "LoxFile",         "Odx",          "MayaNode",    "translate",   "rotate",    "scale",
    "Parent", "ParentTranslate", "ParentRotate", "ParentScale", "ParentJoint", "AsTemplate"};

LevelObject readObject(const data::XmlNode& n) {
    LevelObject o;
    o.uuid = n.childText("UUID", "");
    o.className = n.childText("LoxFile", "");
    o.odx = n.childText("Odx", "");
    o.mayaNode = n.childText("MayaNode", "");
    o.transform = readTransform(n, "translate", "rotate", "scale");
    o.parent = n.childText("Parent", "");
    if (n.child("ParentTranslate")) {
        o.parentTransform = readTransform(n, "ParentTranslate", "ParentRotate", "ParentScale");
    }
    o.parentJoint = n.childText("ParentJoint", "None");
    o.asTemplate = data::parseBool(n.childText("AsTemplate", "false")).value_or(false);
    for (const auto& c : n.children) {
        if (kCommonElements.count(c.name)) {
            continue;
        }
        const std::string* link = c.attribute("link");
        o.attributes.push_back({c.name, c.text, link && data::parseBool(*link).value_or(false)});
    }
    return o;
}

void readDocument(const data::XmlNode& root, Level& level) {
    for (const auto& c : root.children) {
        if (c.name == "MupLevelObject") {
            level.objects.push_back(readObject(c));
        } else if (c.name == "OdxList") {
            c.forEach("Odx", [&](const data::XmlNode& n) { level.odxList.push_back(n.text); });
        } else if (c.name == "Setting") {
            for (const auto& s : c.children) {
                level.settings[s.name] = s.text;
            }
        } else {
            level.otherSections.push_back(c);
        }
    }
}

}   // namespace

// Maya's default rotate order "xyz": rotate about X first, then Y, then Z (column vectors:
// R = Rz * Ry * Rx). World = T * R * S.
math::Mat4 LevelTransform::matrix() const {
    const float cx = std::cos(rotate[0] * kDegToRad), sx = std::sin(rotate[0] * kDegToRad);
    const float cy = std::cos(rotate[1] * kDegToRad), sy = std::sin(rotate[1] * kDegToRad);
    const float cz = std::cos(rotate[2] * kDegToRad), sz = std::sin(rotate[2] * kDegToRad);
    // Columns of Rz * Ry * Rx.
    const float r[9] = {cy * cz,
                        cy * sz,
                        -sy,
                        sx * sy * cz - cx * sz,
                        sx * sy * sz + cx * cz,
                        sx * cy,
                        cx * sy * cz + sx * sz,
                        cx * sy * sz - sx * cz,
                        cx * cy};
    math::Mat4 m{};
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            m[col * 4 + row] = r[col * 3 + row] * scale[col];
        }
    }
    m[12] = translate[0];
    m[13] = translate[1];
    m[14] = translate[2];
    m[15] = 1;
    return m;
}

const LevelAttribute* LevelObject::attribute(std::string_view name) const {
    for (const auto& a : attributes) {
        if (a.name == name) {
            return &a;
        }
    }
    return nullptr;
}

std::string LevelObject::text(std::string_view name, std::string_view fallback) const {
    const LevelAttribute* a = attribute(name);
    return a ? a->value : std::string(fallback);
}

std::optional<float> LevelObject::number(std::string_view name) const {
    const LevelAttribute* a = attribute(name);
    float v = 0;
    if (!a || !data::parseFloatList(a->value, &v, 1)) {
        return std::nullopt;
    }
    return v;
}

std::optional<bool> LevelObject::flag(std::string_view name) const {
    const LevelAttribute* a = attribute(name);
    return a ? data::parseBool(a->value) : std::nullopt;
}

std::vector<LinkTarget> LevelObject::links(std::string_view name) const {
    std::vector<LinkTarget> out;
    const LevelAttribute* a = attribute(name);
    if (!a) {
        return out;
    }
    std::string_view v = a->value;
    while (!v.empty()) {
        std::size_t comma = v.find(',');
        std::string_view item = v.substr(0, comma);
        if (!item.empty() && item != "null") {
            LinkTarget t;
            if (item[0] == ':' && !out.empty()) {
                out.back().port = std::atoi(std::string(item.substr(1)).c_str());   // ":n" after a UUID
            } else {
                t.uuid = std::string(item);
                out.push_back(std::move(t));
            }
        }
        if (comma == std::string_view::npos) {
            break;
        }
        v.remove_prefix(comma + 1);
    }
    return out;
}

bool LevelObject::linkRequiresAll(std::string_view name) const {
    const LevelAttribute* a = attribute(name);
    if (!a) {
        return false;
    }
    std::string_view v = a->value;
    while (!v.empty()) {
        const std::size_t comma = v.find(',');
        const std::string_view item = v.substr(0, comma);
        if (item.size() > 1 && item[0] == ':' && item[1] == '1') {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        v.remove_prefix(comma + 1);
    }
    return false;
}

const LevelObject* Level::find(std::string_view uuid) const {
    auto it = byUuid.find(uuid);
    return it == byUuid.end() ? nullptr : &objects[it->second];
}

std::optional<Level> loadLevel(const std::filesystem::path& root, const std::string& levelName, std::string* error) {
    namespace fs = std::filesystem;
    const fs::path dir = root / "data" / "multi_platformer" / "levels" / levelName / "lvx";
    Level level;
    level.name = levelName;
    bool any = false;
    // The game reads art.lvx and design.lvx (MupLvxDocument, 0x55e9b0); both have the same form.
    for (const char* file : {"design.lvx", "art.lvx"}) {
        std::error_code ec;
        if (!fs::exists(dir / file, ec)) {
            continue;
        }
        auto xml = readXml(dir / file, error);
        if (!xml) {
            return std::nullopt;
        }
        if (xml->name != "MupLevelData") {
            if (error) {
                *error = (dir / file).string() + ": not a level document";
            }
            return std::nullopt;
        }
        readDocument(*xml, level);
        any = true;
    }
    if (!any) {
        if (error) {
            *error = "no level documents in " + dir.string();
        }
        return std::nullopt;
    }
    for (std::size_t i = 0; i < level.objects.size(); ++i) {
        level.byUuid.emplace(level.objects[i].uuid, i);
    }
    return level;
}

std::vector<LevelEntry> listLevels(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    const fs::path base = root / "data" / "multi_platformer";
    std::vector<LevelEntry> out;
    auto listed = [&](const std::string& f) {
        return std::any_of(out.begin(), out.end(), [&](const LevelEntry& e) { return e.file == f; });
    };
    if (auto xml = readXml(base / "mup_product_levels.xml", nullptr)) {
        std::vector<const data::XmlNode*> stack{&*xml};
        while (!stack.empty()) {
            const data::XmlNode* n = stack.back();
            stack.pop_back();
            for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) {
                stack.push_back(&*it);
            }
            if (n->name == "Level") {
                std::string file = n->childText("File", "");
                std::error_code ec;   // the list names a few levels that did not ship
                if (!file.empty() && !listed(file) &&
                    fs::exists(base / "levels" / file / "lvx", ec)) {
                    out.push_back({file, n->childText("Name", "")});
                }
            }
        }
    }
    std::vector<std::string> rest;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(base / "levels", ec)) {
        std::string name = entry.path().filename().string();
        if (fs::exists(entry.path() / "lvx", ec) && !listed(name)) {
            rest.push_back(name);
        }
    }
    std::sort(rest.begin(), rest.end());
    for (auto& r : rest) {
        out.push_back({std::move(r), {}});
    }
    return out;
}

}   // namespace game
