#include "data/ObjectDefinition.hpp"

#include <cstdio>

namespace beacon::data {

const ObjectAnimation* ObjectDefinition::animation(std::string_view animName) const {
    for (const auto& a : animations) {
        if (a.name == animName) {
            return &a;
        }
    }
    return nullptr;
}

std::optional<ObjectDefinition> parseObjectDefinition(std::string_view text, std::string* error) {
    auto root = parseXml(text, error);
    if (!root) {
        return std::nullopt;
    }
    if (root->name != "ObjectDefinition") {
        if (error) {
            *error = "root element is <" + root->name + ">, expected <ObjectDefinition>";
        }
        return std::nullopt;
    }
    ObjectDefinition def;
    def.name = root->childText("Name", "");
    if (auto s = root->childText("Size")) {
        def.hasSize = parseFloats(*s, def.size);
    }
    if (const XmlNode* m = root->child("IModel")) {
        def.model = m->childText("File", "");
        def.hasLowResTexture = parseBool(m->childText("HasLowResTexture", "false")).value_or(false);
    }
    if (const XmlNode* a = root->child("IAnimation")) {
        def.skeleton = a->childText("Skeleton", "");
        a->forEach("Animation", [&](const XmlNode& n) {
            ObjectAnimation anim;
            anim.name = n.childText("Name", "");
            n.forEach("Clip", [&](const XmlNode& clip) {
                if (auto f = clip.childText("File")) {
                    anim.clips.push_back(*f);
                }
            });
            anim.loop = parseBool(n.childText("Loop", "false")).value_or(false);
            if (auto s = n.childText("Speed")) {
                parseFloatList(*s, &anim.speed, 1);
            }
            def.animations.push_back(std::move(anim));
        });
    }
    if (const XmlNode* p = root->child("IPhysics")) {
        def.physics = p->childText("File", "");
        def.physicsMaterial = p->childText("Material", "");
    }
    def.xml = std::move(*root);
    return def;
}

bool ObjectDefinitionLibrary::open(const std::filesystem::path& root, std::string* error) {
    namespace fs = std::filesystem;
    sources_.clear();
    parsed_.clear();
    const fs::path archivePath = root / "data" / "multi_platformer" / "odx.ra";
    if (!archive_.open(archivePath, error)) {
        return false;
    }
    for (const auto& e : archive_.entries()) {
        fs::path p(e.path);
        if (p.extension() == ".odx") {
            sources_[p.stem().string()].archived = &e;
        }
    }
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(root / "data", ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".odx") {
            Source& s = sources_[entry.path().stem().string()];
            if (!s.archived) {
                s.file = entry.path();
            }
        }
    }
    return true;
}

const ObjectDefinition* ObjectDefinitionLibrary::find(const std::string& name) {
    if (auto it = parsed_.find(name); it != parsed_.end()) {
        return it->second.get();
    }
    std::unique_ptr<ObjectDefinition>& slot = parsed_[name];
    auto src = sources_.find(name);
    if (src == sources_.end()) {
        return nullptr;
    }
    std::string text;
    if (src->second.archived) {
        ByteView b = archive_.contents(*src->second.archived);
        text.assign(reinterpret_cast<const char*>(b.data()), b.size());
    } else {
        Bytes b;
        if (!loadFile(src->second.file, b)) {
            return nullptr;
        }
        text.assign(reinterpret_cast<const char*>(b.data()), b.size());
    }
    std::string error;
    if (auto def = parseObjectDefinition(text, &error)) {
        slot = std::make_unique<ObjectDefinition>(std::move(*def));
    } else {
        std::fprintf(stderr, "object definition '%s': %s\n", name.c_str(), error.c_str());
    }
    return slot.get();
}

std::vector<std::string> ObjectDefinitionLibrary::names() const {
    std::vector<std::string> out;
    for (const auto& [name, src] : sources_) {
        out.push_back(name);
    }
    return out;
}

}   // namespace data
