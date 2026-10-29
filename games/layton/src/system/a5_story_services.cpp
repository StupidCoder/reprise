// The fifth adventure's story services: the place table (155xx), the story
// state (165xx: the current place, map, guide and step), the player's names
// (16101). Each comment names the handler (game service table,
// .analysis/adventure56/research/l5-handler-tables.tsv). See
// .analysis/adventure56/docs/story.md.
#include "graphics/a56_model.hpp"
#include "graphics/a56_text.hpp"
#include "import/a56_formats.hpp"
#include "system/a5_game.hpp"
#include "system/a5_progress.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace picarat::adventure5 {

using xq::Call;
using xq::Value;

namespace {

bool done(Call& c, std::int32_t v = 1) {
    c.result = Value::ofInt(v);
    return true;
}

bool doneReal(Call& c, float v) {
    c.result = Value::ofFloat(v);
    return true;
}

// The game's variables the services read and write.
constexpr const char* kPlace =
    "\x8c\xbb\x8d\xdd\x82\xcc\x83\x76\x83\x8c\x83\x43\x83\x58";                    // 現在のプレイス
constexpr const char* kMap = "\x8c\xbb\x8d\xdd\x82\xcc\x83\x7d\x83\x62\x83\x76";   // 現在のマップ
constexpr const char* kGuide = "\x8c\xbb\x8d\xdd\x82\xcc\x83\x4b\x83\x43\x83\x68"; // 現在のガイド
constexpr const char* kStep =
    "\x8c\xbb\x8d\xdd\x82\xcc\x83\x74\x83\x46\x81\x5b\x83\x59"; // 現在のフェーズ

} // namespace

void Game::registerStoryServices() {
    auto& m = machine;
    // 0x2bda44: a place by name (sorted hashes at defs +0x168) or by id
    // (+0x15c).
    auto placeOf = [this](const Call& c, std::size_t i) -> const PlaceRecord* {
        const Value& v = c.at(i);
        return v.isText() ? story.place(v.text) : story.place(v.toInt());
    };
    // 15500 (0x15bde0): a place's id (-1 when there is none).
    m.service(15500, [placeOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const PlaceRecord* p = placeOf(c, 0);
        return done(c, p ? p->id : -1);
    });
    // 15501 (0x15e914 -> 0x33c99c): the place's room: its first 2145 entry
    // whose named condition holds (from the bit set), else -1.
    m.service(15501, [this, placeOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const PlaceRecord* p = placeOf(c, 0);
        return done(c, p ? placeRoom(*this, *p) : -1);
    });
    // 15502 (0x15e8bc -> 0x33c960): the place's area (+6); area 118 reads
    // as 117 before step 775 (0x307).
    m.service(15502, [this, placeOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const PlaceRecord* p = placeOf(c, 0);
        if (!p) {
            return done(c, -1);
        }
        if (p->areaId == 0x76 && variable(kStep) < 0x307) {
            return done(c, 0x75);
        }
        return done(c, p->areaId);
    });
    // 15503 (0x15e9d4 -> 0x33ca00): the place's sound scene: its first 2144
    // entry whose named condition holds, else -1.
    m.service(15503, [this, placeOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const PlaceRecord* p = placeOf(c, 0);
        return done(c, p ? placeSound(*this, *p) : -1);
    });
    // 15504 (0x15e96c): the id of the place whose icon the place uses (its
    // own unless 2142 field 4 names another).
    m.service(15504, [this, placeOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const PlaceRecord* p = placeOf(c, 0);
        const PlaceRecord* icon = p ? story.place(p->iconPlace) : nullptr;
        return done(c, icon ? icon->id : -1);
    });
    // 15506 (0x160990): the place's German name.
    m.service(15506, [placeOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const PlaceRecord* p = placeOf(c, 0);
        if (!p) {
            return false;
        }
        c.result = Value::ofText(p->german);
        return true;
    });
    // 15511 (0x164e34 -> 0x2bb43c): the index of an exit of a place by name
    // (place, name / place, place), or of the current place (name); -1 when
    // there is none.
    m.service(15511, [this, placeOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const PlaceRecord* p = nullptr;
        std::string name;
        if (c.count() == 1) {
            p = story.place(variable(kPlace));
            name = c.at(0).isText() ? c.at(0).text : std::string();
        } else {
            p = placeOf(c, 0);
            if (!p) {
                return false;
            }
            if (c.count() == 2) {
                const PlaceRecord* to = placeOf(c, 1);
                if (!to) {
                    return false;
                }
                name = to->name;
            } else {
                name = c.at(1).isText() ? c.at(1).text : std::string();
            }
        }
        if (!p) {
            return false;
        }
        for (std::size_t i = 0; i < p->exits.size(); ++i) {
            if (p->exits[i].name == name) {
                return done(c, int(i));
            }
        }
        return done(c, -1);
    });
    // Events and steps by name or number.
    auto eventOf = [this](const Call& c, std::size_t i) -> const EventRecord* {
        const Value& v = c.at(i);
        return v.isText() ? story.event(v.text) : story.event(v.toInt());
    };
    auto stepOf = [this](const Call& c, std::size_t i) -> const Step* {
        const Value& v = c.at(i);
        return v.isText() ? story.step(v.text) : story.step(v.toInt());
    };
    // 15600 (0x15bbd0): an event's number (-1).
    m.service(15600, [eventOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const EventRecord* e = eventOf(c, 0);
        return done(c, e ? e->number : -1);
    });
    // 2202 (0x12aca0 -> 0x2cec2c): loads a script file (a path, as the
    // place loop loads res/plc/p<id>; the second operand, a scope level,
    // is not modelled: the current scope), 1 when it loaded. The events'
    // EventMapSetting_Auto builds a place's objects with it.
    m.service(2202, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const std::string* path = c.text(0);
        return done(c, path && loadScript(*path) ? 1 : 0);
    });
    // 2223 (0x14a91c -> 0x2cce08 with 1): the loop's second thread runs
    // (the block 2220 started).
    m.service(2223, [this](Call& c) {
        return done(c, loopThreads[1] && loopThreads[1]->running() ? 1 : 0);
    });
    // 16422 (0x160cd4): the puzzles handed to Nazoba (0x2abdd4 lists the
    // slots, game +0x89a4 in slot order = number - 1, whose state is not
    // solved and has bit 8, +0x8cdd / count +0x8e34; 150 slots, 165 in
    // Japanese): a negative operand the count, else the n-th slot (-1 past
    // the end).
    m.service(16422, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        std::vector<int> slots;
        const int count = language == 0 ? 0x96 + 15 : 0x87 + 15;
        for (const auto& rec : defs.puzzles) {
            const int slot = rec.number - 1;
            const auto it = progress.find(rec.id);
            if (slot >= 0 && slot < count && it != progress.end() &&
                (it->second.state & 3) != 2 && it->second.passedBy) {
                slots.push_back(slot);
            }
        }
        std::sort(slots.begin(), slots.end());
        const int n = c.integer(0);
        if (n < 0) {
            return done(c, int(slots.size()));
        }
        return done(c, std::size_t(n) < slots.size() ? slots[std::size_t(n)] : -1);
    });
    // 16504 (0x1631f4): the current event's number (game +0x197cc, 24
    // bits; made current by 0x184bcc), -1 without one.
    m.service(16504, [this](Call& c) { return done(c, currentEvent ? currentEvent->number : -1); });
    // 15604 (0x15e2d4): an event's puzzle (record +0xc: the puzzle's id).
    m.service(15604, [this, eventOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const EventRecord* e = eventOf(c, 0);
        const PuzzleRecord* p = e ? defs.puzzleByName(e->puzzle) : nullptr;
        return done(c, p ? p->id : -1);
    });
    // 15800 (0x15bd98): a step's id (-1).
    m.service(15800, [stepOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const Step* s = stepOf(c, 0);
        return done(c, s ? s->id : -1);
    });
    // 15801 (0x15e874): a step's goal text (record +0x10: 2201 field 3).
    m.service(15801, [stepOf](Call& c) {
        const Step* s = c.count() ? stepOf(c, 0) : nullptr;
        if (!s) {
            return false;
        }
        c.result = Value::ofText(s->goal);
        return true;
    });
    // 15802 (0x160750): a step's opening event (+8, 2201 field 2: the
    // heading names a 開始暗転イベント) by number, -1; the title's
    // continue plays it (10001 2 with it after a load).
    m.service(15802, [this, stepOf](Call& c) {
        const Step* s = c.count() ? stepOf(c, 0) : nullptr;
        if (!s) {
            return false;
        }
        const EventRecord* e = story.event(s->heading);
        return done(c, e ? e->number : -1);
    });
    // 15804 (0x1607b8): a step's goal place id (+0xc: 2201 field 4), -1.
    m.service(15804, [this, stepOf](Call& c) {
        const Step* s = c.count() ? stepOf(c, 0) : nullptr;
        if (!s) {
            return false;
        }
        const PlaceRecord* p = story.place(s->goalPlace);
        return done(c, p ? p->id : -1);
    });
    // 15000 (0x15bc68): an item's id (lt5_def 2021 by name; -1).
    m.service(15000, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const auto& rows = story.rows[2021];
        for (const auto& r : rows) {
            if (r.size() > 1 && (c.at(0).isText() ? r[1].isText() && r[1].text == c.at(0).text
                                                  : r[0].toInt() == c.integer(0))) {
                return done(c, r[0].toInt());
            }
        }
        return done(c, -1);
    });
    // 16553 (0x1648dc): game +0x19428 is set (*not identified*; 0 here).
    m.service(16553, [](Call& c) { return done(c, 0); });
    // 16552 (0x162400 -> 0x184844): the oldest item notice whose mask
    // matches (operands 4..: masks, or'ed; none: all) leaves the queue into
    // up to four registers {item, 0, kind, mask}; nothing waiting: {-1, 0,
    // 0, 0}.
    m.service(16552, [this](Call& c) {
        int mask = c.count() > 4 ? 0 : 0xffff;
        for (std::size_t i = 4; i < c.count(); ++i) {
            mask |= c.integer(i);
        }
        ItemNotice n{-1, 0, 0, 0};
        for (std::size_t i = 0; i < itemNotices.size(); ++i) {
            if (itemNotices[i].mask & mask) {
                n = itemNotices[i];
                itemNotices.erase(itemNotices.begin() + std::ptrdiff_t(i));
                break;
            }
        }
        const int out[4] = {n.item, n.argument, n.kind, n.mask};
        for (std::size_t i = 0; i < 4 && i < c.count(); ++i) {
            if (Value* r = c.reg(i)) {
                *r = Value::ofInt(out[i]);
            }
        }
        return done(c, 1);
    });
    // 16500 / 16501 / 16506 (0x1632d4 / 0x163250 / 0x1632a4): the current
    // place, map and step.
    m.service(16500, [this](Call& c) { return done(c, variable(kPlace)); });
    m.service(16501, [this](Call& c) { return done(c, variable(kMap)); });
    m.service(16506, [this](Call& c) { return done(c, variable(kStep)); });
    // 16510 (0x15ebb0 -> 0x1858b0): the current place by name or id (-1 and
    // result 0 when there is none).
    m.service(16510, [this, placeOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const PlaceRecord* p = placeOf(c, 0);
        setVariable(kPlace, p ? p->id : -1);
        return done(c, p ? 1 : 0);
    });
    // 16511 (0x15eb30 -> 0x18566c): the current map: a room (2121) by name
    // (0x33eb04) or id (0x2ab954); -1 and result 0 when there is none.
    m.service(16511, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const RoomRecord* r =
            c.at(0).isText() ? story.room(c.at(0).text) : story.room(c.integer(0));
        setVariable(kMap, r ? r->id : -1);
        return done(c, r ? 1 : 0);
    });
    // 16512 (0x15eaf0 -> 0x185094): the current guide: an area (2132) by
    // name (0x33ea70) or id (0x2ac078). A different area clears the guide's
    // icon lists (+0x12c54..+0x12c74; *not modelled yet*).
    m.service(16512, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const AreaRecord* found = nullptr;
        for (const auto& a : story.areas) {
            if (c.at(0).isText() ? a.name == c.at(0).text : a.id == c.integer(0)) {
                found = &a;
            }
        }
        setVariable(kGuide, found ? found->id : -1);
        return done(c, found ? 1 : 0);
    });
    // 16514 (0x15eb70 -> 0x185890 -> 0x2abbc4): the story enters a step by
    // name or id.
    m.service(16514, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const Step* s = c.at(0).isText() ? story.step(c.at(0).text) : story.step(c.integer(0));
        if (!s) {
            return done(c, 0);
        }
        enterStep(*this, *s);
        return done(c, 1);
    });
    // 16101 (0x14fe38 -> 0x185bac): one of four names (0 the player's;
    // 48 bytes each at +0x8694): a text, or "nothing" to clear it.
    m.service(16101, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        const int i = c.integer(0);
        if (i < 0 || i > 3) {
            return done(c, 0);
        }
        names[std::size_t(i)] = c.at(1).isText() ? c.at(1).text.substr(0, 47) : std::string();
        return done(c, 1);
    });
}

} // namespace picarat::adventure5

namespace picarat::adventure5 {

void Game::registerMessageServices() {
    auto& m = machine;
    using adventure56::MessageFlow;
    using adventure56::Object;
    auto object = [this](const Call& c) -> Object* {
        if (c.count() == 0) {
            return nullptr;
        }
        const Value& v = c.at(0);
        return scene.object(v.isText() ? xq::hash(v.text) : v.toUint());
    };
    auto flow = [](Object& o) -> MessageFlow& {
        if (!o.message) {
            o.message = std::make_shared<MessageFlow>();
        }
        return *o.message;
    };
    // 5911 (0x148d2c -> 0x14a500 -> 0x2c1bac -> 0x1703dc): the text [, show
    // at once (1), wait for a tap at the end (0), speed, window width,
    // height, text offset x, y]. With only the text it shows at once.
    m.service(5911, [this, object, flow](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        const Value& t = c.at(1);
        o->text = t.isText() ? t.text : std::string();
        const bool instant = c.count() > 2 ? c.at(2).truthy() : true;
        const bool waitAtEnd = c.count() > 3 ? c.at(3).truthy() : false;
        if (c.count() >= 7) {
            o->windowSize[0] = c.real(5);
            o->windowSize[1] = c.real(6);
        }
        if (c.count() >= 9) {
            o->textOffset[0] = c.real(7);
            o->textOffset[1] = c.real(8);
        }
        flow(*o).set(o->text, instant, waitAtEnd, this);
        if (c.count() > 4) {
            flow(*o).setSpeed(c.real(4));
        }
        return done(c);
    });
    // 5910: the text goes (0x1720ac).
    m.service(5910, [object](Call& c) {
        Object* o = object(c);
        if (!o) {
            return false;
        }
        o->text.clear();
        if (o->message) {
            o->message->clear();
        }
        return done(c);
    });
    // 5913 (0x15145c -> 0x17742c): a tap for the text (+0x162).
    m.service(5913, [object, flow](Call& c) {
        Object* o = object(c);
        if (!o) {
            return false;
        }
        flow(*o).tap();
        return done(c);
    });
    // 5920 (0x131dc4 -> vtable +0x78, 0x33d6e0): the whole text has played;
    // 5922 (0x13833c -> +0x80, 0x33d438): it waits at a mark for a tap.
    // Texts without a component answer 0.
    m.service(5920, [object](Call& c) {
        Object* o = object(c);
        if (!o) {
            return false;
        }
        return done(c, o->message && !o->message->empty() && o->message->done() ? 1 : 0);
    });
    m.service(5922, [object](Call& c) {
        Object* o = object(c);
        if (!o) {
            return false;
        }
        return done(c, o->message && o->message->waitingMark() ? 1 : 0);
    });
    // 5921 (0x1382dc -> 0x33dbfc -> +0x7c): the speaker's mouth moves.
    m.service(5921, [object](Call& c) {
        Object* o = object(c);
        if (!o) {
            return false;
        }
        return done(c, o->message && o->message->talking() ? 1 : 0);
    });
    // 3220 (0x134b40 -> 0x3523cc): the key of a text list's n-th text (-1).
    m.service(3220, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        const Value& v = c.at(0);
        adventure56::Resource* r = scene.resource(v.isText() ? xq::hash(v.text) : v.toUint());
        if (!r || !r->text) {
            c.result = Value::ofInt(-1);
            return false;
        }
        int n = c.integer(1);
        for (const auto& [key, pages] : r->text->entries) {
            if (n-- == 0) {
                return done(c, key);
            }
        }
        return done(c, -1);
    });
    // 5951 (0x1441d4 -> +0x74, 0x170604): characters per frame.
    m.service(5951, [object, flow](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        flow(*o).setSpeed(c.real(1));
        return done(c);
    });
    // 5953 (0x143fb0 -> 0x2c43b8): the typing sound [, its interval (5)].
    m.service(5953, [object, flow](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        flow(*o).setTypingSound(c.integer(1), c.count() > 2 ? c.integer(2) : 5);
        return done(c);
    });
    // 5955 (0x143e6c -> 0x177634): the speaking character (+0x170).
    m.service(5955, [object, flow](Call& c) {
        Object* o = object(c);
        if (!o || c.count() < 2) {
            return false;
        }
        flow(*o).speaker = c.integer(1);
        return done(c);
    });
}

void Game::setCharacterMotion(adventure56::Object& o, std::array<int, 3> numbers) {
    // 0x18b0dc: each list (motions, face motions) on layers 0..2, the first
    // motion named "%01u%02d_" (layer, number).
    if (!o.model) {
        return;
    }
    for (int layer = 0; layer < 3; ++layer) {
        char prefix[16];
        std::snprintf(prefix, sizeof prefix, "%01u%02d_", unsigned(layer),
                      numbers[std::size_t(layer)]);
        auto starts = [&](const std::string& name) {
            return name.rfind(prefix, 0) == 0;
        };
        if (layer == 0) {
            for (const auto& mo : o.model->motions) {
                if (starts(mo.name)) {
                    if (o.motion != mo.name || o.motionDone) {
                        o.motionTime = 0;
                    }
                    o.motion = mo.name;
                    o.motionOnce = false;
                    o.motionDone = false;
                    break;
                }
            }
        }
        for (const auto& mo : o.model->faceMotions) {
            if (starts(mo.name)) {
                auto& f = o.faces[std::size_t(layer)];
                if (f.motion != mo.name) {
                    f.time = 0;
                }
                f.motion = mo.name;
                break;
            }
        }
    }
}

std::array<float, 3> Game::projectToScreen(int screen, float x, float y, float z) const {
    // 0x2cbb48 (4402, the ruins' notice): a world point through the screen's
    // camera to its place across and down the screen (fractions; the middle
    // when behind the eye) and its depth. *Hypothesis* (see 4402).
    const auto v = scene.viewFor(screen);
    const float d[3] = {x - v.eye[0], y - v.eye[1], z - v.eye[2]};
    auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    const float depth = dot(d, v.forward);
    const float w = screen == 1 ? 320.0f : 400.0f, h = 240.0f;
    const float f = std::tan(v.fovY * 0.5f);
    float u = 0.5f, vv = 0.5f;
    if (depth > 1e-6f) {
        u = (dot(d, v.right) / depth / (f * (w / h)) + 1) * 0.5f;
        vv = (1 - dot(d, v.up) / depth / f) * 0.5f;
    }
    return {u, vv, depth};
}

const Game::AreaPaths& Game::areaPaths() {
    // The shown area's file (loaded when it changes).
    if (areaPaths_.area == gmpShown) {
        return areaPaths_;
    }
    areaPaths_ = {};
    areaPaths_.area = gmpShown;
    char path[48];
    std::snprintf(path, sizeof path, "res/lt5_gmp.xb/g%03d.xs", gmpShown);
    const auto file = pack.find(path, "");
    if (gmpShown < 0 || file.empty()) {
        return areaPaths_;
    }
    const auto list = import::adventure56::parseXscr(pack.read(file));
    auto text = [&](const import::adventure56::Operand& o) -> std::string {
        if ((o.type & 15) != 8 || o.value >= list.strings.size()) {
            return {};
        }
        const auto* s = reinterpret_cast<const char*>(list.strings.data() + o.value);
        return std::string(s, strnlen(s, list.strings.size() - o.value));
    };
    for (const auto& cmd : list.commands) {
        const auto* op = list.operands.data() + cmd.first;
        if (cmd.opcode == 2801 && cmd.count >= 3) {
            areaPaths_.spots.push_back({text(op[0]), std::int32_t(op[1].value),
                                        std::int32_t(op[2].value),
                                        cmd.count > 3 ? std::int32_t(op[3].value) : 0});
        } else if (cmd.opcode == 2802 && cmd.count >= 4) {
            AreaPaths::Route r{text(op[0]), text(op[1]), {}};
            for (unsigned i = 2; i + 1 < cmd.count; i += 2) {
                r.points.push_back({std::int32_t(op[i].value), std::int32_t(op[i + 1].value)});
            }
            areaPaths_.routes.push_back(std::move(r));
        }
    }
    // A route runs from its first place's spot through the listed points to
    // the second place's spot (count - 2 inner points, 16571's k form; the
    // single-point routes of g102 need the ends).
    for (auto& r : areaPaths_.routes) {
        for (const auto& s : areaPaths_.spots) {
            if (s.place == r.from) {
                r.points.insert(r.points.begin(), {s.x, s.y});
                break;
            }
        }
        for (const auto& s : areaPaths_.spots) {
            if (s.place == r.to) {
                r.points.push_back({s.x, s.y});
                break;
            }
        }
    }
    return areaPaths_;
}

void Game::removeRoom(int screen) {
    if (screen < 0 || screen >= 5 || screenRooms[std::size_t(screen)] < 0) {
        return;
    }
    char name[32];
    std::snprintf(name, sizeof name, "MAP_%04d", screenRooms[std::size_t(screen)]);
    scene.remove3D(xq::hash(name));
    scene.remove3D(xq::hash(std::string(name) + "_NEAR"));
    scene.removeResource(xq::hash(name));
    screenRooms[std::size_t(screen)] = -1;
}

void Game::releaseAreaMap(unsigned slots) {
    // The lower screen's objects of kinds 5 and 11 (0x2cc090) and the
    // slots' resources.
    std::vector<std::uint32_t> gone;
    for (const auto& [h, o] : scene.objects()) {
        if (o->screen == 1 && (o->kind == 5 || o->kind == 11)) {
            gone.push_back(h);
        }
    }
    for (const auto h : gone) {
        scene.remove(h);
    }
    for (int slot = 1; slot <= 2; ++slot) {
        if (!(slots & unsigned(slot))) {
            continue;
        }
        for (const char* f : {"gmp_anm_icn%d", "gmp_anm_obj%d", "gmp_img%d"}) {
            char name[32];
            std::snprintf(name, sizeof name, f, slot);
            scene.removeResource(xq::hash(name));
        }
    }
    gmpShown = -1;
}

xq::Value Game::messageEvent(int code, std::span<const xq::Value> args) {
    // 0x33336c (the game's vtable +0xa8).
    auto arg = [&](std::size_t i) {
        return i < args.size() ? args[i].toInt() : 0;
    };
    switch (code) {
    case 0: // an effect sounding on that number's player (0x33b570)
        return Value::ofInt(effectsSounding(arg(0)) ? 1 : 0);
    case 1: // an effect (0x2bf9fc)
        return Value::ofInt(playEffect(arg(0)) ? 1 : 0);
    case 2: // an effect stops [over frames] (0x2bf988)
        stopEffect(arg(0), arg(1));
        return Value::ofInt(1);
    case 3: // +0xf4: a player sounding (*hypothesis*: as playing)
    case 5: // +0xfc: playing (11042)
        return Value::ofInt(streams.playing(arg(0)) ? 1 : 0);
    case 4: // +0xf8: preparing (11041)
        return Value::ofInt(streams.preparing(arg(0)) ? 1 : 0);
    case 7: // a stream on player 2 (0x2bc7a0): number, fade, auto-start
        return Value::ofInt(playStream(2, arg(0), arg(1), arg(2) != 0) ? 1 : 0);
    case 12: // a stream player's volume (0x35100c -> 0x2019b0): player, volume
        if (args.size() > 1 && sound) {
            sound->setStreamVolume(adventure56::StreamPlayers::internal(arg(0)), args[1].toFloat());
        }
        return Value::ofInt(1);
    case 8: // the current event's voice line on player 1 (0x2bc724): line, fade, auto-start
        return Value::ofInt(playEventVoice(arg(0), arg(1), arg(2) != 0) ? 1 : 0);
    case 0x10: { // the speaker's typing sound (40 + its 2111 field 4)
        const CharacterRecord* ch = story.character(arg(0));
        return ch ? Value::ofInt(40 + ch->typing) : Value{};
    }
    case 0x16: { // <M a/b/c[/d]>: the speaker's 3D character's motions
        const CharacterRecord* ch = story.character(arg(0));
        adventure56::Object* o = ch ? scene.object3D(xq::hash(ch->name)) : nullptr;
        if (!o) {
            return Value::ofInt(0);
        }
        setCharacterMotion(*o, {arg(1), arg(2), arg(3)});
        return Value::ofInt(1);
    }
    case 0x19: // <N n>: name n (16101's; 0 the player's)
        if (arg(0) >= 0 && arg(0) < 4) {
            return Value::ofText(names[std::size_t(arg(0))]);
        }
        return Value{};
    case 10: // +0x110: a player stops (11044) over frames
        streams.stop(arg(0), arg(1));
        if (sound) {
            sound->stopStream(adventure56::StreamPlayers::internal(arg(0)), arg(1));
        }
        return Value::ofInt(1);
    default:
        if (log) {
            log("message event " + std::to_string(code) + " not reconstructed");
        }
        return Value{};
    }
}

} // namespace picarat::adventure5

namespace picarat::adventure5 {

void Game::registerStageServices() {
    auto& m = machine;
    using adventure56::Object;
    auto object3D = [this](const Call& c, std::size_t i = 0) -> Object* {
        if (c.count() <= i) {
            return nullptr;
        }
        const Value& v = c.at(i);
        return scene.object3D(v.isText() ? xq::hash(v.text) : v.toUint());
    };
    auto characterOf = [this](const Call& c, std::size_t i) -> const CharacterRecord* {
        const Value& v = c.at(i);
        return v.isText() ? story.character(v.text) : story.character(v.toInt());
    };
    // Every 3D object of a screen (-1: all) and kind (above 0x1a: any) -
    // 0x32e0fc / 0x351248 / 0x2c1334 walk the 3D list so.
    auto each3D = [this](int screen, int kind, auto fn) {
        for (const auto& [h, o] : scene.objects(true)) {
            if (screen != -1 && o->screen != screen) {
                continue;
            }
            if (kind <= 0x1a && o->kind != kind) {
                continue;
            }
            fn(*o);
        }
    };
    // 14000 (0x152160 -> 0x332c50): the event's characters (screen,
    // names...): characters (kind 1) not named go; each named one not there
    // yet is made (0x292aa8: priority 0x82, the model chr/c%03d.xc of its
    // 2111 record, alpha 0, touch off, 0x177558 30.0).
    m.service(14000, [this, each3D](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const int screen = c.integer(0);
        std::vector<std::string> names;
        for (std::size_t i = 1; i < c.count() && i <= 16; ++i) {
            if (c.at(i).isText()) {
                names.push_back(c.at(i).text);
            }
        }
        std::vector<std::uint32_t> gone;
        each3D(-1, 1, [&](Object& o) {
            if (std::find(names.begin(), names.end(), o.name) == names.end()) {
                gone.push_back(o.hash);
            }
        });
        for (const auto h : gone) {
            scene.remove3D(h);
        }
        for (const auto& n : names) {
            if (scene.object3D(xq::hash(n))) {
                continue;
            }
            Object& o = scene.create3D(n, screen, 0x82, 1);
            o.values.assign(16, Value{});
            if (const CharacterRecord* ch = story.character(n); ch && ch->model > 0) {
                char path[32];
                std::snprintf(path, sizeof path, "chr/c%03d", ch->model);
                if (const auto folder = pack.find(path, ".xc"); !folder.empty()) {
                    o.model = adventure56::loadModel(pack, folder);
                    o.model->path = path;
                }
            }
            o.alpha.set({0.0f});
            o.touchOn = false;
        }
        return done(c, 1);
    });
    // 6850 (0x149420 -> 0x14a1e8 -> 0x2c1c18 -> 0x18b0dc): a character's
    // motions by number: a, b, c [, blend frames [, speed]]. Each of the
    // model's three lists (motions +0x108, face motions +0x11c, material
    // motions +0x130) takes, on layer n, the first motion whose name starts
    // "%01u%02d_" (n, the number) (0x2a9f00 / 0x2a9d24 / 0x2a9b38 ->
    // 0x2613cc); a layer without one keeps what it plays. *Not modelled*:
    // the blend frames (the switch is at once) and the speed (+0x1b4).
    m.service(6850, [this, object3D](Call& c) {
        Object* o = object3D(c);
        if (!o || c.count() < 4) {
            return false;
        }
        if (!o->model) {
            return done(c, 0);
        }
        setCharacterMotion(*o, {c.integer(1), c.integer(2), c.integer(3)});
        return done(c, 1);
    });
    // 6841 (0x141b30 -> 0x2c5604): a character's lip flag (+0x770 bit 3):
    // its body motions play their "*" twins (Scene::updateObject). *Not
    // reconstructed*: the mouth animation it may also drive.
    m.service(6841, [object3D](Call& c) {
        Object* o = object3D(c);
        if (!o || c.count() < 2) {
            return false;
        }
        o->lipSync = c.at(1).truthy();
        return done(c, 1);
    });
    // 6842 (0x151740 -> 0x18e670): a character's switches (flag [, n [,
    // m]]) on each motion list's bytes +0x11/+0x12/+0x13 (*role not
    // identified*: kept).
    m.service(6842, [object3D](Call& c) {
        Object* o = object3D(c);
        if (!o || c.count() < 2) {
            return false;
        }
        return done(c, 1);
    });
    // 6261 (0x14d214 -> 0x2c1334): every 3D object of a screen and kind:
    // alpha over frames [, curve]; 6260 (0x1618e4 -> 0x32e0fc): any of them
    // still fading; 6322 (0x139af0 -> 0x351248 -> 0x1a803c): alpha at once.
    m.service(6261, [each3D](Call& c) {
        if (c.count() < 4) {
            return false;
        }
        const float alpha = c.real(2), frames = c.real(3);
        const int curve = c.count() > 4 ? c.integer(4) : 0;
        each3D(c.integer(0), c.integer(1),
               [&](Object& o) { o.alpha.start({alpha}, frames, curve); });
        return done(c, 1);
    });
    m.service(6260, [each3D](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        bool moving = false;
        each3D(c.integer(0), c.integer(1), [&](Object& o) {
            if (o.visible && o.alpha.moving()) {
                moving = true;
            }
        });
        return done(c, moving ? 1 : 0);
    });
    m.service(6322, [each3D](Call& c) {
        if (c.count() < 3) {
            return false;
        }
        const float alpha = c.real(2);
        each3D(c.integer(0), c.integer(1), [&](Object& o) { o.alpha.set({alpha}); });
        return done(c, 1);
    });
    // 15200 (0x15bb30): a character's id; 15203 (0x15bb80): its typing
    // sound (40 + record +4 >> 30); 15204 (0x163f08): its four numbers
    // (2111 fields 5, 6 x 10, 7, 8 in radians) into registers; 15201
    // (0x15e1fc -> 0x33c114): its name plate picture "chr/@/c%03d.xi" (2111
    // field 3; "" without one).
    m.service(15200, [characterOf](Call& c) {
        const CharacterRecord* ch = c.count() ? characterOf(c, 0) : nullptr;
        return done(c, ch ? ch->id : -1);
    });
    m.service(15201, [characterOf](Call& c) {
        const CharacterRecord* ch = c.count() ? characterOf(c, 0) : nullptr;
        char path[32] = "";
        if (ch && ch->other != 0) {
            std::snprintf(path, sizeof path, "chr/@/c%03d.xi", ch->other);
        }
        c.result = Value::ofText(path);
        return true;
    });
    // 15206 (0x163164 -> 0x33c02c): its "ex" picture "chr/@/c%03d_ex.xi"
    // (the same 2111 field as the plate; "" without one).
    m.service(15206, [characterOf](Call& c) {
        const CharacterRecord* ch = c.count() ? characterOf(c, 0) : nullptr;
        char path[32] = "";
        if (ch && ch->other != 0) {
            std::snprintf(path, sizeof path, "chr/@/c%03d_ex.xi", ch->other);
        }
        c.result = Value::ofText(path);
        return true;
    });
    m.service(15203, [characterOf](Call& c) {
        const CharacterRecord* ch = c.count() ? characterOf(c, 0) : nullptr;
        return done(c, ch ? 40 + ch->typing : -1);
    });
    m.service(15204, [characterOf](Call& c) {
        const CharacterRecord* ch = c.count() >= 5 ? characterOf(c, 0) : nullptr;
        if (!ch) {
            return false;
        }
        const float scale[4] = {10.0f, 10.0f, 0.017453292f, 0.017453292f};
        for (std::size_t i = 0; i < 4; ++i) {
            if (Value* r = c.reg(1 + i)) {
                *r = Value::ofFloat(ch->values[i] * scale[i]);
            }
        }
        return done(c, 1);
    });
    // 2610 (0x12a178): the cutscene clock (game +0x10b8) restarts; 2611
    // (0x12f828): its time. *Hypothesis*: it counts the frame step (display
    // frames) every game frame.
    m.service(2610, [this](Call& c) {
        dramaClock = 0;
        return done(c, 1);
    });
    m.service(2611, [this](Call& c) {
        c.result = Value::ofFloat(dramaClock);
        return true;
    });
    // 4111 / 4131 (0x13dc88 / 0x13e580): a screen camera's eye / target
    // (+0x344 / +0x3c8) into three registers.
    for (const int op : {4111, 4131}) {
        m.service(op, [this, op](Call& c) {
            if (c.count() < 4) {
                return false;
            }
            const int s = c.integer(0);
            if (s < 0 || s >= adventure56::Scene::kScreens) {
                return false;
            }
            const auto& cam = scene.cameras[std::size_t(s)];
            const auto v = op == 4111 ? cam.eye.value() : cam.target.value();
            for (std::size_t i = 0; i < 3; ++i) {
                if (Value* r = c.reg(1 + i)) {
                    *r = Value::ofFloat(v[i]);
                }
            }
            return done(c, 1);
        });
    }
    // 4205 (0x151dc8 -> 0x32e460 -> the camera's vtable +0xb8): the stereo
    // distance's offset (the 4200 family: +5). Kept; the flat render has
    // no stereo depth.
    m.service(4205, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        const int s = c.integer(0);
        if (s < 0 || s >= adventure56::Scene::kScreens) {
            return false;
        }
        scene.cameras[std::size_t(s)].stereoDistanceOffset = c.real(1);
        return done(c, 1);
    });
    // 5320 (0x156acc -> 0x351494): any shown 2D object of a screen (-1 all)
    // and kind (above 0x34: any; the object's byte +0x436) still fading.
    m.service(5320, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        const int screen = c.integer(0), kind = c.integer(1);
        for (const auto& [h, o] : scene.objects()) {
            if (screen != -1 && o->screen != screen) {
                continue;
            }
            if (kind < 0x35 && o->kind != kind) {
                continue;
            }
            if (o->alpha.moving()) {
                return done(c, 1);
            }
        }
        return done(c, 0);
    });
    // The colour of every 2D object of a screen (-1 all) and kind (above
    // 0x34: any), as 5110 / 5112 / 5113 do for one: 5329 (0x156b30 ->
    // 0x351398) any still changing; 5330 (0x136004 -> 0x350f18, 0x1a8118)
    // set {r, g, b}; 5331 (0x1360b8 -> 0x2ca44c, 0x1a815c) tween to {r, g,
    // b} over frames [curve] (an eighth operand, 0x2d029c, is not read
    // yet). The events dim the area map (screen 1, kind 11) this way.
    auto eachColoured = [this](const Call& c, auto&& f) {
        const int screen = c.integer(0), kind = c.integer(1);
        if (screen > 4) {
            return false;
        }
        for (const auto& [h, o] : scene.objects()) {
            if ((screen == -1 || o->screen == screen) && (kind >= 0x35 || o->kind == kind) &&
                f(*o)) {
                return true;
            }
        }
        return false;
    };
    m.service(5329, [eachColoured](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        return done(c, eachColoured(c, [](adventure56::Object& o) { return o.colour.moving(); }));
    });
    m.service(5330, [eachColoured](Call& c) {
        if (c.count() < 5) {
            return false;
        }
        const std::array<float, 3> rgb = {c.real(2), c.real(3), c.real(4)};
        eachColoured(c, [&](adventure56::Object& o) {
            o.colour.set(rgb);
            return false;
        });
        return done(c, c.integer(0) <= 4 ? 1 : 0);
    });
    m.service(5331, [eachColoured](Call& c) {
        if (c.count() < 6) {
            return false;
        }
        const std::array<float, 3> rgb = {c.real(2), c.real(3), c.real(4)};
        const float frames = c.real(5);
        const int curve = c.count() > 6 ? c.integer(6) : 0;
        eachColoured(c, [&](adventure56::Object& o) {
            o.colour.start(rgb, frames, curve);
            return false;
        });
        return done(c, c.integer(0) <= 4 ? 1 : 0);
    });
    // 5431 (0x12dc38 -> 0x198550): a picture object's shader: 0 the frame
    // look ("_FIX_FRM", or "_FIX_SEPIA" with the third operand), else the
    // plain image ("_FIX_IMG"). Kept: the sepia look is not drawn yet.
    m.service(5431, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        const Value& v = c.at(0);
        adventure56::Object* o = scene.object(v.isText() ? xq::hash(v.text) : v.toUint());
        if (!o) {
            return false;
        }
        o->pictureShader = c.integer(1) != 0 ? 0 : (c.count() > 2 && c.at(2).truthy() ? 2 : 1);
        return done(c, 1);
    });
    // Camera tweens (0x2c93d8): 4103 / 4123 the eye / target to (x, y, z)
    // over frames [, curve]; 4104 / 4124 (0x13e1f4 ...) the same relative
    // to where it is (a running tween stops first).
    auto cameraTween = [this](Call& c, bool target, bool relative) {
        if (c.count() < 5) {
            return false;
        }
        const int s = c.integer(0);
        if (s < 0 || s >= adventure56::Scene::kScreens) {
            return false;
        }
        auto& cam = scene.cameras[std::size_t(s)];
        auto& v = target ? cam.target : cam.eye;
        auto to = std::array<float, 3>{c.real(1), c.real(2), c.real(3)};
        if (relative) {
            const auto now = v.value();
            v.set(now);
            for (std::size_t i = 0; i < 3; ++i) {
                to[i] += now[i];
            }
        }
        v.start(to, c.real(4), c.count() > 5 ? c.integer(5) : 0);
        return done(c, 1);
    };
    m.service(4103, [cameraTween](Call& c) { return cameraTween(c, false, false); });
    m.service(4104, [cameraTween](Call& c) { return cameraTween(c, false, true); });
    m.service(4123, [cameraTween](Call& c) { return cameraTween(c, true, false); });
    m.service(4124, [cameraTween](Call& c) { return cameraTween(c, true, true); });
    // 4100 (0x158078: the camera's vtable +0x68), 4120 and 4125: a tween of
    // the eye's base, the target's base, the target's offset still runs
    // (*hypothesis*: the groups of five, moving / get / set / tween /
    // relative tween, as 4105..4109 are the eye offset's). Place_Change
    // waits on 4100.
    auto moving = [this](Call& c, adventure56::Tween<3> adventure56::Camera::* field) {
        const int s = c.count() ? c.integer(0) : -1;
        if (s < 0 || s >= adventure56::Scene::kScreens) {
            return false;
        }
        return done(c, (scene.cameras[std::size_t(s)].*field).moving() ? 1 : 0);
    };
    m.service(4100, [moving](Call& c) { return moving(c, &adventure56::Camera::eye); });
    m.service(4120, [moving](Call& c) { return moving(c, &adventure56::Camera::target); });
    m.service(4125, [moving](Call& c) { return moving(c, &adventure56::Camera::targetOffset); });
    // 12000 (0x14f46c -> 0x2c07a8): a room (2121, by name or id) [on a
    // screen (0)]: unless that screen shows it already, the model
    // map/m%04d%c.xc (map number, variant) as "MAP_%04d" (room id), a 3D
    // object of that name (priority 0x6e, kind 2) at fields 5..7 x 10, and
    // with field 10 >= 0 a second one "MAP_%04d_NEAR" (priority 0x78). The
    // result is the name "MAP_%04d"; the screen's room is kept (game
    // +0x197e2, 12005). Field 10 splits the model's mesh layers between the
    // two objects (0x2cd8cc's set-up blocks +0 / +2).
    m.service(12000, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const RoomRecord* r =
            c.at(0).isText() ? story.room(c.at(0).text) : story.room(c.integer(0));
        const int screen = c.count() > 1 ? c.integer(1) : 0;
        if (!r || screen < 0 || screen >= 5) {
            c.result = Value{};
            return true;
        }
        char name[32];
        std::snprintf(name, sizeof name, "MAP_%04d", r->id);
        if (screenRooms[std::size_t(screen)] != r->id || !scene.object3D(xq::hash(name))) {
            removeRoom(screen);
            char path[32];
            std::snprintf(path, sizeof path, "map/m%04d%s", r->map, r->variant.c_str());
            if (!scene.resource(name)) {
                if (const auto folder = pack.find(path, ".xc"); !folder.empty()) {
                    adventure56::Resource res;
                    res.kind = adventure56::Resource::Kind::Model;
                    res.path = path;
                    res.model = adventure56::loadModel(pack, folder);
                    res.model->path = path;
                    scene.addResource(name, std::move(res), 0);
                }
            }
            auto place = [&](const char* objectName, int priority) {
                adventure56::Object& o = scene.create3D(objectName, screen, priority, 2, 0);
                o.values.assign(16, Value{});
                if (adventure56::Resource* res = scene.resource(name); res && res->model) {
                    o.model = res->model;
                }
                auto at = o.position.value();
                for (std::size_t i = 0; i < 3; ++i) {
                    at[i] = (5 + i < r->fields.size() ? r->fields[5 + i].toFloat() : 0) * 10.0f;
                }
                o.position.set(at);
                return &o;
            };
            adventure56::Object* map = place(name, 0x6e);
            const int near = r->fields.size() > 10 ? r->fields[10].toInt() : -1;
            if (near >= 0) {
                // 0x2cd8cc's set-up blocks: the map draws the mesh layers
                // 0..near-1, the near map near..0x3f (Mesh::layer).
                map->layerFirst = 0;
                map->layerLast = near - 1;
                char nearName[40];
                std::snprintf(nearName, sizeof nearName, "MAP_%04d_NEAR", r->id);
                // 0x1a803c with 0.0: the near map starts invisible.
                adventure56::Object* nearMap = place(nearName, 0x78);
                nearMap->alpha.set({0.0f});
                nearMap->layerFirst = near;
                nearMap->layerLast = 0x3f;
            }
            screenRooms[std::size_t(screen)] = r->id;
        }
        c.result = Value::ofText(name);
        return true;
    });
    // 12003 (0x162330 -> 0x332f48): {screen a, screen b} the two screens
    // swap rooms: the room ids (game +0x197e2) change places and each
    // room's "MAP_%04d" and "MAP_%04d_NEAR" objects move to the other
    // screen (0x1c4924, the object's +0x12d). A screen without a room
    // gives none and keeps its own id. prog_place builds a new place's
    // room on screen 3 and swaps it onto screen 0 (Place_Change).
    m.service(12003, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        const int a = c.integer(0), b = c.integer(1);
        if (a < 0 || a >= 5 || b < 0 || b >= 5) {
            return false;
        }
        auto move = [this](int room, int screen) {
            for (const char* format : {"MAP_%04d", "MAP_%04d_NEAR"}) {
                char name[40];
                std::snprintf(name, sizeof name, format, room);
                if (auto* o = scene.object3D(xq::hash(name))) {
                    o->screen = screen;
                }
            }
        };
        const int roomA = screenRooms[std::size_t(a)], roomB = screenRooms[std::size_t(b)];
        if (story.room(roomA)) {
            move(roomA, b);
            screenRooms[std::size_t(b)] = roomA;
        } else {
            screenRooms[std::size_t(b)] = -1;
        }
        if (story.room(roomB)) {
            move(roomB, a);
            screenRooms[std::size_t(a)] = roomB;
        }
        return done(c, 1);
    });
    // 12004 (0x15c014 -> 0x2bd950): a screen's room shown or not (its
    // "MAP_%04d_NEAR" and "MAP_%04d", as 5005); 1 when the room is there.
    m.service(12004, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        const int s = c.integer(0);
        if (s < 0 || s >= 5 || screenRooms[std::size_t(s)] < 0) {
            return done(c, 0);
        }
        char name[40];
        std::snprintf(name, sizeof name, "MAP_%04d_NEAR", screenRooms[std::size_t(s)]);
        if (adventure56::Object* o = scene.object3D(xq::hash(name))) {
            o->visible = c.at(1).truthy();
        }
        std::snprintf(name, sizeof name, "MAP_%04d", screenRooms[std::size_t(s)]);
        adventure56::Object* o = scene.object3D(xq::hash(name));
        if (!o) {
            return done(c, 0);
        }
        o->visible = c.at(1).truthy();
        return done(c, 1);
    });
    // Rooms (2121) by name or id: 15300 (0x15bcb0) the id (-1); 15307
    // (0x163f90) the position the room's map stands at (record +0x10..:
    // fields 5..7 x 10, as 12000 places it) into three registers; 15312
    // (0x15e664 -> 0x2bcfdc) its map's path "map/m%04d%c".
    auto roomOf = [this](const Call& c) {
        return c.at(0).isText() ? story.room(c.at(0).text) : story.room(c.integer(0));
    };
    m.service(15300, [roomOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const RoomRecord* r = roomOf(c);
        return done(c, r ? r->id : -1);
    });
    m.service(15307, [roomOf](Call& c) {
        if (c.count() < 4) {
            return false;
        }
        const RoomRecord* r = roomOf(c);
        if (!r) {
            return done(c, 0);
        }
        for (std::size_t i = 0; i < 3; ++i) {
            if (Value* reg = c.reg(1 + i)) {
                *reg = Value::ofFloat(
                    (5 + i < r->fields.size() ? r->fields[5 + i].toFloat() : 0) * 10.0f);
            }
        }
        return true;
    });
    m.service(15312, [roomOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const RoomRecord* r = roomOf(c);
        char path[32] = "";
        if (r) {
            std::snprintf(path, sizeof path, "map/m%04d%s", r->map, r->variant.c_str());
        }
        c.result = Value::ofText(path);
        return true;
    });
    // 12005 (0x1522d8 -> 0x351678): the name "MAP_%04d" of a screen's room,
    // or nothing.
    m.service(12005, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const int s = c.integer(0);
        if (s < 0 || s >= 5 || screenRooms[std::size_t(s)] < 0) {
            c.result = Value{};
            return true;
        }
        char name[32];
        std::snprintf(name, sizeof name, "MAP_%04d", screenRooms[std::size_t(s)]);
        c.result = Value::ofText(name);
        return true;
    });
    // 16430 (0x163364 -> 0x184e4c): the passed-by list is emptied; 16431
    // (0x16493c): its length (operand < 0) or the n-th puzzle's id (-1).
    m.service(16430, [this](Call& c) {
        passedBy.clear();
        return done(c, 1);
    });
    m.service(16431, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const int n = c.integer(0);
        if (n < 0) {
            return done(c, int(passedBy.size()));
        }
        if (std::size_t(n) >= passedBy.size()) {
            return done(c, -1);
        }
        const int index = passedBy[std::size_t(n)];
        return done(c, index >= 0 && std::size_t(index) < defs.puzzles.size()
                           ? defs.puzzles[std::size_t(index)].id
                           : -1);
    });
    // 15311 (0x165c74): a room's field 9 x 10 (record +0x20) as a number.
    m.service(15311, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const RoomRecord* r =
            c.at(0).isText() ? story.room(c.at(0).text) : story.room(c.integer(0));
        c.result =
            Value::ofFloat(r && r->fields.size() > 9 ? r->fields[9].toFloat() * 10.0f : 0.0f);
        return true;
    });
    // 5325 (0x1485d4 -> 0x1c2f68): every 2D object of a screen (-1 all) and
    // kind (255: any) shown or hidden (vtable +0xc, as 5005).
    m.service(5325, [this](Call& c) {
        if (c.count() < 3) {
            return false;
        }
        const int screen = c.integer(0), kind = c.integer(1);
        const bool on = c.at(2).truthy();
        for (const auto& [h, o] : scene.objects()) {
            if ((screen == -1 || o->screen == screen) && (kind == 0xff || o->kind == kind)) {
                o->visible = on;
            }
        }
        return done(c, 1);
    });
    // 6003 (0x132c58 -> 0x2d1b00): 6002 outside the scope levels (as 5003);
    // the result is the object's hash. 6020 (0x132e94) / 5020 (0x131f84):
    // a 3D / 2D object's screen (byte +0x12d; under a parent, the
    // parent's, 0x2cbf2c). The places hand it to the camera services.
    m.service(6003, [this](Call& c) {
        if (c.count() < 4) {
            return false;
        }
        adventure56::Object& o = scene.create3D(c.at(0).isText() ? c.at(0).text : std::string(),
                                                c.integer(1), c.integer(2), c.integer(3));
        o.values.assign(16, Value{});
        for (std::size_t i = 0; i < c.count() && i < 16; ++i) {
            o.values[i] = c.at(i);
        }
        c.result = Value::ofUint(o.hash);
        return true;
    });
    auto screenOf = [this](bool solid, adventure56::Object* o) {
        for (int depth = 0; o && o->parent != 0 && depth < 16; ++depth) {
            adventure56::Object* p = scene.find(solid, o->parent);
            if (!p) {
                break;
            }
            o = p;
        }
        return o->screen;
    };
    m.service(6020, [object3D, screenOf](Call& c) {
        adventure56::Object* o = object3D(c);
        return o ? done(c, screenOf(true, o)) : false;
    });
    m.service(5020, [this, screenOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const Value& v = c.at(0);
        adventure56::Object* o = scene.object(v.isText() ? xq::hash(v.text) : v.toUint());
        return o ? done(c, screenOf(false, o)) : false;
    });
    // The lower screen's area map, double-buffered in slots 1 and 2 (game
    // +0x197ec shown area, +0x197ee prepared area, +0x197f0 the shown
    // slot). 12100 (0x14f374 -> 0x2c0c00): an area (2132, by name or id)
    // [, again] is prepared in the other slot unless shown: its first
    // choice whose named condition holds gives the picture gmp/g%02d.xi
    // ("gmp_img<slot>") and, with bit 15, the animation set gmp/g%02d.xa
    // ("gmp_anm_obj<slot>"); the icons spr/@/icn_gmp.xa (icn_gmp_o.xa while
    // 過去 is set; area 118: icn_htl.xa) as "gmp_anm_icn<slot>".
    m.service(12100, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const AreaRecord* area = nullptr;
        for (const auto& a : story.areas) {
            if (c.at(0).isText() ? a.name == c.at(0).text : a.id == c.integer(0)) {
                area = &a;
            }
        }
        if (!area) {
            return done(c, 0);
        }
        const bool again = c.count() > 1 && c.at(1).truthy();
        if (!again && area->id == gmpShown) {
            return done(c, 1);
        }
        const int slot = gmpSlot ^ 3;
        const AreaRecord::Choice* choice = nullptr;
        for (const auto& ch : area->choices) {
            if (ch.condition < 0 || (std::size_t(ch.condition) < conditionBits.size() &&
                                     conditionBits[std::size_t(ch.condition)])) {
                choice = &ch;
                break;
            }
        }
        if (choice) {
            const int number = choice->value & 0x7fff;
            char path[32], name[32];
            std::snprintf(path, sizeof path, "gmp/g%02d", number);
            std::snprintf(name, sizeof name, "gmp_img%d", slot);
            if (!scene.resource(name)) {
                if (const auto file = pack.find(path, ".xi"); !file.empty()) {
                    adventure56::Resource r;
                    r.kind = adventure56::Resource::Kind::Picture;
                    r.path = path;
                    r.picture = adventure56::loadTexture(pack, file);
                    scene.addResource(name, std::move(r), 0);
                }
            }
            if (choice->flag != 0) {
                std::snprintf(name, sizeof name, "gmp_anm_obj%d", slot);
                if (!scene.resource(name)) {
                    if (const auto folder = pack.find(path, ".xa"); !folder.empty()) {
                        adventure56::Resource r;
                        r.kind = adventure56::Resource::Kind::Animation;
                        r.path = path;
                        r.animation = adventure56::loadAnimationSet(pack, folder);
                        scene.addResource(name, std::move(r), 0);
                    }
                }
            }
        }
        const int past = flagId("\x89\xdf\x8b\x8e"); // 過去
        const char* icons = area->id == 0x76          ? "spr/@/icn_htl"
                            : past >= 0 && flag(past) ? "spr/@/icn_gmp_o"
                                                      : "spr/@/icn_gmp";
        char name[32];
        std::snprintf(name, sizeof name, "gmp_anm_icn%d", slot);
        if (const auto folder = pack.find(icons, ".xa"); !folder.empty()) {
            scene.removeResource(xq::hash(name));
            adventure56::Resource r;
            r.kind = adventure56::Resource::Kind::Animation;
            r.path = icons;
            r.animation = adventure56::loadAnimationSet(pack, folder);
            scene.addResource(name, std::move(r), 0);
        }
        gmpPrepared = area->id;
        return done(c, 1);
    });
    // 12102 (0x14ba40 -> 0x332338): the prepared area map comes up: the
    // lower screen's objects of kinds 5 and 11 go (0x2cc090) and the shown
    // slot's resources (0x29e22c); the slots swap; "gmp_img<slot>" (lower
    // screen, priority 1, kind 11) shows the picture, "gmp_anm_obj<slot>"
    // (priority 0x79) plays the set's "obj". Nothing prepared: 0.
    m.service(12102, [this](Call& c) {
        if (gmpPrepared < 0) {
            return done(c, 0);
        }
        releaseAreaMap(unsigned(gmpSlot));
        gmpShown = gmpPrepared;
        gmpPrepared = -1;
        gmpSlot ^= 3;
        char name[32];
        std::snprintf(name, sizeof name, "gmp_img%d", gmpSlot);
        if (adventure56::Resource* r = scene.resource(name); r && r->picture) {
            adventure56::Object& o = createObject(name, 1, 1, 11, {}, 0);
            o.picture = r->picture;
        }
        std::snprintf(name, sizeof name, "gmp_anm_obj%d", gmpSlot);
        if (adventure56::Resource* r = scene.resource(name); r && r->animation) {
            adventure56::Object& o = createObject(name, 1, 0x79, 11, {}, 0);
            setAnimationSet(o, xq::hash(name));
            playAnimation(o, Value::ofText("obj"), 0);
        }
        return done(c, 1);
    });
    // 12103 (0x152274): the shown slot (operand true) or the other one.
    m.service(12103, [this](Call& c) {
        const bool shown = c.count() > 0 && c.at(0).truthy();
        return done(c, shown ? gmpSlot : gmpSlot ^ 3);
    });
    // 15010 (0x160578 -> 0x2c1764): whether a flag of that name (lt5_def
    // 2001) or number is defined.
    m.service(15010, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const Value& v = c.at(0);
        int id = -1;
        if (v.isText()) {
            const auto it = defs.flagIds.find(v.text);
            id = it == defs.flagIds.end() ? -1 : it->second;
        } else {
            for (const auto& [name, n] : defs.flagIds) {
                if (n == v.toInt()) {
                    id = n;
                }
            }
        }
        return done(c, id >= 0 ? 1 : 0);
    });
    // 16310 (0x1625c8): whether the second place lies on the guide's way
    // from the first to the step's goal.
    m.service(16310, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        auto placeOf = [&](std::size_t i) {
            return c.at(i).isText() ? story.place(c.at(i).text) : story.place(c.integer(i));
        };
        const PlaceRecord* from = placeOf(0);
        const PlaceRecord* on = placeOf(1);
        return done(c, from && on && onGuideWay(*this, *from, *on) ? 1 : 0);
    });
    // 2710 (0x142244 -> 0x2c54cc): the resource cache's entries in use get
    // their idle count (+0xe) cleared; nothing to do here.
    m.service(2710, [](Call& c) { return done(c, 1); });
    // 11015 (0x164a18 -> 0x31d808): the sound output mode (0 mono, 1
    // stereo, 2 surround; 0x313fc8). Kept; the port mixes stereo.
    m.service(11015, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        soundOutputMode = c.integer(0);
        return done(c, 1);
    });
    // 12001 (0x14f53c -> 0x332100): a room's map model (2121 by name or
    // id) is queued for loading (0x26768c); 1 when it was. Rooms load
    // when 12000 asks for them here.
    m.service(12001, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const RoomRecord* r =
            c.at(0).isText() ? story.room(c.at(0).text) : story.room(c.integer(0));
        return done(c, r ? 1 : 0);
    });
    // Route dots and markers (5640 0x143064 -> 0x17748c, 5642 0x135bd0 ->
    // 0x2ca698):
    // an animation set object's dynamic piece list - 5640 starts it anew
    // (0x2aad2c) and adds, 5642 adds - a copy of the named cell at {x,
    // y}. The copy is rotated by the float left in s0, which for 5642 is y
    // (*not modelled*: the dots are drawn unrotated). Result: added.
    auto addDot = [this](Call& c, bool restart) {
        if (c.count() < 4) {
            return false;
        }
        const Value& v = c.at(0);
        adventure56::Object* o = scene.object(v.isText() ? xq::hash(v.text) : v.toUint());
        if (!o) {
            return done(c, 0);
        }
        if (!o->animation || o->content != 9) {
            return done(c, 0);
        }
        if (restart) {
            o->pieces.clear();
        }
        const std::uint32_t h = c.at(1).isText() ? xq::hash(c.at(1).text) : c.at(1).toUint();
        for (std::size_t i = 0; i < o->animation->cells.size(); ++i) {
            if (o->animation->cells[i].hash == h) {
                o->pieces.push_back({int(i), c.real(2), c.real(3)});
                return done(c, 1);
            }
        }
        return done(c, 0);
    };
    m.service(5640, [addDot](Call& c) { return addDot(c, true); });
    m.service(5642, [addDot](Call& c) { return addDot(c, false); });
    // 5641 (0x135ca8 -> 0x188804): the piece list is complete.
    m.service(5641, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const Value& v = c.at(0);
        const adventure56::Object* o = scene.object(v.isText() ? xq::hash(v.text) : v.toUint());
        return done(c, o && o->animation && o->content == 9 ? 1 : 0);
    });
    // 1207 (0x12a8ac -> 0x2cf154): the touch of that source (operand, 0)
    // counts as a tap so far - held under 15 frames and within 8 pixels of
    // where it was pressed (the source's own points) - while the loop's
    // input switch (2109) is off.
    m.service(1207, [this](Call& c) {
        const int source = c.count() == 0 ? 0 : c.integer(0);
        int dx = touchX - pressX, dy = touchY - pressY;
        if (source == 1) {
            dx = int(std::floor(padCursor.x + 0.5f)) - padCursor.pressX;
            dy = int(std::floor(padCursor.y + 0.5f)) - padCursor.pressY;
        }
        return done(c, touchFrom(source) && heldFrames < 15 && dx * dx + dy * dy < 64 ? 1 : 0);
    });
    // 1221 (0x1302cc -> 0x16a84c): the pad-driven touch cursor on {x, y,
    // width, height, bounds x, y, width, height, speed, start x, start y
    // [, ?]} (0x339d88: zero bounds turn it off, result 0); its point and
    // press point go to the start (-1, -1: the rectangle's centre;
    // 0x339928). 1220 (0x130448 -> 0x16a8c8): off, the touch back to source
    // 0 (0x2b6c30, 0x2b6d54).
    m.service(1221, [this](Call& c) {
        if (c.count() < 9) {
            return false;
        }
        auto& p = padCursor;
        if (c.integer(6) == 0 || c.integer(7) == 0) {
            p.on = false;
            return done(c, 0);
        }
        p.on = true;
        p.absolute = false;
        p.x0 = c.integer(0);
        p.y0 = c.integer(1);
        p.x1 = p.x0 + c.integer(2);
        p.y1 = p.y0 + c.integer(3);
        p.bx = c.integer(4);
        p.by = c.integer(5);
        p.bw = c.integer(6);
        p.bh = c.integer(7);
        p.speed = c.real(8);
        int x = c.count() > 9 ? c.integer(9) : -1;
        int y = c.count() > 10 ? c.integer(10) : -1;
        if (x == -1 && y == -1) {
            x = (p.x0 + p.x1) / 2;
            y = (p.y0 + p.y1) / 2;
        }
        p.x = float(x);
        p.y = float(y);
        p.pressX = x;
        p.pressY = y;
        return done(c, 1);
    });
    // 1222 (0x1347a4 -> 0x16a914 -> 0x339bbc) {x, y, width, height, box x,
    // y, width, height}: the cursor on the rectangle, following the press
    // in the box absolutely (zero box: off, result 0).
    m.service(1222, [this](Call& c) {
        if (c.count() < 8) {
            return false;
        }
        auto& p = padCursor;
        if (c.integer(6) == 0 || c.integer(7) == 0) {
            p.on = false;
            return done(c, 0);
        }
        p.on = true;
        p.absolute = true;
        p.x0 = c.integer(0);
        p.y0 = c.integer(1);
        p.x1 = p.x0 + c.integer(2);
        p.y1 = p.y0 + c.integer(3);
        p.bx = c.integer(4);
        p.by = c.integer(5);
        p.bw = c.integer(6);
        p.bh = c.integer(7);
        return done(c, 1);
    });
    m.service(1220, [this](Call& c) {
        padCursor = {};
        touchSourceId = 0;
        return done(c, 1);
    });
    // The last pick (3022; the loop's +0x120 record, 0x346bc0): 3023
    // (0x129e90) the kind of what was hit (+0x131: a touch shape's kind, 1
    // for an object's content; 0 when nothing), 3024 (0x129d50) whether
    // anything was (+0x130 as a signed byte: 1, else -1).
    m.service(3023, [this](Call& c) { return done(c, scene.pick.hit ? scene.pick.kind : 0); });
    m.service(3024, [this](Call& c) { return done(c, scene.pick.hit ? 1 : -1); });
    // 5261 (0x137190 -> 0x352430): a 2D object's depth tween (5263) stops;
    // its depth goes into the register (0 and result 0 without the object).
    m.service(5261, [this](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        const Value& v = c.at(0);
        adventure56::Object* o = scene.object(v.isText() ? xq::hash(v.text) : v.toUint());
        if (o) {
            o->depth.set(o->depth.value());
        }
        if (Value* r = c.reg(1)) {
            *r = Value::ofFloat(o ? o->depth[0] : 0.0f);
        }
        return done(c, o ? 1 : 0);
    });
    // A puzzle's saved state by name or id (2301; game +0x89a4): 16400
    // (0x1624e8 -> 0x33f54c) reads it (0 new, 1 seen, 2 solved; 0 for no
    // puzzle), 16401 (0x162548 -> 0x2d1fcc) sets it.
    auto puzzleOf = [this](const Call& c) {
        const Value& v = c.at(0);
        return v.isText() ? defs.puzzleByName(v.text) : defs.puzzle(v.toInt());
    };
    m.service(16400, [this, puzzleOf](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const PuzzleRecord* p = puzzleOf(c);
        const auto it = p ? progress.find(p->id) : progress.end();
        return done(c, it == progress.end() ? 0 : it->second.state & 3);
    });
    m.service(16401, [this, puzzleOf](Call& c) {
        if (c.count() < 2) {
            return false;
        }
        const PuzzleRecord* p = puzzleOf(c);
        if (!p) {
            return done(c, 0);
        }
        setPuzzleState(p->id, c.integer(1));
        return done(c, 1);
    });
    // 5249 (0x13850c -> 0x2ca238 / 0x2ca228): the offset (5247) tweens by
    // {x, y} from where it is now (a running offset tween stops first) over
    // frames [curve]; as 5244 does for the position.
    m.service(5249, [this](Call& c) {
        if (c.count() < 5) {
            return false;
        }
        const Value& v = c.at(0);
        adventure56::Object* o = scene.object(v.isText() ? xq::hash(v.text) : v.toUint());
        if (!o) {
            return false;
        }
        auto at = o->offset.value();
        o->offset.set(at);
        at[0] += c.real(1);
        at[1] += c.real(2);
        o->offset.start(at, c.real(3), c.integer(4));
        return done(c, 1);
    });
    // 5610 (0x1314e0 -> 0x340030): an animation-set object's animation has
    // played through (the play record's count +0x9c, else its end flag
    // +0xa8 bit 0); 0 for other content. Here the
    // played-through count (Object::passes; a play-once animation also
    // counts at its end).
    m.service(5610, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const Value& v = c.at(0);
        const adventure56::Object* o =
            scene.object(v.isText() ? xq::hash(v.text) : v.toUint());
        if (!o) {
            return false;
        }
        return done(c, o->animation && o->content == 9 && (o->finished || o->passes > 0) ? 1 : 0);
    });
    // 5101 / 6101 (0x136430 / 0x139eec -> 0x2ca404): a 2D / 3D object's
    // alpha tween (+0x94) stops; its alpha goes into the register (0 and
    // result 0 without the object).
    for (const bool solid : {false, true}) {
        m.service(solid ? 6101 : 5101, [this, solid](Call& c) {
            if (c.count() < 2) {
                return false;
            }
            const Value& v = c.at(0);
            adventure56::Object* o = scene.find(solid, v.isText() ? xq::hash(v.text) : v.toUint());
            if (o) {
                o->alpha.set(o->alpha.value());
            }
            if (Value* r = c.reg(1)) {
                *r = Value::ofFloat(o ? o->alpha[0] : 0.0f);
            }
            return done(c, o ? 1 : 0);
        });
    }
    // 15700 (0x15bcf8): a puzzle's id by name (0x33eb98) or by slot (its
    // place in 2301, 0x2bdcec), -1 for none; 15702 (0x160708): its record's text +0xc,
    // taken to be the shown title (*hypothesis*: not compared with the
    // console yet).
    m.service(15700, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const Value& v = c.at(0);
        const PuzzleRecord* p = nullptr;
        if (v.isText()) {
            p = defs.puzzleByName(v.text);
        } else if (v.toInt() >= 0 && std::size_t(v.toInt()) < defs.puzzles.size()) {
            p = &defs.puzzles[std::size_t(v.toInt())];
        }
        return done(c, p ? p->id : -1);
    });
    m.service(15702, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const Value& v = c.at(0);
        const PuzzleRecord* p = v.isText() ? defs.puzzleByName(v.text) : defs.puzzle(v.toInt());
        if (!p) {
            return false;
        }
        c.result = Value::ofText(p->title);
        return true;
    });
    // 16302 (0x14faa8 -> 0x2d25a4): a variable (name or number [, index])
    // goes up by one; result: set (0 when there is no such variable).
    m.service(16302, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const Value& v = c.at(0);
        const Definitions::Variable* var = nullptr;
        if (v.isText()) {
            const auto it = defs.variables.find(v.text);
            var = it == defs.variables.end() ? nullptr : &it->second;
        } else {
            const auto it = defs.variablesByOffset.find(v.toInt());
            var = it == defs.variablesByOffset.end() ? nullptr : &it->second;
        }
        if (!var) {
            return done(c, 0);
        }
        const int slot = c.count() > 1 ? c.integer(1) : 0;
        setVariable(*var, variable(*var, slot) + 1, slot);
        return done(c, 1);
    });
    // 15507 (0x160818): a place's exits (2146): the count; (place, n) the
    // n-th exit's place id (-1 for an event exit or none); (place, to,
    // out, out) the exit to that place's two values (+0xe, +0x10: 2146
    // fields 2 and 3; -1 and 0, result 0, without one).
    m.service(15507, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        auto placeOf = [&](std::size_t i) {
            return c.at(i).isText() ? story.place(c.at(i).text) : story.place(c.integer(i));
        };
        const PlaceRecord* p = placeOf(0);
        if (!p) {
            return done(c, 0);
        }
        if (c.count() < 2) {
            return done(c, int(p->exits.size()));
        }
        if (c.count() < 3) {
            const int n = c.integer(1);
            const PlaceRecord* to = n >= 0 && std::size_t(n) < p->exits.size()
                                        ? story.place(p->exits[std::size_t(n)].name)
                                        : nullptr;
            return done(c, to ? to->id : -1);
        }
        const PlaceRecord* to = placeOf(1);
        for (const auto& e : p->exits) {
            if (to && e.name == to->name) {
                if (Value* r = c.reg(2)) {
                    *r = Value::ofInt(std::int16_t(e.kind));
                }
                if (c.count() > 3) {
                    if (Value* r = c.reg(3)) {
                        *r = Value::ofInt(std::int8_t(e.extra));
                    }
                }
                return done(c, 1);
            }
        }
        if (Value* r = c.reg(2)) {
            *r = Value::ofInt(-1);
        }
        if (c.count() > 3) {
            if (Value* r = c.reg(3)) {
                *r = Value::ofInt(0);
            }
        }
        return done(c, 0);
    });
    // 12002 (0x14ba68 -> 0x2c18b8): a screen's room goes.
    m.service(12002, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const int s = c.integer(0);
        const bool had = s >= 0 && s < 5 && screenRooms[std::size_t(s)] >= 0;
        removeRoom(s);
        return done(c, had ? 1 : 0);
    });
    // 3207 (0x12cee8 -> 0x2ce78c): whether a text list of that name is
    // loaded.
    m.service(3207, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const Value& v = c.at(0);
        const adventure56::Resource* r = scene.resource(v.isText() ? xq::hash(v.text) : v.toUint());
        return done(c, r && r->text ? 1 : 0);
    });
    // Camera reads (screen, register): 4141 (0x13d714) the field of view
    // (its tween +0x44c stops first), 4160 (0x142134) the near clip
    // (+0x330), 4180 (0x13d33c) the far clip (+0x32c); 0 without the
    // screen's camera. 4204 / 4224 (0x157e08 / 0x160480 -> view vtable
    // +0xb4 / +0xe4): the stereo distance / strength (4202 / 4222) still
    // changing - the events wait on them.
    auto screenCamera = [this](const Call& c) -> adventure56::Camera* {
        const int s = c.integer(0);
        return s >= 0 && s < adventure56::Scene::kScreens ? &scene.cameras[s] : nullptr;
    };
    m.service(4141, [screenCamera](Call& c) {
        auto* cam = c.count() > 1 ? screenCamera(c) : nullptr;
        if (!cam) {
            return false;
        }
        cam->fov.set(cam->fov.value());
        if (Value* r = c.reg(1)) {
            *r = Value::ofFloat(cam->fov[0]);
        }
        return done(c, 1);
    });
    for (const bool far : {false, true}) {
        m.service(far ? 4180 : 4160, [screenCamera, far](Call& c) {
            auto* cam = c.count() > 1 ? screenCamera(c) : nullptr;
            if (!cam) {
                return false;
            }
            if (Value* r = c.reg(1)) {
                *r = Value::ofFloat(far ? cam->farZ : cam->nearZ);
            }
            return done(c, 1);
        });
    }
    m.service(4204, [screenCamera](Call& c) {
        auto* cam = c.count() ? screenCamera(c) : nullptr;
        return done(c, cam && cam->stereoDistance.moving() ? 1 : 0);
    });
    m.service(4224, [screenCamera](Call& c) {
        auto* cam = c.count() ? screenCamera(c) : nullptr;
        return done(c, cam && cam->stereoStrength.moving() ? 1 : 0);
    });
    // The pad (game +0x33c, bits in Input's order, 0..13), while the loop's
    // input switch (2109) is off: 1100 (0x125fe0) not held, 1101 held
    // (+0xa4), 1102 released (+0xbc), 1103 newly pressed (+0xb4).
    auto padBit = [](const Call& c) {
        const int b = c.integer(0);
        return b >= 0 && b < 14 ? 1u << b : 0u;
    };
    m.service(1100, [this, padBit](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const auto b = padBit(c);
        return done(c, !objectTouchLocked && b && !(input.buttons & b) ? 1 : 0);
    });
    m.service(1101, [this, padBit](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        return done(c, !objectTouchLocked && (input.buttons & padBit(c)) ? 1 : 0);
    });
    m.service(1102, [this, padBit](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const auto b = padBit(c);
        return done(c,
                    !objectTouchLocked && (previousInput.buttons & b) && !(input.buttons & b) ? 1 : 0);
    });
    m.service(1103, [this, padBit](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const auto b = padBit(c);
        return done(c,
                    !objectTouchLocked && (input.buttons & b) && !(previousInput.buttons & b) ? 1 : 0);
    });
    // The circle pad: 1130 (0x125f68) x, 1131 (0x125f8c) y, 1132 / 1133
    // (0x125fb0 ..) always 0 (the second pad the console lacks).
    m.service(1130, [this](Call& c) { return doneReal(c, input.circleX); });
    m.service(1131, [this](Call& c) { return doneReal(c, input.circleY); });
    m.service(1132, [](Call& c) { return doneReal(c, 0.0f); });
    m.service(1133, [](Call& c) { return doneReal(c, 0.0f); });
    // A 3D object's shake (+0x35c): 6270 (0x129054 -> 0x2ab244) set {x, y,
    // z}, 6272 (0x12910c -> 0x2ab108) over frames [curve], 6274 (0x145204
    // -> 0x3526d8) still shaking (changing, or not zero; 0 while hidden).
    auto shakeOf = [this](const Call& c) -> adventure56::Tween<3>* {
        const Value& v = c.at(0);
        const std::uint32_t h = v.isText() ? xq::hash(v.text) : v.toUint();
        return scene.object3D(h) ? &shakes[h] : nullptr;
    };
    m.service(6270, [shakeOf](Call& c) {
        auto* s = c.count() >= 4 ? shakeOf(c) : nullptr;
        if (!s) {
            return false;
        }
        s->set({c.real(1), c.real(2), c.real(3)});
        return done(c, 1);
    });
    m.service(6272, [shakeOf](Call& c) {
        auto* s = c.count() >= 5 ? shakeOf(c) : nullptr;
        if (!s) {
            return false;
        }
        s->start({c.real(1), c.real(2), c.real(3)}, c.real(4), c.count() > 5 ? c.integer(5) : 0);
        return done(c, 1);
    });
    m.service(6274, [this, shakeOf](Call& c) {
        auto* s = c.count() ? shakeOf(c) : nullptr;
        if (!s) {
            return false;
        }
        const Value& v = c.at(0);
        const auto* o = scene.object3D(v.isText() ? xq::hash(v.text) : v.toUint());
        // 0x3526d8 answers 0 when +0x114 bit 0 (the shown switch) is set.
        const bool shaking = s->moving() || (*s)[0] != 0 || (*s)[1] != 0 || (*s)[2] != 0;
        return done(c, o && !o->visible && shaking ? 1 : 0);
    });
    // The same for 2D objects (+0x404, two values): 5280 (0x128010 ->
    // 0x3380f0) set {x, y}, 5282 (0x1280b4 -> 0x2cfeb0) over frames
    // [curve], 5284 (0x144504 -> 0x352538) still shaking.
    auto shake2DOf = [this](const Call& c) -> adventure56::Tween<3>* {
        const Value& v = c.at(0);
        const std::uint32_t h = v.isText() ? xq::hash(v.text) : v.toUint();
        return scene.object(h) ? &shakes2D[h] : nullptr;
    };
    m.service(5280, [shake2DOf](Call& c) {
        auto* s = c.count() >= 3 ? shake2DOf(c) : nullptr;
        if (!s) {
            return false;
        }
        s->set({c.real(1), c.real(2), 0});
        return done(c, 1);
    });
    m.service(5282, [shake2DOf](Call& c) {
        auto* s = c.count() >= 4 ? shake2DOf(c) : nullptr;
        if (!s) {
            return false;
        }
        s->start({c.real(1), c.real(2), 0}, c.real(3), c.count() > 4 ? c.integer(4) : 0);
        return done(c, 1);
    });
    m.service(5284, [this, shake2DOf](Call& c) {
        auto* s = c.count() ? shake2DOf(c) : nullptr;
        if (!s) {
            return false;
        }
        const Value& v = c.at(0);
        const auto* o = scene.object(v.isText() ? xq::hash(v.text) : v.toUint());
        const bool shaking = s->moving() || (*s)[0] != 0 || (*s)[1] != 0;
        return done(c, o && !o->visible && shaking ? 1 : 0);
    });
    // 5210 (0x1573b4 -> 0x352618): a 2D object's scale or its offset is
    // still changing (+0x1d0 / +0x1ec), as 5100 for the alpha.
    m.service(5210, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const Value& v = c.at(0);
        const adventure56::Object* o = scene.object(v.isText() ? xq::hash(v.text) : v.toUint());
        if (!o) {
            return false;
        }
        return done(c, o->scale.moving() || o->scaleOffset.moving() ? 1 : 0);
    });
    // 4402 (0x13364c -> 0x2cbb48): a world point (screen, x, y, z) seen
    // through the screen's camera into three registers: its place across
    // the screen and down it (each clamped, 0x2d1174, to [0, 1] or to the
    // optional ranges, times the width - 320 on the lower screen, else 400
    // - or the height 240) and its depth. *Hypothesis*: the place is taken
    // from the camera's projection as (ndc x + 1) / 2, (1 - ndc y) / 2 and
    // the depth as the distance along the view (0x2cbb48 not read through).
    m.service(4402, [this](Call& c) {
        if (c.count() < 7) {
            return false;
        }
        const int s = c.integer(0);
        if (s < 0 || s >= adventure56::Scene::kScreens) {
            return done(c, 0);
        }
        const auto p = projectToScreen(s, c.real(1), c.real(2), c.real(3));
        const float w = s == 1 ? 320.0f : 400.0f, h = 240.0f;
        const float u = p[0], vv = p[1], z = p[2];
        const float lo0 = c.count() > 7 ? c.real(7) : 0, hi0 = c.count() > 8 ? c.real(8) : 1;
        const float lo1 = c.count() > 9 ? c.real(9) : 0, hi1 = c.count() > 10 ? c.real(10) : 1;
        const float out[3] = {std::clamp(u, lo0, hi0) * w, std::clamp(vv, lo1, hi1) * h, z};
        for (std::size_t i = 0; i < 3; ++i) {
            if (Value* r = c.reg(4 + i)) {
                *r = Value::ofFloat(out[i]);
            }
        }
        return done(c, 1);
    });
    // The area map's places and routes (res/lt5_gmp.xb/g<area>.xs: 2801
    // {place, x, y, ?}, 2802 {from, to, x, y, ...}; game +0x1944c, switched
    // with the shown area). 16570 (0x164700): the count (no operands), the
    // n-th place's id (-1), or a place's spot into three registers {x, y,
    // a signed byte} (zeros and result 0 when there is none).
    m.service(16570, [this](Call& c) {
        const AreaPaths& a = areaPaths();
        if (c.count() == 0) {
            return done(c, int(a.spots.size()));
        }
        if (c.count() < 4) {
            const int n = c.integer(0);
            if (n < 0 || std::size_t(n) >= a.spots.size()) {
                return done(c, -1);
            }
            const PlaceRecord* p = story.place(a.spots[std::size_t(n)].place);
            return done(c, p ? p->id : -1);
        }
        const PlaceRecord* p =
            c.at(0).isText() ? story.place(c.at(0).text) : story.place(c.integer(0));
        for (const auto& s : a.spots) {
            if (p && s.place == p->name) {
                const int out[3] = {s.x, s.y, s.extra};
                for (std::size_t i = 0; i < 3; ++i) {
                    if (Value* r = c.reg(1 + i)) {
                        *r = Value::ofInt(out[i]);
                    }
                }
                return done(c, 1);
            }
        }
        for (std::size_t i = 0; i < 3; ++i) {
            if (Value* r = c.reg(1 + i)) {
                *r = Value::ofInt(0);
            }
        }
        return done(c, 0);
    });
    // 16571 (0x164050), by operand count: none, the route count; (from,
    // to) the route's point count; (n, from out, to out) the n-th route's
    // ends as place ids (-1); (from, to, k, x out, y out) the k-th inner
    // point counted from `from`; (from, to, t, x out, y out, heading out)
    // the point at the fraction t of the way from `from` (ends at t <= 0
    // and t >= 1; x and y rounded), heading atan2(dy, dx) along the
    // route, + pi wrapped to [-pi, pi) when walked backwards. The last form
    // leaves the result alone when it finds the route. The segment lengths
    // (route +0xc, total +0x14) are taken to be Euclidean (*hypothesis*:
    // the table's builder is not read yet).
    m.service(16571, [this](Call& c) {
        const AreaPaths& a = areaPaths();
        auto setInt = [&](std::size_t i, int v) {
            if (Value* r = c.reg(i)) {
                *r = Value::ofInt(v);
            }
        };
        auto placeOf = [&](std::size_t i) {
            return c.at(i).isText() ? story.place(c.at(i).text) : story.place(c.integer(i));
        };
        if (c.count() < 2) {
            return done(c, int(a.routes.size()));
        }
        if (c.count() >= 3 && c.count() < 5) {
            const int n = c.integer(0);
            if (n >= 0 && std::size_t(n) < a.routes.size()) {
                const auto& r = a.routes[std::size_t(n)];
                const PlaceRecord* from = story.place(r.from);
                const PlaceRecord* to = story.place(r.to);
                setInt(1, from ? from->id : -1);
                setInt(2, to ? to->id : -1);
                return done(c, 1);
            }
            setInt(1, -1);
            setInt(2, -1);
            return done(c, 0);
        }
        const PlaceRecord* from = placeOf(0);
        const PlaceRecord* to = placeOf(1);
        const AreaPaths::Route* route = nullptr;
        for (const auto& r : a.routes) {
            if (from && to &&
                ((r.from == from->name && r.to == to->name) ||
                 (r.from == to->name && r.to == from->name))) {
                route = &r;
                break;
            }
        }
        if (c.count() == 2) {
            return done(c, route ? int(route->points.size()) : 0);
        }
        const bool back = route && route->from != from->name;
        const int n = route ? int(route->points.size()) : 0;
        if (c.count() == 5) {
            int k = c.integer(2);
            if (back) {
                k = n - k - 3;
            }
            if (route && unsigned(k) < unsigned(n - 2)) {
                setInt(3, route->points[std::size_t(k) + 1][0]);
                setInt(4, route->points[std::size_t(k) + 1][1]);
                return done(c, 1);
            }
            setInt(3, 0);
            setInt(4, 0);
            return done(c, 0);
        }
        if (!route || n < 2) {
            setInt(3, 0);
            setInt(4, 0);
            if (Value* r = c.reg(5)) {
                *r = Value::ofFloat(0);
            }
            return done(c, 0);
        }
        const auto& pts = route->points;
        float t = c.real(2);
        if (back) {
            t = 1.0f - t;
        }
        int x, y, dx, dy;
        if (t <= 0.0f || t >= 1.0f) {
            const std::size_t s = t > 0.0f ? std::size_t(n - 2) : 0;
            const std::size_t e = t > 0.0f ? std::size_t(n - 1) : 0;
            x = pts[e][0];
            y = pts[e][1];
            dx = pts[s + 1][0] - pts[s][0];
            dy = pts[s + 1][1] - pts[s][1];
        } else {
            std::vector<float> lengths;
            float total = 0;
            for (std::size_t i = 1; i < pts.size(); ++i) {
                const float lx = float(pts[i][0] - pts[i - 1][0]);
                const float ly = float(pts[i][1] - pts[i - 1][1]);
                lengths.push_back(std::sqrt(lx * lx + ly * ly));
                total += lengths.back();
            }
            const float at = total * t;
            float run = 0;
            std::size_t k = 0;
            while (k + 1 < lengths.size() && run + lengths[k] < at) {
                run += lengths[k++];
            }
            const float f = (at - run) / lengths[k];
            dx = pts[k + 1][0] - pts[k][0];
            dy = pts[k + 1][1] - pts[k][1];
            x = int(std::floor(float(dx) * f + 0.5f)) + pts[k][0];
            y = int(std::floor(float(dy) * f + 0.5f)) + pts[k][1];
        }
        float heading = 0;
        if (dx != 0 || dy != 0) {
            heading = std::atan2(float(dy), float(dx));
        }
        if (back) {
            constexpr float pi = 3.14159265f;
            heading += pi;
            const float turns = heading * (1.0f / (2 * pi));
            if (!(turns > -0.5f && turns < 0.5f)) {
                heading -= std::floor(turns + 0.5f) * (2 * pi);
            }
        }
        setInt(3, x);
        setInt(4, y);
        if (Value* r = c.reg(5)) {
            *r = Value::ofFloat(heading);
        }
        return true;
    });
    // 11052 (0x161468 -> 0x2bc7a0): a stream on player 5 [fade, auto-start].
    m.service(11052, [this](Call& c) {
        if (c.count() == 0) {
            return false;
        }
        const int fade = c.count() > 2 ? c.integer(2) : 0;
        const bool autoStart = c.count() > 1 ? c.at(1).truthy() : false;
        return done(c, playStream(5, c.integer(0), fade, autoStart) ? 1 : 0);
    });
}

} // namespace picarat::adventure5
