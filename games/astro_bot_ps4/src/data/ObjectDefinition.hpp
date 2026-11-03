// Object definitions (.odx): the engine's description of a game object's resources, as plain XML
// with one block per component interface:
//   <ObjectDefinition> <Name/> <Size/> (editor bounding size)
//     <IModel>      File (model stem), HasLowResTexture, TextureReplace sets
//     <IAnimation>  Skeleton, Animation (Name, Clip/File, Loop, Speed, ...), Transition, Sequence
//     <IPhysics>    File (.pfb physics bodies), Material
//     <ISound> <IEffect> <IVibration>  kept as XML until those systems are recovered
// Most definitions live in data/multi_platformer/odx.ra; a few are loose files under data/*/odx/.
// Levels and code refer to them by name (the file stem).
#pragma once

#include "data/ResourceArchive.hpp"
#include "data/Xml.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace beacon::data {

struct ObjectAnimation {
    std::string name;                 // logical name used by code and levels ("idle", "play", ...)
    std::vector<std::string> clips;   // clip file stems (.anim)
    bool loop = false;
    float speed = 1;
};

struct ObjectDefinition {
    std::string name;
    float size[3] = {0, 0, 0};
    bool hasSize = false;

    std::string model;   // IModel/File, empty if the object has no model
    bool hasLowResTexture = false;

    std::string skeleton;   // IAnimation/Skeleton
    std::vector<ObjectAnimation> animations;

    std::string physics;           // IPhysics/File (.pfb stem)
    std::string physicsMaterial;   // IPhysics/Material ("Metal", ...)

    XmlNode xml;   // the whole definition, for components not modelled yet

    const ObjectAnimation* animation(std::string_view animName) const;
};

std::optional<ObjectDefinition> parseObjectDefinition(std::string_view text, std::string* error = nullptr);

// All object definitions of the game, by name; parsed on first use.
class ObjectDefinitionLibrary {
public:
    // `root` is the extracted game folder (with data/).
    bool open(const std::filesystem::path& root, std::string* error = nullptr);

    const ObjectDefinition* find(const std::string& name);
    bool contains(const std::string& name) const { return sources_.count(name) != 0; }
    std::size_t size() const { return sources_.size(); }
    std::vector<std::string> names() const;

private:
    struct Source {
        const ResourceArchive::Entry* archived = nullptr;   // or a loose file
        std::filesystem::path file;
    };
    ResourceArchive archive_;
    std::map<std::string, Source> sources_;
    std::map<std::string, std::unique_ptr<ObjectDefinition>> parsed_;   // null = failed to parse
};

}   // namespace data
