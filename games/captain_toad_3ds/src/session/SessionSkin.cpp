// GPU skinning of the session's animated models (Toggle::GpuSkinning; render/GpuSkin.hpp).
//
// Switching on: every model with a skin (the protagonist's two forms and the actors) gets a range
// of palette entries (its bones' skinning matrices, then the bones themselves for rigid
// vertices), its vertices get their palette indices and weights in a stream of their own, and the
// vertex buffer gets the skin's bind data (the CPU no longer writes posed vertices). Each drawn
// frame, before drawing, the palette entries of the models posed since are written (placement x
// matrix) and the palette texture is uploaded once. Skinned meshes are drawn with skinned variants
// of their programs: the material programs, the shadow casters and the silhouette.
#include "session/SessionState.hpp"

#include "render/GpuSkin.hpp"

#include <stdexcept>

namespace captain_toad::session {

std::vector<CharacterAnimation*> Session::Impl::skinnedModels() {
    std::vector<CharacterAnimation*> out;
    for (CharacterAnimation* a : {&character, &characterAlt}) {
        if (!a->skin.empty()) {
            out.push_back(a);
        }
    }
    for (auto& a : actors) {
        if (!a.skin.empty()) {
            out.push_back(&a);
        }
    }
    return out;
}

void Session::Impl::setGpuSkinning(bool on) {
    if (on == gpuSkinning) {
        return;
    }
    gpuSkinning = on;
    const std::vector<CharacterAnimation*> models = skinnedModels();
    if (!on) {   // back to the CPU: every model writes its posed vertices with the next frame
        for (CharacterAnimation* a : models) {
            a->gpuSkinned = false;
            a->skinPending = true;
        }
        skinRanges.clear();
        meshSkinned.clear();
        return;
    }
    int entries = 0;
    skinRanges.clear();
    for (CharacterAnimation* a : models) {
        SkinRange r;
        r.base = entries;
        r.bones = int(a->bones.size());
        entries += 2 * r.bones;
        skinRanges[a->first] = r;
    }
    // Palette indices and weights per vertex (zero outside the models).
    std::vector<float> stream(vertices.size() * 8, 0.0f);
    for (CharacterAnimation* a : models) {
        const SkinRange& r = skinRanges.at(a->first);
        for (size_t i = 0; i < a->skin.size(); i++) {
            const SkinVertex& v = a->skin[i];
            float* out = &stream.at((size_t(a->first) + i) * 8);
            for (int j = 0; j < 4; j++) {
                if (v.weights[j] != 0 && v.bones[j] >= uint32_t(r.bones)) {
                    throw std::runtime_error("skin bone index beyond the skeleton");
                }
                out[j] = float(r.base + (v.mode == 1 ? 0 : r.bones) + int(v.bones[j] % uint32_t(std::max(r.bones, 1))));
                out[4 + j] = v.weights[j];
            }
        }
        a->writeBindData(vertices, tangents);
        a->gpuSkinned = true;
        if (vertexBuffer) {
            vertexBuffer->update(size_t(a->first) * sizeof(Vertex), &vertices[size_t(a->first)],
                                 a->skin.size() * sizeof(Vertex));
            tangentBuffer->update(size_t(a->first) * 3 * sizeof(float), &tangents[size_t(a->first) * 3],
                                  a->skin.size() * 3 * sizeof(float));
        }
    }
    captain_toad::gpu::Device& device = captain_toad::gpu::device();
    skinBuffer = device.createBuffer(stream.size() * sizeof(float), stream.data());
    const int rows =
        std::max(1, (entries + captain_toad::gpuskin::kEntriesPerRow - 1) / captain_toad::gpuskin::kEntriesPerRow);
    skinPalette.assign(size_t(captain_toad::gpuskin::kEntriesPerRow) * 3 * size_t(rows) * 4, 0.0f);
    skinBones = device.createTexture(captain_toad::gpu::Format::RGBA32F, captain_toad::gpuskin::kEntriesPerRow * 3,
                                     rows, skinPalette.data());
    meshSkinned.assign(meshes.size(), 0);
    for (size_t i = 0; i < meshes.size(); i++) {
        for (CharacterAnimation* a : models) {
            if (meshes[i].first >= a->first && meshes[i].first + meshes[i].count <= a->first + int(a->skin.size())) {
                meshSkinned[i] = 1;
            }
        }
    }
    // The skinned programs now rather than at a mesh's first draw (compiling one stalls a frame).
    for (const Mesh& m : meshes) {
        if (skinnedMesh(m) && m.material >= 0) {
            meshProgram(m);
        }
    }
    skinnedShared("shadow caster", ShadowMap::kCasterVertex, ShadowMap::kCasterFragment);
}

void Session::Impl::updateSkinPalette() {
    if (!gpuSkinning || !skinBones) {
        return;
    }
    bool changed = false;
    auto put = [&](int entry, const Affine& m) {
        const size_t at =
            (size_t(entry / captain_toad::gpuskin::kEntriesPerRow) * captain_toad::gpuskin::kEntriesPerRow * 3 +
             size_t(entry % captain_toad::gpuskin::kEntriesPerRow) * 3) *
            4;
        std::copy(m.m.begin(), m.m.end(), skinPalette.begin() + std::ptrdiff_t(at));
    };
    for (CharacterAnimation* a : skinnedModels()) {
        auto it = skinRanges.find(a->first);
        if (it == skinRanges.end()) {
            continue;
        }
        SkinRange& r = it->second;
        const auto& skinned = a->skinMatrices();
        const auto& global = a->skinGlobals();
        if ((r.version == a->poseVersion && r.placement.m == a->placement.m) || int(skinned.size()) < r.bones ||
            int(global.size()) < r.bones) {
            continue;
        }
        for (int b = 0; b < r.bones; b++) {
            put(r.base + b, Affine::multiply(a->placement, skinned[size_t(b)]));
            put(r.base + r.bones + b, Affine::multiply(a->placement, global[size_t(b)]));
        }
        r.version = a->poseVersion;
        r.placement = a->placement;
        changed = true;
    }
    if (changed) {
        skinBones->upload(skinPalette.data());
    }
}

bool Session::Impl::skinnedMesh(const Mesh& m) const {
    if (!gpuSkinning || meshes.empty() || &m < meshes.data() || &m >= meshes.data() + meshes.size()) {
        return false;
    }
    const size_t i = size_t(&m - meshes.data());
    return i < meshSkinned.size() && meshSkinned[i];
}

captain_toad::gpu::Program& Session::Impl::meshProgram(const Mesh& m) {
    if (!skinnedMesh(m)) {
        return *programs.at(size_t(m.material));
    }
    skinnedPrograms.resize(programs.size());
    std::unique_ptr<captain_toad::gpu::Program>& p = skinnedPrograms.at(size_t(m.material));
    if (!p) {
        p = captain_toad::gpu::device().createProgram(captain_toad::gpuskin::skinnedVertexSource(materialVertexSource),
                                                      materialFragments.at(size_t(m.material)),
                                                      "material " + std::to_string(m.material) + " skinned");
    }
    return *p;
}

captain_toad::gpu::Program& Session::Impl::skinnedShared(const std::string& name, std::string_view vertex,
                                                         std::string_view fragment) {
    auto it = skinnedSharedPrograms.find(name);
    if (it == skinnedSharedPrograms.end()) {
        it = skinnedSharedPrograms
                 .emplace(name, &captain_toad::gpu::device().sharedProgram(
                                    name + " skinned", captain_toad::gpuskin::skinnedVertexSource(vertex), fragment))
                 .first;
    }
    return *it->second;
}

void Session::Impl::addSkin(captain_toad::gpu::Draw& d) const {
    d.streams.push_back(
        {skinBuffer.get(),
         0,
         32,
         {{captain_toad::gpu::Attribute::Joints, 4, 0}, {captain_toad::gpu::Attribute::Weights, 4, 16}}});
    d.textures[6] = {skinBones.get(),
                     {captain_toad::gpu::Wrap::ClampToEdge, captain_toad::gpu::Wrap::ClampToEdge,
                      captain_toad::gpu::Filter::Nearest, captain_toad::gpu::Filter::Nearest}};
    d.samplers.push_back({"skinBones", 6});
}

}   // namespace captain_toad::session
