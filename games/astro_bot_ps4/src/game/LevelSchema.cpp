#include "game/LevelSchema.hpp"

#include "data/Bytes.hpp"
#include "data/Xml.hpp"

#include <cstdlib>

namespace beacon::game {

AttributeType attributeTypeFromName(std::string_view n) {
    if (n == "Float") {
        return AttributeType::Float;
    }
    if (n == "Int") {
        return AttributeType::Int;
    }
    if (n == "Bool") {
        return AttributeType::Bool;
    }
    if (n == "Enum") {
        return AttributeType::Enum;
    }
    if (n == "String") {
        return AttributeType::String;
    }
    if (n == "Link") {
        return AttributeType::Link;
    }
    if (n == "Vector3") {
        return AttributeType::Vector3;
    }
    if (n == "Vector4") {
        return AttributeType::Vector4;
    }
    if (n == "Sequencer") {
        return AttributeType::Sequencer;
    }
    return AttributeType::Unknown;
}

std::optional<int> AttributeSchema::enumValue(std::string_view valueName) const {
    for (const auto& [name, value] : enumValues) {
        if (name == valueName) {
            return value;
        }
    }
    return std::nullopt;
}

const AttributeSchema* LevelObjectSchema::attribute(std::string_view name) const {
    for (const auto& a : attributes) {
        if (a.name == name) {
            return &a;
        }
    }
    return nullptr;
}

namespace {

// "Loop:OneShot:Replay" counts up from 0; "None=0:Parent=5" gives explicit values.
std::vector<std::pair<std::string, int>> parseEnumNames(std::string_view text) {
    std::vector<std::pair<std::string, int>> out;
    int next = 0;
    while (!text.empty()) {
        std::size_t colon = text.find(':');
        std::string_view item = text.substr(0, colon);
        std::size_t eq = item.find('=');
        int value = next;
        if (eq != std::string_view::npos) {
            value = std::atoi(std::string(item.substr(eq + 1)).c_str());
            item = item.substr(0, eq);
        }
        out.emplace_back(std::string(item), value);
        next = value + 1;
        if (colon == std::string_view::npos) {
            break;
        }
        text.remove_prefix(colon + 1);
    }
    return out;
}

}   // namespace

bool LevelSchemaLibrary::open(const std::filesystem::path& root, std::string* error) {
    namespace fs = std::filesystem;
    schemas_.clear();
    const fs::path dir = root / "data" / "multi_platformer" / "lox";
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".lox") {
            continue;
        }
        data::Bytes bytes;
        std::string err;
        std::optional<data::XmlNode> xml;
        if (data::loadFile(entry.path(), bytes)) {
            xml = data::parseXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), &err);
        }
        if (!xml) {
            if (error) {
                *error = entry.path().string() + ": " + (err.empty() ? "cannot read" : err);
            }
            return false;
        }
        LevelObjectSchema s;
        s.className = entry.path().stem().string();
        s.defaultOdx = xml->childText("Odx", "");
        if (const auto* r = xml->child("Resources")) {
            r->forEach("Odx", [&](const data::XmlNode& n) { s.resources.push_back(n.text); });
        }
        if (const auto* c = xml->child("Categories")) {
            c->forEach("Category", [&](const data::XmlNode& n) { s.categories.push_back(n.text); });
        }
        if (const auto* attrs = xml->child("Attributes")) {
            attrs->forEach("Attribute", [&](const data::XmlNode& n) {
                AttributeSchema a;
                a.name = n.childText("Name", "");
                a.type = attributeTypeFromName(n.childText("Type", ""));
                a.defaultValue = n.childText("Value", "");
                a.enumValues = parseEnumNames(n.childText("EnumName", ""));
                a.semantic = n.childText("Semantic", "");
                s.attributes.push_back(std::move(a));
            });
        }
        schemas_[s.className] = std::move(s);
    }
    if (schemas_.empty()) {
        if (error) {
            *error = "no level object schemas in " + dir.string();
        }
        return false;
    }
    return true;
}

const LevelObjectSchema* LevelSchemaLibrary::find(std::string_view className) const {
    auto it = schemas_.find(className);
    return it == schemas_.end() ? nullptr : &it->second;
}

}   // namespace game
