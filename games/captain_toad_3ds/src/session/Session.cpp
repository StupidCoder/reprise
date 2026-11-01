// =============================================================================================
// Session.cpp - loading a scene package and running its game loop
// =============================================================================================
//
// Loading: the scene package (scene.json and its buffers), the actors, the player, the camera
// tickets, the goal sequence data, the layouts, the effect and sound systems.
//
// The game loop runs at the console's rates: every 60 Hz frame samples the controls and updates
// the sound scripts; every second frame is a 30 Hz game tick. Within a tick the order follows the
// original (see gameFrame below). Effects and sound are decided per tick from the game state, by
// the rules recovered from traced runs. The player's interplay with a course's objects (power
// forms, damage, the pickaxe, pulling, carrying, touch, the goal demo's stage objects) is in
// SessionCourse.cpp.
// =============================================================================================
#include "session/SessionState.hpp"

namespace captain_toad::session {

// Effect owners: their EffectYaml (exported triggers), position and joint/base matrices.
void Session::Impl::registerEffectOwners() {
    if (!eft.enabled) {
        return;
    }
    auto actorOwner = [&](const std::string& name, const std::string& yaml, int index) {
        if (index < 0) {
            return;
        }
        OpeningEffects::Owner o;
        o.name = name;
        o.yamlName = yaml;
        o.position = [this, index] {
            const auto& p = actors[size_t(index)].placement.m;
            return captain_toad::RecoveredEft::V3{p[3], p[7], p[11]};
        };
        o.matrix = [this, index](const std::string& j, captain_toad::RecoveredEft::Mtx34& m) {
            const auto& a = actors[size_t(index)];
            if (j.empty()) {
                m = toMtx(a.placement);
                return true;
            }
            const auto joint = a.findJoint(j);
            if (joint) {
                m = toMtx(*joint);
            }
            return joint.has_value();
        };
        eft.owners.push_back(o);
    };
    {
        OpeningEffects::Owner o;
        o.name = "player";
        o.yamlName = "Kinopio";
        // Floor material: the attribute of the collider's winning ground offer (0x1832b4 / 0x2c4410
        // MaterialCode, sound-material-a); the effect keeper reads this tick's value, the sound
        // keeper the previous tick's.
        o.material = [this] {
            return floorMaterial(false);
        };
        o.position = [this] {
            return captain_toad::RecoveredEft::V3{player.body.pos.x, player.body.pos.y, player.body.pos.z};
        };
        o.matrix = [this](const std::string& j, captain_toad::RecoveredEft::Mtx34& m) {
            if (character.skin.empty()) {
                return false;
            }
            if (j.empty()) {
                m = toMtx(character.placement);
                return true;
            }
            const auto joint = character.findJoint(j);
            if (joint) {
                m = toMtx(*joint);
            }
            return joint.has_value();
        };
        eft.owners.push_back(o);
    }
    // The wet effects (the model constructor 0x160b64 binds the Kinopio entries WetHead to the joint
    // Head and WetBody to JointRoot, 0x2f0610) and the player's hit reactions (the EffectYaml user
    // PlayerHitReaction: LandingSmoke, LandingWater, WaterColumn...), at the protagonist or, for
    // DirectPos entries, at the reaction's point.
    for (const auto& [ownerName, jointName] : {std::pair<const char*, const char*>{"wetHead", "Head"},
                                               std::pair<const char*, const char*>{"wetBody", "JointRoot"}}) {
        OpeningEffects::Owner o;
        o.name = ownerName;
        o.yamlName = "Kinopio";
        o.position = [this] {
            return captain_toad::RecoveredEft::V3{player.body.pos.x, player.body.pos.y, player.body.pos.z};
        };
        const std::string jn = jointName;
        o.matrix = [this, jn](const std::string&, captain_toad::RecoveredEft::Mtx34& m) {
            if (character.skin.empty()) {
                return false;
            }
            const auto joint = character.findJoint(jn);
            if (joint) {
                m = toMtx(*joint);
            }
            return joint.has_value();
        };
        eft.owners.push_back(o);
    }
    {
        OpeningEffects::Owner o;
        o.name = "playerReaction";
        o.yamlName = "PlayerHitReaction";
        o.material = [this] {
            return floorMaterial(false);
        };
        o.position = [this] {
            if (playerReactionHasAt) {
                return captain_toad::RecoveredEft::V3{playerReactionAt.x, playerReactionAt.y, playerReactionAt.z};
            }
            return captain_toad::RecoveredEft::V3{player.body.pos.x, player.body.pos.y, player.body.pos.z};
        };
        o.matrix = [this](const std::string& j, captain_toad::RecoveredEft::Mtx34& m) {
            if (character.skin.empty() || playerReactionHasAt) {
                return false;
            }
            if (j.empty()) {
                m = toMtx(character.placement);
                return true;
            }
            const auto joint = character.findJoint(j);
            if (joint) {
                m = toMtx(*joint);
            }
            return joint.has_value();
        };
        eft.owners.push_back(o);
    }
    // The headlamp set (the protagonist's "HeadLight" entry, NeedProgramInfo, no joint) follows the matrix the
    // player program writes each frame (0x160884, the player object +0xe0; writer found with a
    // write watch, evidence/runs/eft-keeper-c): the HeadLight joint's z axis as front with up =
    // world Y, or world Z when the front's Y component dominates (0x2b61e8 -> 0x2edc80), at the
    // joint position; its Y column is scaled by (hit distance - 10) / 150 when a collision segment
    // from 10 units behind the joint along 500 units of the front hits within 160 units.
    {
        OpeningEffects::Owner o;
        o.name = "headlamp";
        o.yamlName = "Kinopio";
        o.position = [this] {
            return captain_toad::RecoveredEft::V3{player.body.pos.x, player.body.pos.y, player.body.pos.z};
        };
        o.matrix = [this](const std::string& j, captain_toad::RecoveredEft::Mtx34& m) {
            if (character.skin.empty()) {
                return false;
            }
            if (!j.empty()) {
                const auto joint = character.findJoint(j);
                if (joint) {
                    m = toMtx(*joint);
                }
                return joint.has_value();
            }
            const captain_toad::RecoveredEft::Mtx34 J = toMtx(character.joint("HeadLight"));
            captain_toad::RecoveredEft::V3 pos{J[3], J[7], J[11]}, dir{J[2], J[6], J[10]};
            if (dir.x * dir.x + dir.y * dir.y + dir.z * dir.z < 1e-6f) {
                dir = {0, 0, 0};
            } else {
                captain_toad::RecoveredEft::normalize(dir);   // 0x251dd8
            }
            const float ax = std::fabs(dir.x), ay = std::fabs(dir.y), az = std::fabs(dir.z);
            const captain_toad::RecoveredEft::V3 up = (!(ax > ay && ax > az) && !(az >= ay))
                                                          ? captain_toad::RecoveredEft::V3{0, 0, 1}
                                                          : captain_toad::RecoveredEft::V3{0, 1, 0};
            m = captain_toad::RecoveredEffectKeeper::identity();
            captain_toad::RecoveredEffectKeeper::lookRotation(m, dir, up);
            captain_toad::RecoveredEffectKeeper::setTranslation(m, pos);
            float sy = 1.0f;
            if (player.enabled) {
                std::vector<captain_toad::RecoveredCollision::HitInfo> hits;
                const captain_toad::RecoveredVec::V3 start{pos.x - dir.x * 10.0f, pos.y - dir.y * 10.0f,
                                                           pos.z - dir.z * 10.0f},
                    ray{dir.x * 500.0f, dir.y * 500.0f, dir.z * 500.0f};
                captain_toad::RecoveredCollision::segmentCheck(player.collider.parts, start, ray, hits);
                const captain_toad::RecoveredCollision::HitInfo* best = nullptr;
                for (const auto& h : hits) {
                    if (!best || h.depth < best->depth) {
                        best = &h;
                    }
                }
                if (best) {
                    const float dx = best->contact.x - pos.x, dy = best->contact.y - pos.y,
                                dz = best->contact.z - pos.z;
                    const float d = std::sqrt(dx * dx + dy * dy + dz * dz) - 10.0f;
                    if (captain_toad::RecoveredEft::floatBits(d) < captain_toad::RecoveredEft::floatBits(150.0f)) {
                        sy = d * 0.0066666668f;
                    }
                }
            }
            m[1] *= sy;
            m[5] *= sy;
            m[9] *= sy;   // 0x39a328 with (1, sy, 1)
            return true;
        };
        eft.owners.push_back(o);
    }
    actorOwner("companion", "KinopicoNpc", companionActor);
    actorOwner("star", "GoalItem", starActor);
    actorOwner("demostar", "GoalItem", demoStarActor);
    actorOwner("bossbird", "BossBird", bossBirdActor);
    {
        OpeningEffects::Owner o;
        o.name = "courseclear";
        o.yamlName = "UIHeadCourseClear";
        eft.owners.push_back(o);
    }
    for (auto& b : birdFlock.initial) {
        const size_t index = b.first;
        OpeningEffects::Owner o;
        o.name = "bird" + std::to_string(index);
        o.yamlName = "Bird";
        o.position = [this, index] {
            for (auto& x : birdFlock.birds) {
                if (x.first == index) {
                    return captain_toad::RecoveredEft::V3{x.second.pos[0], x.second.pos[1], x.second.pos[2]};
                }
            }
            return captain_toad::RecoveredEft::V3{};
        };
        // Base matrix as the keeper reads it at FlyWait: the facing before the flee turn (the flee
        // nerve's new facing reaches the actor matrix only after the effect is created; traced
        // BirdFly rotation in eft-setmtx-a = the birds' resting yaw).
        o.matrix = [this, index](const std::string& j, captain_toad::RecoveredEft::Mtx34& m) {
            if (!j.empty()) {
                return false;
            }
            const auto& a = actors[index];
            const float yaw = birdRestYaw.count(index) ? birdRestYaw[index] : 0.0f;
            float r[3] = {a.placementRotation[0], float(-yaw * M_PI / 180), a.placementRotation[2]};
            float t[3] = {a.placementTranslation[0], a.placementTranslation[1], a.placementTranslation[2]};
            for (auto& x : birdFlock.birds) {
                if (x.first == index) {
                    for (int k = 0; k < 3; k++) {
                        t[k] = x.second.pos[k];
                    }
                }
            }
            m = toMtx(Affine::trs(a.placementScale, r, t));
            return true;
        };
        eft.owners.push_back(o);
    }
}

void Session::Impl::loadScene() {
    sceneDocument = captain_toad::data::readJson(scenePath / "scene.json");
    scene = sceneDocument;
    if (scene["schema"].i() != 1) {
        throw std::runtime_error("Unsupported scene package");
    }
    captain_toad::data::JsonView c = scene["camera"];
    if (!c) {
        throw std::runtime_error("Scene has no evidenced camera");
    }
    openingCamera.azimuth = c["azimuth"].d();
    openingCamera.elevation = c["elevation"].d();
    openingCamera.distance = c["distance"].d();
    openingCamera.x = c["target"][0].d();
    openingCamera.y = c["target"][1].d();
    openingCamera.z = c["target"][2].d();
    camera = openingCamera;
    fovy = c["fovy"].d();
    clipNear = c["near"].d();
    clipFar = c["far"].d();
    // Fixed-function lights of the inspection views: the opening's named lights, or a stage's
    // first directional light.
    bool haveMain = false;
    for (captain_toad::data::JsonView l : (scene["lighting"]["lights"]).items()) {
        haveMain = haveMain || l["name"].is("mainLight");
    }
    for (captain_toad::data::JsonView l : (scene["lighting"]["lights"]).items()) {
        if (!haveMain && l["kind"].i() == 9) {
            for (int k = 0; k < 3; k++) {
                diffuseLight[k] = l["diffuse"][k].f();
                lightDirection[k] = -l["direction"][k].f();
            }
            haveMain = true;
        }
        if (l["name"].is("AmbientLight")) {
            for (int k = 0; k < 3; k++) {
                ambientLight[k] = l["ambient"][k].f();
            }
        }
        if (l["name"].is("mainLight")) {
            for (int k = 0; k < 3; k++) {
                diffuseLight[k] = l["diffuse"][k].f();
                lightDirection[k] = -l["direction"][k].f();
            }
        }
        if (l["name"].is("secondLight")) {
            for (int k = 0; k < 3; k++) {
                secondDirection[k] = -l["direction"][k].f();
            }
        }
    }
    captain_toad::data::JsonView shadow = scene["shadow"];
    if (!shadow) {
        throw std::runtime_error("Missing shadow parameters");
    }
    nativeShadowResolution = shadow["native_resolution"].i();
    if (shadow["original_resolution"]) {
        originalShadowResolution = shadow["original_resolution"].i();
    }
    romShadowProxies = shadow["casters"] && shadow["casters"]["mode"].is("rom_proxies");
    setShadowQuality(highQualityShadows);
    if (shadow["bounds"]) {
        for (captain_toad::data::JsonView c : (shadow["bounds"]["corners"]).items()) {
            shadowMap.bounds.push_back({c[0].d(), c[1].d(), c[2].d()});
        }
        shadowMap.biasWorld = shadow["bounds"]["bias_world"].d();
    }
    for (int k = 0; k < 4; k++) {
        shadowTint[k] = shadow["tint"][k].f();
    }
    for (int k = 0; k < 3; k++) {
        shadowDirection[k] = -shadow["direction"][k].f();
    }
    const captain_toad::data::Bytes caster = captain_toad::data::readBytes(scenePath / "shadow-casters.bin");
    if (caster.size() % 36) {
        throw std::runtime_error("Invalid shadow caster buffer");
    }
    shadowMap.casters.resize(caster.size() / 4);
    memcpy(shadowMap.casters.data(), caster.data(), caster.size());
    const captain_toad::data::Bytes vertexData = captain_toad::data::readBytes(scenePath / "vertices.bin");
    if (vertexData.size() % sizeof(Vertex)) {
        throw std::runtime_error("Invalid vertex buffer");
    }
    vertices.resize(vertexData.size() / sizeof(Vertex));
    memcpy(vertices.data(), vertexData.data(), vertexData.size());
    const captain_toad::data::Bytes col = captain_toad::data::readBytes(scenePath / "collision.bin");
    if (col.size() % 36) {
        throw std::runtime_error("Invalid collision buffer");
    }
    collision.resize(col.size() / 4);
    memcpy(collision.data(), col.data(), col.size());
    for (captain_toad::data::JsonView m : (scene["meshes"]).items()) {
        Mesh mesh{
            m["first"].i(),  m["count"].i(),          m["texture"].i(),        m["wrap_s"].i(),
            m["wrap_t"].i(), m["detail_texture"].i(), m["material_index"].i(), m["texture0"] ? m["texture0"].i() : -1,
            m["sky"].b()};
        mesh.shadowOnly = m["shadow_only"].b();
        mesh.character = m["character"].b();
        mesh.characterForm = m["character_form"] ? m["character_form"].i() : (mesh.character ? 0 : -1);
        mesh.actor = bool(m["actor"]);
        if (m["lod"]) {
            mesh.lod = m["lod"].i();
            captain_toad::data::JsonView lp = m["lod_params"];
            mesh.lodThreshold[0] = lp["thresholds"][0].f();
            mesh.lodThreshold[1] = lp["thresholds"][1].f();
            mesh.lodAxes = lp["axes"].i();
            mesh.lodDistanceOnly = lp["distance_only"].b();
            mesh.lodLevels = lp["levels"].i();
        }
        if (m["object"].isString()) {
            meshObjectIds.push_back({meshes.size(), m["object"].str()});
        }
        if (mesh.first < 0 || mesh.count < 0 || size_t(mesh.first) + mesh.count > vertices.size()) {
            throw std::runtime_error("Mesh exceeds buffer");
        }
        for (int slot = 0; slot < 3; slot++) {
            auto& sampler = mesh.samplers[slot];
            sampler.wrapS = mesh.wrapS;
            sampler.wrapT = mesh.wrapT;
            if (m["samplers"]) {
                captain_toad::data::JsonView source = m["samplers"][slot];
                sampler.wrapS = source["wrap_s"].i();
                sampler.wrapT = source["wrap_t"].i();
                for (int k = 0; k < 4; k++) {
                    sampler.border[k] = source["border"][k].f() / 255.f;
                }
            }
        }
        meshes.push_back(mesh);
    }
    for (captain_toad::data::JsonView t : (scene["textures"]).items()) {
        int w = t["width"].i(), h = t["height"].i();
        const captain_toad::data::Bytes rgba =
            captain_toad::data::readBytes(scenePath / "textures" / (std::to_string(textures.size()) + ".rgba"));
        if (rgba.size() != size_t(w) * h * 4) {
            throw std::runtime_error("Invalid texture size");
        }
        // Decoded ROM rows and PNG evidence are top-down. Texture rows are uploaded bottom-up;
        // invert rows here, after extraction, for every ROM texture.
        std::vector<unsigned char> upload(rgba.size());
        for (int y = 0; y < h; y++) {
            memcpy(upload.data() + y * w * 4, rgba.data() + (h - 1 - y) * w * 4, w * 4);
        }
        textures.push_back(
            captain_toad::gpu::device().createTexture(captain_toad::gpu::Format::RGBA8, w, h, upload.data()));
    }
    const captain_toad::data::Bytes tangentData = captain_toad::data::readBytes(scenePath / "tangents.bin");
    if (tangentData.size() != vertices.size() * 12) {
        throw std::runtime_error("Invalid tangent buffer");
    }
    tangents.resize(vertices.size() * 3);
    memcpy(tangents.data(), tangentData.data(), tangentData.size());
    if (std::filesystem::exists(scenePath / "uv1.bin")) {
        const captain_toad::data::Bytes uv1 = captain_toad::data::readBytes(scenePath / "uv1.bin");
        if (uv1.size() != vertices.size() * 2 * sizeof(float)) {
            throw std::runtime_error("uv1.bin size mismatch");
        }
        secondUv.resize(vertices.size() * 2);
        memcpy(secondUv.data(), uv1.data(), uv1.size());
    }
    int rows = scene["lighting"]["lut_rows"].i();
    const captain_toad::data::Bytes lut = captain_toad::data::readBytes(scenePath / "lighting-luts.bin");
    if (rows < 28 || lut.size() != size_t(rows) * 256 * 16) {
        throw std::runtime_error("Invalid lighting LUT atlas");
    }
    lutRowCount = rows;
    lightingLuts = captain_toad::gpu::device().createTexture(captain_toad::gpu::Format::RGBA32F, 256, rows, lut.data());
    for (auto m : meshes) {
        if (m.texture < -1 || m.texture >= int(textures.size())) {
            throw std::runtime_error("Invalid texture reference");
        }
    }
    const captain_toad::data::Bytes vertexText = captain_toad::data::readBytes(scenePath / "shaders/terrain.vert");
    const std::string_view vertexSource(reinterpret_cast<const char*>(vertexText.data()), vertexText.size());
    materialVertexSource = std::string(vertexSource);   // for the skinned variants (SessionSkin.cpp)
    for (unsigned i = 0; i < scene["materials"].count(); i++) {
        const std::string name = std::to_string(i) + ".frag";
        const captain_toad::data::Bytes fragmentText = captain_toad::data::readBytes(scenePath / "shaders" / name);
        materialFragments.emplace_back(reinterpret_cast<const char*>(fragmentText.data()), fragmentText.size());
        programs.push_back(
            captain_toad::gpu::device().createProgram(vertexSource, materialFragments.back(), "material " + name));
        captain_toad::data::JsonView regs = scene["materials"][i]["fragment_registers"];
        unsigned blend = regs["257"].u(), depth = regs["263"].u();
        // PICA blend register 0x101: equations (bits 0-2, 8-10) and factors (16-31); depth register
        // 0x107: test (bit 0), function (bits 4-6), write (bit 12). The device enumerations follow
        // the register encodings.
        if ((blend & 7) > 4 || ((blend >> 8) & 7) > 4 || ((blend >> 16) & 15) > 14 || ((blend >> 20) & 15) > 14 ||
            ((blend >> 24) & 15) > 14 || ((blend >> 28) & 15) > 14) {
            throw std::runtime_error("Unsupported material blending");
        }
        materialStates.push_back(
            {blend != 0x1010000,
             bool(depth & 0x1000),
             bool(depth & 1),
             bool(scene["materials"][i]["flags"].u() & 32),
             captain_toad::gpu::Compare((depth >> 4) & 7),
             {captain_toad::gpu::BlendOp(blend & 7), captain_toad::gpu::BlendOp((blend >> 8) & 7)},
             {captain_toad::gpu::BlendFactor((blend >> 16) & 15), captain_toad::gpu::BlendFactor((blend >> 20) & 15),
              captain_toad::gpu::BlendFactor((blend >> 24) & 15), captain_toad::gpu::BlendFactor((blend >> 28) & 15)}});
        if (i == 0) {
            if (captain_toad::data::JsonView ma = scene["sky"]["material_animations"]) {
                skyMaterials.rest.load(ma["rest"]);
                for (const std::string& file : ma["clips"].keys()) {
                    skyMaterials.clips[file].load(ma["clips"][file]["materials"]);
                }
                for (captain_toad::data::JsonView f : ma["play"].items()) {
                    skyMaterialClips.push_back({f.str(), ma["clips"][f.str()]["loop"].b()});
                }
            }
        }
        if (captain_toad::data::JsonView a = scene["materials"][i]["uv_animation"]) {
            for (captain_toad::data::JsonView k : (a["keys"]).items()) {
                materialStates.back().uvKeys.push_back({k[0].d(), k[1].d()});
            }
            materialStates.back().uvDuration = scene["sky"]["animation"]["duration"].d();
            skyStart = skyFrame = scene["sky"]["animation"]["start_frame"].d();
        }
    }
    // Blended or depth-masked surfaces (glow billboards, sparkles) do not cast in the full-geometry shadow pass.
    for (auto& m : meshes) {
        if (m.material >= 0 && m.material < (int)materialStates.size()) {
            // Alpha test (register 0x104): enabled and not "always pass" (importer/Shaders.cpp).
            const unsigned test = scene["materials"][m.material]["fragment_registers"]["260"].u();
            m.alphaTested = (test & 1) && ((test >> 4) & 7) != 1;
            m.translucent = materialStates[m.material].blend || !materialStates[m.material].depthWrite;
            m.water = bool(scene["materials"][m.material]["metadata"]["$water"]);
        }
    }
    if (captain_toad::data::JsonView b = scene["bloom"]) {
        bloom.enabled = b["enabled"].b();
        for (int k = 0; k < 3; k++) {
            bloom.dotColor[size_t(k)] = b["dot_color"][k].f() / 255.0f;
        }
        bloom.power = b["power"].f() / 255.0f;
        bloom.spread = b["spread"].f();
    }
    if (captain_toad::data::JsonView m = scene["camera_mask"];
        m && m["texture"] && m["texture"].i() < int(textures.size())) {
        cameraMask.enabled = true;
        for (int k = 0; k < 3; k++) {
            cameraMask.color1[size_t(k)] = m["color1"][k].f();
            cameraMask.color2[size_t(k)] = m["color2"][k].f();
            cameraMask.position[size_t(k)] = m["position"][k].f();
            cameraMask.scale[size_t(k)] = m["scale"][k].f();
        }
        cameraMask.alpha = m["alpha"].f();
        cameraMask.texture = textures[size_t(m["texture"].i())].get();
        for (captain_toad::data::JsonView v : m["vertices"].items()) {
            cameraMask.vertices.push_back({v[0].f(), v[1].f(), v[2].f(), v[3].f(), v[4].f()});
        }
    }
    if (scene["lighting"]["local_lights"]) {
        lightService.load(scene["lighting"], scene["materials"]);
        lightService.loadAreas(scene["lighting"], scene["stage_areas"]);
        lightService.collision = collision;
        picaLighting = true;
    }
    for (captain_toad::data::JsonView a : (scene["actors"]).items()) {
        const captain_toad::data::Value actorDocument = captain_toad::data::readJson(scenePath / a["file"].str());
        captain_toad::data::JsonView c = actorDocument;
        actors.emplace_back();
        actors.back().load(c, captain_toad::data::readBytes(scenePath / a["skin"].str()));
        actorClips.emplace_back();
        for (const std::string& clip : c["curve_clips"].keys()) {
            // Baked clips: two 60 Hz frames per sample (render/CharacterAnimation.hpp curveClipStep).
            const float step = c["curve_clips"][clip]["matrices"].keys().empty() ? 1.0f : 2.0f;
            actorClips.back()[clip] = {c["curve_clips"][clip]["frames"].f() * step, c["curve_clips"][clip]["loop"].b()};
        }
        actorVisibilityClips.emplace_back();
        for (const std::string& v : c["visibility_overlays"].keys()) {
            actorVisibilityClips.back()[v] = {c["visibility_overlays"][v]["frames"].f(),
                                              c["visibility_overlays"][v]["loop"].b()};
        }
        if (c["object"].isString()) {
            actorObjectIds.push_back({actors.size() - 1, c["object"].str()});
        }
        if (c["rail_guide"]) {
            game.loadCompanion(c);
            companionActor = int(actors.size()) - 1;
        }
        if (captain_toad::data::JsonView d = c["drop_shadow"]) {
            actorShadows.push_back(
                {actors.size() - 1, d["joint"].str(), d["radius"].d(), d["height"].d(), d["top_offset"].d()});
        }
        actorMaterials.emplace_back();
        if (c["material_animation"]) {
            actorMaterials.back().load(c["material_animation"]);
        }
        actorClipMaterials.emplace_back();
        for (const std::string& clip : c["clip_material_animation"].keys()) {
            actorClipMaterials.back().sets[clip].load(c["clip_material_animation"][clip]);
        }
        if (a["name"].is("star")) {
            starActor = int(actors.size()) - 1;
        }
        if (a["name"].is("demostar")) {
            demoStarActor = int(actors.size()) - 1;
        }
        if (a["name"].is("bossbird")) {
            bossBirdActor = int(actors.size()) - 1;
        }
        if (captain_toad::data::JsonView g = c["grass_control"]) {
            GrassWindState w;
            w.actor = actors.size() - 1;
            auto& k = w.control;
            k.bendRate = g["bend_rate"].f();
            k.slideMax = g["slide_max"].f();
            k.windX = g["wind_x"].f();
            k.windZ = g["wind_z"].f();
            k.timeShift = g["time_shift"].f();
            k.frames = g["frames"].i();
            // One chain: zero extent bounds (min=max=root), as computed by 0x2d1eb4 for a single Grass*Root.
            k.precompute((int)g["chain"].count(), 0, 0, 0, 0, 0, 0);
            k.index = (g["start_index"].i() - 1 + k.frames) % k.frames;
            grassWinds.push_back(w);
        }
        if (captain_toad::data::JsonView b = c["bird_behaviour"]) {
            captain_toad::RecoveredBird::Bird bird;
            bird.clip = b["clip"].str();
            bird.clipEnd = captain_toad::RecoveredBird::Bird::length(bird.clip);
            bird.clipFrame = b["frame"].f();
            bird.step = b["step"].i();
            bird.yaw = b["yaw"].f();
            for (int k = 0; k < 3; k++) {
                bird.pos[k] = actors.back().placementTranslation[k];
            }
            birdFlock.initial.push_back({actors.size() - 1, bird});
            birdFlock.reset();
        }
        for (auto& entry : actorMaterials.back().slots) {
            materialActor[entry.first] = actors.size() - 1;
        }
        for (auto& entry : actorMaterials.back().constants) {
            materialActor[entry.first] = actors.size() - 1;
        }
    }
    if (scene["character"]) {
        const captain_toad::data::Value characterDocument = captain_toad::data::readJson(scenePath / "character.json");
        character.load(characterDocument, captain_toad::data::readBytes(scenePath / "character-skin.bin"));
        // The wet look's colour clips of both forms (one set: the forms' materials differ).
        const auto loadWet = [this](captain_toad::data::JsonView doc) {
            protagonistMaterials.rest.load(doc["material_rest"]);
            for (const std::string& clip : doc["material_animations"].keys()) {
                protagonistMaterials.clips[clip].load(doc["material_animations"][clip]["materials"]);
            }
        };
        loadWet(characterDocument);
        // The Mini form's model (KinopioMini), swapped in by setCharacterForm.
        if (scene["character_mini"]) {
            const captain_toad::data::Value miniDocument =
                captain_toad::data::readJson(scenePath / "character-mini.json");
            loadWet(miniDocument);
            characterAlt.load(miniDocument, captain_toad::data::readBytes(scenePath / "character-mini-skin.bin"));
            for (auto& m : meshes) {
                if (m.characterForm == 1) {
                    m.visible = false;
                }
            }
        }
        if (scene["player"]) {
            const captain_toad::data::Value playerDocument = captain_toad::data::readJson(scenePath / "player.json");
            captain_toad::data::JsonView pj = playerDocument;
            player.packageDirectory = scenePath;
            const captain_toad::data::Bytes kclBytes =
                pj["collision"]["file"] ? captain_toad::data::readBytes(scenePath / pj["collision"]["file"].str())
                                        : captain_toad::data::Bytes();
            player.load(pj, characterDocument, kclBytes, character);
        }
    }
    if (scene["game_camera"] && player.enabled) {
        game.load(scene["game_camera"], player.body.pos);
        game.begin(player.body.pos);
        game.startCompanion(player.body.pos);
        if (scene["goal"] && starActor >= 0) {
            std::map<std::string, float> lengths;
            for (const char* c : {"GoalPosePairSign", "GoalPosePair"}) {
                lengths[c] = player.clips.at(c).frames;
            }
            lengths["GoalPoseSign"] = actors[starActor].curveClipFrames.at("GoalPoseSign");
            lengths["DemoOpeningSeason1"] = player.clips.at("DemoOpeningSeason1").frames;
            game.loadGoal(scene["goal"], lengths);
            game.loadDemoCamera(scene["demo_camera"]);
            for (captain_toad::data::JsonView l : (scene["layouts"]).items()) {
                layoutStore.push_back(std::make_unique<Layout>());
                Layout* x = layoutStore.back().get();
                x->load(captain_toad::data::readJson(scenePath / l["file"].str()), scenePath);
                x->registry = &layouts;
                layouts[x->name] = x;
            }
            // Frame counts of the layout animations the sequence plays (captain_toad::RecoveredAnimFrame::Layout).
            auto frames = [&](const char* l, const char* a) {
                return layouts.at(l)->animations.at(a).frames;
            };
            game.goal.layoutLength = {{"CourseClear.Appear", frames("HeadCourseClear_TOP", "Appear")},
                                      {"Wipe.Appear", frames("WipeCircle_TOP", "Appear")},
                                      {"Wipe.End", frames("WipeCircle_TOP", "End")},
                                      {"Title.Appear", frames("TitleLogoDemo_TOP", "Appear")},
                                      {"Title.End", frames("TitleLogoDemo_TOP", "End")}};
        }
        // Stage-start demo (scene.json stage_start): placement data and the player's clip lengths.
        if (captain_toad::data::JsonView ss = scene["stage_start"]) {
            captain_toad::RecoveredStageStart::Params p;
            p.demoKind = ss["demo_kind"].str();
            p.demoYaw = ss["demo_yaw"].f();
            p.startAnimation = ss["start_animation"].str();
            p.playerYaw = ss["player_yaw"].f();
            p.ghostStage = ss["ghost_stage"].b();
            for (const std::string& c : {p.startAnimation, std::string("Turn"), std::string("Wait")}) {
                p.clipLength[c] = player.clips.at(c).frames;
            }
            intro.begin(p, player.body.pos);
            if (layouts.count("WipeCircleWhite_TOP")) {
                intro.wipeLength = layouts["WipeCircleWhite_TOP"]->animations.at("End").frames;
            }
        }
    }
    loadStageObjects();
    // A course's goal demo (scene.json goal, StageExport): the opening's goal sequence (Goal.hpp)
    // with the course's parameters; its star and demo star are the stage objects GoalItem and
    // GoalDemoStar, whose actions the sequence starts (their effects and sounds follow from them).
    if (stageObjects && scene["goal"] && !game.goalEnabled && game.enabled) {
        for (size_t i = 0; i < stageWorld.objects.size(); i++) {
            const auto& o = stageWorld.objects[i];
            if (o.cls == "GoalItem") {
                goalStarObject = int(i);
            } else if (o.cls == "GoalDemoStar") {
                goalDemoObject = int(i);
            }
        }
        captain_toad::data::JsonView g = scene["goal"];
        if (goalStarObject >= 0 && goalDemoObject >= 0) {
            stageGoal = true;
            std::map<std::string, float> lengths;
            for (const char* c : {"sign", "pair"}) {
                const std::string name = g["clips"][c].str();
                lengths[name] = player.clips.at(name).frames;
            }
            const auto& starClips = stageWorld.objects[size_t(goalStarObject)].clips;
            for (const char* c : {"star_sign", "pair", "mini"}) {
                const std::string name = g["clips"][c].str();
                if (auto it = starClips.find(name); it != starClips.end()) {
                    lengths[name] = it->second.first;
                }
            }
            game.loadGoal(g, lengths);
            game.goal.layoutLength = {{"CourseClear.Appear", 1.0f}};   // the banner is the shell's (CourseUi)
        }
    }
    eft.load(scenePath);
    registerEffectOwners();
    loadSound();
    registerObjectOwners();
    const unsigned char white[] = {255, 255, 255, 255};
    whiteTexture = captain_toad::gpu::device().createTexture(captain_toad::gpu::Format::RGBA8, 1, 1, white);
    if (getenv("DIORAMA_GPU_SKINNING")) {   // research: the GPU path in the headless modes
        setGpuSkinning(true);
    }
}

// The vertex streams on the GPU: everything on the first draw, then the skinned models' ranges
// that moved since the last upload.
void Session::Impl::uploadVertices() {
    captain_toad::gpu::Device& device = captain_toad::gpu::device();
    if (!vertexBuffer) {
        vertexBuffer = device.createBuffer(vertices.size() * sizeof(Vertex), vertices.data());
        tangentBuffer = device.createBuffer(tangents.size() * sizeof(float), tangents.data());
        if (!secondUv.empty()) {
            secondUvBuffer = device.createBuffer(secondUv.size() * sizeof(float), secondUv.data());
        }
        if (!collision.empty()) {
            collisionBuffer = device.createBuffer(collision.size() * sizeof(float), collision.data());
        }
        dirtyVertices.clear();
        return;
    }
    for (const auto& [first, count] : dirtyVertices) {
        if (gpuSkinning && skinRanges.count(int(first))) {
            continue;   // drawn from its bind data and the palette
        }
        vertexBuffer->update(first * sizeof(Vertex), &vertices[first], count * sizeof(Vertex));
        tangentBuffer->update(first * 3 * sizeof(float), &tangents[first * 3], count * 3 * sizeof(float));
    }
    dirtyVertices.clear();
}

// Course packages: every placed object with an EffectYaml or SeDataBase entry is an owner
// ("obj:<id>"): positioned at its world matrix (its actor's joints for animated models); its clip
// starts are actions for both keepers, and its hit reactions (World::reaction) go through its
// model's ActorHitReactionCtrl table to an effect, a sound PlayInfo and a camera shake.
void Session::Impl::registerObjectOwners() {
    if (!stageObjects) {
        return;
    }
    if (std::filesystem::exists(scenePath / "effects/effects.json")) {
        const captain_toad::data::Value meta = captain_toad::data::readJson(scenePath / "effects/effects.json");
        captain_toad::data::JsonView hr = captain_toad::data::JsonView(meta)["hit_reactions"];
        for (const std::string& model : hr.keys()) {
            for (captain_toad::data::JsonView r : hr[model].items()) {
                HitReaction h;
                h.effect = r["EffectName"].isString() ? r["EffectName"].str() : "";
                h.sound = r["SeName"].isString() ? r["SeName"].str() : "";
                h.shake = r["CameraShakeName"].isString() ? r["CameraShakeName"].str() : "";
                h.shakeDistance = r["CameraShakeDistance"] ? r["CameraShakeDistance"].f() : 0.f;
                if (r["StopSceneFrame"]) {
                    h.stopFrames = r["StopSceneFrame"].i();
                }
                h.stopForHitEffect = r["IsStopSceneForHitEffect"].b();
                hitReactions[model][r["ReactionName"].str()] = h;
            }
        }
    }
    objectEffectOwner.assign(stageWorld.objects.size(), 0);
    objectActionSeen.clear();
    reactionAtObject.assign(stageWorld.objects.size(), 0);
    objectSoundOwner.assign(stageWorld.objects.size(), 0);
    for (size_t i = 0; i < stageWorld.objects.size(); i++) {
        const auto& obj = stageWorld.objects[i];
        const std::string name = "obj:" + obj.id;
        auto position = [this, i] {
            if (reactionAtObject[i]) {
                return reactionAt;
            }
            const auto& m = stageWorld.objects[i].world.m;
            return captain_toad::RecoveredVec::V3{m[3], m[7], m[11]};
        };
        if (eft.enabled && !obj.effectUser.empty() && eft.yaml.count(obj.effectUser)) {
            OpeningEffects::Owner o;
            o.name = name;
            o.yamlName = obj.effectUser;
            o.position = [position] {
                const auto p = position();
                return captain_toad::RecoveredEft::V3{p.x, p.y, p.z};
            };
            if (obj.nerveDriven) {
                o.nerveStep = [this, i]() -> int {
                    return stageWorld.objects[i].nerveStep;
                };
            }
            o.matrix = [this, i](const std::string& j, captain_toad::RecoveredEft::Mtx34& m) {
                const int a = objectActor[i];
                if (!j.empty()) {
                    if (a < 0) {
                        return false;
                    }
                    const auto joint = actors[size_t(a)].findJoint(j);
                    if (joint) {
                        m = toMtx(*joint);
                    }
                    return joint.has_value();
                }
                for (int k = 0; k < 12; k++) {
                    m[k] = stageWorld.objects[i].world.m[k];
                }
                if (reactionAtObject[i]) {
                    m[3] = reactionAt.x;
                    m[7] = reactionAt.y;
                    m[11] = reactionAt.z;
                }
                return true;
            };
            eft.owners.push_back(o);
            objectEffectOwner[i] = 1;
        }
        if (snd && !obj.soundUser.empty()) {
            captain_toad::OpeningSoundKeeper::Owner o;
            o.name = name;
            o.db = obj.soundUser;
            o.position = [position] {
                const auto p = position();
                return captain_toad::audio::Vec3f{p.x, p.y, p.z};
            };
            sndKeeper->owners.push_back(o);
            objectSoundOwner[i] = 1;
        }
    }
    if (snd && sndKeeper->dbs.count("StageStartDemoAtmosphere")) {
        captain_toad::OpeningSoundKeeper::Owner o;
        o.name = "stagestart";
        o.db = "StageStartDemoAtmosphere";
        sndKeeper->owners.push_back(o);
    }
    stageWorld.playSound = [this](const captain_toad::RecoveredStage::Object& o, const char* play, int choice) {
        if (snd) {
            programSounds.push_back({size_t(stageWorld.indexOf(o)), play, choice});
        }
    };
    stageWorld.hitReaction = [this](const captain_toad::RecoveredStage::Object& o, const char* reaction,
                                    const captain_toad::RecoveredVec::V3* at, bool stopScene) {
        const size_t i = size_t(stageWorld.indexOf(o));
        PendingReaction r{i, reaction, at != nullptr, at ? *at : captain_toad::RecoveredVec::V3{}};
        if (stopScene) {
            applySceneStop(o.reactionTable, reaction);
        }
        if (eft.enabled) {
            effectReactions.push_back(r);
        }
        if (snd) {
            soundReactions.push_back(r);
        }
        if (getenv("DIORAMA_DEBUG")) {
            fprintf(stderr, "hit reaction %s %s\n", o.id.c_str(), reaction);
        }
    };
}

void Session::Impl::loadSound() {
    const std::filesystem::path dir = scenePath / "sound";
    if (!std::filesystem::exists(dir) || getenv("DIORAMA_NO_SOUND")) {
        return;
    }
    // Course packages keep the shared middleware data in Sound/Course (sedb.json "data").
    std::filesystem::path dataDir = dir;
    if (std::filesystem::exists(dir / "sedb.json")) {
        const captain_toad::data::Value sedb = captain_toad::data::readJson(dir / "sedb.json");
        captain_toad::data::JsonView j = sedb;
        if (j["data"].isString()) {
            dataDir = (dir / j["data"].str()).lexically_normal();
        }
        stageBgm = j["bgm"]["Stage"].isString() ? j["bgm"]["Stage"].str() : "";
        invincibleBgm = j["bgm"]["Invincible"].isString() ? j["bgm"]["Invincible"].str() : "";
        clearJingle = j["bgm"]["KinotanClear"].isString() ? j["bgm"]["KinotanClear"].str() : "";
        stageStartSound = j["stage"]["StageStartSeName"].isString() ? j["stage"]["StageStartSeName"].str() : "";
    }
    snd = std::make_unique<captain_toad::audio::SoundSystem>(dataDir.string());
    // Random states at the opening checkpoint (export_sound.py checkpoint.json): the sound LCG
    // (0x4f8128) and the shared game xorshift that the birds and every sound request draw.
    if (std::filesystem::exists(dir / "checkpoint.json")) {
        const captain_toad::data::Value checkpoint = captain_toad::data::readJson(dir / "checkpoint.json");
        captain_toad::data::JsonView cp = checkpoint;
        snd->rng.state = cp["sound_lcg"].u();
        for (int i = 0; i < 4; i++) {
            checkpointGameRng[i] = cp["game_rng"][i].u();
        }
        haveCheckpointGameRng = true;
    }
    sndKeeper = std::make_unique<captain_toad::OpeningSoundKeeper::Keeper>(*snd);
    sndKeeper->load(dir);
    if (!getenv("DIORAMA_SOUND_NO_RNG")) {
        sndKeeper->onRequest = [this] {
            birdFlock.rng.unit();
        };   // shared game RNG 0x2f934c
    }
    using O = captain_toad::OpeningSoundKeeper::Owner;
    // Emitter: the animated JointRoot (the protagonist's EmitterInfoList "Actor" at JointRoot; the goal pose
    // pans the steps 55..73 in sound-trace-a while the body stays put).
    {
        O o;
        o.name = "player";
        o.db = "PlayerBase";
        o.forced = 0;
        o.position = [this] {
            if (character.skin.empty()) {
                return av(player.body.pos);
            }
            const float origin[3] = {0, 0, 0};
            auto r = character.joint("JointRoot").apply(origin, true);
            return captain_toad::audio::Vec3f{r[0], r[1], r[2]};
        };
        // Floor material: the collider's winning ground offer of the previous tick (all 40 steps of
        // sound-trace-a; the original reads the attribute's MaterialCode in 0x1832b4 / 0x2c4410).
        o.material = [this] {
            return floorMaterial(true);
        };
        sndKeeper->owners.push_back(o);
    }
    {
        O o;
        o.name = "companion";
        o.db = "KinopicoNpc";
        o.forced = 1;
        o.position = [this] {
            return av(game.companion.c.pose.pos);
        };
        sndKeeper->owners.push_back(o);
    }
    for (auto& b : birdFlock.birds) {
        O o;
        o.name = "bird" + std::to_string(b.first);
        o.db = "Bird";
        size_t i = b.first;
        o.position = [this, i] {
            for (const auto& x : birdFlock.birds) {
                if (x.first == i) {
                    return captain_toad::audio::Vec3f{x.second.pos[0], x.second.pos[1], x.second.pos[2]};
                }
            }
            return captain_toad::audio::Vec3f{};
        };
        sndKeeper->owners.push_back(o);
    }
    {
        O o;
        o.name = "goalitem";
        o.db = "GoalItem";
        o.position = [this] {
            if (starActor < 0) {
                return captain_toad::audio::Vec3f{};
            }
            const auto& m = actors[size_t(starActor)].placement.m;
            return captain_toad::audio::Vec3f{m[3], m[7], m[11]};
        };
        sndKeeper->owners.push_back(o);
    }
    {
        O o;
        o.name = "wipe";
        o.db = "WipeClearDemo";
        sndKeeper->owners.push_back(o);
    }
    {
        O o;
        o.name = "camera";
        o.db = "Camera";
        sndKeeper->owners.push_back(o);
    }
}

// =============================================================================================
// Controls
// =============================================================================================
// The platform controls are mapped to the game's controller layout, so the recovered pad code sees
// exactly what it would on the console.
void Session::Impl::preset(int n) {
    gameCamera = false;
    camera = openingCamera;
    if (n == 2) {
        camera.azimuth = -135;
    }
    if (n == 3) {
        camera.azimuth = 135;
        camera.elevation = 65;
        camera.distance = 3500;
    }
}

// Viewer camera as a camera-to-world matrix (inspection mode only).
void Session::Impl::cameraMatrix(float cam[12]) {
    double a = camera.azimuth * M_PI / 180, e = camera.elevation * M_PI / 180;
    double X[3] = {cos(a), 0, -sin(a)}, Y[3] = {-sin(e) * sin(a), cos(e), -sin(e) * cos(a)},
           Z[3] = {cos(e) * sin(a), sin(e), cos(e) * cos(a)};
    for (int r = 0; r < 3; r++) {
        cam[r * 4] = X[r];
        cam[r * 4 + 1] = Y[r];
        cam[r * 4 + 2] = Z[r];
        cam[r * 4 + 3] = 0;
    }
}

// =============================================================================================
// Effects
// =============================================================================================
// Research log (DIORAMA_DEBUG_FLOOR): the floor attribute under the player and the attributes
// below a 5 x 5 grid around it; with allHits, every ground hit of the collider.
void Session::Impl::logFloor(bool allHits) {
    auto& o = eft.owner("player");
    auto& c = player.collider;
    int attr = -1;
    if (c.ground.count()) {
        const auto& h = c.ground.hits[c.nearestGround(player.body.pos)];
        if (h.part >= 0) {
            attr = c.parts[size_t(h.part)].kcl->prisms.at(size_t(h.prism)).attribute;
        }
    }
    if (allHits) {   // every ground hit (attribute, depth, contact)
        fprintf(stderr, "hits tick %lld best %d pos %.2f %.2f %.2f n %u", eft.ticks, c.groundAttribute(),
                player.body.pos.x, player.body.pos.y, player.body.pos.z, c.ground.count());
        for (uint32_t i = 0; i < c.ground.count(); i++) {
            const auto& h = c.ground.hits[i];
            int a = h.part >= 0 ? c.parts[size_t(h.part)].kcl->prisms.at(size_t(h.prism)).attribute : -1;
            fprintf(stderr, " [%d %.3f %.2f %.2f %.2f]", a, h.depth, h.contact.x, h.contact.y, h.contact.z);
        }
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "floor tick %lld attr %d material %s pos %.1f %.1f %.1f spheres:", eft.ticks, attr,
            o.material().c_str(), player.body.pos.x, player.body.pos.y, player.body.pos.z);
    for (int dx = -40; dx <= 40; dx += 20) {
        for (int dz = -40; dz <= 40; dz += 20) {
            std::vector<captain_toad::RecoveredCollision::HitInfo> hs;
            captain_toad::RecoveredCollision::segmentCheck(
                c.parts, {player.body.pos.x + dx, player.body.pos.y + 50, player.body.pos.z + dz}, {0, -100, 0}, hs);
            int a = -1;
            float bd = 1e9;
            for (auto& h : hs) {
                if (h.depth < bd) {
                    bd = h.depth;
                    a = c.parts[size_t(h.part)].kcl->prisms.at(size_t(h.prism)).attribute;
                }
            }
            fprintf(stderr, "%d", a < 0 ? 9 : a);
        }
    }
    fprintf(stderr, "\n");
}

// Per game tick: which actor actions and code paths start, fade or kill particle effects. The
// rules come from the traced creations (evidence/runs/eft-create-*, eft-setmtx-a).
void Session::Impl::effectsTick() {
    playerFloorPrev = playerFloorAttr;
    playerFloorAttr = player.enabled ? player.collider.groundAttribute() : -1;
    playerFloorPartPrev = playerFloorPart;
    playerFloorPart = player.enabled ? player.collider.groundPart() : -1;
    if (!eft.enabled) {
        return;
    }
    poseScene();
    captain_toad::RecoveredEft::Mtx34 view;
    for (int k = 0; k < 12; k++) {
        view[k] = game.view.m[k];
    }
    // Headlamp sets (the protagonist's HeadLight / HeadLightGlow): alive from the stage start (creation ids
    // 7 and 8 at the checkpoint), killed when the goal demo's Pose nerve starts the pair poses and
    // created again by GoalPose step 0 (goal k 26 / 27 in eft-setmtx-a).
    const auto& g = game.goal;
    if (g.active() && g.pairPosesStarted && headlampOn) {
        eft.kill("headlamp", "HeadLight");
        eft.kill("player", "HeadLightGlow");
        headlampOn = false;
    }
    const bool first = !objectsStarted;
    if (!objectsStarted) {
        eft.startObjects(view);
        objectsStarted = true;
    }   // stage load order: objects, then the headlamp
    if (stageObjects) {
        eft.objectSwitches([this](const std::string& id) { return stageWorld.switchOnId(id); }, view);
    }
    for (const auto& e : playerEffects) {   // the player's hit reactions (ダメージヒット: HitDamage)
        eft.emit("player", e, view);
    }
    playerEffects.clear();
    // The player's hit reactions of the actions and the water (table "Player", effects of the
    // PlayerHitReaction entries listing the reaction or named by the table).
    for (const auto& r : player.machine.ctx.reactions) {
        playerReactions.push_back({r, false, {}});
    }
    player.machine.ctx.reactions.clear();
    for (const auto& r : playerReactions) {
        std::vector<std::string> names = eft.reactionEffects("playerReaction", r.name);
        if (auto t = hitReactions.find("Player"); t != hitReactions.end()) {
            if (auto h = t->second.find(r.name);
                h != t->second.end() && !h->second.effect.empty() &&
                std::find(names.begin(), names.end(), h->second.effect) == names.end()) {
                names.push_back(h->second.effect);
            }
        }
        playerReactionHasAt = r.hasAt;
        playerReactionAt = r.at;
        for (const auto& n : names) {
            eft.emit("playerReaction", n, view);
        }
        playerReactionHasAt = false;
    }
    playerReactions.clear();
    if (playerWet.startEffects) {
        eft.emit("wetHead", "WetHead", view);
        eft.emit("wetBody", "WetBody", view);
    }
    if (playerWet.stopEffects) {
        eft.kill("wetBody", "WetBody");
        eft.kill("wetHead", "WetHead");
    }
    if (!headlampOn && (!g.active() || g.goalPoseStarted)) {
        eft.emit("headlamp", "HeadLight", view);
        eft.emit("player", "HeadLightGlow", view);
        headlampOn = true;
    }
    // Without the intro the play starts in the checkpoint state, where these sets already exist:
    // continue the eft xorshift from its checkpoint value (replaying the traced creations from it
    // reproduces the traced state before Get; evidence/effects/validation.txt).
    if (first && !introEnabled && eft.haveCheckpointXs) {
        memcpy(eft.sys.xs, eft.checkpointXs, 16);
    }
    if (game.goalEnabled && g.active()) {
        auto started = [](const captain_toad::RecoveredGoal::ClipClock& c) {
            return c.running() && c.age == 0;
        };
        if (started(g.star) && getenv("DIORAMA_EFT_DEBUG")) {
            fprintf(stderr, "eft xorshift before Get %08x %08x %08x %08x\n", eft.sys.xs[0], eft.sys.xs[1],
                    eft.sys.xs[2], eft.sys.xs[3]);
        }
        if (started(g.star) && starActor >= 0) {
            eft.setAction("star", g.star.name);
        }
        if (started(g.player)) {
            eft.setAction("player", g.player.name);
            playerEffectAction = g.player.name;
        }
        if (started(g.companion)) {
            // For the opening demo clip the companion uses the protagonist's effect entries.
            const bool demo = g.companion.name == "DemoOpeningSeason1";
            eft.setAction("companion", demo ? "KinopicoDemoOpeningSeason1" : g.companion.name,
                          demo ? "Kinopio" : "KinopicoNpc");
        }
        if (started(g.demoStar) && demoStarActor >= 0) {
            eft.setAction("demostar", g.demoStar.name);
        }
        if (started(g.bossBird) && bossBirdActor >= 0) {
            eft.setAction("bossbird", g.bossBird.name);
        }
        if (g.courseClearAnim.name == "Appear" && g.courseClearAnim.age == 0) {
            eft.setAction("courseclear", "Appear");
        }
    }
    // Player animation actions (Move, Wait, Turn, ...) of the normal game logic; the walk dust
    // controller (0x21b2c4) on the Move blend weights runs after the action keepers (in eft.tick).
    // The companion's rail clips (Walk, Wait, ...) are actions of her NPC effect entries (e.g. Walk ->
    // PlayerWalkSpeedy at AllRoot one tick after the clip starts: eft-create-route-a call 91).
    if (game.companion.enabled && !(g.active() && g.companion.running())) {
        const auto& t = game.companion.c;
        if (t.clipStarted != companionEffectClip) {
            companionEffectClip = t.clipStarted;
            eft.setAction("companion", t.clip, "KinopicoNpc", 0);
        }
    }
    const bool playerLogic = player.enabled && (!g.active() || g.playerMoves() || g.playerAnimates());
    if (playerLogic && player.anim.action != playerEffectAction) {
        if (!playerEffectAction.empty()) {
            eft.setAction("player", player.anim.action);
        }
        playerEffectAction = player.anim.action;
    }
    if (const char* mode = getenv("DIORAMA_DEBUG_FLOOR"); mode && player.enabled) {
        logFloor(!strcmp(mode, "2"));
    }
    // FlyWait: the flee nerve's step 0 plays it, one tick after the contact switched the nerve
    // (eft-create-route-a: BirdFly at calls 547/563, one tick after the native flee start).
    for (auto& b : birdFlock.birds) {
        const bool f = b.second.state == captain_toad::RecoveredBird::Bird::Flee;
        if (!f) {
            birdRestYaw[b.first] = b.second.yaw;
        }
        if (getenv("DIORAMA_DEBUG_BIRDYAW")) {
            fprintf(stderr, "tick %lld bird %zu state %d yaw %.3f flee %.3f %.3f %.3f\n", eft.ticks, b.first,
                    int(b.second.state), b.second.yaw, b.second.flee[0], b.second.flee[1], b.second.flee[2]);
        }
        if (f && birdFleeing[b.first] && !birdFlyWait[b.first]) {
            eft.setAction("bird" + std::to_string(b.first), "FlyWait");
            birdFlyWait[b.first] = true;
        }
        birdFleeing[b.first] = f;
    }
    // Placed objects: a clip started this tick is an action of the object's EffectYaml entry
    // (compared by start count: an action another object starts, as the plant starts its item's
    // PopUp, may already have aged in the same tick); hit reactions with an effect emit it at the
    // object.
    // The first pass takes the start counts as they are: the actions of the setup are not starts.
    const bool firstPass = objectActionSeen.size() != objectEffectOwner.size();
    objectActionSeen.resize(objectEffectOwner.size(), 0);
    objectAliveSeen.resize(objectEffectOwner.size(), 1);
    for (size_t i = 0; i < objectEffectOwner.size(); i++) {
        const auto& o = stageWorld.objects[i];
        const bool started = firstPass ? o.actionAge == 0 : o.actionStarts != objectActionSeen[i];
        objectActionSeen[i] = o.actionStarts;
        if (objectEffectOwner[i] && o.alive && started) {
            eft.setAction("obj:" + o.id, o.action, "", 0);   // the actor's own action: lag 0 (as the companion's clips)
        }
        // A killed actor's action effects stop (e.g. a looping run dust of a KyoroHei killed while
        // moving); a revived one (a pooled item) starts again with its next action.
        if (objectEffectOwner[i] && objectAliveSeen[i] && !o.alive) {
            eft.endActions("obj:" + o.id);
        }
        objectAliveSeen[i] = o.alive;
    }
    for (const auto& r : effectReactions) {
        const auto& o = stageWorld.objects[r.object];
        if (!objectEffectOwner[r.object]) {
            continue;
        }
        // The reaction's effects: its ActorHitReactionCtrl EffectName and the EffectYaml entries
        // listing it (HitReactionList), each once.
        const std::string owner = "obj:" + o.id;
        std::vector<std::string> names = eft.reactionEffects(owner, r.name);
        auto m = hitReactions.find(o.reactionTable);
        if (m != hitReactions.end()) {
            auto h = m->second.find(r.name);
            if (h != m->second.end() && !h->second.effect.empty() &&
                std::find(names.begin(), names.end(), h->second.effect) == names.end()) {
                names.push_back(h->second.effect);
            }
        }
        reactionAtObject[r.object] = r.hasAt;
        reactionAt = r.at;
        for (const auto& n : names) {
            eft.emit(owner, n, view);
        }
        reactionAtObject[r.object] = 0;
    }
    effectReactions.clear();
    eft.tick(view, [&] {
        if (player.enabled) {
            eft.walkTick(eft.owner("player"), playerLogic ? player.anim.action : std::string(), player.anim.weights,
                         &player.anim, view);
        }
    });
}

// Stage start: the atmosphere at the stage start; the Tutorial BGM at intro tick 100 (cold boot,
// sound-boot-trace-a: atmosphere at the stage start, CourseInRove voices, stream start ~100 ticks
// later). Without the intro the play starts in the opening checkpoint state, where both run.
// The atmosphere loop (preset 10); its handle arrives when the object starts (with the command
// latency the request waits a few frames; the object list may still be empty).
void Session::Impl::playAtmosphere() {
    atmosphereHandle = -1;
    snd->play("SeAt" + sndKeeper->atmosphere + "Lv", nullptr, 10, -1,
              [this](int handle) { atmosphereHandle = handle; });
}

void Session::Impl::startStageSound(bool withIntro) {
    if (!snd) {
        return;
    }
    soundStarted = true;
    if (!withIntro && haveCheckpointGameRng && !getenv("DIORAMA_SOUND_NO_RNG")) {
        memcpy(birdFlock.rng.s, checkpointGameRng, 16);
    }
    if (!stageBgm.empty()) {   // a course: its BGM (from the start without the intro)
        if (!withIntro) {
            snd->playStream(stageBgm, -1, 0);
        } else if (!intro.active()) {
            // A course without the ZoomIn demo: the scene's StageStart nerve has a BGM delay of 0
            // (0x147f68: only StageStartBindDemoZoomIn stages wait 100 steps), so the BGM is
            // requested on its first step.
            snd->requestStream(stageBgm, -1, 8);
        }
        if (!sndKeeper->atmosphere.empty()) {
            playAtmosphere();
        }
        if (!stageStartSound.empty() && withIntro) {
            sndKeeper->play(sndKeeper->owner("stagestart"), stageStartSound);
        }
        return;
    }
    if (!withIntro) {
        snd->playStream("Stm_Kp_bgm_Tutorial", -1, uint32_t(kTutorialAtCheckpoint));
    }
    playAtmosphere();
    if (withIntro) {
        return;
    }
    snd->scripts.update(1);   // the loop voice starts; place it at its checkpoint phase
    for (auto& v : snd->voices) {
        if (v.handle == atmosphereHandle) {
            auto& src = snd->mixer.sources[size_t(v.source)];
            src.cursor = uint32_t(kWindAtCheckpoint);
        }
    }
}

// Game-tick part (after effectsTick).
void Session::Impl::soundTick() {
    if (!snd) {
        return;
    }
    std::lock_guard<std::recursive_mutex> lk(snd->lock);
    const auto& g = game.goal;
    auto& K = *sndKeeper;
    K.stamp = soundFrameNo + 1;
    // Listener 0x5445c4: halfway between the camera target and eye (traced: the dumped listener
    // equals the midpoint at frame 2 of the opening checkpoint); view matrix 0x544594.
    // The goal and demo cameras put the listener at the eye (dump at frame 750: listener = eye).
    {
        const auto& v = game.director.view;
        const float r = (g.active() && (g.fixedCamera || g.demoCamera)) ? 1.0f : 0.5f;
        snd->listener = {v.target.x + r * (v.eye.x - v.target.x), v.target.y + r * (v.eye.y - v.target.y),
                         v.target.z + r * (v.eye.z - v.target.z)};
    }
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            snd->view.m[r][c] = game.view.m[r * 4 + c];
        }
    }
    // Player: footsteps (Move clips) and the action entries (goal clips while the sequence runs).
    auto& P = K.owner("player");
    if (introEnabled && intro.active()) {
        K.update(P, intro.action.name, intro.action.logicFrame(), 0);
        if (intro.bgmRequest) {
            if (stageBgm.empty()) {
                snd->requestStream("Stm_Kp_bgm_Tutorial", -1, 8);
            } else {
                snd->requestStream(stageBgm, -1, 8);
            }
        }
    }   // scene step 100 (StageStart.hpp)
    else if (g.active() && g.player.running()) {
        K.update(P, g.player.name, g.player.logicFrame(), 0);   // the keeper reads the action mirror
    } else if (player.enabled) {
        const std::string& a = player.anim.action;
        const float f = player.anim.currentFrame();
        // Footsteps (0x15db84) compare the Move clip frame of this tick with the previous one. The
        // code's sentinel previous frame (-1) would make frame 0 of a new Move clip a step; the
        // traced route has none there (sound-trace-a: the first step is the Turn entry's StepR),
        // so a new Move clip starts from its own frame (hypothesis; Move start phase not traced).
        if (a != playerSoundAction) {
            playerSoundAction = a;
            playerSoundPrev = f;
        }
        K.playerSteps(P, a, playerSoundPrev, f, player.anim.rate * 0.5f);
        // 0x29a394 (after the walk effects): the dash voice at walk level 3.
        if (eft.enabled && stageObjects) {
            const auto& W = eft.owner("player");
            const captain_toad::RecoveredVec::V3 d = captain_toad::RecoveredVec::sub(player.body.pos, dashPrevPos);
            const bool fast = std::sqrt(captain_toad::RecoveredVec::dot(d, d)) > 5.0f;
            if (W.walkLevel >= 0 && W.walkLevel <= 3 && W.walkType != 3) {
                if (fast && W.walkLevel == 3) {
                    if (!dashVoicePlayed) {
                        dashVoicePlayed = true;
                        K.play(P, "DashLoopStartPre");
                    }
                } else if (!(W.walkLevel == 3)) {
                    dashVoicePlayed = false;
                }
            }
        }
        dashPrevPos = player.body.pos;
        playerSoundPrev = f;
        // Action entries (Turn: StepR 5, StepL 20, ...) on the controller frame.
        K.update(P, a, player.anim.frame, player.anim.end);
        for (const auto& v : playerVoices) {   // carry voices (GrabHeavy, CarryUpThrow)
            K.play(P, v);
        }
    }
    playerVoices.clear();
    // The companion: rail clips (the NPC's Walk steps) and the goal clips.
    if (game.companion.enabled) {
        auto& T = K.owner("companion");
        // Goal clips: her keeper sees the clip one tick after the clock (sound-trace-a: both goal
        // voices one tick later than the clock's frame); the effects use lag 1 the same way.
        if (g.active() && g.companion.running()) {
            if (g.companion.name != companionGoalClip) {
                companionGoalClip = g.companion.name;
                companionGoalPrev = -2.f;
            }
            K.update(T, companionGoalClip, companionGoalPrev, 0);
            companionGoalPrev = g.companion.frame();
        } else {
            auto it = game.companion.clips.find(game.companion.c.clip);
            K.update(T, game.companion.c.clip, game.companion.frame,
                     it == game.companion.clips.end() ? 0.f : it->second.first);
        }
    }
    // Birds: GroundWaitA/B, Turn, FlyWait (Bird.byml); muted after the goal (VolumeAfterGoal 0).
    // The bird's clip frame counts 30 Hz ticks; SeDataBase frames are 60 Hz frames (as the effect
    // keeper's 2 * (age - lag)).
    for (auto& b : birdFlock.birds) {
        if (!b.second.dead) {
            K.update(K.owner("bird" + std::to_string(b.first)), b.second.clip, 2.f * b.second.clipFrame,
                     b.second.loop ? 2.f * b.second.clipEnd : 0.f);
        }
    }
    // Placed objects: their action entries on the clip frame, and the sound of their hit reactions.
    for (size_t i = 0; i < objectSoundOwner.size(); i++) {
        const auto& o = stageWorld.objects[i];
        if (!objectSoundOwner[i] || !o.alive || o.actionAge < 0) {
            continue;
        }
        auto& S = K.owner("obj:" + o.id);
        if (o.actionAge == 0) {   // a restarted action starts over (its level sounds go on)
            if (S.action == o.action) {
                S.fresh = true;
            } else {
                S.action.clear();
            }
        }
        if (o.nerveDriven) {   // named nerves: frame = step - 1 in steps of 2, none while step < 0
            if (o.nerveStep >= 0) {
                K.update(S, o.action, float(o.nerveStep - 1), 0.f, 2.f);
            }
            continue;
        }
        // The keeper's frame: the clip's (60 Hz frames), or two per tick for an action without one.
        const bool clip = o.anim.running() && o.anim.name == o.action;
        K.update(S, o.action, clip ? o.anim.frame : 2.f * float(o.actionAge), clip && o.anim.loop ? o.anim.end : 0.f);
    }
    for (const auto& [object, play, choice] : programSounds) {
        if (objectSoundOwner[object]) {
            K.play(K.owner("obj:" + stageWorld.objects[object].id), play, choice);
        }
    }
    programSounds.clear();
    for (const auto& r : soundReactions) {
        const auto& o = stageWorld.objects[r.object];
        if (!objectSoundOwner[r.object]) {
            continue;
        }
        // The reaction's sounds: its ActorHitReactionCtrl SeName and the SeDataBase
        // HitReactionInfoList PlayInfos, each once.
        auto& S = K.owner("obj:" + o.id);
        std::vector<std::string> plays;
        if (auto d = K.dbs.find(S.db); d != K.dbs.end()) {
            if (auto x = d->second.reactions.find(r.name); x != d->second.reactions.end()) {
                plays = x->second;
            }
        }
        auto m = hitReactions.find(o.reactionTable);
        if (m != hitReactions.end()) {
            auto h = m->second.find(r.name);
            if (h != m->second.end() && !h->second.sound.empty() &&
                std::find(plays.begin(), plays.end(), h->second.sound) == plays.end()) {
                plays.push_back(h->second.sound);
            }
        }
        if (r.hasAt) {   // the sound object stays where the reaction happened
            const captain_toad::RecoveredVec::V3 at = r.at;
            auto keep = S.position;
            S.position = [at] {
                return captain_toad::audio::Vec3f{at.x, at.y, at.z};
            };
            for (const auto& p : plays) {
                K.play(S, p);
            }
            S.position = keep;
        } else {
            for (const auto& p : plays) {
                K.play(S, p);
            }
        }
    }
    soundReactions.clear();
    // Camera movement (0x20bb08): |dV| + |dH| bands -> PgGyroMoving{Slow,Medium,Shoulder,Fast}.
    if (getenv("DIORAMA_SOUND_DEBUG") && game.companion.enabled) {
        if (game.companion.c.clip != debugCompanionClip) {
            debugCompanionClip = game.companion.c.clip;
            fprintf(stderr, "sound: companion clip %s frame %.1f\n", debugCompanionClip.c_str(), game.companion.frame);
        }
    }
    // The original's previous angles (+0x14c, +0x150) still hold the defaults when the area camera
    // takes over from the start camera (traced: one PgGyroMovingFast at the switch, frame 27).
    if (game.director.areaCam && game.director.current == game.director.areaCam) {
        const auto& cam = *game.director.areaCam;
        float s = std::fabs(cam.outV - cameraSoundLastV) + std::fabs(cam.outH - cameraSoundLastH);
        cameraSoundLastV = cam.outV;
        cameraSoundLastH = cam.outH;
        if (getenv("DIORAMA_SOUND_DEBUG") && s > 0.001f) {
            fprintf(stderr, "sound: %ld camera dV+dH %.3f (V %.2f/%.2f H %.2f/%.2f)\n", K.stamp, s, cam.outV, cam.prevV,
                    cam.outH, cam.prevH);
        }
        int band = s < 0.01f ? -1 : s <= 1.5f ? 0 : s < 2.0f ? 1 : s < 3.0f ? 2 : s > 6.0f ? 3 : -2;
        if (cameraDrag.enabled && (cameraDrag.turnedThisTick || cameraDrag.moving())) {
            band = -1;   // enhancement: the smooth drag turns quietly
        }
        if (band != -2 && band != cameraSoundBand) {
            if (band >= 0) {
                K.play(K.owner("camera"), band == 0   ? "PgGyroMovingSlow"
                                          : band == 1 ? "PgGyroMovingMedium"
                                          : band == 2 ? "PgGyroMovingShoulder"
                                                      : "PgGyroMovingFast");
            }
            cameraSoundBand = band;
        }
    }
    // Goal events (Goal.hpp nerve events; sound-trace-a / sound-dsp-a):
    if (g.active()) {
        if (g.getSound && goalStarObject >= 0) {
            // A course: the GoalItem's hit reaction 取得 (HrGot) and the course's BGM fade.
            stageWorld.reaction(stageWorld.objects[size_t(goalStarObject)], "取得");
            if (!stageBgm.empty()) {
                snd->fadeOutStream(stageBgm, 10);
            }
            for (auto& o : snd->objects) {
                if (o.handle == atmosphereHandle || o.preset == 10) {
                    o.volumeScale = 0.f;
                }
            }
        } else if (g.getSound) {
            K.play(K.owner("goalitem"), "HrGot");
            snd->fadeOutStream("Stm_Kp_bgm_Tutorial", 10);
            for (auto& o : snd->objects) {
                if (o.handle == atmosphereHandle || o.preset == 10) {
                    o.volumeScale = 0.f;
                }
            }
        }
        if (g.jingleRequest) {   // KinotanClear (a course: its BgmPlayInfo resource)
            snd->requestStream(clearJingle.empty() ? std::string("Ws_Kp_bgm_jg_Clear01") : clearJingle, -1, 8);
        }
        if (g.wipeSound) {
            K.play(K.owner("wipe"), "Close");
        }
        if (g.demoBgmRequest) {
            snd->requestStream("Stm_Kp_bgm_Demo_Op01", -1, 9);
        }
    }
    snd->tickObjects();
}

// Every 60 Hz frame (after the game tick on tick frames).
void Session::Impl::soundFrame() {
    if (snd) {
        snd->rng.clock = &soundFrameNo;
        snd->frameUpdate();
    }
    soundFrameNo++;
}

// =============================================================================================
// The game loop
// =============================================================================================
// One 60 Hz frame reads the controls; every second frame is a 30 Hz game tick. Order of a tick:
// camera director, player (or the goal sequence's scripted poses) - this order is traced in the
// original - then the companion's push on the player, NPCs, birds, goal sequence bookkeeping,
// effects and sound.
// One 60 Hz frame: input every frame, the 30 Hz game tick (camera, then player) on tick frames.
void Session::Impl::gameFrame(captain_toad::RecoveredCamera::CameraPad c, float sx, float sy) {
    if (!player.enabled) {
        return;
    }
    // Stage-start intro: a scripted camera and player pose; the controls are ignored.
    if (introEnabled && intro.active() && game.enabled) {
        if (snd && !soundStarted) {
            startStageSound(true);
        }
        if (!player.input(0, 0, false)) {
            soundFrame();
            return;
        }
        if (intro.update(game.startView.eye, game.startView.target, game.startView.fovy)) {
            player.place(player.body.pos, intro.shownFront);
            player.scriptedClip(intro.action.name, intro.action.frame());
            game.introCamera(intro);
        } else {
            game.endIntro();
            player.place(player.body.pos, intro.shownFront);
            player.anim.action = "";
            player.machine.current->action->enter();
            prevHeld = c.held;
        }
        // The course's actors run during the stage-start demo (only the protagonist is scripted):
        // otherwise they would show their rest pose on the first frame after it.
        if (stageObjects) {
            stageObjectsTick();
        }
        reportTick();
        effectsTick();
        soundTick();
        soundFrame();
        return;
    }
    // The protagonist runs while one of these buttons is held.
    const bool dash = (c.held & (captain_toad::RecoveredCamera::kA | captain_toad::RecoveredCamera::kB |
                                 captain_toad::RecoveredCamera::kY | captain_toad::RecoveredCamera::kZR)) != 0;
    if (snd && !soundStarted) {
        startStageSound(introEnabled);
    }
    // Without the intro the play starts in the reference checkpoint state (front-input-a's end, in
    // PullUpStage), where the course's objects have run for kCheckpointObjectTicks ticks already:
    // run them that long first (the patrolling enemy's phase; the player stands at the start).
    if (!introEnabled && !stageObjectsPreRolled && stageObjects) {
        stageObjectsPreRolled = true;
        if (stageWorld.stageName == kCheckpointStage && !getenv("DIORAMA_PLAYER_AT")) {   // not from a research start
            for (int k = 0; k < kCheckpointObjectTicks; k++) {
                stageObjectsTick();
            }
        }
    }
    walkStick(sx, sy);
    const uint32_t touchPress = touchButtons(sx, sy);
    if (!player.input(sx, sy, dash)) {
        soundFrame();
        return;
    }
    if (padTrace.active) {
        player.stickX = padTrace.get<float>(padCall, 0x11c);
        player.stickY = padTrace.get<float>(padCall, 0x120);
    }
    c.held |= touchPress;
    c.trig = c.held & ~prevHeld;
    prevHeld = c.held;   // the controller samples once per game tick
    // A hit stop (applySceneStop): the update 0x22e76c, then the query 0x221fec.
    if (sceneStop.mode > 0) {
        sceneStop.mode--;
    } else if (sceneStop.length > 0) {
        sceneStop.length--;
    }
    if (sceneStop.mode == 0 && sceneStop.length > 0) {
        soundFrame();
        return;
    }
    if (transformCount > 0) {   // the scene stands still while the power change flickers
        transformStep();
        soundFrame();
        return;
    }
    touchTick();
    reportTick();
    // 1. Camera: the goal sequence's demo or fixed camera, or the game camera.
    float cam[12];
    auto& goal = game.goal;
    if (game.goalEnabled) {
        goal.playerMini = stageWorld.playerPower == 1;
        goal.beginTick();
        courseGoalMirror();
    }
    if (goal.active()) {
        c = captain_toad::RecoveredCamera::CameraPad{};   // the goal sequence takes the controls
    }
    if (game.enabled) {
        if (goal.demoCamera) {
            game.demoCamera();
        } else if (goal.fixedCamera) {
            game.goalCamera();
        } else {
            cameraDragHandOver();
            game.cameraTick(c, player.body.pos, player.body.front, player.body.up);
        }
        game.padCamera(cam);
    } else {
        cameraMatrix(cam);
    }
    std::copy(cam, cam + 12, lastCam);
    // 2. Player: the normal action machine, or the goal sequence's animation / scripted poses.
    if (!goal.active() || goal.playerMoves()) {
        if (stageObjects && !playerBound) {
            stagePullRequest(c.trig);
            // The hold handler (0x15a8b8) runs with the sensor contacts before the player's update:
            // it sees last tick's clip frame (traced: the carry grab at 1237, not 1231).
            carryGrabFrame = player.machine.ctx.anim->isPlaying("PullOutSuccessCarry")
                                 ? player.machine.ctx.anim->currentFrame()
                                 : -1.0f;
        }
        if (game.enabled) {   // the Miss action turns toward the camera (0x1ed62c)
            player.machine.ctx.missLookTarget = game.director.view.eye;
            stageWorld.cameraEye = game.director.view.eye;
            stageWorld.startCameraView = game.director.current && game.director.current == game.director.startCam;
            stageWorld.guidesBlocked = goal.active() || (introEnabled && intro.active());
            player.machine.ctx.haveMissLookTarget = true;
        }
        if (playerBound) {   // bound by a ladder: the action machine does not run
            player.animationTick();
        } else {
            player.tick(cam, stageObjects);
        }
        if (stageObjects && !playerBound) {
            stagePullAfterTick();
            stageCarryTick(c.trig);
            // The model's movement after the player's update, so a clip a pull reply started
            // skips its first advance in this tick (traced: PullOutSuccess model frames 0, 2, ...
            // 8 from the reply's tick on, route pullup-pickaxe-a 1367-1375).
            player.modelMovement();
            stageHoldPlace();
        }
    } else if (goal.playerAnimates()) {
        player.animationTick();
    } else {
        if (goal.player.running()) {
            player.scriptedClip(goal.player.name, goal.player.frame());
        }
        if (goal.playerPlaced) {
            player.place(goal.p.position, goal.front());   // placed with the goal object's matrix (0x1df6a8)
        }
    }
    // 3. The companion pushes the player when they overlap (applied with the next tick's move).
    if (game.enabled && (!goal.active() || goal.playerMoves())) {
        captain_toad::RecoveredVec::V3 push;
        if (game.companionPush(player.body.pos, player.body.up, player.body.gravity, push)) {
            player.addPush(push);
        }
    }
    // 3b. Placed objects that push the player (the brigade NPCs' Body sensor, r90): the NPC's
    // attackSensor 0x1a3fd0 runs per overlapping sensor pair and sends the push (0x2f401c) when
    // its sensor is MapObj (type 0xb, 0x2f677c; sensor type table 0x4f8380) and the other is a
    // player sensor (types 1 / 2, 0x2fb1e0). The player's handler 0x1574dc recomputes each push
    // (0x2db770, at most 20, horizontal) and adds it to the collider's per-axis min / max
    // accumulator (0x2c53cc). The overlap is tested at the previous tick's positions and the push
    // computed with the current ones, as the companion's (Push.hpp).
    if (stageObjects && (!goal.active() || goal.playerMoves()) && !playerBound) {
        std::array<captain_toad::RecoveredVec::V3, 3> now;
        const auto& sensors = captain_toad::RecoveredStage::playerSensors(stageWorld.playerPower == 1);
        for (size_t k = 0; k < 3; k++) {
            now[k] = captain_toad::RecoveredVec::add(player.body.pos,
                                                     captain_toad::RecoveredVec::scale(player.body.up, sensors[k].up));
        }
        if (havePrevPlayerSensors) {
            for (size_t i = 0; i < stageWorld.objects.size(); i++) {
                const auto& o = stageWorld.objects[i];
                const auto* b = stageWorld.behaviours[i].get();
                const captain_toad::RecoveredStage::SensorDecl* body = b ? o.sensor(b->pushSensor()) : nullptr;
                if (!b || !body || !b->pushesPlayer(o)) {
                    continue;
                }
                const auto& m = o.world.m;
                const captain_toad::RecoveredVec::V3 centre{m[3] + m[1] * body->y, m[7] + m[5] * body->y,
                                                            m[11] + m[9] * body->y};
                for (size_t k = 0; k < 3; k++) {
                    const float r = sensors[k].radius;
                    captain_toad::RecoveredVec::V3 push;
                    if (captain_toad::RecoveredPush::overlaps(prevPlayerSensors[k], r, centre, body->radius) &&
                        captain_toad::RecoveredPush::push(now[k], r, centre, body->radius, player.body.gravity, 20.0f,
                                                          push)) {
                        player.addPush(push);
                    }
                }
            }
        }
        prevPlayerSensors = now;
        havePrevPlayerSensors = true;
    }
    // 4. NPCs and birds.
    if (game.enabled) {
        game.npcTick(player.body.pos);
    }
    if (game.enabled && !birdFlock.initial.empty()) {
        birdFlock.gameDriven = true;
        birdFlock.gameTick(player.body.pos, player.body.up);
    }
    // 5. The course's placed objects (stage packages).
    if (stageObjects && (!goal.active() || goal.playerMoves() || goalStarObject >= 0)) {
        stageObjectsTick();
        if (pendingScreenTouch >= 0) {   // the touch reaches its object after the objects ran
            touchTaken = stageWorld.screenTouch(pendingScreenTouch);
            pendingScreenTouch = -1;
        }
    }
    // 6. Goal sequence bookkeeping, then effects and sound for this tick.
    if (game.goalEnabled) {
        if (goalStarObject >= 0) {   // a course: the Head sensor of the player's power form (0x2da968)
            const auto& head = captain_toad::RecoveredStage::playerSensors(stageWorld.playerPower == 1)[2];
            goal.p.headOffset = head.up;
            goal.p.headRadius = head.radius;
        }
        goal.endTick(player.body.pos, player.body.up);
        if (goalStarObject >= 0 && !goal.active() && getenv("DIORAMA_TOUCH_GOAL")) {
            goal.contact = true;   // research: the course's goal demo without walking there
        }
    }
    if (stageGoal && !stageGoalReached) {
        stageGoalReached = game.goal.active();   // the Get (DIORAMA_TOUCH_GOAL: without walking there)
    }
    if (stageGoal && !stageMissed && player.enabled) {
        stageMissed = player.areas.deathAt(player.body.pos) || getenv("DIORAMA_FORCE_MISS") ||
                      (player.machine.current->name == "Miss" && player.anim.isEnd());
    }
    effectsTick();
    soundTick();
    soundFrame();
}

// Stage packages: the placed objects (scene.json objects) with their behaviours, the meshes and the
// player collider's parts that belong to each of them.
void Session::Impl::loadStageObjects() {
    captain_toad::data::JsonView list = scene["objects"];
    if (!list || !player.enabled) {
        return;
    }
    std::map<std::string, int> byId;
    for (captain_toad::data::JsonView o : list.items()) {
        captain_toad::RecoveredStage::Object obj;
        obj.id = o["id"].str();
        obj.cls = o["class"].str();
        obj.unit = o["unit"].str();
        obj.model = o["model"].str();
        for (int k = 0; k < 12; k++) {
            obj.placement.m[k] = o["matrix"][k].f();
        }
        obj.world = obj.placement;
        for (const std::string& key : o["params"].keys()) {
            captain_toad::data::JsonView v = o["params"][key];
            if (v.isString()) {
                obj.strings[key] = v.str();
            } else if (!v.isNull()) {
                obj.numbers[key] = v.d();
            }
        }
        for (const std::string& ln : o["links"].keys()) {
            for (captain_toad::data::JsonView t : o["links"][ln].items()) {
                captain_toad::RecoveredStage::LinkTarget lt;
                lt.id = t["id"].str();
                lt.unit = t["unit"].str();
                for (int k = 0; k < 12; k++) {
                    lt.placement.m[k] = t["matrix"] ? t["matrix"][k].f() : (k % 5 == 0 ? 1.f : 0.f);
                }
                for (captain_toad::data::JsonView pt : t["points"].items()) {
                    lt.points.push_back({pt[0].f(), pt[1].f(), pt[2].f()});
                }
                lt.closed = t["closed"].b();
                obj.links[ln].push_back(lt);
            }
        }
        for (captain_toad::data::JsonView sd : o["sensors"].items()) {
            captain_toad::RecoveredStage::SensorDecl d;
            d.name = sd["Name"].str();
            d.type = sd["Type"].str();
            d.radius = sd["Radius"].f();
            d.y = sd["Y"].f();
            d.z = sd["Z"] && !sd["Z"].isNull() ? sd["Z"].f() : 0.0f;
            d.joint = sd["Joint"].isString() ? sd["Joint"].str() : std::string();
            d.x = sd["X"] && !sd["X"].isNull() ? sd["X"].f() : 0.0f;
            if (sd["joint_rest"].isArray() && sd["joint_rest"].items().size() == 12) {
                d.hasJointRest = true;
                for (size_t k = 0; k < 12; k++) {
                    d.jointRest.m[k] = sd["joint_rest"][k].f();
                }
            }
            obj.sensors.push_back(d);
        }
        obj.effectUser = o["effect_user"].isString() ? o["effect_user"].str() : std::string();
        obj.soundUser = o["sound_user"].isString() ? o["sound_user"].str() : std::string();
        obj.reactionTable = o["reaction_table"].isString() ? o["reaction_table"].str() : obj.model;
        for (captain_toad::data::JsonView sd : o["screen_points"].items()) {
            captain_toad::RecoveredStage::SensorDecl d;
            d.name = sd["Name"].str();
            d.radius = sd["Radius"].f();
            d.y = sd["Y"].f();
            d.z = sd["Z"].f();
            obj.screenPoints.push_back(d);
        }
        if (captain_toad::data::JsonView ma = scene["material_animations"][obj.model]) {
            for (const std::string& file : ma["clips"].keys()) {
                obj.materialClips[file] = {ma["clips"][file]["frames"].f(), ma["clips"][file]["loop"].b()};
            }
            auto [it, fresh] = stageMaterials.try_emplace(obj.model);
            if (fresh) {
                it->second.rest.load(ma["rest"]);
                for (const std::string& file : ma["clips"].keys()) {
                    it->second.clips[file].load(ma["clips"][file]["materials"]);
                }
            }
            objectMaterials.resize(stageWorld.objects.size() + 1, nullptr);
            objectMaterials.back() = &it->second;
        }
        byId[obj.id] = int(stageWorld.objects.size());
        stageWorld.objects.push_back(std::move(obj));
    }
    objectActor.assign(stageWorld.objects.size(), -1);
    for (const auto& [actor, id] : actorObjectIds) {
        auto it = byId.find(id);
        if (it != byId.end()) {
            objectActor[size_t(it->second)] = int(actor);
            stageWorld.objects[size_t(it->second)].jointMatrix =
                [this, a = actor, i = it->second](const std::string& j, captain_toad::RecoveredStage::M34& m) {
                    // The object's recorded pose (Object::posed: last tick's clip, frame and
                    // placement), falling back to the last drawn pose when no clip ran.
                    const auto& o = stageWorld.objects[size_t(i)];
                    std::optional<Affine> joint;
                    if (o.posed.valid) {
                        Affine p;
                        std::copy(o.posed.world.m, o.posed.world.m + 12, p.m.begin());
                        const float* v = game.view.m;   // world -> view rows: the camera's axes
                        const float axes[9] = {v[0], v[1], v[2], v[4], v[5], v[6], v[8], v[9], v[10]};
                        joint = actors[a].jointAt(j, o.posed.clip, o.posed.frame, p, game.enabled ? axes : nullptr);
                    } else {
                        joint = actors[a].findJoint(j);
                    }
                    if (joint) {
                        std::copy(joint->m.begin(), joint->m.end(), m.m);
                    }
                    return joint.has_value();
                };
            stageWorld.objects[size_t(it->second)].clips = actorClips[actor];
            stageWorld.objects[size_t(it->second)].visibilityClips = actorVisibilityClips[actor];
        }
    }
    stageWorld.ray = [this](const captain_toad::RecoveredVec::V3& from, const captain_toad::RecoveredVec::V3& dir) {
        std::vector<captain_toad::RecoveredCollision::HitInfo> hits;
        captain_toad::RecoveredCollision::segmentCheck(player.collider.parts, from, dir, hits);
        int best = -1;
        float bestDepth = 1e30f;
        for (const auto& h : hits) {
            if (h.part >= 0 && size_t(h.part) < partObject.size() && h.depth < bestDepth) {
                bestDepth = h.depth;
                best = partObject[size_t(h.part)];
            }
        }
        return best;
    };
    stageWorld.playerClipFrames = [this](const std::string& name) {
        float frames = 0;
        bool loop = false;
        player.clipInfo(name, frames, loop);
        return frames;
    };
    stageWorld.segmentHits = [this](const captain_toad::RecoveredVec::V3& from,
                                    const captain_toad::RecoveredVec::V3& dir) {
        std::vector<captain_toad::RecoveredCollision::HitInfo> hits;
        captain_toad::RecoveredCollision::segmentCheck(player.collider.parts, from, dir, hits);
        return !hits.empty();
    };
    stageWorld.segmentFirst = [this](const captain_toad::RecoveredVec::V3& from,
                                     const captain_toad::RecoveredVec::V3& dir, captain_toad::RecoveredVec::V3& normal,
                                     float& distance) {
        std::vector<captain_toad::RecoveredCollision::HitInfo> hits;
        captain_toad::RecoveredCollision::segmentCheck(player.collider.parts, from, dir, hits);
        bool any = false;
        for (const auto& h : hits) {
            if (!any || h.depth < distance) {
                any = true;
                distance = h.depth;
                normal = h.faceNormal;
            }
        }
        return any;
    };
    stageWorld.sphereContacts = [this](const captain_toad::RecoveredVec::V3& centre, float radius,
                                       const captain_toad::RecoveredVec::V3& direction,
                                       std::vector<captain_toad::RecoveredCollision::HitInfo>& out) {
        captain_toad::RecoveredCollision::sphereCheck(player.collider.parts, centre, radius, direction, out, 32, true);
    };
    stageWorld.stageName = scene["asset_stage"].isString() ? scene["asset_stage"].str() : scene["name"].str();
    for (captain_toad::data::JsonView a : scene["stage_areas"].items()) {
        captain_toad::RecoveredStage::Area area;
        area.id = a["id"].str();
        area.kind = a["kind"].str();
        for (int k = 0; k < 12; k++) {
            area.matrix.m[k] = a["matrix"][k].f();
        }
        for (const std::string& key : a["params"].keys()) {
            area.numbers[key] = a["params"][key].d();
        }
        for (const std::string& ln : a["links"].keys()) {
            for (captain_toad::data::JsonView t : a["links"][ln].items()) {
                area.links[ln].push_back(t.str());
            }
        }
        stageWorld.areas.push_back(area);
    }
    // The touch hints' 2D actor sequence lengths (StageGuide.hpp).
    if (captain_toad::data::JsonView a2d = scene["actor2d"]["GUIDE_BALLOON"]["sequences"]) {
        for (const std::string& k : a2d.keys()) {
            stageWorld.actor2dFrames[k] = a2d[k]["frames"].i();
        }
    }
    stageWorld.create();
    guideSetup(scene["actor2d"]["GUIDE_BALLOON"]);
    for (const auto& [mesh, id] : meshObjectIds) {
        auto it = byId.find(id);
        if (it != byId.end()) {
            meshes[mesh].object = it->second;
        }
    }
    const captain_toad::data::Value playerDocument = captain_toad::data::readJson(scenePath / "player.json");
    captain_toad::data::JsonView parts = captain_toad::data::JsonView(playerDocument)["collision"]["parts"];
    partObject.assign(player.collider.parts.size(), -1);
    partSlot.assign(player.collider.parts.size(), -1);
    partMaterials.assign(player.collider.parts.size(), {});
    size_t k = 0;
    for (captain_toad::data::JsonView p : parts.items()) {
        if (k < partMaterials.size()) {
            for (captain_toad::data::JsonView m : p["materials"].items()) {
                partMaterials[k].push_back(m.str());
            }
        }
        if (k < partObject.size() && p["object"].isString() && byId.count(p["object"].str())) {
            partObject[k] = byId.at(p["object"].str());
            partSlot[k] = p["slot"] ? p["slot"].i() : -1;
            // TransparentWall registers its collision as TransparentWallPlayerOnly (0x19bc20).
            // Sub-actor collisions flagged player-only in the archive (StageExport "player_only").
            player.collider.parts[k].playerOnly =
                stageWorld.objects[size_t(partObject[k])].cls == "TransparentWall" || p["player_only"].b();
        }
        k++;
    }
    stageObjects = true;
}

void Session::Impl::skipIntro() {
    if (!player.enabled || !game.enabled || !intro.active()) {
        return;   // no stage-start demo: the player keeps the placement's front
    }
    captain_toad::RecoveredStageStart::Demo t = intro;
    while (t.update(game.startView.eye, game.startView.target, game.startView.fovy)) {
    }
    player.place(player.body.pos, t.shownFront);
}

}   // namespace captain_toad::session
