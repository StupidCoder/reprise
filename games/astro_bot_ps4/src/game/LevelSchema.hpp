// Level object schemas (data/multi_platformer/lox/<class>.lox): one per kind of placeable level
// object. The level editor (a Maya plug-in) used them to offer typed attributes; the game uses the
// same names to read each placed object's attributes. A schema gives:
//   Odx         default object definition when the placement names none (hypothesis, see levels.md)
//   Resources   object definitions the class needs loaded (its effects, debris, ...)
//   Categories  editor grouping ("Terrain", "Moving", ...); Primitive/Color: editor display
//   Attributes  Name, Type (Float Int Bool Enum String Link Vector3 Vector4 Sequencer), default
//               Value, EnumName ("A:B:C" or "A=0:B=5") and an optional Semantic (Animation, ODX...)
// The class name (file stem) is what a placement's <LoxFile> names and what the game's object
// factory is keyed on (264 registered names, .analysis/evidence/lox_registry.tsv).
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace beacon::game {

enum class AttributeType { Float, Int, Bool, Enum, String, Link, Vector3, Vector4, Sequencer, Unknown };

struct AttributeSchema {
    std::string name;
    AttributeType type = AttributeType::Unknown;
    std::string defaultValue;                              // as written in the schema (enums: the index)
    std::vector<std::pair<std::string, int>> enumValues;   // name -> value
    std::string semantic;

    // Enum value for a name as written in a level ("OneShot"); nullopt if unknown.
    std::optional<int> enumValue(std::string_view valueName) const;
};

struct LevelObjectSchema {
    std::string className;
    std::string defaultOdx;
    std::vector<std::string> resources;
    std::vector<std::string> categories;
    std::vector<AttributeSchema> attributes;

    const AttributeSchema* attribute(std::string_view name) const;
};

class LevelSchemaLibrary {
public:
    // Reads every .lox in data/multi_platformer/lox of the game folder `root`.
    bool open(const std::filesystem::path& root, std::string* error = nullptr);
    const LevelObjectSchema* find(std::string_view className) const;
    const std::map<std::string, LevelObjectSchema, std::less<>>& all() const { return schemas_; }

private:
    std::map<std::string, LevelObjectSchema, std::less<>> schemas_;
};

AttributeType attributeTypeFromName(std::string_view name);

}   // namespace game
