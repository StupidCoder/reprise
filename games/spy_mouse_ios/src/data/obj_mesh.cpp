#include "data/obj_mesh.hpp"

#include <algorithm>
#include <map>
#include <sstream>

namespace doc::data {
namespace {

std::string folderOf(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

// The exporter wrote Windows paths (C:\...\world01_map02.png); the game keeps the file name.
std::string fileNameOf(std::string path) {
    const std::size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string text(const Bytes& bytes) { return std::string(bytes.begin(), bytes.end()); }

// MTL: material name → texture file (map_Kd), or none.
std::map<std::string, std::string> loadMaterials(const GameFiles& files, const std::string& path) {
    std::map<std::string, std::string> materials;
    auto bytes = files.readResource(path);
    if (!bytes) return materials;
    std::istringstream in(text(*bytes));
    std::string line, current;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string key;
        words >> key;
        if (key == "newmtl") {
            words >> current;
            materials[current];
        } else if (key == "map_Kd" && !current.empty()) {
            std::string rest;
            std::getline(words, rest);
            rest.erase(0, rest.find_first_not_of(" \t"));
            while (!rest.empty() && (rest.back() == '\r' || rest.back() == ' ')) rest.pop_back();
            materials[current] = fileNameOf(rest);
        }
    }
    return materials;
}

}  // namespace

std::optional<ObjMesh> loadObjMesh(const GameFiles& files, const std::string& path, float scale) {
    auto bytes = files.readResource(path);
    if (!bytes) return std::nullopt;
    const std::string folder = folderOf(path);
    std::map<std::string, std::string> materials;
    struct V3 {
        float x, y, z;
    };
    std::vector<V3> positions;
    std::vector<std::pair<float, float>> uvs;
    ObjMesh mesh;
    ObjMesh::Part* part = nullptr;
    bool any = false;
    std::istringstream in(text(*bytes));
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream words(line);
        std::string key;
        words >> key;
        if (key == "v") {
            V3 p{};
            words >> p.x >> p.y >> p.z;
            positions.push_back({p.x * scale, p.y * scale, p.z * scale});
        } else if (key == "vt") {
            float u = 0, v = 0;
            words >> u >> v;
            uvs.emplace_back(u, v);
        } else if (key == "mtllib") {
            std::string name;
            words >> name;
            materials = loadMaterials(files, folder + name);
        } else if (key == "usemtl") {
            std::string name;
            words >> name;
            mesh.parts.emplace_back();
            part = &mesh.parts.back();
            auto it = materials.find(name);
            if (it != materials.end() && !it->second.empty()) part->texture = folder + it->second;
        } else if (key == "f") {
            if (!part) {
                mesh.parts.emplace_back();
                part = &mesh.parts.back();
            }
            std::vector<ObjMesh::Corner> corners;
            std::string word;
            while (words >> word) {
                // v/vt/vn, absolute indices from 1 (the loader rejects relative ones).
                int vi = 0, ti = 0;
                std::sscanf(word.c_str(), "%d/%d", &vi, &ti);
                ObjMesh::Corner c;
                if (vi >= 1 && std::size_t(vi) <= positions.size()) {
                    c.x = positions[std::size_t(vi - 1)].x;
                    c.z = positions[std::size_t(vi - 1)].z;
                }
                if (ti >= 1 && std::size_t(ti) <= uvs.size()) {
                    c.u = uvs[std::size_t(ti - 1)].first;
                    c.v = uvs[std::size_t(ti - 1)].second;
                }
                corners.push_back(c);
            }
            for (std::size_t i = 2; i < corners.size(); ++i) {
                part->triangles.push_back(corners[0]);
                part->triangles.push_back(corners[i - 1]);
                part->triangles.push_back(corners[i]);
                for (const ObjMesh::Corner* c : {&corners[0], &corners[i - 1], &corners[i]}) {
                    if (!any) {
                        mesh.minX = mesh.maxX = c->x;
                        mesh.minZ = mesh.maxZ = c->z;
                        any = true;
                    }
                    mesh.minX = std::min(mesh.minX, c->x);
                    mesh.maxX = std::max(mesh.maxX, c->x);
                    mesh.minZ = std::min(mesh.minZ, c->z);
                    mesh.maxZ = std::max(mesh.maxZ, c->z);
                }
            }
        }
    }
    if (!any) return std::nullopt;  // "contains no groups or verts. Load FAILED!"
    return mesh;
}

}  // namespace doc::data
