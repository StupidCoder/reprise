// See ExportCommon.hpp.
#include "ExportCommon.hpp"

#include <cmath>

namespace captain_toad::exporter {

void pack(Bytes& out, std::initializer_list<double> values) {
    for (double v : values) {
        appendFloats(out, {float(v)});
    }
}

Value xyz(const Value& d) {
    Value v = Value::array();
    for (const char* k : {"X", "Y", "Z"}) {
        v.push(d.at(k));
    }
    return v;
}

const Value& findBy(const Value& list, const std::function<bool(const Value&)>& pred) {
    for (const auto& x : list.items()) {
        if (pred(x)) {
            return x;
        }
    }
    throw ImportError("Expected map entry is missing");
}

bool unitIs(const Value& o, const std::string& unit) {
    const Value* u = o.find("UnitConfigName");
    return u && u->isString() && u->str() == unit;
}

Value strings(std::initializer_list<const char*> items) {
    Value v = Value::array();
    for (const char* s : items) {
        v.push(s);
    }
    return v;
}

Value doubles(std::initializer_list<double> items) {
    Value v = Value::array();
    for (double x : items) {
        v.push(x);
    }
    return v;
}

Value nodeNames(const Bch& b) {
    const uint32_t p = b.table(0)[0], nn = b.u32(p + 136), np = b.u32(p + 140);
    Value names = Value::array();
    for (uint32_t i = 0; i < nn; ++i) {
        names.push(b.nameOf(np + 12 * (i + 1) + 8));
    }
    return names;
}

const Model& modelNamed(const std::vector<Model>& models, const std::string& name) {
    for (const auto& m : models) {
        if (m.name == name) {
            return m;
        }
    }
    throw ImportError("Missing model " + name);
}

double dot3(const double* a, const double* b) {
    return ((0 + a[0] * b[0]) + a[1] * b[1]) + a[2] * b[2];
}

Skin skinOf(const Vertex& v, const Face& f, double u3, double tolerance, bool trimToIndices) {
    Skin s;
    if (f.skin == 1) {
        if (v.has(8)) {
            for (int k = 0; k < v.n[8]; ++k) {
                s.weights.push_back(v.v[8][size_t(k)] * u3);
            }
        } else {
            s.weights.push_back((u3 != 0 ? 1 / u3 : 1.0) * u3);
        }
        if (s.weights.size() > f.palette.size()) {
            s.weights.resize(f.palette.size());
        }
    } else {
        s.weights.push_back(1.0);
    }
    std::vector<double> source;
    if (v.has(7)) {
        for (int k = 0; k < v.n[7]; ++k) {
            source.push_back(v.v[7][size_t(k)]);
        }
    } else {
        for (size_t k = 0; k < f.palette.size(); ++k) {
            source.push_back(double(k));
        }
    }
    for (size_t k = 0; k < source.size() && k < s.weights.size(); ++k) {
        s.indices.push_back(f.palette.at(size_t(int(source[k]))));
    }
    if (trimToIndices && s.weights.size() > s.indices.size()) {
        s.weights.resize(s.indices.size());
    }
    if (f.skin != 1 && f.palette.empty()) {
        // An unskinned face without a bone palette (the sign's arrows): rigid on bone 0.
        s.indices = {0};
        s.weights = {1.0};
    }
    double sum = 0;
    for (double w : s.weights) {
        sum += w;
    }
    if (s.indices.size() != s.weights.size() || s.weights.size() > 4 || std::abs(sum - 1) >= tolerance) {
        throw ImportError("Unexpected skin weights");
    }
    return s;
}

void packSkin(Bytes& skin, const double pos[3], const double normal[3], const double tangent[3], const Skin& s,
              uint32_t mode) {
    pack(skin, {pos[0], pos[1], pos[2], normal[0], normal[1], normal[2], tangent[0], tangent[1], tangent[2]});
    for (size_t k = 0; k < 4; ++k) {
        appendU32(skin, k < s.indices.size() ? s.indices[k] : 0);
    }
    for (size_t k = 0; k < 4; ++k) {
        pack(skin, {k < s.weights.size() ? s.weights[k] : 0.0});
    }
    appendU32(skin, mode);
}

Value kclGeometry(const Bytes& b, std::vector<std::array<std::array<double, 3>, 3>>& triangles, bool tolerant) {
    const uint32_t version = u32(b, 0), table = u32(b, 8), count = u32(b, 12);
    if (version != 0x202 || count != 1) {
        throw ImportError("Unsupported KCL layout");
    }
    const uint32_t base = u32(b, table);
    const uint32_t pos = u32(b, base), norm = u32(b, base + 4), prism = u32(b, base + 8), tree = u32(b, base + 12);
    if ((norm - pos) % 12 || (prism - norm) % 12 || (tree - prism) % 20) {
        throw ImportError("KCL section sizes");
    }
    auto vec = [&](size_t at) {
        return std::array<double, 3>{double(f32(b, at)), double(f32(b, at + 4)), double(f32(b, at + 8))};
    };
    std::vector<std::array<double, 3>> positions, normals;
    for (uint32_t i = pos; i < norm; i += 12) {
        positions.push_back(vec(base + i));
    }
    for (uint32_t i = norm; i < prism; i += 12) {
        normals.push_back(vec(base + i));
    }
    double maxError = 0;
    bool anyError = false;
    size_t degenerate = 0;
    auto cross = [](const std::array<double, 3>& a, const std::array<double, 3>& c) {
        return std::array<double, 3>{a[1] * c[2] - a[2] * c[1], a[2] * c[0] - a[0] * c[2], a[0] * c[1] - a[1] * c[0]};
    };
    for (uint32_t off = prism; off < tree; off += 20) {
        const size_t at = base + off;
        const double length = f32(b, at);
        const auto& origin = positions.at(u16(b, at + 4));
        const auto& n = normals.at(u16(b, at + 6));
        const std::array<double, 3> edges[3] = {normals.at(u16(b, at + 8)), normals.at(u16(b, at + 10)),
                                                normals.at(u16(b, at + 12))};
        const auto d1 = cross(edges[0], n), d2 = cross(edges[1], n);
        const double den1 = dot3(d1.data(), edges[2].data()), den2 = dot3(d2.data(), edges[2].data());
        if (std::min(std::abs(den1), std::abs(den2)) < 1e-9) {
            if (!tolerant) {
                throw ImportError("Degenerate collision prism");
            }
            ++degenerate;
            continue;
        }
        std::array<std::array<double, 3>, 3> tri;
        tri[0] = origin;
        for (int k = 0; k < 3; ++k) {
            tri[1][size_t(k)] = origin[size_t(k)] + d1[size_t(k)] * length / den1;
            tri[2][size_t(k)] = origin[size_t(k)] + d2[size_t(k)] * length / den2;
        }
        for (int j = 0; j < 2; ++j) {
            const auto& v = tri[size_t(j + 1)];
            const double delta[3] = {v[0] - origin[0], v[1] - origin[1], v[2] - origin[2]};
            for (double e : {std::abs(dot3(delta, n.data())), std::abs(dot3(delta, edges[j].data())),
                             std::abs(dot3(delta, edges[2].data()) - length)}) {
                if (e > maxError) {
                    maxError = e;
                    anyError = true;
                }
            }
        }
        triangles.push_back(tri);
    }
    if (triangles.size() + degenerate != u32(b, 52)) {
        throw ImportError("Global prism count mismatch");
    }
    Value kcl = Value::object();
    kcl["version"] = version;
    kcl["model_offset"] = base;
    kcl["positions"] = positions.size();
    kcl["normals"] = normals.size();
    kcl["prisms"] = triangles.size();
    kcl["maximum_plane_residual"] = anyError ? Value(maxError) : Value(0);
    kcl["bounds"] = Value::array();
    kcl["bounds"].push(doubles({f32(b, 16), f32(b, 20), f32(b, 24)}));
    kcl["bounds"].push(doubles({f32(b, 28), f32(b, 32), f32(b, 36)}));
    kcl["status"] = "Interpreted geometry; collision queries and octree traversal are not recovered.";
    return kcl;
}

Value lightRecord(const Bch& b, uint32_t q) {
    const uint32_t flags = b.u32(q + 40);
    auto bytes4 = [&](size_t at) {
        Value v = Value::array();
        for (int k = 0; k < 4; ++k) {
            v.push(b.u8(at + size_t(k)));
        }
        return v;
    };
    auto f3 = [&](size_t at) {
        return doubles({b.f32(at), b.f32(at + 4), b.f32(at + 8)});
    };
    Value r = Value::object();
    r["name"] = b.nameOf(q);
    if ((flags & 15) < 9) {   // ambient/hemisphere/vertex records: only the colour block applies
        r["flags"] = flags;
        r["type"] = flags & 15;
        r["ambient"] = bytes4(q + 48);
        Value zero = Value::list(std::vector<int>{0, 0, 0, 0});
        r["diffuse"] = zero;
        r["specular0"] = zero;
        r["specular1"] = zero;
        r["direction"] = Value::list(std::vector<int>{0, 0, 0});
        r["source_offset"] = q;
        return r;
    }
    r["translation"] = f3(q + 28);
    r["rotation"] = f3(q + 16);
    r["flags"] = flags;
    r["type"] = flags & 15;
    r["two_sided"] = bool(flags & 0x10000);
    r["distance"] = bool(flags & 0x20000);
    r["angle_input"] = (flags >> 24) & 15;
    r["angle_scale"] = (flags >> 28) & 15;
    r["ambient"] = bytes4(q + 48);
    r["diffuse"] = bytes4(q + 52);
    r["specular0"] = bytes4(q + 56);
    r["specular1"] = bytes4(q + 60);
    r["direction"] = f3(q + 64);
    r["attenuation_start"] = b.f32(q + 84);
    r["attenuation_end"] = b.f32(q + 88);
    Value samplers = Value::array();
    for (int i = 0; i < 4; ++i) {
        samplers.push(b.u32(q + 92 + 4 * size_t(i)) ? Value(b.nameOf(q + 92 + 4 * size_t(i))) : Value());
    }
    r["samplers"] = samplers;
    r["source_offset"] = q;
    return r;
}

}   // namespace captain_toad::exporter
