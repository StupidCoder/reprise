// beacon-model: inspects JXMD models (research and verification tool).
//   beacon-model info FILE      tables, draws and materials
//   beacon-model check DIR      builds every .jxm below DIR and reports failures
#include "data/Model.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

using namespace beacon;

namespace {

const char* categoryName(std::uint32_t hash) {
    switch (hash) {
    case data::fnv1a("Opaque"):
        return "Opaque";
    case data::fnv1a("Shadowmap"):
        return "Shadowmap";
    default:
        return nullptr;
    }
}

int info(const char* path) {
    data::Bytes bytes;
    if (!data::loadFile(path, bytes)) {
        std::fprintf(stderr, "%s: cannot read\n", path);
        return 1;
    }
    std::string error;
    auto file = data::JxmFile::parse(bytes, &error);
    if (!file) {
        std::fprintf(stderr, "%s: %s\n", path, error.c_str());
        return 1;
    }
    for (const auto& t : file->tables) {
        std::printf("table %s rows %u stride %u fields %zu\n", t.tag.c_str(), t.rowCount, t.rowStride,
                    t.schema.fields.size());
    }
    if (std::getenv("BEACON_MODEL_STRINGS")) {   // research aid: the string table with name hashes
        for (std::int64_t i = 0;; ++i) {
            const std::string_view str = file->string(i);
            if (str.empty() && i > 0 && file->string(i + 1).empty()) {
                break;
            }
            std::printf("string %lld %08x %.*s\n", (long long)i, data::fnv1a(std::string(str)), int(str.size()),
                        str.data());
        }
    }
    if (const char* tag = std::getenv("BEACON_MODEL_TABLE")) {   // research aid: one table's rows
        if (const data::Table* t = file->table(tag)) {
            for (std::uint32_t r = 0; r < t->rowCount; ++r) {
                std::printf("%s row %u:", tag, r);
                for (const auto& f : t->schema.fields) {
                    std::printf(" %08x=", f.nameHash);
                    for (std::uint32_t e = 0; e < std::max<std::uint32_t>(1, f.count) && e < 16; ++e) {
                        std::printf("%s%g", e ? "," : "", t->number(r, f.nameHash, 0, e));
                    }
                }
                std::printf("\n");
            }
        }
    }
    auto model = data::buildModel(*file, &error);
    if (!model) {
        std::fprintf(stderr, "%s: %s\n", path, error.c_str());
        return 1;
    }
    std::printf("model '%s': %zu vertices, %zu indices, bounds (%g %g %g)-(%g %g %g)\n", model->name.c_str(),
                model->vertices.size(), model->indices.size(), model->boundsMin[0], model->boundsMin[1],
                model->boundsMin[2], model->boundsMax[0], model->boundsMax[1], model->boundsMax[2]);
    for (const auto& d : model->draws) {
        const char* cat = categoryName(d.category);
        std::printf("  draw %u indices from %u, vertex offset %d, material %d, category %s (%08x) at (%g %g %g)\n",
                    d.indexCount, d.firstIndex, d.vertexOffset, d.material, cat ? cat : "?", d.category,
                    d.transform[12], d.transform[13], d.transform[14]);
    }
    auto tex = [](const data::TextureRef& r) {
        static char buf[160];
        if (r.constant) {
            std::snprintf(buf, sizeof buf, "#%08x", r.colorRgba);
        } else {
            std::snprintf(buf, sizeof buf, "%s", r.name.empty() ? "-" : r.name.c_str());
        }
        return std::string(buf);
    };
    for (std::size_t e = 0; e < model->palette.size(); ++e) {
        // Centroid of the vertices on this entry (rigid entries: are the vertices node-local?).
        double c[3] = {0, 0, 0};
        std::size_t n = 0;
        for (const auto& v : model->vertices) {
            if (v.joints[0] == e && v.weights[0] > 0.99f) {
                for (int k = 0; k < 3; ++k) {
                    c[k] += v.position[k];
                }
                ++n;
            }
        }
        std::printf("  palette %zu joint %08x%s: %zu vertices around (%.3f %.3f %.3f)\n", e,
                    model->palette[e].jointHash, model->palette[e].hasBind ? " bind" : "", n, n ? c[0] / n : 0.0,
                    n ? c[1] / n : 0.0, n ? c[2] / n : 0.0);
    }
    for (const auto& m : model->materials) {
        std::printf("  material '%s' template %08x base %s normal %s rough %s metal %s color (%g %g %g)\n",
                    m.name.c_str(), m.templateHash, tex(m.base).c_str(), tex(m.normal).c_str(),
                    tex(m.roughness).c_str(), tex(m.metallic).c_str(), m.color[0], m.color[1], m.color[2]);
        std::printf("    state blend %016llx flags %08x depthOffset %g cull %u zWrite %u zTest %u priority %d\n",
                    (unsigned long long)m.blendState, m.materialFlags, m.depthOffset, m.cullFace, m.zWrite, m.zTest,
                    m.priority);
        std::printf("    emissive %s (%g %g %g) occlusion %s incidence %g..%g^%g alpha x%g >= %g scatter %g%s\n",
                    tex(m.emissive).c_str(), m.emissiveColor[0], m.emissiveColor[1], m.emissiveColor[2],
                    tex(m.occlusion).c_str(), m.incidence[0], m.incidence[1], m.incidence[2], m.alphaScale,
                    m.alphaThreshold, m.fakeScattering, m.unlit ? " unlit" : "");
        if (m.cloud) {
            std::printf("    cloud color %s noise %s:", tex(m.base).c_str(), tex(m.noise).c_str());
            for (std::size_t i = 0; i < m.cloudParams.size(); ++i) {
                std::printf(" %s=%g", data::kCloudFields[i], m.cloudParams[i]);
            }
            std::printf("\n");
        }
        if (!m.baseDetail.name.empty() || !m.normalDetail.name.empty() || !m.roughnessDetail.name.empty() ||
            !m.aoMap.name.empty()) {
            std::printf("    detail base %s normal %s rough %s x%g%s ao map %s\n", tex(m.baseDetail).c_str(),
                        tex(m.normalDetail).c_str(), tex(m.roughnessDetail).c_str(), m.detailScale,
                        m.detailWorldScaling ? " world" : "", tex(m.aoMap).c_str());
        }
    }
    if (std::getenv("BEACON_MODEL_UVS")) {   // research aid: each draw's UV range
        for (const auto& d : model->draws) {
            float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f};
            for (std::uint32_t k = 0; k < d.indexCount; ++k) {
                const auto& v = model->vertices[std::size_t(d.vertexOffset) + model->indices[d.firstIndex + k]];
                for (int c = 0; c < 2; ++c) {
                    lo[c] = std::min(lo[c], v.uv[c]), hi[c] = std::max(hi[c], v.uv[c]);
                }
            }
            std::printf("  draw material %d uv (%.4f %.4f)-(%.4f %.4f)\n", d.material, lo[0], lo[1], hi[0], hi[1]);
        }
    }
    if (std::getenv("BEACON_MODEL_FIELDS")) {
        // Raw template fields (name hash, type code, offset, size, count) for name matching.
        const data::Table* mati = file->table("MATI");
        data::ByteView matd = file->blob("MATD");
        const auto templates = data::materialTemplates(*file);
        for (const auto& [hash, schema] : templates) {
            std::printf("  template %08x:", hash);
            for (const auto& f : schema.fields) {
                std::printf(" %08x(%u@%u)", f.nameHash, f.typeCode, f.offset);
            }
            std::printf("\n");
        }
        for (const auto& m : model->materials) {
            auto it = templates.find(m.templateHash);
            if (it == templates.end()) {
                continue;
            }
            const data::Schema* s = &it->second;
            std::uint32_t row = 0;
            for (; mati && row < mati->rowCount; ++row) {
                if (std::string(file->string(mati->integer(row, data::fnv1a("m_materialName")))) == m.name) {
                    break;
                }
            }
            auto offset = std::size_t(mati->integer(row, data::fnv1a("m_parameterOffset"), 0));
            for (const auto& f : s->fields) {
                std::printf("    field %08x code %u off %u size %u count %u =", f.nameHash, f.typeCode, f.offset,
                            f.size, f.count);
                for (std::uint32_t i = 0; i < f.count * (f.size ? 1 : 0) && i < 4; ++i) {
                    std::size_t at = offset + f.offset + i * f.size;
                    if (f.size == 4 && at + 4 <= matd.size()) {
                        float v;
                        std::memcpy(&v, matd.data() + at, 4);
                        std::printf(" %g/%u", v, data::u32(matd, at));
                    }
                }
                std::printf("\n");
            }
        }
    }
    return 0;
}

}   // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: beacon-model info FILE | check DIR\n");
        return 2;
    }
    std::string cmd = argv[1];
    if (cmd == "info") {
        return info(argv[2]);
    }
    if (cmd == "check") {
        std::map<std::string, int> failures;
        std::map<std::string, std::string> example;
        int ok = 0, empty = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[2])) {
            if (entry.path().extension() != ".jxm") {
                continue;
            }
            data::Bytes bytes;
            data::loadFile(entry.path(), bytes);
            std::string error;
            auto file = data::JxmFile::parse(bytes, &error);
            if (file && !file->table("VBDT")) {
                ++empty;   // material-, light- or skeleton-only file
                continue;
            }
            if (file && data::buildModel(*file, &error)) {
                ++ok;
                continue;
            }
            ++failures[error];
            example.emplace(error, entry.path().string());
        }
        std::printf("models %d, without meshes %d\n", ok, empty);
        for (const auto& [error, n] : failures) {
            std::printf("failed %d: %s (e.g. %s)\n", n, error.c_str(), example[error].c_str());
        }
        return 0;
    }
    return 2;
}
