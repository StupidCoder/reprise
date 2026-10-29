// The puppet show. See a4_puppet.hpp.
#include "minigames/a4_puppet.hpp"
#include "puzzles/a4_loop_kit.hpp"
#include "system/a4_definitions.hpp"
#include <cstdio>

namespace picarat::adventure4 {

namespace {
// Overlay 38 loads at 02105220; its texts.
constexpr std::uint32_t kOverlay = 0x02105220;
constexpr std::uint32_t kNames = 0x021085a4;      // 2, 3, 0: the plays' first scenes;
                                                  // 1 / 4: "(???)" / "(?)"
constexpr std::uint32_t kPlayVars = 0x02108730;   // ＜演目１演技＞, +0x10, +0x20
constexpr std::uint32_t kFinaleVars = 0x021087c0; // ＜演目１大団円＞, +0x14, +0x28
constexpr std::uint32_t kLearned = 0x02108760;    // ＜演技＞
constexpr std::uint32_t kSelection = 0x0210876c;  // 汎用選択
constexpr std::uint32_t kPicture = 0x0210878c;    // mini/ngg_%d%d.ci
constexpr std::uint32_t kButton = 0x021087fc;     // msg_%d
constexpr int kPerPage = 8;
// Effects (020da688): an action already used, an action picked.
constexpr int kUsedSound = 4, kPickSound = 3;
// The text colours of the choice buttons (020ae6d8): normal, picked, used.
constexpr int kNormal = 1, kPicked = 3, kUsed = 8;

std::string fillIn(const std::string& f, const std::string& a) {
    std::string out;
    for (std::size_t i = 0; i < f.size(); ++i) {
        if (f[i] == '%' && i + 1 < f.size() && f[i + 1] == 's') {
            out += a;
            ++i;
        } else if (f[i] == '%' && i + 1 < f.size() && f[i + 1] == '%') {
            out += '%';
            ++i;
        } else {
            out += f[i];
        }
    }
    return out;
}
} // namespace

PuppetLoop::PuppetLoop(Game& game, int id, std::span<const Value> values)
    : ScriptLoop(game, id, values) {
    // 02106464 (start): 02106078.
    loadPlays();
}

void PuppetLoop::loadPlays() {
    // 02106078: res/sub/lt4_sub_puppet.cb with handlers for 5100 (the
    // scene count, 021054bc), 5101 a scene {name, branches} (02105630),
    // 5102 a branch {action, next scene, right, value} (02105974), 5103
    // the scene's end (02105a04), 5110 the action count (02105a1c), 5111 an
    // action {item name} numbered in order (02105b78).
    const auto list = game_.assets.data("res/sub/lt4_sub_puppet.cb");
    auto integer = [&](const import::adventure4::DataCommand& cmd, unsigned i) {
        return i < cmd.count ? std::int32_t(list.operands[cmd.first + i].value) : 0;
    };
    auto text = [&](const import::adventure4::DataCommand& cmd, unsigned i) {
        return i < cmd.count ? list.text(list.operands[cmd.first + i]) : std::string();
    };
    for (const auto& cmd : list.commands) {
        switch (cmd.opcode) {
        case 5101:
            scenes_.push_back({text(cmd, 0), {}});
            break;
        case 5102:
            if (!scenes_.empty()) {
                scenes_.back().branches.push_back({std::int16_t(integer(cmd, 0)), text(cmd, 1),
                                                   integer(cmd, 2) != 0, integer(cmd, 3)});
            }
            break;
        case 5111:
            actions_.push_back(text(cmd, 0));
            break;
        default:
            break;
        }
    }
}

const PuppetLoop::Scene* PuppetLoop::scene(const std::string& name) const {
    // An unknown name ends the search on the last scene (02106818,
    // 02106dd0 keep the last one looked at).
    for (const Scene& s : scenes_) {
        if (s.name == name) {
            return &s;
        }
    }
    return scenes_.empty() ? nullptr : &scenes_.back();
}

const PuppetLoop::Branch* PuppetLoop::branchFor(const Scene& s, int action) const {
    // The first branch for the action; none: the last one (the scene's
    // "any other action").
    for (const Branch& b : s.branches) {
        if (b.action == action) {
            return &b;
        }
    }
    return s.branches.empty() ? nullptr : &s.branches.back();
}

std::string PuppetLoop::actionText(int action) const {
    // The action's item text (lt4_def 2021 operand 4, the item's +8).
    if (action < 0 || action >= int(actions_.size())) {
        return {};
    }
    const auto* r =
        definitions(game_.assets).find(2021, 1, Value::ofText(actions_[std::size_t(action)]));
    return r ? Definitions::text(*r, 4) : std::string();
}

std::string PuppetLoop::placeholder() const {
    // The blank in a line: "(?)" for language 2, else "(???)".
    const std::uint32_t at =
        game_.assets.codeWord(38, kOverlay, kNames + (game_.data.language == 2 ? 16 : 4));
    return game_.assets.codeText(38, kOverlay, at);
}

std::string PuppetLoop::playVariable() const {
    return game_.assets.codeText(38, kOverlay, kPlayVars + std::uint32_t(play_) * 0x10);
}

std::string PuppetLoop::finaleVariable() const {
    return game_.assets.codeText(38, kOverlay, kFinaleVars + std::uint32_t(play_) * 0x14);
}

void PuppetLoop::showPicture(int picture) {
    // 020cee30 (as 4200): the lower screen's picture of the play's scene.
    char path[40];
    std::snprintf(path, sizeof path, game_.assets.codeText(38, kOverlay, kPicture).c_str(), play_,
                  picture);
    scene_.resetRoom();
    if (game_.assets.exists(path)) {
        scene_.screens[0].background = &game_.assets.picture(path);
    }
}

void PuppetLoop::showPage(int page, int slot) {
    // 021080c0: the eight buttons "msg_0".."msg_7" (an object and a message
    // of that name) show a page of the learned actions; the picked one is
    // lit (with `slot` it becomes the picked one), actions used in an
    // earlier step greyed.
    const std::string f = game_.assets.codeText(38, kOverlay, kButton);
    for (int i = 0; i < kPerPage; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, f.c_str(), i);
        Message* m = scene_.message(name);
        Chara* o = scene_.find(name);
        if (o) {
            o->alpha.set(0);
            o->setSwitch(Switches::Master, false);
        }
        const int k = i + page * kPerPage;
        if (k < 0 || k >= int(learned_.size())) {
            continue;
        }
        const int action = learned_[std::size_t(k)];
        auto light = [&] {
            if (m) {
                m->textColour = kPicked;
            }
            if (o) {
                o->alpha.set(0x1000);
                o->setSwitch(Switches::Master, true);
            }
        };
        if (slot == -1) {
            if (action == chosen_ && page == page_) {
                light();
            } else if (m) {
                m->textColour = kNormal;
            }
        } else if (slot != i) {
            if (m) {
                m->textColour = kNormal;
            }
        } else {
            light();
            chosen_ = action;
            chosenSlot_ = i;
            page_ = page;
        }
        for (int s = 0; s < current_; ++s) {
            const Step& st = steps_[std::size_t(s)];
            if (action == st.action && st.slot != -1 && m) {
                m->textColour = kUsed;
            }
        }
        if (m) {
            scene_.setText(*m, actionText(action), true, false);
            m->switches.set(Switches::Master, true);
        }
        if (o) {
            o->setSwitch(Switches::Master, true);
        }
    }
}

bool PuppetLoop::command(gss::Call& c, int op) {
    auto arg = [&](std::size_t i) {
        return c.count() > i ? c.integer(i) : 0;
    };
    auto text = [&](std::size_t i) {
        return kit::text(c, i);
    };
    auto result = [&](int v) {
        c.result = Value::ofInt(v);
        return true;
    };
    GameData& data = game_.data;
    switch (op) {
    case 0: { // 02106658 {play, value}: the play's first scene
        if (c.count() < 3) {
            return false;
        }
        playValue_ = arg(2);
        const int play = arg(1);
        const int index = play == 0 ? 2 : play == 1 ? 3 : 0;
        if (play >= 0 && play <= 2) {
            at_ = game_.assets.codeText(
                38, kOverlay,
                game_.assets.codeWord(38, kOverlay, kNames + 4u * std::uint32_t(index)));
        }
        current_ = 0;
        steps_[0].scene = at_;
        steps_[0].right = true;
        play_ = play;
        return result(1);
    }
    case 1: // 02106918: the scene's block on thread 0
        callback(0, at_, {});
        return result(1);
    case 2: { // 02106818: on by the action in 汎用選択
        const int action = data.variable(game_.assets.codeText(38, kOverlay, kSelection));
        const Scene* s = scene(at_);
        if (!s) {
            return false;
        }
        const Branch* b = branchFor(*s, action);
        if (!b || b->next.empty()) {
            return false;
        }
        at_ = b->next;
        return result(1);
    }
    case 3: { // 02107268: one step back
        current_ = std::max(0, current_ - 1);
        at_ = step().scene;
        for (int i = 0; i < keptCount_; ++i) {
            const Kept& k = step().kept[std::size_t(i)];
            if (Chara* o = scene_.find(k.name)) {
                o->position.set({k.x << 12, k.y << 12});
                kit::play(*o, k.sequence);
                o->alpha.set(0x1000);
            }
        }
        showPicture(step().picture);
        picture_ = step().picture;
        chosen_ = -1;
        page_ = step().page;
        return result(1);
    }
    case 4: { // 021073f8 {success, failure}: every step the right one?
        if (c.count() < 3) {
            return false;
        }
        for (int i = 0; i <= current_; ++i) {
            if (!steps_[std::size_t(i)].right) {
                callback(0, text(2), {});
                return false;
            }
        }
        callback(0, text(1), {});
        return result(1);
    }
    case 5: { // 0210673c: the learned actions
        learned_.clear();
        const std::string flag = game_.assets.codeText(38, kOverlay, kLearned);
        for (int i = 1; i <= int(actions_.size()); ++i) {
            if (data.flag(flag, i)) {
                learned_.push_back(i - 1);
            }
        }
        return result(1);
    }
    case 6: { // 021069b0 {slot, line, page}: a button pressed
        if (c.count() < 4) {
            return false;
        }
        const int slot = arg(1), page = arg(3);
        const int k = slot + page * kPerPage;
        const bool none = slot == -1 || k < 0 || k >= int(learned_.size());
        if (none) {
            // Nothing picked: the step's line (and picked button) as it was
            // when the step was answered before.
            int showPageAt = 0;
            if (data.variable(playVariable(), current_ + 1) != 0) {
                for (const char* name : {"txt_a", "txt_b"}) {
                    if (Message* m = scene_.message(name)) {
                        scene_.setText(*m, step().text, true, false);
                    }
                }
                if (!pending_) {
                    showPage(step().page, step().slot);
                    showPageAt = step().page;
                } else {
                    showPage(page_, chosenSlot_);
                    showPageAt = page_;
                    if (Chara* ok = scene_.find("ok")) {
                        ok->setSwitch(Switches::Master, true);
                    }
                }
            }
            showPage(showPageAt, -1);
            return result(1);
        }
        const int action = learned_[std::size_t(k)];
        for (int s = 0; s < current_; ++s) {
            const Step& st = steps_[std::size_t(s)];
            if (action == st.action && st.slot != -1) {
                services_.effect(kUsedSound);
                pending_ = true;
                return result(1);
            }
        }
        const std::string line = fillIn(text(2), placeholder());
        for (const char* name : {"txt_a", "txt_b"}) {
            if (Message* m = scene_.message(name)) {
                scene_.setText(*m, line, true, false);
            }
        }
        step().text = line;
        showPage(page, slot);
        services_.effect(kPickSound);
        if (Chara* ok = scene_.find("ok")) {
            ok->setSwitch(Switches::Master, true);
        }
        return result(1);
    }
    case 7:  // 02106958 {page}
    case 13: // 02107598
        if (c.count() < 2) {
            return false;
        }
        showPage(arg(1), -1);
        return result(1);
    case 8: { // 02106dd0 {page, slot}: the pick taken - the next step
        if (c.count() < 3) {
            return false;
        }
        int action = -1;
        if (!pending_) {
            const int slot = arg(2);
            page_ = arg(1);
            if (slot == -1) {
                const int k = step().slot + page_ * kPerPage;
                action = k >= 0 && k < int(actions_.size()) ? k : -1;
            } else {
                const int k = slot + page_ * kPerPage;
                action = k >= 0 && k < int(learned_.size()) ? learned_[std::size_t(k)] : -1;
                step().slot = slot;
            }
        } else {
            const int k = chosenSlot_ + page_ * kPerPage;
            action = k >= 0 && k < int(learned_.size()) ? learned_[std::size_t(k)] : -1;
            step().slot = chosenSlot_;
            pending_ = false;
        }
        if (action < 0) {
            return false;
        }
        const Scene* s = scene(at_);
        if (!s) {
            return false;
        }
        const Branch* b = branchFor(*s, action);
        if (!b || b->next.empty()) {
            return false;
        }
        step().page = page_;
        step().action = action;
        if (current_ + 1 >= int(steps_.size())) {
            return false;
        }
        // The next record keeps what it held before (a step taken again
        // after going back finds its earlier answer there).
        ++current_;
        step().scene = b->next;
        step().right = b->right;
        step().picture = picture_;
        data.setVariable(game_.assets.codeText(38, kOverlay, kSelection), action);
        // The play's record: this step's action (+1); a later step that had
        // chosen the same action forgets it.
        const std::string var = playVariable();
        data.setVariable(var, action + 1, current_);
        for (int i = current_ + 1; i <= 10; ++i) {
            if (data.variable(var, i) == action + 1) {
                data.setVariable(var, 0, i);
                break;
            }
        }
        chosen_ = -1;
        for (int i = 0; i < keptCount_; ++i) {
            Kept& k = step().kept[std::size_t(i)];
            k = steps_[std::size_t(current_ - 1)].kept[std::size_t(i)];
            if (const Chara* o = scene_.find(k.name)) {
                k.x = o->position.value()[0] / 4096;
                k.y = o->position.value()[1] / 4096;
                const auto* seq = o->animation.sequence();
                k.sequence = seq ? seq->name : std::string();
            }
        }
        return result(1);
    }
    case 9: { // 02107a70 {line}: the finale's lines, one per call
        if (c.count() < 2) {
            return false;
        }
        const Scene* s = scene(at_);
        if (!s || s->branches.empty()) {
            return false;
        }
        const Branch* right = &s->branches.back();
        for (const Branch& b : s->branches) {
            if (b.right) {
                right = &b;
                break;
            }
        }
        const int v = data.variable(finaleVariable(), finaleStep_);
        const int action = v != 0 ? v - 1 : right->action;
        if (action < 0 || action >= int(actions_.size())) {
            return false;
        }
        if (Message* m = scene_.message("txt_a")) {
            scene_.setText(*m, fillIn(text(1), actionText(action)), false, true);
        }
        data.setVariable(game_.assets.codeText(38, kOverlay, kSelection), right->action);
        ++finaleStep_;
        return result(1);
    }
    case 10: { // 021074d0 {object}: kept with each step
        if (c.count() < 2 || keptCount_ >= 16) {
            return false;
        }
        Kept& k = step().kept[std::size_t(keptCount_)];
        k.name = text(1);
        if (const Chara* o = scene_.find(k.name)) {
            const auto* seq = o->animation.sequence();
            k.sequence = seq ? seq->name : std::string();
        }
        ++keptCount_;
        return result(1);
    }
    case 11: // 02107984 {object, x, y}: its place this step
        if (c.count() < 4) {
            return false;
        }
        for (int i = 0; i < keptCount_; ++i) {
            Kept& k = step().kept[std::size_t(i)];
            if (k.name == text(1)) {
                k.x = std::int16_t(arg(2));
                k.y = std::int16_t(arg(3));
            }
        }
        return result(1);
    case 12: // 02107da0: any kept object still moving
        for (int i = 0; i < keptCount_; ++i) {
            const Chara* o = scene_.find(step().kept[std::size_t(i)].name);
            if (o && kit::moving(*o)) {
                return result(1);
            }
        }
        return result(0);
    case 14: { // 021075f0 {page, pages}: the page buttons
        if (c.count() < 3) {
            return false;
        }
        const int page = arg(1), pages = arg(2);
        if (Chara* next = scene_.find("page_next")) {
            next->setSwitch(Switches::Master, pages >= 1 && page < pages);
        }
        if (Chara* prev = scene_.find("page_prev")) {
            prev->setSwitch(Switches::Master, page != 0 && pages != 0);
        }
        return result(1);
    }
    case 15: { // 021076dc {line}: the step's line, its blank still open
        if (c.count() < 2) {
            return false;
        }
        Message* a = scene_.message("txt_a");
        if (!a) {
            return false;
        }
        const std::string* f = scene_.text("txt", arg(1), 0, 1);
        const std::string line = fillIn(f ? *f : std::string(), placeholder());
        const int v = data.variable(playVariable(), current_ + 1);
        int answer = -1;
        if (v != 0) {
            // Answered before: that action is picked again.
            for (int i = 0; i < int(learned_.size()); ++i) {
                if (learned_[std::size_t(i)] == v - 1) {
                    chosen_ = v - 1;
                    chosenSlot_ = i % kPerPage;
                    page_ = i / kPerPage;
                    break;
                }
            }
            answer = page_;
            pending_ = true;
        }
        c.result = Value::ofInt(answer);
        scene_.setText(*a, line, false, true);
        if (Message* b = scene_.message("txt_b")) {
            scene_.setText(*b, line, true, false);
        }
        step().text = line;
        return true;
    }
    case 16: // 02107cec {picture}
        if (c.count() < 2) {
            return false;
        }
        picture_ = arg(1);
        step().picture = picture_;
        showPicture(picture_);
        return result(1);
    case 17: { // 02107e2c: at the play's first scene?
        const int index = play_ == 0 ? 2 : play_ == 1 ? 3 : play_ == 2 ? 0 : -1;
        const std::string first =
            index < 0
                ? std::string()
                : game_.assets.codeText(
                      38, kOverlay,
                      game_.assets.codeWord(38, kOverlay, kNames + 4u * std::uint32_t(index)));
        return result(index >= 0 && at_ == first ? 1 : 0);
    }
    case 18: { // 02107f10: the value of the branch that led here
        const Scene* s = scene(at_);
        if (!s || s->branches.empty()) {
            return false;
        }
        const Branch* found = &s->branches.back();
        for (const Branch& b : s->branches) {
            if (b.next == step().scene) {
                found = &b;
                break;
            }
        }
        return result(found->value);
    }
    case 19: { // 02107fd8: the finale replays the play's choices
        for (int i = 1; i <= 10; ++i) {
            data.setVariable(finaleVariable(), data.variable(playVariable(), i), i);
        }
        return result(1);
    }
    default:
        return ScriptLoop::command(c, op);
    }
}

} // namespace picarat::adventure4
