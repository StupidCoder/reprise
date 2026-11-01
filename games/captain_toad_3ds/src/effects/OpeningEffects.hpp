// =============================================================================================
// OpeningEffects.hpp - the particle effects of the opening, wired to the actors
// =============================================================================================
//
// Ties the pieces together: the recovered effect runtime (Eft.hpp), driven by the recovered
// keeper (EffectKeeper.hpp) from the actors' EffectYaml entries, and drawn with the ROM's shaders
// and materials (EftRenderer.hpp).
//
// How effects get started
// -----------------------
//   * Action triggers. Each EffectYaml entry lists the actions (animations) it belongs to, with
//     a StartFrame. The entry's StartFrame is compared with the actor's effect frame
//     2 * (clip age - lag), where the clip age counts ticks from the action start (age 0). The
//     lag is 1 for actions started by the goal / demo logic and 0 for the companion's rail clips
//     (eft-create-route-a call 91). StartFrame -1 creates on the action's start tick. These
//     rules reproduce all 26 creations of the goal sequence and the opening demo traced in
//     evidence/runs/eft-create-b.
//   * EndFrame fades the set (it stops emitting and its emitters fade out, e.g. Sweat in
//     eft-setmtx-a), as does an action change for entries without KeepEmitter.
//   * Code-triggered sets (entries without an ActionList) are created by their owner's logic
//     (the headlamp; the birds' FlyWait is an action of the bird nerve).
//   * Floor-material variants: an entry may name different resources per floor material (grass
//     kicked up only on the lawn); a material change swaps them while the action runs.
//
// Per game tick: action triggers, the keeper's follow of every live set, then the runtime calc of
// the draw groups in the order 0, 2, 1, 3 (the 0x352cd4 call order).
// =============================================================================================
#pragma once
#include "render/EftRenderer.hpp"
#include "effects/EffectKeeper.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <vector>

struct OpeningEffects {
    using Entry = captain_toad::RecoveredEffectKeeper::Entry;
    using Sources = captain_toad::RecoveredEffectKeeper::Sources;
    using Mtx34 = captain_toad::RecoveredEft::Mtx34;

    bool enabled = false;
    captain_toad::RecoveredEft::Resource res;
    captain_toad::RecoveredEft::Tables tab;
    captain_toad::RecoveredEft::System sys;
    EftRenderer renderer;
    std::map<std::string, std::vector<Entry>> yaml;                 // actor EffectYaml name -> entries
    std::vector<captain_toad::RecoveredEft::DrawBlock> blocks[4];   // last calc's draw blocks per group
    // Enhancement (drawing between ticks, session Presentation.hpp): the tick before's draw blocks
    // with their particles' ages, and the blend drawn instead of `blocks` while `blendAlpha` < 1.
    struct PreviousBlock {
        captain_toad::RecoveredEft::DrawBlock block;
        float age = 0;
    };
    std::vector<PreviousBlock> previous[4];
    std::vector<captain_toad::RecoveredEft::DrawBlock> blended[4];
    bool keepPrevious = false;   // record the previous blocks at each tick
    float blendAlpha = 1;
    long long ticks = 0;

    // A created emitter set and the entry it came from. The set pointer is only valid while the
    // set's creation id still matches (pool slots are reused).
    struct Instance {
        captain_toad::RecoveredEft::EmitterSet* set = nullptr;
        int createId = -1;
        const Entry* entry = nullptr;
        int actionIndex = -1;   // the action that started it (-1: code-triggered)
        bool faded = false;
        std::string resource;

        bool alive() const { return set && set->alive && set->createId == createId; }
    };

    // An effect owner: its EffectYaml, its sources (position, joint / base matrices) and its action.
    struct Owner {
        std::string name;
        std::string yamlName;
        std::function<captain_toad::RecoveredEffectKeeper::V3()> position;
        std::function<bool(const std::string& joint, Mtx34& m)> matrix;   // joint "" = base matrix
        std::function<std::string()> material;   // floor MaterialCode under the owner ("" = none)
        int walkType = 3;                        // 0x21b2c4 state (+0x20): row of the walk effect names, 3 none
        int walkLevel = -1;                      // (+0x24)
        const void* walkAnim = nullptr;          // (+0x1c)
        std::string action;
        int age = -1;   // ticks since the action started
        int lag = 1;    // effect frame = 2 (age - lag): 1 when the action starts after the actor's keeper update
        // Actors with named nerves: the frame is the nerve's step - 1 (0x24cd6c), none while the
        // step is negative; entries fire when it passes StartFrame within the last 2 frames
        // (0x2b6760 / 0x2958e8: StartFrame >= 0, frame - 2 < StartFrame <= frame).
        std::function<int()> nerveStep;
        std::vector<Instance> live;

        // Material-dependent entries started by the current action with their chosen resource
        // (including "DummyEmitter", which creates no set): a floor-material change swaps them.
        struct MaterialEntry {
            const Entry* entry;
            int actionIndex;
            std::string resource;
        };
        std::vector<MaterialEntry> materialActive;
    };
    std::vector<Owner> owners;
    std::vector<std::string> floorMaterials;   // terrain prism attribute -> MaterialCode
    uint32_t checkpointXs[4] = {};             // eft xorshift state at the opening checkpoint
    bool haveCheckpointXs = false;

    // Floor-material swaps (MaterialType resources), on unless DIORAMA_EFT_NO_MATERIALS: the
    // owner's material is the attribute of the player collider's winning ground offer
    // (MaterialCode read in 0x1832b4 / 0x2c4410; sound-material-a). PlayerMoveAddGrassLoop then
    // starts at calls 33 / 359 / 489 of route-goal-a against the traced 33 / 361 / 487.
    bool materialSwaps = getenv("DIORAMA_EFT_NO_MATERIALS") == nullptr;

    // Map effect objects (EffectObj* units) and their scales.
    struct ObjectEffect {
        std::string owner;
        float scale[3];
        std::string appearSwitch;   // starts when this stage switch turns on (empty: at the stage start)
        bool started = false;
        bool areaCube = false;   // class EffectObjAreaCube (appear 0x24fb9c)
        bool isModel = false;    // generic EffectObj with IsModel: its appear 0x330de8 starts no effect
        float emitRatio = 1;     // EffectEmitRatio (read by the AreaCube init 0x24fcac)
    };
    std::vector<ObjectEffect> objects;

    // Parse one EffectYaml entry (BYML exported as json; flags are the string "o").
    static Entry parse(captain_toad::data::JsonView e) {
        Entry en;
        auto flag = [&](const char* k) {
            return e[k].is("o");
        };
        en.name = e["EffectName"].str();
        if (e["JointName"]) {
            en.joint = e["JointName"].str();
        }
        if (e["DrawCategory"]) {
            en.category = e["DrawCategory"].str();
        }
        for (captain_toad::data::JsonView r : (e["ResourceList"]).items()) {
            if (r["MaterialType"]) {
                en.materialResources.push_back({r["MaterialType"].str(), r["ResourceName"].str()});
            } else {
                en.resources.push_back(r["ResourceName"].str());
            }
        }
        for (captain_toad::data::JsonView a : (e["ActionList"]).items()) {
            captain_toad::RecoveredEffectKeeper::Action x;
            x.name = a["ActionName"].str();
            x.start = a["StartFrame"] ? a["StartFrame"].i() : -1;
            x.end = a["EndFrame"] ? a["EndFrame"].i() : -1;
            x.keep = a["KeepEmitter"].b();
            en.actions.push_back(x);
        }
        for (captain_toad::data::JsonView r : (e["HitReactionList"]).items()) {
            en.reactions.push_back(r["ReactionName"].str());
        }
        if (captain_toad::data::JsonView p = e["PosOffset"]) {
            en.posOffset = {p["X"].f(), p["Y"].f(), p["Z"].f()};
            en.hasPosOffset = true;
        }
        if (captain_toad::data::JsonView p = e["Rotate"]) {
            en.rotate = {p["X"].f(), p["Y"].f(), p["Z"].f()};
            en.hasRotate = true;
        }
        if (e["Scale"]) {
            en.scale = e["Scale"].f();
        }
        if (e["ParticleScale"]) {
            en.particleScale = e["ParticleScale"].f();
        }
        if (e["EmitRatio"]) {
            en.emitRatio = e["EmitRatio"].f();
        }
        en.billboard = flag("EmitBillboard");
        en.camera = flag("EmitCamera");
        en.deleteAtClipping = flag("DeleteAtClipping");
        en.ignoreRotate = flag("EmitIgnoreRotate");
        en.ignoreScale = flag("EmitIgnoreScale");
        en.followPos = flag("FollowPos");
        en.followMtx = flag("FollowMtx");
        en.needProgramInfo = flag("NeedProgramInfo");
        en.directPos = flag("DirectPos");
        en.emitRotate = flag("EmitRotate");
        en.emitScale = flag("EmitScale");
        en.followTrans = flag("FollowTrans");
        en.followRotate = flag("FollowRotate");
        en.followScale = flag("FollowScale");
        en.finish(flag("EmitTrans"));
        return en;
    }

    // Load the effect package: the PTCL resource, the runtime tables, the triggers per actor,
    // the renderer and the map's effect objects.
    void load(const std::filesystem::path& dir) {
        if (!std::filesystem::exists(dir / "effects/effects.json")) {
            return;
        }
        const captain_toad::data::Value metaDocument = captain_toad::data::readJson(dir / "effects/effects.json");
        captain_toad::data::JsonView meta = metaDocument;
        res.load(captain_toad::data::readBytes(dir / meta["ptcl"].str()));
        const captain_toad::data::Bytes tables = captain_toad::data::readBytes(dir / meta["tables"].str());

        // tables.bin: sin/cos (257 x 16), divide sets (2..32 points x 12), random unit and random
        // cube vectors (512 x 12 each), flicker (128 x 4).
        const uint8_t* t = tables.data();
        size_t o = 0;
        tab.sinCos.resize(257 * 4);
        memcpy(tab.sinCos.data(), t, 257 * 16);
        o += 257 * 16;
        for (int n : {2, 3, 4, 6, 8, 12, 20, 32}) {
            std::vector<captain_toad::RecoveredEft::V3> v(static_cast<size_t>(n));
            memcpy(v.data(), t + o, 12 * size_t(n));
            o += 12 * size_t(n);
            tab.divide.push_back(v);
        }
        tab.randomUnit.resize(512);
        memcpy(tab.randomUnit.data(), t + o, 512 * 12);
        o += 512 * 12;
        tab.randomCube.resize(512);
        memcpy(tab.randomCube.data(), t + o, 512 * 12);
        o += 512 * 12;
        tab.flicker.resize(128);
        memcpy(tab.flicker.data(), t + o, 512);
        o += 512;
        if (o < tables.size()) {   // the second divide table (2..64 points; packages since schema 40)
            for (int n = 2; n <= 64; n++) {
                std::vector<captain_toad::RecoveredEft::V3> v(static_cast<size_t>(n));
                memcpy(v.data(), t + o, 12 * size_t(n));
                o += 12 * size_t(n);
                tab.divideMany.push_back(v);
            }
        }
        if (o != tables.size()) {
            throw std::runtime_error("effect tables size");
        }
        sys.res = &res;
        sys.tab = &tab;

        for (captain_toad::data::JsonView m : meta["floor_materials"].items()) {
            floorMaterials.push_back(m.str());
        }
        if (captain_toad::data::JsonView x = meta["checkpoint_xorshift"]) {
            for (int k = 0; k < 4; k++) {
                checkpointXs[k] = uint32_t(strtoul(x[k].str().c_str(), nullptr, 16));
            }
            haveCheckpointXs = true;
        }
        if (captain_toad::data::JsonView c = meta["stage_color"]) {
            for (int k = 0; k < 4; k++) {
                sys.envColor0[k] = c[0][k].f();
                sys.envColor1[k] = c[1][k].f();
            }
        }
        captain_toad::data::JsonView triggers = meta["triggers"];
        for (const std::string& actor : triggers.keys()) {
            auto& list = yaml[actor];
            for (captain_toad::data::JsonView e : (triggers[actor]).items()) {
                list.push_back(parse(e));
            }
        }
        renderer.load(dir, meta, res);
        renderer.warm(res);

        // Map effect objects (EffectObj* units): owners whose base matrix is the placement
        // (rotation Z*Y*X in degrees + translation; the traced set matrix of EffectObjWindGrass in
        // eft-setmtx-a carries no scale). Their effect is created at the stage start.
        for (captain_toad::data::JsonView ob : (meta["objects"]).items()) {
            Owner owner;
            owner.name = ob["id"].str();
            owner.yamlName = ob["unit"].str();
            captain_toad::RecoveredEffectKeeper::V3 r{ob["rotate"][0].f() * 0.017453292f,
                                                      ob["rotate"][1].f() * 0.017453292f,
                                                      ob["rotate"][2].f() * 0.017453292f};
            Mtx34 m = captain_toad::RecoveredEffectKeeper::euler(r);
            const captain_toad::RecoveredEffectKeeper::V3 translate{ob["translate"][0].f(), ob["translate"][1].f(),
                                                                    ob["translate"][2].f()};
            captain_toad::RecoveredEffectKeeper::setTranslation(m, translate);
            owner.position = [translate] {
                return translate;
            };
            owner.matrix = [m](const std::string& j, Mtx34& out) {
                if (!j.empty()) {
                    return false;
                }
                out = m;
                return true;
            };
            ObjectEffect x{owner.name, {ob["scale"][0].f(), ob["scale"][1].f(), ob["scale"][2].f()}};
            x.appearSwitch = ob["switch_appear"].isString() ? ob["switch_appear"].str() : std::string();
            x.areaCube = ob["class"].isString() && ob["class"].str() == "EffectObjAreaCube";
            x.isModel = !x.areaCube && ob["is_model"] && ob["is_model"].b();
            if (ob["emit_ratio"]) {
                x.emitRatio = ob["emit_ratio"].f();
            }
            objects.push_back(x);
            owners.push_back(owner);
        }
        enabled = true;
    }

    // Stage start: the map objects' effects (their EffectYaml entry without actions, e.g.
    // EffectObjWindGrass "Wait"). EffectObjAreaCube's appear (0x24fb9c) gives the set the object
    // scale through 0x32c27c when it is not (1, 1, 1) and its EffectEmitRatio through 0x32c200
    // when it is not 1. Hypothesis (to verify against traced particles): the generic EffectObj
    // (init 0x330e7c, which reads neither) has its scale as the plain volume scale (+0x224).
    void startObjects(const Mtx34& view) {
        for (auto& x : objects) {
            if (x.appearSwitch.empty()) {
                startObject(x, view);
            }
        }
    }
    // Objects waiting for their SwitchAppear switch (one-way: an appeared object stays).
    void objectSwitches(const std::function<bool(const std::string&)>& switchOn, const Mtx34& view) {
        for (auto& x : objects) {
            if (!x.appearSwitch.empty() && !x.started && switchOn(x.appearSwitch)) {
                startObject(x, view);
            }
        }
    }
    void startObject(ObjectEffect& x, const Mtx34& view) {
        x.started = true;
        if (x.isModel) {
            return;   // 0x330de8: "Wait" only without IsModel (StartAction's effects not ported)
        }
        Owner& o = owner(x.owner);
        auto it = yaml.find(o.yamlName);
        if (it == yaml.end()) {
            return;
        }
        for (const auto& e : it->second) {
            if (!e.actions.empty()) {
                continue;
            }
            const size_t before = o.live.size();
            create(o, e, -1, view);
            for (size_t k = before; k < o.live.size(); k++) {
                captain_toad::RecoveredEft::EmitterSet& set = *o.live[k].set;
                if (!x.areaCube) {
                    for (int a = 0; a < 3; a++) {
                        set.volumeScale[a] = x.scale[a];
                    }
                    continue;
                }
                if (x.scale[0] != 1.0f || x.scale[1] != 1.0f || x.scale[2] != 1.0f) {
                    sys.setVolumeScale(set, {x.scale[0], x.scale[1], x.scale[2]});
                }
                if (x.emitRatio != 1.0f) {
                    sys.setEmitRatio(set, x.emitRatio);
                }
            }
        }
    }

    const Entry* entry(const std::string& yamlName, const std::string& effect) const {
        auto it = yaml.find(yamlName);
        if (it == yaml.end()) {
            return nullptr;
        }
        for (const auto& e : it->second) {
            if (e.name == effect) {
                return &e;
            }
        }
        return nullptr;
    }

    Owner& owner(const std::string& name) {
        for (auto& o : owners) {
            if (o.name == name) {
                return o;
            }
        }
        throw std::runtime_error("unknown effect owner " + name);
    }

    // The keeper sources of an owner for an entry (the joint matrix is written to `storage`).
    Sources sources(Owner& o, const Entry& e, Mtx34& storage, const Mtx34& view) {
        Sources s;
        s.position = o.position ? o.position() : captain_toad::RecoveredEffectKeeper::V3{};
        s.view = view;
        if (o.matrix && o.matrix(e.joint, storage)) {
            s.matrix = &storage;
        }
        return s;
    }

    // Resource choice by the owner's floor material: a ResourceList entry with that MaterialType
    // replaces the default ones (e.g. the protagonist's AddGrassLoop: DummyEmitter by default,
    // PlayerMoveAddGrassLoop on "Lawn"; traced creations in eft-create-route-a).
    static std::vector<std::string> resolve(const Entry& e, const std::string& material) {
        for (const auto& [materialType, resource] : e.materialResources) {
            if (materialType == material) {
                return {resource};
            }
        }
        return e.resources;
    }

    static bool materialDependent(const Entry& e) { return !e.materialResources.empty(); }

    // Create the sets of an entry for an owner.
    void create(Owner& o, const Entry& e, int actionIndex, const Mtx34& view) {
        Mtx34 m;
        const Sources s = sources(o, e, m, view);
        const std::string material = (materialSwaps && o.material) ? o.material() : std::string();
        if (materialDependent(e)) {
            const auto chosen = resolve(e, material);
            const std::string r0 = chosen.empty() ? std::string() : chosen[0];
            bool known = false;
            for (auto& active : o.materialActive) {
                if (active.entry == &e) {
                    active.resource = r0;
                    known = true;
                }
            }
            if (!known) {
                o.materialActive.push_back({&e, actionIndex, r0});
            }
        }
        for (const auto& r : resolve(e, material)) {
            auto it = res.setIndex.find(r);
            if (it == res.setIndex.end()) {
                continue;   // e.g. DummyEmitter: no set of that name
            }
            captain_toad::RecoveredEft::EmitterSet* set =
                captain_toad::RecoveredEffectKeeper::create(sys, e, it->second, s);
            o.live.push_back({set, set->createId, &e, actionIndex, false, r});
            if (getenv("DIORAMA_EFT_DEBUG")) {
                fprintf(stderr, "eft tick %lld create %s (%s/%s) set %d group %d\n", ticks, r.c_str(), o.name.c_str(),
                        e.name.c_str(), set->createId, set->group);
            }
        }
    }

    // Code-triggered creation of a named entry.
    void emit(const std::string& ownerName, const std::string& effect, const Mtx34& view) {
        Owner& o = owner(ownerName);
        if (const Entry* e = entry(o.yamlName, effect)) {
            create(o, *e, -1, view);
        }
    }

    void fade(Instance& i) {
        if (i.alive() && !i.faded) {
            i.set->fadeOut = true;
            i.faded = true;
        }
    }

    // The effect names of the owner's entry that a hit reaction creates (HitReactionList).
    std::vector<std::string> reactionEffects(const std::string& ownerName, const std::string& reaction) {
        std::vector<std::string> out;
        auto it = yaml.find(owner(ownerName).yamlName);
        if (it != yaml.end()) {
            for (const auto& e : it->second) {
                for (const auto& r : e.reactions) {
                    if (r == reaction) {
                        out.push_back(e.name);
                    }
                }
            }
        }
        return out;
    }

    void kill(const std::string& ownerName, const std::string& effect) {
        for (auto& i : owner(ownerName).live) {
            if (i.alive() && i.entry->name == effect) {
                sys.kill(i.set);
            }
        }
    }

    // The owner's actor died (makeActorDead): the sets its actions started fade (kept ones too)
    // and no action entries fire any more; sets of hit reactions (the death's own) play on.
    void endActions(const std::string& ownerName) {
        Owner& o = owner(ownerName);
        for (auto& i : o.live) {
            if (i.actionIndex >= 0) {
                fade(i);
            }
        }
        o.action.clear();
        o.materialActive.clear();
    }

    // Start an action on an owner (age 0 this tick); fades the previous action's non-kept sets.
    void setAction(const std::string& ownerName, const std::string& action, const std::string& yamlName = "",
                   int lag = 1) {
        Owner& o = owner(ownerName);
        for (auto& i : o.live) {
            if (i.actionIndex >= 0 && !i.entry->actions[size_t(i.actionIndex)].keep) {
                fade(i);
            }
        }
        if (!yamlName.empty()) {
            o.yamlName = yamlName;
        }
        o.action = action;
        o.age = -1;   // becomes 0 in the tick
        o.lag = lag;
        o.materialActive.clear();
    }

    // Floor-material change of a live material-dependent set: the old resource fades and the
    // new one starts (a change to the default DummyEmitter creates nothing: eft-create-route-a
    // shows PlayerMoveAddGrassLoop at each entry to the lawn and no DummyEmitter on leaving it).
    void materialTick(Owner& o, const Mtx34& view) {
        if (!o.material) {
            return;
        }
        const std::string material = o.material();
        std::vector<std::pair<const Entry*, int>> restart;
        for (auto& m : o.materialActive) {
            const auto want = resolve(*m.entry, material);
            const std::string w0 = want.empty() ? std::string() : want[0];
            if (w0 == m.resource) {
                continue;
            }
            for (auto& i : o.live) {
                if (i.alive() && !i.faded && i.entry == m.entry) {
                    fade(i);
                }
            }
            m.resource = w0;
            if (!(want.size() == 1 && want[0] == "DummyEmitter")) {
                restart.push_back({m.entry, m.actionIndex});
            }
        }
        for (auto& [e, a] : restart) {
            create(o, *e, a, view);
        }
    }

    // 0x21b2c4: the walk effect controller (per player tick) - the dust puffs at the
    // protagonist's feet, chosen by ground and speed.
    // Type 0 while the action is Move or MovePanic, 1 for MoveWater; otherwise the running walk
    // effect stops. The level is the dominant one of the four Move blend weights (first maximum).
    // A changed (type, level) stops the old effect and emits the new one (0x29a394); an unchanged
    // one is emitted again when the animation object changes (no new set while one is active:
    // emit is idempotent).
    void walkTick(Owner& o, const std::string& action, const float weights[4], const void* animId, const Mtx34& view) {
        // Names by type (table 0x4f6778: 0 land, 1 water, 2 ashore).
        static const char* names[3][4] = {{"WalkSlowly", "Walk", "WalkSpeedy", "Run"},
                                          {"WalkWaterSlowly", "WalkWater", "WalkWaterSpeedy", "RunWater"},
                                          {"AshoreRun", "AshoreRun", "AshoreDash", "AshoreDash"}};
        auto stop = [&](int type, int level) {
            if (level < 0 || level > 3 || type > 2) {
                return;
            }
            for (auto& i : o.live) {
                if (i.alive() && i.entry->name == names[type][level]) {
                    fade(i);
                }
            }
        };
        auto emitWalk = [&](int type, int level) {
            if (level < 0 || level > 3 || type > 2) {
                return;
            }
            const Entry* e = entry(o.yamlName, names[type][level]);
            if (!e) {
                return;
            }
            for (auto& i : o.live) {
                if (i.alive() && !i.faded && i.entry == e) {
                    return;
                }
            }
            create(o, *e, -1, view);
        };

        int type;
        if (action == "Move" || action == "MovePanic") {
            type = 0;
        } else if (action == "MoveWater") {
            type = 1;
        } else {
            stop(o.walkType, o.walkLevel);
            o.walkType = 3;
            o.walkLevel = -1;
            return;
        }
        int level = 0;
        float best = weights[0];
        for (int i = 1; i < 4; i++) {
            if (best < weights[i]) {
                best = weights[i];
                level = i;
            }
        }
        if (o.walkType == type && o.walkLevel == level) {
            if (o.walkAnim != animId) {
                emitWalk(type, level);
                o.walkAnim = animId;
            }
        } else {
            stop(o.walkType, o.walkLevel);
            o.walkLevel = level;
            o.walkType = type;
            emitWalk(type, level);
        }
    }

    // One game tick: action triggers, follow, calc. `view` is this tick's camera view matrix.
    // `afterActions` is owner logic that runs after the action keepers of the tick (the walk
    // effect controller 0x21b2c4 creates after the Move action's sets: eft-create-route-a call 15).
    void tick(const Mtx34& view, const std::function<void()>& afterActions = {}) {
        if (!enabled) {
            return;
        }

        // Action triggers.
        for (auto& o : owners) {
            if (o.action.empty()) {
                continue;
            }
            o.age++;
            const int step = o.nerveStep ? o.nerveStep() : 0;
            const bool nerve = bool(o.nerveStep);
            const int frame = nerve ? step - 1 : 2 * (o.age - o.lag);
            const int prev = frame - 2;
            const bool framed = nerve ? step >= 0 : o.age >= o.lag;
            auto it = yaml.find(o.yamlName);
            if (it == yaml.end()) {
                continue;
            }
            for (const auto& e : it->second) {
                for (size_t a = 0; a < e.actions.size(); a++) {
                    const auto& x = e.actions[a];
                    if (x.name != o.action) {
                        continue;
                    }
                    const bool start = x.start < 0 ? o.age == 0 : (framed && prev < x.start && x.start <= frame);
                    if (start) {
                        create(o, e, int(a), view);
                    }
                    if (x.end >= 0 && frame >= x.end && framed) {
                        for (auto& i : o.live) {
                            if (i.entry == &e && i.actionIndex == int(a)) {
                                fade(i);
                            }
                        }
                    }
                }
            }
        }
        if (afterActions) {
            afterActions();
        }
        if (materialSwaps) {
            for (auto& o : owners) {
                materialTick(o, view);
            }
        }

        // Keeper follow.
        for (auto& o : owners) {
            for (auto& i : o.live) {
                if (!i.alive()) {
                    continue;
                }
                Mtx34 m;
                const Sources s = sources(o, *i.entry, m, view);
                captain_toad::RecoveredEffectKeeper::follow(*i.set, *i.entry, s);
            }
        }

        // Research: per-tick set matrices (compare with eft-setmtx-a).
        if (const char* path = getenv("DIORAMA_EFT_SETS")) {
            static FILE* f = fopen(path, "w");
            for (auto& o : owners) {
                for (auto& i : o.live) {
                    if (!i.alive()) {
                        continue;
                    }
                    fprintf(f, "%lld %s %d SRT", ticks, res.sets[size_t(i.set->setIndex)].name.c_str(), i.createId);
                    for (float v : i.set->mtxSRT) {
                        fprintf(f, " %.4f", v);
                    }
                    fprintf(f, " RT");
                    for (float v : i.set->mtxRT) {
                        fprintf(f, " %.4f", v);
                    }
                    fprintf(f, "\n");
                }
            }
            fflush(f);
        }

        for (auto& o : owners) {
            o.live.erase(std::remove_if(o.live.begin(), o.live.end(), [](const Instance& i) { return !i.alive(); }),
                         o.live.end());
        }

        if (keepPrevious) {
            for (int g = 0; g < 4; g++) {
                previous[g].clear();
                for (const auto& b : blocks[g]) {
                    previous[g].push_back({b, b.particle ? b.particle->age : 0.0f});
                }
            }
        }
        // Runtime calc per draw group, in the original call order.
        for (int g : {0, 2, 1, 3}) {
            blocks[g].clear();
            sys.out = &blocks[g];
            sys.calcGroup(g, 1.0f);
        }

        // Research: the live (not fading) sets of one owner every 30 ticks.
        if (const char* f = getenv("DIORAMA_EFT_OWNER"); f && ticks % 30 == 0) {
            int n = 0;
            for (auto& i : owner(f).live) {
                n += i.alive() && !i.faded;
            }
            fprintf(stderr, "owner tick %lld %s live %d\n", ticks, f, n);
        }
        // Research: per-tick particle count of one emitter name.
        if (const char* f = getenv("DIORAMA_EFT_COUNT")) {
            int n = 0;
            for (int g = 0; g < 4; g++) {
                for (auto& b : blocks[g]) {
                    if (res.name(b.emitter->data.u(0x38)) == f) {
                        n++;
                    }
                }
            }
            if (n) {
                fprintf(stderr, "count tick %lld %s %d\n", ticks, f, n);
            }
        }
        sys.out = nullptr;
        ticks++;
    }

    // Enhancement: the draw blocks between the last two ticks at `alpha` (0: the tick before, 1:
    // the last). A particle drawn in both ticks (the same pool slot, emitter and an age that went
    // on) moves, turns, scales and fades between its two states; one that is new shows as it is.
    // Not blended: texture pattern cells (they change in steps), mirror flags, stripes (drawn
    // from the emitter's history).
    void blend(float alpha, const std::function<void(const float*, const float*, float, float*)>& matrixBlend) {
        blendAlpha = alpha;
        if (alpha >= 1) {
            return;
        }
        auto mix = [&](const float* a, const float* b, float* out, int n, float limit) {
            for (int k = 0; k < n; k++) {
                const float d = b[k] - a[k];
                out[k] = std::fabs(d) <= limit ? a[k] + d * alpha : b[k];
            }
        };
        for (int g = 0; g < 4; g++) {
            std::unordered_map<const captain_toad::RecoveredEft::Particle*, const PreviousBlock*> before;
            for (const auto& p : previous[g]) {
                if (p.block.particle && !p.block.stripe) {
                    before[p.block.particle] = &p;
                }
            }
            blended[g] = blocks[g];
            for (auto& b : blended[g]) {
                auto it = b.particle && !b.stripe ? before.find(b.particle) : before.end();
                if (it == before.end()) {
                    continue;
                }
                const auto& a = it->second->block;
                if (a.emitter != b.emitter || a.child != b.child || !(b.particle->age > it->second->age)) {
                    continue;   // the slot holds another particle now
                }
                constexpr float kFar = 1e30f;
                mix(a.color, b.color, b.color, 4, kFar);
                mix(a.scaleRot, b.scaleRot, b.scaleRot, 4, kFar);
                mix(a.tex0, b.tex0, b.tex0, 4, 0.1f);   // a cell change is a step
                mix(a.tex1, b.tex1, b.tex1, 4, 0.1f);
                mix(a.pos, b.pos, b.pos, 3, 400.0f);
                mix(a.dir, b.dir, b.dir, 3, kFar);
                mix(a.rot, b.rot, b.rot, 3, 1.0f);
                captain_toad::RecoveredEft::Mtx34 m;
                matrixBlend(a.mtx.data(), b.mtx.data(), alpha, m.data());
                b.mtx = m;
            }
        }
    }
    const std::vector<captain_toad::RecoveredEft::DrawBlock>& drawn(int group) const {
        return blendAlpha < 1 ? blended[group] : blocks[group];
    }

    // Draw the 3D groups. proj: 4x4 row-major GL clip; view: 3x4 row-major; eye: camera world position.
    void draw3D(const float proj[16], const float view[12], const float eye[3]) {
        for (int g : {0, 2, 1}) {
            renderer.draw(drawn(g), proj, view, eye);
        }
    }

    // The 2D group (1280 x 720 virtual screen, identity view; traced c86..c92 of the 2D draws).
    void draw2D() {
        static const float proj[16] = {1.0f / 640, 0, 0, 0, 0, 1.0f / 360, 0, 0, 0, 0, -0.0002f, 0, 0, 0, 0, 1};
        static const float view[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        static const float eye[3] = {0, 0, 0};
        renderer.draw(drawn(3), proj, view, eye);
    }
};
