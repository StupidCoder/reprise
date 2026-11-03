// Minimal XML reader for the game's plain-text definition files (levels, object definitions,
// level object schemas, tuning). These files use elements, attributes, text, comments and the
// five predefined entities only, so a small tree parser is enough.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace beacon::data {

struct XmlNode {
    std::string name;
    std::string text;   // character data directly inside the element (trimmed)
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<XmlNode> children;

    const XmlNode* child(std::string_view childName) const;
    // Text of the named child, or nullopt if there is no such child.
    std::optional<std::string> childText(std::string_view childName) const;
    std::string childText(std::string_view childName, std::string_view fallback) const;
    const std::string* attribute(std::string_view attributeName) const;

    // Children with the given name, in document order.
    template <typename F>
    void forEach(std::string_view childName, F&& f) const {
        for (const auto& c : children) {
            if (c.name == childName) {
                f(c);
            }
        }
    }
};

// Parses a document and returns its root element; nullopt (with a message) on malformed input.
std::optional<XmlNode> parseXml(std::string_view text, std::string* error = nullptr);

// Whitespace-separated floats ("1.0 0.5 -2"); missing values stay at their defaults.
template <std::size_t N>
bool parseFloats(std::string_view text, float (&out)[N]);
bool parseFloatList(std::string_view text, float* out, std::size_t count);

template <std::size_t N>
bool parseFloats(std::string_view text, float (&out)[N]) {
    return parseFloatList(text, out, N);
}

// "true"/"false" (any case) and "1"/"0".
std::optional<bool> parseBool(std::string_view text);

}   // namespace data
