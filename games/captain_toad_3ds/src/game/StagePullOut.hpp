// =============================================================================================
// StagePullOut.hpp - plants the protagonist pulls out of the ground (StageObjects.hpp)
// =============================================================================================
//
// What the player sees: tufts of leaves stick out of the grass. Standing next to one and pressing
// an action button, the protagonist grabs it and pulls; a coin pops out and is collected, and an
// empty hole is left. Some plants grow back and can be pulled again.
//
// PullOutObj (creator 0x452924, vtable 0x4bd320, init 0x143fa8; units PullOutObj,
// PullOutObjTenCount, PullOutObjAssist):
//   ItemType (string, mapped by 0x2bf188: Radish 0, Coin 2, Coin10 3, ..., AssistItem 29) decides
//   the pull state (+0x90 block, 0x18 bytes): [0] AppearNum, [4] pulls needed, [8] carry (Radish,
//   DoorKey), [9] IsPullOutInfinite, [10] ten-count, [12] item step (4 for Coin, else 0).
//   - Coin and most types: one pull. PullOutObjTenCount: Coin, AppearNum 9 (ten pulls in all),
//     ten-count flag. PullOutObjAssist: AssistItem; it starts dead unless the assist check
//     0x1a1f00 passes (called from the init at 0x1442e8): the course's record (0x2bc870: 0x20 bytes
//     per course, index from the scene info +0x40 +4) has flag bit 1 clear (0x2f93b0; read by 72
//     callers) and its counter +0xc above 4 (0x1c49fc, read only here), and the placement's
//     IsRestartAssist equals the game state byte +0xd (inverted when IsRestartAssist is false).
//     Hypothesis: bit 1 is "cleared" and +0xc counts misses (its writer is not found). A first visit
//     has a zero counter, so the plant is dead (as observed, run front-input-a); the port has no
//     per-course records yet.
//   receiveMsg 0x143e8c forwards to the shared pull-out keeper (built at 0x2eca80, vtable
//   0x4c390c, receiveMsg 0x196ab0), whose nerves drive the plant:
//     Wait     0x420654  "Wait" (loop)
//     Reaction 0x420714  "Reaction" when brushed (messages 0x2f2984 / 0x2ee3f4; not ported), Wait
//     Pulled   0x25b50c  the plant's "PullOutSuccess" (absent from the PullOutObj archive: no clip
//                        change); at step [12] the item callback (+0x48: for a coin the hit
//                        reaction 吹き出しコイン "coin pops out" and the coin); at the action's end
//                        -> Empty
//     Empty    0x1972a8  "Empty" ("EmptyKeep" while items remain); after 35 steps (0 for the
//                        ten-count plant) and with items remaining (0x3e3664: count < AppearNum or
//                        infinite): count + 1 -> Appear
//     Appear   0x4206c0  "Appear" until the action ends -> Wait
//     Miss     0x420768 / 0x4208d4 / 0x420854 (PullOutMiss, PullOutMissWait, PullOutMissEnd):
//                        for pulls short of the count (mushroom / pickaxe plants need three):
//                        they play "PullOutMiss" (absent here) and return to Wait; in the port
//                        the plant just stays in Wait; formerly none in the ported
//                        courses, not ported
//   Keeper protocol (0x196ab0), with the player (session/Session.cpp stagePull*):
//     request (0x2e2418 family, sent by the player's Hold handler 0x15a8b8 via 0x337dac): accepted
//       in Wait / Reaction / Appear / the miss nerves when no other requester holds the plant.
//     pull (0x2e23a0 family, sent by the player's control 0x15b698 via 0x33743c when the action
//       machine has just entered PullOutStart): from the requester only; pulls + 1; with enough
//       pulls the reply is Carry (0x2c3e28) for carried items, Quick (0x3399d0) for the ten-count
//       plant, else Success (0x2e2364), and the plant goes to Pulled; otherwise Miss (0x33789c).
//     cancel (0x2dc2f0, the player left PullOutStart without pulling): the reservation is released.
// =============================================================================================
#pragma once

#include "game/StageItems.hpp"
#include "game/StageObjects.hpp"
#include "game/StagePowerUps.hpp"
#include "game/StageDoorKey.hpp"
#include "game/StageRadish.hpp"

#include <algorithm>

namespace captain_toad::RecoveredStagePullOut {

using namespace captain_toad::RecoveredStage;

// ItemType names to the item enum (0x2bf188).
inline int itemTypeOf(const std::string& name) {
    static const std::pair<const char*, int> kTable[] = {{"Coin", 2},
                                                         {"Coin10", 3},
                                                         {"CoinRandom10", 4},
                                                         {"CoinInfinity", 5},
                                                         {"KinokoOneUp", 7},
                                                         {"KinokoSuper", 8},
                                                         {"SuperBell", 9},
                                                         {"FireFlower", 10},
                                                         {"SuperLeaf", 11},
                                                         {"BoomerangFlower", 12},
                                                         {"PickaxeItem", 13},
                                                         {"Ball", 14},
                                                         {"CoinBlow", 16},
                                                         {"GreenStar", 17},
                                                         {"WarpCubeLockedPiece", 18},
                                                         {"DoubleMario", 19},
                                                         {"KinokoBig", 20},
                                                         {"BoxPropeller", 21},
                                                         {"BoxKiller", 22},
                                                         {"IllustItem", 23},
                                                         {"Radish", 0},
                                                         {"SuperBellSpecial", 24},
                                                         {"CoinConcentricCircle", 25},
                                                         {"CoinBlow30", 26},
                                                         {"CollectItem", 27},
                                                         {"DoorKey", 6},
                                                         {"KinokoTreasure", 28},
                                                         {"AssistItem", 29}};
    for (const auto& [n, v] : kTable) {
        if (name == n) {
            return v;
        }
    }
    return -1;   // "Dummy"
}

struct PullOutObj : Behaviour {
    enum class Nerve { Wait, Pulled, Empty, Appear } nerve = Nerve::Wait;
    int step = 0;
    int itemType = 2;
    int appearNum = 0;       // [0]
    int pullsNeeded = 1;     // [4]
    bool carry = false;      // [8]
    bool infinite = false;   // [9]
    bool tenCount = false;   // [10]
    int itemStep = 0;        // [12]
    int appearCount = 0;     // keeper +0x20
    int pulls = 0;           // keeper +0x1c
    bool reserved = false;   // keeper +0x28
    int coin = -2;           // the coin item object (-1 none, -2 not looked up)
    int powerUp = -2;        // the mushroom / pickaxe object (KinokoSuper, PickaxeItem plants)
    int radish = -2;         // the turnip object (Radish plants)
    bool byPickaxe =
        false;   // Pulled by the pickaxe (nerve 0x4208fc: item callback +0x4c) (the requester; the protagonist is the only one)

    explicit PullOutObj(Object& o) {
        itemType = itemTypeOf(o.string("ItemType", "Dummy"));
        appearNum = int(o.number("AppearNum", 0));
        infinite = o.number("IsPullOutInfinite", 0) != 0;
        // 0x143fa8: types with several pulls (0x1b CollectItem, 6 DoorKey, 0xd/7/8/0x1c) get 3; the
        // rest one. Coin plants (the only ones in the ported courses) need one.
        if (itemType == 0x1b || itemType == 6 || itemType == 0xd || itemType == 7 || itemType == 8 ||
            itemType == 0x1c) {
            pullsNeeded = 3;
        }
        if (itemType == 6) {
            infinite = true;
        }
        if (o.unit == "PullOutObjTenCount") {
            itemType = 2;
            tenCount = true;
            pullsNeeded = 1;
            appearNum = 9;
            infinite = false;
        }
        if (o.unit == "PullOutObjAssist") {
            itemType = 0x1d;
            pullsNeeded = 1;
            infinite = false;
            o.alive = false;   // the assist check 0x1a1f00: no course record, counter 0 (see above)
        }
        itemStep = itemType == 2 ? 4 : 0;
        carry = itemType == 0 || itemType == 6;
    }

    // The item of this plant (the exporter's "SpawnedBy" objects of class cls), or -1.
    static int spawned(const World& w, const Object& o, const char* cls) {
        for (size_t i = 0; i < w.objects.size(); i++) {
            if (w.objects[i].cls == cls && w.objects[i].string("SpawnedBy", "") == o.id) {
                return int(i);
            }
        }
        return -1;
    }
    // The keeper's regrow callback (+0x54; the door-key plant's 0x438314, called by Empty
    // 0x1972a8 before the items check): false while the plant's key is Inserted or Vanish (used in
    // a door); a key that timed out on the ground died in Ground and lets the plant regrow.
    bool regrowAllowed(const World& w, const Object& o) const {
        if (itemType != 6) {
            return true;
        }
        const int k = spawned(w, o, "DoorKey");
        const auto* key =
            k < 0 ? nullptr
                  : dynamic_cast<const captain_toad::RecoveredStageDoorKey::DoorKey*>(w.behaviours[size_t(k)].get());
        return !key || (key->nerve != captain_toad::RecoveredStageRadish::Radish::Nerve::Inserted &&
                        key->nerve != captain_toad::RecoveredStageRadish::Radish::Nerve::Vanish);
    }
    void setNerve(Nerve n) {
        nerve = n;
        step = 0;
    }
    bool itemsRemain() const { return appearCount < appearNum || infinite; }   // 0x3e3664

    void control(World& w, Object& o) override {
        switch (nerve) {
        case Nerve::Wait:   // 0x420654
            if (step == 0) {
                o.startAction("Wait");
            }
            break;
        case Nerve::Pulled:   // 0x25b50c (by hand) / 0x4208fc (by the pickaxe; the same steps)
            if (step == 0) {
                o.startAction("PullOutSuccess");   // not in the archive: the clip does not change
            }
            if (step == itemStep) {
                item(w, o);
            }
            if (step >= itemStep && (o.isActionEnd() || !o.hasClip("PullOutSuccess"))) {
                setNerve(Nerve::Empty);
                return;
            }
            break;
        case Nerve::Empty:   // 0x1972a8
            if (step == 0) {
                o.startAction(itemsRemain() ? "EmptyKeep" : "Empty");
            }
            if ((tenCount ? 0 : 0x23) < step && !regrowAllowed(w, o)) {
                if (o.action != "Empty") {
                    o.startAction("Empty");   // 0x1973ec
                }
            } else if ((tenCount ? 0 : 0x23) < step && itemsRemain()) {
                appearCount++;
                pulls = 0;
                setNerve(Nerve::Appear);
                return;
            }
            break;
        case Nerve::Appear:   // 0x4206c0
            if (step == 0) {
                o.startAction("Appear");
            } else if (o.isActionEnd()) {
                setNerve(Nerve::Wait);
                return;
            }
            break;
        }
        step++;
    }

    // The item callback of the Pulled nerve: for coin plants the coin pops out (hit reaction
    // 吹き出しコイン) and is collected automatically (InitItem: コインx1[自動取得]).
    void item(World& w, Object& o) {
        // The item position (0x2ecdb4): the plant + its up x 100 for coins, x 30 otherwise.
        const V3 itemAt = captain_toad::RecoveredVec::add(
            o.translation(), captain_toad::RecoveredVec::scale(o.up(), itemType == 2 ? 100.0f : 30.0f));
        if (itemType == 2) {
            // Played by the item generator (built with the name at 0x2ec830), not through the
            // reaction controller: its StopSceneFrame 10 does not stop the scene (run hc6: the only
            // stop of the pull is the player's 引っこ抜き成功, 3 frames).
            w.reaction(o, "吹き出しコイン", nullptr, false);
            w.counters.coins += 1;
            // The coin item at the item position: the plant + (0, 100, 0) here (the offset the
            // item callback hands the item keeper, +0x18, read in run coinT at the generator call
            // 0x3e5c1c: (-1200, -650, 350) for the plant at -750).
            if (coin == -2) {
                coin = spawned(w, o, "ItemCoinCountUp");
            }
            if (coin >= 0) {
                Object& c = w.objects[size_t(coin)];
                if (auto* b = dynamic_cast<captain_toad::RecoveredStageItems::ItemCoinCountUp*>(
                        w.behaviours[size_t(coin)].get())) {
                    b->appear(w, c, itemAt);
                    w.sound(c, "PgGet");   // the generator's PlayInfo on the coin
                }
            }
            if (getenv("DIORAMA_DEBUG")) {
                fprintf(stderr, "pull-out %s: coin (coins %d)\n", o.id.c_str(), w.counters.coins);
            }
        } else if (itemType == 0) {
            // The turnip (generator branch カブ[引っこ抜き] 0x3e8594): the pooled Radish at the
            // item position (the plant + up x 100, as the coin), started by 0x260434.
            if (radish == -2) {
                radish = spawned(w, o, "Radish");
            }
            if (radish >= 0) {
                if (auto* r =
                        dynamic_cast<captain_toad::RecoveredStageRadish::Radish*>(w.behaviours[size_t(radish)].get())) {
                    if (byPickaxe) {
                        // 0x143fa0 (flag 0): カブ%s%s -> カブ ([コイン] / [地形追従] not set here):
                        // 0x2605a0, facing the plant's front.
                        const auto& m = o.placement.m;
                        r->popUp(w, w.objects[size_t(radish)], itemAt,
                                 captain_toad::RecoveredVec::V3{m[2], m[6], m[10]});
                    } else {
                        r->appear(w, w.objects[size_t(radish)], itemAt, w.player.front);
                    }
                }
            }
        } else if (itemType == 6) {
            // The door key (keeper 0x2ecdb4 -> 0x3c39c0): the plant's own key at the item position.
            if (const int i = spawned(w, o, "DoorKey"); i >= 0) {
                if (auto* k =
                        dynamic_cast<captain_toad::RecoveredStageDoorKey::DoorKey*>(w.behaviours[size_t(i)].get())) {
                    k->start(w, w.objects[size_t(i)], itemAt, w.player.front);
                }
            }
        } else if (itemType == 8 || itemType == 13) {
            // スーパーキノコ[真上出現] (start 0x155490) / 無敵ツルハシ[真上出現] (start 0x156f0c): the
            // pooled item at the item position.
            const char* cls = itemType == 8 ? "KinokoSuper" : "PickaxeItem";
            if (powerUp == -2) {
                powerUp = spawned(w, o, cls);
            }
            if (powerUp >= 0) {
                if (auto* p = dynamic_cast<captain_toad::RecoveredStagePowerUps::PopItem*>(
                        w.behaviours[size_t(powerUp)].get())) {
                    p->appear(w, w.objects[size_t(powerUp)], itemAt);
                }
            }
        } else if (itemType == 28) {
            // The zoom plant's item callback 0x185880: お宝キノコ[地形追従] with IsConnectToCollision
            // (+0xac, 0x2f7040), else お宝キノコ, 30 above the plant: the plant's treasure.
            if (const int i = spawned(w, o, "KinokoTreasure"); i >= 0) {
                if (auto* t = dynamic_cast<captain_toad::RecoveredStageItems::KinokoTreasure*>(
                        w.behaviours[size_t(i)].get())) {
                    t->appear(w.objects[size_t(i)], itemAt);
                }
            }
        } else if (getenv("DIORAMA_DEBUG")) {
            fprintf(stderr, "pull-out %s: item type %d not ported\n", o.id.c_str(), itemType);
        }
    }

    // The pickaxe (keeper receiveMsg 0x196ab0, 0x2fc6d0): in Wait / Reaction / Appear / the miss
    // nerves the plant is uprooted at once (nerve 0x4f5f68, exec 0x4208fc), whatever its pull
    // count and reservation; its item comes from the callback +0x4c (0x143fa0).
    bool pickaxeHit(World& w, Object& o, const Sphere&, bool) override {
        if (!(nerve == Nerve::Wait || nerve == Nerve::Appear) || !o.alive) {
            return false;
        }
        if (getenv("DIORAMA_DEBUG")) {
            fprintf(stderr, "pull-out %s uprooted by the pickaxe\n", o.id.c_str());
        }
        (void)w;
        byPickaxe = true;
        setNerve(Nerve::Pulled);
        return true;
    }

    bool pullable(const Object&) const override { return true; }
    bool pullRequest(World&, Object&) override {
        if (reserved || !(nerve == Nerve::Wait || nerve == Nerve::Appear)) {
            return false;
        }
        reserved = true;
        return true;
    }
    PullReply pull(World&, Object& o) override {
        if (!reserved || !(nerve == Nerve::Wait || nerve == Nerve::Appear)) {
            return PullReply::None;   // 0x196ab0 takes the pull only in those nerves
        }
        reserved = false;
        pulls++;
        if (pulls < pullsNeeded) {
            return PullReply::Miss;   // the miss nerves are not ported (no such plant here)
        }
        if (getenv("DIORAMA_DEBUG")) {
            fprintf(stderr, "pull-out %s pulled\n", o.id.c_str());
        }
        byPickaxe = false;
        // The keeper takes the new nerve at its next update: the plant has already run this tick
        // (traced: Success sent at 1367, Pulled step 0 with the item at 1369, route
        // pullup-pickaxe-a), so step 0 comes in the next tick.
        setNerve(Nerve::Pulled);
        step = -1;
        return carry ? PullReply::Carry : tenCount ? PullReply::Quick : PullReply::Success;
    }
    void pullCancel(World&, Object&) override { reserved = false; }
};

inline const Register<PullOutObj> kPullOutObj{"PullOutObj"};

// PullOutObjZoom (creator 0x4530fc, vtable 0x4c2048, init 0x185a40, receiveMsg 0x185394,
//   control 0x185d6c): a plant that hides a treasure (item type 0x1c: お宝キノコ, お宝キノコ[地形追従] with
//   IsConnectToCollision; item callback 0x185880); one
//   pull. Nerves (0x4f5f80..0x4f5f90):
//     WaitZoomOut 0x41cbbc  every 5 steps the hit reaction お知らせ[近] while the protagonist is
//                           within 120 (literal 0x41cc9c); within 100 (0x41cc98) -> WaitZoomIn.
//     WaitZoomIn  0x185684  "WaitZoomIn", the same notice; back to WaitZoomOut once its counter
//                           (+0xd4, kept up by the headlamp's flash) has run out and he is not
//                           within 100.
//     Pulled      0x25be24  "PullOutSuccess", the item at the item step (+0xc0 = 0), then the
//                           plant is gone (vtable +0x18).
//   receiveMsg 0x185394: the pull request and pull as the keeper's (0x2e2418 / 0x2e23a0) in
//   either wait nerve; the reply Success (Carry with +0x9c clear); the flash (0x2ce8f8 family)
//   within 500 counts +0xd4 and after 20 messages zooms in (not ported: the flash count).
struct PullOutObjZoom : PullOutObj {
    bool zoomIn = false;
    int zoomStep = 0;
    int zoomCount = 0;   // +0xd4
    explicit PullOutObjZoom(Object& o) : PullOutObj(o) {
        itemType = 28;
        pullsNeeded = 1;
        infinite = false;
        appearNum = 0;
        itemStep = 0;
        carry = false;
    }
    void control(World& w, Object& o) override {
        if (nerve == Nerve::Pulled) {
            if (step == 0) {
                o.startAction("PullOutSuccess");
            }
            if (step == itemStep) {
                item(w, o);
            }
            if (step >= itemStep && (o.isActionEnd() || !o.hasClip("PullOutSuccess"))) {
                o.alive = false;   // vtable +0x18
                return;
            }
            step++;
            return;
        }
        if (nerve != Nerve::Wait) {
            PullOutObj::control(w, o);
            return;
        }
        const V3 d = captain_toad::RecoveredVec::sub(w.player.pos, o.translation());
        const float dist = std::sqrt(captain_toad::RecoveredVec::dot(d, d));
        if (zoomStep == 0) {
            o.startAction(zoomIn ? "WaitZoomIn" : "WaitZoomOut");
        }
        if (zoomStep % 5 == 0 && dist < 120.0f) {
            w.reaction(o, "お知らせ[近]");
        }
        if (!zoomIn && dist < 100.0f) {
            zoomIn = true;
            zoomCount = 2;
            zoomStep = 0;
            return;
        }
        if (zoomIn && --zoomCount <= 0 && !(dist < 100.0f)) {
            zoomIn = false;
            zoomStep = 0;
            return;
        }
        zoomStep++;
        step++;
    }
};

inline const Register<PullOutObjZoom> kPullOutObjZoom{"PullOutObjZoom"};

}   // namespace captain_toad::RecoveredStagePullOut
