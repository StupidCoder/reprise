// The rabbit's notebook (loop 6004, lt5CLoopMiniRabbitNote; vtable 0x4033b4:
// start 0x301e40, commands 0x301ea4): ten tricks a page with their state,
// and a trick's page with its explanation and picture once mastered. And
// the show puzzles (loop 6002, lt5CLoopMiniRabbitNazo; vtable 0x403324:
// start 0x3003bc, commands 0x300420, reset 0x301e04): a show's slots are
// filled with tricks picked from a board of the mastered ones; a step asks
// for one trick or any trick of a kind, and the show plays once the slots
// it needs are filled. Docs: .analysis/adventure56/docs/rabbit.md.
#include "story/a5_rabbit_parts.hpp"
#include "graphics/a56_text.hpp"
#include <algorithm>
#include <array>
#include <string>

namespace picarat::adventure5::rabbit {

namespace {

// ------------------------------------------------------------------ 6004

class RabbitNote final : public RabbitPart {
public:
    RabbitNote(Story& story, int id, std::vector<Value> values)
        : RabbitPart(story, id, std::move(values), "mini_rabbit_note") {}

    bool command(int number, xq::Call& c, std::size_t first) override {
        switch (number) {
        case 0:
            return page(c, first);
        case 1:
            return detail(c, first);
        default: // (the debug block's 10 reaches the hub, which has none)
            return RabbitPart::command(number, c, first);
        }
    }

private:
    // 0x302518 {page, rabbit, current}: the page's ten entries "rab_msg%d"
    // (the title) and "rab_icn%d" (the icon) for tricks (page - 1) x 10 + i.
    bool page(xq::Call& c, std::size_t first) {
        RabbitHub* h = hub();
        const std::uint32_t list = xq::hash("rbt_txt_a");
        if (!h || operands(c, first) < 2 || !game_.scene.resource(list)) {
            return false;
        }
        const int page = c.integer(first), kind = c.integer(first + 1);
        const int current = operands(c, first) > 2 ? c.integer(first + 2) : -1;
        for (int i = 0; i < 10; ++i) {
            Object* msg = object(format("rab_msg%d", i));
            Object* icon = object(format("rab_icn%d", i));
            if (!msg || !icon) {
                return false;
            }
            const int index = i + (page - 1) * 10;
            const Trick* t = h->trickAt(index);
            msg->visible = icon->visible = t != nullptr;
            if (!t) {
                continue;
            }
            const int state = trickState(game_, t->slot);
            if (state == 0) {
                // Not learned: the dim icon, untouchable, and text 1000 in
                // grey (0x177b18: 9/31 from the ramp at 0x3e72ac).
                play(icon, format("icon%02d_off", kind));
                icon->touchOn = false;
                constexpr float grey = 9.0f / 31.0f;
                msg->textRgba = std::array<float, 4>{grey, grey, grey, 1.0f};
                setText(msg, row(list, 1000));
            } else {
                play(icon, format(state == 2 ? "icon%02d" : "icon%02d_n", kind));
                icon->touchOn = true;
                msg->textColour = index == current ? 3 : 1; // 0x172a60
                msg->textRgba.reset();
                setText(msg, row(list, t->text));
            }
            if (fresh(game_, t->slot)) {
                play(icon, "new");
            }
        }
        return result(c, 1);
    }

    // 0x301f3c {index}: the trick's page: msg_title, msg_info, the
    // protagonist's words (its object's name at 0x367440)
    // and its picture pce_icn_b.
    bool detail(xq::Call& c, std::size_t first) {
        RabbitHub* h = hub();
        const Trick* t = h && operands(c, first) > 0 ? h->trickAt(c.integer(first)) : nullptr;
        const std::uint32_t list = xq::hash("rbt_txt_a");
        Object* title = object("msg_title");
        Object* info = object("msg_info");
        Object* comment = object(game_.codeText(0x367440));
        Object* picture = object("pce_icn_b");
        if (!t || !game_.scene.resource(list) || !title || !info || !comment || !picture) {
            return false;
        }
        const int state = trickState(game_, t->slot);
        if (state != 0) {
            setText(title, row(list, t->text));
            // The protagonist's words in the trick's variant, else the first.
            const std::string* words = row(list, t->text + 2, variant(game_, t->slot));
            setText(comment, words ? words : row(list, t->text + 2));
            setFresh(game_, t->slot, false);
        }
        if (state == 2) {
            setText(info, row(list, t->text + 1));
            play(picture, format("i%d_%d", t->id, rabbitKind()));
            picture->visible = true;
        } else {
            setText(info, row(list, 1001));
            picture->visible = false;
        }
        return result(c, 1);
    }
};

// ------------------------------------------------------------------ 6002

class RabbitNazo final : public RabbitPart {
public:
    RabbitNazo(Story& story, int id, std::vector<Value> values)
        : RabbitPart(story, id, std::move(values), "mini_rabbit_nazo") {
        slots_.fill(0);
    }

    bool command(int number, xq::Call& c, std::size_t first) override;

private:
    static constexpr int kSlots = 40; // +0x16c

    const Show* show() const {
        RabbitHub* h = hub();
        return h ? h->show(show_) : nullptr;
    }
    int slotValue(int i) const { return i >= 0 && i < kSlots ? slots_[std::size_t(i)] : 0; }
    void setSlot(int i, int v) {
        if (i >= 0 && i < kSlots) {
            slots_[std::size_t(i)] = v;
        }
    }
    // An operand that is the number -1 (not a name).
    static bool isNone(const Value& v) { return !v.isText() && v.toInt() == -1; }

    void gauge(int cursor);
    void board();
    void selectButtons(int mode);
    bool complete() const;
    void showButton(bool on);
    void cursorAt(int piece, bool on);
    bool pieces(xq::Call& c, std::size_t first);
    bool prepare(xq::Call& c, std::size_t first);
    bool move(xq::Call& c, std::size_t first);
    bool decide(xq::Call& c, std::size_t first);
    bool remove(xq::Call& c, std::size_t first);
    bool judge();
    bool reset();
    bool info(xq::Call& c, std::size_t first);
    bool showAt(xq::Call& c, std::size_t first);
    bool answer();

    std::array<int, kSlots> slots_{}; // the tricks in the show's slots (-1 blocked)
    int show_ = -1;                   // +0x20c
};

// 0x2717e8: the gauge "mrg_gauge" of the show's slots, 46 apart ("piece",
// "piece_n" blocked), each filled one with its trick's icon (the trick's
// board piece no longer touchable); at (180, 193), moved by the cursor.
void RabbitNazo::gauge(int cursor) {
    const Show* s = show();
    Object* g = object("mrg_gauge");
    if (!s || !g) {
        return;
    }
    beginCells(*g);
    for (int i = 0; i < s->slots; ++i) {
        const int v = slotValue(i);
        addCell(*g, v != -1 ? "piece" : "piece_n", float(i * 46), 0);
        if (v > 0) {
            addCell(*g, format("i%d_%d", v, rabbitKind()), float(i * 46 + 5), 5);
            if (Object* p = object(format("piece%d", v))) {
                p->touchOn = false;
            }
        }
    }
    g->alpha.set({1});
    // (200 + floor(-19.5), 213 + floor(-20): 0x2bd8b8 is floor.)
    g->position.set({180, 193, g->position.value()[2]});
    g->offset.set({float(-46 * cursor), 0, 0});
    g->depth.set({4});
}

// 0x271560: the board "mrg_piece" (at (54, 21)): a trick's icon in a cell
// of 7 x 30 pixels for each trick whose piece exists and is in no slot.
void RabbitNazo::board() {
    const Show* s = show();
    RabbitHub* h = hub();
    Object* b = object("mrg_piece");
    if (!s || !h || !b) {
        return;
    }
    beginCells(*b);
    for (int u = 0; u < h->trickCount(); ++u) {
        bool free = true;
        for (int k = 0; k < s->slots; ++k) {
            if (slotValue(k) == u + 1 || !object(format("piece%d", u + 1))) {
                free = false;
            }
        }
        if (free) {
            addCell(*b, format("i%d_%d", u + 1, rabbitKind()), float(u % 7 * 30), float(u / 7 * 30));
        }
    }
    b->alpha.set({1});
    b->position.set({54, 21, b->position.value()[2]});
}

// 0x271314: the decide and remove buttons: 1 decide (a free slot), 0
// remove (a filled one), -1 neither. A button's values 4 / 5 are its
// normal and touched animations.
void RabbitNazo::selectButtons(int mode) {
    Object* decide = object("slct_decide");
    Object* remove = object("slct_remove");
    if (!decide || !remove) {
        return;
    }
    const char* d = mode == 1 ? "ok_off" : "ok_n";
    const char* dTouched = mode == 1 ? "ok_on" : "ok_n";
    const char* r = mode == 0 ? "out_off" : "out_n";
    const char* rTouched = mode == 0 ? "out_on" : "out_n";
    auto set = [this](Object* o, const char* normal, const char* touched, bool on) {
        play(o, normal);
        game_.setObjectValue(*o, 4, Value::ofText(normal));
        game_.setObjectValue(*o, 5, Value::ofText(touched));
        o->touchOn = on;
    };
    set(decide, d, dTouched, mode == 1);
    set(remove, r, rTouched, mode == 0);
}

// 0x271168: the slots filled (not free, not blocked) are as many as the
// show needs.
bool RabbitNazo::complete() const {
    const Show* s = show();
    if (!s) {
        return false;
    }
    int filled = 0;
    for (int i = 0; i < s->slots; ++i) {
        if (slotValue(i) != 0 && slotValue(i) != -1) {
            ++filled;
        }
    }
    return filled == s->needed;
}

// 0x271b0c: the show button "btn_show" ("start_off" touchable, else
// "start_n").
void RabbitNazo::showButton(bool on) {
    Object* o = object("btn_show");
    if (!o) {
        return;
    }
    const char* name = on ? "start_off" : "start_n";
    play(o, name);
    game_.setObjectValue(*o, 4, Value::ofText(name));
    game_.setObjectValue(*o, 5, Value::ofText(name));
    o->touchOn = on;
}

// 0x2711e0: the cursor "slct_cur" on a board cell (shown, priority 0x79)
// or left there hidden.
void RabbitNazo::cursorAt(int piece, bool on) {
    Object* o = object("slct_cur");
    if (!o) {
        return;
    }
    const int k = piece - 1;
    o->position.set({float(54 + k % 7 * 30 + 15), float(21 + k / 7 * 30 + 15), o->position.value()[2]});
    o->alpha.set({on ? 1.0f : 0.0f});
    if (on) {
        game_.scene.setPriority(*o, 0x79);
    }
}

// 10 (0x300e6c) {sound, all}: a touch piece "piece<id>" on the lower screen
// (priority 0x6f, kind 7) over each mastered trick's board cell (every
// trick with `all`), handlers Piece_Normal / Piece_Touch; values {name, 1,
// 0x6f, 7, "id", "id", sound, -1, id, text row}. Result: the show's slots.
bool RabbitNazo::pieces(xq::Call& c, std::size_t first) {
    RabbitHub* h = hub();
    const Show* s = show();
    if (!h || !s || operands(c, first) < 2) {
        return false;
    }
    c.result = Value::ofInt(s->slots);
    const bool all = c.at(first + 1).truthy();
    for (int u = 0; u < h->trickCount(); ++u) {
        const Trick* t = h->trickAt(u);
        if (!all && trickState(game_, t->slot) != 2) {
            continue;
        }
        const std::string name = format("piece%d", t->id);
        const std::string id = format("%d", t->id);
        const std::vector<Value> values{Value::ofText(name), Value::ofInt(1), Value::ofInt(0x6f),
                                        Value::ofInt(7), Value::ofText(id), Value::ofText(id),
                                        Value::ofInt(c.integer(first)), Value::ofInt(-1),
                                        Value::ofInt(t->id), Value::ofInt(t->text)};
        Object& o = game_.createObject(name, 1, 0x6f, 7, values);
        o.position.set({0, 0, o.position.value()[2]});
        o.areas.push_back({1, float(54 + u % 7 * 30), float(21 + u / 7 * 30), 30, 30});
        const std::array<std::string, 2> handlers{"Piece_Normal", "Piece_Touch"};
        game_.setHandlers(o, 1, handlers);
    }
    board();
    return true;
}

// 20 (0x300b5c) {slot set, icon set, fresh}: the slots cleared (fresh;
// the steps without a trick blocked), the gauge, and two hidden pictures
// of the top screen: "g_icon" (the icon set, at (200, 213)) and "g_cap"
// (the slot set, at (338, 129)).
bool RabbitNazo::prepare(xq::Call& c, std::size_t first) {
    if (operands(c, first) < 3) {
        return false;
    }
    if (c.at(first + 2).truthy()) {
        slots_.fill(0);
        if (const Show* s = show()) {
            for (const auto& st : s->steps) {
                if (st.trick == -1) {
                    setSlot(st.slot, -1);
                }
            }
        }
    }
    gauge(0);
    auto make = [this, &c](const char* name, int marker, std::size_t set, float x, float y) {
        const std::vector<Value> values{Value::ofText(name), Value::ofInt(marker), Value::ofInt(0x7a),
                                        Value::ofInt(7)};
        Object& o = game_.createObject(name, 0, 0x7a, 7, values);
        const std::string* res = c.text(set);
        game_.setAnimationSet(o, xq::hash(res ? *res : std::string()));
        o.position.set({x, y, o.position.value()[2]});
        o.drawn = false;
        o.touchOn = false;
    };
    make("g_icon", 0, first + 1, 200, 213);
    make("g_cap", 1, first, 338, 129);
    return result(c, 1);
}

// 30 (0x301560) {cursor, arrow ("slct_lft" / "slct_rgt", -1 none),
// -> the slot's piece}: the cursor moves a slot (the gauge slides over 4
// frames; an arrow at the end goes off), then the decide / remove buttons
// and the board cursor by the slot. Result 1 when the slot holds a trick.
bool RabbitNazo::move(xq::Call& c, std::size_t first) {
    const Show* s = show();
    if (!s || operands(c, first) < 3) {
        return false;
    }
    int cursor = c.integer(first);
    c.result = Value::ofInt(0);
    Object* left = object("slct_lft");
    Object* right = object("slct_rgt");
    if (!left || !right) {
        return false;
    }
    const Value& arrow = c.at(first + 1);
    if (!isNone(arrow)) {
        const bool toRight = arrow.isText() && arrow.text == "slct_rgt";
        Object* end = nullptr;
        if (toRight) {
            if (++cursor > s->slots - 1) {
                cursor = s->slots - 1;
                end = right;
            }
        } else if (--cursor < 0) {
            cursor = 0;
            end = left;
        }
        if (end) {
            end->touchOn = false;
            play(end, end == right ? "btn_R_off" : "btn_L_off");
        } else if (Object* g = object("mrg_gauge")) {
            g->offset.start({float(-46 * cursor), 0, 0}, 4, 0);
        }
    }
    Object* cur = object("slct_cur");
    const int v = slotValue(cursor);
    if (v < 1) {
        if (cur) {
            cur->position.set({0, 0, cur->position.value()[2]});
            cur->alpha.set({0});
        }
        put(c, first + 2, Value::ofInt(-1));
        selectButtons(-1);
    } else {
        put(c, first + 2, Value::ofText(format("piece%d", v)));
        selectButtons(0);
        c.result = Value::ofInt(1);
        cursorAt(v, true);
    }
    if (Object* b = object("btn_show"); b && b->handlerState == 0) {
        showButton(complete());
    }
    put(c, first, Value::ofInt(cursor));
    if (cursor == 0) {
        left->touchOn = false;
    }
    if (cursor == s->slots - 1) {
        right->touchOn = false;
    }
    return true;
}

// 31 (0x301180) {slot, piece name (-1 none), trick}: the trick goes into
// the slot (a free one, or in place of another trick, whose piece is
// touchable again). Result 1 when it went in.
bool RabbitNazo::decide(xq::Call& c, std::size_t first) {
    if (operands(c, first) < 3) {
        return false;
    }
    const int i = c.integer(first);
    const int trick = c.integer(first + 2);
    const int v = slotValue(i);
    const Value& piece = c.at(first + 1);
    int mode;
    if (v == 0) {
        mode = 1; // free
    } else if (piece.isText() && xq::hash(format("piece%d", v)) == xq::hash(piece.text)) {
        mode = 0; // the same trick
    } else {
        mode = -1; // another
    }
    if (mode == 1 || (mode == -1 && !isNone(piece))) {
        if (mode == -1) {
            if (Object* old = object(format("piece%d", v))) {
                old->touchOn = true;
            }
        }
        setSlot(i, trick);
        if (Object* p = object(format("piece%d", trick))) {
            p->touchOn = false;
        }
        c.result = Value::ofInt(1);
        mode = 0;
    }
    selectButtons(mode);
    showButton(complete());
    gauge(i);
    board();
    cursorAt(trick, true);
    return true;
}

// 32 (0x3013d0) {slot, trick}: the slot's trick comes out (its piece
// touchable again, the flying icon hidden).
bool RabbitNazo::remove(xq::Call& c, std::size_t first) {
    if (operands(c, first) < 2) {
        return false;
    }
    const int i = c.integer(first);
    if (i < 0 || slotValue(i) <= 0) {
        return false;
    }
    if (!object(format("piece%d", slotValue(i)))) {
        return false;
    }
    if (Object* icon = object("g_icon")) {
        icon->drawn = false;
    }
    if (Object* p = object(format("piece%d", slotValue(i)))) {
        p->touchOn = true;
    }
    setSlot(i, 0);
    c.result = Value::ofInt(1);
    selectButtons(1);
    showButton(complete());
    gauge(i);
    board();
    cursorAt(c.integer(first + 1), true);
    return true;
}

// 40 (0x2ffec8): the show judged for the Show loop: per slot its trick and
// mark (a step asking for a trick: 1 that trick, or any of the kind
// 100..130 asked for; else -1 and the show fails; a step asking nothing:
// 0).
bool RabbitNazo::judge() {
    RabbitHub* h = hub();
    const Show* s = show();
    if (!h || !s) {
        return false;
    }
    std::vector<RabbitHub::Mark> marks(std::size_t(std::max(0, s->slots)));
    for (int i = 0; i < s->slots; ++i) {
        marks[std::size_t(i)].trick = slotValue(i);
    }
    bool passed = true;
    for (const auto& st : s->steps) {
        if (st.slot < 0 || st.slot >= s->slots) {
            continue;
        }
        auto& m = marks[std::size_t(st.slot)];
        const Trick* t = h->trick(slotValue(st.slot));
        if (st.trick == -1) {
            m.mark = 0;
        } else if (t && (st.trick < 100 ? st.trick == t->id : st.trick == t->kind)) {
            m.mark = 1;
        } else {
            m.mark = -1;
            passed = false;
        }
    }
    h->setJudgement(passed, std::move(marks));
    return true;
}

// 41 (0x30002c): the slots emptied (blocked ones stay), every piece
// touchable, the pictures and texts gone, the show button off.
bool RabbitNazo::reset() {
    for (int& v : slots_) {
        if (v != -1) {
            v = 0;
        }
    }
    if (Object* cur = object("slct_cur")) {
        cur->alpha.set({0});
        cur->position.set({0, 0, cur->position.value()[2]});
    }
    if (RabbitHub* h = hub()) {
        for (int u = 0; u < h->trickCount(); ++u) {
            if (Object* p = object(format("piece%d", h->trickAt(u)->id))) {
                p->touchOn = true;
            }
        }
    }
    for (const char* name : {"g_cap", "g_icon", "piece_info"}) {
        if (Object* o = object(name)) {
            o->drawn = false;
        }
    }
    const std::string empty;
    setText(object("inf_top"), &empty);
    setText(object("inf_btm"), &empty);
    showButton(false);
    gauge(0);
    board();
    return true;
}

// 42 (0x3018b0) {trick, top text, bottom text, show}: the trick's window
// "piece_info" (its frame and icon) and caption "g_cap", or (show 0) both
// hidden with the cursor; then the two texts.
bool RabbitNazo::info(xq::Call& c, std::size_t first) {
    if (operands(c, first) < 4) {
        return false;
    }
    const int trick = c.integer(first);
    if (!c.at(first + 3).truthy()) {
        if (Object* o = object("piece_info")) {
            o->drawn = false;
        }
        if (Object* cur = object("slct_cur")) {
            cur->alpha.set({0});
            cur->position.set({0, 0, cur->position.value()[2]});
        }
        if (Object* cap = object("g_cap")) {
            cap->drawn = false;
        }
    } else if (object(format("piece%d", trick))) {
        cursorAt(trick, true);
        const int kind = rabbitKind();
        if (Object* o = object("piece_info")) {
            beginCells(*o);
            addCell(*o, "info_win_lft", 8, 123);
            addCell(*o, "info_win_rgt", 248, 123);
            addCell(*o, format("i%d_%d", trick, kind), 15, 138);
            o->position.set({0, 0, o->position.value()[2]});
            o->drawn = true;
        }
        if (Object* cap = object("g_cap")) {
            play(cap, format("cap%02d_%d", trick, kind));
            cap->drawn = true;
        }
    }
    const std::string* top = c.text(first + 1);
    const std::string* bottom = c.text(first + 2);
    if (Object* o = object("inf_top")) {
        setText(o, top);
    }
    if (Object* o = object("inf_btm")) {
        setText(o, bottom);
    }
    return result(c, 1);
}

// 45 (0x300828) {cursor, -> the slot's piece}: the gauge and board at the
// cursor, the cursor on the slot's trick (or hidden), the arrows.
bool RabbitNazo::showAt(xq::Call& c, std::size_t first) {
    const Show* s = show();
    if (!s || operands(c, first) < 2) {
        return false;
    }
    const int cursor = c.integer(first);
    gauge(cursor);
    board();
    const int v = slotValue(cursor);
    if (v < 1) {
        if (Object* cur = object("slct_cur")) {
            cur->position.set({0, 0, cur->position.value()[2]});
            cur->alpha.set({0});
        }
        selectButtons(-1);
        put(c, first + 1, Value::ofInt(-1));
    } else {
        selectButtons(0);
        cursorAt(v, true);
    }
    if (Object* b = object("btn_show"); b && b->handlerState == 0) {
        showButton(complete());
    }
    Object* left = object("slct_lft");
    Object* right = object("slct_rgt");
    if (left && right) {
        left->touchOn = right->touchOn = true;
        if (cursor == s->slots - 1) {
            right->touchOn = false;
            play(right, "btn_R_off");
        }
        if (cursor == 0) {
            left->touchOn = false;
            play(left, "btn_L_off");
        }
    }
    return true;
}

// 50 (0x300a60): the answer: each step's trick in its slot (a kind asked
// for gives that kind's seventh trick: 100 -> 7, 110 -> 14, 120 -> 21,
// 130 -> 28).
bool RabbitNazo::answer() {
    const Show* s = show();
    if (!s) {
        return false;
    }
    for (const auto& st : s->steps) {
        if (st.trick == -1) {
            continue;
        }
        if (st.trick < 100) {
            setSlot(st.slot, st.trick);
        } else if (st.trick == 100 || st.trick == 110 || st.trick == 120 || st.trick == 130) {
            setSlot(st.slot, (st.trick - 100) / 10 * 7 + 7);
        }
    }
    showButton(true);
    gauge(0);
    board();
    cursorAt(1, false);
    return true;
}

bool RabbitNazo::command(int number, xq::Call& c, std::size_t first) {
    const std::size_t n = operands(c, first);
    switch (number) {
    case 10:
        return pieces(c, first);
    case 11: // the pieces deleted
        if (RabbitHub* h = hub()) {
            for (int u = 0; u < h->trickCount(); ++u) {
                game_.scene.remove(xq::hash(format("piece%d", u + 1)));
            }
        }
        return true;
    case 20:
        return prepare(c, first);
    case 21: { // {slot}: -1 blocked, 1 a step's slot, else 0
        const Show* s = show();
        if (!s || n == 0) {
            return false;
        }
        int r = 0;
        for (const auto& st : s->steps) {
            if (st.slot == c.integer(first)) {
                r = st.second == -1 ? -1 : 1;
            }
        }
        return result(c, r);
    }
    case 30:
        return move(c, first);
    case 31:
        return decide(c, first);
    case 32:
        return remove(c, first);
    case 40:
        return judge();
    case 41:
        return reset();
    case 42:
        return info(c, first);
    case 43: { // {show (-1: the first)}: the show; result: one was set before
        if (n == 0) {
            return false;
        }
        c.result = Value::ofInt(show_ != -1 ? 1 : 0);
        if (c.integer(first) == -1) {
            show_ = 0;
            put(c, first, Value::ofInt(0));
        } else {
            show_ = c.integer(first);
        }
        return true;
    }
    case 44: // {slot}: its trick (0 free, -1 blocked)
        if (n == 0) {
            return false;
        }
        return result(c, slotValue(c.integer(first)));
    case 45:
        return showAt(c, first);
    case 50:
        return answer();
    default:
        return RabbitPart::command(number, c, first);
    }
}

} // namespace

std::unique_ptr<Loop> makeNote(Story& story, int id, std::vector<Value> values) {
    return std::make_unique<RabbitNote>(story, id, std::move(values));
}

std::unique_ptr<Loop> makeNazo(Story& story, int id, std::vector<Value> values) {
    return std::make_unique<RabbitNazo>(story, id, std::move(values));
}

} // namespace picarat::adventure5::rabbit
