// A level as authored: the placed objects of data/multi_platformer/levels/<level>/lvx/design.lvx
// (plus art.lvx where present), exported from the studio's Maya level editor as plain XML.
//
//   <MupLevelData>
//     <MupLevelObject>*    one placement: UUID, MayaNode (outliner path), translate / rotate
//                          (degrees) / scale, LoxFile (object class, see LevelSchema.hpp), Odx
//                          (object definition), Parent (UUID of the Maya parent node),
//                          ParentTranslate/Rotate/Scale (that parent's transform when it is a plain
//                          group), ParentJoint, AsTemplate, Suspendable, then the class attributes.
//                          Link attributes carry link="True" and hold UUID lists.
//     <ObjectSets> <LevelObjectTemplates> <OdxList> (definitions to preload) <Setting> (sound
//     banks, player mode) <OrderListTable> <TimerGroupObjectList> [<Cutscene> <UnlockList>
//     <CategorySettings> <CrewBotSettings>]
//
// Placement transforms are world-space (objects under a moved group already include the group's
// offset); the Parent* fields let the game keep children attached when the parent moves. The game
// stores every placement's elements as a generic attribute set ("MayaAttributes") that each object
// class reads by name; this document does the same.
#pragma once

#include "data/Xml.hpp"
#include "math/Transform.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beacon::game {

struct LevelTransform {
    float translate[3] = {0, 0, 0};
    float rotate[3] = {0, 0, 0};   // degrees, Maya rotate order XYZ (verified by rendering, levels.md)
    float scale[3] = {1, 1, 1};

    math::Mat4 matrix() const;
};

// One entry of a Link attribute: the target UUID. (`port` keeps the number of a ":n" entry that
// follows it; the game reads ":1" anywhere in the list as "all targets must be on" for the whole
// link, see LevelObject::linkRequiresAll and LevelRuntime.hpp.)
struct LinkTarget {
    std::string uuid;
    int port = 0;
};

struct LevelAttribute {
    std::string name;
    std::string value;
    bool link = false;
};

struct LevelObject {
    std::string uuid;
    std::string className;   // <LoxFile>
    std::string odx;         // <Odx>, may be empty
    std::string mayaNode;
    LevelTransform transform;
    std::string parent;                              // UUID of the Maya parent node (a placed object or a plain group)
    std::optional<LevelTransform> parentTransform;   // present when the parent is a plain group
    std::string parentJoint;                         // "None" or a joint of the parent's model
    bool asTemplate = false;
    std::vector<LevelAttribute> attributes;   // every element not listed above, in file order

    const LevelAttribute* attribute(std::string_view name) const;
    std::string text(std::string_view name, std::string_view fallback = {}) const;
    std::optional<float> number(std::string_view name) const;
    std::optional<bool> flag(std::string_view name) const;
    std::vector<LinkTarget> links(std::string_view name) const;
    // A ":1" entry anywhere in the link (0x552a00: an entry starting with ':' whose next character
    // is '1').
    bool linkRequiresAll(std::string_view name) const;
};

struct Level {
    std::string name;
    std::vector<LevelObject> objects;
    std::map<std::string, std::size_t, std::less<>> byUuid;
    std::vector<std::string> odxList;              // <OdxList>
    std::map<std::string, std::string> settings;   // <Setting> children
    std::vector<data::XmlNode> otherSections;      // templates, order lists, timer groups, ...

    const LevelObject* find(std::string_view uuid) const;
};

// Loads `levelName` (a folder under data/multi_platformer/levels) from the game folder `root`.
std::optional<Level> loadLevel(const std::filesystem::path& root, const std::string& levelName,
                               std::string* error = nullptr);
struct LevelEntry {
    std::string file;    // folder under data/multi_platformer/levels
    std::string title;   // shipping name from mup_product_levels.xml ("ROOFTOPS"), or empty
};
// Levels in the shipped order (mup_product_levels.xml, levels present on disc only), then the
// remaining level folders by name.
std::vector<LevelEntry> listLevels(const std::filesystem::path& root);

}   // namespace game
