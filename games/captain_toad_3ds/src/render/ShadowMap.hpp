// =============================================================================================
// ShadowMap.hpp - the directional shadow map
// =============================================================================================
//
// The scene is lit by a main directional light that casts shadows. The original renders a depth
// map from the light's direction using coarse shadow-only proxy meshes (the ROM's "_shd" models)
// inside a fixed box per map (ShadowBounds). This port can do the same (proxyCasters = true) or,
// as an enhancement, use all visible geometry with its alpha tests. Resolution and filtering
// deliberately exceed the console's baseline, at the user's request.
//
// Drawn at the display's full rate, the map would be redrawn every frame while anything moves.
// With the fixed box (ShadowBounds) and the visible-geometry casters it keeps the depth of what
// does not move in a second map, redrawn only when that part changes (a mesh shown, hidden,
// starting or stopping to move, the light, the size); each build copies it and adds the meshes
// whose vertices changed lately (the protagonist, swaying grass, animated objects). Depth testing
// keeps the nearest of both, as one pass would.
// =============================================================================================
#pragma once
#include "gpu/Device.hpp"
#include "render/SceneTypes.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>

struct Vec3 {
    double x, y, z;
};

static double dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static Vec3 normalized(Vec3 v) {
    double n = sqrt(dot(v, v));
    if (n < 1e-9) {
        throw std::runtime_error("Invalid light direction");
    }
    return {v.x / n, v.y / n, v.z / n};
}

struct ShadowMap {
    std::unique_ptr<captain_toad::gpu::Target> target;   // depth only
    int size = 0;
    std::array<float, 16> matrix{};   // world -> shadow clip space
    std::vector<float> casters;       // shadow proxy triangles (world space)
    double depthSpan = 1;
    std::vector<Vec3> bounds;    // original ShadowBounds corners
    double biasWorld = 0;        // original depth bias (world units)
    bool proxyCasters = false;   // original _shd proxies (true) or visible geometry (enhancement)

    bool cacheStatic = true;   // keep the unmoving casters' depth between builds (see above)
    // Which casters move: a mesh whose vertices changed within the last kMovingBuilds builds is
    // drawn every build, any other one is kept in the cached map (a change in that set redraws
    // the cache once). The scene reports vertex changes with changed().
    static constexpr uint32_t kMovingBuilds = 60;
    // A placed object's baked mesh drawn away from its placement (a lift that has risen): the
    // column-major matrix that moves it there (false: drawn where its vertices are). The scene
    // also reports such a move with changed().
    std::function<bool(const Mesh&, float[16])> meshMove;
    // GPU skinning: swaps a skinned mesh's program for its skinned variant and adds the skin's
    // stream and palette to its draw (`caster`: the proxy casters' program, else its material's).
    std::function<void(const Mesh&, captain_toad::gpu::Draw&, captain_toad::gpu::Program*&, bool caster)> skin;
    static constexpr const char* kCasterVertex =
        "in vec3 inPosition;uniform mat4 modelViewProjectionMatrix;\n"
        "void main(){gl_Position=modelViewProjectionMatrix*vec4(inPosition,1.0);}";
    static constexpr const char* kCasterFragment = "void main(){fragColor=vec4(1.0);}";
    void changed(size_t mesh) {
        if (mesh >= changedAt_.size()) {
            changedAt_.resize(mesh + 1, 0);
        }
        changedAt_[mesh] = builds_ + 1;
    }

    // The map's depth texture (nullptr before the first build).
    const captain_toad::gpu::Texture* depth() const { return target ? target->depth() : nullptr; }

    // Renders the map. `vertices` holds the scene's vertex buffer (`terrain` its CPU copy, for the
    // bounds), `materials` the scene's programs and `textures` its textures.
    void build(int resolution, const float* towardLight, const std::vector<Vertex>& terrain,
               const captain_toad::gpu::Buffer& vertices, const std::vector<Mesh>& meshList,
               const std::vector<std::unique_ptr<captain_toad::gpu::Program>>& materials,
               const std::vector<std::unique_ptr<captain_toad::gpu::Texture>>& textures,
               const captain_toad::gpu::Texture& white) {
        captain_toad::gpu::Device& device = captain_toad::gpu::device();
        if (resolution < 1 || resolution > 16384) {
            throw std::runtime_error("Unsupported shadow resolution");
        }
        if (!target || size != resolution) {
            target = device.createTarget(resolution, resolution, true);
        }
        size = resolution;

        // Light space: z toward the light, x / y across.
        Vec3 z = normalized({towardLight[0], towardLight[1], towardLight[2]});
        Vec3 x = normalized(cross(fabs(z.y) > .99 ? Vec3{0, 0, 1} : Vec3{0, 1, 0}, z));
        Vec3 y = cross(z, x);

        // Light-space bounds of what the map must cover.
        std::array<double, 3> lo = {1e30, 1e30, 1e30};
        std::array<double, 3> hi = {-1e30, -1e30, -1e30};
        auto include = [&](Vec3 p) {
            double q[] = {dot(p, x), dot(p, y), dot(p, z)};
            for (int k = 0; k < 3; k++) {
                lo[k] = std::min(lo[k], q[k]);
                hi[k] = std::max(hi[k], q[k]);
            }
        };
        if (!bounds.empty()) {
            // Original clip volume: the map's ShadowBounds box, without padding.
            for (auto p : bounds) {
                include(p);
            }
        } else {
            for (size_t i = 0; i < casters.size(); i += 3) {
                include({casters[i], casters[i + 1], casters[i + 2]});
            }
            for (auto m : meshList) {
                if (!m.sky) {
                    for (int i = m.first; i < m.first + m.count; i++) {
                        include({terrain[i].p[0], terrain[i].p[1], terrain[i].p[2]});
                    }
                }
            }
            for (int k = 0; k < 3; k++) {
                double padding = std::max(10., (hi[k] - lo[k]) * .025);
                lo[k] -= padding;
                hi[k] += padding;
            }
        }
        depthSpan = hi[2] - lo[2];

        // Rows map world positions directly into orthographic clip coordinates. The depth toward
        // the light is reversed: closest to the light has depth zero.
        Vec3 rows[] = {x, y, z};
        for (int r = 0; r < 3; r++) {
            double scale = (r == 2 ? -2. : 2.) / (hi[r] - lo[r]);
            matrix[r] = rows[r].x * scale;
            matrix[4 + r] = rows[r].y * scale;
            matrix[8 + r] = rows[r].z * scale;
            matrix[12 + r] = (r == 2 ? 1. : -1.) * (hi[r] + lo[r]) / (hi[r] - lo[r]);
        }
        matrix[15] = 1;

        captain_toad::gpu::Target* const frameTarget = device.target();
        // The cached part: the casters that do not move (visible-geometry path, fixed box).
        const bool split = cacheStatic && !proxyCasters && !bounds.empty() && !getenv("DIORAMA_NO_SHADOW_CACHE");
        ++builds_;
        auto moving = [&](const Mesh& m) {
            const size_t i = size_t(&m - meshList.data());
            return i < changedAt_.size() && changedAt_[i] != 0 && builds_ - changedAt_[i] < kMovingBuilds;
        };
        if (split) {
            uint64_t key = 1469598103934665603ull;   // FNV-1a over what the static part shows
            auto mixIn = [&](uint64_t v) {
                key = (key ^ v) * 1099511628211ull;
            };
            mixIn(uint64_t(size));
            for (float v : matrix) {
                uint32_t bits;
                std::memcpy(&bits, &v, 4);
                mixIn(bits);
            }
            for (size_t i = 0; i < meshList.size(); i++) {
                const Mesh& m = meshList[i];
                if (!moving(m) && casts(m)) {
                    mixIn(i + 1);
                }
            }
            if (!staticTarget_ || staticTarget_->width() != size || key != staticKey_) {
                if (!staticTarget_ || staticTarget_->width() != size) {
                    staticTarget_ = device.createTarget(size, size, true);
                }
                device.bindTarget(staticTarget_.get());
                device.setViewport({0, 0, size, size});
                captain_toad::gpu::Clear clear;
                clear.depth = true;
                device.clear(clear);
                drawCasters(vertices, meshList, materials, textures, white, [&](const Mesh& m) { return !moving(m); });
                staticKey_ = key;
            }
        } else {
            staticTarget_.reset();
        }
        device.bindTarget(target.get());
        device.setViewport({0, 0, size, size});
        captain_toad::gpu::Clear clear;
        clear.depth = true;
        device.clear(clear);
        if (split) {
            if (!device.copyDepth(*staticTarget_, *target)) {   // a backend without a depth copy
                copyDepth(*staticTarget_->depth());
            }
            drawCasters(vertices, meshList, materials, textures, white, moving);
            device.bindTarget(frameTarget);
            return;
        }
        captain_toad::gpu::RenderState state;
        state.depth = {true, true, captain_toad::gpu::Compare::Less};
        captain_toad::gpu::VertexStream positions{
            &vertices, 0, int(sizeof(Vertex)), {{captain_toad::gpu::Attribute::Position, 3, 0}}};

        if (proxyCasters) {
            // Original shadow pass: untextured ROM _shd proxies (terrain world-space triangles
            // plus skinned shadow-only character meshes). Depth only, no offset (the original
            // relies on its depth bias alone).
            captain_toad::gpu::Program& depthOnly =
                device.sharedProgram("shadow caster", kCasterVertex, kCasterFragment);
            depthOnly.setFloats("modelViewProjectionMatrix", matrix.data(), 16);
            captain_toad::gpu::Draw d;
            d.program = &depthOnly;
            d.state = state;
            if (!casters.empty()) {
                if (!casterBuffer_) {
                    casterBuffer_ = device.createBuffer(casters.size() * sizeof(float), casters.data());
                }
                d.streams = {{casterBuffer_.get(), 0, 12, {{captain_toad::gpu::Attribute::Position, 3, 0}}}};
                d.count = int(casters.size() / 3);
                device.draw(d);
            }
            d.streams = {positions};
            for (const auto& m : meshList) {
                if (m.shadowOnly && m.drawn()) {
                    captain_toad::gpu::Draw md = d;
                    captain_toad::gpu::Program* program = &depthOnly;
                    if (skin) {
                        skin(m, md, program, true);
                    }
                    float moved[16];
                    program->setFloats("modelViewProjectionMatrix", meshMatrix(m, moved), 16);
                    md.program = program;
                    md.first = m.first;
                    md.count = m.count;
                    device.draw(md);
                }
            }
        } else {
            drawCasters(vertices, meshList, materials, textures, white, [](const Mesh&) { return true; });
        }
        device.bindTarget(frameTarget);
    }

private:
    // The light's matrix for one mesh: moved by meshMove when it is drawn elsewhere.
    const float* meshMatrix(const Mesh& m, float out[16]) const {
        float move[16];
        if (!meshMove || !meshMove(m, move)) {
            return matrix.data();
        }
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                float v = 0;
                for (int k = 0; k < 4; k++) {
                    v += matrix[size_t(k * 4 + row)] * move[col * 4 + k];
                }
                out[col * 4 + row] = v;
            }
        }
        return out;
    }

    std::unique_ptr<captain_toad::gpu::Buffer> casterBuffer_;
    std::unique_ptr<captain_toad::gpu::Target> staticTarget_;
    uint64_t staticKey_ = 0;
    uint32_t builds_ = 0;
    std::vector<uint32_t> changedAt_;   // per mesh: the build (+1) its vertices last changed before

    static bool casts(const Mesh& m) { return !(m.sky || !m.drawn() || m.shadowOnly || m.translucent); }

    // The depth of another map of the same size into the bound one (a full-screen pass).
    void copyDepth(const captain_toad::gpu::Texture& source) {
        captain_toad::gpu::Device& device = captain_toad::gpu::device();
        captain_toad::gpu::Program& copy = device.sharedProgram(
            "shadow depth copy", "in vec3 inPosition;void main(){gl_Position=vec4(inPosition,1.0);}",
            "uniform sampler2D source;\n"
            "void main(){gl_FragDepth=texelFetch(source,ivec2(gl_FragCoord.xy),0).r;fragColor=vec4(0.0);}");
        captain_toad::gpu::Draw d;
        d.program = &copy;
        d.state.depth = {true, true, captain_toad::gpu::Compare::Always};
        d.samplers = {{"source", 0}};
        d.textures[0] = {&source, {}};
        captain_toad::gpu::Immediate v;
        v.attributes = {{captain_toad::gpu::Attribute::Position, 3, 0}};
        v.floatsPerVertex = 3;
        v.vertex({-1, -1, 0});
        v.vertex({3, -1, 0});
        v.vertex({-1, 3, 0});
        device.draw(d, v);
    }

    // High-quality caster path (enhancement): all visible opaque geometry (terrain, characters,
    // actors) with its original alpha tests, instead of the coarse proxies; `which` picks meshes.
    template <typename Which>
    void drawCasters(const captain_toad::gpu::Buffer& vertices, const std::vector<Mesh>& meshList,
                     const std::vector<std::unique_ptr<captain_toad::gpu::Program>>& materials,
                     const std::vector<std::unique_ptr<captain_toad::gpu::Texture>>& textures,
                     const captain_toad::gpu::Texture& white, const Which& which) {
        captain_toad::gpu::Device& device = captain_toad::gpu::device();
        captain_toad::gpu::RenderState state;
        state.depth = {true, true, captain_toad::gpu::Compare::Less};
        {
            state.raster.depthBias = true;
            state.raster.biasFactor = 1.5f;
            state.raster.biasUnits = 2.f;
            static const float identity4[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            static const float identity3[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
            captain_toad::gpu::Draw d;
            d.state = state;
            d.streams = {{&vertices,
                          0,
                          int(sizeof(Vertex)),
                          {{captain_toad::gpu::Attribute::Position, 3, 0},
                           {captain_toad::gpu::Attribute::Normal, 3, 12},
                           {captain_toad::gpu::Attribute::TexCoord0, 2, 24},
                           {captain_toad::gpu::Attribute::Color, 4, 32}}}};
            d.samplers = {{"albedo", 0}, {"detail", 1}, {"textureZero", 2}, {"shadowDepth", 3}, {"lightingLuts", 4}};
            captain_toad::gpu::Program& depthOnly =
                device.sharedProgram("shadow caster", kCasterVertex, kCasterFragment);
            for (const auto& m : meshList) {
                if (!casts(m) || !which(m)) {
                    continue;
                }
                captain_toad::gpu::Draw md = d;
                float moved[16];
                if (!m.alphaTested) {   // no fragment is discarded: the same depths, depth only
                    captain_toad::gpu::Program* program = &depthOnly;
                    if (skin) {
                        skin(m, md, program, true);
                    }
                    program->setFloats("modelViewProjectionMatrix", meshMatrix(m, moved), 16);
                    md.program = program;
                    md.first = m.first;
                    md.count = m.count;
                    device.draw(md);
                    continue;
                }
                captain_toad::gpu::Program* program = materials.at(m.material).get();
                if (skin) {
                    skin(m, md, program, false);
                }
                captain_toad::gpu::Program& p = *program;
                p.set("useShadows", 0);
                p.set("useLighting", 0);
                p.set("showShadowMask", 0);
                p.set("useNormalMaps", 0);
                p.setFloats("modelViewProjectionMatrix", meshMatrix(m, moved), 16);
                p.setFloats("modelViewMatrix", identity4, 16);
                p.setFloats("normalMatrix", identity3, 9);
                md.program = &p;
                // Units 0 / 1 / 2: slots 1 / 2 / 0; unit 3 (the map itself) reads white.
                md.textures[0] = {m.texture >= 0 ? textures.at(m.texture).get() : &white,
                                  meshSampler(m, 1, m.texture < 0)};
                md.textures[1] = {m.detail >= 0 ? textures.at(m.detail).get() : &white,
                                  meshSampler(m, 2, m.detail < 0)};
                md.textures[2] = {m.texture0 >= 0 ? textures.at(m.texture0).get() : &white,
                                  meshSampler(m, 0, m.texture0 < 0)};
                md.textures[3] = {&white, {}};
                md.first = m.first;
                md.count = m.count;
                device.draw(md);
            }
        }
    }
};
