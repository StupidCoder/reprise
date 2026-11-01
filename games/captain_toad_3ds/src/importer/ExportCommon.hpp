// Helpers shared by the scene package exporters (the opening's SceneExport and the generic
// StageExport): placement values, BCH model lookups, skinning records and KCL geometry.
#pragma once
#include "Bch.hpp"
#include "Json.hpp"

#include <array>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace captain_toad::exporter {

using json::Value;

// Appends the values as little-endian float32.
void pack(Bytes& out, std::initializer_list<double> values);
// [X, Y, Z] of a placement vector ({"X": .., "Y": .., "Z": ..}).
Value xyz(const Value& d);
// The first list entry matching `pred`; throws when there is none.
const Value& findBy(const Value& list, const std::function<bool(const Value&)>& pred);
// True when the placement's UnitConfigName is `unit`.
bool unitIs(const Value& o, const std::string& unit);
Value strings(std::initializer_list<const char*> items);
Value doubles(std::initializer_list<double> items);
// The BCH skeleton's node names (first model).
Value nodeNames(const Bch& b);
const Model& modelNamed(const std::vector<Model>& models, const std::string& name);
double dot3(const double* a, const double* b);

// One skinned vertex's bone indices and weights (shared by the character, proxy and actor paths).
struct Skin {
    std::vector<uint32_t> indices;
    std::vector<double> weights;
};
Skin skinOf(const Vertex& v, const Face& f, double u3, double tolerance, bool trimToIndices);
// One 72-byte skin record: position, normal, tangent, 4 bone indices, 4 weights, mode.
void packSkin(Bytes& skin, const double pos[3], const double normal[3], const double tangent[3], const Skin& s,
              uint32_t mode);

// KCL collision prisms as world triangles (model space); returns the file's summary. `tolerant`
// skips degenerate prisms (zero-area faces some stage parts contain) instead of failing.
Value kclGeometry(const Bytes& b, std::vector<std::array<std::array<double, 3>, 3>>& triangles, bool tolerant = false);

// One BCH light record (table 5 entry at `q`) as exported to the package.
Value lightRecord(const Bch& b, uint32_t q);

}   // namespace captain_toad::exporter
