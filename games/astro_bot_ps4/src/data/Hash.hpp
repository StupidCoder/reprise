// FNV-1a (32-bit): the engine's name hash. Data files identify chunk types, fields, material
// templates, render categories and type names ("float", "uint32_t", ...) by the hash of their
// source identifier, so code can refer to them by name: fnv1a("m_vertexFormat").
#pragma once

#include <cstdint>
#include <string_view>

namespace beacon::data {

constexpr std::uint32_t fnv1a(std::string_view text) {
    std::uint32_t hash = 0x811c9dc5u;
    for (char c : text) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 0x01000193u;
    }
    return hash;
}

static_assert(fnv1a("float") == 0xa6c45d85u, "matches the type hash stored in model schemas");

}   // namespace data
