// =============================================================================================
// SessionCourse.cpp - the player's interplay with a course's placed objects
// =============================================================================================
//
// The session side of the course mechanics that join the protagonist and the stage objects
// (game/StageObjects.hpp and its class headers): the goal demo's star objects, hit stops, the
// object tick and its inputs, the power forms and their flicker, damage and the blink, the
// invincible pickaxe, pulling and grabbing, carrying and throwing, the touch screen and tap to walk.
// Each function's comment names the original routines it follows.
// =============================================================================================
#include "session/SessionState.hpp"

namespace captain_toad::session {

// A course's goal: the sequence's star and demo star clips on the stage objects GoalItem and
// GoalDemoStar (started when the sequence starts them; the star dies at its clip's end, the demo
// star appears at GoalPose step 0).
void Session::Impl::courseGoalMirror() {
    const auto& g = game.goal;
    if (goalStarObject < 0 || !g.active()) {
        return;
    }
    auto& star = stageWorld.objects[size_t(goalStarObject)];
    auto& demo = stageWorld.objects[size_t(goalDemoObject)];
    if (g.star.running() && g.star.age == 0) {
        star.startAction(g.star.name);
    }
    if (!g.starVisible) {
        star.visible = false;
        star.alive = false;
    }
    if (g.demoStarVisible && !demo.alive) {
        demo.alive = true;
        demo.visible = true;
    }
    if (g.demoStar.running() && g.demoStar.age == 0) {
        demo.startAction(g.demoStar.name);
    }
}

// The reaction controller's scene stop (0x28a0a0 -> 0x22e73c). The stop object holds a length
// [0] and a mode [1]: a reaction with StopSceneFrame above 0 keeps the longer length, and sets the
// mode (2 with IsStopSceneForHitEffect, else 0) when no stop is running. Its update 0x22e76c
// counts the mode down first, then the length; the scene is stopped while the mode is 0 and the
// length above 0 (0x221fec; the player skips his update then, 0x15c5e4). Counted once per game
// tick at its start, before the check (traced: 0x22e76c every tick; 引っこ抜き成功, 3 / mode 0,
// started at 1375, holds 1377 and 1379 of route pullup-pickaxe-a; the pickaxe's 無敵アタックヒット,
// 2 / mode 2, holds 2 ticks after 2 running ticks; runs hc6 / hc1).
void Session::Impl::applySceneStop(const std::string& table, const std::string& reaction) {
    if (auto t = hitReactions.find(table); t != hitReactions.end()) {
        if (auto e = t->second.find(reaction); e != t->second.end() && e->second.stopFrames > 0) {
            if (sceneStop.length <= 0 && sceneStop.mode <= 0) {
                sceneStop.mode = e->second.stopForHitEffect ? 2 : 0;
            }
            sceneStop.length = std::max(sceneStop.length, e->second.stopFrames);
        }
    }
}

// One tick of the placed objects after the player moved; dead objects lose their collision.
void Session::Impl::stageObjectsTick() {
    const std::string& st = player.machine.current->name;
    // The player model's movement (0x160a8c): PullOutSuccess or PullOutSuccessCarry at frame 8
    // plays the player's reaction 引っこ抜き成功 (its table in Kinopio.szs).
    if ((player.anim.action == "PullOutSuccess" || player.anim.action == "PullOutSuccessCarry") &&
        player.anim.frame == 8.0f) {
        applySceneStop("Player", "引っこ抜き成功");
    }
    stageWorld.stickX = player.stickX;
    stageWorld.stickY = player.stickY;
    stageWorld.dashHeld = player.dashHeld;
    stageWorld.playerOnGround = player.collider.onGround();
    stageWorld.playerFree = st == "Walk" || st == "Wait" || st == "Idle" || st == "Turn" || st == "Pivot" ||
                            st == "RunTurn" || st == "RunSign" || st == "DashWait";
    stageWorld.playerDown = st == "Miss" || st == "DeathAreaFall";
    stageWorld.playerDamageable = damageInvalid <= 1 && !player.machine.ctx.invincibleFlag && !stageWorld.playerDown;
    stageWorld.playerInvincible = player.machine.ctx.invincibleFlag;
    stageWorld.headlamp.on = lightService.headlampEnabled && lightService.records.count("headlamp");
    for (int k = 0; k < 3; k++) {
        (&stageWorld.headlamp.pos.x)[k] = float(lightService.headlampPosition[size_t(k)]);
        (&stageWorld.headlamp.dir.x)[k] = float(lightService.headlampBeam[size_t(k)]);
    }
    stageWorld.headlamp.length = float(lightService.headlampEnd);
    stageWorld.tick({player.body.pos, player.body.up, player.body.front, player.body.vel, stageWorld.playerPower == 1});
    // The protagonist and the water: the wet state (0x15d118) and the surface detector (0x20eacc).
    if (!player.areas.water.empty()) {
        const auto inWater = [this](const captain_toad::RecoveredVec::V3& p) {
            return player.areas.waterAt(p);
        };
        playerWet.tick(player.body.pos, player.body.up, stageWorld.playerPower == 1, inWater);
        // The model's "WetStart" / "WetEnd" (colour clips, 60 Hz frames, held at their end).
        protagonistColor.update();
        for (const auto& [event, clip] :
             {std::pair{playerWet.startEffects, "WetStart"}, std::pair{playerWet.stopEffects, "WetEnd"}}) {
            auto c = protagonistMaterials.clips.find(clip);
            if (event && c != protagonistMaterials.clips.end()) {
                protagonistColor.start(clip, c->second.length(), false);
                if (getenv("DIORAMA_WET_DEBUG")) {
                    fprintf(stderr, "wet: %s (%.0f frames)\n", clip, c->second.length());
                }
            }
        }
        std::vector<captain_toad::RecoveredPlayerWater::WaterBox> boxes;
        for (const auto& a : player.areas.water) {
            boxes.push_back({captain_toad::RecoveredVec::add(a.translate, a.min),
                             captain_toad::RecoveredVec::add(a.translate, a.max)});
        }
        if (havePrevWaterPoint) {
            captain_toad::RecoveredVec::V3 at;
            const std::string r =
                captain_toad::RecoveredPlayerWater::surfaceCrossing(boxes, prevWaterPoint, player.body.pos, at);
            if (!r.empty()) {
                playerReactions.push_back({r, true, at});
            }
        }
        prevWaterPoint = player.body.pos;
        havePrevWaterPoint = true;
    }
    // The body hit plays the player's 無敵アタックヒット (HitInvincible, StopSceneFrame 2 with
    // the hit-effect delay; traced: the player starts that stop at the knock-away, 1453).
    for (size_t i = 0; i < stageWorld.invincibleBodyHits.size(); i++) {
        playerEffects.push_back("HitInvincible");
        applySceneStop("Player", "無敵アタックヒット");
    }
    for (const auto& push : stageWorld.playerPushes) {   // enemies' pushes (0x2db770), with the next move
        player.addPush(push);
    }
    // The player bind (ladders): place him and play the binder's clip; the release places him
    // once more where the binder left him.
    if (stageWorld.bind.active || playerBound) {
        player.place(stageWorld.bind.pos, stageWorld.bind.front);
        if (stageWorld.bind.active) {
            player.boundClip(stageWorld.bind.clip, stageWorld.bind.frame);
        } else {
            // Released: walking on (the binder's 0x335d38 posts a walk request to the player's
            // controller: flag +0x114, the stick at +0x118 / +0x11c), he moves at once at the
            // walk maximum NormalSpeedMaxGround (+0xb0; Mini +0xb4), as traced after the ladder's
            // top exit (run ladA frames 1192-1204). Where the walk reads the request is not decoded.
            if (stageWorld.bind.fall) {   // release mode 5: the ground under him is gone
                stageWorld.bind.fall = false;
                // The collider did not run while he was bound: its ground contact is stale
                // (traced: the first free tick goes to the fall, run revolver-west-east 167).
                player.collider.clearContacts();
                // Hypothesis: the bound player is in a standing state (the bind's own state is not
                // ported), whose transition to the fall needs one airborne tick, not Walk's two.
                if (player.machine.current != player.machine.state("Wait")) {
                    player.machine.next = player.machine.state("Wait");
                    player.machine.apply();
                }
            }
            if (stageWorld.bind.walkOn) {
                const auto& wp = player.machine.ctx.walkParams;
                const float speed = stageWorld.playerPower == 1 ? wp.normalSpeedMaxGroundMini : wp.normalSpeedMaxGround;
                player.body.vel = captain_toad::RecoveredVec::scale(stageWorld.bind.front, speed);
            }
        }
    }
    playerBound = stageWorld.bind.active;
    playerPowerTick();
    for (size_t k = 0; k < partObject.size(); k++) {
        if (partObject[k] >= 0) {
            const auto& o = stageWorld.objects[size_t(partObject[k])];
            auto& part = player.collider.parts[k];
            // An object with several collision parts (a conveyor's blocks) places each slot itself.
            const int slot = k < partSlot.size() ? partSlot[k] : -1;
            const bool slotted = slot >= 0 && size_t(slot) < o.partWorlds.size();
            const auto& world = slotted ? o.partWorlds[size_t(slot)] : o.world;
            part.valid = o.alive && o.collision && (!slotted || o.partShown[size_t(slot)]);
            part.previousInverse = part.inverse;
            part.moved = false;
            if (memcmp(part.world.m, world.m, sizeof part.world.m) != 0) {   // a moving map part
                part.world = world;
                part.inverse = PlayerRuntime::inverseAffine(world);
                part.moved = true;
            }
        }
    }
}

// The player's power form (+0xd0 -> +0x44: 0 Super, 1 Mini) and the damage (the Miss
// condition's object 0x334e7574, vtable 0x4c69cc):
//   an enemy attack while invulnerable is ignored (the invulnerability object 0x4cbef8: its
//   counter, set to DamageInvalidCount 90 by 0x1bcb38, counted down each tick by 0x1feab4 in the
//   context update 0x3b9b40, or the invincible flags);
//   at Super (0x1bcb68): the voice "DamageS", the event onDamage (0x4399a4: the carried object is
//   dropped, hit reaction ダメージヒット: effect HitDamage), the successor form (0x1e5774 with the
//   table 0x4f66fc: Super -> Mini, Mini -> Mini); the context update applies it (0x2d7348: sound
//   "PowerDown" from the table 0x4f66f4, the current form changes) and the model container
//   switches to KinopioMini (0x1bd1c4; UserModelList);
//   at Mini: the Miss flag (+0x1c, damageFlag).
// The mushroom's power-up event (0x336f18) at Mini: Super again ("PowerUpNormal").
void Session::Impl::setPlayerForm(int form) {
    stageWorld.playerPower = form;
    for (auto* w : {player.machine.walk, player.machine.invincibleWalk}) {
        if (w) {
            w->walk.services.mode = form;   // *(walk+0x2c)
        }
    }
    showPlayerForm(form);
}

// The model shown (the model container switch 0x1bd1c4; the shared clips restart on the other
// model, 0x2d7930 -> 0x181980, and are retargeted for it).
void Session::Impl::showPlayerForm(int form) {
    player.form = form;
    setCharacterForm(form);
}

// The power change flicker (player +0x10c, 0x2d7d9c, run by the player update after the form is
// applied, 0x2d7348): a change of kind 2 (damage, 0x1e5774) or 3 (an item's power-up, 0x1e57bc)
// sets the counter to 0x3f; each tick it falls by 2 and shows the previous form while
// (counter & 7) < (counter >> 3), otherwise the new one; at 0 the new form stays. While it runs
// the scene stands still (the player update takes its stopped path 0x15c5e4 +0x195: only the
// form, the flicker and the matrices; observed: run hold, frames 1301-1361 after the hit of
// hit2.tsv, nothing but the protagonist changes on screen and the invulnerability count holds).
// The first step runs in the normal tick after the change; the next 31 ticks are frozen.
void Session::Impl::transformStep() {
    if (transformCount > 0) {
        transformCount--;
        int c = 0;
        if (transformCount != 0) {
            c = --transformCount;
        }
        showPlayerForm((c & 7) < (c >> 3) ? transformFrom : transformTo);
        if (transformCount != 0) {
            return;
        }
    }
    if (characterForm != transformTo) {
        showPlayerForm(transformTo);
    }
}

void Session::Impl::playerPowerTick() {
    // Kind 2 (damage) and kind 3 (the mushroom) changes flicker; the change applies in the next
    // tick's context update (0x2d7348), where the flicker starts.
    if (transformRequest) {
        transformRequest = false;
        transformCount = 0x3f;
        transformStep();
    }
    auto setForm = [this](int form, const char* sound) {
        transformFrom = characterForm;
        transformTo = form;
        const int shown = characterForm;
        setPlayerForm(form);
        showPlayerForm(shown);   // the flicker shows the new model
        transformRequest = true;
        playerVoices.push_back(sound);
    };
    if (damageInvalid > 0) {
        damageInvalid--;   // 0x1feab4
    }
    // The blink runs in the player update (0x15bf68), before an attack reaches the player.
    damageBlinkTick();
    const bool invulnerable = damageInvalid > 0 || player.machine.ctx.invincibleFlag;
    if (stageWorld.playerAttacked && !invulnerable) {
        if (stageWorld.playerPower == 0) {
            playerVoices.push_back("DamageS");
            playerEffects.push_back("HitDamage");
            if (heldObject >= 0) {   // onDamage 0x4399a4: the held object is let go
                if (auto* r = dynamic_cast<captain_toad::RecoveredStageRadish::Radish*>(
                        stageWorld.behaviours[size_t(heldObject)].get())) {
                    r->drop(stageWorld, stageWorld.objects[size_t(heldObject)]);
                }
                heldObject = -1;
                player.carryEnd();
            }
            damageInvalid = int(player.machine.ctx.params.damageInvalidCount);
            setForm(1, "PowerDown");
        } else {
            player.machine.ctx.damageFlag = true;
        }
    }
    if (stageWorld.powerUpRequest >= 0) {
        if (stageWorld.playerPower == 1 && stageWorld.powerUpRequest == 0) {
            setForm(0, "PowerUpNormal");
        }
        stageWorld.powerUpRequest = -1;
    }
    invincibleTick();
}

// The damage blink (player +0x110, update 0x1bc94c, run by the player update 0x15b698 after the
// context update): while the invulnerability object (ctx +0x3c) reports invulnerable (vt+0x1c
// 0x3ead04: its flags or the DamageInvalidCount counter), the model is hidden for the first 2
// of every 6 ticks (phase 0-1 hidden, 2-5 shown, phase + 1 mod 6). No blink, phase 0 and the model
// shown while invincible (the power-up object's flags +0x1c / +0x1d / +0x1e), in the states Miss
// and DeathAreaFall (0x2b1a90), while the power change flickers (the object +0x110 [1] is the
// flicker, player +0x10c: vtable 0x4c79b4 entry 0 0x3e5214 tests its counter +0x14) or the
// invulnerability object's +6 flag is set (not set in this course). The model is hidden through the player's hide request
// list (player +0xb4, 0x1edb90: any request hides, event 0x439fcc; none shows, 0x2f95e0).
// Observed (run blw / blww: frames 1395-1418 after the hit of hit2.tsv): the Mini model's hidden
// flag 0x32f0b241 is set for 4 of every 12 frames.
void Session::Impl::damageBlinkTick() {
    // 0x2b1a90: the current state is Miss (type 0x4fd76c, vtable 0x4ca8d4) or DeathAreaFall
    // (type 0x4fd6c4, vtable 0x4c6800).
    const std::string& state = player.machine.current ? player.machine.current->name : std::string();
    // The flicker excludes the blink (0x3e5214), so it starts with phase 0 (hidden) in the first
    // tick after the flicker (as observed, run hold).
    const bool blinking = damageInvalid > 0 && !player.machine.ctx.invincibleFlag && state != "Miss" &&
                          state != "DeathAreaFall" && transformCount == 0;
    bool hidden = false;
    if (!blinking) {
        blinkPhase = 0;
    } else {
        hidden = blinkPhase < 2;
        blinkPhase = (blinkPhase + 1) % 6;
    }
    if (hidden != characterHidden) {
        characterHidden = hidden;
        showCharacter();
        if (getenv("DIORAMA_DEBUG")) {
            fprintf(stderr, "player blink: %s (invulnerable %d)\n", hidden ? "hidden" : "shown", damageInvalid);
        }
    }
}

// The invincible mode (the pickaxe; the player's power-up object +0xd0 -> +0x5c, vtable 0x4ca9e8):
//   request 0x2a2ee8: the timer = InvincibleFrame (270); when not yet invincible: the flag +0x1c
//   (ctx.invincibleFlag, which takes every state to InvincibleWalk), the colour animation
//   "SuperStarStart", the action clip "Invincible" (anim +8), the invulnerability flag, the voice
//   "GetStar" (0x4397cc) and the music request (player +0x1c4: BGM "Invincible" =
//   Ws_Kp_bgm_Pick on the line Star, priority 4 over the stage's Main line);
//   update 0x2d74cc: at 30 left "PowerDown" and the music ends (0x439628 -> 0x2d8d90 ->
//   0x2b4de0 "Invincible"); at 0 "SuperStarEnd" and "Wait" when "Invincible" plays; the next
//   tick (the colour animation has ended) "Normal" and the flags off.
// While invincible the pickaxe hangs on HandR (InitPartsFixInfoPickaxe: no offset, the joint's
// scale kept) and attacks. Its sensor Pickaxe (EnemyAttack r90, Joint JointRoot, Z 120) sits at
// the joint's matrix times the offset (0x331638 -> 0x2fd284); JointRoot is off the model root by
// its rest translation. The player model's sensor handler 0x1601a4 sends the pickaxe attack
// 0x3381ac (tested by 0x2fc6d0) for that sensor (name index 0x25 in the table 0x4fd8e0): enemy
// bodies are knocked away (HeyhoRail 0x2f2aec) and blocks break (StageBlocks.hpp). The player's
// own contact sensors send 0x2d937c (message 0x4fe08c, tested by 0x2f29fc) to enemy bodies, with
// the objects' contacts (World::tick: the pickup's tick already counts).
// Reactions: 無敵アタックヒット on the pickaxe, and the player's own 無敵アタックヒット for a body hit
// (traced stop 2 / mode 2 from the player at 1453; 無敵タッチヒット, without a stop, is not used here).
// Hypothesis: the pickaxe sensor is valid for the whole mode (its validity during the swing is
// not traced).
// Not ported: the colour animations (the player's material animations are not played), the hit
// stop of 無敵アタックヒット (StopSceneFrame 2), InvincibleJumpPow.
void Session::Impl::invincibleTick() {
    using namespace captain_toad::RecoveredVec;
    auto& ctx = player.machine.ctx;
    if (stageWorld.invincibleRequest) {
        stageWorld.invincibleRequest = false;
        invincibleTimer = int(ctx.params.invincibleFrame);
        if (!ctx.invincibleFlag) {
            ctx.invincibleFlag = true;
            ctx.anim->start("Invincible");
            playerVoices.push_back("GetStar");
            if (snd) {
                if (!stageBgm.empty()) {
                    snd->pauseStream(stageBgm);   // the Main line paused under the Star line (0x32a3d8)
                }
                if (!invincibleBgm.empty()) {
                    snd->requestStream(invincibleBgm, -1, 0);
                }
            }
        }
    }
    if (ctx.invincibleFlag) {
        if (invincibleTimer > 0) {
            invincibleTimer--;
            if (invincibleTimer == 30) {
                playerVoices.push_back("PowerDown");
                if (snd) {
                    snd->setStreamScale(invincibleBgm, 0.0f);
                    if (!stageBgm.empty()) {
                        // The Main line resumes where it was, fading in over 0x14 frames
                        // (0x32a63c -> 0x378578): 10 game ticks, as the sound library's fader
                        // (0x37866c) is stepped once per 60 Hz frame (0x283a0c, twice a tick).
                        snd->resumeStream(stageBgm, 10);
                    }
                }
            }
            if (invincibleTimer == 0 && ctx.anim->isPlaying("Invincible")) {
                ctx.anim->start("Wait");
            }
        } else {
            ctx.invincibleFlag = false;   // "Normal"
        }
    }
    // The pickaxe on the hand.
    if (pickaxeObject == -2) {
        pickaxeObject = -1;
        for (size_t i = 0; i < stageWorld.objects.size(); i++) {
            if (stageWorld.objects[i].cls == "PlayerPickaxe") {
                pickaxeObject = int(i);
            }
        }
    }
    captain_toad::RecoveredStage::Object* pick =
        pickaxeObject >= 0 ? &stageWorld.objects[size_t(pickaxeObject)] : nullptr;
    if (pick) {
        // Appearing, it starts its action Wait: the EffectYaml entry Pickaxe/Wait (PickaxeWait, a
        // glow on the joint Pickaxe) comes from the action's trigger list (0x2b6760 -> 0x2f31b4 ->
        // 0x32c6a4; traced creation at 1455 of route pullup-pickaxe-a, the mode's first tick).
        // Hypothesis: the start is the sub-actor's appear (the call itself is not traced).
        if (ctx.invincibleFlag && !pick->alive) {
            pick->startAction("Wait");
        }
        pick->visible = ctx.invincibleFlag;
        pick->alive = ctx.invincibleFlag;
        if (!ctx.invincibleFlag) {
            // hidden: no placement (the pose may not exist yet)
        } else if (const auto hand = character.findJoint("HandR")) {
            for (int k = 0; k < 12; k++) {
                pick->world.m[k] = hand->m[size_t(k)];
            }
        }
    }
    if (!ctx.invincibleFlag) {
        return;
    }
    // The attacks.
    V3 pickCentre{};
    float pickRadius = 0;
    if (pick) {
        if (const auto* s = pick->sensor("Pickaxe")) {
            // The joint's matrix (the hand's, then JointRoot's rest offset) times the offset.
            V3 p{s->x, s->y, s->z};
            if (s->hasJointRest) {
                const auto& r = s->jointRest.m;
                p = {r[0] * p.x + r[1] * p.y + r[2] * p.z + r[3], r[4] * p.x + r[5] * p.y + r[6] * p.z + r[7],
                     r[8] * p.x + r[9] * p.y + r[10] * p.z + r[11]};
            }
            const auto& m = pick->world.m;
            pickCentre = {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
                          m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
            pickRadius = s->radius;
        }
    }
    for (size_t i = 0; i < stageWorld.objects.size(); i++) {
        auto& e = stageWorld.objects[i];
        auto* b = stageWorld.behaviours[i].get();
        if (!e.alive || !b || &e == pick) {
            continue;
        }
        for (const auto& s : e.sensors) {
            if (s.type == "MapObj" && pickRadius > 0) {
                // The pickaxe against map objects (blocks): 0x3381ac to their MapObj sensors.
                const V3 d = sub(captain_toad::RecoveredStage::World::sensorCentre(e, s), pickCentre);
                const float r = pickRadius + s.radius;
                if (dot(d, d) < r * r &&
                    b->pickaxeHit(stageWorld, e, {pickCentre, pickRadius, 0}, stageWorld.playerPower == 1)) {
                    if (getenv("DIORAMA_DEBUG")) {
                        const V3 sc = captain_toad::RecoveredStage::World::sensorCentre(e, s);
                        fprintf(stderr,
                                "pickaxe hit %s (%s), player at %.0f %.0f %.0f, pickaxe %.0f %.0f %.0f, sensor %.0f "
                                "%.0f %.0f\n",
                                e.id.c_str(), e.cls.c_str(), player.body.pos.x, player.body.pos.y, player.body.pos.z,
                                pickCentre.x, pickCentre.y, pickCentre.z, sc.x, sc.y, sc.z);
                    }
                    break;
                }
                continue;
            }
            if (s.type != "EnemyBody") {
                continue;
            }
            const auto& m = e.world.m;
            const V3 c{m[3] + m[1] * s.y, m[7] + m[5] * s.y, m[11] + m[9] * s.y};
            bool byPickaxe = false;
            if (pickRadius > 0) {
                const V3 d = sub(c, pickCentre);
                byPickaxe = dot(d, d) < (pickRadius + s.radius) * (pickRadius + s.radius);
            }
            // (The body's own attack is delivered with the objects' contacts: World::tick.)
            if (byPickaxe && b->thrownHit(stageWorld, e, player.body.pos, true)) {
                stageWorld.reaction(*pick, "無敵アタックヒット");
                break;
            }
        }
    }
}

// The grab from the ground: the player's attackSensor 0x15838c, for his Hold sensor (id 0xc)
// against another sensor, with an action-button press (controller wrapper vtable +0x24,
// 0x3e34dc: A, B, Y, ZR) calls the grab 0x2da15c (front test on, fifth argument 0). The grab
// refuses in some states (0x2d88c4: PullOutMissWait, Miss, DeathAreaFall, Slide and two others
// not ported), while pulling unless in PullOutSuccessCarry (0x2d9b0c), while bound and while
// holding; the object must lie ahead: (its centre - the Hold centre) . front + its radius >= 0.
// An accepted hold request (Radish in PullOut or Wait) starts the overhead carry with its lift
// clip (kind 1: CarryUpStart).
bool Session::Impl::stageGroundGrab() {
    using namespace captain_toad::RecoveredVec;
    auto& ctx = player.machine.ctx;
    if (heldObject >= 0 || pullTarget >= 0 || playerBound || !stageObjects) {
        return false;
    }
    const std::string& st = player.machine.current->name;
    if (st == "PullOutMissWait" || st == "Miss") {
        return false;
    }
    if (st == "PullOutStart" && !ctx.anim->isPlaying("PullOutSuccessCarry")) {
        return false;
    }
    const V3 hold = add(player.body.pos, scale(player.body.up, captain_toad::RecoveredStage::World::kHoldUp));
    for (size_t i = 0; i < stageWorld.objects.size(); i++) {
        auto* r = dynamic_cast<captain_toad::RecoveredStageRadish::Radish*>(stageWorld.behaviours[i].get());
        auto& o = stageWorld.objects[i];
        const auto* s = o.sensor("Hold");
        if (!r || !o.alive || !s || !r->grabbable()) {
            continue;
        }
        const V3 c = r->sensorCentre(*s);
        const V3 d = sub(c, hold);
        const float rr = captain_toad::RecoveredStage::World::kHoldRadius + s->radius;
        if (dot(d, d) >= rr * rr || dot(d, player.body.front) + s->radius < 0.0f) {
            continue;
        }
        if (r->grab(stageWorld, o)) {
            player.carryStart(1);
            heldObject = int(i);
            heldTicks = 0;
            throwDelay = 0;
            return true;
        }
    }
    return false;
}

// The player's Hold handler 0x15a8b8 (run from the control's list pass 0x158730 for the Hold
// contacts of the previous tick): on an action-button press (A, B, Y, ZR: 0x3e34dc) and with the
// plant in front (beyond PullOutAngleCheckDisableDistance it must lie within PullOutAngle of the
// front, cos via 0x2f7264), the request reserves the plant; the player keeps it (+0x2a4) and
// raises request bit 10, which the action machine's PullOutStart conditions read this tick.
void Session::Impl::stagePullRequest(uint32_t trig) {
    using namespace captain_toad::RecoveredVec;
    auto& ctx = player.machine.ctx;
    const bool pressed = (trig & (captain_toad::RecoveredCamera::kA | captain_toad::RecoveredCamera::kB |
                                  captain_toad::RecoveredCamera::kY | captain_toad::RecoveredCamera::kZR)) != 0;
    if (pressed && stageGroundGrab()) {
        return;   // the grab took the object; the pull request sees +0x1c0 set
    }
    if (pullTarget >= 0 || !pressed || stageWorld.holdContacts.empty() || heldObject >= 0) {
        return;   // 0x15a8b8: no request while something is held (+0x1c0)
    }
    // In PullOutStart a new pull waits for the success clip (0x2d8c04 with RePullOutEnableFrame 9,
    // Quick 4: hypothesis: ticks in the state); never during PullOutMiss.
    if (player.machine.current->name == "PullOutStart") {
        const int counter = player.machine.pullOutStart->counter;
        if (ctx.anim->isPlaying("PullOutMiss") || (ctx.anim->isPlaying("PullOutSuccess") && counter < 9) ||
            (ctx.anim->isPlaying("PullOutSuccessQuick") && counter < 4)) {
            return;
        }
    }
    const V3 hold = add(player.body.pos, scale(player.body.up, captain_toad::RecoveredStage::World::kHoldUp));
    for (size_t index : stageWorld.holdContacts) {
        auto& o = stageWorld.objects[index];
        const auto* body = o.sensor("Body");
        if (!body || !o.alive) {
            continue;
        }
        V3 v = sub(captain_toad::RecoveredStage::World::sensorCentre(o, *body), hold);
        v.y = 0;
        const float d2 = dot(v, v);
        const float near = ctx.params.pullOutAngleCheckDisableDistance;
        if (near * near < d2) {
            const float d = std::sqrt(d2);
            const V3 u = scale(v, 1.0f / d);
            const float cosA = std::cos(ctx.params.pullOutAngle * 0.0174533f);
            if (dot(u, player.body.front) < cosA) {
                continue;
            }
        }
        if (stageWorld.behaviours[index]->pullRequest(stageWorld, o)) {
            pullTarget = int(index);
            ctx.buttons0 |= 1u << 10;
            return;
        }
    }
}

// After the action machine (player control 0x15b698): when it has just entered PullOutStart the
// player sends the pull to the reserved plant (0x33743c) and plays the reply (player receiveMsg
// 0x1574dc), turning to the plant beyond PullOutDirectTargetEnableDistance and stepping up to
// PullOutSnapDistance (collider push); in any other state a reservation is released (0x2dc2f0).
// The request bits are cleared at the end of the controller tick (0x173634 from 0x3b9d8c).
void Session::Impl::stagePullAfterTick() {
    using namespace captain_toad::RecoveredVec;
    auto& m = player.machine;
    m.ctx.buttons0 = 0;
    if (pullTarget < 0) {
        return;
    }
    auto& o = stageWorld.objects[size_t(pullTarget)];
    auto& b = *stageWorld.behaviours[size_t(pullTarget)];
    if (m.current->name != "PullOutStart" || m.step != 1) {
        b.pullCancel(stageWorld, o);
        pullTarget = -1;
        return;
    }
    using Reply = captain_toad::RecoveredStage::Behaviour::PullReply;
    const Reply reply = b.pull(stageWorld, o);
    pullTarget = -1;
    const char* clip = reply == Reply::Success ? "PullOutSuccess"
                       : reply == Reply::Quick ? "PullOutSuccessQuick"
                       : reply == Reply::Carry ? "PullOutSuccessCarry"
                       : reply == Reply::Miss  ? "PullOutMiss"
                                               : nullptr;
    if (!clip) {
        return;
    }
    if (reply == Reply::Quick) {   // receiveMsg 0x1574dc: the quick reply plays 引っこ抜き成功 at once
        applySceneStop("Player", "引っこ抜き成功");
    }
    m.ctx.anim->start(clip);
    // Face and snap: plant Body minus the player's Body sensor (42 up), without the up component.
    const auto* body = o.sensor("Body");
    if (!body) {
        return;
    }
    const V3 mine = add(player.body.pos, scale(player.body.up, 42.0f));
    V3 v = sub(captain_toad::RecoveredStage::World::sensorCentre(o, *body), mine);
    v = sub(v, scale(player.body.up, dot(v, player.body.up)));
    const float d2 = dot(v, v);
    if (d2 <= 0) {
        return;
    }
    const float d = std::sqrt(d2);
    const V3 u = scale(v, 1.0f / d);
    const float direct = m.ctx.params.pullOutDirectTargetEnableDistance;
    if (direct * direct < d2) {
        player.body.front = u;
    }
    const float snap = m.ctx.params.pullOutSnapDistance;
    if (snap * snap < d2) {
        player.addPush(scale(u, d - snap));
    }
}

// Carrying (player carry update 0x1581d4, grab 0x15a8b8 -> 0x2da15c, release 0x2db54c).
void Session::Impl::stageCarryTick(uint32_t trig) {
    using namespace captain_toad::RecoveredVec;
    auto& ctx = player.machine.ctx;
    // The grab: during PullOutSuccessCarry from frame PullOutSuccessCarryHoldFrame on, the
    // player's Hold sensor against a grabbable object's Hold sensor sends the hold request; the
    // overhead carry starts without its lift clip (0x31750c accepted: kind 3, since the pull
    // passes the fifth argument 1; the pull clip itself lifts the object).
    if (heldObject < 0 && carryGrabFrame >= float(ctx.params.pullOutSuccessCarryHoldFrame)) {
        const V3 hold = add(player.body.pos, scale(player.body.up, captain_toad::RecoveredStage::World::kHoldUp));
        for (size_t i = 0; i < stageWorld.objects.size(); i++) {
            auto* r = dynamic_cast<captain_toad::RecoveredStageRadish::Radish*>(stageWorld.behaviours[i].get());
            auto& o = stageWorld.objects[i];
            const auto* s = o.sensor("Hold");
            if (!r || !o.alive || !s || !r->grabbable()) {
                continue;
            }
            const V3 d = sub(r->sensorCentre(*s), hold);
            const float rr = captain_toad::RecoveredStage::World::kHoldRadius + s->radius;
            if (dot(d, d) < rr * rr && r->grab(stageWorld, o)) {
                player.carryStart(3);   // 0x15a8b8 passes 1 as the grab's fifth argument: kind 3
                heldObject = int(i);
                heldTicks = 0;
                throwDelay = 0;
                break;
            }
        }
    }
    if (heldObject >= 0) {
        auto& o = stageWorld.objects[size_t(heldObject)];
        auto* r =
            dynamic_cast<captain_toad::RecoveredStageRadish::Radish*>(stageWorld.behaviours[size_t(heldObject)].get());
        if (!r || !o.alive || !r->held()) {
            // The door took the key from the hands (0x158630): the carry ends.
            if (r && r->nerve == captain_toad::RecoveredStageRadish::Radish::Nerve::Inserted) {
                player.carryEnd();
            }
            heldObject = -1;
        } else if (throwDelay != 0) {
            if (--throwDelay == 0) {
                stageCarryRelease();
            }
        } else if (heldTicks != 0 &&
                   (trig & (captain_toad::RecoveredCamera::kA | captain_toad::RecoveredCamera::kB |
                            captain_toad::RecoveredCamera::kY | captain_toad::RecoveredCamera::kZR))) {
            // 0x158b28: the release waits HoldThrowUpTiming (kinds 1 / 3) or HoldThrowFrontTiming.
            throwDelay = int(player.carry.kind == 1 || player.carry.kind == 3 ? ctx.params.holdThrowUpTiming
                                                                              : ctx.params.holdThrowFrontTiming);
            // Pulling (0x2d9b0c: PullOutStart) or standing (0x2d9a84: the Wait family) without an
            // override clip playing: the throw clip plays on the whole body (anim vtable +0x34)
            // and the upper-body carry ends (0x1ee6f4); otherwise the upper-body throw (0x1ee23c).
            const std::string& st = player.machine.current->name;
            const bool fullBody = (st == "PullOutStart" || st == "Wait" || st == "Idle") && !player.overridePlaying();
            if (fullBody) {
                const int k = player.carry.kind;
                if (k == 0 || k == 1 || k == 3) {
                    player.playOverride(captain_toad::RecoveredPlayerCarry::kClips[k].throwClip);
                    // Hypothesis: the override start runs the action's sound keeper like the
                    // upper-body throw's voice (same clip name).
                    playerVoices.push_back(k == 0 ? "CarryFrontThrow" : "CarryUpThrow");
                }
                player.carryEnd();
            } else {
                player.carryThrow();
            }
            if (throwDelay == 0) {
                stageCarryRelease();
            }
        }
        if (heldObject >= 0) {
            heldTicks++;
        }
    }
    player.carryUpdate();
    player.overrideUpdate();
    if (!player.carry.voice.empty()) {
        playerVoices.push_back(player.carry.voice);
        player.carry.voice.clear();
    }
}

// 0x2db54c: the throw direction, flattened against the player's up, becomes his front (0x2dc0a8,
// only when it is not zero), and the held object is thrown (0x31757c) from that front. The
// direction is the stick's move vector while the action is RunTurn, Pivot, Land or RunSign (0x1e4e5c:
// type keys 0x4fd6b4 / 0x4fd720 of the current action) and the move is not zero; otherwise it is
// the Y axis of the Spine2 joint (matrix +4, +0x14, +0x24; the upper body's facing). Traced: a throw
// out of Turn at 1265 of route pullup-turnip-a takes the Spine2 axis (0x1e4e5c returns 0).
void Session::Impl::stageCarryRelease() {
    using namespace captain_toad::RecoveredVec;
    if (heldObject < 0) {
        return;
    }
    const std::string& state = player.machine.current->name;
    const bool stickTurns = state == "RunTurn" || state == "Pivot" || state == "Land" || state == "RunSign";
    V3 dir = player.pad.move;
    if (dot(dir, dir) < 1.0e-6f || !stickTurns) {
        if (const auto spine = character.findJoint("Spine2")) {
            dir = {spine->m[1], spine->m[5], spine->m[9]};   // the joint matrix +4, +0x14, +0x24: its Y axis
        }
    }
    const V3 up = player.body.up;
    dir = sub(dir, scale(up, dot(dir, up)));
    if (dot(dir, dir) >= 1.0e-6f) {
        normalize(dir);
        player.body.front = dir;
    }
    auto& o = stageWorld.objects[size_t(heldObject)];
    if (auto* r = dynamic_cast<captain_toad::RecoveredStageRadish::Radish*>(
            stageWorld.behaviours[size_t(heldObject)].get())) {
        r->release(stageWorld, o, player.body.front, player.body.vel);
    }
    heldObject = -1;
}

// The hold component's placement (0x1d43b0) of the held object, after the player's model has
// moved this tick: it reads the holder's joints as posed in this tick (traced: the turnip's
// hold points of the route follow the protagonist's pose of the same tick, not the previous one).
void Session::Impl::stageHoldPlace() {
    if (heldObject < 0) {
        return;
    }
    poseScene();
    float m[12];
    holdMatrix(m);
    if (auto* r = dynamic_cast<captain_toad::RecoveredStageRadish::Radish*>(
            stageWorld.behaviours[size_t(heldObject)].get())) {
        r->holdAt(stageWorld.objects[size_t(heldObject)], m);
    }
}

// The held object's matrix: the hold component (0x1d43b0) on the holder's hold matrix 0x2be004
// (the Spine2 joint rotated -90 degrees about X, at the middle of HandL and HandR): position at
// the matrix x (85, 0, 12) (70, 0, 15 for a Mini holder), rotation the matrix's times
// Euler (330, 0, 270) degrees (hold parameters 0x5017a0).
void Session::Impl::holdMatrix(float out[12]) const {
    const auto spine = character.findJoint("Spine2");
    const auto handL = character.findJoint("HandL");
    const auto handR = character.findJoint("HandR");
    if (!spine || !handL || !handR) {
        const auto& p = player.body.pos;
        const float id[12] = {1, 0, 0, p.x, 0, 1, 0, p.y + 150.0f, 0, 0, 1, p.z};
        std::copy(id, id + 12, out);
        return;
    }
    const auto& s = spine->m;
    // Spine2 * Rx(-90): columns (x, -z, y) of the joint's rotation.
    float h[12] = {s[0], -s[2],  s[1], 0.5f * (handL->m[3] + handR->m[3]),
                   s[4], -s[6],  s[5], 0.5f * (handL->m[7] + handR->m[7]),
                   s[8], -s[10], s[9], 0.5f * (handL->m[11] + handR->m[11])};
    // The second offset when the holder is Mini (0x2ec288; 0x2a927c, the index of the holder's
    // matching sub-object, is always 0 or 1 and so passes its "< 2" test).
    const bool mini = stageWorld.playerPower == 1;
    const float off[3] = {mini ? 70.0f : 85.0f, 0.0f, mini ? 15.0f : 12.0f};
    float pos[3];
    for (int r = 0; r < 3; r++) {
        pos[r] = h[r * 4] * off[0] + h[r * 4 + 1] * off[1] + h[r * 4 + 2] * off[2] + h[r * 4 + 3];
    }
    // Euler (330, 0, 270) degrees as the engine's XYZ rotation (0x2f1bd8: Rz * Ry * Rx).
    const float d = 0.017453292f;
    const float rx = 330.0f * d, rz = 270.0f * d;
    const float cx = std::cos(rx), sx = std::sin(rx), cz = std::cos(rz), sz = std::sin(rz);
    const float e[9] = {cz, -sz * cx, sz * sx, sz, cz * cx, -cz * sx, 0, sx, cx};
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            out[r * 4 + c] = h[r * 4] * e[c] + h[r * 4 + 1] * e[3 + c] + h[r * 4 + 2] * e[6 + c];
        }
        out[r * 4 + 3] = pos[r];
    }
}

// Without the intro the opening starts in the reference checkpoint state, which the intro ends
// in: facing its final yaw (62.1 deg; the checkpoint front, player-memdump-f0-a, agrees to 1e-7).
// The touched point in the game view's pixels (the top screen's 400 x 240, extended past them by
// a wide view; the view is boxed as in renderView). False outside the game view.
bool Session::Impl::touchPixel(float out[2]) const {
    float u = touchView[0], v = touchView[1];
    const float shape = viewShape(viewAspect);
    if (viewAspect > shape) {
        u = (u - 0.5f) * viewAspect / shape + 0.5f;
    } else {
        v = (v - 0.5f) * shape / viewAspect + 0.5f;
    }
    // In the original view's pixels: a wide view reaches past 0..400 (or 0..240).
    float kx, ky;
    viewExtent(shape, kx, ky);
    out[0] = 200.0f + (u - 0.5f) * 400.0f * kx / kOriginalAspect;
    out[1] = 120.0f + (v - 0.5f) * 240.0f * ky;
    return u >= 0 && u <= 1 && v >= 0 && v <= 1;
}

// The ray from the game camera through a pixel of the top screen. The touch screen shows the
// middle 320 pixels of the same view at the same scale (observed, run touchB), so a touch at
// (x, y) is the top screen's (x + 40, y); the side strips, which the touch screen cannot reach,
// are accepted here too (the window shows them).
bool Session::Impl::touchRay(const float px[2], captain_toad::RecoveredVec::V3& origin,
                             captain_toad::RecoveredVec::V3& dir) const {
    if (!game.enabled) {
        return false;
    }
    const float top = std::tan(game.fovy() * float(M_PI / 360.0));
    const float x = (px[0] / 400.0f * 2.0f - 1.0f) * top * 5.0f / 3.0f;
    const float y = (1.0f - px[1] / 240.0f * 2.0f) * top;
    const float* m = game.view.m;   // world -> view, rows
    const captain_toad::RecoveredVec::V3 viewDir{x, y, -1.0f};
    dir = {m[0] * viewDir.x + m[4] * viewDir.y + m[8] * viewDir.z,
           m[1] * viewDir.x + m[5] * viewDir.y + m[9] * viewDir.z,
           m[2] * viewDir.x + m[6] * viewDir.y + m[10] * viewDir.z};
    captain_toad::RecoveredVec::normalize(dir);
    origin = {-(m[0] * m[3] + m[4] * m[7] + m[8] * m[11]), -(m[1] * m[3] + m[5] * m[7] + m[9] * m[11]),
              -(m[2] * m[3] + m[6] * m[7] + m[10] * m[11])};
    return true;
}

// Once per game tick, after the controls were read: the touch's press, slide and release.
void Session::Impl::touchTick() {
    float px[2] = {0, 0};
    const bool inView = touchPixel(px);
    const bool pressed = touchHeld && !touchWasHeld;
    const bool released = !touchHeld && touchWasHeld;
    touchWasHeld = touchHeld;
    captain_toad::RecoveredVec::V3 origin, dir;
    if (pressed) {
        touchStart[0] = px[0];
        touchStart[1] = px[1];
        touchMoved = false;
        touchTaken = false;
        touchTicks = 0;
        touchStartInView = inView;
        assistTarget = -1;
        assistSent = false;
        // 0x18dc94: a press that has not moved 16 pixels touches the nearest touch point.
        if (stageObjects && inView && touchRay(px, origin, dir)) {
            pendingScreenTouch = stageWorld.pickScreenPoint(origin, dir);
            // 0x18d42c: the touch ray against the collision (0x22e7a4); the nearest hit between 500
            // and 8500 from the camera whose object answers the pointer's queries is the target.
            std::vector<captain_toad::RecoveredCollision::HitInfo> hits;
            captain_toad::RecoveredCollision::segmentCheck(player.collider.parts, origin,
                                                           captain_toad::RecoveredVec::scale(dir, 8500.0f), hits);
            float best = 1e30f;
            for (const auto& h : hits) {
                if (h.depth < 500.0f || h.depth > 8500.0f || h.depth >= best || h.part < 0 ||
                    size_t(h.part) >= partObject.size() || partObject[size_t(h.part)] < 0) {
                    continue;
                }
                const auto& b = stageWorld.behaviours[size_t(partObject[size_t(h.part)])];
                if (!b || !b->touchTarget()) {
                    continue;
                }
                best = h.depth;
                assistTarget = partObject[size_t(h.part)];
            }
            if (getenv("DIORAMA_DEBUG") && assistTarget >= 0) {
                fprintf(stderr, "touch target %s at %.0f (tick %ld)\n",
                        stageWorld.objects[size_t(assistTarget)].id.c_str(), best, stageWorld.tickCount);
            }
        }
        return;
    }
    if (touchHeld) {
        touchTicks++;
        const float dx = px[0] - touchStart[0], dy = px[1] - touchStart[1];
        const float d2 = dx * dx + dy * dy;
        if (d2 >= 16.0f * 16.0f) {
            touchMoved = true;
        }
        // 0x18d42c: the hold message once the touch was held 5 counts (2 per tick) and moved less
        // than 16 pixels. The original also requires a movement above 0 (a finger always moves a
        // little); a mouse held still does not, so 0 counts here (input-device adaptation).
        if (assistTarget >= 0 && !assistSent && touchTicks * 2 >= 5 && d2 < 16.0f * 16.0f) {
            auto& b = stageWorld.behaviours[size_t(assistTarget)];
            if (b && b->assistHold(stageWorld, stageWorld.objects[size_t(assistTarget)])) {
                touchTaken = true;
            }
            assistSent = true;
        }
        return;
    }
    // Enhancement: a short tap that no object took walks to the walkable ground under it; a second
    // one soon after at the same place is a double tap (touchButtons).
    const bool tap =
        released && tapToWalk && stageObjects && !touchMoved && !touchTaken && touchTicks < 30 && touchStartInView;
    if (tap) {
        const float dx = touchStart[0] - lastTap[0], dy = touchStart[1] - lastTap[1];
        const bool doubleTap = stageWorld.tickCount - lastTapTick <= 15 && dx * dx + dy * dy < 24.0f * 24.0f;
        lastTapTick = doubleTap ? -1000 : stageWorld.tickCount;   // a third tap starts over
        lastTap[0] = touchStart[0];
        lastTap[1] = touchStart[1];
        if (doubleTap) {
            if (touchDoubleTap(firstTap)) {
                walkTo.active = false;
                return;
            }
        } else {
            firstTap = touchAim(touchStart);
        }
    }
    if (tap && touchRay(touchStart, origin, dir)) {
        std::vector<captain_toad::RecoveredCollision::HitInfo> hits;
        captain_toad::RecoveredCollision::segmentCheck(player.collider.parts, origin,
                                                       captain_toad::RecoveredVec::scale(dir, 20000.0f), hits);
        float best = 1e30f;
        if (getenv("DIORAMA_DEBUG")) {
            fprintf(stderr, "tap ray (%.0f, %.0f, %.0f) dir (%.2f, %.2f, %.2f): %zu hits\n", origin.x, origin.y,
                    origin.z, dir.x, dir.y, dir.z, hits.size());
        }
        for (const auto& h : hits) {
            if (h.faceNormal.y > 0.7f && h.depth < best) {   // floors only (not walls, not ceilings)
                best = h.depth;
                walkTo.pos = h.contact;
            }
        }
        if (best < 1e30f) {
            walkRoute = planWalk(player.collider.parts, ladderSpans(), player.body.pos, walkTo.pos, 20.0f);
            walkTo.active = true;
            walkTo.ticks = walkTo.stalled = 0;
            walkTo.best = 1e30f;
            if (getenv("DIORAMA_DEBUG")) {
                fprintf(stderr, "walk to (%.0f, %.0f, %.0f), route of %zu points\n", walkTo.pos.x, walkTo.pos.y,
                        walkTo.pos.z, walkRoute.points.size());
            }
        }
    }
}

// The game view pixel (400 x 240) a world point appears at; false behind the camera. scale: view
// pixels per world unit at the point's depth.
bool Session::Impl::screenPixel(const captain_toad::RecoveredVec::V3& p, float out[2], float* scale) const {
    if (!game.enabled) {
        return false;
    }
    const float* m = game.view.m;   // world -> view, rows
    const float vx = m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3];
    const float vy = m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7];
    const float vz = m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11];
    if (vz > -1.0f) {
        return false;
    }
    const float top = std::tan(game.fovy() * float(M_PI / 360.0));
    out[0] = (vx / -vz / (top * 5.0f / 3.0f) + 1.0f) * 200.0f;
    out[1] = (1.0f - vy / -vz / top) * 120.0f;
    if (scale) {
        *scale = 120.0f / (top * -vz);
    }
    return true;
}

// The object a double tap at a view pixel means: the nearest on screen (within a finger's width
// of its sensor sphere) of those the action button acts on: pullable objects (plants, levers;
// their Body sensor) and the objects that can be picked up from the ground (their Hold sensor).
// Objects beyond the first floor under the tap (seen through the ground) do not count; walls do
// not hide (some are invisible, as at the lift's window in the first course).
int Session::Impl::touchObjectAt(const float px[2]) const {
    using namespace captain_toad::RecoveredVec;
    V3 origin, dir;
    if (!touchRay(px, origin, dir)) {
        return -1;
    }
    std::vector<captain_toad::RecoveredCollision::HitInfo> hits;
    captain_toad::RecoveredCollision::segmentCheck(player.collider.parts, origin, scale(dir, 20000.0f), hits);
    float surface = 20000.0f;
    for (const auto& h : hits) {
        if (h.faceNormal.y > 0.7f) {
            surface = std::min(surface, h.depth);
        }
    }
    int best = -1;
    float bestD = 1e30f;
    for (size_t i = 0; i < stageWorld.objects.size(); i++) {
        const auto& o = stageWorld.objects[i];
        const auto* b = stageWorld.behaviours[i].get();
        if (!o.alive || !b) {
            continue;
        }
        captain_toad::RecoveredVec::V3 c;
        float radius = 0;
        const auto* r = dynamic_cast<const captain_toad::RecoveredStageRadish::Radish*>(b);
        if (b->pullable(o)) {
            const auto* s = o.sensor("Body");
            if (!s) {
                continue;
            }
            c = captain_toad::RecoveredStage::World::sensorCentre(o, *s);
            radius = s->radius;
        } else if (r && r->grabbable()) {
            const auto* s = o.sensor("Hold");
            if (!s) {
                continue;
            }
            c = r->sensorCentre(*s);
            radius = s->radius;
        } else {
            continue;
        }
        float q[2], k = 0;
        const V3 toObject = sub(c, origin);
        if (!screenPixel(c, q, &k) || std::sqrt(dot(toObject, toObject)) > surface + radius + 100.0f) {
            continue;
        }
        const float d = std::hypot(q[0] - px[0], q[1] - px[1]) - std::min(radius * k, 40.0f);
        if (d < 24.0f && d < bestD) {
            bestD = d;
            best = int(i);
        }
    }
    return best;
}

// What a tap could mean as the first of a double tap, resolved in the view it was made in (the
// walk it starts moves the camera before the second tap): the action button's object under it,
// and the course surface it shows (or the object's place).
Session::Impl::TapAim Session::Impl::touchAim(const float px[2]) const {
    using namespace captain_toad::RecoveredVec;
    TapAim aim;
    aim.object = touchObjectAt(px);
    if (aim.object >= 0) {
        aim.point = stageWorld.objects[size_t(aim.object)].translation();
        aim.hasPoint = true;
        return aim;
    }
    V3 origin, dir;
    if (touchRay(px, origin, dir)) {
        std::vector<captain_toad::RecoveredCollision::HitInfo> hits;
        captain_toad::RecoveredCollision::segmentCheck(player.collider.parts, origin, scale(dir, 20000.0f), hits);
        float best = 1e30f;
        for (const auto& h : hits) {
            if (h.depth < best) {
                best = h.depth;
                aim.point = h.contact;
                aim.hasPoint = true;
            }
        }
    }
    return aim;
}

// A double tap (aimed by its first tap): carrying, throw toward the tapped object or point;
// otherwise act on the tapped object. False when it means nothing (the taps stay walks).
bool Session::Impl::touchDoubleTap(const TapAim& aim) {
    if (heldObject >= 0) {
        if (!aim.hasPoint || aim.object == heldObject) {
            return false;
        }
        touchIntent.kind = TouchIntent::Kind::Throw;
        touchIntent.point = aim.point;
        walkRoute = {};
    } else if (aim.object >= 0) {
        touchIntent.kind = TouchIntent::Kind::Interact;
        touchIntent.object = aim.object;
        walkRoute = planWalk(player.collider.parts, ladderSpans(), player.body.pos, aim.point, 60.0f);
    } else {
        return false;
    }
    touchIntent.ticks = touchIntent.stalled = 0;
    touchIntent.best = 1e30f;
    if (getenv("DIORAMA_DEBUG")) {
        fprintf(stderr, "double tap: %s %s at (%.0f, %.0f, %.0f), route of %zu points (tick %ld)\n",
                touchIntent.kind == TouchIntent::Kind::Throw ? "throw" : "interact",
                aim.object >= 0 ? stageWorld.objects[size_t(aim.object)].id.c_str() : "ground", aim.point.x,
                aim.point.y, aim.point.z, walkRoute.points.size(), stageWorld.tickCount);
    }
    return true;
}

// Enhancement: the stick and the action button for a double tap's intent, before the controls are
// read (the game's own rules then decide what the press does). Interact walks to the object and
// presses once the press would act on it (the pull request's contact and angle test, or the
// ground grab's reach); Throw turns the protagonist toward the point and presses once his upper
// body faces it (the throw's direction, stageCarryRelease). The player's stick cancels it.
uint32_t Session::Impl::touchButtons(float& sx, float& sy) {
    using namespace captain_toad::RecoveredVec;
    using Kind = TouchIntent::Kind;
    auto& t = touchIntent;
    if (t.kind == Kind::None) {
        return 0;
    }
    auto finish = [&] {
        t.kind = Kind::None;
        t.object = -1;
        return 0u;
    };
    if (sx * sx + sy * sy > 0.04f || ++t.ticks > 900) {
        return finish();
    }
    if (t.kind == Kind::Interact && routeClimb(sx, sy, t.best, t.stalled)) {
        return 0;
    }
    if (playerBound || stageMissed || stageGoalReached || game.goal.active()) {
        return finish();
    }
    V3 forward, right;
    const bool basis = captain_toad::RecoveredPad::cameraBasis(lastCam, forward, right);
    auto steer = [&](V3 d, float k) {
        if (basis) {
            sx = dot(d, right) * k;
            sy = dot(d, forward) * k;
        }
    };
    const uint32_t press = captain_toad::RecoveredCamera::kA;
    const bool pressing = (prevHeld & press) != 0;   // a press needs the button up the tick before
    if (t.kind == Kind::Throw) {
        if (heldObject < 0) {
            return finish();
        }
        if (throwDelay != 0) {
            return 0;
        }
        V3 d = sub(t.point, player.body.pos);
        d.y = 0;
        const float len = std::sqrt(dot(d, d));
        if (len < 1.0f) {
            return pressing ? 0 : press;
        }
        d = scale(d, 1.0f / len);
        V3 facing = player.body.front;
        if (const auto spine = character.findJoint("Spine2")) {
            facing = {spine->m[1], 0, spine->m[9]};
            const float f = std::sqrt(dot(facing, facing));
            facing = f > 1e-4f ? scale(facing, 1.0f / f) : player.body.front;
        }
        if (dot(facing, d) >= std::cos(12.0f * 0.0174533f) || t.ticks > 60) {
            return pressing ? 0 : press;   // turned (or a minute effort): throw now
        }
        steer(d, 0.25f);
        return 0;
    }
    // Interact.
    if (pullTarget >= 0 || heldObject >= 0) {
        return finish();   // the press was taken
    }
    const size_t index = size_t(t.object);
    const auto& o = stageWorld.objects[index];
    const auto* b = stageWorld.behaviours[index].get();
    const auto* r = dynamic_cast<const captain_toad::RecoveredStageRadish::Radish*>(b);
    const bool pull = b && o.alive && b->pullable(o);
    const bool grab = r && o.alive && r->grabbable();
    const auto* s = o.sensor(pull ? "Body" : "Hold");
    if (!(pull || grab) || !s) {
        return finish();   // gone, or no longer the action button's
    }
    const V3 hold = add(player.body.pos, scale(player.body.up, captain_toad::RecoveredStage::World::kHoldUp));
    const V3 c = pull ? captain_toad::RecoveredStage::World::sensorCentre(o, *s) : r->sensorCentre(*s);
    V3 d = sub(c, hold);
    d.y = 0;
    const float dist = std::sqrt(dot(d, d));
    bool ready = false;
    if (pull) {
        const auto& contacts = stageWorld.holdContacts;
        const float near = player.machine.ctx.params.pullOutAngleCheckDisableDistance;
        ready = std::find(contacts.begin(), contacts.end(), index) != contacts.end() &&
                (dist <= near || dot(scale(d, 1.0f / dist), player.body.front) >=
                                     std::cos(player.machine.ctx.params.pullOutAngle * 0.0174533f));
    } else {
        const V3 v = sub(c, hold);
        const float rr = captain_toad::RecoveredStage::World::kHoldRadius + s->radius;
        ready = dot(v, v) < rr * rr && dot(v, player.body.front) + s->radius >= 0.0f;
    }
    if (ready) {
        return pressing ? 0 : press;
    }
    // Along the route, closer by at least a little every 3/4 second, or give up.
    V3 target = c;
    float progress = 0;
    if (!routeAim(c, target, progress) && dist < 1e-3f) {
        return 0;
    }
    if (progress < t.best - 1.0f) {
        t.best = progress;
        t.stalled = 0;
    } else if (++t.stalled > 45) {
        return finish();
    }
    V3 e = sub(target, player.body.pos);
    e.y = 0;
    const float el = std::sqrt(dot(e, e));
    if (el > 1e-3f) {
        steer(scale(e, 1.0f / el), std::min(1.0f, dist / 80.0f + 0.3f));
    }
    return 0;
}

// Enhancement: the stick that walks toward the tapped point, expressed in the last tick's camera
// (the player's own movement rules then walk him there). The player's stick cancels it.
void Session::Impl::walkStick(float& sx, float& sy) {
    using namespace captain_toad::RecoveredVec;
    if (!walkTo.active) {
        return;
    }
    if (sx * sx + sy * sy > 0.04f || ++walkTo.ticks > 900) {
        walkTo.active = false;
        return;
    }
    if (routeClimb(sx, sy, walkTo.best, walkTo.stalled)) {
        return;
    }
    if (playerBound || stageMissed || stageGoalReached || game.goal.active()) {
        walkTo.active = false;
        return;
    }
    V3 d = sub(walkTo.pos, player.body.pos);
    d.y = 0;
    const float dist = std::sqrt(dot(d, d));
    // Arrived, or no further along the route for half a second (a wall in the way).
    if (dist < 20.0f && std::fabs(walkTo.pos.y - player.body.pos.y) < 60.0f) {
        walkTo.active = false;
        return;
    }
    V3 target = walkTo.pos;
    float progress = 0;
    routeAim(walkTo.pos, target, progress);
    if (progress < walkTo.best - 1.0f) {
        walkTo.best = progress;
        walkTo.stalled = 0;
    } else if (++walkTo.stalled > 30) {
        walkTo.active = false;
        return;
    }
    V3 forward, right;
    if (!captain_toad::RecoveredPad::cameraBasis(lastCam, forward, right)) {
        return;
    }
    V3 e = sub(target, player.body.pos);
    e.y = 0;
    const float el = std::sqrt(dot(e, e));
    if (el < 1e-3f) {
        return;
    }
    e = scale(e, 1.0f / el);
    const float k = std::min(1.0f, dist / 80.0f + 0.3f);   // ease in over the last steps
    sx = dot(e, right) * k;
    sy = dot(e, forward) * k;
}

// A ladder on the route: while the ladder holds the protagonist, the stick climbs (straight up or
// down, as the ladder reads it); once it lets him go, the route goes on from its top or foot.
bool Session::Impl::routeClimb(float& sx, float& sy, float& best, int& stalled) {
    using namespace captain_toad::RecoveredVec;
    if (walkRoute.empty() || walkRoute.next >= walkRoute.points.size()) {
        return false;
    }
    if (playerBound) {
        if (!walkRoute.climbing) {
            // The ladder takes hold from up to its sensor's reach (100) away, before the route's
            // point at its foot or ledge: a climb a few points ahead is the one he is on. A ladder
            // the route did not plan for (he walked into one) is climbed toward the route's next
            // height.
            walkRoute.climbDirection = 0;
            for (size_t k = walkRoute.next; k < walkRoute.points.size() && k <= walkRoute.next + 8; k++) {
                if (walkRoute.points[k].climb != 0) {
                    walkRoute.next = k;
                    walkRoute.climbDirection = walkRoute.points[k].climb;
                    break;
                }
            }
            if (walkRoute.climbDirection == 0) {
                const float y = walkRoute.points.back().pos.y;
                walkRoute.climbDirection = y > player.body.pos.y ? 1 : -1;
                walkRoute.unplannedClimb = true;
            }
            walkRoute.climbing = true;
        }
        sx = 0;
        sy = float(walkRoute.climbDirection);
        best = 1e30f;
        stalled = 0;
        return true;
    }
    if (walkRoute.climbing) {
        walkRoute.climbing = false;
        best = 1e30f;
        stalled = 0;
        if (walkRoute.unplannedClimb) {
            // On from the route point nearest to where the ladder let him go.
            walkRoute.unplannedClimb = false;
            float nearest = 1e30f;
            for (size_t k = walkRoute.next; k < walkRoute.points.size(); k++) {
                const V3 d = sub(walkRoute.points[k].pos, player.body.pos);
                if (walkRoute.points[k].climb == 0 && dot(d, d) < nearest) {
                    nearest = dot(d, d);
                    walkRoute.next = k;
                }
            }
        } else if (++walkRoute.next >= walkRoute.points.size()) {
            walkRoute = {};
        }
    }
    return false;
}

// The ladders a route may use: those waiting for the player (switched on where they need it).
std::vector<LadderSpan> Session::Impl::ladderSpans() {
    std::vector<LadderSpan> out;
    for (size_t i = 0; i < stageWorld.objects.size(); i++) {
        auto* l = dynamic_cast<captain_toad::RecoveredStageLadder::Ladder*>(stageWorld.behaviours[i].get());
        auto& o = stageWorld.objects[i];
        if (l && o.alive && l->nerve == captain_toad::RecoveredStageLadder::Ladder::Nerve::Wait &&
            l->valid(stageWorld, o)) {
            out.push_back({l->bottom, l->top, l->front});
        }
    }
    return out;
}

// The point to steer for on the way to target: along the planned route while it lasts, then the
// target itself. progress falls as the walk gets on (the remaining route, then the distance).
bool Session::Impl::routeAim(const captain_toad::RecoveredVec::V3& target, captain_toad::RecoveredVec::V3& aim,
                             float& progress) {
    using namespace captain_toad::RecoveredVec;
    const V3 pos = player.body.pos;
    auto flat = [&](const V3& q) {
        return std::hypot(q.x - pos.x, q.z - pos.z);
    };
    aim = target;
    progress = flat(target);
    if (walkRoute.empty()) {
        return false;
    }
    aim = walkRoute.steerPoint(pos);
    const size_t left = walkRoute.points.size() - walkRoute.next;
    if (left <= 1 && walkRoute.points.back().climb == 0 && flat(walkRoute.points.back().pos) < 30.0f) {
        walkRoute = {};   // the route is done: straight on
        aim = target;
        return false;
    }
    progress = float(left) * 25.0f + flat(aim);
    return true;
}

// ---- touch hints (StageGuide.hpp) -----------------------------------------------------------

void Session::Impl::guideSetup(captain_toad::data::JsonView a) {
    guidePoints.clear();
    for (size_t i = 0; i < stageWorld.behaviours.size(); i++) {
        if (dynamic_cast<captain_toad::RecoveredStageGuide::GuideAppearPoint*>(stageWorld.behaviours[i].get())) {
            guidePoints.push_back(i);
        }
    }
    if (guidePoints.empty() || !a) {
        return;
    }
    for (const std::string& name : a["sequences"].keys()) {
        captain_toad::data::JsonView seq = a["sequences"][name];
        auto& keys = guideSprite.sequences[name];
        for (captain_toad::data::JsonView k : seq["keys"].items()) {
            keys.push_back(
                {k[0].f(), k[1].f(), k[2].f(), {k[3].f() / 255, k[4].f() / 255, k[5].f() / 255, k[6].f() / 255}});
        }
        guideSprite.loops[name] = seq["loop"].b();
    }
    for (int k = 0; k < 4; k++) {
        guideSprite.quad[k] = a["quad"][k].f();
    }
    for (const std::string& name : a["textures"].keys()) {
        captain_toad::data::JsonView t = a["textures"][name];
        const int w = t["size"][0].i(), h = t["size"][1].i();
        std::ifstream f(scenePath / t["file"].str(), std::ios::binary);
        std::vector<char> rgba((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (rgba.size() < size_t(w) * size_t(h) * 4) {
            continue;
        }
        guideSprite.textures[name] =
            captain_toad::gpu::device().createTexture(captain_toad::gpu::Format::RGBA8, w, h, rgba.data());
    }
}

// Control 0x1a2d8c: the 2D actor's scale is 0.24 x the target field of view of the camera's zoom
// state (0x4f7a70; traced 30.0 = ZoomFovyFar with the player camera, 0 under the Start camera),
// at least 2.0 and at most 5.75 (the touch / microphone icons) or 2.0 / 4.0 / 5.0 by the zoom state
// (0x4f7a74: 1 / 2 / else; the button text).
float Session::Impl::guideScale(int iconType) const {
    float fov = 0.0f;
    int zoom = -1;
    if (game.director.current && game.director.current != game.director.startCam) {   // a player camera
        const auto* cam = static_cast<const captain_toad::RecoveredCamera::PlayerCamera*>(game.director.current);
        zoom = cam->zoomState;
        fov = zoom == 1 ? cam->zoomFovy : zoom == 2 ? cam->zoomFovyMedium : zoom == 3 ? 5.0f : cam->zoomFovyFar;
    }
    float most = 5.75f;
    if (iconType == 0) {
        most = zoom == 1 ? 2.0f : zoom == 2 ? 4.0f : 5.0f;
    }
    return std::max(2.0f, std::min(fov * 0.24f, most));
}

// The sprite of each shown hint: its nerve's sequence (INTRO / IDLE / OUTRO) at two frames per
// tick, a camera-facing quad of the sequence's corners times the scale and the key's scale about
// its centre, the centre 16 texels (times the scale) above the point plus the key's y offset,
// tinted by the key's colour, drawn over the scene without depth test. Traced (draw 393 of the
// rotation runs at frames 60 and 240): 368 x 368 and 368 x 391 view units (scale 5.75, key scale
// y 32 / 34 of 32), centred on the point horizontally and 87 / 94 units above it (16 texels: 92;
// hypothesis), about 50 units nearer the camera than the point (not reproduced).
void Session::Impl::drawGuideBalloons(const double* mv, const captain_toad::gpu::Mat4& projection) {
    using Guide = captain_toad::RecoveredStageGuide::GuideAppearPoint;
    captain_toad::gpu::Program& program = captain_toad::gpu::device().sharedProgram(
        "guide balloon",
        "in vec3 inPosition;in vec2 inTexCoord0;out vec2 texCoord;uniform mat4 modelViewProjectionMatrix;\n"
        "void main(){gl_Position=modelViewProjectionMatrix*vec4(inPosition,1.0);texCoord=inTexCoord0;}",
        "in vec2 texCoord;uniform sampler2D image;uniform vec4 tint;\n"
        "void main(){fragColor=texture(image,texCoord)*tint;}");
    // Placed in view space: the projection alone.
    const auto mvp = captain_toad::gpu::modelViewProjection(projection, captain_toad::gpu::Mat4());
    program.setFloats("modelViewProjectionMatrix", mvp.data(), 16);
    for (size_t index : guidePoints) {
        const auto* g = static_cast<const Guide*>(stageWorld.behaviours[index].get());
        if (!g->balloonShown()) {
            continue;
        }
        const char* suffix = g->nerve == Guide::Nerve::Appear ? "INTRO"
                             : g->nerve == Guide::Nerve::End  ? "OUTRO"
                                                              : "IDLE";
        const std::string name = std::string(g->sequencePrefix()) + suffix;
        auto seq = guideSprite.sequences.find(name);
        auto tex = guideSprite.textures.find(g->iconType == 0 ? "guideballoon_button_0" : "guideballoon_touch_0");
        if (seq == guideSprite.sequences.end() || seq->second.empty() || tex == guideSprite.textures.end()) {
            continue;
        }
        int frame = 2 * g->step;
        const int n = int(seq->second.size());
        frame = guideSprite.loops[name] ? frame % n : std::min(frame, n - 1);
        const auto& k = seq->second[size_t(frame)];
        const double p[3] = {g->position.x, g->position.y, g->position.z};
        double e[3];
        for (int r = 0; r < 3; r++) {
            e[r] = mv[r] * p[0] + mv[4 + r] * p[1] + mv[8 + r] * p[2] + mv[12 + r];
        }
        const float s = guideScale(g->iconType);
        const float x0 = guideSprite.quad[0] * k.sx * s, x1 = guideSprite.quad[2] * k.sx * s;
        const float centre = float(e[1]) + (16.0f + k.dy) * s;
        const float y0 = centre + guideSprite.quad[1] * k.sy * s, y1 = centre + guideSprite.quad[3] * k.sy * s;
        program.set("tint", k.color[0], k.color[1], k.color[2], k.color[3]);
        captain_toad::gpu::Draw d;
        d.program = &program;
        d.state.blend = captain_toad::gpu::BlendState::alpha();
        d.textures[0] = {tex->second.get(), {}};
        d.samplers = {{"image", 0}};
        captain_toad::gpu::Immediate v;
        v.attributes = {{captain_toad::gpu::Attribute::Position, 3, 0},
                        {captain_toad::gpu::Attribute::TexCoord0, 2, 12}};
        v.floatsPerVertex = 5;
        const float c[4][5] = {{float(e[0] + x0), y0, float(e[2]), 0, 1},
                               {float(e[0] + x1), y0, float(e[2]), 1, 1},
                               {float(e[0] + x1), y1, float(e[2]), 1, 0},
                               {float(e[0] + x0), y1, float(e[2]), 0, 0}};
        v.quad(c[0], c[1], c[2], c[3]);
        captain_toad::gpu::device().draw(d, v);
    }
}

}   // namespace captain_toad::session
