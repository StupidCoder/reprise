// Yaz0 decompression, SARC archives and BYML v2 documents (general formats).
#pragma once
#include "Binary.hpp"
#include "Json.hpp"

#include <string>
#include <vector>

namespace captain_toad {

// Returns the input unchanged when it is not Yaz0-compressed.
Bytes yaz0(const Bytes& raw);

struct SarcMember {
    std::string name;
    Bytes data;
};
std::vector<SarcMember> sarc(const Bytes& raw);
// Yaz0 + SARC member by name.
const Bytes& member(const std::vector<SarcMember>& archive, const std::string& name);

// Little-endian BYML v2 subset (dict, array, string, bool, int, float, uint, null).
json::Value byml(const Bytes& raw);

}   // namespace captain_toad
